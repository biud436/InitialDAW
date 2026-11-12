#include "TestFramework.h"
#include "TestUtil.h"

#include "io/AudioFile.h"
#include "realtime/RealtimePlayer.h"
#include "rendering/OfflineRenderer.h"

#include <cmath>

using namespace mr;

namespace
{

constexpr double kRate = 48000.0;

Composition composition (const std::string& json)
{
    auto r = mrtest::parse (json);
    if (! r.composition)
        for (const auto& e : r.diagnostics.errors)
            std::fprintf (stderr, "    parse error %s: %s\n", e.path.c_str(), e.message.c_str());
    return *r.composition;
}

juce::AudioBuffer<float> offline (const Composition& c, const std::string& out)
{
    RenderOptions o;
    o.useDefaultRegistry = false;
    o.bitDepth = 32;
    o.output = mrtest::outputDir().getChildFile (out);
    renderComposition (c, o);
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

// 오디오 콜백처럼 callbackSize 씩 불러 seconds 만큼 받는다
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

// 피아노와 패드, CC11 곡선, 휴머나이즈, EQ 가 든 버스와 마스터 (지연이 없는 효과만)
const char* mixJson = R"({ "tempo": 90, "performance": { "humanizeSeed": 7, "timingHumanizeMs": 10, "velocityHumanize": 6 },
  "render": { "tailSeconds": 0.6 },
  "buses": [ { "name": "room", "effects": [ { "type": "eq", "lowCutHz": 200, "bands": [ { "type": "peak", "freqHz": 1200, "gainDb": 4, "q": 1 } ] } ] } ],
  "master": { "mixer": { "gainDb": -2 }, "effects": [ { "type": "eq", "highCutHz": 9000 }, { "type": "gain", "gainDb": 1.5 } ] },
  "tracks": [
    { "name": "Keys", "instrument": "synth.piano", "mixer": { "pan": -0.4 }, "sends": [ { "bus": "room", "levelDb": -6 } ],
      "notes": [ { "pitch": "E4", "startBeat": 0, "durationBeat": 1, "velocity": 90 }, { "pitch": "G4", "startBeat": 1, "durationBeat": 1, "velocity": 80 },
                 { "pitch": "B4", "startBeat": 2, "durationBeat": 2, "velocity": 85 }, { "pitch": "C5", "startBeat": 4, "durationBeat": 2, "velocity": 70 } ] },
    { "name": "Pad", "instrument": "synth.pad", "mixer": { "gainDb": -4, "pan": 0.3 }, "sends": [ { "bus": "room", "levelDb": -3, "preFader": true } ],
      "cc11": [ { "beat": 0, "value": 40 }, { "beat": 4, "value": 120, "curve": "smooth" }, { "beat": 6, "value": 60 } ],
      "notes": [ { "pitch": "E3", "startBeat": 0, "durationBeat": 6, "velocity": 70 }, { "pitch": "B3", "startBeat": 0, "durationBeat": 6, "velocity": 70 } ] } ] })";

} // namespace

TEST (realtime_playback_matches_the_offline_render)
{
    const auto c = composition (mixJson);
    const auto reference = offline (c, "rt_reference.wav");

    RealtimePlayer player (c, playerOptions());
    CHECK_EQ (player.length(), (int64_t) reference.getNumSamples());
    CHECK_EQ (player.trackCount(), (size_t) 2);
    CHECK_EQ (player.trackInfo (1).resolved, std::string ("synth.pad"));
    player.play();
    const auto live = pull (player, (double) reference.getNumSamples() / kRate);

    double maxDiff = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < reference.getNumSamples(); ++i)
            maxDiff = std::max (maxDiff, (double) std::abs (live.getSample (ch, i) - reference.getSample (ch, i)));
    CHECK (rms (reference, 0.2, 2.0) > 0.01f);
    CHECK (maxDiff < 1.0e-5);
    // 끝에 닿으면 멈춘다
    CHECK (! player.isPlaying());
    CHECK_EQ (player.position(), player.length());
}

TEST (realtime_seek_continues_from_the_new_position)
{
    // 0~1박과 6~7박에 음, 그 사이는 조용하다. 5초로 옮기면 6초의 음이 오프라인과 같은 자리에서 울린다.
    const auto c = composition (R"({ "tempo": 60, "render": { "tailSeconds": 1 },
      "tracks": [ { "name": "Keys", "instrument": "synth.piano", "notes": [
        { "pitch": "A4", "startBeat": 0, "durationBeat": 1, "velocity": 90 }, { "pitch": "C5", "startBeat": 6, "durationBeat": 1, "velocity": 90 } ] } ] })");
    const auto reference = offline (c, "rt_seek_reference.wav");

    RealtimePlayer player (c, playerOptions());
    player.seekSeconds (5.0);
    player.play();
    const auto live = pull (player, 3.0);
    double maxDiff = 0.0;
    for (int i = 0; i < live.getNumSamples(); ++i)
        maxDiff = std::max (maxDiff, (double) std::abs (live.getSample (0, i) - reference.getSample (0, i + (int) (5.0 * kRate))));
    CHECK (rms (live, 1.05, 1.5) > 0.01f);   // 6초의 음
    CHECK (maxDiff < 1.0e-5);
}

TEST (realtime_stop_releases_sounding_notes_and_holds_the_position)
{
    const auto c = composition (R"({ "tempo": 60, "render": { "tailSeconds": 1 },
      "tracks": [ { "name": "Pad", "instrument": "synth.pad", "notes": [ { "pitch": "A3", "startBeat": 0, "durationBeat": 8, "velocity": 90 } ] } ] })");
    RealtimePlayer player (c, playerOptions());
    player.play();
    const auto playingPart = pull (player, 1.5);
    player.stop();
    const int64_t stoppedAt = player.position();
    const auto after = pull (player, 4.0);
    CHECK (rms (playingPart, 1.0, 1.5) > 0.01f);
    CHECK (rms (after, 3.5, 4.0) < 0.001f * rms (playingPart, 1.0, 1.5) + 1.0e-6f);
    CHECK_EQ (player.position(), stoppedAt);
    CHECK (std::abs ((double) stoppedAt / kRate - 1.5) < 0.02);
}

TEST (realtime_mute_solo_and_monitor_gain_apply_while_playing)
{
    const auto c = composition (R"({ "tempo": 60, "render": { "tailSeconds": 0.2 },
      "tracks": [ { "name": "Low", "instrument": "synth.sine", "notes": [ { "pitch": "A3", "startBeat": 0, "durationBeat": 8, "velocity": 100 } ] },
                  { "name": "High", "instrument": "synth.sine", "notes": [ { "pitch": "A5", "startBeat": 0, "durationBeat": 8, "velocity": 100 } ] } ] })");
    RealtimePlayer player (c, playerOptions());
    player.play();
    pull (player, 0.5);
    player.setTrackSolo (0, true);
    const auto solo = pull (player, 1.0);
    CHECK (std::abs (frequency (solo, 0.2, 0.9) - 220.0) < 2.0);

    player.setTrackSolo (0, false);
    player.setTrackMute (0, true);
    pull (player, 0.05);                 // 뮤트는 한 블록에 걸쳐 줄어든다
    player.takeTrackPeak (0);
    player.takeTrackPeak (1);
    const auto muted = pull (player, 1.0);
    CHECK (std::abs (frequency (muted, 0.2, 0.9) - 880.0) < 4.0);
    CHECK (player.takeTrackPeak (0) < 1.0e-6f);
    CHECK (player.takeTrackPeak (1) > 0.01f);

    const float before = rms (muted, 0.2, 0.9);
    player.setMonitorGainDb (6.0);
    const auto louder = pull (player, 1.0);
    CHECK (std::abs (rms (louder, 0.2, 0.9) / before - 1.995f) < 0.05f);
    CHECK (player.takeMasterPeak (0) > 0.0f);
}

TEST (realtime_output_does_not_depend_on_the_number_of_workers)
{
    // 사이드체인(키 트랙이 앞 묶음)과 버스가 있는 곡을 일꾼 0, 3, 8 로: 샘플이 모두 같아야 한다
    const auto c = composition (R"({ "tempo": 120, "render": { "tailSeconds": 0.3 },
      "buses": [ { "name": "verb", "effects": [ { "type": "reverb", "mix": 1, "decaySeconds": 1.0 },
                                                { "type": "compressor", "sidechain": "Kick", "thresholdDb": -30, "ratio": 8 } ] } ],
      "tracks": [
        { "name": "Pad", "instrument": "synth.pad", "sends": [ { "bus": "verb", "levelDb": -6 } ],
          "effects": [ { "type": "compressor", "sidechain": "Kick", "thresholdDb": -30, "ratio": 8, "attackMs": 1, "releaseMs": 100 } ],
          "notes": [ { "pitch": "A3", "startBeat": 0, "durationBeat": 4, "velocity": 90 }, { "pitch": "E4", "startBeat": 0, "durationBeat": 4, "velocity": 90 } ] },
        { "name": "Kick", "instrument": "synth.drums", "notes": [ { "pitch": 36, "startBeat": 0, "durationBeat": 0.5, "velocity": 110 },
          { "pitch": 36, "startBeat": 1, "durationBeat": 0.5, "velocity": 110 }, { "pitch": 36, "startBeat": 2, "durationBeat": 0.5, "velocity": 110 } ] },
        { "name": "Keys", "instrument": "synth.piano", "sends": [ { "bus": "verb", "levelDb": -3 } ],
          "notes": [ { "pitch": "C5", "startBeat": 0.5, "durationBeat": 1, "velocity": 80 }, { "pitch": "G4", "startBeat": 1.5, "durationBeat": 1, "velocity": 80 } ] } ] })");
    auto runWith = [&] (int threads)
    {
        auto o = playerOptions();
        o.threads = threads;
        RealtimePlayer p (c, o);
        p.play();
        return pull (p, 2.5, 256);
    };
    const auto solo = runWith (0);
    for (int threads : { 3, 8 })
    {
        const auto many = runWith (threads);
        double maxDiff = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < solo.getNumSamples(); ++i)
                maxDiff = std::max (maxDiff, (double) std::abs (many.getSample (ch, i) - solo.getSample (ch, i)));
        CHECK (maxDiff == 0.0);
    }
    CHECK (rms (solo, 0.2, 1.5) > 0.005f);
}

