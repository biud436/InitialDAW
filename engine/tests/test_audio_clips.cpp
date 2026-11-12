#include "TestFramework.h"
#include "TestUtil.h"

#include "analysis/NoteSegments.h"
#include "analysis/Onsets.h"
#include "analysis/PitchTrack.h"
#include "composition/Arrange.h"
#include "composition/Document.h"
#include "core/Random.h"
#include "instruments/AudioClips.h"
#include "io/AudioFile.h"
#include "realtime/RealtimePlayer.h"
#include "rendering/OfflineRenderer.h"

#include <cmath>

using namespace mr;

namespace
{

constexpr double kRate = 48000.0;

// 44.1 kHz 의 440 Hz 사인파 (크기 0.5), 두 번째 초는 880 Hz
juce::File sineFile (const char* name, double rate = 44100.0)
{
    const auto file = mrtest::outputDir().getChildFile (name);
    const int n = (int) (2.0 * rate);
    juce::AudioBuffer<float> b (1, n);
    for (int i = 0; i < n; ++i)
    {
        const double f = i < (int) rate ? 440.0 : 880.0;
        b.setSample (0, i, (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * f * i / rate)));
    }
    writeWav (file, b, n, rate, 32);
    return file;
}

std::string json (const std::string& clips, const std::string& extra = {})
{
    return R"({ "tempo": 120, "render": { "tailSeconds": 0.5 }, "tracks": [ { "name": "Audio", "instrument": "audio", "clips": [ )" + clips
           + R"( ] }, { "name": "Keys", "instrument": "synth.piano", "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 0.25, "velocity": 1 } ] } ])"
           + extra + " }";
}

std::string clip (const juce::File& file, double startBeat, const std::string& rest = {})
{
    return R"({ "file": ")" + file.getFullPathName().toStdString() + R"(", "startBeat": )" + juce::String (startBeat).toStdString() + rest + " }";
}

Composition composition (const std::string& text)
{
    auto r = mrtest::parse (text);
    if (! r.composition)
        for (const auto& e : r.diagnostics.errors)
            std::fprintf (stderr, "    parse error %s: %s\n", e.path.c_str(), e.message.c_str());
    return *r.composition;
}

juce::AudioBuffer<float> offline (const Composition& c, const std::string& out, RenderReport* report = nullptr)
{
    RenderOptions o;
    o.useDefaultRegistry = false;
    o.bitDepth = 32;
    o.output = mrtest::outputDir().getChildFile (out);
    const auto r = renderComposition (c, o);
    if (report != nullptr)
        *report = r;
    return readAudioFile (o.output).buffer;
}

PlayerOptions playerOptions()
{
    PlayerOptions o;
    o.useDefaultRegistry = false;
    o.sampleRate = kRate;
    o.blockSize = 512;
    return o;
}

juce::AudioBuffer<float> pull (RealtimePlayer& p, double seconds, int callbackSize = 300)
{
    const int total = (int) std::llround (seconds * kRate);
    juce::AudioBuffer<float> out (2, total);
    out.clear();
    for (int done = 0; done < total;)
    {
        const int n = std::min (callbackSize, total - done);
        float* ptrs[2] = { out.getWritePointer (0, done), out.getWritePointer (1, done) };
        p.process (ptrs, 2, n);
        done += n;
    }
    return out;
}

float rms (const juce::AudioBuffer<float>& b, double fromSec, double toSec)
{
    const int from = (int) (fromSec * kRate), count = (int) ((toSec - fromSec) * kRate);
    return b.getRMSLevel (0, from, count);
}

double frequency (const juce::AudioBuffer<float>& b, double fromSec, double toSec)
{
    const int from = (int) (fromSec * kRate), to = (int) (toSec * kRate);
    int crossings = 0, first = -1, last = -1;
    for (int i = from + 1; i < to; ++i)
        if (b.getSample (0, i - 1) < 0.0f && b.getSample (0, i) >= 0.0f)
        {
            if (first < 0) first = i;
            last = i;
            ++crossings;
        }
    return crossings > 1 ? (crossings - 1) * kRate / (last - first) : 0.0;
}

double maxDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int offsetB = 0, int from = 0)
{
    double d = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = from; i < a.getNumSamples() && i + offsetB < b.getNumSamples(); ++i)
            d = std::max (d, (double) std::abs (a.getSample (ch, i) - b.getSample (ch, i + offsetB)));
    return d;
}

} // namespace

TEST (audio_clip_plays_its_part_of_the_file_at_its_beat)
{
    const auto file = sineFile ("clip_sine.wav");
    // 120 BPM: 2박이 1초. 파일의 0.25초부터 0.5초를 -6 dB 로
    const auto c = composition (json (clip (file, 2, R"(, "offsetSeconds": 0.25, "lengthSeconds": 0.5, "gainDb": -6)")));
    REQUIRE (c.tracks[0].clips.size() == 1u);
    RenderReport report;
    const auto out = offline (c, "clip_render.wav", &report);
    CHECK (rms (out, 0.3, 0.99) < 1.0e-4f);   // 앞은 조용하다 (피아노는 벨로시티 1)
    const float expected = (float) (0.5 * 0.5011872 / std::sqrt (2.0));
    CHECK_NEAR (rms (out, 1.02, 1.48), expected, 0.01);
    CHECK (rms (out, 1.51, 1.9) < 1.0e-4f);
    CHECK_NEAR (frequency (out, 1.05, 1.45), 440.0, 0.5);   // 44.1 kHz 에서 48 kHz 로 바꿔도 높이가 같다
    CHECK (out.getNumSamples() >= (int) (1.5 * kRate) && out.getNumSamples() < (int) (2.2 * kRate));   // 클립 끝과 꼬리

    // 파일 끝까지 (길이가 없으면), 둘째 초의 880 Hz 가 1.75초 뒤에
    const auto whole = offline (composition (json (clip (file, 2, R"(, "offsetSeconds": 0.25)"))), "clip_whole.wav");
    CHECK_NEAR (frequency (whole, 1.8, 2.6), 880.0, 1.0);
    CHECK_NEAR (estimateDurationSeconds (composition (json (clip (file, 2, R"(, "offsetSeconds": 0.25)")))), 1.0 + 1.75 + 0.5, 1.0e-3);
}

TEST (audio_clip_fades_and_track_chain)
{
    const auto file = sineFile ("clip_fade.wav");
    const auto c = composition (json (clip (file, 0, R"(, "lengthSeconds": 1, "fadeInSeconds": 0.5, "fadeOutSeconds": 0.25)")));
    const auto out = offline (c, "clip_fade_render.wav");
    CHECK (rms (out, 0.0, 0.1) < rms (out, 0.4, 0.5));        // 커진다
    CHECK_NEAR (rms (out, 0.55, 0.7), 0.5f / std::sqrt (2.0f), 0.01);
    CHECK (rms (out, 0.9, 1.0) < rms (out, 0.6, 0.7) * 0.5f);   // 줄어든다
    // 트랙의 페이더도 지난다
    auto text = json (clip (file, 0, R"(, "lengthSeconds": 1)"));
    const std::string from = R"("instrument": "audio",)";
    text.replace (text.find (from), from.size(), R"("instrument": "audio", "mixer": { "gainDb": -12 },)");
    const auto quiet = offline (composition (text), "clip_fader.wav");
    CHECK_NEAR (rms (quiet, 0.2, 0.8), 0.5f / std::sqrt (2.0f) * 0.2511886f, 0.01);
}

TEST (audio_clip_realtime_matches_offline_and_is_silent_when_stopped)
{
    const auto file = sineFile ("clip_rt.wav");
    const auto c = composition (json (clip (file, 1, R"(, "lengthSeconds": 1.2, "gainDb": -3)") + ", " + clip (file, 4, R"(, "offsetSeconds": 1)")));
    const auto reference = offline (c, "clip_rt_reference.wav");
    RealtimePlayer player (c, playerOptions());
    CHECK_EQ (player.length(), (int64_t) reference.getNumSamples());
    player.play();
    const auto live = pull (player, (double) reference.getNumSamples() / kRate);
    CHECK (rms (reference, 0.6, 1.6) > 0.1f);
    CHECK (maxDiff (live, reference) < 1.0e-5);

    // 멈춘 동안은 조용하다
    RealtimePlayer stopped (c, playerOptions());
    stopped.seekSeconds (1.0);
    const auto silence = pull (stopped, 0.3);
    CHECK (silence.getMagnitude (0, silence.getNumSamples()) < 1.0e-6f);

    // 클립 가운데로 옮기면 그 자리부터 같은 소리 (처음 64 샘플은 짧게 키운다)
    RealtimePlayer seek (c, playerOptions());
    seek.seekSeconds (1.0);
    seek.play();
    const auto after = pull (seek, 0.5);
    CHECK (maxDiff (after, reference, (int) kRate, 64) < 1.0e-5);
}

TEST (audio_clip_edits_apply_while_playing_like_a_new_player)
{
    const auto file = sineFile ("clip_apply.wav");
    const auto before = composition (json (clip (file, 0, R"(, "lengthSeconds": 1)")));
    const auto after = composition (json (clip (file, 1, R"(, "offsetSeconds": 0.5, "lengthSeconds": 0.75, "gainDb": -6)")));
    RealtimePlayer fresh (after, playerOptions());
    RealtimePlayer applied (before, playerOptions());
    CHECK (applied.apply (after));
    fresh.play();
    applied.play();
    const auto a = pull (fresh, 2.0), b = pull (applied, 2.0);
    CHECK (rms (a, 0.6, 1.2) > 0.1f);
    CHECK_EQ (maxDiff (a, b), 0.0);
}

TEST (audio_clip_missing_file_is_a_warning_and_silence)
{
    const auto missing = mrtest::outputDir().getChildFile ("no_such_clip.wav");
    missing.deleteFile();
    const auto c = composition (json (clip (missing, 0, R"(, "lengthSeconds": 1)")));
    RenderReport report;
    const auto out = offline (c, "clip_missing.wav", &report);
    bool warned = false;
    for (const auto& w : report.warnings)
        warned = warned || (w.path.find (".clips") != std::string::npos && w.message.find ("not found") != std::string::npos);
    CHECK (warned);
    CHECK (rms (out, 0.1, 0.9) < 1.0e-4f);
}

