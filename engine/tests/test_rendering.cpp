#include "TestFramework.h"
#include "TestUtil.h"

#include "analysis/Analysis.h"
#include "io/AudioFile.h"
#include "midi/MidiExport.h"
#include "rendering/OfflineRenderer.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <optional>

using namespace mr;

namespace
{

Composition sineComposition (double bpm = 120.0)
{
    auto r = mrtest::parse (R"({ "tempo": 120, "render": { "tailSeconds": 0.5 },
      "tracks": [ { "name": "Tone", "instrument": "synth.sine",
                    "notes": [ { "pitch": "A4", "startBeat": 0, "durationBeat": 4, "velocity": 100 } ] } ] })");
    auto c = *r.composition;
    c.tempo[0].bpm = bpm;
    return c;
}

RenderOptions options (const std::string& name, int bitDepth = 24, int block = 512)
{
    RenderOptions o;
    o.sampleRate = 48000.0;
    o.bitDepth = bitDepth;
    o.blockSize = block;
    o.useDefaultRegistry = false;
    o.output = mrtest::outputDir().getChildFile (name);
    return o;
}

double zeroCrossingFrequency (const juce::AudioBuffer<float>& b, int from, int to, double sr)
{
    const float* x = b.getReadPointer (0);
    int crossings = 0;
    int first = -1, last = -1;
    for (int i = from + 1; i < to; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            if (first < 0) first = i;
            last = i;
            ++crossings;
        }
    return crossings > 1 ? (crossings - 1) * sr / (last - first) : 0.0;
}

} // namespace

TEST (renders_a_sine_with_exact_length_and_pitch)
{
    const auto c = sineComposition();
    const auto o = options ("sine.wav");
    const auto report = renderComposition (c, o);
    // 4박(2초) + 꼬리 0.5초
    CHECK_EQ (report.stats.samples, (int64_t) 120000);
    CHECK_NEAR (report.stats.durationSeconds, 2.5, 1e-9);

    const auto data = readAudioFile (o.output);
    CHECK_EQ (data.samples, (int64_t) 120000);
    CHECK_NEAR (data.sampleRate, 48000.0, 1e-9);
    CHECK_EQ (data.bitDepth, 24);
    CHECK_EQ (data.buffer.getNumChannels(), 2);
    CHECK_NEAR (zeroCrossingFrequency (data.buffer, 4800, 90000, 48000.0), 440.0, 0.5);
    // 음이 끝난 뒤(2.2초부터)는 조용하다
    CHECK (data.buffer.getMagnitude (0, 105600, 14400) < 1.0e-3f);
    CHECK (data.buffer.getMagnitude (0, 24000, 24000) > 0.1f);
    CHECK_EQ (report.tracks.size(), (size_t) 1);
    CHECK_EQ (report.tracks[0].description, std::string ("synth:sine"));
}

TEST (rendering_is_deterministic_and_block_size_independent)
{
    auto r = mrtest::parse (R"({ "tempo": 96, "performance": { "timingHumanizeMs": 10, "velocityHumanize": 6, "humanizeSeed": 3 },
      "render": { "tailSeconds": 1 },
      "tracks": [
        { "name": "Keys", "instrument": "synth.piano", "notes": [
          { "pitch": "E4", "startBeat": 0, "durationBeat": 1, "velocity": 70 }, { "pitch": "G4", "startBeat": 0.5, "durationBeat": 1, "velocity": 80 },
          { "pitch": "B4", "startBeat": 1, "durationBeat": 2, "velocity": 90 } ] },
        { "name": "Kit", "instrument": "synth.drums", "notes": [
          { "pitch": 36, "startBeat": 0, "durationBeat": 0.5, "velocity": 110 }, { "pitch": 38, "startBeat": 1, "durationBeat": 0.5, "velocity": 100 },
          { "pitch": 42, "startBeat": 1.5, "durationBeat": 0.25, "velocity": 80 } ] },
        { "name": "Pluck", "instrument": "synth.pluck", "notes": [ { "pitch": "E3", "startBeat": 0.25, "durationBeat": 2, "velocity": 90 } ] } ] })");
    REQUIRE (r.composition.has_value());
    const auto a = options ("det_a.wav", 32, 512);
    const auto b = options ("det_b.wav", 32, 512);
    const auto small = options ("det_small.wav", 32, 37);
    renderComposition (*r.composition, a);
    renderComposition (*r.composition, b);
    renderComposition (*r.composition, small);
    juce::MemoryBlock ma, mb, ms;
    a.output.loadFileAsData (ma);
    b.output.loadFileAsData (mb);
    small.output.loadFileAsData (ms);
    CHECK (ma.getSize() > 1000);
    CHECK (ma == mb);
    CHECK (ma == ms);
}

