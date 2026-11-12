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

// 4초 동안 크기 0.5 의 440 Hz 사인파 (오디오 클립으로 튼다). 버스 효과는 지연이 없는 딜레이 (리버브의 지연은 오프라인만 되돌린다)
juce::File toneFile()
{
    const auto file = mrtest::outputDir().getChildFile ("fader_tone.wav");
    const int n = (int) (4.0 * kRate);
    juce::AudioBuffer<float> b (1, n);
    for (int i = 0; i < n; ++i)
        b.setSample (0, i, (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / kRate)));
    writeWav (file, b, n, kRate, 32);
    return file;
}

std::string song (const std::string& trackMixer, const std::string& busMixer = R"({ "gainDb": -6 })", const std::string& masterMixer = "{}")
{
    const auto file = toneFile().getFullPathName().toStdString();
    return R"({ "tempo": 120, "render": { "tailSeconds": 0.2 },
      "buses": [ { "name": "Verb", "mixer": )" + busMixer + R"(, "effects": [ { "type": "delay", "timeMs": 120, "feedback": 0.3, "mix": 1 } ] } ],
      "master": { "mixer": )" + masterMixer + R"( },
      "tracks": [ { "name": "Tone", "instrument": "audio", "mixer": )" + trackMixer + R"(, "sends": [ { "bus": "Verb", "levelDb": -12 } ],
                    "clips": [ { "file": ")" + file + R"(", "startBeat": 0 } ] } ] })";
}

Composition composition (const std::string& text)
{
    auto r = mrtest::parse (text);
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

juce::AudioBuffer<float> pull (RealtimePlayer& p, double seconds)
{
    const int total = (int) std::llround (seconds * kRate);
    juce::AudioBuffer<float> out (2, total);
    out.clear();
    for (int done = 0; done < total;)
    {
        const int n = std::min (300, total - done);
        float* ptrs[2] = { out.getWritePointer (0, done), out.getWritePointer (1, done) };
        p.process (ptrs, 2, n);
        done += n;
    }
    return out;
}

float rms (const juce::AudioBuffer<float>& b, int ch, double fromSec, double toSec)
{
    return b.getRMSLevel (ch, (int) (fromSec * kRate), (int) ((toSec - fromSec) * kRate));
}

double maxDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    double d = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < std::min (a.getNumSamples(), b.getNumSamples()); ++i)
            d = std::max (d, (double) std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
    return d;
}

} // namespace

TEST (mixer_automation_follows_the_fader_and_pan_curves)
{
    // 버스는 뮤트 (센드의 딜레이를 빼고 잰다). 페이더는 0~1초 -24 dB, 1~2초 -24 에서 0 으로, 2초 뒤 0. 밸런스는 3초에 왼쪽 끝
    const auto c = composition (song (R"({ "automation": { "gainDb": [ { "beat": 0, "value": -24, "curve": "step" }, { "beat": 2, "value": -24 }, { "beat": 4, "value": 0 } ],
                                                          "pan": [ { "beat": 0, "value": 0, "curve": "step" }, { "beat": 6, "value": -1 } ] } })",
                                      R"({ "mute": true })"));
    REQUIRE (! c.tracks[0].mixer.gainLane.empty() && ! c.tracks[0].mixer.panLane.empty());
    const auto out = offline (c, "fader_curve.wav");
    const float full = 0.5f / std::sqrt (2.0f);
    CHECK_NEAR (rms (out, 0, 0.1, 0.9), full * 0.0630957f, 0.002);   // -24 dB
    CHECK (rms (out, 0, 1.2, 1.4) < rms (out, 0, 1.6, 1.8));         // 오른다
    CHECK_NEAR (rms (out, 0, 2.1, 2.9), full, 0.005);
    CHECK_NEAR (rms (out, 1, 3.1, 3.9), 0.0, 0.002);                 // 밸런스가 왼쪽 끝
    CHECK_NEAR (rms (out, 0, 3.1, 3.9), full, 0.005);

    auto bad = mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "A", "instrument": "synth.piano", "notes": [],
                                   "mixer": { "automation": { "gainDb": [ { "beat": 0, "value": 40 } ], "mute": [] } } } ] })");
    CHECK (mrtest::hasError (bad.diagnostics, "$.tracks[0].mixer.automation.gainDb[0].value"));
    CHECK (mrtest::hasError (bad.diagnostics, "$.tracks[0].mixer.automation.mute"));
}

TEST (mixer_automation_realtime_matches_offline_on_tracks_buses_and_master)
{
    const auto c = composition (song (R"({ "automation": { "gainDb": [ { "beat": 0, "value": -12 }, { "beat": 8, "value": 0, "curve": "smooth" } ],
                                                          "pan": [ { "beat": 2, "value": -0.5 }, { "beat": 6, "value": 0.5 } ] } })",
                                      R"({ "automation": { "gainDb": [ { "beat": 0, "value": -30 }, { "beat": 6, "value": -3 } ] } })",
                                      R"({ "automation": { "gainDb": [ { "beat": 0, "value": 0 }, { "beat": 7, "value": -18, "curve": "exponential" } ], "pan": [ { "beat": 4, "value": 0.2 } ] } })"));
    const auto reference = offline (c, "fader_rt_reference.wav");
    RealtimePlayer player (c, playerOptions());
    player.play();
    const auto live = pull (player, (double) reference.getNumSamples() / kRate);
    CHECK (rms (reference, 0, 0.5, 3.5) > 0.05f);
    CHECK (maxDiff (live, reference) < 1.0e-5);

    // 곡선을 바꾼 편집은 재생기를 다시 만들지 않고 바꿔 끼운다: 새로 만든 재생기와 같다
    const auto edited = composition (song (R"({ "automation": { "gainDb": [ { "beat": 0, "value": 0 }, { "beat": 8, "value": -20 } ] } })",
                                           R"({ "gainDb": -6 })", R"({ "automation": { "pan": [ { "beat": 0, "value": -0.3 } ] } })"));
    RealtimePlayer applied (c, playerOptions());
    CHECK (applied.apply (edited));
    RealtimePlayer fresh (edited, playerOptions());
    applied.play();
    fresh.play();
    const auto a = pull (applied, 3.0), b = pull (fresh, 3.0);
    CHECK (rms (a, 0, 0.5, 2.5) > 0.05f);
    CHECK_EQ (maxDiff (a, b), 0.0);
}