TEST (audio_clip_reads_flac_ogg_and_mp3)
{
    const auto wav = sineFile ("clip_formats.wav", 48000.0);
    const auto data = readAudioFile (wav);
    for (const auto* ext : { "flac", "ogg", "mp3" })
    {
        const auto file = mrtest::outputDir().getChildFile (juce::String ("clip_formats.") + ext);
        AudioFileFormat format;
        format.type = *audioFileTypeFor (file);
        format.bitDepth = 24;
        if (format.type == AudioFileType::mp3 && ! mp3EncoderAvailable())
            continue;
        writeAudioFile (file, data.buffer, 0, data.buffer.getNumSamples(), 48000.0, format);
        const auto info = audioFileInfo (file);
        REQUIRE (info.has_value());
        CHECK_NEAR (info->seconds(), 2.0, 0.06);
        std::string error;
        const auto loaded = loadClipAudio (file, kRate, error);
        REQUIRE (loaded != nullptr);
        CHECK (isAudioFileName (file.getFileName()));
        const auto out = offline (composition (json (clip (file, 0, R"(, "lengthSeconds": 1)"))), std::string ("clip_formats_") + ext + ".wav");
        CHECK_NEAR (frequency (out, 0.2, 0.8), 440.0, 1.0);
    }
    // 같은 파일과 샘플레이트는 한 번만 읽는다
    std::string error;
    CHECK (loadClipAudio (wav, kRate, error) == loadClipAudio (wav, kRate, error));
    const auto peaks = clipPeaks (wav);
    REQUIRE (peaks != nullptr);
    CHECK_EQ (peaks->maximum.size(), (size_t) (96000 / peaks->bucketFrames + (96000 % peaks->bucketFrames != 0 ? 1 : 0)));
    CHECK_NEAR (peaks->maximum[100], 0.5f, 0.01);
}

TEST (audio_clip_parse_checks)
{
    auto r = mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "A", "instrument": "audio", "clips": [ { "startBeat": 0 }, { "file": "x.wav", "startBeat": -1 },
                               { "file": "x.wav", "startBeat": 0, "lengthSeconds": 0 }, { "file": "x.wav", "startBeat": 0, "gainDb": 40, "loop": true } ] } ] })");
    CHECK (mrtest::hasError (r.diagnostics, "$.tracks[0].clips[0].file"));
    CHECK (mrtest::hasError (r.diagnostics, "$.tracks[0].clips[1].startBeat"));
    CHECK (mrtest::hasError (r.diagnostics, "$.tracks[0].clips[2].lengthSeconds"));
    CHECK (mrtest::hasError (r.diagnostics, "$.tracks[0].clips[3].gainDb"));
    CHECK (mrtest::hasWarning (r.diagnostics, "$.tracks[0].clips[3].loop"));
    r = mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "A", "instrument": "audio", "clips": [ { "file": "x.wav", "startBeat": 4 } ] } ] })");
    CHECK (r.composition.has_value() && r.diagnostics.warnings.empty());
}

TEST (audio_clip_arrange_edits_split_clips_at_the_edges)
{
    // 120 BPM: 한 박이 0.5초. 클립은 4박(2초)에서 3초 동안, 파일의 0.5초부터
    const auto file = sineFile ("clip_arrange.wav");
    juce::var root;
    juce::JSON::parse (juce::String (json (clip (file, 4, R"(, "offsetSeconds": 0.5, "lengthSeconds": 3, "fadeInSeconds": 0.1, "fadeOutSeconds": 0.2)"))),
                       root);
    auto clipsOf = [] (const juce::var& r)
    {
        std::vector<std::tuple<double, double, double>> out;   // 시작 박, 파일 안의 시작, 길이
        for (const auto& c : *r["tracks"][0]["clips"].getArray())
            out.push_back ({ (double) c["startBeat"], (double) c["offsetSeconds"], (double) c["lengthSeconds"] });
        return out;
    };
    using T = std::vector<std::tuple<double, double, double>>;

    // 6박(3초)에 두 박 넣기: 앞 부분은 4~6박(1초), 뒤 부분은 8박에서 파일의 1.5초부터 2초
    auto inserted = withTimeInserted (root, 6, 2);
    CHECK (clipsOf (inserted) == (T { { 4.0, 0.5, 1.0 }, { 8.0, 1.5, 2.0 } }));
    CHECK (inserted["tracks"][0]["clips"][0].getDynamicObject()->hasProperty ("fadeInSeconds")
           && ! inserted["tracks"][0]["clips"][0].getDynamicObject()->hasProperty ("fadeOutSeconds"));
    CHECK (! inserted["tracks"][0]["clips"][1].getDynamicObject()->hasProperty ("fadeInSeconds"));
    // 넣은 자리를 지우면 두 부분이 다시 이어진다 (소리는 같다)
    const auto back = withTimeDeleted (inserted, 6, 8);
    CHECK (clipsOf (back) == (T { { 4.0, 0.5, 1.0 }, { 6.0, 1.5, 2.0 } }));
    const auto a = offline (composition (juce::JSON::toString (root).toStdString()), "clip_arrange_a.wav");
    const auto b = offline (composition (juce::JSON::toString (back).toStdString()), "clip_arrange_b.wav");
    CHECK (rms (b, 2.05, 3.45) > 0.3f);   // 파일(2초)의 0.5초부터 끝까지
    // 이음매 주변의 페이드(32 샘플씩) 말고는 같다
    double far = 0.0;
    for (int i = 0; i < a.getNumSamples(); ++i)
        if (std::abs (i - (int) (3.0 * kRate)) > 40)
            far = std::max (far, (double) std::abs (a.getSample (0, i) - b.getSample (0, i)));
    CHECK (far < 1.0e-5);

    // 내용 지우기 [5, 7): 앞 4~5박과 뒤 7박부터
    const auto cleared = withRangeCleared (root, { 0 }, 5, 7);
    CHECK (clipsOf (cleared) == (T { { 4.0, 0.5, 0.5 }, { 7.0, 2.0, 1.5 } }));
    // 복사 [6, 8) 을 12박에 붙이기: 파일의 1.5초부터 1초
    const auto copied = copyRange (root, { 0 }, 6, 8);
    REQUIRE (copied.tracks.size() == 1u && copied.tracks[0].clips.size() == 1);
    const auto pasted = withClipPasted (root, copied, 0, 12);
    CHECK (clipsOf (pasted) == (T { { 4.0, 0.5, 3.0 }, { 12.0, 1.5, 1.0 } }));
    // 길이가 없는 클립은 곡 폴더로 파일 길이를 읽는다
    juce::var open;
    juce::JSON::parse (juce::String (json (clip (file, 0))), open);
    const auto split = withRangeCleared (open, { 0 }, 2, 3, file.getParentDirectory());
    REQUIRE (split["tracks"][0]["clips"].getArray()->size() == 2);
    CHECK_NEAR ((double) split["tracks"][0]["clips"][0]["lengthSeconds"], 1.0, 1.0e-6);
    CHECK_NEAR ((double) split["tracks"][0]["clips"][1]["offsetSeconds"], 1.5, 1.0e-6);
    CHECK (! split["tracks"][0]["clips"][1].getDynamicObject()->hasProperty ("lengthSeconds"));   // 파일 끝까지
    // 파일 끝 뒤의 지우기는 클립을 나누지 않는다
    CHECK_EQ (withRangeCleared (open, { 0 }, 6, 8, file.getParentDirectory())["tracks"][0]["clips"].getArray()->size(), 1);
}

TEST (audio_clip_changes_pitch_and_stretches)
{
    if (! clipStretchAvailable())
        return;
    const auto file = sineFile ("clip_stretch.wav");   // 440 Hz, 둘째 초는 880 Hz
    // 음높이 +12: 길이는 그대로 (2 박부터 1초), 높이는 두 배
    auto c = composition (json (clip (file, 2, R"(, "lengthSeconds": 1, "pitchSemitones": 12)")));
    auto out = offline (c, "clip_pitch.wav");
    std::printf ("    pitch +12: %.1f Hz, rms %.3f\n", frequency (out, 1.2, 1.8), rms (out, 1.2, 1.8));
    CHECK_NEAR (frequency (out, 1.2, 1.8), 880.0, 4.0);
    CHECK (rms (out, 1.2, 1.8) > 0.2f);
    CHECK (rms (out, 2.1, 2.4) < 1.0e-3f);
    // 늘이기 2: 0.5초가 1초로, 높이는 그대로
    c = composition (json (clip (file, 2, R"(, "lengthSeconds": 0.5, "stretch": 2)")));
    out = offline (c, "clip_stretch2.wav");
    std::printf ("    stretch 2: %.1f Hz, rms %.3f\n", frequency (out, 1.2, 1.8), rms (out, 1.2, 1.8));
    CHECK_NEAR (frequency (out, 1.2, 1.8), 440.0, 4.0);
    CHECK (rms (out, 1.3, 1.7) > 0.2f);
    CHECK (rms (out, 2.1, 2.4) < 1.0e-3f);
    CHECK_NEAR (clipEndSeconds (c.tracks[0].clips[0], TempoMap (c.tempo), juce::File()), 2.0, 1.0e-9);
    // 같은 곡을 다시 렌더링하면 같은 샘플 (씨앗이 고정)
    CHECK (maxDiff (out, offline (c, "clip_stretch2_again.wav")) == 0.0);
}

TEST (audio_clip_follows_the_tempo)
{
    if (! clipStretchAvailable())
        return;
    const auto file = sineFile ("clip_follow.wav");
    // 파일의 템포 60: 파일 2초가 2박. 곡은 0 박에서 120 BPM, 1 박부터 60 BPM 이라 0.5초 + 1초 = 1.5초에 끝난다
    auto r = mrtest::parse (R"({ "tempo": [ { "beat": 0, "bpm": 120 }, { "beat": 1, "bpm": 60 } ], "render": { "tailSeconds": 0.5 },
        "tracks": [ { "name": "Audio", "instrument": "audio", "clips": [ )" + clip (file, 0, R"(, "sourceBpm": 60)") + " ] } ] }");
    REQUIRE (r.composition.has_value());
    const auto& c = *r.composition;
    const TempoMap tempo (c.tempo);
    const auto& spec = c.tracks[0].clips[0];
    CHECK_NEAR (clipEndSeconds (spec, tempo, mrtest::outputDir()), 1.5, 1.0e-6);
    CHECK_NEAR (clipSourceAt (spec, tempo, 0.5), 1.0, 1.0e-9);   // 1 박 = 파일의 1초
    CHECK_NEAR (clipSongSecondsAt (spec, tempo, clipSourceAt (spec, tempo, 1.2)), 1.2, 1.0e-9);
    const auto out = offline (c, "clip_follow_render.wav");
    std::printf ("    follow: %.1f Hz then %.1f Hz\n", frequency (out, 0.05, 0.45), frequency (out, 0.7, 1.3));
    CHECK_NEAR (frequency (out, 0.05, 0.45), 440.0, 5.0);   // 빠른 템포에서 줄여도 높이는 그대로
    CHECK_NEAR (frequency (out, 0.7, 1.3), 880.0, 5.0);     // 파일의 둘째 초가 1 박부터
    CHECK (rms (out, 1.6, 1.9) < 1.0e-3f);
    // 실시간 재생이 오프라인과 같다 (0 박의 클립은 재생을 시작한 블록의 처음 64 샘플을 키운다)
    RealtimePlayer player (c, playerOptions());
    player.play();
    const auto live = pull (player, (double) out.getNumSamples() / kRate);
    CHECK (maxDiff (live, out, 0, 64) < 1.0e-5);
    // 시간 넣기는 클립을 파일의 박으로 나눈다: 0.5 박에 1 박을 넣으면 뒤쪽은 파일의 0.5초부터 1.5초
    auto root = juce::JSON::parse (R"({ "tempo": 60, "tracks": [ { "name": "Audio", "instrument": "audio", "clips": [ { "file": "x.wav", "startBeat": 0,
        "lengthSeconds": 2, "sourceBpm": 120 } ] } ] })");
    const auto edited = withTimeInserted (root, 0.5, 1.0, juce::File());
    const auto* clips = edited["tracks"][0]["clips"].getArray();
    REQUIRE (clips != nullptr && clips->size() == 2);
    CHECK_NEAR ((double) (*clips)[0]["lengthSeconds"], 0.25, 1.0e-9);   // 120 BPM 파일의 0.5 박
    CHECK_NEAR ((double) (*clips)[1]["startBeat"], 1.5, 1.0e-9);
    CHECK_NEAR ((double) (*clips)[1]["offsetSeconds"], 0.25, 1.0e-9);
    CHECK_NEAR ((double) (*clips)[1]["lengthSeconds"], 1.75, 1.0e-9);
}

