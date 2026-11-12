#include "io/AudioFile.h"

#include "core/Diagnostics.h"
#include "io/Codecs.h"

#if MUSIC_RENDER_HAS_LAME
 #include <lame/lame.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace mr
{

namespace
{

constexpr uint32_t oggSerial = 0x494e4954;   // 같은 곡이 같은 바이트가 되도록 스트림 번호를 고정한다

void prepareFile (const juce::File& file)
{
    const auto parent = file.getParentDirectory();
    if (! parent.isDirectory() && ! parent.createDirectory())
        throw Failure ("io", "cannot create directory " + parent.getFullPathName().toStdString());
    if (file.exists() && ! file.deleteFile())
        throw Failure ("io", "cannot overwrite " + file.getFullPathName().toStdString());
}

void checkWavBitDepth (int bitDepth)
{
    if (bitDepth != 16 && bitDepth != 24 && bitDepth != 32)
        throw Failure ("invalid-input", "bit depth must be 16, 24 or 32, got " + std::to_string (bitDepth));
}

// 버퍼의 채널 포인터 (start 부터)
std::vector<const float*> channelPointers (const juce::AudioBuffer<float>& buffer, int start)
{
    std::vector<const float*> out ((size_t) buffer.getNumChannels());
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        out[(size_t) c] = buffer.getReadPointer (c, start);
    return out;
}

juce::MemoryBlock toBlock (const std::vector<uint8_t>& bytes) { return juce::MemoryBlock (bytes.data(), bytes.size()); }

uint32_t readLE32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }

void appendLE32 (std::vector<uint8_t>& out, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        out.push_back ((uint8_t) (v >> (8 * i)));
}

// Vorbis 주석의 몸통: 공급자 문자열, 주석 수, 주석들 (Ogg 는 앞에 0x03 "vorbis", 뒤에 틀 비트가 붙는다)
std::vector<uint8_t> vorbisCommentBody (const std::string& vendor, const std::vector<std::string>& comments)
{
    std::vector<uint8_t> out;
    appendLE32 (out, (uint32_t) vendor.size());
    out.insert (out.end(), vendor.begin(), vendor.end());
    appendLE32 (out, (uint32_t) comments.size());
    for (const auto& c : comments)
    {
        appendLE32 (out, (uint32_t) c.size());
        out.insert (out.end(), c.begin(), c.end());
    }
    return out;
}

// 몸통에서 공급자와 주석을 읽는다. 잘못되면 false.
bool parseVorbisCommentBody (const uint8_t* p, size_t n, std::string& vendor, std::vector<std::string>& comments)
{
    if (n < 8)
        return false;
    const uint32_t vendorLength = readLE32 (p);
    if ((size_t) vendorLength + 8 > n)
        return false;
    vendor.assign ((const char*) p + 4, vendorLength);
    size_t pos = 4 + vendorLength;
    const uint32_t count = readLE32 (p + pos);
    pos += 4;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (pos + 4 > n)
            return false;
        const uint32_t length = readLE32 (p + pos);
        pos += 4;
        if (pos + length > n)
            return false;
        comments.emplace_back ((const char*) p + pos, length);
        pos += length;
    }
    return true;
}

// ---- Ogg ----

uint32_t oggCrc (const uint8_t* data, size_t n)
{
    static const auto table = []
    {
        std::array<uint32_t, 256> t {};
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t r = i << 24;
            for (int k = 0; k < 8; ++k)
                r = (r & 0x80000000u) != 0 ? (r << 1) ^ 0x04c11db7u : r << 1;
            t[i] = r;
        }
        return t;
    }();
    uint32_t crc = 0;
    for (size_t i = 0; i < n; ++i)
        crc = (crc << 8) ^ table[((crc >> 24) & 0xff) ^ data[i]];
    return crc;
}

