// 직접 만든 구현이 지금까지 쓰던 JUCE 구현과 같은 결과를 내는지 본다 (JUCE 를 걷어내는 동안만 둔다, docs/plans/own-framework.md).
// 같은 입력을 양쪽에 넣고 비트나 바이트까지 견준다.
#include "TestFramework.h"
#include "TestUtil.h"

#include "dsp/Fft.h"
#include "dsp/Freeverb.h"
#include "io/XmlWriter.h"
#include "midi/Smf.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <cstring>
#include <random>

using namespace mr;

namespace
{
std::vector<float> noise (size_t n, uint32_t seed, float scale = 1.0f)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> d (-scale, scale);
    std::vector<float> v (n);
    for (auto& x : v)
        x = d (rng);
    return v;
}

bool sameBits (const float* a, const float* b, size_t n) { return std::memcmp (a, b, n * sizeof (float)) == 0; }
} // namespace

TEST (parity_fft_matches_juce)
{
    for (int order = 1; order <= 15; ++order)
    {
        const int n = 1 << order;
        juce::dsp::FFT theirs (order);
        dsp::Fft ours (order);
        for (bool ignore : { true, false })
        {
            auto a = noise ((size_t) n * 2, (uint32_t) order * 7 + (ignore ? 1u : 0u));
            auto b = a;
            theirs.performRealOnlyForwardTransform (a.data(), ignore);
            ours.forwardReal (b.data(), ignore);
            CHECK (sameBits (a.data(), b.data(), (size_t) (ignore ? n + 2 : n * 2)));
            theirs.performRealOnlyInverseTransform (a.data());
            ours.inverseReal (b.data());
            CHECK (sameBits (a.data(), b.data(), (size_t) n * 2));
            auto c = noise ((size_t) n * 2, (uint32_t) order * 13 + 5), d = c;
            theirs.performFrequencyOnlyForwardTransform (c.data(), ignore);
            ours.forwardMagnitudes (d.data(), ignore);
            CHECK (sameBits (c.data(), d.data(), (size_t) n * 2));
        }
    }
}

TEST (parity_freeverb_matches_juce)
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        juce::Reverb theirs;
        dsp::Freeverb ours;
        juce::Reverb::Parameters tp;
        dsp::Freeverb::Parameters op;
        auto setBoth = [&] (float room, float damp, float wet, float dry, float width, float freeze)
        {
            tp.roomSize = op.roomSize = room;
            tp.damping = op.damping = damp;
            tp.wetLevel = op.wetLevel = wet;
            tp.dryLevel = op.dryLevel = dry;
            tp.width = op.width = width;
            tp.freezeMode = op.freezeMode = freeze;
            theirs.setParameters (tp);
            ours.setParameters (op);
        };
        // 엔진과 같은 순서: 매개변수, 샘플레이트, 다시 매개변수, 지우기
        setBoth (0.8f, 0.3f, 0.4f, 0.6f, 1.0f, 0.0f);
        theirs.setSampleRate (rate);
        ours.setSampleRate (rate);
        setBoth (0.8f, 0.3f, 0.4f, 0.6f, 1.0f, 0.0f);
        theirs.reset();
        ours.reset();
        auto l1 = noise (48000, 3, 0.5f), r1 = noise (48000, 4, 0.5f);
        auto l2 = l1, r2 = r1;
        int at = 0;
        int block = 0;
        for (const int size : { 512, 1, 333, 4096, 64, 9000, 2048, 31000 })
        {
            // 블록마다 매개변수를 바꿔 0.01초 선형 변화를 지난다
            if (block == 2)
                setBoth (0.2f, 0.9f, 1.0f / 3.0f, 0.0f, 0.5f, 0.0f);
            if (block == 4)
                setBoth (0.5f, 0.5f, 0.33f, 0.4f, 0.0f, 0.7f);
            if (block == 6)
                setBoth (0.95f, 0.1f, 0.2f, 0.8f, 1.0f, 0.0f);
            const int n = std::min (size, 48000 - at);
            theirs.processStereo (l1.data() + at, r1.data() + at, n);
            ours.processStereo (l2.data() + at, r2.data() + at, n);
            at += n;
            ++block;
        }
        CHECK (sameBits (l1.data(), l2.data(), l1.size()));
        CHECK (sameBits (r1.data(), r2.data(), r1.size()));
    }
}