TEST (audio_clip_warp_markers_map_file_seconds_to_beats)
{
    // 파일 0~1초가 1 박, 1~2초가 2 박에 놓인다. 마커 밖은 끝 쪽 마커 사이의 빠르기 (2 박/초)
    AudioClip clip;
    clip.warp = { { 0.0, 0.0 }, { 1.0, 1.0 }, { 2.0, 3.0 } };
    const TempoMap tempo ({ { 0.0, 120.0, false } });
    CHECK_NEAR (clipSongSecondsAt (clip, tempo, 1.0), 0.5, 1.0e-9);
    CHECK_NEAR (clipSongSecondsAt (clip, tempo, 1.5), 1.0, 1.0e-9);
    CHECK_NEAR (clipSongSecondsAt (clip, tempo, 2.5), 2.0, 1.0e-9);
    CHECK_NEAR (clipSourceAt (clip, tempo, 1.0), 1.5, 1.0e-9);
    CHECK_NEAR (clipSourceAt (clip, tempo, 0.25), 0.5, 1.0e-9);
    // 앞을 잘라도 (offsetSeconds 와 startBeat 를 함께 옮기면) 같은 자리에서 같은 소리
    auto trimmed = clip;
    trimmed.offsetSeconds = 1.5;
    trimmed.startBeat = 2.0;
    for (const double s : { 1.6, 1.9, 2.4 })
        CHECK_NEAR (clipSongSecondsAt (trimmed, tempo, s), clipSongSecondsAt (clip, tempo, s), 1.0e-9);
    // 곡의 템포를 따른다: 60 BPM 이면 박이 두 배 길다
    const TempoMap slow ({ { 0.0, 60.0, false } });
    CHECK_NEAR (clipSongSecondsAt (clip, slow, 1.5), 2.0, 1.0e-9);
    // 마커 밖의 빠르기는 sourceBpm 이 정한다
    clip.sourceBpm = 30.0;
    CHECK_NEAR (clipSongSecondsAt (clip, tempo, 3.0), (3.0 + 0.5) * 0.5, 1.0e-9);
    // 검사: 둘보다 적거나 오르지 않는 마커는 오류
    CHECK (! mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "A", "instrument": "audio", "clips": [ { "file": "x.wav", "startBeat": 0,
        "warp": [ { "seconds": 0, "beat": 0 } ] } ] } ] })").composition);
    CHECK (! mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "A", "instrument": "audio", "clips": [ { "file": "x.wav", "startBeat": 0,
        "warp": [ { "seconds": 0, "beat": 0 }, { "seconds": 1, "beat": 0 } ] } ] } ] })").composition);
    CHECK (! mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "A", "instrument": "audio", "clips": [ { "file": "x.wav", "startBeat": 0,
        "stretch": 2, "warp": [ { "seconds": 0, "beat": 0 }, { "seconds": 1, "beat": 1 } ] } ] } ] })").composition);
}

TEST (audio_clip_warp_marker_edits_keep_the_rest_in_place)
{
    const TempoMap tempo ({ { 0.0, 120.0, false } });
    // 늘이지 않은 클립 (2 박에서 파일 0.5초부터 2초): 마커를 더해도 소리 자리는 그대로
    AudioClip clip;
    clip.startBeat = 2.0;
    clip.offsetSeconds = 0.5;
    auto added = withWarpMarkerAdded (clip, tempo, 2.0, 1.0);
    REQUIRE (added.warp.size() == 3);
    for (const double s : { 0.6, 1.0, 2.2 })
        CHECK_NEAR (clipSongSecondsAt (added, tempo, s), clipSongSecondsAt (clip, tempo, s), 1.0e-9);
    CHECK_NEAR (warpMarkerSongBeat (added, 1), 3.0, 1.0e-9);   // 파일 1초 = 곡 1.5초 = 3 박
    // 가운데 마커를 반 박 늦추면 양 끝은 그대로, 그 사이만 늘고 준다
    const auto moved = withWarpMarkerMoved (added, 1, added.warp[1].beat + 0.5);
    CHECK_NEAR (warpMarkerSongBeat (moved, 1), 3.5, 1.0e-9);
    CHECK_NEAR (moved.startBeat, 2.0, 1.0e-9);
    CHECK_NEAR (clipSongSecondsAt (moved, tempo, 2.5), clipSongSecondsAt (clip, tempo, 2.5), 1.0e-9);
    // 첫 마커(클립 머리)를 옮기면 클립 머리가 따라가고 나머지는 제자리
    const auto head = withWarpMarkerMoved (moved, 0, moved.warp[0].beat + 0.25);
    CHECK_NEAR (head.startBeat, 2.25, 1.0e-9);
    CHECK_NEAR (warpMarkerSongBeat (head, 1), 3.5, 1.0e-9);
    CHECK_NEAR (warpMarkerSongBeat (head, 2), warpMarkerSongBeat (moved, 2), 1.0e-9);
    // 이웃을 넘지 않는다
    const auto clamped = withWarpMarkerMoved (added, 1, 100.0);
    CHECK (clamped.warp[1].beat < clamped.warp[2].beat);
    // 지우기: 가운데를 지우면 양 끝은 그대로, 둘보다 적게 남으면 마커가 없다
    const auto removed = withWarpMarkerRemoved (moved, 1);
    CHECK_EQ (removed.warp.size(), 2u);
    CHECK_NEAR (warpMarkerSongBeat (removed, 1), warpMarkerSongBeat (moved, 2), 1.0e-9);
    CHECK (withWarpMarkerRemoved (removed, 0).warp.empty());
    // 고정 늘이기는 마커로 바뀐다
    clip.stretch = 2.0;
    const auto fromStretch = withWarpMarkerAdded (clip, tempo, 1.0, 1.0);
    CHECK_EQ (fromStretch.stretch, 1.0);
    CHECK_NEAR (clipSongSecondsAt (fromStretch, tempo, 1.2), clipSongSecondsAt (clip, tempo, 1.2), 1.0e-9);
}

TEST (audio_clip_warp_renders_and_splits)
{
    if (! clipStretchAvailable())
        return;
    const auto file = sineFile ("clip_warp.wav");   // 440 Hz, 둘째 초는 880 Hz
    // 120 BPM: 파일의 첫 초가 0.5초로 줄고, 둘째 초가 1초로 는다 (높이는 그대로)
    const std::string warp = R"(, "warp": [ { "seconds": 0, "beat": 0 }, { "seconds": 1, "beat": 1 }, { "seconds": 2, "beat": 3 } ])";
    const auto c = composition (json (clip (file, 0, warp)));
    REQUIRE (c.tracks[0].clips[0].warp.size() == 3);
    CHECK_NEAR (clipEndSeconds (c.tracks[0].clips[0], TempoMap (c.tempo), juce::File()), 1.5, 1.0e-9);
    const auto out = offline (c, "clip_warp_render.wav");
    std::printf ("    warp: %.1f Hz then %.1f Hz\n", frequency (out, 0.05, 0.45), frequency (out, 0.6, 1.4));
    CHECK_NEAR (frequency (out, 0.05, 0.45), 440.0, 5.0);
    CHECK_NEAR (frequency (out, 0.6, 1.4), 880.0, 5.0);
    CHECK (rms (out, 0.6, 1.4) > 0.2f);
    CHECK (rms (out, 1.6, 1.9) < 1.0e-3f);
    // 실시간 재생이 오프라인과 같다
    RealtimePlayer player (c, playerOptions());
    player.play();
    CHECK (maxDiff (pull (player, (double) out.getNumSamples() / kRate), out, 0, 64) < 1.0e-5);
    // 나눈 두 클립은 같은 자리에서 같은 파일의 초를 낸다 (워프 마커는 그대로)
    juce::var root;
    juce::JSON::parse (juce::String (json (clip (file, 0, warp))), root);
    const auto split = withRangeCleared (root, { 0 }, 1.5, 1.5 + 1.0e-3, file.getParentDirectory());
    const auto parts = composition (juce::JSON::toString (split).toStdString());
    REQUIRE (parts.tracks[0].clips.size() == 2);
    const TempoMap tempo (parts.tempo);
    const auto& tail = parts.tracks[0].clips[1];
    CHECK (tail.warp == c.tracks[0].clips[0].warp);
    CHECK_NEAR (tail.offsetSeconds, 1.2505, 1.0e-6);   // 1.501 박
    CHECK_NEAR (clipSongSecondsAt (tail, tempo, 1.5), clipSongSecondsAt (c.tracks[0].clips[0], tempo, 1.5), 1.0e-6);
}

