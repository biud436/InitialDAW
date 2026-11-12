#include "TestFramework.h"
#include "TestUtil.h"

#include "effects/Effects.h"
#include "effects/Presets.h"

using namespace mr;

TEST (factory_presets_are_valid_effects)
{
    int count = 0;
    for (const auto& type : effectTypes())
        for (const auto& p : effectPresets (type))
        {
            ++count;
            EffectSpec spec;
            spec.type = type;
            spec.params = p.effect;
            spec.path = "$.preset." + p.id;
            Diagnostics d;
            validateEffectSpec (spec, d);
            if (! CHECK (d.ok() && d.warnings.empty()))
                std::fprintf (stderr, "    preset %s: %s\n", p.id.c_str(), d.errors.empty() ? "warning" : d.errors[0].message.c_str());
            // 만들어 소리를 지날 수 있다
            auto chain = createEffectChain ({ spec }, mrtest::sourceDir(), 48000.0, 512);
            juce::AudioBuffer<float> b (2, 512);
            b.clear();
            chain.process (b, 512, 0);
        }
    CHECK (count >= 25);
    CHECK (effectPresets ("nonsense").empty());
}

TEST (presets_keep_routing_bypass_and_usable_curves)
{
    const auto effect = juce::JSON::parse (R"({ "type": "compressor", "thresholdDb": -30, "ratio": 10, "sidechain": "Kick", "bypass": true, "comment": "pump" })");
    const auto& presets = effectPresets ("compressor");
    REQUIRE (! presets.empty());
    const auto applied = withPreset (effect, presets.front().effect);
    CHECK_EQ ((double) applied.getProperty ("thresholdDb", 0.0), (double) presets.front().effect.getProperty ("thresholdDb", 0.0));
    CHECK (applied.getProperty ("sidechain", "").toString() == "Kick" && (bool) applied.getProperty ("bypass", false)
           && applied.getProperty ("comment", "").toString() == "pump");
    // 딜레이: 박 시간 프리셋이 timeMs 를 지우고, mix 곡선은 남는다
    const auto delay = juce::JSON::parse (R"({ "type": "delay", "timeMs": 300, "automation": { "mix": [ { "beat": 0, "value": 0.2 } ] } })");
    juce::var dotted;
    for (const auto& p : effectPresets ("delay"))
        if (p.id == "dotted-eighth")
            dotted = p.effect;
    const auto d = withPreset (delay, dotted);
    CHECK (! d.hasProperty ("timeMs") && (double) d.getProperty ("timeBeats", 0.0) == 0.75 && d.getProperty ("automation", juce::var()).hasProperty ("mix"));
}
