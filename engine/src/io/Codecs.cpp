#include "io/Codecs.h"

#include <FLAC/stream_decoder.h>
#include <FLAC/stream_encoder.h>
#include <vorbis/vorbisenc.h>
#include <vorbis/vorbisfile.h>

#if defined(__APPLE__)
 #include <AudioToolbox/AudioToolbox.h>
#endif

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>

namespace mr::codec
{

namespace
{
// 인코더에 한 번에 넣는 프레임 수 (이전 쓰기와 같다: 4096 칸을 채널 수로 나눈다)
int chunkFrames (int numChannels) { return 4096 / std::max (1, numChannels); }

std::optional<std::vector<uint8_t>> readAll (const std::string& path)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
        return std::nullopt;
    return std::vector<uint8_t> ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
}

uint32_t le32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
uint16_t le16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
uint32_t be32 (const uint8_t* p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3]; }
uint16_t be16 (const uint8_t* p) { return (uint16_t) ((p[0] << 8) | p[1]); }

void put16 (std::vector<uint8_t>& o, uint32_t v) { o.push_back ((uint8_t) v); o.push_back ((uint8_t) (v >> 8)); }
void put32 (std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back ((uint8_t) (v >> (8 * i))); }
void putTag (std::vector<uint8_t>& o, const char* tag) { o.insert (o.end(), tag, tag + 4); }

// 정수 샘플(왼쪽 정렬 32비트)을 float 로: 2^-31 을 곱한다
inline float fixedToFloat (int32_t v) noexcept { return (float) v * (1.0f / 2147483648.0f); }

// ---- WAV ----
// 머리: RIFF, WAVE, JUNK 52 바이트 (RF64 로 바꿀 자리), fmt 16 바이트, [smpl], data
std::vector<uint8_t> wavHeader (int numChannels, double sampleRate, int bitDepth, uint64_t dataBytes, const std::optional<WavLoop>& loop)
{
    const uint32_t bytesPerFrame = (uint32_t) (numChannels * bitDepth / 8);
    const uint64_t smplBytes = loop ? 8 + 60 : 0;
    int64_t riff = (int64_t) (4 + 8 + 40 + 8 + dataBytes + (dataBytes & 1) + smplBytes + (8 + 28));
    riff += riff & 1;
    std::vector<uint8_t> h;
    putTag (h, "RIFF");
    put32 (h, (uint32_t) riff);
    putTag (h, "WAVE");
    putTag (h, "JUNK");
    put32 (h, 52);
    h.insert (h.end(), 52, 0);
    putTag (h, "fmt ");
    put32 (h, 16);
    put16 (h, bitDepth == 32 ? 3 : 1);
    put16 (h, (uint32_t) numChannels);
    put32 (h, (uint32_t) (int) sampleRate);
    put32 (h, (uint32_t) (int) ((double) bytesPerFrame * sampleRate));
    put16 (h, bytesPerFrame);
    put16 (h, (uint32_t) bitDepth);
    if (loop)
    {
        putTag (h, "smpl");
        put32 (h, 60);
        put32 (h, 0);                                                          // 제조사
        put32 (h, 0);                                                          // 제품
        put32 (h, (uint32_t) (int) std::lround (1.0e9 / sampleRate));         // 샘플 주기 (ns)
        put32 (h, 60);                                                         // MIDI 기준 음
        put32 (h, 0);
        put32 (h, 0);
        put32 (h, 0);
        put32 (h, 1);                                                          // 반복 수
        put32 (h, 0);
        put32 (h, 0);                                                          // 반복 번호
        put32 (h, 0);                                                          // 정방향
        put32 (h, (uint32_t) loop->start);
        put32 (h, (uint32_t) loop->endInclusive);
        put32 (h, 0);
        put32 (h, 0);                                                          // 무한 반복
    }
    putTag (h, "data");
    put32 (h, (uint32_t) dataBytes);
    return h;
}