TEST (audio_quantize_pulls_attacks_to_the_grid)
{
    if (! clipStretchAvailable())
        return;
    // 120 BPM 의 8분음표 16타 (음과 잡음의 타격), 처음 말고는 ±30 ms 어긋난다
    Random noise (7);
    const double rate = 48000.0;
    const int n = (int) (4.5 * rate);
    juce::AudioBuffer<float> b (1, n);
    b.clear();
    const double jitter[] = { 0.0, -0.017, 0.003, -0.008, 0.006, 0.007, -0.026, -0.029, 0.02, -0.014, -0.016, 0.029, -0.002, 0.02, -0.001, 0.008 };
    for (int i = 0; i < 16; ++i)
    {
        const int at = (int) std::lround ((i * 0.25 + jitter[i]) * rate);
        for (int k = 0; k < (int) (0.08 * rate) && at + k < n; ++k)
            b.setSample (0, at + k, (float) (0.5 * std::exp (-k / (0.012 * rate))
                                             * (std::sin (2.0 * juce::MathConstants<double>::pi * (200.0 + 40.0 * (i % 4)) * k / rate) + 0.3 * noise.gaussian())));
    }
    const auto file = mrtest::outputDir().getChildFile ("clip_quantize.wav");
    writeWav (file, b, n, rate, 32);
    auto c = composition (json (clip (file, 4)));
    auto& spec = c.tracks[0].clips[0];
    const TempoMap tempo (c.tempo);
    const double length = clipLengthSeconds (spec, juce::File());
    const auto onsets = detectOnsets (file, 0.0, length);
    REQUIRE (onsets.size() == 16);
    // 같은 격자로 가는 어택은 하나만 옮긴다 (1/2 박 격자에 1/4 박마다의 어택)
    AudioQuantizeOptions coarse;
    coarse.grid = 2.0;
    const auto few = quantizedWarp (spec, tempo, length, onsets, coarse);
    CHECK (few.size() < 10);
    AudioQuantizeOptions options;
    options.grid = 0.5;
    spec.warp = quantizedWarp (spec, tempo, length, onsets, options);
    REQUIRE (spec.warp.size() == 17);   // 처음의 어택은 클립 머리라 옮기지 않고, 끝 마커가 더해진다
    const auto out = offline (c, "clip_quantize_render.wav");
    std::vector<float> mono ((size_t) out.getNumSamples());
    for (int i = 0; i < out.getNumSamples(); ++i)
        mono[(size_t) i] = 0.5f * (out.getSample (0, i) + out.getSample (1, i));
    const auto heard = detectOnsets (mono.data(), (int) mono.size(), kRate);
    REQUIRE (heard.size() == 16);
    double worst = 0.0;
    for (size_t i = 0; i < heard.size(); ++i)
        worst = std::max (worst, std::abs (heard[i] - (2.0 + 0.25 * (double) i)));
    std::printf ("    quantized attacks: worst %.2f ms from the grid\n", worst * 1000.0);
    CHECK (worst < 0.003);
    // 자르기 방식: 조각을 늘이지 않고 그 자리에서 튼다 (어택이 더 정확하고, 조각 안은 파일 그대로)
    spec.warpSlices = true;
    const auto sliced = offline (c, "clip_quantize_slices.wav");
    for (int i = 0; i < sliced.getNumSamples(); ++i)
        mono[(size_t) i] = 0.5f * (sliced.getSample (0, i) + sliced.getSample (1, i));
    const auto cut = detectOnsets (mono.data(), (int) mono.size(), kRate);
    REQUIRE (cut.size() == 16);
    double worstCut = 0.0;
    for (size_t i = 0; i < cut.size(); ++i)
        worstCut = std::max (worstCut, std::abs (cut[i] - (2.0 + 0.25 * (double) i)));
    std::printf ("    sliced attacks: worst %.2f ms from the grid\n", worstCut * 1000.0);
    CHECK (worstCut < 0.002);
    // 넷째 조각 (파일의 그 마커부터)이 3.5초... 곡의 2 + 4 x 0.25 = 3초 자리에서 파일과 같은 모양
    const auto& marker = spec.warp[4];
    const int64_t songAt = (int64_t) std::llround (clipSongSecondsAt (spec, tempo, marker.seconds) * kRate);
    const int64_t fileAt = (int64_t) std::llround (marker.seconds * rate);
    double dot = 0.0, energy = 0.0;
    for (int i = 100; i < 4000; ++i)
    {
        dot += sliced.getSample (0, (int) songAt + i) * b.getSample (0, (int) fileAt + i);
        energy += b.getSample (0, (int) fileAt + i) * b.getSample (0, (int) fileAt + i);
    }
    const double g = dot / energy;
    double residual = 0.0;
    for (int i = 100; i < 4000; ++i)
        residual = std::max (residual, (double) std::abs (sliced.getSample (0, (int) songAt + i) - g * b.getSample (0, (int) fileAt + i)));
    CHECK (g > 0.1 && residual < 1.0e-5);
    // 실시간 재생이 오프라인과 같다
    RealtimePlayer player (c, playerOptions());
    player.play();
    CHECK (maxDiff (pull (player, (double) sliced.getNumSamples() / kRate), sliced, 0, 64) < 1.0e-5);
    CHECK (! mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "A", "instrument": "audio", "clips": [ { "file": "x.wav", "startBeat": 0,
        "warpMode": "slice", "pitchSemitones": 2 } ] } ] })").composition);
    // 세기 50%: 어긋남이 반으로
    AudioQuantizeOptions half = options;
    half.strength = 0.5;
    auto plain = composition (json (clip (file, 4))).tracks[0].clips[0];
    const auto halfWarp = quantizedWarp (plain, tempo, length, onsets, half);
    plain.warp = halfWarp;
    for (size_t i = 1; i < 16; ++i)
    {
        const double beat = tempo.beatAt (clipSongSecondsAt (plain, tempo, onsets[i])) - 4.0;
        CHECK_NEAR (beat - 0.5 * (double) i, jitter[i] * 2.0 * 0.5, 0.01);
    }
}

TEST (audio_clip_pitch_correction)
{
    if (! clipStretchAvailable())
        return;
    // 목소리 같은 음 넷 (배음과 두 포먼트): A3 +35 센트, C4 -40 센트, E4 와 ±25 센트의 5.5 Hz 비브라토, C#4 +30 센트. 음 사이 0.1초 쉼
    struct Sung { double midi, cents, seconds, vibrato; };
    const Sung sung[] = { { 57, 35, 0.6, 0 }, { 60, -40, 0.6, 0 }, { 64, 0, 0.8, 25 }, { 61, 30, 0.6, 0 } };
    const double rate = 48000.0;
    std::vector<std::pair<double, double>> spans;   // 음마다 잴 구간 (초)
    std::vector<float> voice;
    double phase = 0.0;
    for (const auto& n : sung)
    {
        const int count = (int) (n.seconds * rate);
        spans.push_back ({ voice.size() / rate + 0.12, voice.size() / rate + n.seconds - 0.12 });
        for (int i = 0; i < count; ++i)
        {
            const double t = i / rate;
            const double midi = n.midi + n.cents / 100.0 + n.vibrato / 100.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t);
            const double f = 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
            phase += 2.0 * juce::MathConstants<double>::pi * f / rate;
            double x = 0.0;
            for (int k = 1; k <= 15; ++k)
            {
                const double fk = f * k;
                const double amp = std::exp (-std::pow ((fk - 700.0) / 500.0, 2.0)) + 0.6 * std::exp (-std::pow ((fk - 1200.0) / 400.0, 2.0)) + 0.3 / k;
                x += amp * std::sin (k * phase);
            }
            voice.push_back ((float) (0.08 * x * std::min (1.0, std::min (t / 0.03, (n.seconds - t) / 0.05))));
        }
        voice.insert (voice.end(), (size_t) (0.1 * rate), 0.0f);
    }
    juce::AudioBuffer<float> b (1, (int) voice.size());
    b.copyFrom (0, 0, voice.data(), (int) voice.size());
    const auto file = mrtest::outputDir().getChildFile ("clip_voice.wav");
    writeWav (file, b, b.getNumSamples(), rate, 32);
    auto song = [&file] (const std::string& correction)
    {
        return composition (R"({ "tempo": 120, "render": { "tailSeconds": 0.3 }, "tracks": [ { "name": "Vox", "instrument": "audio", "clips": [ { "file": ")"
                            + file.getFullPathName().toStdString() + R"(", "startBeat": 0)" + correction + " } ] } ] }");
    };
    // 음마다 잰 높이: 가까운 반음에서의 센트 (중앙값)와 흔들림 (표준 편차)
    auto measure = [&spans] (const juce::AudioBuffer<float>& out)
    {
        std::vector<float> mono ((size_t) out.getNumSamples());
        for (int i = 0; i < out.getNumSamples(); ++i)
            mono[(size_t) i] = out.getSample (0, i);
        std::vector<std::pair<double, double>> result;
        for (const auto& [from, to] : spans)
        {
            std::vector<double> midi;
            for (const auto& f : trackPitch (mono.data(), (int) mono.size(), kRate, (int) (from * kRate), (int) (to * kRate)))
                if (f.midi > 0.0)
                    midi.push_back (f.midi);
            if (midi.empty())
            {
                result.push_back ({ 0.0, 0.0 });
                continue;
            }
            auto sorted = midi;
            std::sort (sorted.begin(), sorted.end());
            const double median = sorted[sorted.size() / 2];
            double var = 0.0;
            for (const double m : midi)
                var += (m - median) * (m - median);
            result.push_back ({ median, 100.0 * std::sqrt (var / (double) midi.size()) });
        }
        return result;
    };
    auto cents = [] (double midi, double target) { return 100.0 * (midi - target); };
    const auto plain = measure (offline (song (""), "clip_voice_plain.wav"));
    const auto fast = measure (offline (song (R"(, "pitchCorrection": { "strength": 1, "speedMs": 0 })"), "clip_voice_fast.wav"));
    const auto half = measure (offline (song (R"(, "pitchCorrection": { "strength": 0.5, "speedMs": 0 })"), "clip_voice_half.wav"));
    const auto slow = measure (offline (song (R"(, "pitchCorrection": { "strength": 1, "speedMs": 120 })"), "clip_voice_slow.wav"));
    const auto minor = song (R"(, "pitchCorrection": { "strength": 1, "speedMs": 0, "key": "A minor" })");
    const auto inKey = offline (minor, "clip_voice_key.wav");
    const auto key = measure (inKey);
    const double targets[] = { 57, 60, 64, 61 };
    for (size_t i = 0; i < 4; ++i)
        std::printf ("    note %d: plain %+.1f, fast %+.1f, half %+.1f, slow %+.1f cents (spread %.1f / %.1f / %.1f)\n", (int) i + 1, cents (plain[i].first, targets[i]),
                     cents (fast[i].first, targets[i]), cents (half[i].first, targets[i]), cents (slow[i].first, targets[i]), plain[i].second, fast[i].second, slow[i].second);
    CHECK_NEAR (cents (plain[0].first, 57), 35.0, 3.0);
    for (size_t i : { 0u, 1u, 3u })
    {
        CHECK (std::abs (cents (fast[i].first, targets[i])) < 5.0);
        CHECK (std::abs (cents (slow[i].first, targets[i])) < 6.0);
    }
    CHECK (std::abs (cents (half[0].first, 57) - 17.5) < 5.0);
    CHECK (std::abs (cents (half[1].first, 60) + 20.0) < 5.0);
    CHECK (plain[2].second > 14.0 && fast[2].second < 8.0 && slow[2].second > 12.0);   // 빠른 교정만 비브라토를 편다
    CHECK (std::abs (cents (key[3].first, 62)) < 5.0);   // A minor: C# 은 D 로
    // 옮긴 높이에서 목표를 고른다: A3 +35 센트를 30 센트 올리면 Bb3 에 맞춘다
    const auto shifted = measure (offline (song (R"(, "pitchSemitones": 0.3, "pitchCorrection": { "speedMs": 0 })"), "clip_voice_shifted.wav"));
    CHECK (std::abs (cents (shifted[0].first, 58)) < 5.0 && std::abs (cents (shifted[1].first, 60)) < 5.0);
    std::printf ("    A minor: C# +30 becomes D %+.1f cents\n", cents (key[3].first, 62));
    // 실시간 재생이 오프라인과 같다
    RealtimePlayer player (minor, playerOptions());
    player.play();
    CHECK (maxDiff (pull (player, (double) inKey.getNumSamples() / kRate), inKey, 0, 64) < 1.0e-5);
    // 검사: 모르는 조, 근음 없는 음계, 자르기 방식
    auto fails = [] (const std::string& clip)
    {
        return ! mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "A", "instrument": "audio", "clips": [ { "file": "x.wav", "startBeat": 0, )" + clip
                                + " } ] } ] }").composition;
    };
    CHECK (fails (R"("pitchCorrection": { "key": "H minor" })"));
    CHECK (fails (R"("pitchCorrection": { "scale": "dorian" })"));
    CHECK (fails (R"("pitchCorrection": { "scale": "nope", "root": "D" })"));
    CHECK (fails (R"("warpMode": "slice", "pitchCorrection": { })"));
    CHECK (! fails (R"("pitchCorrection": { "scale": "dorian", "root": "D" })"));
    const auto dorian = mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "A", "instrument": "audio", "clips": [ { "file": "x.wav", "startBeat": 0,
        "pitchCorrection": { "scale": "dorian", "root": "D" } } ] } ] })");
    REQUIRE (dorian.composition.has_value());
    const auto& allowed = dorian.composition->tracks[0].clips[0].pitchCorrection->allowed;
    CHECK (allowed[2] && allowed[4] && allowed[5] && allowed[11] && ! allowed[10] && ! allowed[1]);   // D 도리안: B 는 있고 Bb, C# 은 없다
}