TEST (end_beat_and_fade_out_shape_the_length)
{
    auto c = sineComposition (60.0);
    c.render.endBeat = 2.0;           // 2초
    c.render.tailSeconds = 1.0;
    c.render.fadeOutSeconds = 0.5;
    const auto o = options ("endbeat.wav", 32);
    const auto report = renderComposition (c, o);
    CHECK_EQ (report.stats.samples, (int64_t) 144000);
    bool warned = false;
    for (const auto& w : report.warnings)
        warned = warned || w.path == "$.render.endBeat";
    CHECK (warned);   // 음표(4초)가 endBeat 뒤까지 이어진다
    const auto data = readAudioFile (o.output);
    CHECK (std::abs (data.buffer.getSample (0, (int) data.samples - 1)) < 1.0e-4f);
    CHECK (data.buffer.getMagnitude (0, 96000, 12000) > 0.1f);
}

TEST (wav_bit_depths_round_trip)
{
    juce::AudioBuffer<float> b (2, 4800);
    for (int i = 0; i < 4800; ++i)
    {
        b.setSample (0, i, 0.5f * std::sin (i * 0.05f));
        b.setSample (1, i, -0.25f);
    }
    for (int bits : { 16, 24, 32 })
    {
        const auto f = mrtest::outputDir().getChildFile ("bits" + std::to_string (bits) + ".wav");
        writeWav (f, b, 4800, 44100.0, bits);
        const auto d = readAudioFile (f);
        CHECK_EQ (d.bitDepth, bits);
        CHECK_EQ (d.samples, (int64_t) 4800);
        CHECK_NEAR (d.sampleRate, 44100.0, 1e-9);
        CHECK_NEAR (d.buffer.getSample (1, 100), -0.25, bits == 16 ? 1.0e-4 : 1.0e-6);
        CHECK_EQ (d.floatingPoint, bits == 32);
    }
    bool threw = false;
    try { writeWav (mrtest::outputDir().getChildFile ("bits8.wav"), b, 4800, 44100.0, 8); }
    catch (const Failure&) { threw = true; }
    CHECK (threw);
}

TEST (loudness_and_levels_are_calibrated)
{
    const double sr = 48000.0;
    juce::AudioBuffer<float> b (2, (int) (sr * 5));
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float s = 0.1f * (float) std::sin (2.0 * 3.141592653589793 * 1000.0 * i / sr);
        b.setSample (0, i, s);
        b.setSample (1, i, s);
    }
    const auto st = analyseAudio (b, b.getNumSamples(), sr);
    REQUIRE (st.lufs.has_value());
    CHECK_NEAR (*st.lufs, -20.0, 0.05);           // BS.1770: 양 채널 -20 dBFS 1 kHz 사인 = -20 LUFS
    CHECK_NEAR (st.peakDb, -20.0, 0.01);
    CHECK_NEAR (st.rmsDb, -23.01, 0.02);
    CHECK (! st.clipping);

    juce::AudioBuffer<float> silent (2, 48000);
    silent.clear();
    const auto quiet = analyseAudio (silent, 48000, sr);
    CHECK (! quiet.lufs.has_value());
    CHECK (quiet.peakDb < -1000.0);

    b.setSample (0, 10, 1.0f);
    CHECK (analyseAudio (b, b.getNumSamples(), sr, 1.0f).clippedSamples == 1);
}