void appendFrames (std::vector<uint8_t>& out, const float* const* channels, int numChannels, int64_t start, int64_t count, int bitDepth)
{
    const size_t bytes = (size_t) (bitDepth / 8);
    out.reserve (out.size() + (size_t) count * (size_t) numChannels * bytes);
    for (int64_t i = start; i < start + count; ++i)
        for (int c = 0; c < numChannels; ++c)
        {
            const float s = channels[c][i];
            if (bitDepth == 32)
            {
                uint32_t bits;
                std::memcpy (&bits, &s, 4);
                put32 (out, bits);
                continue;
            }
            const int32_t v = floatToInt32 (s);
            if (bitDepth == 16)
                put16 (out, (uint32_t) (uint16_t) (int16_t) (v >> 16));
            else
            {
                const int32_t w = v >> 8;
                out.push_back ((uint8_t) w);
                out.push_back ((uint8_t) (w >> 8));
                out.push_back ((uint8_t) (w >> 16));
            }
        }
}

struct Chunk
{
    char id[5] = {};
    const uint8_t* data = nullptr;
    uint32_t size = 0;
};

std::optional<Decoded> decodePcm (const uint8_t* data, uint64_t bytes, int channels, int bits, bool floating, bool bigEndian, double rate,
                                  const char* format)
{
    if (channels <= 0 || bits <= 0 || rate <= 0.0)
        return std::nullopt;
    const int bytesPer = (bits + 7) / 8;
    const uint64_t frames = bytes / (uint64_t) (bytesPer * channels);
    Decoded d;
    d.sampleRate = rate;
    d.bitDepth = bits;
    d.floatingPoint = floating;
    d.frames = (int64_t) frames;
    d.format = format;
    d.channels.assign ((size_t) channels, std::vector<float> ((size_t) frames));
    for (uint64_t i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c)
        {
            const uint8_t* p = data + (i * (uint64_t) channels + (uint64_t) c) * (uint64_t) bytesPer;
            float s = 0.0f;
            if (floating && bytesPer == 4)
            {
                const uint32_t u = bigEndian ? be32 (p) : le32 (p);
                std::memcpy (&s, &u, 4);
            }
            else if (floating && bytesPer == 8)
            {
                uint64_t u = 0;
                for (int k = 0; k < 8; ++k)
                    u |= (uint64_t) p[bigEndian ? 7 - k : k] << (8 * k);
                double v;
                std::memcpy (&v, &u, 8);
                s = (float) v;
            }
            else
            {
                // 왼쪽 정렬 32비트 정수로 모은 뒤 2^-31 을 곱한다
                uint32_t u = 0;
                for (int k = 0; k < bytesPer; ++k)
                {
                    const uint8_t b = p[bigEndian ? k : bytesPer - 1 - k];
                    u = (u << 8) | b;
                }
                u <<= (32 - 8 * bytesPer);
                if (bytesPer == 1 && ! bigEndian)
                    u ^= 0x80000000u;   // WAV 의 8비트는 부호 없는 수
                s = fixedToFloat ((int32_t) u);
            }
            d.channels[(size_t) c][(size_t) i] = s;
        }
    return d;
}

std::optional<Decoded> decodeWav (const std::vector<uint8_t>& b, bool headerOnly)
{
    if (b.size() < 12 || (std::memcmp (b.data(), "RIFF", 4) != 0 && std::memcmp (b.data(), "RF64", 4) != 0) || std::memcmp (b.data() + 8, "WAVE", 4) != 0)
        return std::nullopt;
    int channels = 0, bits = 0, tag = 0;
    double rate = 0.0;
    const uint8_t* data = nullptr;
    uint64_t dataBytes = 0, ds64Data = 0;
    size_t pos = 12;
    while (pos + 8 <= b.size())
    {
        const uint8_t* id = b.data() + pos;
        const uint32_t size = le32 (b.data() + pos + 4);
        const uint8_t* body = b.data() + pos + 8;
        const size_t available = b.size() - pos - 8;
        if (std::memcmp (id, "ds64", 4) == 0 && available >= 16)
            ds64Data = (uint64_t) le32 (body + 8) | ((uint64_t) le32 (body + 12) << 32);
        else if (std::memcmp (id, "fmt ", 4) == 0 && available >= 16)
        {
            tag = le16 (body);
            channels = le16 (body + 2);
            rate = (double) le32 (body + 4);
            bits = le16 (body + 14);
            if (tag == 0xfffe && size >= 40 && available >= 40)
                tag = le16 (body + 24);   // 확장 형식의 하위 형식 GUID 첫 두 바이트
        }
        else if (std::memcmp (id, "data", 4) == 0)
        {
            data = body;
            dataBytes = size == 0xffffffffu && ds64Data > 0 ? ds64Data : size;
            dataBytes = std::min<uint64_t> (dataBytes, available);
            break;
        }
        pos += 8 + (size_t) size + (size & 1);
    }
    if (data == nullptr || (tag != 1 && tag != 3))
        return std::nullopt;
    if (headerOnly)
    {
        Decoded d;
        d.sampleRate = rate;
        d.bitDepth = bits;
        d.floatingPoint = tag == 3;
        d.frames = channels > 0 && bits > 0 ? (int64_t) (dataBytes / (uint64_t) (channels * ((bits + 7) / 8))) : 0;
        d.format = "WAV file";
        d.channels.resize ((size_t) std::max (0, channels));
        return d;
    }
    return decodePcm (data, dataBytes, channels, bits, tag == 3, false, rate, "WAV file");
}