TEST (realtime_update_track_matches_a_player_built_from_the_edited_song)
{
    const auto before = composition (mixJson);
    // 고친 곡: 피아노의 음을 바꾸고 하나를 더하고, 패드의 CC11 곡선을 바꾼다
    std::string edited = mixJson;
    auto replace = [&edited] (const std::string& a, const std::string& b)
    {
        const auto at = edited.find (a);
        REQUIRE (at != std::string::npos);
        edited.replace (at, a.size(), b);
    };
    replace (R"({ "pitch": "G4", "startBeat": 1, "durationBeat": 1, "velocity": 80 })",
             R"({ "pitch": "A4", "startBeat": 1.5, "durationBeat": 0.5, "velocity": 100 }, { "pitch": "D5", "startBeat": 3, "durationBeat": 1, "velocity": 75 })");
    replace (R"({ "beat": 4, "value": 120, "curve": "smooth" })", R"({ "beat": 2, "value": 90, "curve": "step" })");
    const auto after = composition (edited);

    RealtimePlayer fresh (after, playerOptions());
    RealtimePlayer updated (before, playerOptions());
    updated.updateTrack (0, after.tracks[0]);
    updated.updateTrack (1, after.tracks[1]);
    CHECK_EQ (updated.trackInfo (0).notes, (size_t) 5);
    CHECK_EQ (updated.length(), fresh.length());
    fresh.play();
    updated.play();
    const double seconds = (double) fresh.length() / kRate;
    const auto a = pull (fresh, seconds), b = pull (updated, seconds);
    double maxDiff = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            maxDiff = std::max (maxDiff, (double) std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
    CHECK (rms (a, 0.2, 2.0) > 0.01f);
    CHECK_EQ (maxDiff, 0.0);
    updated.collectRetired();
}

TEST (realtime_update_track_while_playing_leaves_no_sounding_notes)
{
    const auto c = composition (R"({ "tempo": 60, "render": { "tailSeconds": 1 },
      "tracks": [ { "name": "Lead", "instrument": "synth.lead", "notes": [ { "pitch": "A3", "startBeat": 0, "durationBeat": 8, "velocity": 100 } ] } ] })");
    RealtimePlayer player (c, playerOptions());
    player.play();
    const auto first = pull (player, 1.0);
    CHECK (rms (first, 0.5, 1.0) > 0.01f);

    auto empty = c.tracks[0];
    empty.notes.clear();
    player.updateTrack (0, empty);
    const auto second = pull (player, 2.0);
    CHECK (rms (second, 0.0, 0.05) > 0.0f);     // 바꿔 끼운 블록부터 꼬리만 남는다
    CHECK (rms (second, 1.0, 2.0) < 1.0e-4f);   // 지운 음이 계속 울리지 않는다

    // 곡 끝 뒤에 음을 더하면 곡이 길어진다
    auto longer = c.tracks[0];
    longer.notes.push_back (Note { 69, 20.0, 1.0, 90 });
    player.updateTrack (0, longer);
    CHECK_NEAR ((double) player.length() / kRate, 22.0, 0.05);
}

namespace
{

std::string edit (std::string text, const std::vector<std::pair<std::string, std::string>>& replacements)
{
    for (const auto& [a, b] : replacements)
    {
        const auto at = text.find (a);
        if (at == std::string::npos)
        {
            std::fprintf (stderr, "    missing: %s\n", a.c_str());
            continue;
        }
        text.replace (at, a.size(), b);
    }
    return text;
}

double maxDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from = 0)
{
    double d = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = from; i < std::min (a.getNumSamples(), b.getNumSamples()); ++i)
            d = std::max (d, (double) std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
    return d;
}

// 버스 둘(잔향, 딜레이와 컴프레서), 트랙 효과, 마스터 효과가 든 곡
const char* busyJson = R"({ "tempo": 100, "performance": { "humanizeSeed": 3, "timingHumanizeMs": 6 },
  "render": { "tailSeconds": 1.0 },
  "buses": [ { "name": "hall", "effects": [ { "type": "reverb", "mix": 1, "decaySeconds": 1.4 } ] },
             { "name": "echo", "mixer": { "gainDb": -3 }, "effects": [ { "type": "delay", "timeMs": 180, "feedback": 0.5, "mix": 1 },
                                                                       { "type": "compressor", "thresholdDb": -30, "ratio": 4 } ] } ],
  "master": { "mixer": { "gainDb": -1 }, "effects": [ { "type": "eq", "lowCutHz": 40, "bands": [ { "type": "peak", "freqHz": 2000, "gainDb": 2 } ] },
                                                      { "type": "compressor", "thresholdDb": -20, "ratio": 2 } ] },
  "tracks": [
    { "name": "Keys", "instrument": "synth.piano", "mixer": { "pan": -0.3 },
      "effects": [ { "type": "eq", "highCutHz": 7000 }, { "type": "distortion", "driveDb": 6, "mix": 0.3 } ],
      "sends": [ { "bus": "hall", "levelDb": -8 }, { "bus": "echo", "levelDb": -12 } ],
      "notes": [ { "pitch": "C4", "startBeat": 0, "durationBeat": 1, "velocity": 90 }, { "pitch": "E4", "startBeat": 1, "durationBeat": 1, "velocity": 80 },
                 { "pitch": "G4", "startBeat": 2, "durationBeat": 1, "velocity": 85 }, { "pitch": "B4", "startBeat": 3, "durationBeat": 2, "velocity": 70 } ] },
    { "name": "Pad", "instrument": "synth.pad", "mixer": { "gainDb": -6, "pan": 0.4 }, "sends": [ { "bus": "hall", "levelDb": -4, "preFader": true } ],
      "notes": [ { "pitch": "C3", "startBeat": 0, "durationBeat": 5, "velocity": 70 }, { "pitch": "G3", "startBeat": 0, "durationBeat": 5, "velocity": 70 } ] } ] })";

} // namespace

TEST (realtime_apply_matches_a_player_built_from_the_edited_song)
{
    // 페이더, 밸런스, 센드, 버스 뮤트, 효과 매개변수, 우회, 음을 한꺼번에 바꾼다. 재생 전에 반영하면 새로 만든 재생기와 샘플 단위로 같다.
    const auto before = composition (busyJson);
    const auto after = composition (edit (busyJson, {
        { R"("mixer": { "pan": -0.3 })", R"("mixer": { "gainDb": -4.5, "pan": 0.2 })" },
        { R"({ "bus": "hall", "levelDb": -8 }, { "bus": "echo", "levelDb": -12 })", R"({ "bus": "echo", "levelDb": -6, "preFader": true })" },
        { R"({ "type": "distortion", "driveDb": 6, "mix": 0.3 })", R"({ "type": "distortion", "driveDb": 6, "mix": 0.3, "bypass": true }, { "type": "gain", "gainDb": 2 })" },
        { R"({ "type": "reverb", "mix": 1, "decaySeconds": 1.4 })", R"({ "type": "reverb", "mix": 1, "decaySeconds": 2.0 })" },
        { R"("mixer": { "gainDb": -3 })", R"("mixer": { "gainDb": -3, "mute": true })" },
        { R"({ "type": "compressor", "thresholdDb": -20, "ratio": 2 })", R"({ "type": "compressor", "thresholdDb": -26, "ratio": 3 })" },
        { R"("mixer": { "gainDb": -1 })", R"("mixer": { "gainDb": 1.5, "pan": -0.1 })" },
        { R"({ "pitch": "E4", "startBeat": 1, "durationBeat": 1, "velocity": 80 })", R"({ "pitch": "F4", "startBeat": 1.5, "durationBeat": 1, "velocity": 95 })" },
        { R"("mixer": { "gainDb": -6, "pan": 0.4 })", R"("mixer": { "gainDb": -2, "pan": -0.5, "solo": false })" } }));

    RealtimePlayer fresh (after, playerOptions());
    RealtimePlayer applied (before, playerOptions());
    CHECK (applied.apply (after));
    CHECK_EQ (applied.length(), fresh.length());
    fresh.play();
    applied.play();
    const double seconds = (double) fresh.length() / kRate;
    const auto a = pull (fresh, seconds), b = pull (applied, seconds);
    CHECK (rms (a, 0.2, 2.0) > 0.01f);
    CHECK_EQ (maxDiff (a, b), 0.0);
    applied.collectRetired();

    // 버스를 다시 켜고 트랙을 끈다: 출력 지연(버스 리버브의 지연 포함)만큼 당기면 오프라인 렌더와 같다
    const auto offlineSong = composition (edit (busyJson, { { R"("mixer": { "gainDb": -6, "pan": 0.4 })", R"("mixer": { "gainDb": -6, "pan": 0.4, "mute": true })" },
                                                            { R"({ "type": "reverb", "mix": 1, "decaySeconds": 1.4 })", R"({ "type": "reverb", "mix": 1, "decaySeconds": 0.8 })" } }));
    RealtimePlayer live (before, playerOptions());
    CHECK (live.apply (offlineSong));
    const int latency = live.outputLatencySamples();
    live.play();
    const auto reference = offline (offlineSong, "rt_apply_reference.wav");
    const auto played = pull (live, (double) (reference.getNumSamples() + latency) / kRate);
    double worst = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < reference.getNumSamples(); ++i)
            worst = std::max (worst, (double) std::abs (played.getSample (ch, i + latency) - reference.getSample (ch, i)));
    CHECK (latency > 0);
    CHECK (worst < 1.0e-5);
    // 버스 효과의 지연이 바뀌는 교체는 재생기를 다시 만든다 (버스 지연 맞추기가 바뀐다)
    RealtimePlayer other (before, playerOptions());
    CHECK (! other.apply (composition (edit (busyJson, { { R"({ "type": "reverb", "mix": 1, "decaySeconds": 1.4 })", R"({ "type": "eq", "lowCutHz": 300 })" } }))));
}