TEST (parity_midi_file_matches_juce)
{
    // 같은 이벤트를 같은 순서로 넣는다: 겹친 같은 음, 같은 시각의 켜고 끄기, 메타, 컨트롤러, 피치 벤드
    std::mt19937 rng (11);
    juce::MidiMessageSequence theirs;
    midi::Sequence ours;
    auto both = [&] (const juce::MidiMessage& m, const midi::Event& e)
    {
        theirs.addEvent (m);
        ours.add (e);
    };
    auto stamped = [] (juce::MidiMessage m, double t) { m.setTimeStamp (t); return m; };
    both (stamped (juce::MidiMessage::textMetaEvent (3, juce::String::fromUTF8 ("선율 트랙")), 0), midi::Event::textMeta (3, "선율 트랙", 0));
    for (int i = 0; i < 400; ++i)
    {
        const double t = (double) (rng() % 4000);
        const int note = 50 + (int) (rng() % 12), vel = 1 + (int) (rng() % 127), ch = 1 + (int) (rng() % 2);
        const double len = (double) (1 + rng() % 300);
        switch (rng() % 6)
        {
            case 0:
            case 1:
            case 2:
                both (stamped (juce::MidiMessage::noteOn (ch, note, (juce::uint8) vel), t), midi::Event::noteOn (ch, note, vel, t));
                both (stamped (juce::MidiMessage::noteOff (ch, note), t + len), midi::Event::noteOff (ch, note, t + len));
                break;
            case 3:
            {
                const int cc = (int) (rng() % 120), value = (int) (rng() % 128);
                both (stamped (juce::MidiMessage::controllerEvent (1, cc, value), t), midi::Event::controller (1, cc, value, t));
                break;
            }
            case 4:
            {
                const int v = (int) (rng() % 16384);
                both (stamped (juce::MidiMessage::pitchWheel (1, v), t), midi::Event::pitchWheel (1, v, t));
                break;
            }
            default:
                both (stamped (juce::MidiMessage::textMetaEvent (5, juce::String::fromUTF8 ("가")), t), midi::Event::textMeta (5, "가", t));
                break;
        }
    }
    theirs.sort();
    theirs.updateMatchedPairs();
    ours.sort();
    ours.matchNoteOffs();
    REQUIRE (theirs.getNumEvents() == (int) ours.size());

    juce::MidiFile file;
    file.setTicksPerQuarterNote (960);
    file.addTrack (theirs);
    juce::MemoryOutputStream stream;
    file.writeTo (stream, 1);
    midi::File mine;
    mine.timeFormat = 960;
    mine.tracks.push_back (ours);
    const auto bytes = midi::write (mine);
    CHECK (bytes.size() == stream.getDataSize() && std::memcmp (bytes.data(), stream.getData(), bytes.size()) == 0);

    // 읽기: JUCE 가 읽은 것과 같은 이벤트, 같은 음 끔의 짝
    juce::MidiFile read;
    juce::MemoryInputStream in (stream.getData(), stream.getDataSize(), false);
    REQUIRE (read.readFrom (in, true));
    const auto parsed = midi::read (bytes);
    REQUIRE (parsed.has_value() && parsed->tracks.size() == 1);
    const auto& a = *read.getTrack (0);
    const auto& b = parsed->tracks[0];
    REQUIRE (a.getNumEvents() == (int) b.size());
    int mismatches = 0;
    for (int i = 0; i < a.getNumEvents(); ++i)
    {
        const auto& m = a.getEventPointer (i)->message;
        const bool sameEvent = m.getTimeStamp() == b[(size_t) i].time && (size_t) m.getRawDataSize() == b[(size_t) i].bytes.size()
                               && std::memcmp (m.getRawData(), b[(size_t) i].bytes.data(), b[(size_t) i].bytes.size()) == 0;
        const auto* off = a.getEventPointer (i)->noteOffObject;
        const auto* ourOff = b.noteOffFor ((size_t) i);
        const bool samePair = (off == nullptr) == (ourOff == nullptr) && (off == nullptr || off->message.getTimeStamp() == ourOff->time);
        if (! sameEvent || ! samePair)
            ++mismatches;
    }
    CHECK_EQ (mismatches, 0);
}