// AIFF 의 80비트 확장 정밀도 수
double readExtended (const uint8_t* p)
{
    const int exponent = ((p[0] & 0x7f) << 8) | p[1];
    uint64_t mantissa = 0;
    for (int i = 0; i < 8; ++i)
        mantissa = (mantissa << 8) | p[2 + i];
    if (exponent == 0 && mantissa == 0)
        return 0.0;
    const double v = std::ldexp ((double) mantissa, exponent - 16383 - 63);
    return (p[0] & 0x80) ? -v : v;
}

std::optional<Decoded> decodeAiff (const std::vector<uint8_t>& b, bool headerOnly)
{
    if (b.size() < 12 || std::memcmp (b.data(), "FORM", 4) != 0)
        return std::nullopt;
    const bool aifc = std::memcmp (b.data() + 8, "AIFC", 4) == 0;
    if (! aifc && std::memcmp (b.data() + 8, "AIFF", 4) != 0)
        return std::nullopt;
    int channels = 0, bits = 0;
    double rate = 0.0;
    bool floating = false, littleEndian = false;
    const uint8_t* data = nullptr;
    uint64_t dataBytes = 0;
    size_t pos = 12;
    while (pos + 8 <= b.size())
    {
        const uint8_t* id = b.data() + pos;
        const uint32_t size = be32 (b.data() + pos + 4);
        const uint8_t* body = b.data() + pos + 8;
        const size_t available = b.size() - pos - 8;
        if (std::memcmp (id, "COMM", 4) == 0 && available >= 18)
        {
            channels = be16 (body);
            bits = be16 (body + 6);
            rate = readExtended (body + 8);
            if (aifc && available >= 22)
            {
                floating = std::memcmp (body + 18, "fl32", 4) == 0 || std::memcmp (body + 18, "FL32", 4) == 0;
                littleEndian = std::memcmp (body + 18, "sowt", 4) == 0;
                if (floating)
                    bits = 32;
            }
        }
        else if (std::memcmp (id, "SSND", 4) == 0 && available >= 8)
        {
            const uint32_t offset = be32 (body);
            data = body + 8 + offset;
            dataBytes = size >= 8 + offset ? size - 8 - offset : 0;
            dataBytes = std::min<uint64_t> (dataBytes, available >= 8 + offset ? available - 8 - offset : 0);
        }
        pos += 8 + (size_t) size + (size & 1);
    }
    if (data == nullptr)
        return std::nullopt;
    if (headerOnly)
    {
        Decoded d;
        d.sampleRate = rate;
        d.bitDepth = bits;
        d.floatingPoint = floating;
        d.frames = channels > 0 && bits > 0 ? (int64_t) (dataBytes / (uint64_t) (channels * ((bits + 7) / 8))) : 0;
        d.format = "AIFF file";
        d.channels.resize ((size_t) std::max (0, channels));
        return d;
    }
    return decodePcm (data, dataBytes, channels, bits, floating, ! littleEndian, rate, "AIFF file");
}

// ---- FLAC ----
struct FlacReadState
{
    const std::vector<uint8_t>* bytes = nullptr;
    size_t pos = 0;
    Decoded out;
    bool headerOnly = false;
    bool failed = false;
};