TEST (realtime_apply_while_playing_keeps_effect_state)
{
    // 재생 중에 효과 객체를 다시 만들게 하되 매개변수는 그대로 (주석만 바꾼다): 출력이 끊김 없이 한 샘플도 달라지지 않는다
    const auto song = composition (busyJson);
    const auto touched = composition (edit (busyJson, {
        { R"({ "type": "reverb", "mix": 1, "decaySeconds": 1.4 })", R"({ "type": "reverb", "mix": 1, "decaySeconds": 1.4, "comment": "a" })" },
        { R"({ "type": "delay", "timeMs": 180, "feedback": 0.5, "mix": 1 })", R"({ "type": "delay", "timeMs": 180, "feedback": 0.5, "mix": 1, "comment": "b" })" },
        { R"({ "type": "eq", "highCutHz": 7000 })", R"({ "type": "eq", "highCutHz": 7000, "comment": "c" })" },
        { R"({ "type": "compressor", "thresholdDb": -20, "ratio": 2 })", R"({ "type": "compressor", "thresholdDb": -20, "ratio": 2, "comment": "d" })" } }));
    RealtimePlayer steady (song, playerOptions()), swapped (song, playerOptions());
    steady.play();
    swapped.play();
    const auto a1 = pull (steady, 1.3), b1 = pull (swapped, 1.3);
    CHECK (swapped.apply (touched));
    const auto a2 = pull (steady, 2.0), b2 = pull (swapped, 2.0);
    CHECK_EQ (maxDiff (a1, b1), 0.0);
    CHECK_EQ (maxDiff (a2, b2), 0.0);
    CHECK (rms (a2, 0.0, 1.0) > 0.001f);
    swapped.collectRetired();
}

TEST (realtime_apply_ramps_fader_changes_while_playing)
{
    const auto c = composition (R"({ "tempo": 60, "render": { "tailSeconds": 0.2 },
      "tracks": [ { "name": "Tone", "instrument": "synth.sine", "notes": [ { "pitch": "A4", "startBeat": 0, "durationBeat": 8, "velocity": 100 } ] } ] })");
    RealtimePlayer player (c, playerOptions());
    player.play();
    const auto loud = pull (player, 1.0, 512);
    auto quieter = c;
    quieter.tracks[0].mixer.gainDb = -6.0206;
    CHECK (player.apply (quieter));
    const auto after = pull (player, 1.0, 512);
    CHECK_NEAR (rms (after, 0.2, 0.9) / rms (loud, 0.2, 0.9), 0.5, 0.005);
    // 첫 블록은 직선으로 내려간다 (한 번에 뛰지 않는다)
    double step = 0.0;
    for (int i = 1; i < 512; ++i)
        step = std::max (step, (double) std::abs (after.getSample (0, i) - after.getSample (0, i - 1)));
    double normalStep = 0.0;
    for (int i = 24001; i < 24512; ++i)
        normalStep = std::max (normalStep, (double) std::abs (loud.getSample (0, i) - loud.getSample (0, i - 1)));
    CHECK (step <= normalStep * 1.05);
    CHECK (player.takeTrackPeaks (0).left > 0.0f);
    CHECK (player.takeMasterPeaks().right > 0.0f);
}

TEST (realtime_apply_refuses_changes_that_need_a_new_player)
{
    const auto song = composition (busyJson);
    auto refused = [&song] (const std::vector<std::pair<std::string, std::string>>& change)
    {
        RealtimePlayer player (song, playerOptions());
        const auto edited = composition (edit (busyJson, change));
        const bool ok = player.apply (edited);
        // 거절하면 아무것도 바꾸지 않는다 (함께 고친 페이더도)
        return ! ok && player.composition().tracks[1].mixer.gainDb == -6.0;
    };
    const std::pair<std::string, std::string> fader { R"("mixer": { "gainDb": -6, "pan": 0.4 })", R"("mixer": { "gainDb": 0, "pan": 0.4 })" };
    CHECK (refused ({ fader, { R"("tempo": 100)", R"("tempo": 110)" } }));
    CHECK (refused ({ fader, { R"("instrument": "synth.pad")", R"("instrument": "synth.strings")" } }));
    CHECK (refused ({ fader, { R"("name": "echo")", R"("name": "echo2")" }, { R"("bus": "echo")", R"("bus": "echo2")" } }));
    CHECK (refused ({ fader, { R"({ "type": "eq", "highCutHz": 7000 })", R"({ "type": "eq", "highCutHz": 7000 }, { "type": "limiter" })" } }));
    CHECK (refused ({ fader, { R"({ "type": "eq", "highCutHz": 7000 })", R"({ "type": "compressor", "sidechain": "Pad" })" } }));
    CHECK (refused ({ fader, { R"("render": { "tailSeconds": 1.0 },)", R"("render": { "tailSeconds": 1.0 }, "performance": { "swing": 0.2 },)" } }));

    // 지연이 같은 효과 바꾸기와 마스터 리미터는 받는다 (마스터와 버스의 지연은 실시간에서 맞추지 않는다)
    RealtimePlayer player (song, playerOptions());
    CHECK (player.apply (composition (edit (busyJson, { { R"({ "type": "compressor", "thresholdDb": -20, "ratio": 2 })",
                                                          R"({ "type": "compressor", "thresholdDb": -20, "ratio": 2 }, { "type": "limiter", "ceilingDb": -2 })" } }))));
    CHECK_EQ (player.composition().master.effects.size(), (size_t) 3);
}

TEST (realtime_audition_plays_a_transposed_note_while_stopped)
{
    // 트랙의 이조(+12)를 재생과 같게 쓴다: 곡의 A4 를 누르면 880 Hz. 곡의 음은 20초 뒤라 멈춘 0초는 조용하다.
    const auto c = composition (R"({ "tempo": 60, "render": { "tailSeconds": 0.5 },
      "tracks": [ { "name": "Sine", "instrument": "synth.sine", "transpose": 12,
                    "notes": [ { "pitch": "A3", "startBeat": 20, "durationBeat": 1, "velocity": 90 } ] } ] })");
    RealtimePlayer player (c, playerOptions());
    const auto before = pull (player, 0.2);
    player.auditionNoteOn (0, 69, 100);
    const auto held = pull (player, 0.6);
    player.auditionNoteOff (0, 69);
    const auto released = pull (player, 1.5);
    CHECK (rms (before, 0.0, 0.2) < 1.0e-6f);
    CHECK (rms (held, 0.1, 0.6) > 0.01f);
    CHECK (std::abs (frequency (held, 0.1, 0.6) - 880.0) < 3.0);
    CHECK (rms (released, 1.0, 1.5) < 1.0e-4f);
    CHECK (! player.isPlaying());
    CHECK_EQ (player.position(), (int64_t) 0);
}

TEST (realtime_audition_leaves_playback_like_the_offline_render)
{
    const auto c = composition (mixJson);
    const auto reference = offline (c, "rt_audition_reference.wav");
    RealtimePlayer player (c, playerOptions());
    player.auditionNoteOn (0, 72, 90);
    player.auditionNoteOn (1, 52, 70);
    const auto held = pull (player, 0.3);
    player.auditionAllOff();
    pull (player, 4.0);   // 울림이 사라질 때까지
    player.play();
    const auto live = pull (player, (double) reference.getNumSamples() / kRate);
    double maxDiff = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < reference.getNumSamples(); ++i)
            maxDiff = std::max (maxDiff, (double) std::abs (live.getSample (ch, i) - reference.getSample (ch, i)));
    CHECK (rms (held, 0.05, 0.3) > 0.005f);
    CHECK (maxDiff < 1.0e-5);
}