TEST (midi_export_writes_tracks_and_notes)
{
    auto r = mrtest::parse (R"({ "title": "Export", "tempo": [ { "beat": 0, "bpm": 80, "ramp": true }, { "beat": 4, "bpm": 60 } ],
      "tracks": [ { "name": "A", "instrument": "synth.sine", "cc11": [ { "beat": 0, "value": 100 }, { "beat": 2, "value": 40 } ], "notes": [
                    { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 70 },
                    { "pitch": 64, "startBeat": 1, "durationBeat": 1, "velocity": 70 } ] },
                  { "name": "B", "instrument": "synth.sine", "notes": [ { "pitch": 48, "startBeat": 0, "durationBeat": 4, "velocity": 70 } ] } ] })");
    REQUIRE (r.composition.has_value());
    const auto f = mrtest::outputDir().getChildFile ("export.mid");
    const auto report = exportMidi (*r.composition, f, false, std::nullopt);
    CHECK_EQ (report.tracks, 2);
    CHECK_EQ (report.notes, 3);
    juce::FileInputStream in (f);
    juce::MidiFile mf;
    REQUIRE (mf.readFrom (in));
    CHECK_EQ (mf.getNumTracks(), 3);
    CHECK_EQ (mf.getTimeFormat(), (short) 960);
    int notes = 0, tempos = 0, cc11 = 0;
    for (int t = 0; t < mf.getNumTracks(); ++t)
        for (const auto* e : *mf.getTrack (t))
        {
            notes += e->message.isNoteOn() ? 1 : 0;
            tempos += e->message.isTempoMetaEvent() ? 1 : 0;
            cc11 += e->message.isController() && e->message.getControllerNumber() == 11 ? 1 : 0;
        }
    CHECK_EQ (notes, 3);
    CHECK (cc11 >= 10);   // 곡선이 32분음표 간격으로 적힌다
    CHECK (tempos >= 16);   // 램프는 16분음표 계단
}

namespace
{

const char* lyricSong = R"({ "tempo": 72, "performance": { "timingHumanizeMs": 12, "humanizeSeed": 5 }, "render": { "tailSeconds": 0.3 },
  "tracks": [ { "name": "Lead", "instrument": "synth.sine", "transpose": 12, "notes": [
                { "pitch": "Eb4", "startBeat": 0, "durationBeat": 1, "velocity": 80, "lyric": "늦" },
                { "pitch": "Ab4", "startBeat": 1, "durationBeat": 0.5, "velocity": 80, "lyric": "은" },
                { "pitch": "Bb4", "startBeat": 1.5, "durationBeat": 0.5, "velocity": 80, "lyric": "-" },
                { "pitch": "C5", "startBeat": 2, "durationBeat": 1, "velocity": 80 },
                { "pitch": "C5", "startBeat": 3, "durationBeat": 1, "velocity": 80, "lyric": "편" } ] },
              { "name": "Pad", "instrument": "synth.sine", "notes": [ { "pitch": 48, "startBeat": 0, "durationBeat": 4, "velocity": 60, "lyric": "아" } ] } ] })";

struct LyricEvent
{
    int track;
    double tick;
    std::string bytes;
    bool beforeItsNote;   // 같은 틱의 음 켜짐보다 앞에 있다
};