struct OggPage
{
    uint8_t flags = 0;   // 1 이어지는 패킷, 2 처음, 4 끝
    uint64_t granule = 0;
    uint32_t serial = 0;
    uint32_t sequence = 0;
    std::vector<uint8_t> lacing;
    std::vector<uint8_t> body;
};

std::optional<std::vector<OggPage>> parseOggPages (const uint8_t* d, size_t n)
{
    std::vector<OggPage> pages;
    size_t pos = 0;
    while (pos < n)
    {
        if (pos + 27 > n || std::memcmp (d + pos, "OggS", 4) != 0 || d[pos + 4] != 0)
            return std::nullopt;
        OggPage p;
        p.flags = d[pos + 5];
        for (int i = 0; i < 8; ++i)
            p.granule |= (uint64_t) d[pos + 6 + i] << (8 * i);
        p.serial = readLE32 (d + pos + 14);
        p.sequence = readLE32 (d + pos + 18);
        const size_t segments = d[pos + 26];
        if (pos + 27 + segments > n)
            return std::nullopt;
        p.lacing.assign (d + pos + 27, d + pos + 27 + segments);
        size_t bodySize = 0;
        for (const auto l : p.lacing)
            bodySize += l;
        const size_t bodyStart = pos + 27 + segments;
        if (bodyStart + bodySize > n)
            return std::nullopt;
        p.body.assign (d + bodyStart, d + bodyStart + bodySize);
        pages.push_back (std::move (p));
        pos = bodyStart + bodySize;
    }
    return pages;
}

void appendOggPage (juce::MemoryOutputStream& out, const OggPage& p)
{
    std::vector<uint8_t> page { 'O', 'g', 'g', 'S', 0, p.flags };
    for (int i = 0; i < 8; ++i)
        page.push_back ((uint8_t) (p.granule >> (8 * i)));
    appendLE32 (page, p.serial);
    appendLE32 (page, p.sequence);
    appendLE32 (page, 0);
    page.push_back ((uint8_t) p.lacing.size());
    page.insert (page.end(), p.lacing.begin(), p.lacing.end());
    page.insert (page.end(), p.body.begin(), p.body.end());
    const uint32_t crc = oggCrc (page.data(), page.size());
    for (int i = 0; i < 4; ++i)
        page[(size_t) 22 + (size_t) i] = (uint8_t) (crc >> (8 * i));
    out.write (page.data(), page.size());
}

// 처음 세 패킷(식별, 주석, 설정), 그것이 든 쪽 수, 설정 패킷이 쪽의 끝에서 끝나는지
struct OggHeaders
{
    std::array<std::vector<uint8_t>, 3> packets;
    size_t pages = 0;
    bool endsPage = false;
};

std::optional<OggHeaders> oggHeaderPackets (const std::vector<OggPage>& pages)
{
    OggHeaders h;
    size_t complete = 0;
    for (size_t pi = 0; pi < pages.size(); ++pi)
    {
        const auto& p = pages[pi];
        size_t offset = 0;
        for (size_t li = 0; li < p.lacing.size(); ++li)
        {
            const size_t length = p.lacing[li];
            auto& packet = h.packets[complete];
            packet.insert (packet.end(), p.body.begin() + (std::ptrdiff_t) offset, p.body.begin() + (std::ptrdiff_t) (offset + length));
            offset += length;
            if (length < 255 && ++complete == 3)
            {
                h.pages = pi + 1;
                h.endsPage = li + 1 == p.lacing.size();
                return h;
            }
        }
    }
    return std::nullopt;
}