TEST (realtime_stop_releases_an_audition_held_while_playing)
{
    const auto c = composition (R"({ "tempo": 60, "render": { "tailSeconds": 1 },
      "tracks": [ { "name": "Pad", "instrument": "synth.pad", "notes": [ { "pitch": "A3", "startBeat": 8, "durationBeat": 1, "velocity": 90 } ] } ] })");
    RealtimePlayer player (c, playerOptions());
    player.play();
    player.auditionNoteOn (0, 64, 90);
    const auto playing = pull (player, 1.0);
    player.stop();
    const auto after = pull (player, 4.0);
    CHECK (rms (playing, 0.5, 1.0) > 0.005f);
    CHECK (rms (after, 3.5, 4.0) < 0.001f * rms (playing, 0.5, 1.0) + 1.0e-6f);
}

TEST (realtime_play_range_stops_on_the_end_sample)
{
    // 1~3초의 음 둘. 1초에서 2초까지만 재생하면 그 구간은 오프라인과 샘플까지 같고, 2초 샘플에서 멈춰 위치가 거기 남는다.
    const auto c = composition (R"({ "tempo": 60, "render": { "tailSeconds": 1 },
      "tracks": [ { "name": "Keys", "instrument": "synth.piano", "notes": [
        { "pitch": "A4", "startBeat": 1, "durationBeat": 2, "velocity": 90 }, { "pitch": "E5", "startBeat": 1.5, "durationBeat": 1.5, "velocity": 80 } ] } ] })");
    const auto reference = offline (c, "rt_range_reference.wav");
    RealtimePlayer player (c, playerOptions());
    const int64_t from = (int64_t) kRate, to = (int64_t) (2.0 * kRate);
    player.playRange (from, to);
    CHECK (player.playingRange());
    const auto live = pull (player, 3.0);
    CHECK (! player.isPlaying());
    CHECK (! player.playingRange());
    CHECK_EQ (player.position(), to);
    double maxDiff = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < (int) (to - from); ++i)
            maxDiff = std::max (maxDiff, (double) std::abs (live.getSample (ch, i) - reference.getSample (ch, i + (int) from)));
    CHECK (rms (live, 0.1, 0.9) > 0.01f);
    CHECK (maxDiff < 1.0e-5);
    CHECK (rms (live, 1.2, 1.6) < 0.5f * rms (reference, 2.2, 2.6));   // 끝에서 음을 뗐다 (오프라인은 3초까지 누른다)
    // 다시 play() 하면 끝 없이 이어서 재생한다
    player.play();
    pull (player, 0.5);
    CHECK (player.isPlaying() && ! player.playingRange());
}

namespace
{
// 반복 시험용: 120 BPM, 플럭 세 음. 반복 구간 [2, 4)박의 끝에서는 울리는 음이 없다 (마지막 음은 3박에서 끝난다).
const char* loopJson = R"({ "tempo": 120, "render": { "tailSeconds": 1 },
  "tracks": [ { "name": "Pluck", "instrument": "synth.pluck", "notes": [
    { "pitch": "C4", "startBeat": 0, "durationBeat": 0.5, "velocity": 90 }, { "pitch": "E4", "startBeat": 2, "durationBeat": 0.5, "velocity": 90 },
    { "pitch": "G4", "startBeat": 2.5, "durationBeat": 0.5, "velocity": 80 } ] } ] })";

double maxDifference (const juce::AudioBuffer<float>& a, int aFrom, const juce::AudioBuffer<float>& b, int bFrom, int count)
{
    double maxDiff = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < count; ++i)
            maxDiff = std::max (maxDiff, (double) std::abs (a.getSample (ch, aFrom + i) - b.getSample (ch, bFrom + i)));
    return maxDiff;
}

float peak (const juce::AudioBuffer<float>& b, double fromSec, double toSec)
{
    return b.getMagnitude (0, (int) (fromSec * kRate), (int) ((toSec - fromSec) * kRate));
}
} // namespace

TEST (realtime_loop_wraps_to_the_loop_start_like_a_fresh_start)
{
    const auto c = composition (loopJson);
    const auto reference = offline (c, "rt_loop_reference.wav");
    RealtimePlayer player (c, playerOptions());
    CHECK (! player.hasLoopRange());
    player.setLoopRange ((int64_t) (1.0 * kRate), (int64_t) (2.0 * kRate));   // 2~4박
    player.setLooping (true);
    CHECK (player.hasLoopRange() && player.looping());
    player.play();
    const auto live = pull (player, 4.0, 700);
    const int half = (int) kRate, second = 2 * half;
    CHECK (maxDifference (live, 0, reference, 0, second) < 1.0e-5);                 // 처음부터 반복 끝까지는 곡 그대로
    CHECK (maxDifference (live, second, reference, half, half) < 1.0e-5);          // 되돌아간 뒤는 2박에서 새로 시작한 재생과 같다
    CHECK (maxDifference (live, second + half, reference, half, half) < 1.0e-5);   // 두 번째 되풀이
    CHECK (rms (live, 2.0, 2.3) > 0.01f);
    CHECK (player.isPlaying());
    CHECK (player.position() >= half && player.position() < second);

    // 구간을 지우면 끝까지 재생하고 멈춘다
    player.setLoopRange (0, 0);
    pull (player, 3.0);
    CHECK (! player.isPlaying());
    CHECK_EQ (player.position(), player.length());
}

TEST (realtime_metronome_clicks_on_every_beat_and_accents_the_bar)
{
    const auto c = composition (R"({ "tempo": 120, "timeSignature": [3, 4], "render": { "tailSeconds": 0.5 },
      "tracks": [ { "name": "Keys", "instrument": "synth.piano", "notes": [ { "pitch": "C4", "startBeat": 100, "durationBeat": 1, "velocity": 90 } ] } ] })");
    RealtimePlayer player (c, playerOptions());
    CHECK (! player.metronome());
    player.setMetronome (true);
    player.play();
    const auto live = pull (player, 2.0, 700);
    for (int beat = 0; beat < 4; ++beat)
    {
        const double t = beat * 0.5;
        CHECK (peak (live, t, t + 0.03) > 0.05f);
        CHECK (peak (live, t + 0.08, t + 0.48) < 1.0e-3f);
    }
    CHECK (std::abs (frequency (live, 0.0, 0.015) - 1760.0) < 80.0);    // 마디 첫 박
    CHECK (std::abs (frequency (live, 0.5, 0.515) - 1320.0) < 80.0);    // 둘째 박
    CHECK (std::abs (frequency (live, 1.5, 1.515) - 1760.0) < 80.0);    // 둘째 마디
    CHECK (peak (live, 0.0, 0.03) > peak (live, 0.5, 0.53));            // 강세는 더 크다

    player.setMetronomeGainDb (-20.0);
    const auto quieter = pull (player, 0.5, 700);
    CHECK (peak (quieter, 0.0, 0.03) < peak (live, 0.0, 0.03) * 0.3f);
    player.setMetronome (false);
    const auto silent = pull (player, 1.0, 700);
    CHECK (peak (silent, 0.0, 1.0) < 1.0e-6f);
}

TEST (realtime_count_in_clicks_then_starts_at_the_same_sample)
{
    const auto c = composition (loopJson);
    const auto reference = offline (c, "rt_countin_reference.wav");
    RealtimePlayer player (c, playerOptions());
    const int64_t start = (int64_t) (1.0 * kRate);   // 2박
    player.seek (start);
    player.playWithCountIn (1);   // 4/4 한 마디 = 2초
    CHECK (player.countingIn() && ! player.isPlaying());
    const auto first = pull (player, 1.0, 700);
    CHECK (player.countingIn() && ! player.isPlaying());
    CHECK_EQ (player.position(), start);
    CHECK (peak (first, 0.0, 0.03) > 0.05f && peak (first, 0.5, 0.53) > 0.05f);
    CHECK (peak (first, 0.08, 0.48) < 1.0e-3f && peak (first, 0.58, 0.98) < 1.0e-3f);
    CHECK (std::abs (frequency (first, 0.0, 0.015) - 1320.0) < 80.0);   // 카운트인은 -2박에서 시작한다 (마디 가운데)
    const auto second = pull (player, 1.0, 700);
    CHECK (peak (second, 0.0, 0.03) > 0.05f && peak (second, 0.5, 0.53) > 0.05f);
    CHECK (std::abs (frequency (second, 0.0, 0.015) - 1760.0) < 80.0);   // 0박이 마디의 첫 박
    CHECK (player.isPlaying() && ! player.countingIn());
    CHECK_EQ (player.position(), start);
    const auto song = pull (player, 1.0, 700);
    CHECK (maxDifference (song, 0, reference, (int) start, (int) kRate) < 1.0e-5);   // 카운트인 뒤는 2박에서 시작한 재생과 같다
    CHECK (rms (song, 0.0, 0.3) > 0.01f);
    CHECK_EQ (player.position(), start + (int64_t) kRate);

    // 카운트인 중에 멈추면 재생하지 않는다
    player.seek (start);
    player.playWithCountIn (2);
    pull (player, 0.5);
    player.stop();
    CHECK (! player.countingIn() && ! player.isPlaying());
    pull (player, 4.5);
    CHECK (! player.isPlaying());
    CHECK_EQ (player.position(), start);
    player.playWithCountIn (0);   // 0 마디는 바로 재생
    CHECK (player.isPlaying() && ! player.countingIn());
}

