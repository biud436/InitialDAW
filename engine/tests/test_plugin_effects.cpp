#include "TestFramework.h"
#include "TestUtil.h"

#include "io/AudioFile.h"
#include "plugins/PluginEffect.h"
#include "plugins/PluginHost.h"
#include "realtime/RealtimePlayer.h"
#include "rendering/OfflineRenderer.h"

#include <cmath>

using namespace mr;

#ifdef MUSIC_RENDER_TEST_EFFECT_VST3

namespace
{

constexpr double kRate = 48000.0;

std::string effectPath() { return juce::File (MUSIC_RENDER_TEST_EFFECT_VST3).getFullPathName().toStdString(); }

// 4초 동안 크기 0.5 의 440 Hz 사인파 (오디오 클립)
juce::File toneFile()
{
    const auto file = mrtest::outputDir().getChildFile ("plugin_effect_tone.wav");
    const int n = (int) (4.0 * kRate);
    juce::AudioBuffer<float> b (1, n);
    for (int i = 0; i < n; ++i)
        b.setSample (0, i, (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / kRate)));
    writeWav (file, b, n, kRate, 32);
    return file;
}

std::string plugin (const std::string& extra = {})
{
    return R"({ "type": "plugin", "format": "vst3", "plugin": ")" + effectPath() + "\"" + extra + " }";
}

std::string song (const std::string& trackEffects, const std::string& busEffects = "[]", const std::string& masterEffects = "[]")
{
    return R"({ "tempo": 120, "render": { "tailSeconds": 0.2 },
      "buses": [ { "name": "Echo", "mixer": { "gainDb": -6 }, "effects": )" + busEffects + R"( } ],
      "master": { "effects": )" + masterEffects + R"( },
      "tracks": [ { "name": "Tone", "instrument": "audio", "effects": )" + trackEffects + R"(, "sends": [ { "bus": "Echo", "levelDb": -12 } ],
                    "clips": [ { "file": ")" + toneFile().getFullPathName().toStdString() + R"(", "startBeat": 0 } ] } ] })";
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

float rms (const juce::AudioBuffer<float>& b, double fromSec, double toSec)
{
    return b.getRMSLevel (0, (int) (fromSec * kRate), (int) ((toSec - fromSec) * kRate));
}

double maxDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    double d = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < std::min (a.getNumSamples(), b.getNumSamples()); ++i)
            d = std::max (d, (double) std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
    return d;
}

std::string stateWith (const std::map<std::string, float>& values)
{
    juce::PluginDescription chosen;
    std::string error;
    auto instance = PluginHost::get().load ("vst3", effectPath(), "", kRate, 512, chosen, error);
    REQUIRE (instance != nullptr);
    for (auto* p : instance->getParameters())
        if (auto it = values.find (p->getName (64).toStdString()); it != values.end())
            p->setValueNotifyingHost (it->second);
    return pluginStateBase64 (*instance);
}

} // namespace

TEST (plugin_effect_processes_tracks_buses_and_master)
{
    const float full = 0.5f / std::sqrt (2.0f);
    // 기본 Gain -6 dB (버스는 뮤트로 빼고 잰다)
    auto text = song ("[ " + plugin() + " ]");
    const std::string fader = R"("mixer": { "gainDb": -6 })";
    text.replace (text.find (fader), fader.size(), R"("mixer": { "mute": true })");
    const auto c = composition (text);
    const auto out = offline (c, "plugin_effect_track.wav");
    CHECK_NEAR (rms (out, 0.5, 3.5), full * 0.501187f, 0.003);
    // 상태로 소리를 정한다 (Gain 0 dB 는 정규화 값 60/72)
    const auto state = stateWith ({ { "Gain", 60.0f / 72.0f } });
    const auto loud = offline (composition (song ("[ " + plugin (R"(, "state": ")" + state + "\"") + " ]")), "plugin_effect_state.wav");
    CHECK (rms (loud, 0.5, 3.5) > rms (out, 0.5, 3.5) * 1.5f);
    // 마스터에도
    const auto master = offline (composition (song ("[]", "[]", "[ " + plugin() + " ]")), "plugin_effect_master.wav");
    const auto plain = offline (composition (song ("[]")), "plugin_effect_plain.wav");
    CHECK_NEAR (rms (master, 0.5, 3.5) / rms (plain, 0.5, 3.5), 0.501187f, 0.005);
    // 검사: 형식, 빈 플러그인, 모르는 키
    auto bad = mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "A", "instrument": "synth.piano", "notes": [],
                                   "effects": [ { "type": "plugin", "format": "lv2", "plugin": "" , "speed": 2 } ] } ] })");
    CHECK (mrtest::hasError (bad.diagnostics, "$.tracks[0].effects[0].format"));
    CHECK (mrtest::hasError (bad.diagnostics, "$.tracks[0].effects[0].plugin"));
    CHECK (mrtest::hasWarning (bad.diagnostics, "$.tracks[0].effects[0].speed"));
}