TEST (audio_clip_pitch_edits)
{
    if (! clipStretchAvailable())
        return;
    // 목소리 같은 음 다섯 (배음과 두 포먼트). 1: A3 의 0.8 Hz 느린 흔들림 ±30 센트, 2: C4 의 5.5 Hz 비브라토 ±30 센트,
    // 3: E4 +10 센트에 흔들림 ±20 과 비브라토 ±25 (4 로 이어 부른다), 4: D4 의 흔들림 ±15 와 비브라토 ±20, 5: G4 -20 센트의 비브라토 ±25
    struct Sung { double midi, cents, seconds, driftCents, driftHz, vibratoCents, gapAfter; };
    const Sung sung[] = { { 57, 0, 0.8, 30, 1.25, 0, 0.1 }, { 60, 0, 0.8, 0, 0, 30, 0.1 }, { 64, 10, 0.8, 20, 1.25, 25, 0.0 },
                          { 62, 0, 0.7, 15, 1.4286, 20, 0.1 }, { 67, -20, 0.7, 0, 0, 25, 0.1 } };
    const double rate = 48000.0;
    const double twoPi = 2.0 * juce::MathConstants<double>::pi;
    std::vector<std::pair<double, double>> truth;   // 음마다의 시작과 끝 (초)
    std::vector<float> voice ((size_t) (0.1 * rate), 0.0f);
    double phase = 0.0, phraseStart = voice.size() / rate;
    for (size_t k = 0; k < std::size (sung); ++k)
    {
        const auto& n = sung[k];
        const int count = (int) (n.seconds * rate);
        const double start = voice.size() / rate;
        truth.push_back ({ start, start + n.seconds });
        double phraseEnd = start + n.seconds;
        for (size_t j = k; j < std::size (sung) && sung[j].gapAfter == 0.0; ++j)
            phraseEnd += sung[j + 1].seconds;
        for (int i = 0; i < count; ++i)
        {
            const double t = i / rate, at = start + t;
            const double midi = n.midi + (n.cents + n.driftCents * std::sin (twoPi * n.driftHz * t) + n.vibratoCents * std::sin (twoPi * 5.5 * t)) / 100.0;
            const double f = 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
            phase += twoPi * f / rate;
            double x = 0.0;
            for (int h = 1; h <= 15; ++h)
            {
                const double fh = f * h;
                x += (std::exp (-std::pow ((fh - 700.0) / 500.0, 2.0)) + 0.6 * std::exp (-std::pow ((fh - 1200.0) / 400.0, 2.0)) + 0.3 / h) * std::sin (h * phase);
            }
            voice.push_back ((float) (0.08 * x * std::min (1.0, std::min ((at - phraseStart) / 0.03, (phraseEnd - at) / 0.05))));
        }
        voice.insert (voice.end(), (size_t) (n.gapAfter * rate), 0.0f);
        if (n.gapAfter > 0.0)
            phraseStart = voice.size() / rate;
    }
    juce::AudioBuffer<float> b (1, (int) voice.size());
    b.copyFrom (0, 0, voice.data(), (int) voice.size());
    const auto file = mrtest::outputDir().getChildFile ("clip_sung.wav");
    writeWav (file, b, b.getNumSamples(), rate, 32);

    // 음 덩어리: 다섯 음, 시작과 끝, 가운데 높이, 흔들림과 비브라토의 크기
    const auto segments = noteSegments (file);
    REQUIRE (segments.size() == 5);
    for (size_t k = 0; k < 5; ++k)
    {
        const auto& s = segments[k];
        std::printf ("    note %d: %.3f-%.3f s (truth %.3f-%.3f), %.2f (truth %.2f), drift %.1f, vibrato %.1f cents\n", (int) k + 1, s.startSeconds, s.endSeconds,
                     truth[k].first, truth[k].second, s.midi, sung[k].midi + sung[k].cents / 100.0, s.driftCents, s.vibratoCents);
        CHECK (std::abs (s.startSeconds - truth[k].first) < (k == 3 ? 0.06 : 0.03));
        CHECK (std::abs (s.endSeconds - truth[k].second) < (k == 2 ? 0.06 : 0.03));
        CHECK (std::abs (s.midi - (sung[k].midi + sung[k].cents / 100.0)) < 0.08);
    }
    // 흔들림과 비브라토가 갈린다 (1.25 Hz 흔들림은 0.2초 평균 두 번을 19% 남짓 빠져나간다)
    CHECK (segments[0].driftCents > 15.0 && segments[0].vibratoCents < segments[0].driftCents / 3.0);
    CHECK (segments[1].vibratoCents > 15.0 && segments[1].driftCents < segments[1].vibratoCents / 3.0);
    CHECK (fileNoteSegments (file) != nullptr && cachedFileNoteSegments (file) == fileNoteSegments (file) && fileNoteSegments (file)->size() == 5);

    auto edit = [&segments] (size_t k, double semitones, double flatten = 0.0, double vibrato = 1.0)
    {
        PitchEdit e;
        e.seconds = segments[k].startSeconds;
        e.lengthSeconds = segments[k].endSeconds - segments[k].startSeconds;
        e.semitones = semitones;
        e.flatten = flatten;
        e.vibrato = vibrato;
        return e;
    };
    auto song = [&file] (const std::vector<PitchEdit>& edits)
    {
        AudioClip clip;
        clip.file = file.getFullPathName().toStdString();
        clip.pitchEdits = edits;
        const auto json = clipJson (clip, juce::var());
        return composition (R"({ "tempo": 120, "render": { "tailSeconds": 0.3 }, "tracks": [ { "name": "Vox", "instrument": "audio", "clips": [ )"
                            + juce::JSON::toString (json, true).toStdString() + " ] } ] }");
    };
    // 음마다 가운데 높이(중앙값), 느린 흔들림과 비브라토의 크기 (센트): 비브라토 한 주기(0.182초) 이동 평균이 느린 쪽이다
    struct Measured { double midi, drift, vibrato; };
    auto measure = [&truth] (const juce::AudioBuffer<float>& out)
    {
        std::vector<float> mono ((size_t) out.getNumSamples());
        for (int i = 0; i < out.getNumSamples(); ++i)
            mono[(size_t) i] = out.getSample (0, i);
        std::vector<Measured> result;
        for (const auto& [from, to] : truth)
        {
            std::vector<double> midi, inner;
            const auto frames = trackPitch (mono.data(), (int) mono.size(), kRate, (int) ((from + 0.03) * kRate), (int) ((to - 0.03) * kRate));
            for (const auto& f : frames)
                midi.push_back (f.midi);
            const int half = 18;
            std::vector<double> slow (midi.size()), fast (midi.size());
            for (size_t i = 0; i < midi.size(); ++i)
            {
                const size_t lo = i >= (size_t) half ? i - half : 0, hi = std::min (midi.size() - 1, i + half);
                double sum = 0.0;
                for (size_t j = lo; j <= hi; ++j)
                    sum += midi[j];
                slow[i] = sum / (double) (hi - lo + 1);
                fast[i] = midi[i] - slow[i];
            }
            std::vector<double> slowInner, fastInner;
            for (size_t i = 0; i < frames.size(); ++i)
                if (frames[i].seconds >= from + 0.12 && frames[i].seconds <= to - 0.12 && frames[i].midi > 0.0)
                {
                    inner.push_back (midi[i]);
                    slowInner.push_back (slow[i]);
                    fastInner.push_back (fast[i]);
                }
            if (inner.empty())
            {
                result.push_back ({ 0.0, 0.0, 0.0 });
                continue;
            }
            auto sorted = inner;
            std::sort (sorted.begin(), sorted.end());
            const double median = sorted[sorted.size() / 2];
            double mean = 0.0;
            for (const double v : slowInner)
                mean += v / (double) slowInner.size();
            double ds = 0.0, vs = 0.0;
            for (size_t i = 0; i < slowInner.size(); ++i)
            {
                ds += (slowInner[i] - mean) * (slowInner[i] - mean);
                vs += fastInner[i] * fastInner[i];
            }
            result.push_back ({ median, 100.0 * std::sqrt (ds / (double) slowInner.size()), 100.0 * std::sqrt (vs / (double) fastInner.size()) });
        }
        return result;
    };
    const auto original = measure (b);   // 원래 파일 (렌더와 같은 샘플레이트)

    // 셋째 음을 2 반음 올린다: 그 음은 원래 가운데 + 2 의 5 센트 안, 이웃 음은 2 센트 안에서 그대로
    const auto raisedSong = song ({ edit (2, 2.0) });
    const auto raised = offline (raisedSong, "clip_sung_raised.wav");
    const auto up = measure (raised);
    for (size_t k = 0; k < 5; ++k)
        std::printf ("    raised note %d: %+.1f cents from the original\n", (int) k + 1, 100.0 * (up[k].midi - original[k].midi));
    CHECK (std::abs (100.0 * (up[2].midi - original[2].midi) - 200.0) < 5.0);
    for (size_t k : { 0u, 1u, 3u, 4u })
        CHECK (std::abs (100.0 * (up[k].midi - original[k].midi)) < 2.0);
    // 경계에서 파형이 뛰지 않는다: 편집의 양 끝 둘레 15 ms 의 2차 차분이 음 안보다 크지 않다
    auto jump = [&raised] (double from, double to)
    {
        double most = 0.0;
        for (int i = std::max (2, (int) (from * kRate)); i < std::min (raised.getNumSamples(), (int) (to * kRate)); ++i)
            most = std::max (most, (double) std::abs (raised.getSample (0, i) - 2.0f * raised.getSample (0, i - 1) + raised.getSample (0, i - 2)));
        return most;
    };
    const double inside = jump (truth[2].first + 0.1, truth[2].second - 0.1);
    const double atStart = jump (segments[2].startSeconds - 0.015, segments[2].startSeconds + 0.015);
    const double atEnd = jump (segments[2].endSeconds - 0.015, segments[2].endSeconds + 0.015);
    std::printf ("    largest second difference: inside %.4f, at the start %.4f, at the end %.4f\n", inside, atStart, atEnd);
    CHECK (atStart < 1.5 * inside && atEnd < 1.5 * inside);

    // 펴기와 비브라토: 1 의 흔들림을 펴고, 2 의 비브라토를 지우고, 3 은 흔들림만 펴고, 5 의 비브라토를 두 배로. 4 는 그대로
    const auto shaped = measure (offline (song ({ edit (0, 0.0, 1.0), edit (1, 0.0, 0.0, 0.0), edit (2, 0.0, 1.0), edit (4, 0.0, 0.0, 2.0) }), "clip_sung_shaped.wav"));
    for (size_t k = 0; k < 5; ++k)
        std::printf ("    note %d: drift %.1f -> %.1f, vibrato %.1f -> %.1f cents, centre %+.1f\n", (int) k + 1, original[k].drift, shaped[k].drift, original[k].vibrato,
                     shaped[k].vibrato, 100.0 * (shaped[k].midi - original[k].midi));
    CHECK (original[0].drift > 12.0 && shaped[0].drift < original[0].drift / 4.0);
    CHECK (original[1].vibrato > 15.0 && shaped[1].vibrato < original[1].vibrato / 4.0);
    CHECK (shaped[2].drift < original[2].drift / 2.0 && std::abs (shaped[2].vibrato / original[2].vibrato - 1.0) < 0.3);
    CHECK (shaped[4].vibrato / original[4].vibrato > 1.6 && shaped[4].vibrato / original[4].vibrato < 2.4);
    CHECK (std::abs (100.0 * (shaped[3].midi - original[3].midi)) < 2.0);
    for (size_t k = 0; k < 5; ++k)
        CHECK (std::abs (100.0 * (shaped[k].midi - original[k].midi)) < 6.0);   // 가운데 높이는 그대로

    // 교정과 함께: 편집한 음 안에서는 편집이 이긴다 (셋째 음은 +10 센트에서 2 반음 위, 다섯째 음은 교정으로 G4)
    auto tuned = raisedSong;
    PitchCorrection pc;
    pc.speedMs = 0.0;
    pc.allowed.fill (true);
    tuned.tracks[0].clips[0].pitchCorrection = pc;
    const auto both = measure (offline (tuned, "clip_sung_tuned.wav"));
    std::printf ("    with correction: note 3 %+.1f cents from the original, note 5 %+.1f cents from G4\n", 100.0 * (both[2].midi - original[2].midi),
                 100.0 * (both[4].midi - 67.0));
    CHECK (std::abs (100.0 * (both[2].midi - original[2].midi) - 200.0) < 5.0);
    CHECK (std::abs (100.0 * (both[4].midi - 67.0)) < 5.0);

    // 실시간 재생이 오프라인과 같다
    RealtimePlayer player (raisedSong, playerOptions());
    player.play();
    CHECK (maxDiff (pull (player, (double) raised.getNumSamples() / kRate), raised, 0, 64) < 1.0e-5);
    // 앱의 재생기: 편집을 뒤 스레드에서 준비해 넘긴 뒤에도 오프라인과 같다
    auto background = playerOptions();
    background.prepareClipsInBackground = true;
    RealtimePlayer later (song ({}), background);
    CHECK (later.apply (raisedSong));
    for (int waited = 0; later.clipsPending() && waited < 3000; waited += 10)
        juce::Thread::sleep (10);
    CHECK (! later.clipsPending());
    later.play();
    CHECK (maxDiff (pull (later, (double) raised.getNumSamples() / kRate), raised, 0, 64) < 1.0e-5);

    // 화면의 곡선: 편집한 음의 높이 곡선은 가운데에서 2 반음 위
    const auto shown = editedPitchCurve (segments[2], edit (2, 2.0));
    const size_t middle = shown.size() / 2;
    CHECK (shown.size() == segments[2].curve.size() && std::abs (shown[middle] - segments[2].curve[middle] - 2.0f) < 1.0e-4f);

    // 쓰고 읽기, 검사
    const auto written = clipJson (raisedSong.tracks[0].clips[0], juce::var());
    CHECK (written.getProperty ("pitchEdits", juce::var()).size() == 1 && (double) written["pitchEdits"][0]["semitones"] == 2.0
           && ! written["pitchEdits"][0].hasProperty ("flatten") && ! written["pitchEdits"][0].hasProperty ("vibrato"));
    auto fails = [] (const std::string& clip)
    {
        return ! mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "A", "instrument": "audio", "clips": [ { "file": "x.wav", "startBeat": 0, )" + clip
                                + " } ] } ] }").composition;
    };
    CHECK (! fails (R"("pitchEdits": [ { "seconds": 0.5, "lengthSeconds": 0.4, "semitones": 1.5, "flatten": 0.5, "vibrato": 0 } ])"));
    CHECK (fails (R"("pitchEdits": [ { "seconds": 0.5, "lengthSeconds": 0.4 }, { "seconds": 0.8, "lengthSeconds": 0.2 } ])"));   // 겹친다
    CHECK (fails (R"("pitchEdits": [ { "seconds": 0.5, "lengthSeconds": 0.4, "flatten": 2 } ])"));
    CHECK (fails (R"("pitchEdits": [ { "seconds": 0.5 } ])"));
    CHECK (fails (R"("warpMode": "slice", "warp": [ { "seconds": 0, "beat": 0 }, { "seconds": 1, "beat": 2 } ], "pitchEdits": [ { "seconds": 0.5, "lengthSeconds": 0.4 } ])"));
}