TEST (realtime_audition_controller_reaches_the_track)
{
    const auto c = composition (R"({ "tempo": 60, "render": { "tailSeconds": 0.5 },
      "tracks": [ { "name": "Bass", "instrument": "synth.bass", "notes": [ { "pitch": "A1", "startBeat": 20, "durationBeat": 1, "velocity": 90 } ] } ] })");
    RealtimePlayer player (c, playerOptions());
    player.auditionController (0, 74, 0);     // 거르개를 닫고
    player.auditionNoteOn (0, 45, 100);
    const auto closed = pull (player, 0.5);
    player.auditionNoteOff (0, 45);
    pull (player, 1.0);
    player.auditionController (0, 74, 127);   // 열고
    player.auditionNoteOn (0, 45, 100);
    const auto open = pull (player, 0.5);
    player.auditionNoteOff (0, 45);
    CHECK (rms (closed, 0.1, 0.5) > 0.001f);
    CHECK (rms (open, 0.1, 0.5) > rms (closed, 0.1, 0.5) * 1.2f);
}

namespace
{
// 곡선 필터, 게인, 덕킹을 트랙, 버스, 마스터에. 센드 크기 곡선과 박에 맞춘 딜레이 버스. 패드는 4박부터 (8박 동안 쓸기)
const char* sweepJson = R"({ "tempo": [ { "beat": 0, "bpm": 126, "ramp": true }, { "beat": 16, "bpm": 132 } ], "render": { "tailSeconds": 0.5 },
  "buses": [ { "name": "echo", "effects": [ { "type": "delay", "timeBeats": 0.75, "feedback": 0.4, "mix": 1 },
                                            { "type": "filter", "mode": "lowpass", "cutoffHz": 8000,
                                              "automation": { "cutoffHz": [ { "beat": 4, "value": 8000 }, { "beat": 12, "value": 400, "curve": "exponential" } ] } },
                                            { "type": "ducker", "depthDb": -9, "releaseBeats": 0.4 } ] } ],
  "master": { "effects": [ { "type": "filter", "mode": "highpass", "slope": 24, "cutoffHz": 20,
                             "automation": { "cutoffHz": [ { "beat": 2, "value": 20 }, { "beat": 14, "value": 600 } ], "resonance": [ { "beat": 2, "value": 0 }, { "beat": 14, "value": 0.5 } ] } },
                           { "type": "gain", "automation": { "gainDb": [ { "beat": 13, "value": 0 }, { "beat": 15, "value": -24, "curve": "smooth" } ] } } ] },
  "tracks": [
    { "name": "Pad", "instrument": "synth.saw-pad",
      "effects": [ { "type": "filter", "mode": "lowpass", "slope": 24, "cutoffHz": 300,
                     "automation": { "cutoffHz": [ { "beat": 4, "value": 300 }, { "beat": 12, "value": 9000 } ], "resonance": [ { "beat": 4, "value": 0.6 } ] } },
                   { "type": "ducker", "rateBeats": 1, "depthDb": 0, "automation": { "depthDb": [ { "beat": 8, "value": 0, "curve": "step" }, { "beat": 8, "value": -15 } ] } },
                   { "type": "gain", "automation": { "gainDb": [ { "beat": 4, "value": -12 }, { "beat": 6, "value": 0 } ] } } ],
      "sends": [ { "bus": "echo", "levelDb": -6, "automation": { "levelDb": [ { "beat": 4, "value": -120 }, { "beat": 10, "value": -3 }, { "beat": 11, "value": -120, "curve": "step" } ] } } ],
      "notes": [ { "pitch": "A3", "startBeat": 4, "durationBeat": 10, "velocity": 90 }, { "pitch": "E4", "startBeat": 4, "durationBeat": 10, "velocity": 90 },
                 { "pitch": "C5", "startBeat": 6, "durationBeat": 6, "velocity": 80 } ] },
    { "name": "Pluck", "instrument": "synth.pluck", "sends": [ { "bus": "echo", "levelDb": -9 } ],
      "notes": [ { "pitch": "E5", "startBeat": 4.5, "durationBeat": 0.25, "velocity": 100 }, { "pitch": "G5", "startBeat": 7.25, "durationBeat": 0.25, "velocity": 100 },
                 { "pitch": "B5", "startBeat": 10.5, "durationBeat": 0.25, "velocity": 100 } ] } ] })";
} // namespace

TEST (realtime_matches_offline_with_automated_effects_and_sends)
{
    const auto c = composition (sweepJson);
    const auto reference = offline (c, "rt_sweep_reference.wav");
    RealtimePlayer player (c, playerOptions());
    CHECK_EQ (player.length(), (int64_t) reference.getNumSamples());
    player.play();
    const double seconds = (double) reference.getNumSamples() / kRate;
    const auto live = pull (player, seconds);
    CHECK (rms (reference, 3.0, 5.0) > 0.01f);
    CHECK (maxDiff (reference, live) < 1.0e-5);

    // 쓸기 한가운데(3.5박, 패드가 울리기 전)로 옮겨 재생해도 그 뒤는 오프라인과 같다
    RealtimePlayer seeking (c, playerOptions());
    const int64_t from = TempoMap (c.tempo).sampleAt (3.5, kRate);
    seeking.seek (from);
    seeking.play();
    const auto after = pull (seeking, (double) (reference.getNumSamples() - from) / kRate, 512);
    double diff = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < std::min (after.getNumSamples(), reference.getNumSamples() - (int) from); ++i)
            diff = std::max (diff, (double) std::abs (after.getSample (ch, i) - reference.getSample (ch, i + (int) from)));
    CHECK (diff < 1.0e-5);

    // 곡선을 고친 곡을 재생 전에 반영하면 새로 만든 재생기와 같다 (효과 사슬과 센드를 바꿔 끼운다)
    std::string edited = sweepJson;
    const std::string a = R"({ "beat": 12, "value": 9000 })", b = R"({ "beat": 12, "value": 2000, "curve": "smooth" })";
    edited.replace (edited.find (a), a.size(), b);
    const std::string s1 = R"({ "beat": 10, "value": -3 })", s2 = R"({ "beat": 9, "value": 0 })";
    edited.replace (edited.find (s1), s1.size(), s2);
    const auto after2 = composition (edited);
    RealtimePlayer fresh (after2, playerOptions()), applied (c, playerOptions());
    CHECK (applied.apply (after2));
    fresh.play();
    applied.play();
    CHECK_EQ (maxDiff (pull (fresh, seconds), pull (applied, seconds)), 0.0);
    applied.collectRetired();
}


namespace
{
// 멈춘 동안의 시험: 덕킹, 곡선 게인, CC11 곡선, 센드 곡선, 곡선 버스 게인, 곡선 마스터 필터. 곡의 음은 40박에만 (곡 길이)
const char* heldJson = R"({ "tempo": 120, "render": { "tailSeconds": 0.5 },
  "buses": [ { "name": "fx", "effects": [ { "type": "gain", "automation": { "gainDb": [ { "beat": 2, "value": 0 }, { "beat": 2.2, "value": -12 } ] } } ] } ],
  "master": { "effects": [ { "type": "filter", "mode": "lowpass", "cutoffHz": 8000,
                             "automation": { "cutoffHz": [ { "beat": 2, "value": 8000 }, { "beat": 3, "value": 500 } ] } } ] },
  "tracks": [ { "name": "Pad", "instrument": "synth.saw-pad", "synth": { "attack": 0.002 },
      "cc11": [ { "beat": 2, "value": 127 }, { "beat": 2.1, "value": 60 } ],
      "effects": [ { "type": "ducker", "rateBeats": 1, "depthDb": -12, "attackMs": 1, "holdMs": 30, "releaseBeats": 0.5 },
                   { "type": "gain", "automation": { "gainDb": [ { "beat": 2, "value": 0 }, { "beat": 2.05, "value": -6 } ] } } ],
      "sends": [ { "bus": "fx", "levelDb": -6, "automation": { "levelDb": [ { "beat": 2, "value": -3 }, { "beat": 2.3, "value": -20 } ] } } ],
      "notes": [ { "pitch": "A3", "startBeat": 40, "durationBeat": 1, "velocity": 90 } ] } ] })";

// 멈춘 채(또는 카운트인 중에) at 에서 패드의 음 하나를 1초 들려준다
juce::AudioBuffer<float> auditionAt (const Composition& c, int64_t at, int callbackSize, bool countIn)
{
    RealtimePlayer p (c, playerOptions());
    p.seek (at);
    if (countIn)
        p.playWithCountIn (1);
    p.auditionNoteOn (0, 57, 100);
    auto out = pull (p, 1.0, callbackSize);
    if (countIn)
        CHECK (p.countingIn() && ! p.isPlaying());
    else
        CHECK (! p.isPlaying());
    CHECK_EQ (p.position(), at);
    return out;
}

double maxDiffIn (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, double fromSec, double toSec)
{
    return maxDifference (a, (int) (fromSec * kRate), b, (int) (fromSec * kRate), (int) ((toSec - fromSec) * kRate));
}
} // namespace