std::vector<LyricEvent> lyricEvents (const juce::File& f, int& noteOns)
{
    juce::FileInputStream in (f);
    juce::MidiFile mf;
    std::vector<LyricEvent> out;
    noteOns = 0;
    if (! mf.readFrom (in))
        return out;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        const auto* seq = mf.getTrack (t);
        for (int i = 0; i < seq->getNumEvents(); ++i)
        {
            const auto& m = seq->getEventPointer (i)->message;
            noteOns += m.isNoteOn() ? 1 : 0;
            if (! m.isMetaEvent() || m.getMetaEventType() != 5)
                continue;
            bool before = false;
            for (int j = i + 1; j < seq->getNumEvents() && seq->getEventPointer (j)->message.getTimeStamp() == m.getTimeStamp(); ++j)
                before |= seq->getEventPointer (j)->message.isNoteOn();
            out.push_back ({ t, m.getTimeStamp(), std::string ((const char*) m.getMetaEventData(), (size_t) m.getMetaEventLength()), before });
        }
    }
    return out;
}

double noteOnTick (const juce::File& f, int track, int pitch, double nearTick)
{
    juce::FileInputStream in (f);
    juce::MidiFile mf;
    mf.readFrom (in);
    double best = -1.0;
    for (const auto* e : *mf.getTrack (track))
        if (e->message.isNoteOn() && e->message.getNoteNumber() == pitch
            && (best < 0.0 || std::abs (e->message.getTimeStamp() - nearTick) < std::abs (best - nearTick)))
            best = e->message.getTimeStamp();
    return best;
}

} // namespace

TEST (midi_export_writes_lyrics_at_note_ons)
{
    auto r = mrtest::parse (lyricSong);
    REQUIRE (r.composition.has_value());

    // 박 그대로: 음 켜짐의 틱에 UTF-8 가사, "-" 와 가사 없는 음은 적지 않는다
    const auto f = mrtest::outputDir().getChildFile ("lyrics.mid");
    const auto report = exportMidi (*r.composition, f, false, std::nullopt);
    CHECK_EQ (report.notes, 6);
    CHECK_EQ (report.lyrics, 4);
    int noteOns = 0;
    const auto events = lyricEvents (f, noteOns);
    CHECK_EQ (noteOns, 6);
    REQUIRE (events.size() == 4);
    CHECK_EQ (events[0].bytes, std::string ("\xEB\x8A\xA6"));   // 늦
    CHECK_EQ (events[1].bytes, std::string ("은"));
    CHECK_EQ (events[2].bytes, std::string ("편"));
    CHECK_EQ (events[3].bytes, std::string ("아"));
    CHECK_EQ (events[0].tick, 0.0);
    CHECK_EQ (events[1].tick, 960.0);
    CHECK_EQ (events[2].tick, 2880.0);
    CHECK_EQ (events[2].track, 1);
    CHECK_EQ (events[3].track, 2);
    for (const auto& e : events)
        CHECK (e.beforeItsNote);

    // 연주 처리를 거친 시각: 가사가 옮겨진 음 켜짐과 같은 틱에 있다
    const auto h = mrtest::outputDir().getChildFile ("lyrics_humanized.mid");
    const auto hr = exportMidi (*r.composition, h, true, std::nullopt);
    CHECK_EQ (hr.lyrics, 4);
    const auto moved = lyricEvents (h, noteOns);
    REQUIRE (moved.size() == 4);
    bool anyMoved = false;
    const int pitches[] = { 63 + 12, 68 + 12, 72 + 12 };
    for (int i = 0; i < 3; ++i)
    {
        CHECK_EQ (moved[(size_t) i].bytes, events[(size_t) i].bytes);
        CHECK_EQ (moved[(size_t) i].tick, noteOnTick (h, 1, pitches[i], moved[(size_t) i].tick));
        CHECK (moved[(size_t) i].beforeItsNote);
        anyMoved |= moved[(size_t) i].tick != events[(size_t) i].tick;
    }
    CHECK (anyMoved);
}

TEST (lyrics_do_not_change_the_rendered_sound)
{
    auto with = mrtest::parse (lyricSong);
    REQUIRE (with.composition.has_value());
    auto without = *with.composition;
    for (auto& t : without.tracks)
        for (auto& n : t.notes)
            n.lyric.clear();
    const auto a = options ("lyrics_with.wav", 32);
    const auto b = options ("lyrics_without.wav", 32);
    renderComposition (*with.composition, a);
    renderComposition (without, b);
    juce::MemoryBlock ma, mb;
    a.output.loadFileAsData (ma);
    b.output.loadFileAsData (mb);
    CHECK (ma.getSize() > 1000);
    CHECK (ma == mb);
}

