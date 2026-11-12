#include "effects/Presets.h"

#include "effects/Effects.h"

#include <map>

namespace mr
{

const std::vector<EffectPreset>& effectPresets (const std::string& type)
{
    static const std::map<std::string, std::vector<EffectPreset>> table = []
    {
        std::map<std::string, std::vector<EffectPreset>> t;
        auto add = [&t] (const char* id, const char* json)
        {
            const auto v = juce::JSON::parse (juce::String (json));
            t[v.getProperty ("type", "").toString().toStdString()].push_back ({ id, v });
        };
        add ("vocal-polish", R"({ "type": "eq", "lowCutHz": 90, "lowCutSlope": 24, "bands": [ { "type": "peak", "freqHz": 300, "gainDb": -2.5, "q": 1 },
                                  { "type": "peak", "freqHz": 3200, "gainDb": 1.5, "q": 0.8 }, { "type": "highShelf", "freqHz": 10000, "gainDb": 2 } ] })");
        add ("low-cleanup", R"({ "type": "eq", "lowCutHz": 120, "lowCutSlope": 24, "bands": [ { "type": "peak", "freqHz": 350, "gainDb": -2, "q": 1.2 } ] })");
        add ("bright", R"({ "type": "eq", "bands": [ { "type": "highShelf", "freqHz": 8000, "gainDb": 3 }, { "type": "peak", "freqHz": 250, "gainDb": -1.5, "q": 0.9 } ] })");
        add ("telephone", R"({ "type": "eq", "lowCutHz": 400, "lowCutSlope": 24, "highCutHz": 3200, "highCutSlope": 24,
                               "bands": [ { "type": "peak", "freqHz": 1500, "gainDb": 4, "q": 1.2 } ] })");
        add ("vocal-level", R"({ "type": "compressor", "thresholdDb": -20, "ratio": 3, "attackMs": 5, "releaseMs": 100, "kneeDb": 6, "makeupDb": 3 })");
        add ("drum-glue", R"({ "type": "compressor", "thresholdDb": -18, "ratio": 4, "attackMs": 10, "releaseMs": 80, "kneeDb": 4, "makeupDb": 2, "mix": 0.6 })");
        add ("bass-firm", R"({ "type": "compressor", "thresholdDb": -24, "ratio": 5, "attackMs": 15, "releaseMs": 120, "kneeDb": 6, "makeupDb": 4 })");
        add ("gentle-bus", R"({ "type": "compressor", "thresholdDb": -12, "ratio": 2, "attackMs": 30, "releaseMs": 300, "kneeDb": 10, "makeupDb": 1 })");
        add ("small-room", R"({ "type": "reverb", "mode": "algorithmic", "roomSize": 0.35, "damping": 0.6, "width": 0.8, "mix": 0.2 })");
        add ("plate", R"({ "type": "reverb", "decaySeconds": 1.6, "preDelayMs": 10, "damping": 0.3, "lowCutHz": 150, "highCutHz": 12000, "mix": 0.25 })");
        add ("large-hall", R"({ "type": "reverb", "decaySeconds": 3.8, "preDelayMs": 35, "damping": 0.5, "lowCutHz": 120, "highCutHz": 8000, "mix": 0.3 })");
        add ("send-hall", R"({ "type": "reverb", "decaySeconds": 2.2, "preDelayMs": 20, "damping": 0.5, "mix": 1 })");
        add ("slapback", R"({ "type": "delay", "timeMs": 110, "feedback": 0.1, "mix": 0.25, "lowCutHz": 200, "highCutHz": 6000 })");
        add ("dotted-eighth", R"({ "type": "delay", "timeBeats": 0.75, "feedback": 0.35, "mix": 0.25, "lowCutHz": 250, "highCutHz": 5000 })");
        add ("ping-pong-eighth", R"({ "type": "delay", "timeBeats": 0.5, "feedback": 0.4, "mix": 0.3, "pingPong": 1, "lowCutHz": 200, "highCutHz": 6000 })");
        add ("warm-saturation", R"({ "type": "distortion", "driveDb": 6, "toneHz": 6000, "mix": 0.6 })");
        add ("hard-drive", R"({ "type": "distortion", "driveDb": 24, "toneHz": 4000, "mix": 1, "outputDb": -8 })");
        add ("mono", R"({ "type": "stereo", "width": 0 })");
        add ("wide", R"({ "type": "stereo", "width": 1.4 })");
        add ("master-ceiling", R"({ "type": "limiter", "ceilingDb": -1, "releaseMs": 60, "lookaheadMs": 5, "truePeak": 1 })");
        add ("lo-fi", R"({ "type": "filter", "mode": "lowpass", "cutoffHz": 3000, "resonance": 0.2, "slope": 12 })");
        add ("rumble-cut", R"({ "type": "filter", "mode": "highpass", "cutoffHz": 40, "slope": 24 })");
        add ("quarter-pump", R"({ "type": "ducker", "rateBeats": 1, "depthDb": -12, "attackMs": 1, "holdMs": 30, "releaseBeats": 0.5 })");
        add ("eighth-pump", R"({ "type": "ducker", "rateBeats": 0.5, "depthDb": -9, "attackMs": 1, "holdMs": 15, "releaseBeats": 0.3 })");
        add ("wide-chorus", R"({ "type": "chorus", "rateHz": 0.6, "depthMs": 4, "delayMs": 14, "mix": 0.5, "phaseDeg": 90 })");
        add ("doubling", R"({ "type": "chorus", "rateHz": 0.3, "depthMs": 1.5, "delayMs": 18, "mix": 0.35, "phaseDeg": 120 })");
        add ("jet", R"({ "type": "flanger", "rateHz": 0.12, "depthMs": 3, "delayMs": 1, "feedback": 0.75, "mix": 0.5 })");
        add ("slow-phaser", R"({ "type": "phaser", "rateHz": 0.2, "stages": 6, "feedback": 0.4, "mix": 0.5 })");
        add ("sixteenth-tremolo", R"({ "type": "tremolo", "rateBeats": 0.25, "depth": 0.6, "shape": "square" })");
        add ("slow-autopan", R"({ "type": "tremolo", "rateBeats": 4, "depth": 0.8, "phaseDeg": 180 })");
        add ("drum-gate", R"({ "type": "gate", "thresholdDb": -30, "rangeDb": -60, "attackMs": 0.2, "holdMs": 30, "releaseMs": 80 })");
        add ("master-multiband", R"({ "type": "multiband", "lowHz": 120, "highHz": 4000, "lowThresholdDb": -24, "lowRatio": 2, "midThresholdDb": -20,
                                     "midRatio": 1.5, "highThresholdDb": -26, "highRatio": 2, "attackMs": 15, "releaseMs": 200 })");
        add ("bass-even", R"({ "type": "multiband", "lowHz": 150, "highHz": 2500, "lowThresholdDb": -30, "lowRatio": 4, "midRatio": 1,
                              "highRatio": 1, "attackMs": 20, "releaseMs": 250 })");
        add ("punchy-drums", R"({ "type": "transient", "attackDb": 6, "sustainDb": -2 })");
        add ("tight-drums", R"({ "type": "transient", "attackDb": 2, "sustainDb": -8 })");
        add ("long-sustain", R"({ "type": "transient", "attackDb": -2, "sustainDb": 6 })");
        add ("vocal-deesser", R"({ "type": "deesser", "frequencyHz": 6500, "thresholdDb": -32, "rangeDb": 6 })");
        add ("bright-deesser", R"({ "type": "deesser", "frequencyHz": 9000, "thresholdDb": -36, "rangeDb": 4 })");
        return t;
    }();
    static const std::vector<EffectPreset> none;
    const auto it = table.find (type);
    return it != table.end() ? it->second : none;
}

juce::var withPreset (const juce::var& effect, const juce::var& preset)
{
    auto result = preset.clone();
    auto* out = result.getDynamicObject();
    const auto* in = effect.getDynamicObject();
    if (out == nullptr || in == nullptr)
        return effect;
    const auto type = out->getProperty ("type").toString().toStdString();
    for (const char* key : { "bypass", "comment", "sidechain" })
        if (in->hasProperty (key) && ! (std::string (key) == "sidechain" && type != "compressor"))
            out->setProperty (key, in->getProperty (key));
    if (const auto* lanes = in->getProperty ("automation").getDynamicObject())
    {
        auto* kept = new juce::DynamicObject();
        for (const auto& p : lanes->getProperties())
            if (const auto* info = findEffectParameter (type, p.name.toString().toStdString()); info != nullptr && info->automatable)
                kept->setProperty (p.name, p.value);
        if (! kept->getProperties().isEmpty())
            out->setProperty ("automation", juce::var (kept));
        else
            delete kept;
    }
    return result;
}

} // namespace mr