// 주석 패킷을 바꾸고 모든 쪽의 스트림 번호, 차례, CRC 를 다시 쓴다
juce::MemoryBlock rewriteOggHeaders (const juce::MemoryBlock& in, const std::vector<std::string>& comments)
{
    const auto pages = parseOggPages ((const uint8_t*) in.getData(), in.getSize());
    const auto headers = pages ? oggHeaderPackets (*pages) : std::nullopt;
    if (! headers || ! headers->endsPage)
        throw Failure ("io", "the Ogg Vorbis writer produced an unexpected stream");
    const auto& packets = headers->packets;
    const size_t headerPages = headers->pages;
    const auto& oldComment = packets[1];
    std::string vendor;
    std::vector<std::string> oldComments;
    if (oldComment.size() < 7 || oldComment[0] != 3 || std::memcmp (oldComment.data() + 1, "vorbis", 6) != 0
        || ! parseVorbisCommentBody (oldComment.data() + 7, oldComment.size() - 7, vendor, oldComments))
        throw Failure ("io", "the Ogg Vorbis writer produced an unexpected comment header");
    std::vector<uint8_t> comment { 3, 'v', 'o', 'r', 'b', 'i', 's' };
    const auto body = vorbisCommentBody (vendor, comments);
    comment.insert (comment.end(), body.begin(), body.end());
    comment.push_back (1);

    juce::MemoryOutputStream out;
    uint32_t sequence = 0;
    // 식별 패킷은 첫 쪽에 혼자 둔다
    OggPage first;
    first.flags = 2;
    first.serial = oggSerial;
    first.sequence = sequence++;
    for (size_t left = packets[0].size();; left -= 255)
    {
        first.lacing.push_back ((uint8_t) std::min<size_t> (left, 255));
        if (left < 255)
            break;
    }
    first.body = packets[0];
    appendOggPage (out, first);

    // 주석과 설정 패킷: 쪽마다 마디 값 255 개까지. 패킷이 끝나는 쪽은 granule 0, 아니면 -1
    std::vector<uint8_t> lacing, data;
    std::vector<bool> ends;
    for (const auto* packet : std::initializer_list<const std::vector<uint8_t>*> { &comment, &packets[2] })
    {
        for (size_t left = packet->size();; left -= 255)
        {
            lacing.push_back ((uint8_t) std::min<size_t> (left, 255));
            ends.push_back (left < 255);
            if (left < 255)
                break;
        }
        data.insert (data.end(), packet->begin(), packet->end());
    }
    bool continued = false;
    for (size_t li = 0, offset = 0; li < lacing.size();)
    {
        const size_t count = std::min<size_t> (255, lacing.size() - li);
        OggPage p;
        p.flags = continued ? 1 : 0;
        p.serial = oggSerial;
        p.sequence = sequence++;
        p.lacing.assign (lacing.begin() + (std::ptrdiff_t) li, lacing.begin() + (std::ptrdiff_t) (li + count));
        size_t bytes = 0;
        bool anyEnd = false;
        for (size_t k = li; k < li + count; ++k)
        {
            bytes += lacing[k];
            anyEnd = anyEnd || ends[k];
        }
        p.granule = anyEnd ? 0 : ~(uint64_t) 0;
        p.body.assign (data.begin() + (std::ptrdiff_t) offset, data.begin() + (std::ptrdiff_t) (offset + bytes));
        appendOggPage (out, p);
        continued = ! ends[li + count - 1];
        li += count;
        offset += bytes;
    }

    for (size_t i = headerPages; i < pages->size(); ++i)
    {
        auto p = (*pages)[i];
        p.serial = oggSerial;
        p.sequence = sequence++;
        appendOggPage (out, p);
    }
    return out.getMemoryBlock();
}

// ---- FLAC ----

struct FlacBlock
{
    uint8_t type = 0;
    std::vector<uint8_t> body;
};