namespace
{

juce::AudioBuffer<float> testTone (int samples, double sr)
{
    juce::AudioBuffer<float> b (2, samples);
    for (int i = 0; i < samples; ++i)
    {
        b.setSample (0, i, 0.4f * (float) std::sin (2.0 * 3.141592653589793 * 440.0 * i / sr));
        b.setSample (1, i, 0.3f * (float) std::sin (2.0 * 3.141592653589793 * 660.0 * i / sr));
    }
    return b;
}

juce::MemoryBlock bytesOf (const juce::File& f)
{
    juce::MemoryBlock m;
    f.loadFileAsData (m);
    return m;
}

bool hasComment (const std::vector<std::string>& comments, const std::string& c)
{
    return std::find (comments.begin(), comments.end(), c) != comments.end();
}

// 사인 하나의 곡 (120 BPM, 한 박 0.5초). 반복은 4박에서 12박.
Composition loopComposition()
{
    auto r = mrtest::parse (R"({ "title": "되풀이 시험", "tempo": 120, "render": { "tailSeconds": 1 },
      "metadata": { "loop": { "startBeat": 4, "endBeat": 12 } },
      "tracks": [ { "name": "Tone", "instrument": "synth.sine", "notes": [
        { "pitch": "A3", "startBeat": 0, "durationBeat": 4, "velocity": 90 }, { "pitch": "C4", "startBeat": 4, "durationBeat": 4, "velocity": 90 },
        { "pitch": "E4", "startBeat": 8, "durationBeat": 4, "velocity": 90 }, { "pitch": "A3", "startBeat": 12, "durationBeat": 2, "velocity": 90 } ] } ] })");
    return *r.composition;
}

} // namespace

TEST (flac_ogg_and_mp3_round_trip)
{
    const double sr = 48000.0;
    const auto tone = testTone (48000, sr);
    const auto ref = analyseAudio (tone, tone.getNumSamples(), sr);
    for (int bits : { 16, 24 })
    {
        const auto f = mrtest::outputDir().getChildFile ("tone" + std::to_string (bits) + ".flac");
        writeAudioFile (f, tone, 0, tone.getNumSamples(), sr, { AudioFileType::flac, bits, -1 });
        const auto d = readAudioFile (f);
        CHECK_EQ (d.samples, (int64_t) 48000);
        CHECK_EQ (d.bitDepth, bits);
        CHECK_NEAR (d.buffer.getSample (0, 1234), tone.getSample (0, 1234), bits == 16 ? 1.0e-4 : 1.0e-6);
        CHECK_NEAR (d.buffer.getSample (1, 4321), tone.getSample (1, 4321), bits == 16 ? 1.0e-4 : 1.0e-6);
    }
    bool threw = false;
    try { writeAudioFile (mrtest::outputDir().getChildFile ("tone32.flac"), tone, 0, 48000, sr, { AudioFileType::flac, 32, -1 }); }
    catch (const Failure&) { threw = true; }
    CHECK (threw);

    const auto ogg = mrtest::outputDir().getChildFile ("tone.ogg");
    writeAudioFile (ogg, tone, 0, tone.getNumSamples(), sr, { AudioFileType::ogg, 24, 6 });
    const auto od = readAudioFile (ogg);
    CHECK_EQ (od.samples, (int64_t) 48000);
    CHECK_NEAR (od.sampleRate, sr, 1e-9);
    const auto os = analyseAudio (od.buffer, (int) od.samples, sr);
    CHECK_NEAR (os.rmsDb, ref.rmsDb, 0.3);

    if (mp3EncoderAvailable())
    {
        const auto mp3 = mrtest::outputDir().getChildFile ("tone.mp3");
        writeAudioFile (mp3, tone, 0, tone.getNumSamples(), sr, { AudioFileType::mp3, 24, 320 });
        CHECK (mp3.getSize() > 30000);   // 1초, 320 kbps 는 40 kB
        CHECK (mp3.getSize() < 50000);
        const auto md = readAudioFile (mp3);
        CHECK_NEAR (md.sampleRate, sr, 1e-9);
        CHECK (std::abs (md.samples - 48000) < 4000);
        const auto ms = analyseAudio (md.buffer, (int) md.samples, sr);
        CHECK_NEAR (ms.peakDb, ref.peakDb, 0.5);
    }
    else
        std::fprintf (stderr, "  (MP3: this build has no LAME, skipped)\n");
}