TEST (song_tempo_follows_a_recorded_performance)
{
    // 90 에서 110 BPM 으로 빨라지는 32 박의 연주 (박마다 ±8 ms, 박 사이에 여린 8분음표), 파일의 0.3초부터
    const double rate = 48000.0;
    Random rng (17);
    std::vector<double> truth;
    double t = 0.3;
    for (int i = 0; i < 32; ++i)
    {
        truth.push_back (t + 0.008 * rng.symmetric());
        t += 60.0 / (90.0 + 20.0 * i / 31.0);
    }
    juce::AudioBuffer<float> b (1, (int) ((t + 0.5) * rate));
    b.clear();
    auto hit = [&] (double at, double amp)
    {
        const int a = (int) std::lround (at * rate);
        for (int k = 0; k < 6000 && a + k < b.getNumSamples(); ++k)
            b.addSample (0, a + k, (float) (amp * rng.gaussian() * std::exp (-k / (0.02 * rate))));
    };
    for (size_t i = 0; i < truth.size(); ++i)
    {
        hit (truth[i], i % 4 == 0 ? 0.5 : 0.3);
        if (i + 1 < truth.size())
            hit (0.5 * (truth[i] + truth[i + 1]), 0.08);
    }
    const auto file = mrtest::outputDir().getChildFile ("clip_performance.wav");
    writeWav (file, b, b.getNumSamples(), rate, 32);
    const auto beats = trackBeats (file);
    REQUIRE (beats.size() == truth.size());
    juce::var root;
    juce::JSON::parse (juce::String (R"({ "tempo": 120, "timeSignature": [4, 4], "tracks": [ { "name": "Drums", "instrument": "audio", "clips": [ { "file": ")")
                           + file.getFullPathName() + R"(", "startBeat": 2 } ] } ] })", root);
    const auto before = composition (juce::JSON::toString (root).toStdString());
    const auto after = withTempoFollowingClip (root, 0, 0, beats, before.meters());
    REQUIRE (! after.isVoid());
    const auto c = composition (juce::JSON::toString (after).toStdString());
    const TempoMap tempo (c.tempo), fixed (before.tempo);
    const auto& clip = c.tracks[0].clips[0];
    CHECK (! clip.stretched());
    double worst = 0.0, worstBefore = 0.0;
    for (const double hitAt : truth)
    {
        const double beat = tempo.beatAt (clipSongSecondsAt (clip, tempo, hitAt));
        worst = std::max (worst, std::abs (beat - std::round (beat)));
        const double old = fixed.beatAt (clipSongSecondsAt (before.tracks[0].clips[0], fixed, hitAt));
        worstBefore = std::max (worstBefore, std::abs (old - std::round (old)));
    }
    const double firstBeat = tempo.beatAt (clipSongSecondsAt (clip, tempo, truth.front()));
    std::printf ("    follow tempo: hits within %.3f beats of the grid (before %.3f), first beat at %.3f, %d tempo points\n", worst, worstBefore, firstBeat,
                 (int) c.tempo.size());
    CHECK (worst < 0.01);
    CHECK (worstBefore > 0.2);
    CHECK_NEAR (std::fmod (firstBeat + 0.01, 4.0), 0.01, 0.01);   // 마디 머리 (어택을 찾은 자리는 친 자리보다 1~2 ms 앞)
    CHECK (c.tempo.size() >= 20);
}

TEST (audio_clip_muted_takes_are_silent)
{
    const auto file = sineFile ("clip_muted.wav");
    // 같은 자리에 겹친 두 테이크: 뮤트한 것은 소리가 없어 한 클립의 크기
    const auto both = composition (json (clip (file, 2, R"(, "lengthSeconds": 0.5)") + ", " + clip (file, 2, R"(, "lengthSeconds": 0.5, "muted": true)")));
    const auto one = composition (json (clip (file, 2, R"(, "lengthSeconds": 0.5)")));
    CHECK (both.tracks[0].clips[1].muted);
    CHECK (maxDiff (offline (both, "clip_muted_both.wav"), offline (one, "clip_muted_one.wav")) < 1.0e-6);
}

namespace
{

// 파일 하나 (채널마다 같은 샘플을 쓴다)
juce::File bufferFile (const char* name, const juce::AudioBuffer<float>& b, double rate)
{
    const auto file = mrtest::outputDir().getChildFile (name);
    writeWav (file, b, b.getNumSamples(), rate, 32);
    return file;
}

juce::AudioBuffer<float> noiseBuffer (uint64_t seed, double seconds, double rate, float level)
{
    Random r (seed);
    juce::AudioBuffer<float> b (2, (int) std::llround (seconds * rate));
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (ch, i, (float) (level * r.symmetric()));
    return b;
}

juce::AudioBuffer<float> reversedBuffer (const juce::AudioBuffer<float>& b)
{
    juce::AudioBuffer<float> r (b.getNumChannels(), b.getNumSamples());
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            r.setSample (ch, i, b.getSample (ch, b.getNumSamples() - 1 - i));
    return r;
}

float rmsDb (const juce::AudioBuffer<float>& b, double fromSec, double toSec)
{
    return juce::Decibels::gainToDecibels (rms (b, fromSec, toSec), -200.0f);
}

} // namespace