TEST (realtime_holds_automation_and_the_ducker_while_stopped)
{
    // 2박에서 100ms 뒤: 덕킹이 돌아오는 중이고 곡선도 바뀌는 중이다
    const auto c = composition (heldJson);
    const int64_t at = TempoMap (c.tempo).sampleAt (2.2, kRate);
    const auto params = juce::JSON::parse (R"({ "type": "ducker", "rateBeats": 1, "depthDb": -12, "attackMs": 1, "holdMs": 30, "releaseBeats": 0.5 })");
    const double shape = duckerShape (params, TempoMap (c.tempo), kRate, at);
    CHECK (shape > 0.5 && shape < 0.9);

    // 블록 크기와 상관없이 같다 (블록마다 곡선을 되풀이하면 블록 주기로 출렁인다)
    const auto a = auditionAt (c, at, 512, false);
    const auto b = auditionAt (c, at, 97, false);
    CHECK (rms (a, 0.2, 1.0) > 5.0e-4f);
    CHECK (maxDiffIn (a, b, 0.0, 1.0) < 1.0e-5);

    // 멈춘 동안의 덕킹은 그 자리의 크기 하나다: 그 크기의 고정 게인을 둔 곡과 같다
    std::string fixed = heldJson;
    const std::string ducker = R"({ "type": "ducker", "rateBeats": 1, "depthDb": -12, "attackMs": 1, "holdMs": 30, "releaseBeats": 0.5 })";
    char gain[96];
    std::snprintf (gain, sizeof (gain), R"({ "type": "gain", "gainDb": %.17g })", -12.0 * shape);
    fixed.replace (fixed.find (ducker), ducker.size(), gain);
    const auto g = auditionAt (composition (fixed), at, 512, false);
    CHECK (maxDiffIn (a, g, 0.0, 1.0) < 1.0e-5);

    // 카운트인 동안도 멈춘 것과 같다 (클릭이 없는 자리에서 비교한다: 클릭은 0초와 0.5초, 30ms)
    const auto k = auditionAt (c, at, 300, true);
    CHECK (maxDiffIn (a, k, 0.05, 0.48) < 1.0e-5);
    CHECK (maxDiffIn (a, k, 0.55, 0.98) < 1.0e-5);
}

namespace
{
// 반복 시험: 7~8박에서 -96 dB 로 끊는 게인 곡선 (마스터, 또는 리버브 뒤의 트랙 효과). 다른 트랙의 리버브가 재생 지연 1024 를 만든다.
std::string wrapJson (bool onTrack)
{
    const std::string lane = R"({ "type": "gain", "automation": { "gainDb": [ { "beat": 0, "value": 0, "curve": "step" }, { "beat": 7, "value": -96, "curve": "step" }, { "beat": 8, "value": 0 } ] } })";
    return std::string (R"({ "tempo": 120, "render": { "tailSeconds": 0.5 },
  "master": { "effects": [ )") + (onTrack ? "" : lane) + R"( ] },
  "tracks": [
    { "name": "Pad", "instrument": "synth.saw-pad", "synth": { "attack": 0.002 },
      "effects": [ )" + (onTrack ? R"({ "type": "reverb", "mix": 0 }, )" + lane : std::string()) + R"( ],
      "notes": [ { "pitch": "A3", "startBeat": 0, "durationBeat": 16, "velocity": 90 } ] },
    { "name": "Room", "instrument": "synth.sine", "effects": [ { "type": "reverb", "mix": 0.3 } ],
      "notes": [ { "pitch": "A4", "startBeat": 15, "durationBeat": 1, "velocity": 60 } ] } ] })";
}
} // namespace

TEST (realtime_loop_wrap_reads_lanes_where_the_sound_came_from)
{
    // 되돌아간 뒤 1024 샘플 동안 나오는 소리는 반복 끝 앞(7~8박, -96 dB)의 소리다. 곡선도 그 자리에서 읽어야 끊긴 채로 남는다.
    for (const bool onTrack : { false, true })
    {
        const auto c = composition (wrapJson (onTrack));
        RealtimePlayer player (c, playerOptions());
        const int latency = player.trackInfo (1).latencySamples;
        CHECK (latency >= 512);
        player.setLoopRange (0, (int64_t) (4.0 * kRate));   // 0~8박
        player.setLooping (true);
        player.play();
        const auto live = pull (player, 5.0, 300);
        const int wrap = (int) (4.0 * kRate);
        const float loud = live.getRMSLevel (0, (int) (1.0 * kRate), (int) (2.0 * kRate));
        CHECK (loud > 0.01f);
        CHECK (live.getRMSLevel (0, wrap - (int) (0.3 * kRate), (int) (0.3 * kRate)) < loud * 1.0e-3f);   // 7.4~8박은 끊겼다
        // 되돌아간 뒤에도 그 소리는 끊긴 채 (끝의 32 샘플 격자 칸은 8박의 0 dB 로 오른다)
        CHECK (live.getRMSLevel (0, wrap, latency - 64) < loud * 1.0e-3f);
        CHECK (live.getRMSLevel (0, wrap + latency + (int) (0.1 * kRate), (int) (0.4 * kRate)) > loud * 0.3f);   // 0박의 소리는 돌아온다
    }
}

namespace
{
// 위치 옮기기 시험: 박마다 치는 패드, 리버브(지연 1024) 뒤의 곡선 필터와 덕킹, 마스터의 곡선 고역 통과
std::string seekSweepJson()
{
    std::string notes;
    for (int beat = 0; beat < 16; ++beat)
        notes += std::string (beat > 0 ? ", " : "") + R"({ "pitch": ")" + (beat % 2 == 0 ? "A3" : "C4") + R"(", "startBeat": )" + std::to_string (beat)
                 + R"(, "durationBeat": 1, "velocity": 90 })";
    return R"({ "tempo": 120, "render": { "tailSeconds": 0.3 },
  "master": { "effects": [ { "type": "filter", "mode": "highpass", "cutoffHz": 20, "automation": { "cutoffHz": [ { "beat": 0, "value": 20 }, { "beat": 16, "value": 800 } ] } } ] },
  "tracks": [ { "name": "Pad", "instrument": "synth.saw-pad", "synth": { "attack": 0.002, "release": 0.01 },
      "effects": [ { "type": "reverb", "mix": 0 },
                   { "type": "filter", "mode": "lowpass", "slope": 24, "cutoffHz": 300, "resonance": 0.3,
                     "automation": { "cutoffHz": [ { "beat": 0, "value": 300 }, { "beat": 16, "value": 6000 } ] } },
                   { "type": "ducker", "depthDb": -9, "releaseBeats": 0.4 } ],
      "notes": [ )" + notes + R"( ] } ] })";
}
} // namespace

TEST (realtime_seek_while_a_swept_pad_sounds)
{
    const auto c = composition (seekSweepJson());
    const auto reference = offline (c, "rt_seek_sweep_reference.wav");
    RealtimePlayer player (c, playerOptions());
    const int latency = player.trackInfo (0).latencySamples;
    CHECK (latency >= 512);
    player.play();
    const int seekAt = (int) (3.0 * kRate);   // 6박
    const auto before = pull (player, 3.0);
    const int64_t target = TempoMap (c.tempo).sampleAt (10.0, kRate);
    player.seek (target);
    const auto after = pull (player, 3.5);
    CHECK (rms (before, 0.5, 3.0) > 0.004f);
    // 실시간 출력은 재생 지연만큼 늦다: 옮기기 전은 오프라인을 지연만큼 늦춘 것과 같다
    CHECK (maxDifference (before, latency, reference, 0, seekAt - latency) < 1.0e-5);
    // 옮긴 뒤 지연만큼은 옮기기 전의 소리가 그 자리의 곡선으로 나온다 (6박 앞의 1024 샘플)
    CHECK (rms (after, 0.0, (double) latency / kRate) > 0.004f);
    CHECK (maxDifference (after, 0, reference, seekAt - latency, latency) < 1.0e-5);
    // 그 뒤는 10박부터의 오프라인과 같다 (앞 음의 놓는 소리 10ms 가 지난 뒤, 곡선과 덕킹이 그 자리의 값)
    const int settle = (int) (0.05 * kRate);
    CHECK (maxDifference (after, latency + settle, reference, (int) target + settle, (int) (3.0 * kRate)) < 1.0e-5);
    CHECK (rms (after, 0.2, 3.0) > 0.004f);
}