TEST (encoded_files_are_byte_identical_and_carry_tags)
{
    const double sr = 48000.0;
    const auto tone = testTone (96000, sr);
    AudioFileTags tags;
    tags.title = "칼날 질주";
    tags.loopStart = 24000;
    tags.loopEnd = 72000;
    for (auto type : { AudioFileType::ogg, AudioFileType::flac })
    {
        const std::string ext = type == AudioFileType::ogg ? ".ogg" : ".flac";
        const auto a = mrtest::outputDir().getChildFile ("tags_a" + ext), b = mrtest::outputDir().getChildFile ("tags_b" + ext);
        writeAudioFile (a, tone, 0, 96000, sr, { type, 24, -1 }, tags);
        writeAudioFile (b, tone, 0, 96000, sr, { type, 24, -1 }, tags);
        CHECK (bytesOf (a) == bytesOf (b));
        const auto comments = readVorbisComments (a);
        CHECK (hasComment (comments, "TITLE=칼날 질주"));
        CHECK (hasComment (comments, "LOOPSTART=24000"));
        CHECK (hasComment (comments, "LOOPLENGTH=48000"));
        const auto d = readAudioFile (a);   // 다시 쓴 머리(CRC, 차례)를 디코더가 받는다
        CHECK_EQ (d.samples, (int64_t) 96000);
        CHECK (d.buffer.getMagnitude (0, 90000, 4000) > 0.2f);
    }
    // 주석이 길어 쪽을 여럿 넘겨도 읽힌다
    AudioFileTags longTags;
    longTags.title = std::string (70000, 'x');
    const auto longOgg = mrtest::outputDir().getChildFile ("tags_long.ogg");
    writeAudioFile (longOgg, tone, 0, 96000, sr, { AudioFileType::ogg, 24, 4 }, longTags);
    CHECK (hasComment (readVorbisComments (longOgg), "TITLE=" + longTags.title));
    CHECK_EQ (readAudioFile (longOgg).samples, (int64_t) 96000);

    if (mp3EncoderAvailable())
    {
        const auto a = mrtest::outputDir().getChildFile ("tags_a.mp3"), b = mrtest::outputDir().getChildFile ("tags_b.mp3");
        AudioFileTags titled;
        titled.title = tags.title;
        writeAudioFile (a, tone, 0, 96000, sr, { AudioFileType::mp3, 24, 192 }, titled);
        writeAudioFile (b, tone, 0, 96000, sr, { AudioFileType::mp3, 24, 192 }, titled);
        const auto bytes = bytesOf (a);
        CHECK (bytes == bytesOf (b));
        REQUIRE (bytes.getSize() > 10);
        CHECK (std::memcmp (bytes.getData(), "ID3\x04", 4) == 0);   // ID3v2.4
        bool found = false;
        for (size_t i = 0; i + 13 < bytes.getSize() && ! found; ++i)
            found = std::memcmp ((const char*) bytes.getData() + i, "칼날 질주", 13) == 0;
        CHECK (found);
        bool threw = false;
        try { writeAudioFile (a, tone, 0, 96000, sr, { AudioFileType::mp3, 24, 320 }, tags); }
        catch (const Failure&) { threw = true; }
        CHECK (threw);   // MP3 는 반복 구간을 받지 않는다
        threw = false;
        try { writeAudioFile (a, tone, 0, 96000, 96000.0, { AudioFileType::mp3, 24, 320 }); }
        catch (const Failure&) { threw = true; }
        CHECK (threw);
    }
}