TEST (audio_clip_reverse_plays_the_reversed_file)
{
    // 첫 초는 440 Hz, 둘째 초는 880 Hz (44.1 kHz): 거꾸로 돌리면 880 Hz 가 먼저
    const auto file = sineFile ("clip_reverse.wav");
    const auto original = readAudioFile (file).buffer;
    const auto reversedFile = bufferFile ("clip_reverse_copy.wav", reversedBuffer (original), 44100.0);
    // 뒤집은 클립의 렌더는 거꾸로 돌린 파일을 그대로 튼 렌더와 같다 (자르기, 게인, 페이드, 음높이 바꾸기와 함께)
    for (const std::string rest : { R"(, "offsetSeconds": 0.25, "lengthSeconds": 1.0, "gainDb": -3, "fadeInSeconds": 0.1)",
                                    R"(, "offsetSeconds": 0.5, "lengthSeconds": 0.8, "pitchSemitones": 2)" })
    {
        const auto flagged = offline (composition (json (clip (file, 2, R"(, "reverse": true)" + rest))), "clip_reverse_flag.wav");
        const auto copy = offline (composition (json (clip (reversedFile, 2, rest))), "clip_reverse_file.wav");
        CHECK (rms (flagged, 1.1, 1.5) > 0.1f);
        CHECK (maxDiff (flagged, copy) < 1.0e-6);
    }
    const auto whole = offline (composition (json (clip (file, 0, R"(, "reverse": true)"))), "clip_reverse_whole.wav");
    CHECK_NEAR (frequency (whole, 0.1, 0.9), 880.0, 1.0);
    CHECK_NEAR (frequency (whole, 1.1, 1.9), 440.0, 0.5);

    // 뒤집기를 켜면 같은 곡 구간에서 같은 소리 조각을 거꾸로: 파일의 [0.25, 1.25) 는 뒤집은 파일의 [0.75, 1.75)
    AudioClip spec;
    spec.file = file.getFullPathName().toStdString();
    spec.startBeat = 2.0;
    spec.offsetSeconds = 0.25;
    spec.lengthSeconds = 1.0;
    const auto flipped = withClipReversed (spec, 2.0);
    CHECK (flipped.reverse);
    CHECK_NEAR (flipped.offsetSeconds, 0.75, 1.0e-9);
    CHECK_NEAR (*flipped.lengthSeconds, 1.0, 1.0e-9);
    CHECK (withClipReversed (flipped, 2.0) == spec);
    // 파일 끝까지 트는 클립 (길이 없음): 뒤집으면 처음부터 원래의 시작까지
    AudioClip open = spec;
    open.lengthSeconds.reset();
    const auto openFlipped = withClipReversed (open, 2.0);
    CHECK_NEAR (openFlipped.offsetSeconds, 0.0, 1.0e-9);
    CHECK (openFlipped.lengthSeconds && std::abs (*openFlipped.lengthSeconds - 1.75) < 1.0e-9);
    // 워프 마커, 높이 편집, 게인 곡선은 거울 자리로 가고, 곡의 박 구간은 그대로
    AudioClip warped = spec;
    warped.warp = { { 0.25, 10.0 }, { 0.75, 11.5 }, { 1.25, 12.0 } };
    warped.pitchEdits = { { 0.3, 0.2, 1.0, 0.0, 1.0 }, { 0.9, 0.3, -2.0, 0.5, 1.0 } };
    warped.gainCurve.points = { { 0.25, 0.0, CurveShape::linear }, { 1.25, -12.0, CurveShape::linear } };
    const auto w = withClipReversed (warped, 2.0);
    REQUIRE (w.warp.size() == 3u && w.pitchEdits.size() == 2u && w.gainCurve.points.size() == 2u);
    CHECK_NEAR (w.warp[0].seconds, 0.75, 1.0e-9);
    CHECK_NEAR (w.warp[1].seconds, 1.25, 1.0e-9);
    CHECK_NEAR (w.warp[1].beat - w.warp[0].beat, 0.5, 1.0e-9);   // 원래 뒤쪽 칸 (0.5 박)이 앞으로
    CHECK_NEAR (w.pitchEdits[0].seconds, 0.8, 1.0e-9);
    CHECK_NEAR (w.pitchEdits[1].seconds, 1.5, 1.0e-9);
    CHECK_NEAR (w.pitchEdits[0].semitones, -2.0, 1.0e-9);
    CHECK_NEAR (w.gainCurve.valueAt (0.75), -12.0, 1.0e-9);
    CHECK_NEAR (w.gainCurve.valueAt (1.5), -3.0, 1.0e-9);
    const TempoMap tempo ({ { 0.0, 120.0, false } });
    CHECK_NEAR (clipEndSeconds (w, tempo, {}), clipEndSeconds (warped, tempo, {}), 1.0e-6);
    for (size_t k = 0; k < warped.warp.size(); ++k)
        CHECK_NEAR (warpMarkerSongBeat (w, 2 - k), 2.0 + (warpMarkerSongBeat (warped, 2) - warpMarkerSongBeat (warped, k)), 1.0e-6);
    CHECK (withClipReversed (w, 2.0) == warped);

    // 재는 곳이 읽는 파일: 거꾸로 돌린 사본 (같은 이름에 reversed), 샘플이 원래 파일을 뒤집은 것
    AudioClip measured = spec;
    measured.reverse = true;
    const auto source = clipSourceFile (measured, {});
    CHECK (source != file && source.getFileName() == "clip_reverse reversed.wav");
    const auto read = readAudioFile (source).buffer;
    REQUIRE (read.getNumSamples() == original.getNumSamples());
    double worst = 0.0;
    for (int i = 0; i < read.getNumSamples(); ++i)
        worst = std::max (worst, (double) std::abs (read.getSample (0, i) - original.getSample (0, original.getNumSamples() - 1 - i)));
    CHECK (worst == 0.0);
    CHECK (clipSourceFile (spec, {}) == file);
    // 뒤집은 클립의 봉우리는 거꾸로 돌린 파일의 봉우리
    const auto peaks = clipPeaks (file, true), copyPeaks = clipPeaks (reversedFile);
    REQUIRE (peaks != nullptr && copyPeaks != nullptr);
    CHECK (peaks->maximum == copyPeaks->maximum && peaks->minimum == copyPeaks->minimum);

    // 실시간 재생도 같다
    const auto c = composition (json (clip (file, 1, R"(, "reverse": true, "offsetSeconds": 0.1, "lengthSeconds": 1.2)")));
    const auto reference = offline (c, "clip_reverse_rt.wav");
    RealtimePlayer player (c, playerOptions());
    player.play();
    CHECK (maxDiff (pull (player, (double) reference.getNumSamples() / kRate), reference) < 1.0e-5);
}

TEST (audio_clip_gain_curve_follows_the_sound)
{
    // 48 kHz 의 440 Hz 사인파 4초 (크기 0.5)
    juce::AudioBuffer<float> b (1, (int) (4.0 * kRate));
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample (0, i, (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / kRate)));
    const auto file = bufferFile ("clip_gain_curve.wav", b, kRate);
    const float full = rmsDb (b, 0.5, 1.5);   // 곧은 소리의 크기 (가운데 채널 하나)
    // 파일의 1초에서 0 dB, 3초에서 -12 dB 로 곧게 (그 밖은 끝 값을 붙든다)
    const std::string curve = R"(, "gainCurve": [ { "seconds": 1, "value": 0 }, { "seconds": 3, "value": -12 } ])";
    const auto out = offline (composition (json (clip (file, 0, curve))), "clip_gain_curve_render.wav");
    // 22 주기 (50 ms) 창의 크기
    auto at = [] (const juce::AudioBuffer<float>& x, double t) { return rmsDb (x, t - 0.025, t + 0.025); };
    for (const auto& [t, db] : std::vector<std::pair<double, double>> { { 0.5, 0.0 }, { 1.5, -3.0 }, { 2.0, -6.0 }, { 2.5, -9.0 }, { 3.5, -12.0 } })
    {
        CHECK_NEAR (at (out, t) - full, db, 0.05);
        std::printf ("    gain curve at %.1f s: %+.3f dB (curve %+.1f)\n", t, at (out, t) - full, db);
    }
    // 클립의 게인과 더한다
    const auto louder = offline (composition (json (clip (file, 0, R"(, "gainDb": 6)" + curve))), "clip_gain_curve_plus.wav");
    CHECK_NEAR (at (louder, 2.0) - full, 0.0, 0.05);
    // 파일의 초라서 자른 클립은 곡의 0초에서 파일의 2초 (-6 dB)
    const auto trimmed = offline (composition (json (clip (file, 0, R"(, "offsetSeconds": 2)" + curve))), "clip_gain_curve_trim.wav");
    CHECK_NEAR (at (trimmed, 0.03) - full, -6.18, 0.05);   // 곡의 0.03초는 파일의 2.03초
    // 두 배로 늘인 클립: 곡의 4초가 파일의 2초 (-6 dB)
    if (clipStretchAvailable())
    {
        const auto stretched = offline (composition (json (clip (file, 0, R"(, "stretch": 2)" + curve))), "clip_gain_curve_stretch.wav");
        CHECK_NEAR (rmsDb (stretched, 3.95, 4.05) - rmsDb (stretched, 0.9, 1.1), -6.0, 0.3);
    }
    // 같은 초의 두 점은 뛰기 (파일의 2초에서 0 dB 에서 -20 dB 로)
    const auto jump = offline (composition (json (clip (file, 0, R"(, "gainCurve": [ { "seconds": 2, "value": 0 }, { "seconds": 2, "value": -20 } ])"))),
                               "clip_gain_curve_jump.wav");
    CHECK_NEAR (at (jump, 1.95) - full, 0.0, 0.05);
    CHECK_NEAR (at (jump, 2.05) - full, -20.0, 0.05);
    // 곡의 시간을 넣어 클립이 나뉘어도 같은 소리에 같은 게인
    juce::var root;
    juce::JSON::parse (juce::String (json (clip (file, 0, curve))), root);
    const auto split = offline (composition (juce::JSON::toString (withTimeInserted (root, 4, 2)).toStdString()), "clip_gain_curve_split.wav");
    CHECK_NEAR (at (split, 3.5) - full, -9.0, 0.05);   // 곡의 2초(4박)에 1초를 넣었다: 곡의 3.5초는 파일의 2.5초
    // 실시간 재생도 같다
    const auto c = composition (json (clip (file, 0, curve)));
    RealtimePlayer player (c, playerOptions());
    player.play();
    CHECK (maxDiff (pull (player, (double) out.getNumSamples() / kRate), out) < 1.0e-5);
    // 읽기와 쓰기
    CHECK_EQ (c.tracks[0].clips[0].gainCurve.points.size(), 2u);
    const auto written = clipJson (c.tracks[0].clips[0], {});
    REQUIRE (written["gainCurve"].isArray() && written["gainCurve"].getArray()->size() == 2);
    CHECK_NEAR ((double) written["gainCurve"][1]["seconds"], 3.0, 1.0e-9);
    CHECK_NEAR ((double) written["gainCurve"][1]["value"], -12.0, 1.0e-9);
    auto bad = mrtest::parse (json (clip (file, 0, R"(, "gainCurve": [ { "seconds": 2, "value": 0 }, { "seconds": 1, "value": -20 } ])")));
    CHECK (! bad.diagnostics.errors.empty());
    // 거울 곡선: 곧은 선, 사인, 뛰기 모두 거울 자리의 값
    AutomationLane lane;
    lane.points = { { 0.5, 0.0, CurveShape::smooth }, { 1.0, -6.0, CurveShape::step }, { 1.5, -10.0, CurveShape::linear }, { 2.5, -2.0, CurveShape::linear } };
    const auto m = mirroredLane (lane, 3.0);
    for (double x = 0.0; x <= 3.0; x += 0.01)
        if (std::abs (x - 1.0) > 1.0e-6 && std::abs (x - 1.5) > 1.0e-6)
            CHECK_NEAR (m.valueAt (3.0 - x), lane.valueAt (x), 1.0e-9);
}

