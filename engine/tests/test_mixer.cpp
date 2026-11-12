#include "TestFramework.h"
#include "TestUtil.h"

#include "io/AudioFile.h"
#include "mixer/Mixer.h"
#include "rendering/OfflineRenderer.h"

using namespace mr;

namespace
{

Composition twoTones (const std::string& extraTrackA = {}, const std::string& extraTrackB = {}, const std::string& extraRoot = {})
{
    const std::string json = R"({ "tempo": 120, "render": { "tailSeconds": 0.25 }, )" + extraRoot + R"(
      "tracks": [
        { "name": "A", "instrument": "synth.sine" )" + extraTrackA + R"(,
          "notes": [ { "pitch": "A4", "startBeat": 0, "durationBeat": 2, "velocity": 127 } ] },
        { "name": "B", "instrument": "synth.sine" )" + extraTrackB + R"(,
          "notes": [ { "pitch": "E5", "startBeat": 0, "durationBeat": 2, "velocity": 127 } ] } ] })";
    auto r = mrtest::parse (json);
    if (! r.composition)
        for (const auto& e : r.diagnostics.errors)
            std::fprintf (stderr, "    parse error %s: %s\n", e.path.c_str(), e.message.c_str());
    return *r.composition;
}

AudioData render (const Composition& c, const std::string& name, std::optional<juce::File> stems = std::nullopt)
{
    RenderOptions o;
    o.useDefaultRegistry = false;
    o.bitDepth = 32;
    o.output = mrtest::outputDir().getChildFile (name);
    o.stemsDir = stems;
    renderComposition (c, o);
    return readAudioFile (o.output);
}

float level (const AudioData& d, int channel)
{
    return d.buffer.getMagnitude (channel, 12000, 24000);
}

} // namespace

TEST (balance_and_fader_gains)
{
    auto g = balanceGains (0.0);
    CHECK_NEAR (g.left, 1.0, 1e-7);
    CHECK_NEAR (g.right, 1.0, 1e-7);
    g = balanceGains (1.0);
    CHECK_NEAR (g.left, 0.0, 1e-7);
    CHECK_NEAR (g.right, 1.0, 1e-7);
    g = balanceGains (-0.5);
    CHECK_NEAR (g.left, 1.0, 1e-7);
    CHECK_NEAR (g.right, std::sqrt (0.5), 1e-6);
    MixerSettings m;
    m.gainDb = -6.0206;
    g = faderGains (m);
    CHECK_NEAR (g.left, 0.5, 1e-4);
    m.gainDb = -120.0;
    CHECK_NEAR (faderGains (m).left, 0.0, 1e-12);
}

TEST (track_gain_pan_mute_and_solo)
{
    const auto base = render (twoTones ({}, R"(, "mixer": { "mute": true })"), "mix_base.wav");
    const auto quiet = render (twoTones (R"(, "mixer": { "gainDb": -6.0206 })", R"(, "mixer": { "mute": true })"), "mix_gain.wav");
    CHECK_NEAR (level (quiet, 0) / level (base, 0), 0.5, 0.002);

    const auto left = render (twoTones (R"(, "mixer": { "pan": -1 })", R"(, "mixer": { "mute": true })"), "mix_pan.wav");
    CHECK (level (left, 1) < 1.0e-6f);
    CHECK_NEAR (level (left, 0), level (base, 0), 1e-4);

    // B 만 솔로: A 가 사라지고 B 만 남는다
    const auto solo = render (twoTones ({}, R"(, "mixer": { "solo": true })"), "mix_solo.wav");
    const auto onlyB = render (twoTones (R"(, "mixer": { "mute": true })", {}), "mix_onlyb.wav");
    double maxDiff = 0.0;
    for (int i = 0; i < solo.buffer.getNumSamples(); ++i)
        maxDiff = std::max (maxDiff, (double) std::abs (solo.buffer.getSample (0, i) - onlyB.buffer.getSample (0, i)));
    CHECK (maxDiff < 1.0e-7);
}

TEST (sends_feed_buses_pre_and_post_fader)
{
    // A: 게인 -6 dB, 포스트 센드 0 dB → 버스 = A 의 포스트 페이더. 마스터 = 2 × 포스트
    const auto post = render (twoTones (R"(, "mixer": { "gainDb": -6.0206 }, "sends": [ { "bus": "fx" } ])", R"(, "mixer": { "mute": true })",
                                        R"("buses": [ { "name": "fx" } ],)"), "mix_post.wav");
    const auto plain = render (twoTones ({}, R"(, "mixer": { "mute": true })"), "mix_plain.wav");
    CHECK_NEAR (level (post, 0) / level (plain, 0), 1.0, 0.003);
    // 프리 센드: 버스는 트랙 게인과 상관없다. 마스터 = 0.5 + 1.0 = 1.5 × 원래
    const auto pre = render (twoTones (R"(, "mixer": { "gainDb": -6.0206 }, "sends": [ { "bus": "fx", "preFader": true } ])", R"(, "mixer": { "mute": true })",
                                       R"("buses": [ { "name": "fx" } ],)"), "mix_pre.wav");
    CHECK_NEAR (level (pre, 0) / level (plain, 0), 1.5, 0.004);
    // 버스 게인과 마스터 게인
    const auto busGain = render (twoTones (R"(, "sends": [ { "bus": "fx", "levelDb": -6.0206 } ])", R"(, "mixer": { "mute": true })",
                                           R"("buses": [ { "name": "fx", "mixer": { "gainDb": -6.0206 } } ], "master": { "mixer": { "gainDb": -6.0206 } },)"), "mix_busgain.wav");
    CHECK_NEAR (level (busGain, 0) / level (plain, 0), 0.5 * (1.0 + 0.25), 0.004);
}

