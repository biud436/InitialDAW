#include "composition/Composition.h"
#include "composition/Document.h"

#include "composition/Features.h"
#include "composition/Harmony.h"
#include "core/Json.h"
#include "effects/Effects.h"
#include "instruments/SimpleSynth.h"

#include <algorithm>
#include <functional>
#include <cctype>
#include <cmath>
#include <set>

namespace mr
{

using json::ObjectReader;

// ---------------------------------------------------------------------------
// 오토메이션
// ---------------------------------------------------------------------------

double AutomationLane::valueAt (double beat) const
{
    if (points.empty())
        return 0.0;
    if (beat <= points.front().beat)
        return points.front().value;
    if (beat >= points.back().beat)
        return points.back().value;

    auto it = std::upper_bound (points.begin(), points.end(), beat,
                                [] (double b, const AutomationPoint& p) { return b < p.beat; });
    const auto& b = *it;
    const auto& a = *(it - 1);
    const double span = b.beat - a.beat;
    if (span <= 0.0)
        return b.value;
    const double t = (beat - a.beat) / span;

    switch (a.curve)
    {
        case CurveShape::step:
            return a.value;
        case CurveShape::smooth:
        {
            const double s = t * t * (3.0 - 2.0 * t);
            return a.value + (b.value - a.value) * s;
        }
        case CurveShape::exponential:
        {
            // 양수끼리는 비율로, 아니면 선형으로 (0 이나 부호가 바뀌면 지수 곡선이 정의되지 않는다)
            if (a.value > 0.0 && b.value > 0.0)
                return a.value * std::pow (b.value / a.value, t);
            return a.value + (b.value - a.value) * t;
        }
        case CurveShape::linear:
        default:
            return a.value + (b.value - a.value) * t;
    }
}

std::string controllerName (int id)
{
    if (id == kPitchBend)
        return "pitchBend";
    if (id == kAftertouch)
        return "aftertouch";
    return "cc" + std::to_string (id);
}

std::optional<CurveShape> parseCurveShape (const std::string& name)
{
    if (name == "linear") return CurveShape::linear;
    if (name == "step") return CurveShape::step;
    if (name == "exponential") return CurveShape::exponential;
    if (name == "smooth") return CurveShape::smooth;
    return std::nullopt;
}

std::optional<FadeShape> parseFadeShape (const std::string& name)
{
    if (name == "sine") return FadeShape::sine;
    if (name == "linear") return FadeShape::linear;
    if (name == "equalPower") return FadeShape::equalPower;
    if (name == "exponential") return FadeShape::exponential;
    return std::nullopt;
}

const char* fadeShapeName (FadeShape shape)
{
    switch (shape)
    {
        case FadeShape::linear: return "linear";
        case FadeShape::equalPower: return "equalPower";
        case FadeShape::exponential: return "exponential";
        case FadeShape::sine: break;
    }
    return "sine";
}

std::optional<int> parseControllerName (const std::string& name)
{
    if (name == "pitchBend")
        return kPitchBend;
    if (name == "aftertouch")
        return kAftertouch;
    if (name.size() >= 3 && name.size() <= 5 && name.rfind ("cc", 0) == 0)
    {
        for (size_t i = 2; i < name.size(); ++i)
            if (! std::isdigit ((unsigned char) name[i]))
                return std::nullopt;
        if (name.size() > 3 && name[2] == '0')
            return std::nullopt;
        const int n = std::stoi (name.substr (2));
        if (n >= 0 && n <= 127)
            return n;
    }
    return std::nullopt;
}

std::optional<int> parseNoteName (const std::string& raw)
{
    if (raw.empty())
        return std::nullopt;
    static const int base[] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    const char letter = (char) std::toupper ((unsigned char) raw[0]);
    if (letter < 'A' || letter > 'G')
        return std::nullopt;
    int value = base[letter - 'A'];
    size_t i = 1;
    while (i < raw.size() && (raw[i] == '#' || raw[i] == 'b'))
    {
        value += raw[i] == '#' ? 1 : -1;
        ++i;
    }
    if (i >= raw.size())
        return std::nullopt;
    bool negative = false;
    if (raw[i] == '-')
    {
        negative = true;
        ++i;
    }
    if (i >= raw.size())
        return std::nullopt;
    int octave = 0;
    for (; i < raw.size(); ++i)
    {
        if (! std::isdigit ((unsigned char) raw[i]))
            return std::nullopt;
        octave = octave * 10 + (raw[i] - '0');
        if (octave > 20)
            return std::nullopt;
    }
    if (negative)
        octave = -octave;
    const int midi = (octave + 1) * 12 + value;
    if (midi < 0 || midi > 127)
        return std::nullopt;
    return midi;
}

// ---------------------------------------------------------------------------
// 파싱
// ---------------------------------------------------------------------------

namespace
{

const std::vector<std::string> rootKeys { "$schema", "title", "comment", "description", "metadata", "tempo", "timeSignature",
                                          "performance", "instruments", "tracks", "buses", "vcas", "master", "render", "markers", "meterChanges", "key", "keyChanges",
                                          "video" };
const std::vector<std::string> trackKeys { "name", "instrument", "articulation", "transpose", "notes", "automation",
                                           "performance", "mixer", "effects", "sends", "plugin", "synth", "color", "clips", "output", "folder", "vca", "comment" };
const std::vector<std::string> noteKeys { "pitch", "startBeat", "durationBeat", "velocity", "channel", "articulation", "humanize", "lyric",
                                          "comment" };
const std::vector<std::string> performanceKeys { "timingHumanizeMs", "velocityHumanize", "noteLengthHumanize", "legatoOverlapMs",
                                                 "swing", "swingGrid", "accentPattern", "accentGrid", "humanizeSeed", "comment" };
const std::vector<std::string> mixerKeys { "gainDb", "pan", "mute", "solo", "automation" };
const std::vector<std::string> renderKeys { "endBeat", "tailSeconds", "fadeOutSeconds" };
const std::vector<std::string> masterKeys { "mixer", "effects", "targetLufs" };
const std::vector<std::string> busKeys { "name", "mixer", "effects", "vca", "comment" };
const std::vector<std::string> vcaKeys { "name", "gainDb", "mute", "automation", "comment" };
const std::vector<std::string> sendKeys { "bus", "levelDb", "preFader", "automation" };
const std::vector<std::string> trackPluginKeys { "state", "program", "parameters", "comment" };
const std::vector<std::string> pointKeys { "beat", "value", "curve" };

void notImplemented (Diagnostics& d, const std::string& path, const std::string& what, const std::string& phase)
{
    d.error (path, what + " is not implemented yet", "planned for " + phase + " (docs/plans/mu1-music-render.md)");
}

std::optional<int> readPitch (const juce::var& v, const std::string& path, Diagnostics& d)
{
    if (json::isInteger (v))
    {
        auto p = json::readInteger (v, path, d, 0, 127);
        return p ? std::optional<int> ((int) *p) : std::nullopt;
    }
    if (v.isString())
    {
        const auto name = v.toString().toStdString();
        if (auto p = parseNoteName (name))
            return p;
        d.error (path, "invalid note name '" + name + "'", "use a MIDI number 0..127 or a name like C4, F#3, Bb2 (C4 = 60)");
        return std::nullopt;
    }
    d.error (path, "pitch must be an integer 0..127 or a note name, got " + json::typeName (v));
    return std::nullopt;
}

PerformanceSettings readPerformance (const juce::var& v, const std::string& path, Diagnostics& d)
{
    PerformanceSettings p;
    ObjectReader r (v, path, d);
    if (! r.ok())
        return p;
    r.known ("comment");
    p.timingHumanizeMs = r.number ("timingHumanizeMs", false, 0.0, 200.0);
    p.velocityHumanize = r.number ("velocityHumanize", false, 0.0, 64.0);
    p.noteLengthHumanize = r.number ("noteLengthHumanize", false, 0.0, 0.9);
    p.legatoOverlapMs = r.number ("legatoOverlapMs", false, 0.0, 500.0);
    p.swing = r.number ("swing", false, 0.0, 1.0);
    p.swingGrid = r.number ("swingGrid", false, 0.0625, 4.0);
    p.accentGrid = r.number ("accentGrid", false, 0.0625, 16.0);
    if (auto* arr = r.array ("accentPattern", false))
    {
        std::vector<int> pattern;
        for (int i = 0; i < arr->size(); ++i)
            if (auto x = json::readInteger ((*arr)[i], json::indexPath (r.pathOf ("accentPattern"), (size_t) i), d, -64, 64))
                pattern.push_back ((int) *x);
        if (arr->isEmpty())
            d.error (r.pathOf ("accentPattern"), "accentPattern must not be empty");
        p.accentPattern = pattern;
    }
    if (const auto* seed = r.get ("humanizeSeed"))
    {
        if (seed->isString())
        {
            const auto s = seed->toString().toStdString();
            if (! s.empty() && std::all_of (s.begin(), s.end(), [] (char c) { return std::isdigit ((unsigned char) c); }) && s.size() <= 19)
                p.humanizeSeed = (uint64_t) std::stoull (s);
            else
                d.error (r.pathOf ("humanizeSeed"), "humanizeSeed string must contain only digits");
        }
        else if (auto x = json::readInteger (*seed, r.pathOf ("humanizeSeed"), d, 0))
            p.humanizeSeed = (uint64_t) *x;
    }
    r.warnUnknown (performanceKeys);
    return p;
}

MixerSettings readMixer (const juce::var& v, const std::string& path, Diagnostics& d)
{
    MixerSettings m;
    ObjectReader r (v, path, d);
    if (! r.ok())
        return m;
    m.gainDb = r.number ("gainDb", false, -120.0, 24.0).value_or (0.0);
    m.pan = r.number ("pan", false, -1.0, 1.0).value_or (0.0);
    m.mute = r.boolean ("mute", false).value_or (false);
    m.solo = r.boolean ("solo", false).value_or (false);
    if (const auto* lanes = r.object ("automation", false))
    {
        // 페이더와 밸런스의 곡선: "automation": { "gainDb": [...], "pan": [...] }
        ObjectReader ar (*lanes, r.pathOf ("automation"), d);
        if (const auto* lane = ar.get ("gainDb"))
            m.gainLane = readAutomationLane (*lane, ar.pathOf ("gainDb"), -120.0, 24.0, d);
        if (const auto* lane = ar.get ("pan"))
            m.panLane = readAutomationLane (*lane, ar.pathOf ("pan"), -1.0, 1.0, d);
        for (const auto& key : ar.keys())
            if (key != "gainDb" && key != "pan")
                d.error (ar.pathOf (key), "a mixer can automate only gainDb and pan, not '" + key + "'");
    }
    r.warnUnknown (mixerKeys);
    return m;
}

std::vector<EffectSpec> readEffects (const juce::Array<juce::var>& arr, const std::string& path, Diagnostics& d)
{
    std::vector<EffectSpec> out;
    for (int i = 0; i < arr.size(); ++i)
    {
        const auto p = json::indexPath (path, (size_t) i);
        ObjectReader r (arr[i], p, d);
        if (! r.ok())
            continue;
        EffectSpec e;
        e.type = r.string ("type", true).value_or ("");
        e.bypass = r.boolean ("bypass", false).value_or (false);
        e.params = arr[i];
        e.path = p;
        if (! e.type.empty())
            validateEffectSpec (e, d);
        out.push_back (e);
    }
    return out;
}

AutomationLane readLane (const juce::var& v, const std::string& path, int controller, Diagnostics& d)
{
    const double lo = controller == kPitchBend ? -1.0 : 0.0;
    const double hi = controller == kPitchBend ? 1.0 : 127.0;
    return readAutomationLane (v, path, lo, hi, d);
}

Note readNote (const juce::var& v, const std::string& path, Diagnostics& d, bool& valid)
{
    Note n;
    valid = false;
    ObjectReader r (v, path, d);
    if (! r.ok())
        return n;
    r.known ("comment");
    std::optional<int> pitch;
    if (const auto* pv = r.get ("pitch"))
        pitch = readPitch (*pv, r.pathOf ("pitch"), d);
    else
        d.error (r.pathOf ("pitch"), "required pitch is missing");
    auto start = r.number ("startBeat", true, 0.0);
    auto duration = r.number ("durationBeat", true);
    if (duration && *duration <= 0.0)
    {
        d.error (r.pathOf ("durationBeat"), "durationBeat must be greater than 0, got " + formatNumber (*duration));
        duration.reset();
    }
    auto velocity = r.integer ("velocity", true, 1, 127);
    n.channel = (int) r.integer ("channel", false, 1, 16).value_or (1);
    n.articulation = r.string ("articulation", false).value_or ("");
    n.humanize = r.boolean ("humanize", false).value_or (true);
    n.lyric = r.string ("lyric", false).value_or ("");
    r.warnUnknown (noteKeys);
    if (pitch && start && duration && velocity)
    {
        n.pitch = *pitch;
        n.startBeat = *start;
        n.durationBeat = *duration;
        n.velocity = (int) *velocity;
        valid = true;
    }
    return n;
}

void readTempo (const juce::var& v, const std::string& path, Composition& c, Diagnostics& d)
{
    if (json::isNumber (v))
    {
        if (auto bpm = json::readNumber (v, path, d, 1.0, 999.0))
            c.tempo = { TempoPoint { 0.0, *bpm, false } };
        return;
    }
    if (! v.isArray())
    {
        d.error (path, "tempo must be a number (bpm) or an array of {beat, bpm, ramp}, got " + json::typeName (v));
        return;
    }
    const auto& arr = *v.getArray();
    double last = -1.0;
    for (int i = 0; i < arr.size(); ++i)
    {
        const auto p = json::indexPath (path, (size_t) i);
        ObjectReader r (arr[i], p, d);
        if (! r.ok())
            continue;
        auto beat = r.number ("beat", true, 0.0);
        auto bpm = r.number ("bpm", true, 1.0, 999.0);
        auto ramp = r.boolean ("ramp", false).value_or (false);
        r.warnUnknown ({ "beat", "bpm", "ramp" });
        if (! beat || ! bpm)
            continue;
        if (i == 0 && *beat != 0.0)
            d.error (r.pathOf ("beat"), "the first tempo point must be at beat 0");
        if (*beat <= last)
            d.error (r.pathOf ("beat"), "tempo points must have strictly ascending beats");
        last = *beat;
        c.tempo.push_back ({ *beat, *bpm, ramp });
    }
    if (arr.isEmpty())
        d.error (path, "tempo array must not be empty");
}

void readTrack (const juce::var& v, const std::string& path, Composition& c, Diagnostics& d)
{
    TrackSpec t;
    t.path = path;
    ObjectReader r (v, path, d);
    if (! r.ok())
        return;
    r.known ("comment");
    t.name = r.string ("name", true).value_or ("");
    t.instrument = r.string ("instrument", true).value_or ("");
    t.articulation = r.string ("articulation", false).value_or ("");
    t.transpose = (int) r.integer ("transpose", false, -48, 48).value_or (0);
    t.output = r.string ("output", false).value_or ("");
    t.folder = r.string ("folder", false).value_or ("");
    t.vca = r.string ("vca", false).value_or ("");
    if (auto colour = r.string ("color", false))
    {
        const bool hex = colour->size() == 7 && (*colour)[0] == '#'
                         && std::all_of (colour->begin() + 1, colour->end(), [] (char ch) { return std::isxdigit ((unsigned char) ch) != 0; });
        if (hex)
            t.colour = *colour;
        else
            d.error (r.pathOf ("color"), "track color must be \"#rrggbb\", got '" + *colour + "'");
    }

    if (auto* notes = r.array ("notes", false))
    {
        for (int i = 0; i < notes->size(); ++i)
        {
            bool valid = false;
            auto n = readNote ((*notes)[i], json::indexPath (r.pathOf ("notes"), (size_t) i), d, valid);
            if (valid)
                t.notes.push_back (n);
        }
    }
    else if (! r.has ("notes") && ! r.has ("clips"))
        d.warning (r.pathOf ("notes"), "track has no notes");

    // 컨트롤러 곡선: "automation": {"cc1": [...]} 또는 트랙에 바로 "cc1": [...]
    auto addLane = [&] (const std::string& key, const juce::var& lane, const std::string& lanePath)
    {
        auto id = parseControllerName (key);
        if (! id)
        {
            d.error (lanePath, "unknown controller '" + key + "'", "use cc0..cc127, pitchBend or aftertouch");
            return;
        }
        if (t.automation.count (*id) > 0)
        {
            d.error (lanePath, "controller '" + key + "' is defined twice (in the track and in automation)");
            return;
        }
        t.automation[*id] = readLane (lane, lanePath, *id, d);
    };
    if (const auto* autoObj = r.object ("automation", false))
    {
        ObjectReader ar (*autoObj, r.pathOf ("automation"), d);
        for (const auto& key : ar.keys())
            addLane (key, *ar.get (key), ar.pathOf (key));
    }
    for (const auto& key : r.keys())
        if (parseControllerName (key))
            addLane (key, *r.get (key), r.pathOf (key));

    if (auto* clips = r.array ("clips", false))
    {
        for (int i = 0; i < clips->size(); ++i)
        {
            const auto p = json::indexPath (r.pathOf ("clips"), (size_t) i);
            ObjectReader cr ((*clips)[i], p, d);
            if (! cr.ok())
                continue;
            cr.known ("comment");
            AudioClip clip;
            clip.path = p;
            clip.file = cr.string ("file", true).value_or ("");
            auto start = cr.number ("startBeat", true, 0.0);
            clip.offsetSeconds = cr.number ("offsetSeconds", false, 0.0).value_or (0.0);
            if (cr.has ("lengthSeconds"))
                clip.lengthSeconds = cr.number ("lengthSeconds", true, 1.0e-4);
            clip.gainDb = cr.number ("gainDb", false, -96.0, 24.0).value_or (0.0);
            clip.fadeInSeconds = cr.number ("fadeInSeconds", false, 0.0, 600.0).value_or (0.0);
            clip.fadeOutSeconds = cr.number ("fadeOutSeconds", false, 0.0, 600.0).value_or (0.0);
            auto fadeShape = [&cr, &d] (const char* key)
            {
                const auto name = cr.string (key, false);
                if (! name)
                    return FadeShape::sine;
                if (const auto shape = parseFadeShape (*name))
                    return *shape;
                d.error (cr.pathOf (key), "unknown fade shape '" + *name + "'", "use sine, linear, equalPower or exponential");
                return FadeShape::sine;
            };
            clip.fadeInShape = fadeShape ("fadeInShape");
            clip.fadeOutShape = fadeShape ("fadeOutShape");
            clip.reverse = cr.boolean ("reverse", false).value_or (false);
            if (auto* curve = cr.array ("gainCurve", false))
            {
                // 클립 안의 게인 곡선: [{ "seconds", "value" (dB), "curve" }], 파일의 초 오름차순 (같은 초의 두 점은 뛰기)
                for (int k = 0; k < curve->size(); ++k)
                {
                    ObjectReader gr ((*curve)[k], json::indexPath (cr.pathOf ("gainCurve"), (size_t) k), d);
                    if (! gr.ok())
                        continue;
                    const auto seconds = gr.number ("seconds", true, 0.0);
                    const auto value = gr.number ("value", true, -96.0, 24.0);
                    AutomationPoint pt;
                    if (const auto name = gr.string ("curve", false))
                    {
                        if (const auto shape = parseCurveShape (*name))
                            pt.curve = *shape;
                        else
                            d.error (gr.pathOf ("curve"), "unknown curve '" + *name + "'", "use linear, step, exponential or smooth");
                    }
                    gr.warnUnknown ({ "seconds", "value", "curve", "comment" });
                    if (! seconds || ! value)
                        continue;
                    if (! clip.gainCurve.points.empty() && *seconds < clip.gainCurve.points.back().beat)
                    {
                        d.error (gr.pathOf ("seconds"), "gain curve points must be in ascending seconds");
                        continue;
                    }
                    pt.beat = *seconds;
                    pt.value = *value;
                    clip.gainCurve.points.push_back (pt);
                }
            }
            if (cr.has ("sourceBpm"))
                clip.sourceBpm = cr.number ("sourceBpm", true, 20.0, 400.0);
            clip.stretch = cr.number ("stretch", false, 0.25, 4.0).value_or (1.0);
            clip.pitchSemitones = cr.number ("pitchSemitones", false, -24.0, 24.0).value_or (0.0);
            clip.muted = cr.boolean ("muted", false).value_or (false);
            if (clip.sourceBpm && clip.stretch != 1.0)
                d.error (cr.pathOf ("stretch"), "a clip either follows the tempo (sourceBpm) or has a fixed stretch, not both");
            if (auto* warp = cr.array ("warp", false))
            {
                for (int k = 0; k < warp->size(); ++k)
                {
                    ObjectReader wr ((*warp)[k], json::indexPath (cr.pathOf ("warp"), (size_t) k), d);
                    if (! wr.ok())
                        continue;
                    const auto seconds = wr.number ("seconds", true, 0.0);
                    const auto beat = wr.number ("beat", true);
                    wr.warnUnknown ({ "seconds", "beat" });
                    if (! seconds || ! beat)
                        continue;
                    if (! clip.warp.empty() && (*seconds <= clip.warp.back().seconds || *beat <= clip.warp.back().beat))
                    {
                        d.error (wr.pathOf ("seconds"), "warp markers must rise in both seconds and beat");
                        continue;
                    }
                    clip.warp.push_back ({ *seconds, *beat });
                }
                if (clip.warp.size() == 1)
                {
                    d.error (cr.pathOf ("warp"), "warp needs at least two markers");
                    clip.warp.clear();
                }
                if (! clip.warp.empty() && clip.stretch != 1.0)
                    d.error (cr.pathOf ("stretch"), "a warped clip has no fixed stretch");
            }
            if (const auto mode = cr.string ("warpMode", false))
            {
                if (*mode == "slice")
                    clip.warpSlices = true;
                else if (*mode != "stretch")
                    d.error (cr.pathOf ("warpMode"), "warpMode is stretch or slice, got '" + *mode + "'");
                if (clip.warpSlices && clip.pitchSemitones != 0.0)
                    d.error (cr.pathOf ("pitchSemitones"), "a sliced clip plays at the file's own pitch");
            }
            if (const auto* tuneVar = cr.get ("pitchCorrection"))
            {
                ObjectReader tr (*tuneVar, cr.pathOf ("pitchCorrection"), d);
                if (tr.ok())
                {
                    PitchCorrection pc;
                    pc.strength = tr.number ("strength", false, 0.0, 1.0).value_or (1.0);
                    pc.speedMs = tr.number ("speedMs", false, 0.0, 1000.0).value_or (30.0);
                    pc.formant = tr.boolean ("formant", false).value_or (true);
                    pc.key = tr.string ("key", false).value_or ("");
                    pc.scale = tr.string ("scale", false).value_or ("");
                    pc.root = tr.string ("root", false).value_or ("");
                    tr.warnUnknown ({ "strength", "speedMs", "formant", "key", "scale", "root" });
                    bool valid = true;
                    if (! pc.key.empty())
                    {
                        if (const auto key = parseKeyName (pc.key))
                        {
                            const int tonic = tonicPitchClass (*key);
                            for (const int step : key->minor ? std::vector<int> { 0, 2, 3, 5, 7, 8, 10 } : std::vector<int> { 0, 2, 4, 5, 7, 9, 11 })
                                pc.allowed[(size_t) ((tonic + step) % 12)] = true;
                        }
                        else
                        {
                            d.error (tr.pathOf ("key"), "unknown key '" + pc.key + "'", "a tonic (C, F#, Bb, ...) and major or minor");
                            valid = false;
                        }
                        if (! pc.scale.empty())
                            d.error (tr.pathOf ("scale"), "pitchCorrection takes a key or a scale, not both");
                    }
                    else if (! pc.scale.empty())
                    {
                        const auto* scale = findScaleType (pc.scale);
                        const auto root = parseNoteName (pc.root + "4");
                        if (scale == nullptr)
                            d.error (tr.pathOf ("scale"), "unknown scale '" + pc.scale + "'", "see music-render scales");
                        if (! root)
                            d.error (tr.pathOf ("root"), "a scale needs its root note (C, F#, Bb, ...)");
                        if (scale == nullptr || ! root)
                            valid = false;
                        else
                            for (const int step : scale->intervals)
                                pc.allowed[(size_t) ((*root + step) % 12)] = true;
                    }
                    else
                        pc.allowed.fill (true);
                    if (clip.warpSlices)
                        d.error (tr.pathOf ("strength"), "a sliced clip plays at the file's own pitch");
                    if (valid)
                        clip.pitchCorrection = pc;
                }
            }
            if (auto* edits = cr.array ("pitchEdits", false))
            {
                for (int k = 0; k < edits->size(); ++k)
                {
                    ObjectReader er ((*edits)[k], json::indexPath (cr.pathOf ("pitchEdits"), (size_t) k), d);
                    if (! er.ok())
                        continue;
                    const auto seconds = er.number ("seconds", true, 0.0);
                    const auto length = er.number ("lengthSeconds", true, 0.01, 3600.0);
                    PitchEdit e;
                    e.semitones = er.number ("semitones", false, -24.0, 24.0).value_or (0.0);
                    e.flatten = er.number ("flatten", false, 0.0, 1.0).value_or (0.0);
                    e.vibrato = er.number ("vibrato", false, 0.0, 2.0).value_or (1.0);
                    er.warnUnknown ({ "seconds", "lengthSeconds", "semitones", "flatten", "vibrato", "comment" });
                    if (! seconds || ! length)
                        continue;
                    e.seconds = *seconds;
                    e.lengthSeconds = *length;
                    if (! clip.pitchEdits.empty() && e.seconds < clip.pitchEdits.back().endSeconds() - 1.0e-6)
                    {
                        d.error (er.pathOf ("seconds"), "pitch edits must be in time order and must not overlap");
                        continue;
                    }
                    clip.pitchEdits.push_back (e);
                }
                if (clip.warpSlices && ! clip.pitchEdits.empty())
                    d.error (cr.pathOf ("pitchEdits"), "a sliced clip plays at the file's own pitch");
            }
            cr.warnUnknown ({ "file", "startBeat", "offsetSeconds", "lengthSeconds", "gainDb", "fadeInSeconds", "fadeOutSeconds", "fadeInShape", "fadeOutShape",
                              "reverse", "gainCurve", "sourceBpm", "stretch", "pitchSemitones", "warp", "warpMode", "pitchCorrection", "pitchEdits", "muted",
                              "comment" });
            if (! start || clip.file.empty())
                continue;
            clip.startBeat = *start;
            t.clips.push_back (clip);
        }
    }

    if (const auto* perf = r.object ("performance", false))
        t.performance = readPerformance (*perf, r.pathOf ("performance"), d);
    if (const auto* mixer = r.object ("mixer", false))
    {
        t.mixer = readMixer (*mixer, r.pathOf ("mixer"), d);
        t.mixerSpecified = true;
    }
    if (auto* effects = r.array ("effects", false))
        t.effects = readEffects (*effects, r.pathOf ("effects"), d);
    if (auto* sends = r.array ("sends", false))
    {
        for (int i = 0; i < sends->size(); ++i)
        {
            ObjectReader sr ((*sends)[i], json::indexPath (r.pathOf ("sends"), (size_t) i), d);
            if (! sr.ok())
                continue;
            SendSpec s;
            s.bus = sr.string ("bus", true).value_or ("");
            s.levelDb = sr.number ("levelDb", false, -120.0, 12.0).value_or (0.0);
            s.preFader = sr.boolean ("preFader", false).value_or (false);
            if (const auto* lanes = sr.object ("automation", false))
            {
                // 센드 크기의 곡선: "automation": { "levelDb": [ {beat, value, curve}, ... ] }
                ObjectReader ar (*lanes, sr.pathOf ("automation"), d);
                if (const auto* lane = ar.get ("levelDb"))
                    s.levelLane = readAutomationLane (*lane, ar.pathOf ("levelDb"), -120.0, 12.0, d);
                for (const auto& key : ar.keys())
                    if (key != "levelDb")
                        d.error (ar.pathOf (key), "a send can automate only levelDb, not '" + key + "'");
            }
            sr.warnUnknown (sendKeys);
            t.sends.push_back (s);
        }
    }
    if (const auto* plugin = r.object ("plugin", false))
    {
        // 악기가 플러그인일 때 레지스트리 설정 뒤에 적용한다 (매개변수 이름은 플러그인을 불러야 알 수 있다)
        ObjectReader pr (*plugin, r.pathOf ("plugin"), d);
        if (auto state = pr.string ("state", false))
        {
            juce::MemoryBlock block;
            if (! decodePluginState (*state, block))
                d.error (pr.pathOf ("state"), "plugin state must be base64 (as written by the app or `music-render plugins save-state`)");
            t.plugin.stateBase64 = *state;
        }
        if (auto program = pr.integer ("program", false, 0, 100000))
            t.plugin.program = (int) *program;
        if (const auto* params = pr.object ("parameters", false))
        {
            ObjectReader ppr (*params, pr.pathOf ("parameters"), d);
            for (const auto& key : ppr.keys())
            {
                const auto* v = ppr.get (key);
                if (v->isString() || json::isNumber (*v))
                    t.plugin.parameters[key] = *v;
                else
                    d.error (ppr.pathOf (key), "parameter value must be a number 0..1 or text, got " + json::typeName (*v));
            }
        }
        pr.warnUnknown (trackPluginKeys);
    }
    if (const auto* synth = r.object ("synth", false))
    {
        // 내장 신스의 값 (키와 범위는 모든 프리셋을 통틀어 검사하고, 그 프리셋이 쓰지 않는 키는 넘어간다)
        ObjectReader sr (*synth, r.pathOf ("synth"), d);
        const auto keys = synthParameterKeys();
        for (const auto& key : sr.keys())
        {
            if (key == "comment")
                continue;
            const auto* p = findSynthParameter (key);
            if (p == nullptr)
            {
                const auto s = closestMatch (key, keys);
                d.error (sr.pathOf (key), "unknown synth parameter '" + key + "'", s.empty() ? std::string() : "did you mean '" + s + "'?");
                continue;
            }
            if (auto v = readSynthValue (sr, key, *p, d))
                t.synth[key] = *v;
        }
    }

    std::vector<std::string> candidates = trackKeys;
    candidates.push_back ("cc1");
    candidates.push_back ("cc11");
    candidates.push_back ("cc64");
    candidates.push_back ("pitchBend");
    r.warnUnknown (candidates);
    c.tracks.push_back (std::move (t));
}

void checkUnimplemented (const Composition& c, Diagnostics& d)
{
    for (const auto& t : c.tracks)
    {
        if (! features::automation)
            for (const auto& [id, lane] : t.automation)
                notImplemented (d, t.path + "." + controllerName (id), "controller automation", "phase 4");
        if (! features::articulation)
        {
            if (! t.articulation.empty())
                notImplemented (d, t.path + ".articulation", "articulation", "phase 4");
            for (size_t i = 0; i < t.notes.size(); ++i)
                if (! t.notes[i].articulation.empty())
                {
                    notImplemented (d, t.path + ".notes[" + std::to_string (i) + "].articulation", "articulation", "phase 4");
                    break;
                }
        }
        if (! features::mixer && t.mixerSpecified)
            notImplemented (d, t.path + ".mixer", "track mixer", "phase 3");
        if (! features::mixer && ! t.sends.empty())
            notImplemented (d, t.path + ".sends", "sends", "phase 3");
        if (! features::effects && ! t.effects.empty())
            notImplemented (d, t.path + ".effects", "effects", "phase 5");
    }
    if (! features::mixer && ! c.buses.empty())
        notImplemented (d, "$.buses", "buses", "phase 3");
    if (! features::effects)
        for (const auto& b : c.buses)
            if (! b.effects.empty())
                notImplemented (d, b.path + ".effects", "effects", "phase 5");
    if (! features::mixer && c.master.mixerSpecified)
        notImplemented (d, "$.master.mixer", "master mixer", "phase 3");
    if (! features::effects && ! c.master.effects.empty())
        notImplemented (d, "$.master.effects", "effects", "phase 5");
    if (! features::loudnessTarget && c.master.targetLufs)
        notImplemented (d, "$.master.targetLufs", "loudness normalisation", "phase 5");
}

} // namespace

AutomationLane readAutomationLane (const juce::var& v, const std::string& path, double lo, double hi, Diagnostics& d)
{
    AutomationLane lane;
    if (! v.isArray())
    {
        d.error (path, "automation lane must be an array of points, got " + json::typeName (v));
        return lane;
    }
    const auto& arr = *v.getArray();
    double lastBeat = -1.0;
    for (int i = 0; i < arr.size(); ++i)
    {
        const auto p = json::indexPath (path, (size_t) i);
        ObjectReader r (arr[i], p, d);
        if (! r.ok())
            continue;
        AutomationPoint pt;
        auto beat = r.number ("beat", true, 0.0);
        auto value = r.number ("value", true, lo, hi);
        if (auto curve = r.string ("curve", false))
        {
            if (const auto shape = parseCurveShape (*curve))
                pt.curve = *shape;
            else
                d.error (r.pathOf ("curve"), "unknown curve '" + *curve + "'", "use linear, step, exponential or smooth");
        }
        r.warnUnknown (pointKeys);
        if (! beat || ! value)
            continue;
        if (*beat < lastBeat)
            d.error (r.pathOf ("beat"), "automation points must be in ascending beat order");
        lastBeat = *beat;
        pt.beat = *beat;
        pt.value = *value;
        lane.points.push_back (pt);
    }
    return lane;
}

ParseResult parseComposition (const juce::String& text, const juce::File& sourceFile)
{
    juce::var root;
    const auto parsed = juce::JSON::parse (text, root);
    if (parsed.failed())
    {
        ParseResult result;
        result.diagnostics.error ("$", describeJsonError (parsed.getErrorMessage()));
        return result;
    }
    return parseCompositionValue (root, sourceFile);
}

ParseResult parseCompositionValue (const juce::var& root, const juce::File& sourceFile)
{
    ParseResult result;
    auto& d = result.diagnostics;

    Composition c;
    c.sourceFile = sourceFile;
    ObjectReader r (root, "$", d);
    if (! r.ok())
        return result;
    for (const auto& k : { "$schema", "comment", "description", "metadata" })
        r.known (k);

    c.title = r.string ("title", false, true).value_or ("");
    if (const auto loop = root.getProperty ("metadata", {}).getProperty ("loop", {}); ! loop.isVoid())
    {
        const auto start = loop.getProperty ("startBeat", {}), end = loop.getProperty ("endBeat", {});
        const bool numbers = (start.isInt() || start.isInt64() || start.isDouble()) && (end.isInt() || end.isInt64() || end.isDouble());
        if (numbers && (double) start >= 0.0 && (double) end > (double) start)
            c.loop = LoopSpec { (double) start, (double) end };
        else
            d.warning ("$.metadata.loop", "loop needs numeric startBeat and endBeat with endBeat > startBeat >= 0; it is ignored",
                       "e.g. \"loop\": { \"startBeat\": 16, \"endBeat\": 288 }");
    }

    if (auto* markers = r.array ("markers", false))
    {
        double last = 0.0;
        for (int i = 0; i < markers->size(); ++i)
        {
            const auto p = json::indexPath (r.pathOf ("markers"), (size_t) i);
            ObjectReader mr ((*markers)[i], p, d);
            if (! mr.ok())
                continue;
            mr.known ("comment");
            auto beat = mr.number ("beat", true, 0.0);
            auto name = mr.string ("name", true, true);
            mr.warnUnknown ({ "beat", "name", "comment" });
            if (! beat || ! name)
                continue;
            if (*beat < last)
                d.error (mr.pathOf ("beat"), "markers must be in ascending beat order");
            last = *beat;
            c.markers.push_back ({ *beat, *name });
        }
    }

    if (const auto* video = r.object ("video", false))
    {
        ObjectReader vr (*video, r.pathOf ("video"), d);
        vr.known ("comment");
        auto file = vr.string ("file", true, false);
        auto offset = vr.number ("offsetSeconds", false, -86400.0, 86400.0);
        vr.warnUnknown ({ "file", "offsetSeconds", "comment" });
        if (file && ! file->empty())
            c.video = VideoSpec { *file, offset.value_or (0.0) };
    }

    if (const auto* tempo = r.get ("tempo"))
        readTempo (*tempo, r.pathOf ("tempo"), c, d);
    else
        d.error (r.pathOf ("tempo"), "required tempo is missing", "e.g. \"tempo\": 92");

    if (auto* ts = r.array ("timeSignature", false))
    {
        if (ts->size() != 2)
            d.error (r.pathOf ("timeSignature"), "timeSignature must be [beatsPerBar, beatUnit], e.g. [4, 4]");
        else
        {
            auto num = json::readInteger ((*ts)[0], r.pathOf ("timeSignature") + "[0]", d, 1, 32);
            auto den = json::readInteger ((*ts)[1], r.pathOf ("timeSignature") + "[1]", d, 1, 32);
            if (den && (*den & (*den - 1)) != 0)
                d.error (r.pathOf ("timeSignature") + "[1]", "beat unit must be a power of two (1, 2, 4, 8, 16, 32)");
            if (num && den)
            {
                c.beatsPerBar = (int) *num;
                c.beatUnit = (int) *den;
            }
        }
    }

    // 곡 중간의 박자 바꾸기: [{ "beat": 64, "timeSignature": [3, 4] }], 앞 박자의 마디 줄에
    if (auto* changes = r.array ("meterChanges", false))
    {
        double last = 0.0;
        for (int i = 0; i < changes->size(); ++i)
        {
            const auto p = json::indexPath (r.pathOf ("meterChanges"), (size_t) i);
            ObjectReader mr ((*changes)[i], p, d);
            if (! mr.ok())
                continue;
            mr.known ("comment");
            auto beat = mr.number ("beat", true, 0.0);
            std::optional<int> num, den;
            if (auto* ts = mr.array ("timeSignature", true))
            {
                if (ts->size() != 2)
                    d.error (mr.pathOf ("timeSignature"), "timeSignature must be [beatsPerBar, beatUnit], e.g. [3, 4]");
                else
                {
                    if (auto n = json::readInteger ((*ts)[0], mr.pathOf ("timeSignature") + "[0]", d, 1, 32))
                        num = (int) *n;
                    if (auto dd = json::readInteger ((*ts)[1], mr.pathOf ("timeSignature") + "[1]", d, 1, 32))
                    {
                        if ((*dd & (*dd - 1)) != 0)
                            d.error (mr.pathOf ("timeSignature") + "[1]", "beat unit must be a power of two (1, 2, 4, 8, 16, 32)");
                        else
                            den = (int) *dd;
                    }
                }
            }
            mr.warnUnknown ({ "beat", "timeSignature", "comment" });
            if (! beat)
                continue;
            if (*beat <= last)
            {
                d.error (mr.pathOf ("beat"), "meter changes must come after beat 0 in ascending beat order");
                continue;
            }
            last = *beat;
            if (num && den)
                c.meterChanges.push_back ({ *beat, *num, *den });
        }
    }

    // 조: "key": "Bb minor", "keyChanges": [{ "beat": 64, "key": "A major" }]
    auto readKey = [&d] (const juce::var& v, const std::string& path) -> std::optional<KeySignature>
    {
        if (! v.isString())
        {
            d.error (path, "key must be a string such as \"Bb minor\" or \"F# major\"");
            return std::nullopt;
        }
        auto key = parseKeyName (v.toString().toStdString());
        if (! key)
            d.error (path, "unknown key '" + v.toString().toStdString() + "'", "a tonic (C, F#, Bb, ...) and major or minor, at most seven sharps or flats");
        return key;
    };
    if (const auto* key = r.get ("key"))
        c.key = readKey (*key, r.pathOf ("key"));
    if (auto* changes = r.array ("keyChanges", false))
    {
        double last = 0.0;
        for (int i = 0; i < changes->size(); ++i)
        {
            const auto p = json::indexPath (r.pathOf ("keyChanges"), (size_t) i);
            ObjectReader kr ((*changes)[i], p, d);
            if (! kr.ok())
                continue;
            const auto beat = kr.number ("beat", true, 0.0);
            std::optional<KeySignature> key;
            if (const auto* v = kr.get ("key"))
                key = readKey (*v, kr.pathOf ("key"));
            else
                d.error (kr.pathOf ("key"), "required key is missing");
            kr.warnUnknown ({ "beat", "key", "comment" });
            if (! beat || ! key)
                continue;
            if (*beat <= last)
            {
                d.error (kr.pathOf ("beat"), "key changes must come after beat 0 in ascending beat order");
                continue;
            }
            last = *beat;
            c.keyChanges.push_back ({ *beat, *key });
        }
    }

    if (const auto* perf = r.object ("performance", false))
        c.performance = readPerformance (*perf, r.pathOf ("performance"), d);

    if (const auto* reg = r.get ("instruments"))
    {
        const auto base = sourceFile.getParentDirectory();
        auto addRegistry = [&] (const juce::var& v, const std::string& p)
        {
            if (v.getDynamicObject() != nullptr)
            {
                c.inlineInstruments.push_back ({ v, p });   // 곡 안의 정의 (레지스트리가 검사한다)
                return;
            }
            if (! v.isString() || v.toString().isEmpty())
            {
                d.error (p, "instruments must be registry file paths or objects of instrument definitions, got " + json::typeName (v));
                return;
            }
            c.registries.push_back (base.getChildFile (v.toString()));
        };
        if (reg->isArray())
            for (int i = 0; i < reg->getArray()->size(); ++i)
                addRegistry ((*reg->getArray())[i], json::indexPath (r.pathOf ("instruments"), (size_t) i));
        else
            addRegistry (*reg, r.pathOf ("instruments"));
    }

    if (auto* tracks = r.array ("tracks", true))
    {
        if (tracks->isEmpty())
            d.error (r.pathOf ("tracks"), "tracks must contain at least one track");
        for (int i = 0; i < tracks->size(); ++i)
            readTrack ((*tracks)[i], json::indexPath (r.pathOf ("tracks"), (size_t) i), c, d);

        std::set<std::string> names;
        for (const auto& t : c.tracks)
            if (! t.name.empty() && ! names.insert (t.name).second)
                d.error (t.path + ".name", "duplicate track name '" + t.name + "'", "track names must be unique (they name the stems)");
    }

    if (auto* buses = r.array ("buses", false))
    {
        for (int i = 0; i < buses->size(); ++i)
        {
            const auto p = json::indexPath (r.pathOf ("buses"), (size_t) i);
            ObjectReader br ((*buses)[i], p, d);
            if (! br.ok())
                continue;
            br.known ("comment");
            BusSpec b;
            b.path = p;
            b.name = br.string ("name", true).value_or ("");
            b.vca = br.string ("vca", false).value_or ("");
            if (const auto* m = br.object ("mixer", false))
                b.mixer = readMixer (*m, br.pathOf ("mixer"), d);
            if (auto* fx = br.array ("effects", false))
                b.effects = readEffects (*fx, br.pathOf ("effects"), d);
            br.warnUnknown (busKeys);
            c.buses.push_back (b);
        }
    }

    if (auto* vcas = r.array ("vcas", false))
    {
        std::set<std::string> names;
        for (int i = 0; i < vcas->size(); ++i)
        {
            const auto p = json::indexPath (r.pathOf ("vcas"), (size_t) i);
            ObjectReader vr ((*vcas)[i], p, d);
            if (! vr.ok())
                continue;
            VcaSpec v;
            v.path = p;
            v.name = vr.string ("name", true).value_or ("");
            if (! v.name.empty() && ! names.insert (v.name).second)
                d.error (vr.pathOf ("name"), "duplicate VCA name '" + v.name + "'");
            v.gainDb = vr.number ("gainDb", false, -120.0, 24.0).value_or (0.0);
            v.mute = vr.boolean ("mute", false).value_or (false);
            if (const auto* lanes = vr.object ("automation", false))
            {
                ObjectReader ar (*lanes, vr.pathOf ("automation"), d);
                if (const auto* lane = ar.get ("gainDb"))
                    v.gainLane = readAutomationLane (*lane, ar.pathOf ("gainDb"), -120.0, 24.0, d);
                for (const auto& key : ar.keys())
                    if (key != "gainDb")
                        d.error (ar.pathOf (key), "a VCA can automate only gainDb, not '" + key + "'");
            }
            vr.warnUnknown (vcaKeys);
            c.vcas.push_back (v);
        }
    }

    if (const auto* master = r.object ("master", false))
    {
        ObjectReader mr (*master, r.pathOf ("master"), d);
        if (const auto* m = mr.object ("mixer", false))
        {
            c.master.mixer = readMixer (*m, mr.pathOf ("mixer"), d);
            c.master.mixerSpecified = true;
        }
        if (auto* fx = mr.array ("effects", false))
            c.master.effects = readEffects (*fx, mr.pathOf ("effects"), d);
        c.master.targetLufs = mr.number ("targetLufs", false, -60.0, 0.0);
        mr.warnUnknown (masterKeys);
    }

    if (const auto* render = r.object ("render", false))
    {
        ObjectReader rr (*render, r.pathOf ("render"), d);
        c.render.endBeat = rr.number ("endBeat", false, 0.0);
        c.render.tailSeconds = rr.number ("tailSeconds", false, 0.0, 60.0).value_or (2.0);
        c.render.fadeOutSeconds = rr.number ("fadeOutSeconds", false, 0.0, 60.0).value_or (0.0);
        rr.warnUnknown (renderKeys);
    }

    // 버스 이름과 센드
    std::set<std::string> busNames;
    std::vector<std::string> busList;
    for (const auto& b : c.buses)
    {
        if (b.name.empty())
            continue;
        if (b.name == "master")
            d.error (b.path + ".name", "'master' is reserved for the master bus");
        if (! busNames.insert (b.name).second)
            d.error (b.path + ".name", "duplicate bus name '" + b.name + "'");
        busList.push_back (b.name);
    }
    for (const auto& t : c.tracks)
        for (size_t i = 0; i < t.sends.size(); ++i)
            if (! t.sends[i].bus.empty() && busNames.count (t.sends[i].bus) == 0)
            {
                const auto s = closestMatch (t.sends[i].bus, busList);
                d.error (t.path + ".sends[" + std::to_string (i) + "].bus", "unknown bus '" + t.sends[i].bus + "'",
                         s.empty() ? "define it in $.buses" : "did you mean '" + s + "'?");
            }
    for (const auto& t : c.tracks)
        if (! t.output.empty() && t.output != "master" && busNames.count (t.output) == 0)
        {
            const auto s = closestMatch (t.output, busList);
            d.error (t.path + ".output", "unknown bus '" + t.output + "'", s.empty() ? "define it in $.buses, or leave output out for the master" : "did you mean '" + s + "'?");
        }

    // VCA: 있는 VCA 를 가리켜야 한다
    {
        std::vector<std::string> vcaNames;
        for (const auto& v : c.vcas)
            vcaNames.push_back (v.name);
        auto checkVca = [&] (const std::string& name, const std::string& path)
        {
            if (name.empty() || std::find (vcaNames.begin(), vcaNames.end(), name) != vcaNames.end())
                return;
            const auto s = closestMatch (name, vcaNames);
            d.error (path + ".vca", "unknown VCA '" + name + "'", s.empty() ? "define it in $.vcas" : "did you mean '" + s + "'?");
        };
        for (const auto& t : c.tracks)
            checkVca (t.vca, t.path);
        for (const auto& b : c.buses)
            checkVca (b.vca, b.path);
        resolveVcas (c);
    }

    // 사이드체인 키: 있는 트랙이어야 하고, 자기 자신이나 서로를 키로 쓰는 고리는 안 된다
    {
        std::map<std::string, size_t> trackIndex;
        std::vector<std::string> trackNames;
        for (size_t i = 0; i < c.tracks.size(); ++i)
        {
            trackIndex[c.tracks[i].name] = i;
            trackNames.push_back (c.tracks[i].name);
        }
        std::vector<std::vector<size_t>> keyedBy (c.tracks.size());   // 트랙마다 키로 쓰는 트랙
        auto check = [&] (const EffectSpec& e, const TrackSpec* owner)
        {
            if (e.type != "compressor")
                return;
            const auto v = e.params.getProperty ("sidechain", juce::var());
            if (! v.isString() || v.toString().isEmpty())
                return;
            const auto name = v.toString().toStdString();
            const auto it = trackIndex.find (name);
            if (it == trackIndex.end())
            {
                const auto s = closestMatch (name, trackNames);
                d.error (e.path + ".sidechain", "unknown track '" + name + "'", s.empty() ? "use the name of a track" : "did you mean '" + s + "'?");
                return;
            }
            if (owner != nullptr)
            {
                if (owner->name == name)
                    d.error (e.path + ".sidechain", "a track cannot key its own compressor", "use another track, such as the kick");
                else
                    keyedBy[trackIndex.at (owner->name)].push_back (it->second);
            }
        };
        for (const auto& t : c.tracks)
            for (const auto& e : t.effects)
                check (e, &t);
        for (const auto& b : c.buses)
            for (const auto& e : b.effects)
                check (e, nullptr);
        for (const auto& e : c.master.effects)
            check (e, nullptr);
        // 고리 찾기 (0 = 아직, 1 = 보는 중, 2 = 끝)
        std::vector<int> state (c.tracks.size(), 0);
        std::function<bool (size_t)> cyclic = [&] (size_t i)
        {
            if (state[i] == 1)
                return true;
            if (state[i] == 2)
                return false;
            state[i] = 1;
            for (size_t k : keyedBy[i])
                if (cyclic (k))
                    return true;
            state[i] = 2;
            return false;
        };
        for (size_t i = 0; i < c.tracks.size(); ++i)
            if (state[i] == 0 && cyclic (i))
            {
                d.error (c.tracks[i].path + ".effects", "sidechain keys form a loop through track '" + c.tracks[i].name + "'",
                         "a track that keys another cannot itself be keyed by it");
                break;
            }
    }

    // 박자 바꾸기는 앞 박자의 마디 줄에 놓인다
    {
        MeterMap so (c.beatsPerBar, c.beatUnit);
        std::vector<MeterChange> accepted;
        for (size_t i = 0; i < c.meterChanges.size(); ++i)
        {
            const auto& m = c.meterChanges[i];
            if (! so.isBarLine (m.beat))
                d.error ("$.meterChanges[" + std::to_string (i) + "].beat",
                         "a meter change must start a bar of the meter before it (beat " + formatNumber (so.floorToBar (m.beat), 3) + " or "
                             + formatNumber (so.barStart (so.barAt (m.beat) + 1), 3) + ")");
            accepted.push_back (m);
            so = MeterMap (c.beatsPerBar, c.beatUnit, accepted);
        }
    }

    // 조 바꾸기는 마디 줄에 놓인다 (악보의 조표는 마디 처음에 적는다)
    {
        const auto meters = c.meters();
        for (size_t i = 0; i < c.keyChanges.size(); ++i)
            if (! meters.isBarLine (c.keyChanges[i].beat))
                d.error ("$.keyChanges[" + std::to_string (i) + "].beat",
                         "a key change must start a bar (beat " + formatNumber (meters.floorToBar (c.keyChanges[i].beat), 3) + " or "
                             + formatNumber (meters.barStart (meters.barAt (c.keyChanges[i].beat) + 1), 3) + ")");
    }

    // 박에 맞춘 딜레이는 가장 느린 템포에서 16초까지다 (렌더를 만들 때 지연선을 그 길이로 잡는다)
    if (d.ok())
    {
        const double slowest = TempoMap (c.tempo).slowestBpm();
        auto checkDelay = [&] (const EffectSpec& e)
        {
            const auto v = e.params.getProperty ("timeBeats", juce::var());
            if (e.type != "delay" || ! json::isNumber (v))
                return;
            const double seconds = (double) v * 60.0 / slowest;
            if (seconds > 16.0)
                d.error (e.path + ".timeBeats", "the echo would be " + formatNumber (seconds, 1) + " s long at the slowest tempo (at most 16 s)",
                         "use fewer beats, or timeMs");
        };
        for (const auto& t : c.tracks)
            for (const auto& e : t.effects)
                checkDelay (e);
        for (const auto& b : c.buses)
            for (const auto& e : b.effects)
                checkDelay (e);
        for (const auto& e : c.master.effects)
            checkDelay (e);
    }

    r.warnUnknown (rootKeys);
    checkUnimplemented (c, d);

    result.partial = c;
    if (d.ok())
        result.composition = std::move (c);
    return result;
}

std::string describeJsonError (const juce::String& juceMessage)
{
    // JUCE: "<줄>:<칸>: error: <내용>"
    const auto parts = juce::StringArray::fromTokens (juceMessage, ":", "");
    if (parts.size() >= 4 && parts[0].containsOnly ("0123456789") && parts[1].containsOnly ("0123456789"))
        return "invalid JSON at line " + parts[0].toStdString() + ", column " + parts[1].toStdString() + ": "
               + juceMessage.fromFirstOccurrenceOf ("error:", false, false).trim().toStdString();
    return "invalid JSON: " + juceMessage.toStdString();
}

std::optional<KeySignature> keyAt (const Composition& c, double beat)
{
    std::optional<KeySignature> key = c.key;
    for (const auto& change : c.keyChanges)
        if (change.beat <= beat + 1.0e-9)
            key = change.key;
    return key;
}

juce::File videoFileOf (const Composition& c)
{
    if (! c.video || c.video->file.empty())
        return {};
    const auto path = juce::String::fromUTF8 (c.video->file.c_str());
    if (juce::File::isAbsolutePath (path))
        return juce::File (path);
    return c.sourceFile.getParentDirectory().getChildFile (path);
}

ParseResult loadComposition (const juce::File& file)
{
    if (! file.existsAsFile())
    {
        ParseResult r;
        r.diagnostics.error ("$", "file not found: " + file.getFullPathName().toStdString());
        return r;
    }
    return parseComposition (file.loadFileAsString(), file);
}

void resolveVcas (Composition& c)
{
    auto apply = [&] (MixerSettings& m, const std::string& name)
    {
        m.vcaDb = 0.0;
        m.vcaMute = false;
        m.vcaLane = {};
        for (const auto& v : c.vcas)
            if (! name.empty() && v.name == name)
            {
                m.vcaDb = v.gainDb;
                m.vcaMute = v.mute;
                m.vcaLane = v.gainLane;
            }
    };
    for (auto& t : c.tracks)
        apply (t.mixer, t.vca);
    for (auto& b : c.buses)
        apply (b.mixer, b.vca);
}

} // namespace mr