std::optional<std::pair<std::vector<FlacBlock>, size_t>> parseFlacBlocks (const uint8_t* d, size_t n)
{
    if (n < 4 || std::memcmp (d, "fLaC", 4) != 0)
        return std::nullopt;
    std::vector<FlacBlock> blocks;
    size_t pos = 4;
    for (bool last = false; ! last;)
    {
        if (pos + 4 > n)
            return std::nullopt;
        last = (d[pos] & 0x80) != 0;
        const size_t length = ((size_t) d[pos + 1] << 16) | ((size_t) d[pos + 2] << 8) | d[pos + 3];
        if (pos + 4 + length > n)
            return std::nullopt;
        blocks.push_back ({ (uint8_t) (d[pos] & 0x7f), std::vector<uint8_t> (d + pos + 4, d + pos + 4 + length) });
        pos += 4 + length;
    }
    return std::make_pair (std::move (blocks), pos);
}

constexpr uint8_t flacVorbisComment = 4;

// Vorbis 주석 블록을 바꾸거나 STREAMINFO 뒤에 넣는다 (FLAC 에는 틀 비트가 없다)
juce::MemoryBlock rewriteFlacComments (const juce::MemoryBlock& in, const std::vector<std::string>& comments)
{
    const auto* d = (const uint8_t*) in.getData();
    auto parsed = parseFlacBlocks (d, in.getSize());
    if (! parsed || parsed->first.empty())
        throw Failure ("io", "the FLAC writer produced an unexpected stream");
    auto& [blocks, audioStart] = *parsed;
    std::string vendor = "InitialDAW";
    auto it = std::find_if (blocks.begin(), blocks.end(), [] (const FlacBlock& b) { return b.type == flacVorbisComment; });
    if (it != blocks.end())
    {
        std::vector<std::string> old;
        parseVorbisCommentBody (it->body.data(), it->body.size(), vendor, old);
    }
    else
        it = blocks.insert (blocks.begin() + 1, FlacBlock { flacVorbisComment, {} });
    it->body = vorbisCommentBody (vendor, comments);

    juce::MemoryOutputStream out;
    out.write ("fLaC", 4);
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        const auto& b = blocks[i];
        const uint8_t header[4] { (uint8_t) (b.type | (i + 1 == blocks.size() ? 0x80 : 0)), (uint8_t) (b.body.size() >> 16),
                                  (uint8_t) (b.body.size() >> 8), (uint8_t) b.body.size() };
        out.write (header, 4);
        out.write (b.body.data(), b.body.size());
    }
    out.write (d + audioStart, in.getSize() - audioStart);
    return out.getMemoryBlock();
}

// ---- MP3 ----

constexpr std::array<int, 14> mp3Bitrates { 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 };