TEST (realtime_apply_while_automation_runs_continues_like_a_fresh_player)
{
    // 곡선이 움직이는 중(2박)에 4박 뒤의 곡선 점만 고친 곡을 반영하면, 처음부터 고친 곡을 재생한 것과 샘플까지 같다
    const std::string original = R"({ "tempo": 120, "render": { "tailSeconds": 0.5 },
  "buses": [ { "name": "echo", "effects": [ { "type": "delay", "timeBeats": 0.75, "feedback": 0.4, "mix": 1 } ] } ],
  "tracks": [ { "name": "Pad", "instrument": "synth.saw-pad",
      "effects": [ { "type": "filter", "mode": "lowpass", "slope": 24, "cutoffHz": 300, "resonance": 0.4,
                     "automation": { "cutoffHz": [ { "beat": 0, "value": 300 }, { "beat": 4, "value": 1200 }, { "beat": 8, "value": 3000 } ] } },
                   { "type": "ducker", "depthDb": -9, "releaseBeats": 0.4 },
                   { "type": "gain", "automation": { "gainDb": [ { "beat": 0, "value": -6 }, { "beat": 4, "value": 0 }, { "beat": 8, "value": -3 } ] } } ],
      "sends": [ { "bus": "echo", "levelDb": -6, "automation": { "levelDb": [ { "beat": 0, "value": -20 }, { "beat": 4, "value": -6 }, { "beat": 8, "value": -12 } ] } } ],
      "notes": [ { "pitch": "A3", "startBeat": 0, "durationBeat": 8, "velocity": 90 }, { "pitch": "E4", "startBeat": 1, "durationBeat": 7, "velocity": 80 } ] } ] })";
    std::string edited = original;
    for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>> {
             { R"({ "beat": 8, "value": 3000 })", R"({ "beat": 8, "value": 800 })" },
             { R"({ "beat": 8, "value": -3 })", R"({ "beat": 6, "value": -9 })" },
             { R"({ "beat": 8, "value": -12 })", R"({ "beat": 8, "value": -2 })" } })
        edited.replace (edited.find (from), from.size(), to);
    const auto c = composition (original), e = composition (edited);
    RealtimePlayer fresh (e, playerOptions()), applied (c, playerOptions());
    fresh.play();
    applied.play();
    const auto a1 = pull (fresh, 1.0, 512);
    const auto b1 = pull (applied, 1.0, 512);
    CHECK_EQ (maxDiff (a1, b1), 0.0);   // 4박 앞은 두 곡이 같다
    CHECK (applied.apply (e));
    const auto a2 = pull (fresh, 4.0, 512);
    const auto b2 = pull (applied, 4.0, 512);
    CHECK (rms (b2, 0.5, 3.0) > 0.01f);
    CHECK_EQ (maxDiff (a2, b2), 0.0);
    applied.collectRetired();
}

TEST (realtime_removing_a_send_lane_ramps_from_the_lane_value)
{
    // 센드 곡선이 -120 dB 로 막고 있던 버스: 곡선을 지우고 0 dB 로 열면 블록 하나에 걸쳐 0 에서 커진다 (튀지 않는다)
    const std::string original = R"({ "tempo": 120, "render": { "tailSeconds": 0.5 },
  "buses": [ { "name": "fx", "effects": [] } ],
  "tracks": [ { "name": "Tone", "instrument": "synth.saw-pad", "synth": { "attack": 0.002 }, "mixer": { "gainDb": -120 },
      "sends": [ { "bus": "fx", "levelDb": 0, "preFader": true, "automation": { "levelDb": [ { "beat": 0, "value": -120 } ] } } ],
      "notes": [ { "pitch": "A3", "startBeat": 0, "durationBeat": 8, "velocity": 90 } ] } ] })";
    std::string edited = original;
    const std::string lane = R"(, "automation": { "levelDb": [ { "beat": 0, "value": -120 } ] })";
    edited.erase (edited.find (lane), lane.size());
    RealtimePlayer player (composition (original), playerOptions()), open (composition (edited), playerOptions());
    player.play();
    open.play();
    const auto closed = pull (player, 1.0, 512);
    pull (open, 1.0, 512);
    CHECK (closed.getMagnitude (0, 0, closed.getNumSamples()) < 1.0e-5f);
    CHECK (player.apply (composition (edited)));
    const auto opened = pull (player, 0.5, 512);
    const auto reference = pull (open, 0.5, 512);
    CHECK (reference.getMagnitude (0, 0, 64) > 0.01f);
    CHECK (opened.getMagnitude (0, 0, 64) < 0.2f * reference.getMagnitude (0, 0, 64));   // 처음 64 샘플의 게인은 1/8 아래
    CHECK (maxDifference (opened, 512, reference, 512, 4096) < 1.0e-6);                  // 블록 하나 뒤에는 열린 센드와 같다
    player.collectRetired();
}

TEST (realtime_metronome_follows_meter_changes)
{
    // 4/4 한 마디 뒤 3/4, 그 뒤 6/8 (한 마디가 4분음표 셋): 강세는 0, 4, 7, 10 박
    const auto c = composition (R"({ "tempo": 120, "timeSignature": [4, 4], "meterChanges": [ { "beat": 4, "timeSignature": [3, 4] }, { "beat": 7, "timeSignature": [6, 8] } ],
      "render": { "tailSeconds": 0.5 },
      "tracks": [ { "name": "Keys", "instrument": "synth.piano", "notes": [ { "pitch": "C4", "startBeat": 100, "durationBeat": 1, "velocity": 90 } ] } ] })");
    RealtimePlayer player (c, playerOptions());
    player.setMetronome (true);
    player.play();
    const auto live = pull (player, 6.0, 700);
    for (int beat = 0; beat < 12; ++beat)
    {
        const double t = beat * 0.5;
        const bool accent = beat == 0 || beat == 4 || beat == 7 || beat == 10;
        CHECK (std::abs (frequency (live, t, t + 0.015) - (accent ? 1760.0 : 1320.0)) < 80.0);
    }
}

TEST (realtime_compensates_bus_latency)
{
    // 버스의 리미터(미리보기 지연)와 버스 페이더 곡선, 마스터 압축기의 키 트랙: 실시간 출력을 재생기의 출력 지연만큼 당기면 오프라인과 같다
    const auto c = composition (R"({ "tempo": 120, "render": { "tailSeconds": 1.0 },
      "buses": [ { "name": "crush", "mixer": { "automation": { "gainDb": [ { "beat": 0, "value": -12 }, { "beat": 4, "value": 0 } ] } },
                   "effects": [ { "type": "limiter", "ceilingDb": -12 } ] } ],
      "master": { "effects": [ { "type": "compressor", "thresholdDb": -30, "ratio": 4, "sidechain": "Kick" } ] },
      "tracks": [
        { "name": "Keys", "instrument": "synth.pluck", "sends": [ { "bus": "crush", "levelDb": 0 } ],
          "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 100 }, { "pitch": 64, "startBeat": 1.5, "durationBeat": 1, "velocity": 110 },
                     { "pitch": 67, "startBeat": 3, "durationBeat": 2, "velocity": 120 }, { "pitch": 72, "startBeat": 5.5, "durationBeat": 1, "velocity": 100 } ] },
        { "name": "Kick", "instrument": "synth.edm-drums", "mixer": { "gainDb": -6 },
          "notes": [ { "pitch": 36, "startBeat": 0, "durationBeat": 0.5, "velocity": 110 }, { "pitch": 36, "startBeat": 2, "durationBeat": 0.5, "velocity": 110 },
                     { "pitch": 36, "startBeat": 4, "durationBeat": 0.5, "velocity": 110 } ] } ] })");
    const auto reference = offline (c, "realtime_bus_latency.wav");
    RealtimePlayer player (c, playerOptions());
    const int latency = player.outputLatencySamples();
    CHECK (latency > 0);
    player.play();
    const auto raw = pull (player, (double) (reference.getNumSamples() + latency) / kRate);
    double worst = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < reference.getNumSamples(); ++i)
            worst = std::max (worst, (double) std::abs (raw.getSample (ch, i + latency) - reference.getSample (ch, i)));
    std::printf ("    bus latency %d samples, max difference %.2e\n", latency, worst);
    CHECK (rms (reference, 0.0, 3.0) > 0.01f);
    CHECK (worst < 1.0e-5);
}

TEST (track_output_goes_to_a_bus)
{
    // 두 트랙이 효과(-12 dB)를 가진 버스로 나간다: 버스 스템이 두 트랙의 합에 -12 dB, 마스터는 그 버스. 실시간과 오프라인이 같다
    const auto text = std::string (R"({ "tempo": 120, "render": { "tailSeconds": 0.5 },
      "buses": [ { "name": "Group", "effects": [ { "type": "gain", "gainDb": -12 } ] } ],
      "tracks": [
        { "name": "A", "instrument": "synth.sine", "output": "Group", "notes": [ { "pitch": 69, "startBeat": 0, "durationBeat": 2, "velocity": 100 } ] },
        { "name": "B", "instrument": "synth.sine", "output": "Group", "notes": [ { "pitch": 76, "startBeat": 1, "durationBeat": 2, "velocity": 100 } ] } ] })");
    const auto c = composition (text);
    CHECK_EQ (c.tracks[0].output, std::string ("Group"));
    const auto routed = offline (c, "output_routed.wav");
    // 같은 곡을 마스터로 보내면 12 dB 크다
    auto direct = c;
    for (auto& t : direct.tracks)
        t.output.clear();
    const auto plain = offline (direct, "output_direct.wav");
    const float ratio = rms (routed, 0.1, 1.4) / rms (plain, 0.1, 1.4);
    std::printf ("    routed / direct %.4f (expected %.4f)\n", ratio, std::pow (10.0, -12.0 / 20.0));
    CHECK_NEAR (ratio, std::pow (10.0, -12.0 / 20.0), 0.002);
    RealtimePlayer player (c, playerOptions());
    player.play();
    const auto live = pull (player, (double) routed.getNumSamples() / kRate);
    CHECK (maxDiff (live, routed) < 1.0e-5);
    // 없는 버스는 검사 오류
    auto bad = mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "A", "instrument": "synth.sine", "output": "Nope" } ] })");
    CHECK (! bad.composition.has_value());
}