TEST (plugin_effect_latency_is_compensated_and_the_playhead_follows_the_song)
{
    // Latency 300 샘플: 트랙 정렬이 되돌려 플러그인 없는 트랙과 같은 자리에 난다
    const auto delayed = stateWith ({ { "Gain", 60.0f / 72.0f }, { "Latency", 300.0f / 4096.0f } });
    const auto withLatency = offline (composition (song ("[ " + plugin (R"(, "state": ")" + delayed + "\"") + " ]")), "plugin_effect_latency.wav");
    const auto plain = offline (composition (song ("[]")), "plugin_effect_plain2.wav");
    CHECK (maxDiff (withLatency, plain) < 1.0e-4);
    RealtimePlayer player (composition (song ("[ " + plugin (R"(, "state": ")" + delayed + "\"") + " ]")), playerOptions());
    CHECK_EQ (player.trackInfo (0).latencySamples, 300);
    // 박의 뒤 반을 끈다: 120 BPM 에서 0.25~0.5초는 조용하고 0~0.25초는 소리가 난다
    const auto gated = stateWith ({ { "Gain", 60.0f / 72.0f }, { "Beat Gate", 1.0f } });
    const auto out = offline (composition (song ("[ " + plugin (R"(, "state": ")" + gated + "\"") + " ]")), "plugin_effect_gate.wav");
    CHECK (rms (out, 1.27, 1.48) < 0.01f);
    CHECK (rms (out, 1.02, 1.23) > 0.2f);
}

TEST (plugin_effect_realtime_matches_offline_and_settings_apply_in_place)
{
    const auto gated = stateWith ({ { "Gain", 54.0f / 72.0f }, { "Beat Gate", 1.0f } });
    const auto c = composition (song ("[ " + plugin (R"(, "state": ")" + gated + "\"") + " ]", "[ " + plugin() + " ]", "[ " + plugin() + " ]"));
    const auto reference = offline (c, "plugin_effect_rt_reference.wav");
    RealtimePlayer player (c, playerOptions());
    CHECK (player.effectPlugin ("track:0", 0) != nullptr && player.effectPlugin ("bus:0", 0) != nullptr
           && player.effectPlugin ("master", 0) != nullptr && player.effectPlugin ("track:0", 1) == nullptr);
    player.play();
    const auto live = pull (player, (double) reference.getNumSamples() / kRate);
    CHECK (rms (reference, 1.02, 1.23) > 0.05f);
    CHECK (maxDiff (live, reference) < 1.0e-5);

    // 상태만 바뀐 편집: 같은 플러그인에 넣는다 (사슬을 바꾸지 않는다)
    const auto quiet = stateWith ({ { "Gain", 30.0f / 72.0f } });
    const auto edited = composition (song ("[ " + plugin (R"(, "state": ")" + quiet + "\"") + " ]", "[ " + plugin() + " ]", "[ " + plugin() + " ]"));
    RealtimePlayer applied (c, playerOptions());
    const auto before = applied.effectPlugin ("track:0", 0);
    CHECK (applied.apply (edited));
    CHECK (applied.effectPlugin ("track:0", 0) == before);
    CHECK (pluginStateBase64 (*before) == quiet);
    // 효과를 더하면 사슬을 바꾸고 새 플러그인이 된다
    const auto grown = composition (song ("[ " + plugin() + R"(, { "type": "gain", "gainDb": -3 } ])", "[ " + plugin() + " ]", "[ " + plugin() + " ]"));
    CHECK (applied.apply (grown));
    CHECK (applied.effectPlugin ("track:0", 0) != before && applied.effectPlugin ("track:0", 0) != nullptr);
}

#endif
