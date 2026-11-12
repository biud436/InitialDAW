#include "AutomationTargets.h"

#include "MixerText.h"
#include "Project.h"
#include "composition/Arrange.h"

#include <cmath>

namespace daw
{

namespace
{

const int controllerChoices[] = { 1, 11, 7, 10, 74, 64, mr::kPitchBend };

double controllerDefault (int id)
{
    switch (id)
    {
        case 7: return 100.0;
        case 10: return 64.0;
        case 64: return 0.0;
        case mr::kPitchBend: return 0.0;
        default: return 100.0;
    }
}

LaneTarget controllerTarget (int id)
{
    LaneTarget t;
    t.key = mr::controllerName (id);
    t.label = controllerLabel (id);
    t.controller = id;
    t.group = LaneTarget::Group::controller;
    t.minimum = id == mr::kPitchBend ? -1.0 : 0.0;
    t.maximum = id == mr::kPitchBend ? 1.0 : 127.0;
    t.defaultValue = controllerDefault (id);
    return t;
}

LaneTarget faderTarget (const std::string& key, const juce::String& label, double initial)
{
    LaneTarget t;
    t.key = key;
    t.label = label;
    t.minimum = faderlaw::minDb;
    t.maximum = 12.0;
    t.defaultValue = initial;
    t.scale = LaneTarget::Scale::fader;
    return t;
}

} // namespace

LaneTarget vcaTarget (const mr::VcaSpec& vca)
{
    auto t = faderTarget ("vca", juce::String::fromUTF8 ("VCA 게인 (") + juce::String::fromUTF8 (vca.name.c_str()) + ")", vca.gainDb);
    t.group = LaneTarget::Group::song;
    return t;
}

const mr::VcaSpec* vcaOf (const mr::Composition& c, const mr::TrackSpec& track)
{
    for (const auto& v : c.vcas)
        if (! track.vca.empty() && v.name == track.vca)
            return &v;
    return nullptr;
}

double LaneTarget::toNorm (double v) const
{
    switch (scale)
    {
        case Scale::fader:
            return faderlaw::toPosition (v) / faderlaw::toPosition (maximum);
        case Scale::logarithmic:
            if (minimum > 0.0 && v > 0.0)
                return std::log (v / minimum) / std::log (maximum / minimum);
            break;
        case Scale::linear:
            break;
    }
    return maximum > minimum ? (v - minimum) / (maximum - minimum) : 0.0;
}

double LaneTarget::fromNorm (double n) const
{
    n = juce::jlimit (0.0, 1.0, n);
    switch (scale)
    {
        case Scale::fader:
            return juce::jlimit (minimum, maximum, faderlaw::toDb (n * faderlaw::toPosition (maximum)));
        case Scale::logarithmic:
            if (minimum > 0.0)
                return minimum * std::pow (maximum / minimum, n);
            break;
        case Scale::linear:
            break;
    }
    return minimum + n * (maximum - minimum);
}

double LaneTarget::round (double v) const
{
    if (key == "tempo")
        return std::round (v * 10.0) / 10.0;
    if (parameter)
        return roundParameter (*parameter, v);
    if (controller >= 0)
        return controller == mr::kPitchBend ? std::round (v * 1000.0) / 1000.0 : std::round (v);
    return std::round (v * 100.0) / 100.0;
}

juce::String LaneTarget::format (double v) const
{
    if (key == "tempo")
        return juce::String (v, 1) + " BPM";
    if (parameter)
        return formatParameter (*parameter, v);
    if (key == "mixer/pan")
        return formatPan (v);
    if (scale == Scale::fader)
        return formatGainDb (v) + " dB";
    if (controller == mr::kPitchBend)
        return juce::String (v, 3);
    return juce::String ((int) std::lround (v));
}

LaneTarget tempoTarget (const std::vector<mr::TempoPoint>& tempo)
{
    LaneTarget t;
    t.key = "tempo";
    t.label = juce::String::fromUTF8 ("템포 (곡 전체)");
    t.group = LaneTarget::Group::song;
    double lo = 120.0, hi = 120.0;
    if (! tempo.empty())
    {
        lo = hi = tempo.front().bpm;
        for (const auto& p : tempo)
        {
            lo = std::min (lo, p.bpm);
            hi = std::max (hi, p.bpm);
        }
    }
    t.minimum = std::max (10.0, std::floor ((lo - 40.0) / 10.0) * 10.0);
    t.maximum = std::min (400.0, std::ceil ((hi + 40.0) / 10.0) * 10.0);
    t.defaultValue = tempo.empty() ? 120.0 : tempo.front().bpm;
    return t;
}

mr::AutomationLane tempoLane (const std::vector<mr::TempoPoint>& tempo)
{
    mr::AutomationLane lane;
    for (const auto& p : tempo)
        lane.points.push_back ({ p.beat, p.bpm, p.ramp ? mr::CurveShape::linear : mr::CurveShape::step });
    return lane;
}

std::vector<mr::TempoPoint> tempoPoints (const mr::AutomationLane& lane)
{
    std::vector<mr::TempoPoint> out;
    for (const auto& p : lane.points)
    {
        const mr::TempoPoint t { std::max (0.0, std::round (p.beat * 1.0e6) / 1.0e6), juce::jlimit (1.0, 999.0, p.value),
                                 p.curve == mr::CurveShape::linear || p.curve == mr::CurveShape::smooth || p.curve == mr::CurveShape::exponential };
        if (! out.empty() && out.back().beat >= t.beat)
            out.back() = t;
        else
            out.push_back (t);
    }
    if (out.empty())
        out.push_back ({ 0.0, 120.0, false });
    out.front().beat = 0.0;   // 첫 점은 늘 곡의 처음
    if (out.size() > 1 && out[1].beat <= 0.0)
        out.erase (out.begin());
    return out;
}

std::vector<LaneTarget> automationTargets (const mr::TrackSpec& track)
{
    std::vector<LaneTarget> out;
    for (const int id : controllerChoices)
        out.push_back (controllerTarget (id));
    for (const auto& [id, lane] : track.automation)
        if (std::none_of (std::begin (controllerChoices), std::end (controllerChoices), [id = id] (int c) { return c == id; }))
            out.push_back (controllerTarget (id));
    out.push_back (faderTarget ("mixer/gainDb", juce::String::fromUTF8 ("볼륨 (페이더)"), track.mixer.gainDb));
    {
        LaneTarget pan;
        pan.key = "mixer/pan";
        pan.label = juce::String::fromUTF8 ("밸런스");
        pan.group = LaneTarget::Group::mixer;
        pan.minimum = -1.0;
        pan.maximum = 1.0;
        pan.defaultValue = track.mixer.pan;
        out.push_back (pan);
    }
    out[out.size() - 2].group = LaneTarget::Group::mixer;
    for (const auto& send : track.sends)
    {
        auto t = faderTarget ("sends/" + send.bus, juce::String::fromUTF8 ("센드: ") + juce::String::fromUTF8 (send.bus.c_str()), send.levelDb);
        t.group = LaneTarget::Group::send;
        out.push_back (t);
    }
    for (size_t i = 0; i < track.effects.size(); ++i)
    {
        const auto& e = track.effects[i];
        for (const auto& p : mr::effectParameters (e.type))
        {
            if (! p.automatable)
                continue;
            LaneTarget t;
            t.key = "effects/" + std::to_string (i) + ":" + e.type + "/" + p.key;
            t.label = juce::String ((int) i + 1) + ". " + effectName (e.type) + ": " + parameterLabel (e.type, p.key);
            t.group = LaneTarget::Group::effect;
            t.minimum = p.minimum;
            t.maximum = p.maximum;
            const auto current = e.params.getProperty (juce::Identifier (p.key), juce::var());
            t.defaultValue = current.isDouble() || current.isInt() || current.isInt64() ? (double) current : p.defaultValue;
            t.scale = p.logarithmic && p.minimum > 0.0 ? LaneTarget::Scale::logarithmic : LaneTarget::Scale::linear;
            if (p.unit == "dB" && p.minimum <= -60.0)
                t.scale = LaneTarget::Scale::linear;
            t.parameter = p;
            out.push_back (t);
        }
    }
    return out;
}

std::optional<LaneTarget> automationTarget (const mr::TrackSpec& track, const std::string& key)
{
    for (auto& t : automationTargets (track))
        if (t.key == key)
            return t;
    return std::nullopt;
}

std::vector<LaneTarget> automatedTargets (const mr::TrackSpec& track)
{
    std::vector<LaneTarget> out;
    for (auto& t : automationTargets (track))
        if (! mr::automationLaneOf (track, t.key).empty())
            out.push_back (t);
    return out;
}

} // namespace daw