#if MUSIC_RENDER_HAS_LAME
juce::MemoryBlock encodeMp3 (const juce::AudioBuffer<float>& buffer, int start, int numSamples, double sampleRate, int kbps,
                             const std::string& title)
{
    std::unique_ptr<lame_global_flags, int (*) (lame_global_flags*)> lame (lame_init(), &lame_close);
    if (lame == nullptr)
        throw Failure ("io", "cannot start the MP3 encoder");
    auto* l = lame.get();
    const int channels = buffer.getNumChannels();
    lame_set_num_channels (l, channels);
    lame_set_in_samplerate (l, (int) sampleRate);
    lame_set_out_samplerate (l, (int) sampleRate);
    lame_set_mode (l, channels == 1 ? MONO : JOINT_STEREO);
    lame_set_VBR (l, vbr_off);
    lame_set_brate (l, kbps);
    lame_set_quality (l, 2);
    lame_set_num_samples (l, (unsigned long) numSamples);
    lame_set_bWriteVbrTag (l, 1);              // 정보 프레임: 인코더 지연과 끝에 덧붙인 샘플 수
    lame_set_write_id3tag_automatic (l, 0);    // ID3v2 는 직접 앞에 쓴다
    if (! title.empty())
    {
        id3tag_init (l);
        id3tag_v2_4_UTF8_only (l);
        id3tag_set_textinfo_utf8 (l, "TIT2", title.c_str());
    }
    if (lame_init_params (l) < 0)
        throw Failure ("invalid-input", "the MP3 encoder rejected " + std::to_string (kbps) + " kbps at " + std::to_string ((int) sampleRate) + " Hz");

    juce::MemoryOutputStream out;
    if (! title.empty())
    {
        std::vector<unsigned char> tag (lame_get_id3v2_tag (l, nullptr, 0));
        if (! tag.empty() && lame_get_id3v2_tag (l, tag.data(), tag.size()) == tag.size())
            out.write (tag.data(), tag.size());
    }
    const auto audioStart = (size_t) out.getPosition();
    constexpr int chunk = 4096;
    std::vector<unsigned char> mp3 ((size_t) (chunk * 5 / 4 + 7200));
    for (int done = 0; done < numSamples; done += chunk)
    {
        const int count = std::min (chunk, numSamples - done);
        const int bytes = lame_encode_buffer_ieee_float (l, buffer.getReadPointer (0, start + done),
                                                         buffer.getReadPointer (std::min (1, channels - 1), start + done), count,
                                                         mp3.data(), (int) mp3.size());
        if (bytes < 0)
            throw Failure ("io", "the MP3 encoder failed (" + std::to_string (bytes) + ")");
        out.write (mp3.data(), (size_t) bytes);
    }
    const int tail = lame_encode_flush (l, mp3.data(), (int) mp3.size());
    if (tail < 0)
        throw Failure ("io", "the MP3 encoder failed while flushing (" + std::to_string (tail) + ")");
    out.write (mp3.data(), (size_t) tail);

    // 첫 프레임 자리에 비워 둔 정보 프레임을 채운다
    auto data = out.getMemoryBlock();
    std::vector<unsigned char> info (4096);
    const size_t infoSize = lame_get_lametag_frame (l, info.data(), info.size());
    if (infoSize > 0 && infoSize <= info.size() && audioStart + infoSize <= data.getSize())
        std::memcpy ((char*) data.getData() + audioStart, info.data(), infoSize);
    return data;
}
#endif

void writeBytes (const juce::File& file, const juce::MemoryBlock& data)
{
    prepareFile (file);
    if (! file.replaceWithData (data.getData(), data.getSize()))
        throw Failure ("io", "cannot write " + file.getFullPathName().toStdString());
}

} // namespace

std::optional<AudioFileType> audioFileTypeFor (const juce::File& file)
{
    const auto ext = file.getFileExtension().toLowerCase();
    if (ext == ".wav" || ext == ".wave")
        return AudioFileType::wav;
    if (ext == ".flac")
        return AudioFileType::flac;
    if (ext == ".ogg" || ext == ".oga")
        return AudioFileType::ogg;
    if (ext == ".mp3")
        return AudioFileType::mp3;
    return std::nullopt;
}

const char* audioFileTypeName (AudioFileType type)
{
    switch (type)
    {
        case AudioFileType::wav: return "wav";
        case AudioFileType::flac: return "flac";
        case AudioFileType::ogg: return "ogg";
        case AudioFileType::mp3: return "mp3";
    }
    return "wav";
}

int defaultQuality (AudioFileType type)
{
    return type == AudioFileType::ogg ? 6 : type == AudioFileType::mp3 ? 320 : 0;
}

bool mp3EncoderAvailable()
{
#if MUSIC_RENDER_HAS_LAME
    return true;
#else
    return false;
#endif
}