TEST (wav_loop_writes_a_smpl_chunk)
{
    const double sr = 44100.0;
    const auto tone = testTone (44100, sr);
    AudioFileTags tags;
    tags.loopStart = 1000;
    tags.loopEnd = 40000;
    const auto f = mrtest::outputDir().getChildFile ("loop_smpl.wav");
    writeAudioFile (f, tone, 0, 44100, sr, { AudioFileType::wav, 24, -1 }, tags);
    // 청크를 직접 읽는다: smpl 의 샘플 주기, 반복 수, 반복 시작과 끝, data 의 크기
    juce::MemoryBlock bytes;
    REQUIRE (f.loadFileAsData (bytes));
    const auto* d = static_cast<const uint8_t*> (bytes.getData());
    auto le32 = [d] (size_t at) { return (uint32_t) d[at] | ((uint32_t) d[at + 1] << 8) | ((uint32_t) d[at + 2] << 16) | ((uint32_t) d[at + 3] << 24); };
    std::optional<size_t> smpl, data;
    uint32_t dataSize = 0;
    for (size_t pos = 12; pos + 8 <= bytes.getSize(); pos += 8 + le32 (pos + 4) + (le32 (pos + 4) & 1))
    {
        if (std::memcmp (d + pos, "smpl", 4) == 0)
            smpl = pos + 8;
        if (std::memcmp (d + pos, "data", 4) == 0)
        {
            data = pos + 8;
            dataSize = le32 (pos + 4);
        }
    }
    REQUIRE (smpl.has_value() && data.has_value());
    CHECK_EQ (le32 (*smpl + 8), (uint32_t) 22676);    // 샘플 주기 (ns)
    CHECK_EQ (le32 (*smpl + 28), (uint32_t) 1);       // 반복 수
    CHECK_EQ (le32 (*smpl + 36 + 8), (uint32_t) 1000);
    CHECK_EQ (le32 (*smpl + 36 + 12), (uint32_t) 39999);
    CHECK_EQ (dataSize, (uint32_t) (44100 * 2 * 3));
}

TEST (loop_export_cuts_at_the_loop_end_and_crossfades_the_seam)
{
    const auto c = loopComposition();
    const auto full = options ("loop_full.wav", 32);
    renderComposition (c, full);
    auto o = options ("loop_cut.ogg", 32);
    o.fileType = AudioFileType::ogg;
    o.loop = true;
    const auto report = renderComposition (c, o);
    REQUIRE (report.loop.applied);
    CHECK_EQ (report.loop.startSample, (int64_t) 96000);    // 4박 = 2초
    CHECK_EQ (report.loop.endSample, (int64_t) 288000);     // 12박 = 6초
    CHECK_EQ (report.loop.crossfadeSamples, 960);           // 20ms
    CHECK_EQ (report.stats.samples, (int64_t) 288000);
    CHECK_EQ (report.format, std::string ("ogg"));
    REQUIRE (report.loop.seamDb.has_value());
    CHECK (*report.loop.seamDb > -10.0);   // 반복 끝 앞(E4)과 시작 앞(A3)은 다른 음이다
    const auto comments = readVorbisComments (o.output);
    CHECK (hasComment (comments, "LOOPSTART=96000"));
    CHECK (hasComment (comments, "LOOPLENGTH=192000"));
    CHECK (hasComment (comments, "TITLE=되풀이 시험"));
    CHECK_EQ (readAudioFile (o.output).samples, (int64_t) 288000);

    auto w = options ("loop_cut.wav", 32);
    w.loop = true;
    renderComposition (c, w);
    const auto whole = readAudioFile (full.output), cut = readAudioFile (w.output);
    REQUIRE (cut.samples == 288000);
    // 크로스페이드 앞은 곡 전체와 같고, 마지막 샘플은 반복 시작 바로 앞의 샘플이다
    CHECK_EQ (cut.buffer.getSample (0, 200000), whole.buffer.getSample (0, 200000));
    CHECK_EQ (cut.buffer.getSample (1, 287000), whole.buffer.getSample (1, 287000));
    CHECK_NEAR (cut.buffer.getSample (0, 287999), whole.buffer.getSample (0, 95999), 1e-7);
    const float w0 = 480.0f / 960.0f;
    CHECK_NEAR (cut.buffer.getSample (0, 287519), whole.buffer.getSample (0, 287519) * (1.0f - w0) + whole.buffer.getSample (0, 95519) * w0, 1e-6);

    auto noLoop = sineComposition();
    auto bad = options ("loop_none.wav", 32);
    bad.loop = true;
    bool threw = false;
    try { renderComposition (noLoop, bad); }
    catch (const Failure&) { threw = true; }
    CHECK (threw);
}