TEST (parity_xml_matches_juce)
{
    juce::XmlElement theirs ("score-partwise");
    XmlNode ours ("score-partwise");
    theirs.setAttribute ("version", "4.0");
    ours.set ("version", "4.0");
    auto* tw = theirs.createNewChildElement ("work");
    tw->createNewChildElement ("work-title")->addTextElement (juce::String::fromUTF8 ("늦은 편지 & <밤> \"따옴\" 'x' é\n둘째 줄"));
    ours.add ("work").add ("work-title", "늦은 편지 & <밤> \"따옴\" 'x' é\n둘째 줄");
    auto* tn = theirs.createNewChildElement ("note");
    tn->setAttribute ("dynamics", "88.89");
    tn->createNewChildElement ("chord");
    tn->createNewChildElement ("lyric")->setAttribute ("number", 1);
    auto& on = ours.add ("note");
    on.set ("dynamics", "88.89");
    on.add ("chord");
    on.add ("lyric").set ("number", 1);
    auto* attr = theirs.createNewChildElement ("direction");
    attr->setAttribute ("placement", juce::String::fromUTF8 ("위\n\"쪽\""));
    attr->setAttribute ("placement", "above");
    ours.add ("direction").set ("placement", "위\n\"쪽\"").set ("placement", "above");
    juce::XmlElement::TextFormat format;
    format.dtd = "<!DOCTYPE score-partwise>";
    format.lineWrapLength = 1000;
    format.newLineChars = "\n";   // 이전 MusicXML 쓰기는 파일에 쓸 때 줄바꿈을 \n 으로 바꿨다
    const auto expected = theirs.toString (format).toStdString();
    CHECK_EQ (ours.document ("<!DOCTYPE score-partwise>"), expected);
}

// ---- 오디오 파일 형식 ----
#include "io/Codecs.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
juce::AudioBuffer<float> testSignal (int channels, int frames, uint32_t seed)
{
    juce::AudioBuffer<float> b (channels, frames);
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> d (-0.9f, 0.9f);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < frames; ++i)
            b.setSample (c, i, 0.5f * std::sin ((float) i * 0.01f * (float) (c + 1)) + 0.3f * d (rng));
    b.setSample (0, 10, 1.0f);    // 양 끝 자르기
    b.setSample (0, 11, -1.0f);
    b.setSample (0, 12, 1.5f);
    return b;
}

std::vector<uint8_t> juceWrite (juce::AudioFormat& format, const juce::AudioBuffer<float>& b, double rate, int bits, bool floating, int quality)
{
    juce::MemoryBlock data;
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (data, false);
        using Options = juce::AudioFormatWriterOptions;
        auto writer = format.createWriterFor (stream, Options {}.withSampleRate (rate).withNumChannels (b.getNumChannels()).withBitsPerSample (bits)
                                                          .withSampleFormat (floating ? Options::SampleFormat::floatingPoint : Options::SampleFormat::integral)
                                                          .withQualityOptionIndex (quality));
        if (writer == nullptr)
            return {};
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
    return { static_cast<const uint8_t*> (data.getData()), static_cast<const uint8_t*> (data.getData()) + data.getSize() };
}

std::vector<const float*> pointers (const juce::AudioBuffer<float>& b)
{
    std::vector<const float*> p ((size_t) b.getNumChannels());
    for (int c = 0; c < b.getNumChannels(); ++c)
        p[(size_t) c] = b.getReadPointer (c);
    return p;
}

// Ogg 페이지마다 스트림 번호와 CRC 를 지운다 (JUCE 는 번호를 무작위로 고른다)
std::vector<uint8_t> withoutSerials (std::vector<uint8_t> bytes)
{
    for (size_t pos = 0; pos + 27 <= bytes.size();)
    {
        if (std::memcmp (bytes.data() + pos, "OggS", 4) != 0)
            break;
        std::memset (bytes.data() + pos + 14, 0, 4);
        std::memset (bytes.data() + pos + 22, 0, 4);
        const int segments = bytes[pos + 26];
        size_t body = 0;
        for (int i = 0; i < segments; ++i)
            body += bytes[pos + 27 + (size_t) i];
        pos += 27 + (size_t) segments + body;
    }
    return bytes;
}

std::string tempFile (const char* name, const std::vector<uint8_t>& bytes)
{
    const auto f = mrtest::outputDir().getChildFile (name);
    f.replaceWithData (bytes.data(), bytes.size());
    return f.getFullPathName().toStdString();
}
} // namespace