void checkAudioFileFormat (const AudioFileFormat& format, double sampleRate, int channels, bool loop)
{
    const int quality = format.quality >= 0 ? format.quality : defaultQuality (format.type);
    switch (format.type)
    {
        case AudioFileType::wav:
            checkWavBitDepth (format.bitDepth);
            break;
        case AudioFileType::flac:
            if (format.bitDepth != 16 && format.bitDepth != 24)
                throw Failure ("invalid-input", "FLAC bit depth must be 16 or 24, got " + std::to_string (format.bitDepth));
            break;
        case AudioFileType::ogg:
            if (quality < 0 || quality > 10)
                throw Failure ("invalid-input", "Ogg Vorbis quality must be 0 to 10, got " + std::to_string (quality));
            break;
        case AudioFileType::mp3:
            if (! mp3EncoderAvailable())
                throw Failure ("invalid-input", "this build has no MP3 encoder (./build.sh builds LAME); use .ogg, .flac or .wav");
            if (loop)
                throw Failure ("invalid-input", "MP3 cannot loop sample-accurately (the encoder adds silence at both ends); use .ogg, .flac or .wav");
            if (std::find (mp3Bitrates.begin(), mp3Bitrates.end(), quality) == mp3Bitrates.end())
                throw Failure ("invalid-input", "MP3 bitrate must be one of 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 kbps, got "
                                                    + std::to_string (quality));
            if (sampleRate != 32000.0 && sampleRate != 44100.0 && sampleRate != 48000.0)
                throw Failure ("invalid-input", "MP3 needs a sample rate of 32000, 44100 or 48000 Hz");
            if (channels < 1 || channels > 2)
                throw Failure ("invalid-input", "MP3 holds one or two channels");
            break;
    }
}

void writeAudioFile (const juce::File& file, const juce::AudioBuffer<float>& buffer, int start, int numSamples, double sampleRate,
                     const AudioFileFormat& format, const AudioFileTags& tags)
{
    if (start < 0 || numSamples < 0 || start + numSamples > buffer.getNumSamples())
        throw Failure ("invalid-input", "the samples to write are outside the buffer");
    const bool loop = tags.loopStart.has_value() && tags.loopEnd.has_value();
    if (loop && (*tags.loopStart < 0 || *tags.loopEnd <= *tags.loopStart || *tags.loopEnd > numSamples))
        throw Failure ("invalid-input", "the loop must lie inside the written samples");
    checkAudioFileFormat (format, sampleRate, buffer.getNumChannels(), loop);
    const int quality = format.quality >= 0 ? format.quality : defaultQuality (format.type);

    std::vector<std::string> comments;
    if (! tags.title.empty())
        comments.push_back ("TITLE=" + tags.title);
    if (loop)
    {
        comments.push_back ("LOOPSTART=" + std::to_string (*tags.loopStart));
        comments.push_back ("LOOPLENGTH=" + std::to_string (*tags.loopEnd - *tags.loopStart));
    }

    switch (format.type)
    {
        case AudioFileType::wav:
        {
            checkWavBitDepth (format.bitDepth);
            std::optional<codec::WavLoop> smpl;
            if (loop)
                smpl = codec::WavLoop { *tags.loopStart, *tags.loopEnd - 1 };   // smpl 청크: 정방향 무한 반복 하나, 끝 샘플을 포함한다
            const auto channels = channelPointers (buffer, start);
            writeBytes (file, toBlock (codec::encodeWav (channels.data(), buffer.getNumChannels(), numSamples, sampleRate, format.bitDepth, smpl)));
            return;
        }
        case AudioFileType::flac:
        {
            const auto channels = channelPointers (buffer, start);
            const auto bytes = codec::encodeFlac (channels.data(), buffer.getNumChannels(), numSamples, sampleRate, format.bitDepth, 5);
            if (bytes.empty())
                throw Failure ("io", "the FLAC encoder failed");
            const auto data = toBlock (bytes);
            writeBytes (file, comments.empty() ? data : rewriteFlacComments (data, comments));
            return;
        }
        case AudioFileType::ogg:
        {
            const auto channels = channelPointers (buffer, start);
            const auto bytes = codec::encodeVorbis (channels.data(), buffer.getNumChannels(), numSamples, sampleRate, quality, oggSerial);
            if (bytes.empty())
                throw Failure ("io", "the Ogg Vorbis encoder rejected " + std::to_string ((int) sampleRate) + " Hz output");
            writeBytes (file, rewriteOggHeaders (toBlock (bytes), comments));
            return;
        }
        case AudioFileType::mp3:
        {
#if MUSIC_RENDER_HAS_LAME
            writeBytes (file, encodeMp3 (buffer, start, numSamples, sampleRate, quality, tags.title));
            return;
#else
            return;   // checkAudioFileFormat 이 이미 거절했다
#endif
        }
    }
}