namespace
{
// 모듈레이션 효과와 게이트: 트랙, 버스, 마스터에 (박에 맞춘 LFO 와 곡선 깊이), 템포가 바뀌는 곡
const char* modulationJson = R"({ "tempo": [ { "beat": 0, "bpm": 100, "ramp": true }, { "beat": 12, "bpm": 140 } ], "render": { "tailSeconds": 0.5 },
  "buses": [ { "name": "wide", "effects": [ { "type": "chorus", "rateBeats": 2, "depthMs": 5, "feedback": 0.2, "mix": 0.6,
                                              "automation": { "depthMs": [ { "beat": 2, "value": 1 }, { "beat": 10, "value": 8 } ] } },
                                            { "type": "phaser", "rateHz": 0.6, "stages": 6, "feedback": 0.5 } ] } ],
  "master": { "effects": [ { "type": "tremolo", "rateBeats": 0.5, "depth": 0.3, "phaseDeg": 120, "shape": "triangle" } ] },
  "tracks": [
    { "name": "Pad", "instrument": "synth.saw-pad", "synth": { "release": 0.05 },
      "effects": [ { "type": "flanger", "rateBeats": 4, "feedback": 0.7, "automation": { "mix": [ { "beat": 4, "value": 0 }, { "beat": 8, "value": 0.7 } ] } },
                   { "type": "gate", "thresholdDb": -30, "holdMs": 10, "releaseMs": 40 } ],
      "sends": [ { "bus": "wide", "levelDb": -6 } ],
      "notes": [ { "pitch": "A3", "startBeat": 1, "durationBeat": 2, "velocity": 90 }, { "pitch": "E4", "startBeat": 5, "durationBeat": 3, "velocity": 90 },
                 { "pitch": "C5", "startBeat": 8.5, "durationBeat": 3, "velocity": 80 } ] } ] })";
} // namespace

TEST (realtime_modulation_effects_match_the_offline_render)
{
    const auto c = composition (modulationJson);
    const auto reference = offline (c, "rt_modulation_reference.wav");
    RealtimePlayer player (c, playerOptions());
    player.play();
    const double seconds = (double) reference.getNumSamples() / kRate;
    const auto live = pull (player, seconds, 333);
    CHECK (rms (reference, 1.0, 3.0) > 0.01f);
    CHECK (maxDiff (reference, live) < 1.0e-5);

    // 소리가 없는 4.5박으로 옮겨 재생해도 그 뒤는 같다 (LFO 는 곡 위치의 위상이다)
    RealtimePlayer seeking (c, playerOptions());
    const int64_t from = TempoMap (c.tempo).sampleAt (4.5, kRate);
    CHECK (rms (reference, (double) from / kRate - 0.05, (double) from / kRate) < 1.0e-5f);
    seeking.seek (from);
    seeking.play();
    const auto after = pull (seeking, (double) (reference.getNumSamples() - from) / kRate, 512);
    double diff = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < std::min (after.getNumSamples(), reference.getNumSamples() - (int) from); ++i)
            diff = std::max (diff, (double) std::abs (after.getSample (ch, i) - reference.getSample (ch, i + (int) from)));
    CHECK (diff < 1.0e-5);
}

namespace
{
// VCA: 같은 값을 채널에 직접 준 곡과 샘플까지 같아야 한다. %VCA% 자리에 vcas 를, %A%, %B% 에 트랙과 버스의 추가 필드를 넣는다
std::string vcaSong (const std::string& vcas, const std::string& trackA, const std::string& bus)
{
    std::string s = R"({ "tempo": 120, "render": { "tailSeconds": 0.3 }, %VCA%
      "buses": [ { "name": "Room", "effects": [ { "type": "gain", "gainDb": -3 } ] %B% } ],
      "tracks": [
        { "name": "A", "instrument": "synth.saw-pad" %A%, "sends": [ { "bus": "Room", "levelDb": -6 } ],
          "notes": [ { "pitch": "A3", "startBeat": 0, "durationBeat": 6, "velocity": 90, "humanize": false } ] },
        { "name": "B", "instrument": "synth.pluck", "notes": [ { "pitch": "E5", "startBeat": 1, "durationBeat": 1, "velocity": 90, "humanize": false } ] } ] })";
    auto put = [&] (const std::string& key, const std::string& value) { s.replace (s.find (key), key.size(), value); };
    put ("%VCA%", vcas);
    put ("%A%", trackA);
    put ("%B%", bus);
    return s;
}
} // namespace

TEST (vca_adds_its_gain_lane_and_mute_to_its_channels)
{
    // 고정 게인: 트랙 -3 dB 에 VCA -6 dB 는 트랙 -9 dB 와 같다 (버스도 VCA 에)
    const auto withVca = composition (vcaSong (R"("vcas": [ { "name": "Group", "gainDb": -6 } ],)", R"(, "vca": "Group", "mixer": { "gainDb": -3 })", R"(, "vca": "Group")"));
    const auto direct = composition (vcaSong ("", R"(, "mixer": { "gainDb": -9 })", R"(, "mixer": { "gainDb": -6 })"));
    CHECK_EQ (withVca.tracks[0].mixer.gainDb, -3.0);   // 채널 자신의 값은 그대로
    CHECK_EQ (withVca.tracks[0].mixer.vcaDb, -6.0);
    const auto a = offline (withVca, "vca_fixed.wav"), b = offline (direct, "vca_fixed_direct.wav");
    CHECK (rms (a, 0.2, 2.0) > 0.001f);
    CHECK_EQ (maxDiff (a, b), 0.0);
    // 곡선: VCA 의 gainDb 곡선은 채널 곡선과 같다
    const std::string lane = R"([ { "beat": 0, "value": 0 }, { "beat": 4, "value": -18, "curve": "smooth" }, { "beat": 6, "value": -6 } ])";
    const auto laneVca = composition (vcaSong (R"("vcas": [ { "name": "Group", "automation": { "gainDb": )" + lane + " } } ],", R"(, "vca": "Group")", ""));
    const auto laneDirect = composition (vcaSong ("", R"(, "mixer": { "automation": { "gainDb": )" + lane + " } }", ""));
    const auto c = offline (laneVca, "vca_lane.wav"), d = offline (laneDirect, "vca_lane_direct.wav");
    CHECK (maxDiff (c, d) < 1.0e-6);
    // 실시간도 같다
    RealtimePlayer player (laneVca, playerOptions());
    player.play();
    CHECK (maxDiff (c, pull (player, (double) c.getNumSamples() / kRate)) < 1.0e-5);
    // 뮤트: 속한 트랙이 들리지 않는다 (트랙 자신의 뮤트와 같다)
    const auto muted = composition (vcaSong (R"("vcas": [ { "name": "Group", "mute": true } ],)", R"(, "vca": "Group")", ""));
    const auto mutedDirect = composition (vcaSong ("", R"(, "mixer": { "mute": true })", ""));
    CHECK_EQ (maxDiff (offline (muted, "vca_mute.wav"), offline (mutedDirect, "vca_mute_direct.wav")), 0.0);
    CHECK (! muted.tracks[0].mixer.mute && muted.tracks[0].mixer.muted());
}

TEST (realtime_apply_follows_a_vca_edit)
{
    // 재생 전에 VCA 를 바꾼 곡을 반영하면 새로 만든 재생기와 같다 (재생기를 다시 만들지 않는다)
    const auto before = composition (vcaSong (R"("vcas": [ { "name": "Group", "gainDb": 0 } ],)", R"(, "vca": "Group")", ""));
    const auto after = composition (vcaSong (R"("vcas": [ { "name": "Group", "gainDb": -12 } ],)", R"(, "vca": "Group")", ""));
    RealtimePlayer applied (before, playerOptions()), fresh (after, playerOptions());
    CHECK (applied.apply (after));
    applied.play();
    fresh.play();
    const double seconds = 2.0;
    CHECK_EQ (maxDiff (pull (applied, seconds), pull (fresh, seconds)), 0.0);
    applied.collectRetired();
    // 곡선이 생기면 곡선을 바꿔 끼운다
    const auto laned = composition (vcaSong (R"("vcas": [ { "name": "Group", "automation": { "gainDb": [ { "beat": 0, "value": -20 } ] } } ],)", R"(, "vca": "Group")", ""));
    RealtimePlayer swapped (before, playerOptions()), freshLane (laned, playerOptions());
    CHECK (swapped.apply (laned));
    swapped.play();
    freshLane.play();
    CHECK (maxDiff (pull (swapped, seconds), pull (freshLane, seconds)) < 1.0e-6);
    swapped.collectRetired();
}

TEST (vcas_are_validated)
{
    auto errors = [] (const std::string& json)
    {
        auto r = parseComposition (juce::String::fromUTF8 (json.c_str()), juce::File());
        return r.diagnostics.errors;
    };
    auto has = [] (const std::vector<Diagnostic>& e, const std::string& path) { return std::any_of (e.begin(), e.end(), [&] (const Diagnostic& d) { return d.path == path; }); };
    CHECK (has (errors (vcaSong (R"("vcas": [ { "name": "Group" } ],)", R"(, "vca": "Gruop")", "")), "$.tracks[0].vca"));
    CHECK (has (errors (vcaSong (R"("vcas": [ { "name": "G" }, { "name": "G" } ],)", "", "")), "$.vcas[1].name"));
    CHECK (has (errors (vcaSong (R"("vcas": [ { "name": "G", "automation": { "pan": [ { "beat": 0, "value": 0 } ] } } ],)", "", "")), "$.vcas[0].automation.pan"));
    CHECK (errors (vcaSong (R"("vcas": [ { "name": "G", "gainDb": -4, "mute": false } ],)", R"(, "vca": "G")", R"(, "vca": "G")")).empty());
}