FLAC__StreamDecoderReadStatus flacRead (const FLAC__StreamDecoder*, FLAC__byte buffer[], size_t* bytes, void* client)
{
    auto& s = *static_cast<FlacReadState*> (client);
    const size_t left = s.bytes->size() - s.pos;
    if (left == 0)
    {
        *bytes = 0;
        return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
    }
    const size_t n = std::min (left, *bytes);
    std::memcpy (buffer, s.bytes->data() + s.pos, n);
    s.pos += n;
    *bytes = n;
    return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

FLAC__StreamDecoderWriteStatus flacWrite (const FLAC__StreamDecoder*, const FLAC__Frame* frame, const FLAC__int32* const buffer[], void* client)
{
    auto& s = *static_cast<FlacReadState*> (client);
    const unsigned bits = frame->header.bits_per_sample;
    const int shift = 32 - (int) bits;
    for (size_t c = 0; c < s.out.channels.size() && c < frame->header.channels; ++c)
    {
        auto& dest = s.out.channels[c];
        for (unsigned i = 0; i < frame->header.blocksize; ++i)
            dest.push_back (fixedToFloat ((int32_t) ((uint32_t) buffer[c][i] << shift)));
    }
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void flacMetadata (const FLAC__StreamDecoder*, const FLAC__StreamMetadata* m, void* client)
{
    auto& s = *static_cast<FlacReadState*> (client);
    if (m->type != FLAC__METADATA_TYPE_STREAMINFO)
        return;
    const auto& info = m->data.stream_info;
    s.out.sampleRate = info.sample_rate;
    s.out.bitDepth = (int) info.bits_per_sample;
    s.out.frames = (int64_t) info.total_samples;
    s.out.channels.resize (info.channels);
    if (! s.headerOnly)
        for (auto& c : s.out.channels)
            c.reserve ((size_t) info.total_samples);
}

void flacError (const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void* client) { static_cast<FlacReadState*> (client)->failed = true; }

std::optional<Decoded> decodeFlac (const std::vector<uint8_t>& b, bool headerOnly)
{
    if (b.size() < 4 || std::memcmp (b.data(), "fLaC", 4) != 0)
        return std::nullopt;
    FlacReadState s;
    s.bytes = &b;
    s.headerOnly = headerOnly;
    s.out.format = "FLAC file";
    auto* decoder = FLAC__stream_decoder_new();
    if (decoder == nullptr)
        return std::nullopt;
    bool ok = FLAC__stream_decoder_init_stream (decoder, flacRead, nullptr, nullptr, nullptr, nullptr, flacWrite, flacMetadata, flacError, &s)
              == FLAC__STREAM_DECODER_INIT_STATUS_OK;
    if (ok)
        ok = headerOnly ? FLAC__stream_decoder_process_until_end_of_metadata (decoder) : FLAC__stream_decoder_process_until_end_of_stream (decoder);
    FLAC__stream_decoder_finish (decoder);
    FLAC__stream_decoder_delete (decoder);
    if (! ok || s.out.sampleRate <= 0.0)
        return std::nullopt;
    if (! headerOnly)
    {
        // 머리에 적힌 길이에 맞춘다 (모자라면 0)
        for (auto& c : s.out.channels)
            c.resize ((size_t) s.out.frames, 0.0f);
    }
    return std::move (s.out);
}

// ---- Ogg Vorbis ----
struct OggSource
{
    const std::vector<uint8_t>* bytes = nullptr;
    size_t pos = 0;
};

size_t oggRead (void* ptr, size_t size, size_t count, void* source)
{
    auto& s = *static_cast<OggSource*> (source);
    const size_t want = size * count;
    const size_t n = std::min (want, s.bytes->size() - s.pos);
    std::memcpy (ptr, s.bytes->data() + s.pos, n);
    s.pos += n;
    return size > 0 ? n / size : 0;
}

int oggSeek (void* source, ogg_int64_t offset, int whence)
{
    auto& s = *static_cast<OggSource*> (source);
    int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (int64_t) s.pos : (int64_t) s.bytes->size();
    const int64_t target = base + offset;
    if (target < 0 || target > (int64_t) s.bytes->size())
        return -1;
    s.pos = (size_t) target;
    return 0;
}

long oggTell (void* source) { return (long) static_cast<OggSource*> (source)->pos; }

std::optional<Decoded> decodeVorbis (const std::vector<uint8_t>& b, bool headerOnly)
{
    if (b.size() < 4 || std::memcmp (b.data(), "OggS", 4) != 0)
        return std::nullopt;
    OggSource src { &b, 0 };
    OggVorbis_File vf;
    ov_callbacks callbacks { oggRead, oggSeek, nullptr, oggTell };
    if (ov_open_callbacks (&src, &vf, nullptr, 0, callbacks) != 0)
        return std::nullopt;
    const vorbis_info* info = ov_info (&vf, -1);
    Decoded d;
    d.format = "Ogg-Vorbis file";
    d.bitDepth = 16;
    d.sampleRate = info != nullptr ? (double) info->rate : 0.0;
    const int channels = info != nullptr ? info->channels : 0;
    d.frames = (int64_t) ov_pcm_total (&vf, -1);
    d.channels.assign ((size_t) std::max (0, channels), {});
    if (! headerOnly && channels > 0)
    {
        for (auto& c : d.channels)
            c.reserve ((size_t) std::max<int64_t> (0, d.frames));
        int section = 0;
        for (;;)
        {
            float** pcm = nullptr;
            const long n = ov_read_float (&vf, &pcm, 4096, &section);
            if (n <= 0)
                break;
            for (int c = 0; c < channels; ++c)
                d.channels[(size_t) c].insert (d.channels[(size_t) c].end(), pcm[c], pcm[c] + n);
        }
        for (auto& c : d.channels)
            c.resize ((size_t) std::max<int64_t> (0, d.frames), 0.0f);
    }
    ov_clear (&vf);
    if (d.sampleRate <= 0.0)
        return std::nullopt;
    return d;
}

// ---- MP3 (AudioToolbox) ----
std::optional<Decoded> decodeSystem (const std::string& path, bool headerOnly)
{
#if defined(__APPLE__)
    CFURLRef url = CFURLCreateFromFileSystemRepresentation (nullptr, (const UInt8*) path.c_str(), (CFIndex) path.size(), false);
    if (url == nullptr)
        return std::nullopt;
    ExtAudioFileRef file = nullptr;
    const OSStatus opened = ExtAudioFileOpenURL (url, &file);
    CFRelease (url);
    if (opened != noErr || file == nullptr)
        return std::nullopt;
    AudioStreamBasicDescription source {};
    UInt32 size = sizeof (source);
    SInt64 frames = 0;
    UInt32 framesSize = sizeof (frames);
    if (ExtAudioFileGetProperty (file, kExtAudioFileProperty_FileDataFormat, &size, &source) != noErr
        || ExtAudioFileGetProperty (file, kExtAudioFileProperty_FileLengthFrames, &framesSize, &frames) != noErr)
    {
        ExtAudioFileDispose (file);
        return std::nullopt;
    }
    Decoded d;
    d.format = source.mFormatID == kAudioFormatMPEGLayer3 ? "MP3 file" : "CoreAudio supported file";
    d.sampleRate = source.mSampleRate;
    d.bitDepth = 16;
    d.frames = frames;
    const int channels = (int) source.mChannelsPerFrame;
    d.channels.assign ((size_t) channels, {});
    if (! headerOnly && channels > 0)
    {
        AudioStreamBasicDescription client {};
        client.mSampleRate = source.mSampleRate;
        client.mFormatID = kAudioFormatLinearPCM;
        client.mFormatFlags = (AudioFormatFlags) kAudioFormatFlagsNativeFloatPacked | (AudioFormatFlags) kAudioFormatFlagIsNonInterleaved;
        client.mBitsPerChannel = 32;
        client.mChannelsPerFrame = (UInt32) channels;
        client.mFramesPerPacket = 1;
        client.mBytesPerFrame = 4;
        client.mBytesPerPacket = 4;
        ExtAudioFileSetProperty (file, kExtAudioFileProperty_ClientDataFormat, sizeof (client), &client);
        for (auto& c : d.channels)
            c.resize ((size_t) std::max<SInt64> (0, frames), 0.0f);
        std::vector<float> scratch ((size_t) channels * 4096);
        std::vector<uint8_t> listBytes (sizeof (AudioBufferList) + sizeof (AudioBuffer) * (size_t) channels);
        auto* list = reinterpret_cast<AudioBufferList*> (listBytes.data());
        int64_t at = 0;
        while (at < frames)
        {
            list->mNumberBuffers = (UInt32) channels;
            for (int c = 0; c < channels; ++c)
                list->mBuffers[c] = { 1, (UInt32) (4096 * sizeof (float)), scratch.data() + (size_t) c * 4096 };
            UInt32 n = 4096;
            if (ExtAudioFileRead (file, &n, list) != noErr || n == 0)
                break;
            const int64_t take = std::min<int64_t> (n, frames - at);
            for (int c = 0; c < channels; ++c)
                std::memcpy (d.channels[(size_t) c].data() + at, scratch.data() + (size_t) c * 4096, (size_t) take * sizeof (float));
            at += take;
        }
    }
    ExtAudioFileDispose (file);
    if (d.sampleRate <= 0.0)
        return std::nullopt;
    return d;
#else
    (void) path;
    (void) headerOnly;
    return std::nullopt;
#endif
}

std::optional<Decoded> decodeAny (const std::string& path, bool headerOnly)
{
    auto bytes = readAll (path);
    if (! bytes)
        return std::nullopt;
    if (auto d = decodeWav (*bytes, headerOnly))
        return d;
    if (auto d = decodeAiff (*bytes, headerOnly))
        return d;
    if (auto d = decodeFlac (*bytes, headerOnly))
        return d;
    if (auto d = decodeVorbis (*bytes, headerOnly))
        return d;
    return decodeSystem (path, headerOnly);
}

void packBig (uint8_t* p, uint64_t v, int bytes)
{
    for (int i = bytes - 1; i >= 0; --i)
    {
        p[i] = (uint8_t) (v & 0xff);
        v >>= 8;
    }
}

struct FlacWriteState
{
    std::vector<uint8_t> out;
};

FLAC__StreamEncoderWriteStatus flacEncodeWrite (const FLAC__StreamEncoder*, const FLAC__byte buffer[], size_t bytes, uint32_t, uint32_t, void* client)
{
    auto& s = *static_cast<FlacWriteState*> (client);
    s.out.insert (s.out.end(), buffer, buffer + bytes);
    return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

FLAC__StreamEncoderTellStatus flacEncodeTell (const FLAC__StreamEncoder*, FLAC__uint64* offset, void* client)
{
    *offset = (FLAC__uint64) static_cast<FlacWriteState*> (client)->out.size();
    return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
}

// 인코딩이 끝나면 STREAMINFO (최소 최대 프레임 크기, 샘플 수, MD5)를 맨 앞 블록 자리에 다시 쓴다
void flacEncodeMetadata (const FLAC__StreamEncoder*, const FLAC__StreamMetadata* m, void* client)
{
    auto& s = *static_cast<FlacWriteState*> (client);
    if (m->type != FLAC__METADATA_TYPE_STREAMINFO || s.out.size() < 4 + 4 + 34)
        return;
    const auto& info = m->data.stream_info;
    uint8_t b[4 + 34] = {};
    packBig (b, 34, 4);   // 블록 머리: 마지막 아님, 종류 0, 길이 34
    uint8_t* p = b + 4;
    packBig (p, info.min_blocksize, 2);
    packBig (p + 2, info.max_blocksize, 2);
    packBig (p + 4, info.min_framesize, 3);
    packBig (p + 7, info.max_framesize, 3);
    const unsigned ch = info.channels - 1, bits = info.bits_per_sample - 1;
    p[10] = (uint8_t) ((info.sample_rate >> 12) & 0xff);
    p[11] = (uint8_t) ((info.sample_rate >> 4) & 0xff);
    p[12] = (uint8_t) (((info.sample_rate & 0x0f) << 4) | (ch << 1) | (bits >> 4));
    p[13] = (uint8_t) (((bits & 0x0f) << 4) | (unsigned) ((info.total_samples >> 32) & 0x0f));
    packBig (p + 14, (uint32_t) info.total_samples, 4);
    std::memcpy (p + 18, info.md5sum, 16);
    std::memcpy (s.out.data() + 4, b, sizeof (b));
}
} // namespace

int32_t floatToInt32 (float sample) noexcept
{
    const double s = sample;
    if (s <= -1.0)
        return std::numeric_limits<int32_t>::min();
    if (s >= 1.0)
        return std::numeric_limits<int32_t>::max();
    return (int32_t) std::nearbyint ((double) std::numeric_limits<int32_t>::max() * s);
}

std::optional<Probe> probeFile (const std::string& path)
{
    const auto d = decodeAny (path, true);
    if (! d)
        return std::nullopt;
    return Probe { d->sampleRate, d->frames, (int) d->channels.size(), d->bitDepth, d->floatingPoint, d->format };
}

std::optional<Decoded> decodeFile (const std::string& path, std::string& error)
{
    auto d = decodeAny (path, false);
    if (! d)
        error = "not a readable audio file: " + path;
    return d;
}

std::vector<uint8_t> encodeWav (const float* const* channels, int numChannels, int64_t numFrames, double sampleRate, int bitDepth,
                                const std::optional<WavLoop>& loop)
{
    const uint64_t dataBytes = (uint64_t) numFrames * (uint64_t) numChannels * (uint64_t) (bitDepth / 8);
    auto out = wavHeader (numChannels, sampleRate, bitDepth, dataBytes, loop);
    appendFrames (out, channels, numChannels, 0, numFrames, bitDepth);
    if (dataBytes & 1)
        out.push_back (0);
    return out;
}

WavFileWriter::WavFileWriter (const std::string& path, double sampleRate, int numChannels, int bitDepth)
    : sampleRate_ (sampleRate), channels_ (numChannels), bitDepth_ (bitDepth)
{
    file_ = std::fopen (path.c_str(), "wb");
    if (file_ == nullptr)
        return;
    const auto header = wavHeader (channels_, sampleRate_, bitDepth_, 0, std::nullopt);
    if (std::fwrite (header.data(), 1, header.size(), file_) != header.size())
    {
        std::fclose (file_);
        file_ = nullptr;
    }
}

WavFileWriter::~WavFileWriter() { close(); }

bool WavFileWriter::write (const float* const* channels, int numFrames)
{
    if (file_ == nullptr || numFrames <= 0)
        return file_ != nullptr;
    scratch_.clear();
    appendFrames (scratch_, channels, channels_, 0, numFrames, bitDepth_);
    frames_ += numFrames;
    return std::fwrite (scratch_.data(), 1, scratch_.size(), file_) == scratch_.size();
}

bool WavFileWriter::close()
{
    if (file_ == nullptr)
        return false;
    const uint64_t dataBytes = (uint64_t) frames_ * (uint64_t) channels_ * (uint64_t) (bitDepth_ / 8);
    bool ok = true;
    if (dataBytes & 1)
        ok = std::fputc (0, file_) != EOF;
    const auto header = wavHeader (channels_, sampleRate_, bitDepth_, dataBytes, std::nullopt);
    ok = ok && std::fseek (file_, 0, SEEK_SET) == 0 && std::fwrite (header.data(), 1, header.size(), file_) == header.size();
    ok = std::fclose (file_) == 0 && ok;
    file_ = nullptr;
    return ok;
}

std::vector<uint8_t> encodeFlac (const float* const* channels, int numChannels, int64_t numFrames, double sampleRate, int bitDepth,
                                 int compression)
{
    FlacWriteState state;
    auto* encoder = FLAC__stream_encoder_new();
    if (encoder == nullptr)
        return {};
    // 압축 단계를 먼저 정하고 스테레오 설정을 덮어쓴다 (이전 쓰기와 같은 순서)
    if (compression > 0)
        FLAC__stream_encoder_set_compression_level (encoder, (uint32_t) std::min (8, compression));
    FLAC__stream_encoder_set_do_mid_side_stereo (encoder, numChannels == 2);
    FLAC__stream_encoder_set_loose_mid_side_stereo (encoder, numChannels == 2);
    FLAC__stream_encoder_set_channels (encoder, (uint32_t) numChannels);
    FLAC__stream_encoder_set_bits_per_sample (encoder, (uint32_t) std::min (24, bitDepth));
    FLAC__stream_encoder_set_sample_rate (encoder, (uint32_t) sampleRate);
    FLAC__stream_encoder_set_blocksize (encoder, 0);
    FLAC__stream_encoder_set_do_escape_coding (encoder, true);
    bool ok = FLAC__stream_encoder_init_stream (encoder, flacEncodeWrite, nullptr, flacEncodeTell, flacEncodeMetadata, &state)
              == FLAC__STREAM_ENCODER_INIT_STATUS_OK;
    const int shift = 32 - bitDepth;
    const int chunk = chunkFrames (numChannels);
    std::vector<FLAC__int32> block ((size_t) numChannels * (size_t) chunk);
    std::vector<const FLAC__int32*> pointers ((size_t) numChannels);
    for (int64_t at = 0; ok && at < numFrames; at += chunk)
    {
        const int n = (int) std::min<int64_t> (chunk, numFrames - at);
        for (int c = 0; c < numChannels; ++c)
        {
            auto* dest = block.data() + (size_t) c * (size_t) chunk;
            for (int i = 0; i < n; ++i)
                dest[i] = floatToInt32 (channels[c][at + i]) >> shift;
            pointers[(size_t) c] = dest;
        }
        ok = FLAC__stream_encoder_process (encoder, pointers.data(), (uint32_t) n) != 0;
    }
    if (ok)
        ok = FLAC__stream_encoder_finish (encoder) != 0;
    FLAC__stream_encoder_delete (encoder);
    return ok ? std::move (state.out) : std::vector<uint8_t> {};
}

std::vector<uint8_t> encodeVorbis (const float* const* channels, int numChannels, int64_t numFrames, double sampleRate, int quality,
                                   uint32_t serial)
{
    vorbis_info vi;
    vorbis_info_init (&vi);
    if (vorbis_encode_init_vbr (&vi, numChannels, (long) sampleRate, std::clamp ((float) quality * 0.1f, 0.0f, 1.0f)) != 0)
    {
        vorbis_info_clear (&vi);
        return {};
    }
    std::vector<uint8_t> out;
    auto writePage = [&out] (const ogg_page& page)
    {
        out.insert (out.end(), page.header, page.header + page.header_len);
        out.insert (out.end(), page.body, page.body + page.body_len);
    };
    vorbis_comment vc;
    vorbis_comment_init (&vc);
    vorbis_dsp_state vd;
    vorbis_block vb;
    vorbis_analysis_init (&vd, &vi);
    vorbis_block_init (&vd, &vb);
    ogg_stream_state os;
    ogg_stream_init (&os, (int) serial);
    ogg_packet header, comments, codebooks, packet;
    ogg_page page;
    vorbis_analysis_headerout (&vd, &vc, &header, &comments, &codebooks);
    ogg_stream_packetin (&os, &header);
    ogg_stream_packetin (&os, &comments);
    ogg_stream_packetin (&os, &codebooks);
    while (ogg_stream_flush (&os, &page) != 0)
        writePage (page);

    // 정수로 바꿨다가 2^-31 을 곱해 되돌린 값을 넣는다 (이전 쓰기와 같은 입력)
    auto analyse = [&] (int n)
    {
        vorbis_analysis_wrote (&vd, n);
        while (vorbis_analysis_blockout (&vd, &vb) == 1)
        {
            vorbis_analysis (&vb, nullptr);
            vorbis_bitrate_addblock (&vb);
            while (vorbis_bitrate_flushpacket (&vd, &packet))
            {
                ogg_stream_packetin (&os, &packet);
                while (ogg_stream_pageout (&os, &page) != 0)
                {
                    writePage (page);
                    if (ogg_page_eos (&page))
                        break;
                }
            }
        }
    };
    const double gain = 1.0 / 2147483648.0;
    const int chunk = chunkFrames (numChannels);
    for (int64_t at = 0; at < numFrames; at += chunk)
    {
        const int n = (int) std::min<int64_t> (chunk, numFrames - at);
        float** buffer = vorbis_analysis_buffer (&vd, n);
        for (int c = numChannels; --c >= 0;)
            for (int i = 0; i < n; ++i)
                buffer[c][i] = (float) (floatToInt32 (channels[c][at + i]) * gain);
        analyse (n);
    }
    analyse (0);

    ogg_stream_clear (&os);
    vorbis_block_clear (&vb);
    vorbis_dsp_clear (&vd);
    vorbis_comment_clear (&vc);
    vorbis_info_clear (&vi);
    return out;
}

} // namespace mr::codec