TEST (range_export_keeps_the_song_level_and_fades_after_the_end)
{
    const auto c = loopComposition();
    const auto full = options ("range_full.wav", 32);
    const auto fullReport = renderComposition (c, full);
    auto o = options ("range_cut.wav", 32);
    o.range = std::make_pair (2.0, 6.0);     // 1초에서 3초
    o.rangeFadeSeconds = 0.5;
    const auto report = renderComposition (c, o);
    REQUIRE (report.range.applied);
    CHECK_NEAR (report.range.startSeconds, 1.0, 1e-12);
    CHECK_NEAR (report.range.endSeconds, 3.0, 1e-12);
    CHECK_NEAR (report.range.fadeSeconds, 0.5, 1e-12);
    CHECK_EQ (report.stats.samples, (int64_t) 120000);   // 2초 + 0.5초
    const auto whole = readAudioFile (full.output), cut = readAudioFile (o.output);
    CHECK_EQ (cut.buffer.getSample (0, 1000), whole.buffer.getSample (0, 49000));     // 페이드인(10ms) 뒤는 같다
    CHECK_EQ (cut.buffer.getSample (1, 90000), whole.buffer.getSample (1, 138000));
    CHECK (std::abs (cut.buffer.getSample (0, 0)) <= std::abs (whole.buffer.getSample (0, 48000)));
    CHECK (std::abs (cut.buffer.getSample (0, 119999)) < 1.0e-4f);
    CHECK (cut.buffer.getMagnitude (0, 96000, 4800) > 0.05f);   // 페이드 첫머리는 아직 들린다
    CHECK_NEAR (report.normalisation.gainDb, fullReport.normalisation.gainDb, 1e-12);

    // 곡 끝을 넘는 구간은 곡 끝에서 자른다
    auto past = options ("range_past.wav", 32);
    past.range = std::make_pair (12.0, 40.0);
    const auto pastReport = renderComposition (c, past);
    CHECK_EQ (pastReport.stats.samples, fullReport.stats.samples - 288000);
    CHECK_NEAR (pastReport.range.fadeSeconds, 0.0, 1e-12);
}

TEST (split_render_matches_one_call_and_reports_progress)
{
    const auto c = loopComposition();
    const auto a = options ("split_a.wav", 32);
    renderComposition (c, a);
    auto b = options ("split_b.wav", 32);
    std::atomic<float> progress { -1.0f };
    b.progress = &progress;
    OfflineRender render (c, b);
    CHECK (progress.load() < 0.0f);   // 만들기는 진행을 바꾸지 않는다
    render.run();
    CHECK_NEAR (progress.load(), 1.0, 1e-9);
    CHECK (bytesOf (a.output) == bytesOf (b.output));
    bool threw = false;
    try { render.run(); }
    catch (const Failure&) { threw = true; }
    CHECK (threw);
}