TEST (parity_wav_matches_juce)
{
    for (int channels : { 1, 2 })
        for (int bits : { 16, 24, 32 })
        {
            const auto b = testSignal (channels, 10001, (uint32_t) (channels * 100 + bits));
            juce::WavAudioFormat wav;
            const auto theirs = juceWrite (wav, b, 48000.0, bits, bits == 32, 0);
            const auto p = pointers (b);
            const auto ours = codec::encodeWav (p.data(), channels, b.getNumSamples(), 48000.0, bits);
            CHECK (theirs == ours);
            // 읽기: JUCE 가 읽은 float 와 같다
            std::string error;
            const auto decoded = codec::decodeFile (tempFile ("parity.wav", ours), error);
            REQUIRE (decoded.has_value());
            juce::AudioFormatManager manager;
            manager.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (manager.createReaderFor (mrtest::outputDir().getChildFile ("parity.wav")));
            REQUIRE (reader != nullptr);
            juce::AudioBuffer<float> read (channels, b.getNumSamples());
            reader->read (&read, 0, b.getNumSamples(), 0, true, true);
            bool same = decoded->frames == b.getNumSamples();
            for (int c = 0; c < channels && same; ++c)
                same = sameBits (read.getReadPointer (c), decoded->channels[(size_t) c].data(), (size_t) b.getNumSamples());
            CHECK (same);
        }
}

TEST (parity_flac_matches_juce)
{
    for (int channels : { 1, 2 })
        for (int bits : { 16, 24 })
        {
            const auto b = testSignal (channels, 30011, (uint32_t) (channels * 10 + bits));
            juce::FlacAudioFormat flac;
            const auto theirs = juceWrite (flac, b, 44100.0, bits, false, 5);
            const auto p = pointers (b);
            const auto ours = codec::encodeFlac (p.data(), channels, b.getNumSamples(), 44100.0, bits, 5);
            CHECK (! ours.empty() && theirs == ours);
            std::string error;
            const auto decoded = codec::decodeFile (tempFile ("parity.flac", ours), error);
            REQUIRE (decoded.has_value());
            juce::AudioFormatManager manager;
            manager.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (manager.createReaderFor (mrtest::outputDir().getChildFile ("parity.flac")));
            REQUIRE (reader != nullptr);
            juce::AudioBuffer<float> read (channels, b.getNumSamples());
            reader->read (&read, 0, b.getNumSamples(), 0, true, true);
            bool same = decoded->frames == b.getNumSamples() && decoded->bitDepth == bits;
            for (int c = 0; c < channels && same; ++c)
                same = sameBits (read.getReadPointer (c), decoded->channels[(size_t) c].data(), (size_t) b.getNumSamples());
            CHECK (same);
        }
}

TEST (parity_ogg_matches_juce)
{
    for (int channels : { 1, 2 })
        for (int quality : { 0, 6, 10 })
        {
            const auto b = testSignal (channels, 48007, (uint32_t) (channels * 7 + quality));
            juce::OggVorbisAudioFormat ogg;
            const auto theirs = juceWrite (ogg, b, 48000.0, 32, false, quality);
            const auto p = pointers (b);
            const auto ours = codec::encodeVorbis (p.data(), channels, b.getNumSamples(), 48000.0, quality, 0x494e4954);
            CHECK (! ours.empty() && withoutSerials (theirs) == withoutSerials (ours));
            std::string error;
            const auto decoded = codec::decodeFile (tempFile ("parity.ogg", ours), error);
            REQUIRE (decoded.has_value());
            juce::AudioFormatManager manager;
            manager.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (manager.createReaderFor (mrtest::outputDir().getChildFile ("parity.ogg")));
            REQUIRE (reader != nullptr);
            CHECK_EQ ((int64_t) reader->lengthInSamples, decoded->frames);
            juce::AudioBuffer<float> read (channels, (int) reader->lengthInSamples);
            reader->read (&read, 0, (int) reader->lengthInSamples, 0, true, true);
            bool same = true;
            for (int c = 0; c < channels && same; ++c)
                same = sameBits (read.getReadPointer (c), decoded->channels[(size_t) c].data(), (size_t) decoded->frames);
            CHECK (same);
        }
}