TEST (stems_sum_to_the_master)
{
    const auto dir = mrtest::outputDir().getChildFile ("stems");
    dir.deleteRecursively();
    const auto master = render (twoTones (R"(, "mixer": { "gainDb": -3, "pan": 0.3 }, "sends": [ { "bus": "hall", "levelDb": -9 } ])",
                                          R"(, "mixer": { "pan": -0.4 }, "sends": [ { "bus": "hall", "levelDb": -12, "preFader": true } ])",
                                          R"("buses": [ { "name": "hall", "mixer": { "gainDb": -2 } } ],)"),
                                "mix_stems.wav", dir);
    const auto a = readAudioFile (dir.getChildFile ("A.wav"));
    const auto b = readAudioFile (dir.getChildFile ("B.wav"));
    const auto hall = readAudioFile (dir.getChildFile ("bus-hall.wav"));
    const auto m = readAudioFile (dir.getChildFile ("master.wav"));
    CHECK_EQ (a.samples, master.samples);
    CHECK_EQ (hall.samples, master.samples);
    CHECK_EQ (m.samples, master.samples);
    double maxDiff = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < (int) master.samples; ++i)
        {
            const double sum = a.buffer.getSample (ch, i) + b.buffer.getSample (ch, i) + hall.buffer.getSample (ch, i);
            maxDiff = std::max (maxDiff, std::abs (sum - master.buffer.getSample (ch, i)));
            maxDiff = std::max (maxDiff, (double) std::abs (m.buffer.getSample (ch, i) - master.buffer.getSample (ch, i)));
        }
    CHECK (maxDiff < 1.0e-6);
    CHECK_EQ (stemFileName ("Violins I/II"), std::string ("Violins_I_II.wav"));
}

TEST (mixer_validation_errors)
{
    auto r = mrtest::parse (R"({ "tempo": 100,
      "buses": [ { "name": "hall" }, { "name": "hall" }, { "name": "master" }, { "name": "verb", "effects": [ { "type": "reverb" } ] } ],
      "tracks": [ { "name": "A", "instrument": "synth.sine", "sends": [ { "bus": "hal" }, { "bus": "nowhere" } ],
                    "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 64 } ] } ] })");
    CHECK (! r.composition.has_value());
    CHECK (mrtest::hasError (r.diagnostics, "$.buses[1].name", "duplicate"));
    CHECK (mrtest::hasError (r.diagnostics, "$.buses[2].name", "reserved"));
    CHECK (! mrtest::hasError (r.diagnostics, "$.buses[3].effects"));   // 버스 효과는 5단계에서 구현되었다
    bool suggested = false;
    for (const auto& e : r.diagnostics.errors)
        suggested = suggested || (e.path == "$.tracks[0].sends[0].bus" && e.hint.find ("hall") != std::string::npos);
    CHECK (suggested);
    CHECK (mrtest::hasError (r.diagnostics, "$.tracks[0].sends[1].bus", "unknown bus"));
}

TEST (muted_bus_is_silent_in_the_render)
{
    const std::string buses = R"("buses": [ { "name": "fx", "mixer": { "mute": true } } ],)";
    const auto c = twoTones (R"(, "mixer": { "gainDb": -120 }, "sends": [ { "bus": "fx", "levelDb": 0, "preFader": true } ])", {}, buses);
    const auto muted = render (c, "bus_muted.wav");
    const auto open = render (twoTones (R"(, "mixer": { "gainDb": -120 }, "sends": [ { "bus": "fx", "levelDb": 0, "preFader": true } ])", {},
                                        R"("buses": [ { "name": "fx" } ],)"), "bus_open.wav");
    // A 는 페이더를 내리고 프리 페이더 센드로만 버스에 간다: 버스를 끄면 B 만 남는다
    const auto onlyB = render (twoTones (R"(, "mixer": { "mute": true })"), "bus_only_b.wav");
    double maxDiff = 0.0;
    for (int i = 0; i < muted.buffer.getNumSamples(); ++i)
        maxDiff = std::max (maxDiff, (double) std::abs (muted.buffer.getSample (0, i) - onlyB.buffer.getSample (0, i)));
    CHECK (maxDiff < 1.0e-6);
    CHECK (level (open, 0) > level (muted, 0) * 1.3f);
}