StreamingWavWriter::StreamingWavWriter (const juce::File& file, double sampleRate, int channels, int bitDepth) : file_ (file)
{
    checkWavBitDepth (bitDepth);
    prepareFile (file);
    writer_ = std::make_unique<codec::WavFileWriter> (file.getFullPathName().toStdString(), sampleRate, channels, bitDepth);
    if (! writer_->ok())
        throw Failure ("io", "cannot open " + file.getFullPathName().toStdString() + " for writing");
}

StreamingWavWriter::~StreamingWavWriter()
{
    if (writer_ != nullptr)
        writer_->close();
}

void StreamingWavWriter::write (const juce::AudioBuffer<float>& buffer, int start, int numSamples)
{
    if (writer_ == nullptr || numSamples <= 0)
        return;
    const auto channels = channelPointers (buffer, start);
    if (! writer_->write (channels.data(), numSamples))
        throw Failure ("io", "failed while writing " + file_.getFullPathName().toStdString());
    written_ += numSamples;
}

void StreamingWavWriter::close()
{
    if (writer_ != nullptr && ! writer_->close())
        throw Failure ("io", "failed while writing " + file_.getFullPathName().toStdString());
    writer_.reset();
}

void writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, int numSamples, double sampleRate, int bitDepth)
{
    checkWavBitDepth (bitDepth);
    writeAudioFile (file, buffer, 0, numSamples, sampleRate, { AudioFileType::wav, bitDepth, 0 });
}

AudioData readAudioFile (const juce::File& file)
{
    if (! file.existsAsFile())
        throw Failure ("io", "file not found: " + file.getFullPathName().toStdString());
    std::string error;
    auto decoded = codec::decodeFile (file.getFullPathName().toStdString(), error);
    if (! decoded)
        throw Failure ("io", "not a readable audio file: " + file.getFullPathName().toStdString());
    if (decoded->frames > (int64_t) std::numeric_limits<int>::max())
        throw Failure ("io", "audio file is too long to analyse in memory");

    AudioData data;
    data.sampleRate = decoded->sampleRate;
    data.bitDepth = decoded->bitDepth;
    data.floatingPoint = decoded->floatingPoint;
    data.samples = decoded->frames;
    data.format = decoded->format;
    data.buffer.setSize ((int) decoded->channels.size(), (int) decoded->frames);
    for (size_t c = 0; c < decoded->channels.size(); ++c)
        data.buffer.copyFrom ((int) c, 0, decoded->channels[c].data(), (int) decoded->frames);
    return data;
}

std::vector<std::string> readVorbisComments (const juce::File& file)
{
    juce::MemoryBlock data;
    if (! file.loadFileAsData (data))
        return {};
    const auto* d = (const uint8_t*) data.getData();
    std::string vendor;
    std::vector<std::string> comments;
    if (const auto pages = parseOggPages (d, data.getSize()))
    {
        if (const auto headers = oggHeaderPackets (*pages))
        {
            const auto& packet = headers->packets[1];
            if (packet.size() > 7 && packet[0] == 3 && parseVorbisCommentBody (packet.data() + 7, packet.size() - 7, vendor, comments))
                return comments;
        }
        return {};
    }
    if (const auto blocks = parseFlacBlocks (d, data.getSize()))
        for (const auto& b : blocks->first)
            if (b.type == flacVorbisComment && parseVorbisCommentBody (b.body.data(), b.body.size(), vendor, comments))
                return comments;
    return {};
}

} // namespace mr