TEST (audio_clip_fade_shapes)
{
    CHECK_NEAR (fadeGain (FadeShape::sine, 0.5), 0.5, 1.0e-6);
    CHECK_NEAR (fadeGain (FadeShape::linear, 0.25), 0.25, 1.0e-6);
    CHECK_NEAR (fadeGain (FadeShape::equalPower, 0.5), std::sqrt (0.5), 1.0e-6);
    CHECK_NEAR (fadeGain (FadeShape::exponential, 0.5), (std::pow (10.0, -1.5) - 1.0e-3) / (1.0 - 1.0e-3), 1.0e-6);
    for (const auto shape : { FadeShape::sine, FadeShape::linear, FadeShape::equalPower, FadeShape::exponential })
    {
        CHECK (fadeGain (shape, 0.0) == 0.0f);
        CHECK_NEAR (fadeGain (shape, 1.0), 1.0, 1.0e-6);
    }
    // 맞선 두 페이드: 사인 제곱은 크기의 합이, 등전력은 힘의 합이 1
    for (double t = 0.0; t <= 1.0; t += 0.05)
    {
        CHECK_NEAR (fadeGain (FadeShape::sine, t) + fadeGain (FadeShape::sine, 1.0 - t), 1.0, 1.0e-6);
        CHECK_NEAR (std::pow (fadeGain (FadeShape::equalPower, t), 2) + std::pow (fadeGain (FadeShape::equalPower, 1.0 - t), 2), 1.0, 1.0e-6);
    }
    // 렌더: 크기 0.5 의 곧은 값(직류) 1초에 0.4초씩의 페이드, 모양의 값 (다른 트랙 없이)
    juce::AudioBuffer<float> dc (1, (int) kRate);
    for (int i = 0; i < dc.getNumSamples(); ++i)
        dc.setSample (0, i, 0.5f);
    const auto file = bufferFile ("clip_fade_shape.wav", dc, kRate);
    for (const auto& [name, shape] : std::vector<std::pair<std::string, FadeShape>> {
             { "linear", FadeShape::linear }, { "equalPower", FadeShape::equalPower }, { "exponential", FadeShape::exponential }, { "sine", FadeShape::sine } })
    {
        const auto c = composition (R"({ "tempo": 120, "tracks": [ { "name": "Audio", "instrument": "audio", "clips": [ )"
                                    + clip (file, 0, R"(, "fadeInSeconds": 0.4, "fadeOutSeconds": 0.4, "fadeInShape": ")" + name + R"(", "fadeOutShape": ")" + name
                                                         + R"(")") + " ] } ] }");
        CHECK (c.tracks[0].clips[0].fadeInShape == shape && c.tracks[0].clips[0].fadeOutShape == shape);
        const auto out = offline (c, "clip_fade_shape_" + name + ".wav");
        CHECK_NEAR (out.getSample (0, (int) (0.2 * kRate)), 0.5 * fadeGain (shape, 0.5), 1.0e-5);
        CHECK_NEAR (out.getSample (0, (int) (0.9 * kRate)), 0.5 * fadeGain (shape, 0.25), 1.0e-5);
        const auto written = juce::JSON::toString (clipJson (c.tracks[0].clips[0], {}));
        CHECK (written.contains ("fadeInShape") == (shape != FadeShape::sine));
    }
    CHECK (! mrtest::parse (json (clip (file, 0, R"(, "fadeInShape": "cubic")"))).diagnostics.errors.empty());
}

TEST (audio_clip_crossfades)
{
    // 상관없는 두 잡음 (같은 크기): 등전력 크로스페이드에서 겹친 자리의 크기가 앞뒤와 같다
    const auto a = bufferFile ("clip_xfade_a.wav", noiseBuffer (11, 3.0, kRate, 0.3f), kRate);
    const auto b = bufferFile ("clip_xfade_b.wav", noiseBuffer (12, 3.0, kRate, 0.3f), kRate);
    auto c = composition (json (clip (a, 0, R"(, "lengthSeconds": 2)") + ", " + clip (b, 3, R"(, "offsetSeconds": 0.2, "lengthSeconds": 2)")));
    const TempoMap tempo (c.tempo);
    const auto pair = crossfadedClips (c.tracks[0].clips[0], c.tracks[0].clips[1], tempo, {});
    REQUIRE (pair.has_value());
    CHECK (pair->first.fadeOutShape == FadeShape::equalPower && pair->second.fadeInShape == FadeShape::equalPower);
    CHECK_NEAR (pair->first.fadeOutSeconds, 0.5, 1.0e-6);
    CHECK_NEAR (pair->second.fadeInSeconds, 0.5, 1.0e-6);
    c.tracks[0].clips = { pair->first, pair->second };
    const auto out = offline (c, "clip_xfade.wav");
    const float before = rmsDb (out, 1.0, 1.45), after = rmsDb (out, 2.05, 2.5);
    CHECK_NEAR (before, after, 0.3);
    float worst = 0.0f;
    for (double t = 1.5; t < 1.95; t += 0.1)
    {
        CHECK_NEAR (rmsDb (out, t, t + 0.1), 0.5f * (before + after), 0.5);
        worst = std::max (worst, std::abs (rmsDb (out, t, t + 0.1) - 0.5f * (before + after)));
    }
    std::printf ("    equal-power crossfade of two noises: before %.2f, after %.2f dB, overlap within %.2f dB\n", before, after, worst);
    // 겹치지도 맞붙지도 않으면 없다, 뒤 클립이 앞 클립 안에 들어 있어도 없다
    const auto apart = composition (json (clip (a, 0, R"(, "lengthSeconds": 1)") + ", " + clip (b, 4, R"(, "lengthSeconds": 1)")));
    CHECK (! crossfadedClips (apart.tracks[0].clips[0], apart.tracks[0].clips[1], tempo, {}).has_value());
    const auto inside = composition (json (clip (a, 0, R"(, "lengthSeconds": 2)") + ", " + clip (b, 1, R"(, "lengthSeconds": 0.5)")));
    CHECK (! crossfadedClips (inside.tracks[0].clips[0], inside.tracks[0].clips[1], tempo, {}).has_value());

    // 나눈 클립 (같은 파일의 이어지는 소리): 이음매 앞뒤 10 ms 씩 늘여 사인 제곱 페이드, 나누기 전의 소리와 같다
    const auto whole = composition (json (clip (a, 0, R"(, "offsetSeconds": 0.5, "lengthSeconds": 2)")));
    auto split = composition (json (clip (a, 0, R"(, "offsetSeconds": 0.5, "lengthSeconds": 1)") + ", " + clip (a, 2, R"(, "offsetSeconds": 1.5, "lengthSeconds": 1)")));
    const auto joined = crossfadedClips (split.tracks[0].clips[0], split.tracks[0].clips[1], tempo, {}, 0.02);
    REQUIRE (joined.has_value());
    CHECK (joined->first.fadeOutShape == FadeShape::sine && joined->second.fadeInShape == FadeShape::sine);
    CHECK_NEAR (*joined->first.lengthSeconds, 1.01, 1.0e-6);
    CHECK_NEAR (joined->second.offsetSeconds, 1.49, 1.0e-6);
    CHECK_NEAR (joined->second.startBeat, 1.98, 1.0e-6);
    CHECK_NEAR (joined->first.fadeOutSeconds, 0.02, 1.0e-6);
    split.tracks[0].clips = { joined->first, joined->second };
    const auto reference = offline (whole, "clip_xfade_whole.wav"), rejoined = offline (split, "clip_xfade_split.wav");
    CHECK (maxDiff (reference, rejoined) < 1.0e-5);
    std::printf ("    split clip crossfaded back together: largest difference from the whole clip %.2e\n", maxDiff (reference, rejoined));
    // 파일의 처음에서 시작하는 뒤 클립은 앞 클립 쪽으로만 늘인다
    auto edge = composition (json (clip (a, 0, R"(, "lengthSeconds": 1)") + ", " + clip (b, 2, R"(, "lengthSeconds": 1)")));
    const auto oneSided = crossfadedClips (edge.tracks[0].clips[0], edge.tracks[0].clips[1], tempo, {}, 0.02);
    REQUIRE (oneSided.has_value());
    CHECK_NEAR (*oneSided->first.lengthSeconds, 1.02, 1.0e-6);
    CHECK_NEAR (oneSided->second.startBeat, 2.0, 1.0e-6);
    CHECK (oneSided->first.fadeOutShape == FadeShape::equalPower);
}

TEST (audio_clip_zero_crossings)
{
    // 48 kHz 의 100 Hz 사인파: 영점은 5 ms 마다
    juce::AudioBuffer<float> b (1, (int) kRate);
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample (0, i, (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 100.0 * i / kRate + 0.3)));
    const auto file = bufferFile ("clip_zero.wav", b, kRate);
    AudioClip spec;
    spec.file = file.getFullPathName().toStdString();
    for (const double at : { 0.2013, 0.5, 0.7777 })
    {
        const double z = nearestZeroCrossing (spec, {}, at);
        const int i = (int) std::llround (z * kRate);
        CHECK (std::abs (b.getSample (0, i)) < 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 100.0 / kRate) + 1.0e-6f);
        CHECK (std::abs (z - at) <= 0.0025 + 1.0 / kRate);   // 반 주기 안
    }
    // 찾는 폭 안에 영점이 없으면 그대로
    CHECK_NEAR (nearestZeroCrossing (spec, {}, 0.5, 1.0e-5), 0.5, 1.0e-12);
    // 뒤집은 클립은 거꾸로 돌린 파일의 초에서 찾는다
    spec.reverse = true;
    const double z = nearestZeroCrossing (spec, {}, 0.3);
    CHECK (std::abs (b.getSample (0, b.getNumSamples() - 1 - (int) std::llround (z * kRate))) < 0.01f);
}
