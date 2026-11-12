#include "composition/Arrange.h"

#include "composition/Document.h"
#include "core/Json.h"
#include "instruments/AudioClips.h"
#include "timing/TempoMap.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace mr
{

namespace
{

// 박의 덧셈이 남기는 꼬리를 지운다 (16 + 0.1 이 16.100000000000001 이 되지 않게)
double tidy (double beat)
{
    const double r = std::round (beat * 1.0e9) / 1.0e9;
    return r == 0.0 ? 0.0 : r;
}

juce::var numberLike (const juce::var& original, double value)
{
    if ((original.isInt() || original.isInt64()) && value == std::floor (value) && std::abs (value) < 1.0e9)
        return (int) value;
    return value;
}

juce::Array<juce::var>* arrayOf (const juce::var& owner, const juce::Identifier& key)
{
    return owner.getProperty (key, juce::var()).getArray();
}

juce::DynamicObject* objectOf (const juce::var& v) { return v.getDynamicObject(); }

std::string nameOf (const juce::var& track) { return track.getProperty ("name", "").toString().toStdString(); }

std::set<std::string> trackNames (const juce::var& root)
{
    std::set<std::string> names;
    if (const auto* tracks = arrayOf (root, "tracks"))
        for (const auto& t : *tracks)
            names.insert (nameOf (t));
    return names;
}

// 압축기의 sidechain 이 name 을 가리키는 효과마다 f 를 부른다 (트랙, 버스, 마스터)
template <typename F>
void forEachSidechain (juce::var& root, const std::string& name, F&& f)
{
    auto visit = [&] (const juce::var& owner)
    {
        if (auto* effects = arrayOf (owner, "effects"))
            for (auto& e : *effects)
                if (auto* o = objectOf (e); o != nullptr && o->getProperty ("type").toString() == "compressor"
                                             && o->getProperty ("sidechain").toString().toStdString() == name)
                    f (*o);
    };
    if (auto* tracks = arrayOf (root, "tracks"))
        for (auto& t : *tracks)
            visit (t);
    if (auto* buses = arrayOf (root, "buses"))
        for (auto& b : *buses)
            visit (b);
    visit (root.getProperty ("master", juce::var()));
}

// ---- 곡선 ----

// x 바로 왼쪽에서 다가간 값
double leftValue (const AutomationLane& lane, double x)
{
    const auto& p = lane.points;
    if (p.empty())
        return 0.0;
    if (x <= p.front().beat)
        return p.front().value;
    // a: x 보다 앞의 마지막 점, b: 그다음 점 (x 에 있는 점이면 그 값으로 다가간다)
    auto it = std::lower_bound (p.begin(), p.end(), x, [] (const AutomationPoint& q, double b) { return q.beat < b; });
    const auto& a = *(it - 1);
    if (it == p.end() || a.curve == CurveShape::step)
        return a.value;
    AutomationLane segment;
    segment.points = { a, *it };
    return segment.valueAt (x);
}

// x 를 지나는 구간의 모양 (x 앞의 마지막 점의 곡선, 첫 점 앞은 step)
CurveShape curveAt (const AutomationLane& lane, double x)
{
    CurveShape c = CurveShape::step;
    for (const auto& q : lane.points)
    {
        if (q.beat >= x)
            break;
        c = q.curve;
    }
    return c;
}

// 같은 박에 같은 값이 잇달아 있으면 뒤의 것만 남긴다
void collapse (AutomationLane& lane)
{
    auto& p = lane.points;
    for (size_t i = 0; i + 1 < p.size();)
    {
        if (p[i].beat == p[i + 1].beat && p[i].value == p[i + 1].value)
            p.erase (p.begin() + (long) i);
        else
            ++i;
    }
}

// 구간을 이어 붙인 곡선에서 자르며 생긴 점(added) 가운데 소리를 바꾸지 않는 것을 지운다 (사람이 둔 점은 둔다):
// 같은 박에 셋이면 가운데, 같은 박 같은 값의 둘은 하나, 앞 점과 같은 값의 평평한 자리, 앞뒤 점을 잇는 직선 위의 linear 점,
// 마지막으로 첫 점이 다음 점과 같은 값이면 (첫 점 앞은 첫 값이다)
void simplifyJoins (AutomationLane& lane, std::vector<bool>& added)
{
    auto& p = lane.points;
    auto erase = [&] (size_t i)
    {
        p.erase (p.begin() + (long) i);
        added.erase (added.begin() + (long) i);
    };
    for (size_t i = 1; i + 1 < p.size();)
    {
        if (added[i] && p[i - 1].beat == p[i].beat && p[i].beat == p[i + 1].beat)
            erase (i);
        else
            ++i;
    }
    for (size_t i = 1; i < p.size();)
    {
        if (p[i - 1].beat == p[i].beat && p[i - 1].value == p[i].value && (added[i - 1] || added[i]))
            erase (added[i - 1] ? i - 1 : i);
        else
            ++i;
    }
    for (size_t i = 1; i < p.size();)
    {
        const auto& a = p[i - 1];
        const auto& q = p[i];
        const bool last = i + 1 == p.size();
        bool redundant = false;
        if (added[i] && a.beat != q.beat)
        {
            if (a.value == q.value)
                redundant = last || p[i + 1].value == q.value || (a.curve == CurveShape::step && q.curve == CurveShape::step);
            else if (! last && a.curve == CurveShape::linear && q.curve == CurveShape::linear && p[i + 1].beat > q.beat)
            {
                const auto& b = p[i + 1];
                const double expected = a.value + (b.value - a.value) * (q.beat - a.beat) / (b.beat - a.beat);
                redundant = std::abs (expected - q.value) <= 1.0e-9 * std::max (1.0, std::abs (q.value));
            }
        }
        if (redundant)
            erase (i);
        else
            ++i;
    }
    if (p.size() > 1 && added[0] && p[1].value == p[0].value && p[1].beat != p[0].beat)
        erase (0);
}

AutomationLane parseLane (const juce::var& points)
{
    Diagnostics ignored;
    return readAutomationLane (points, "$", -1.0e12, 1.0e12, ignored);
}

// ---- 트랙의 곡선 열쇠 ----

std::string effectKey (int index, const juce::var& effect, const juce::String& param)
{
    return "effects/" + std::to_string (index) + ":" + effect.getProperty ("type", "").toString().toStdString() + "/" + param.toStdString();
}

// owner(트랙, 버스, 마스터)의 효과 매개변수 곡선
void effectLanes (const juce::var& owner, std::map<std::string, AutomationLane>& out)
{
    const auto* effects = arrayOf (owner, "effects");
    if (effects == nullptr)
        return;
    for (int i = 0; i < effects->size(); ++i)
        if (const auto* lanes = objectOf ((*effects)[i].getProperty ("automation", juce::var())))
            for (const auto& prop : lanes->getProperties())
                if (prop.value.isArray())
                    out[effectKey (i, (*effects)[i], prop.name.toString())] = parseLane (prop.value);
}

// owner 의 mixer.automation 곡선 ("mixer/gainDb", "mixer/pan")
void mixerLanes (const juce::var& owner, std::map<std::string, AutomationLane>& out)
{
    if (const auto* lanes = objectOf (owner.getProperty ("mixer", juce::var()).getProperty ("automation", juce::var())))
        for (const auto& prop : lanes->getProperties())
            if (prop.value.isArray())
                out["mixer/" + prop.name.toString().toStdString()] = parseLane (prop.value);
}

bool setMixerLane (juce::var& owner, const std::string& key, const AutomationLane& lane)
{
    if (key.rfind ("mixer/", 0) != 0)
        return false;
    const juce::Identifier param (key.substr (6));
    auto* o = objectOf (owner);
    if (o == nullptr)
        return false;
    auto mixer = o->getProperty ("mixer");
    if (objectOf (mixer) == nullptr)
    {
        if (lane.empty())
            return true;
        mixer = juce::var (new juce::DynamicObject());
        o->setProperty ("mixer", mixer);
    }
    auto automation = objectOf (mixer)->getProperty ("automation");
    if (objectOf (automation) == nullptr)
    {
        if (lane.empty())
            return true;
        automation = juce::var (new juce::DynamicObject());
        objectOf (mixer)->setProperty ("automation", automation);
    }
    if (lane.empty())
    {
        objectOf (automation)->removeProperty (param);
        if (objectOf (automation)->getProperties().isEmpty())
            objectOf (mixer)->removeProperty ("automation");
    }
    else
        objectOf (automation)->setProperty (param, laneJson (lane));
    return true;
}

std::map<std::string, AutomationLane> trackLanes (const juce::var& track)
{
    std::map<std::string, AutomationLane> out;
    mixerLanes (track, out);
    auto addControllers = [&] (const juce::var& owner)
    {
        if (const auto* o = objectOf (owner))
            for (const auto& prop : o->getProperties())
                if (const auto id = parseControllerName (prop.name.toString().toStdString()); id && prop.value.isArray())
                    out[controllerName (*id)] = parseLane (prop.value);
    };
    addControllers (track.getProperty ("automation", juce::var()));
    addControllers (track);
    effectLanes (track, out);
    if (const auto* sends = arrayOf (track, "sends"))
        for (const auto& s : *sends)
        {
            const auto lane = s.getProperty ("automation", juce::var()).getProperty ("levelDb", juce::var());
            if (lane.isArray())
                out["sends/" + s.getProperty ("bus", "").toString().toStdString()] = parseLane (lane);
        }
    return out;
}

// 효과 매개변수 곡선을 owner 에 쓴다. 열쇠의 번호와 종류가 맞는 효과가 없으면 false
bool setEffectLane (juce::var& owner, const std::string& key, const AutomationLane& lane)
{
    // effects/<i>:<type>/<param>
    const auto colon = key.find (':'), slash = key.find ('/', 8);
    if (key.rfind ("effects/", 0) != 0 || colon == std::string::npos || slash == std::string::npos || slash < colon)
        return false;
    const int index = std::atoi (key.substr (8, colon - 8).c_str());
    const auto type = key.substr (colon + 1, slash - colon - 1);
    const juce::Identifier param (key.substr (slash + 1));
    auto* effects = arrayOf (owner, "effects");
    if (effects == nullptr || index < 0 || index >= effects->size())
        return false;
    auto* e = objectOf ((*effects)[index]);
    if (e == nullptr || e->getProperty ("type").toString().toStdString() != type)
        return false;
    auto automation = e->getProperty ("automation");
    if (objectOf (automation) == nullptr)
    {
        if (lane.empty())
            return true;
        automation = juce::var (new juce::DynamicObject());
        e->setProperty ("automation", automation);
    }
    if (lane.empty())
    {
        objectOf (automation)->removeProperty (param);
        if (objectOf (automation)->getProperties().isEmpty())
            e->removeProperty ("automation");
    }
    else
        objectOf (automation)->setProperty (param, laneJson (lane));
    return true;
}

bool setTrackLane (juce::var& track, const std::string& key, const AutomationLane& lane)
{
    if (setMixerLane (track, key, lane))
        return true;
    if (const auto id = parseControllerName (key))
    {
        track = withLane (track, *id, lane);
        return true;
    }
    if (key.rfind ("sends/", 0) == 0)
    {
        const auto bus = key.substr (6);
        if (auto* sends = arrayOf (track, "sends"))
            for (auto& s : *sends)
                if (auto* o = objectOf (s); o != nullptr && o->getProperty ("bus").toString().toStdString() == bus)
                {
                    auto automation = o->getProperty ("automation");
                    if (objectOf (automation) == nullptr)
                    {
                        if (lane.empty())
                            return true;
                        automation = juce::var (new juce::DynamicObject());
                        o->setProperty ("automation", automation);
                    }
                    if (lane.empty())
                    {
                        objectOf (automation)->removeProperty ("levelDb");
                        if (objectOf (automation)->getProperties().isEmpty())
                            o->removeProperty ("automation");
                    }
                    else
                        objectOf (automation)->setProperty ("levelDb", laneJson (lane));
                    return true;
                }
        return false;
    }
    return setEffectLane (track, key, lane);
}

// ---- 음 ----

double startOf (const juce::var& note) { return (double) note.getProperty ("startBeat", 0.0); }
double lengthOf (const juce::var& note) { return (double) note.getProperty ("durationBeat", 0.0); }

void setStart (juce::var& note, double beat)
{
    auto* o = objectOf (note);
    o->setProperty ("startBeat", numberLike (o->getProperty ("startBeat"), tidy (beat)));
}

void setLength (juce::var& note, double beats)
{
    auto* o = objectOf (note);
    o->setProperty ("durationBeat", numberLike (o->getProperty ("durationBeat"), tidy (beats)));
}

// [from, to) 를 inserted(처음이 0)로 바꾸고 뒤를 length - (to - from) 만큼 민다. 앞에서 들어오는 음은 from 에서 자른다
juce::Array<juce::var> replaceNotes (const juce::Array<juce::var>& notes, double from, double to, const juce::Array<juce::var>& inserted,
                                     double length)
{
    const double shift = length - (to - from);
    juce::Array<juce::var> before, after;
    for (const auto& n : notes)
    {
        if (objectOf (n) == nullptr)
            continue;
        auto copy = n.clone();
        const double s = startOf (n);
        if (s < from)
        {
            if (s + lengthOf (n) > from + 1.0e-9)
                setLength (copy, from - s);
            before.add (copy);
        }
        else if (s >= to)
        {
            if (shift != 0.0)
                setStart (copy, s + shift);
            after.add (copy);
        }
    }
    juce::Array<juce::var> out = before;
    std::vector<juce::var> fresh;
    for (const auto& n : inserted)
    {
        auto copy = n.clone();
        setStart (copy, startOf (n) + from);
        fresh.push_back (copy);
    }
    std::stable_sort (fresh.begin(), fresh.end(), [] (const juce::var& a, const juce::var& b) { return startOf (a) < startOf (b); });
    for (auto& n : fresh)
        out.add (n);
    out.addArray (after);
    return out;
}

void replaceTrackNotes (juce::var& track, double from, double to, const juce::Array<juce::var>& inserted, double length)
{
    const auto* notes = arrayOf (track, "notes");
    if (notes == nullptr && inserted.isEmpty())
        return;
    const auto replaced = replaceNotes (notes != nullptr ? *notes : juce::Array<juce::var>(), from, to, inserted, length);
    objectOf (track)->setProperty ("notes", replaced);
}

// ---- 오디오 클립 ----

TempoMap tempoOf (const juce::var& root)
{
    std::vector<TempoPoint> points;
    const auto tempo = root.getProperty ("tempo", juce::var());
    if (tempo.isArray())
        for (const auto& p : *tempo.getArray())
            points.push_back ({ (double) p.getProperty ("beat", 0.0), (double) p.getProperty ("bpm", 120.0), (bool) p.getProperty ("ramp", false) });
    else if (json::isNumber (tempo))
        points.push_back ({ 0.0, (double) tempo, false });
    if (points.empty())
        points.push_back ({ 0.0, 120.0, false });
    return TempoMap (points);
}

constexpr double endless = 1.0e300;

// 클립 JSON 의 소리에 쓰는 값 (늘이기 포함)
AudioClip clipSpecOf (const juce::var& clip)
{
    AudioClip spec;
    spec.file = clip.getProperty ("file", "").toString().toStdString();
    spec.startBeat = (double) clip.getProperty ("startBeat", 0.0);
    spec.offsetSeconds = (double) clip.getProperty ("offsetSeconds", 0.0);
    if (const auto length = clip.getProperty ("lengthSeconds", juce::var()); json::isNumber (length))
        spec.lengthSeconds = (double) length;
    if (const auto bpm = clip.getProperty ("sourceBpm", juce::var()); json::isNumber (bpm) && (double) bpm > 0.0)
        spec.sourceBpm = (double) bpm;
    spec.stretch = (double) clip.getProperty ("stretch", 1.0);
    if (spec.stretch <= 0.0)
        spec.stretch = 1.0;
    if (const auto* warp = clip.getProperty ("warp", juce::var()).getArray(); warp != nullptr && warp->size() >= 2)
        for (const auto& w : *warp)
            spec.warp.push_back ({ (double) w.getProperty ("seconds", 0.0), (double) w.getProperty ("beat", 0.0) });
    return spec;
}

// 클립이 소리를 내는 초 [시작, 끝). 길이를 모르면 (파일을 읽지 못하면) 끝이 없다
std::pair<double, double> clipSpan (const juce::var& clip, const TempoMap& tempo, const juce::File& folder)
{
    const auto spec = clipSpecOf (clip);
    const double start = tempo.secondsAt (spec.startBeat);
    std::optional<double> length = spec.lengthSeconds;
    if (! length && folder != juce::File())
        if (const auto info = audioFileInfo (clipFile (spec, folder)))
            length = std::max (0.0, info->seconds() - spec.offsetSeconds);
    if (! length)
        return { start, endless };
    return { start, clipSongSecondsAt (spec, tempo, spec.offsetSeconds + *length) };
}

// 클립의 박 [fromBeat, toBeat) 안의 부분 (없으면 void). 잘린 쪽의 페이드는 지운다. 파일 안의 자리는 늘이기를 따라 옮긴다
juce::var clipPart (const juce::var& clip, const TempoMap& tempo, const juce::File& folder, double fromBeat, double toBeat)
{
    const auto [cs, ce] = clipSpan (clip, tempo, folder);
    const double fs = fromBeat <= -endless ? -endless : tempo.secondsAt (std::max (0.0, fromBeat));
    const double ts = toBeat >= endless ? endless : tempo.secondsAt (toBeat);
    const double s0 = std::max (cs, fs), s1 = std::min (ce, ts);
    if (s1 <= s0 + 1.0e-6)
        return {};
    if (s0 == cs && s1 == ce)
        return clip.clone();   // 구간 안에 다 들어온다
    const auto spec = clipSpecOf (clip);
    auto copy = clip.clone();
    auto* o = objectOf (copy);
    double offset = spec.offsetSeconds;
    if (s0 > cs)
    {
        offset = clipSourceAt (spec, tempo, s0);
        o->setProperty ("startBeat", tidy (tempo.beatAt (s0)));
        o->setProperty ("offsetSeconds", tidy (offset));
        o->removeProperty ("fadeInSeconds");
        o->removeProperty ("fadeInShape");
    }
    if (s1 < endless)
    {
        if (s1 < ce)
        {
            o->removeProperty ("fadeOutSeconds");
            o->removeProperty ("fadeOutShape");
        }
        if (s1 < ce || o->hasProperty ("lengthSeconds"))
            o->setProperty ("lengthSeconds", tidy (clipSourceAt (spec, tempo, s1) - offset));
    }
    return copy;
}

// [from, to) 박의 클립을 inserted(처음이 0 박)로 바꾸고 뒤를 length - (to - from) 만큼 민다 (클립은 경계에서 나눈다)
void replaceTrackClips (juce::var& track, const TempoMap& tempo, const juce::File& folder, double from, double to,
                        const juce::Array<juce::var>& inserted, double length)
{
    const auto* clips = arrayOf (track, "clips");
    if (clips == nullptr && inserted.isEmpty())
        return;
    const double shift = length - (to - from);
    juce::Array<juce::var> out;
    if (clips != nullptr)
        for (const auto& c : *clips)
        {
            if (objectOf (c) == nullptr)
                continue;
            if (auto head = clipPart (c, tempo, folder, -endless, from); ! head.isVoid())
                out.add (head);
            if (auto tail = clipPart (c, tempo, folder, to, endless); ! tail.isVoid())
            {
                objectOf (tail)->setProperty ("startBeat", tidy ((double) tail.getProperty ("startBeat", 0.0) + shift));
                out.add (tail);
            }
        }
    for (const auto& c : inserted)
    {
        auto copy = c.clone();
        objectOf (copy)->setProperty ("startBeat", tidy ((double) c.getProperty ("startBeat", 0.0) + from));
        out.add (copy);
    }
    std::stable_sort (out.begin(), out.end(), [] (const juce::var& a, const juce::var& b)
                      { return (double) a.getProperty ("startBeat", 0.0) < (double) b.getProperty ("startBeat", 0.0); });
    objectOf (track)->setProperty ("clips", out);
}

// owner 의 모든 효과 곡선과 믹서 곡선에 f 를 쓴다 (버스와 마스터)
template <typename F>
void mapEffectLanes (juce::var& owner, F&& f)
{
    std::map<std::string, AutomationLane> lanes;
    effectLanes (owner, lanes);
    for (const auto& [key, lane] : lanes)
        setEffectLane (owner, key, f (lane));
    lanes.clear();
    mixerLanes (owner, lanes);
    for (const auto& [key, lane] : lanes)
        setMixerLane (owner, key, f (lane));
}

// 곡 전체의 시간 편집: [from, to) 를 길이 length 의 빈자리로 바꾼다 (넣기는 from == to, 지우기는 length == 0)
juce::var withTimeReplaced (const juce::var& root, double from, double to, double length, const juce::File& folder)
{
    auto out = root.clone();
    const double shift = length - (to - from);
    const auto tempoBefore = tempoOf (root);
    auto laneEdit = [&] (const AutomationLane& lane)
    {
        if (lane.empty() || lane.points.back().beat < from)
            return lane;   // 바뀌는 자리 뒤에 점이 없다
        AutomationLane hold;
        if (length > 0.0)
            hold.points.push_back ({ 0.0, leftValue (lane, from), CurveShape::step });
        return replaceLaneSpan (lane, from, to, hold, length);
    };

    if (auto* tracks = arrayOf (out, "tracks"))
        for (auto& t : *tracks)
        {
            replaceTrackNotes (t, from, to, {}, length);
            replaceTrackClips (t, tempoBefore, folder, from, to, {}, length);
            for (const auto& [key, lane] : trackLanes (t))
                setTrackLane (t, key, laneEdit (lane));
        }
    if (auto* buses = arrayOf (out, "buses"))
        for (auto& b : *buses)
            mapEffectLanes (b, laneEdit);
    if (auto master = out.getProperty ("master", juce::var()); objectOf (master) != nullptr)
        mapEffectLanes (master, laneEdit);

    // 템포: ramp 는 linear, 아니면 step 인 곡선으로 고치고 되돌린다. 같은 박의 점은 뒤의 것을, 첫 점은 0 박에
    {
        const auto tempo = out.getProperty ("tempo", juce::var());
        if (tempo.isArray())
        {
            AutomationLane lane;
            for (const auto& p : *tempo.getArray())
                lane.points.push_back ({ (double) p.getProperty ("beat", 0.0), (double) p.getProperty ("bpm", 120.0),
                                         (bool) p.getProperty ("ramp", false) ? CurveShape::linear : CurveShape::step });
            const auto edited = laneEdit (lane);
            std::vector<TempoPoint> points;
            for (const auto& p : edited.points)
            {
                if (! points.empty() && points.back().beat == p.beat)
                    points.pop_back();
                points.push_back ({ p.beat, p.value, p.curve == CurveShape::linear });
            }
            if (! points.empty() && points.front().beat > 0.0)
                points.insert (points.begin(), { 0.0, points.front().bpm, false });
            if (edited.points.size() != lane.points.size()
                || ! std::equal (edited.points.begin(), edited.points.end(), lane.points.begin(), [] (const AutomationPoint& a, const AutomationPoint& b)
                                 { return a.beat == b.beat && a.value == b.value && a.curve == b.curve; }))
                objectOf (out)->setProperty ("tempo", tempoJson (points));
        }
    }

    // 박 하나의 자리 (start: 그 박의 내용이 밀리면 함께, end: 그 박 앞까지의 내용을 따라)
    auto mapStart = [&] (double x) -> double
    {
        if (x < from)
            return x;
        if (x < to)
            return from;
        return tidy (x + shift);
    };
    auto mapEnd = [&] (double x) -> double
    {
        if (x <= from)
            return x;
        if (x < to)
            return from + std::min (x - from, length);
        return tidy (x + shift);
    };

    if (auto* markers = arrayOf (out, "markers"))
    {
        juce::Array<juce::var> kept;
        for (const auto& m : *markers)
        {
            const double b = (double) m.getProperty ("beat", 0.0);
            if (b >= from && b < to)
                continue;
            auto copy = m.clone();
            if (b >= to && shift != 0.0)
                objectOf (copy)->setProperty ("beat", numberLike (m.getProperty ("beat", 0.0), tidy (b + shift)));
            kept.add (copy);
        }
        if (kept.isEmpty())
            objectOf (out)->removeProperty ("markers");
        else
            objectOf (out)->setProperty ("markers", kept);
    }

    // 박자 바꾸기: 뒤의 것은 밀고, 지운 자리의 마지막 것은 지운 자리의 처음으로 (뒤의 음악이 제 박자를 지킨다)
    if (auto* meters = arrayOf (out, "meterChanges"))
    {
        juce::Array<juce::var> kept;
        juce::var inside;
        for (const auto& m : *meters)
        {
            const double b = (double) m.getProperty ("beat", 0.0);
            if (b >= from && b < to)
            {
                inside = m;   // 마지막 것
                continue;
            }
            auto copy = m.clone();
            if (b >= to && shift != 0.0)
                objectOf (copy)->setProperty ("beat", numberLike (m.getProperty ("beat", 0.0), tidy (b + shift)));
            kept.add (copy);
        }
        if (! inside.isVoid() && from > 0.0)
        {
            auto moved = inside.clone();
            objectOf (moved)->setProperty ("beat", numberLike (inside.getProperty ("beat", 0.0), tidy (from)));
            kept.add (moved);
        }
        else if (! inside.isVoid())
            objectOf (out)->setProperty ("timeSignature", inside.getProperty ("timeSignature", juce::var()).clone());   // 곡의 처음 박자가 된다
        std::stable_sort (kept.begin(), kept.end(), [] (const juce::var& a, const juce::var& b)
                          { return (double) a.getProperty ("beat", 0.0) < (double) b.getProperty ("beat", 0.0); });
        if (kept.isEmpty())
            objectOf (out)->removeProperty ("meterChanges");
        else
            objectOf (out)->setProperty ("meterChanges", kept);
    }

    // 조 바꾸기: 박자 바꾸기와 같다 (처음에서 지우면 곡의 처음 조가 된다)
    if (auto* keys = arrayOf (out, "keyChanges"))
    {
        juce::Array<juce::var> kept;
        juce::var inside;
        for (const auto& k : *keys)
        {
            const double b = (double) k.getProperty ("beat", 0.0);
            if (b >= from && b < to)
            {
                inside = k;
                continue;
            }
            auto copy = k.clone();
            if (b >= to && shift != 0.0)
                objectOf (copy)->setProperty ("beat", numberLike (k.getProperty ("beat", 0.0), tidy (b + shift)));
            kept.add (copy);
        }
        if (! inside.isVoid() && from > 0.0)
        {
            auto moved = inside.clone();
            objectOf (moved)->setProperty ("beat", numberLike (inside.getProperty ("beat", 0.0), tidy (from)));
            kept.add (moved);
        }
        else if (! inside.isVoid())
            objectOf (out)->setProperty ("key", inside.getProperty ("key", juce::var()));
        std::stable_sort (kept.begin(), kept.end(), [] (const juce::var& a, const juce::var& b)
                          { return (double) a.getProperty ("beat", 0.0) < (double) b.getProperty ("beat", 0.0); });
        if (kept.isEmpty())
            objectOf (out)->removeProperty ("keyChanges");
        else
            objectOf (out)->setProperty ("keyChanges", kept);
    }

    if (auto* metadata = objectOf (out.getProperty ("metadata", juce::var())))
        if (auto* loop = objectOf (metadata->getProperty ("loop")))
        {
            const auto s = loop->getProperty ("startBeat"), e = loop->getProperty ("endBeat");
            if (json::isNumber (s) && json::isNumber (e))
            {
                const double ns = mapStart ((double) s), ne = mapEnd ((double) e);
                if (ne > ns)
                {
                    loop->setProperty ("startBeat", numberLike (s, ns));
                    loop->setProperty ("endBeat", numberLike (e, ne));
                }
                else
                    metadata->removeProperty ("loop");
            }
        }

    // 곡의 끝 박: 넣기는 그 박 뒤의 끝을 민다 (끝에 넣으면 곡이 길어진다)
    if (auto* render = objectOf (out.getProperty ("render", juce::var())))
        if (const auto e = render->getProperty ("endBeat"); json::isNumber (e))
        {
            const double x = (double) e;
            const double moved = from == to ? (x >= from ? tidy (x + length) : x) : mapEnd (x);
            render->setProperty ("endBeat", numberLike (e, moved));
        }
    return out;
}

juce::var sortedMarkers (juce::Array<juce::var> markers)
{
    std::stable_sort (markers.begin(), markers.end(),
                      [] (const juce::var& a, const juce::var& b) { return (double) a.getProperty ("beat", 0.0) < (double) b.getProperty ("beat", 0.0); });
    return markers.isEmpty() ? juce::var() : juce::var (markers);
}

} // namespace

// ---------------------------------------------------------------------------
// 트랙 목록
// ---------------------------------------------------------------------------

std::string uniqueTrackName (const juce::var& root, const std::string& base)
{
    const auto names = trackNames (root);
    if (! base.empty() && names.count (base) == 0)
        return base;
    // 끝의 " 숫자" 를 뗀다
    std::string stem = base.empty() ? std::string ("Track") : base;
    if (const auto space = stem.find_last_of (' '); space != std::string::npos && space + 1 < stem.size()
        && std::all_of (stem.begin() + (long) space + 1, stem.end(), [] (char c) { return c >= '0' && c <= '9'; }))
        stem = stem.substr (0, space);
    if (names.count (stem) == 0)
        return stem;
    for (int n = 2;; ++n)
    {
        const auto candidate = stem + " " + std::to_string (n);
        if (names.count (candidate) == 0)
            return candidate;
    }
}

juce::var newTrackJson (const std::string& name, const std::string& instrument)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", juce::String::fromUTF8 (name.c_str()));
    o->setProperty ("instrument", juce::String::fromUTF8 (instrument.c_str()));
    o->setProperty ("notes", juce::Array<juce::var>());
    return juce::var (o);
}

juce::var withTrackInserted (const juce::var& root, size_t index, const juce::var& track)
{
    auto out = root.clone();
    auto* tracks = arrayOf (out, "tracks");
    if (tracks == nullptr)
    {
        objectOf (out)->setProperty ("tracks", juce::Array<juce::var>());
        tracks = arrayOf (out, "tracks");
    }
    auto copy = track.clone();
    if (auto* o = objectOf (copy))
        o->setProperty ("name", juce::String::fromUTF8 (uniqueTrackName (out, nameOf (copy)).c_str()));
    tracks->insert ((int) std::min (index, (size_t) tracks->size()), copy);
    return out;
}

juce::var withTrackRemoved (const juce::var& root, size_t index, int* clearedSidechains)
{
    auto out = root.clone();
    auto* tracks = arrayOf (out, "tracks");
    int cleared = 0;
    if (tracks != nullptr && (int) index < tracks->size())
    {
        const auto name = nameOf ((*tracks)[(int) index]);
        tracks->remove ((int) index);
        forEachSidechain (out, name, [&cleared] (juce::DynamicObject& e)
        {
            e.removeProperty ("sidechain");
            ++cleared;
        });
    }
    if (clearedSidechains != nullptr)
        *clearedSidechains = cleared;
    return out;
}

juce::var withTrackDuplicated (const juce::var& root, size_t index)
{
    const auto* tracks = arrayOf (root, "tracks");
    if (tracks == nullptr || (int) index >= tracks->size())
        return root.clone();
    return withTrackInserted (root, index + 1, (*tracks)[(int) index]);
}

juce::var withTrackRenamed (const juce::var& root, size_t index, const std::string& name)
{
    auto out = root.clone();
    auto* tracks = arrayOf (out, "tracks");
    if (tracks == nullptr || (int) index >= tracks->size() || name.empty())
        return out;
    const auto old = nameOf ((*tracks)[(int) index]);
    if (old == name)
        return out;
    objectOf ((*tracks)[(int) index])->setProperty ("name", juce::String::fromUTF8 (name.c_str()));
    forEachSidechain (out, old, [&name] (juce::DynamicObject& e) { e.setProperty ("sidechain", juce::String::fromUTF8 (name.c_str())); });
    return out;
}

juce::var withTrackMoved (const juce::var& root, size_t from, size_t to)
{
    auto out = root.clone();
    auto* tracks = arrayOf (out, "tracks");
    if (tracks != nullptr && (int) from < tracks->size() && (int) to < tracks->size())
        tracks->move ((int) from, (int) to);
    return out;
}

juce::var withTrackColour (const juce::var& root, size_t index, const std::string& colour)
{
    auto out = root.clone();
    auto* tracks = arrayOf (out, "tracks");
    if (tracks == nullptr || (int) index >= tracks->size())
        return out;
    auto* t = objectOf ((*tracks)[(int) index]);
    if (colour.empty())
        t->removeProperty ("color");
    else
        t->setProperty ("color", juce::String (colour));
    return out;
}

// ---------------------------------------------------------------------------
// 마커
// ---------------------------------------------------------------------------

namespace
{

juce::Array<juce::var> markerArray (const juce::var& root)
{
    const auto copy = root.getProperty ("markers", juce::var()).clone();
    if (const auto* arr = copy.getArray())
        return *arr;
    return {};
}

size_t indexOfObject (const juce::var& markers, const juce::DynamicObject* target)
{
    if (const auto* arr = markers.getArray())
        for (int i = 0; i < arr->size(); ++i)
            if (objectOf ((*arr)[i]) == target)
                return (size_t) i;
    return 0;
}

} // namespace

juce::var withMarkerAdded (const juce::var& root, double beat, const std::string& name, size_t* index)
{
    auto markers = markerArray (root);
    auto* o = new juce::DynamicObject();
    o->setProperty ("beat", tidy (std::max (0.0, beat)));
    o->setProperty ("name", juce::String::fromUTF8 (name.c_str()));
    markers.add (juce::var (o));
    const auto sorted = sortedMarkers (markers);
    if (index != nullptr)
        *index = indexOfObject (sorted, o);
    return sorted;
}

juce::var withMarkerMoved (const juce::var& root, size_t marker, double beat, size_t* index)
{
    auto markers = markerArray (root);
    if ((int) marker >= markers.size())
        return sortedMarkers (markers);
    auto* o = objectOf (markers[(int) marker]);
    o->setProperty ("beat", numberLike (o->getProperty ("beat"), tidy (std::max (0.0, beat))));
    const auto sorted = sortedMarkers (markers);
    if (index != nullptr)
        *index = indexOfObject (sorted, o);
    return sorted;
}

juce::var withMarkerRenamed (const juce::var& root, size_t marker, const std::string& name)
{
    auto markers = markerArray (root);
    if ((int) marker < markers.size())
        objectOf (markers[(int) marker])->setProperty ("name", juce::String::fromUTF8 (name.c_str()));
    return sortedMarkers (markers);
}

juce::var withMarkerRemoved (const juce::var& root, size_t marker)
{
    auto markers = markerArray (root);
    if ((int) marker < markers.size())
        markers.remove ((int) marker);
    return sortedMarkers (markers);
}

// ---------------------------------------------------------------------------
// 곡선의 시간 편집
// ---------------------------------------------------------------------------

AutomationLane sliceLane (const AutomationLane& lane, double from, double to)
{
    AutomationLane out;
    if (lane.empty() || to <= from)
        return out;
    const auto& p = lane.points;
    const bool pointAtFrom = std::any_of (p.begin(), p.end(), [from] (const AutomationPoint& q) { return q.beat == from; });
    if (! pointAtFrom)
        out.points.push_back ({ 0.0, lane.valueAt (from), curveAt (lane, from) });
    for (const auto& q : p)
        if (q.beat >= from && q.beat < to)
            out.points.push_back ({ tidy (q.beat - from), q.value, q.curve });
    out.points.push_back ({ tidy (to - from), leftValue (lane, to), CurveShape::linear });
    collapse (out);
    return out;
}

AutomationLane replaceLaneSpan (const AutomationLane& lane, double from, double to, const AutomationLane& content, double length)
{
    if (lane.empty())
    {
        AutomationLane out;
        for (const auto& q : content.points)
            out.points.push_back ({ tidy (q.beat + from), q.value, q.curve });
        return out;
    }
    const auto& p = lane.points;
    const bool anyAtOrAfter = p.back().beat >= from;
    if (content.empty() && ! anyAtOrAfter)
        return lane;

    const double shift = length - (to - from);
    const double end = from + length;
    const double vL = leftValue (lane, from), vR = lane.valueAt (to);
    const CurveShape cR = curveAt (lane, to);

    AutomationLane out;
    for (const auto& q : p)
        if (q.beat < from)
            out.points.push_back (q);
    // 왼쪽 닫기: 구간 앞의 곡선이 원래 값으로 from 에 닿는다
    if (content.empty() || content.points.front().beat > 0.0 || content.points.front().value != vL)
    {
        CurveShape c = CurveShape::step;
        if (content.empty())
            c = from > p.front().beat ? curveAt (lane, from) : CurveShape::step;
        out.points.push_back ({ tidy (from), vL, c });
    }
    for (const auto& q : content.points)
        if (q.beat <= length)
            out.points.push_back ({ tidy (q.beat + from), q.value, q.curve });
    std::vector<AutomationPoint> after;
    for (const auto& q : p)
        if (q.beat >= to)
            after.push_back ({ tidy (q.beat + shift), q.value, q.curve });
    // 오른쪽 닫기: 뒤의 곡선이 원래 값에서 이어진다 (to 에 점이 있었으면 그 점이 한다)
    if (after.empty() || after.front().beat > end)
        out.points.push_back ({ tidy (end), vR, cR });
    out.points.insert (out.points.end(), after.begin(), after.end());
    collapse (out);
    return out;
}

// ---------------------------------------------------------------------------
// 구간
// ---------------------------------------------------------------------------

size_t ArrangeClip::noteCount() const noexcept
{
    size_t n = 0;
    for (const auto& t : tracks)
        n += (size_t) t.notes.size();
    return n;
}

ArrangeClip copyRange (const juce::var& root, const std::vector<size_t>& tracks, double from, double to, const juce::File& songFolder)
{
    const auto tempo = tempoOf (root);
    ArrangeClip clip;
    clip.lengthBeats = std::max (0.0, to - from);
    const auto* all = arrayOf (root, "tracks");
    if (all == nullptr || to <= from)
        return clip;
    for (const size_t index : tracks)
    {
        if ((int) index >= all->size())
            continue;
        const auto& track = (*all)[(int) index];
        ClipTrack ct;
        if (const auto* notes = arrayOf (track, "notes"))
            for (const auto& n : *notes)
            {
                const double s = startOf (n);
                if (objectOf (n) == nullptr || s < from || s >= to)
                    continue;
                auto copy = n.clone();
                setStart (copy, s - from);
                if (s + lengthOf (n) > to)
                    setLength (copy, to - s);
                ct.notes.add (copy);
            }
        for (const auto& [key, lane] : trackLanes (track))
            if (auto slice = sliceLane (lane, from, to); ! slice.empty())
                ct.lanes[key] = std::move (slice);
        if (const auto* clips = arrayOf (track, "clips"))
            for (const auto& c : *clips)
                if (auto part = clipPart (c, tempo, songFolder, from, to); ! part.isVoid())
                {
                    objectOf (part)->setProperty ("startBeat", tidy ((double) part.getProperty ("startBeat", 0.0) - from));
                    ct.clips.add (part);
                }
        clip.tracks.push_back (std::move (ct));
    }
    return clip;
}

juce::var withRangeCleared (const juce::var& root, const std::vector<size_t>& tracks, double from, double to, const juce::File& songFolder)
{
    const auto tempo = tempoOf (root);
    auto out = root.clone();
    auto* all = arrayOf (out, "tracks");
    if (all == nullptr || to <= from)
        return out;
    for (const size_t index : tracks)
    {
        if ((int) index >= all->size())
            continue;
        auto& track = all->getReference ((int) index);
        replaceTrackNotes (track, from, to, {}, to - from);
        replaceTrackClips (track, tempo, songFolder, from, to, {}, to - from);
        for (const auto& [key, lane] : trackLanes (track))
            setTrackLane (track, key, replaceLaneSpan (lane, from, to, {}, to - from));
    }
    return out;
}

juce::var withClipPasted (const juce::var& root, const ArrangeClip& clip, size_t firstTrack, double at, const juce::File& songFolder)
{
    const auto tempo = tempoOf (root);
    auto out = root.clone();
    auto* all = arrayOf (out, "tracks");
    if (all == nullptr || clip.empty())
        return out;
    const double length = clip.lengthBeats;
    for (size_t k = 0; k < clip.tracks.size(); ++k)
    {
        const size_t index = firstTrack + k;
        if ((int) index >= all->size())
            break;
        auto& track = all->getReference ((int) index);
        const auto& ct = clip.tracks[k];
        replaceTrackNotes (track, at, at + length, ct.notes, length);
        replaceTrackClips (track, tempo, songFolder, at, at + length, ct.clips, length);
        const auto lanes = trackLanes (track);
        for (const auto& [key, content] : ct.lanes)
        {
            const auto it = lanes.find (key);
            const AutomationLane existing = it != lanes.end() ? it->second : AutomationLane();
            setTrackLane (track, key, replaceLaneSpan (existing, at, at + length, content, length));
        }
    }
    return out;
}

juce::var withTimeInserted (const juce::var& root, double at, double length, const juce::File& songFolder)
{
    if (length <= 0.0)
        return root.clone();
    return withTimeReplaced (root, std::max (0.0, at), std::max (0.0, at), length, songFolder);
}

juce::var withTimeDeleted (const juce::var& root, double from, double to, const juce::File& songFolder)
{
    from = std::max (0.0, from);
    if (to <= from)
        return root.clone();
    return withTimeReplaced (root, from, to, 0.0, songFolder);
}

// ---------------------------------------------------------------------------
// 구간 (마커에서 다음 마커까지)
// ---------------------------------------------------------------------------

namespace
{

MeterMap metersOfRoot (const juce::var& root)
{
    int num = 4, den = 4;
    if (const auto* ts = arrayOf (root, "timeSignature"); ts != nullptr && ts->size() == 2)
    {
        num = (int) (*ts)[0];
        den = (int) (*ts)[1];
    }
    std::vector<MeterChange> changes;
    if (const auto* list = arrayOf (root, "meterChanges"))
        for (const auto& m : *list)
            if (const auto* ts = arrayOf (m, "timeSignature"); ts != nullptr && ts->size() == 2)
                changes.push_back ({ (double) m.getProperty ("beat", 0.0), (int) (*ts)[0], (int) (*ts)[1] });
    return MeterMap (num, den, changes);
}

// 템포 점을 곡선으로 (ramp 는 linear, 아니면 step)
AutomationLane tempoLaneOf (const juce::var& root)
{
    AutomationLane lane;
    const auto tempo = root.getProperty ("tempo", juce::var());
    if (tempo.isArray())
        for (const auto& p : *tempo.getArray())
            lane.points.push_back ({ (double) p.getProperty ("beat", 0.0), (double) p.getProperty ("bpm", 120.0),
                                     (bool) p.getProperty ("ramp", false) ? CurveShape::linear : CurveShape::step });
    else if (json::isNumber (tempo))
        lane.points.push_back ({ 0.0, (double) tempo, CurveShape::step });
    return lane;
}

// 원래 곡의 x 가 든 구간 (start: s <= x < e, 끝: s < x <= e) 의 새 자리. 없으면 nullopt
std::optional<double> mappedBeat (const std::vector<std::pair<double, double>>& spans, const std::vector<double>& at, double x, bool asEnd)
{
    for (size_t k = 0; k < spans.size(); ++k)
    {
        const auto [s, e] = spans[k];
        if (asEnd ? (x > s && x <= e) : (x >= s && x < e))
            return tidy (at[k] + (x - s));
    }
    return std::nullopt;
}

} // namespace

double songEndBeat (const juce::var& root, const juce::File& folder)
{
    if (const auto e = root.getProperty ("render", juce::var()).getProperty ("endBeat", juce::var()); json::isNumber (e))
        return (double) e;
    const auto tempo = tempoOf (root);
    double end = 0.0;
    if (const auto* tracks = arrayOf (root, "tracks"))
        for (const auto& t : *tracks)
        {
            if (const auto* notes = arrayOf (t, "notes"))
                for (const auto& n : *notes)
                    end = std::max (end, startOf (n) + lengthOf (n));
            if (const auto* clips = arrayOf (t, "clips"))
                for (const auto& c : *clips)
                    if (const auto span = clipSpan (c, tempo, folder); span.second < endless)
                        end = std::max (end, tempo.beatAt (span.second));
        }
    const auto meters = metersOfRoot (root);
    return meters.isBarLine (end) ? end : meters.barStart (meters.barAt (end) + 1);
}

std::vector<Section> sectionsOf (const juce::var& root, const juce::File& folder)
{
    std::vector<Section> out;
    const auto* markers = arrayOf (root, "markers");
    if (markers == nullptr)
        return out;
    const double end = songEndBeat (root, folder);
    for (int i = 0; i < markers->size(); ++i)
    {
        const double b = (double) (*markers)[i].getProperty ("beat", 0.0);
        const double next = i + 1 < markers->size() ? (double) (*markers)[i + 1].getProperty ("beat", 0.0) : end;
        if (next <= b)
            continue;
        out.push_back ({ (*markers)[i].getProperty ("name", "").toString().toStdString(), b, next, (size_t) i });
    }
    return out;
}

juce::var withSpansArranged (const juce::var& root, const std::vector<std::pair<double, double>>& spans, double tailFrom, const juce::File& folder)
{
    constexpr double tailLength = 1.0e6;
    auto all = spans;
    all.push_back ({ tailFrom, tailFrom + tailLength });
    std::vector<double> at;
    double position = 0.0;
    for (const auto& [s, e] : all)
    {
        at.push_back (tidy (position));
        position += e - s;
    }
    auto out = root.clone();

    // 곡선을 구간마다 잘라 이어 붙인다 (이음매는 같은 박의 두 점, 소리를 바꾸지 않는 점은 지운다). 원래와 같으면 원래 곡선
    auto arrangeLane = [&] (const AutomationLane& lane)
    {
        if (lane.empty())
            return lane;
        AutomationLane joined;
        std::vector<bool> added;   // 자르며 생긴 점 (구간 처음의 값과 끝의 값)
        for (size_t k = 0; k < all.size(); ++k)
        {
            const double from = all[k].first;
            auto slice = sliceLane (lane, from, all[k].second).points;
            if (slice.empty())
                continue;
            const bool startAdded = std::none_of (lane.points.begin(), lane.points.end(), [from] (const AutomationPoint& q) { return q.beat == from; });
            // 조각의 첫 박에 점이 여럿이면 (앞의 이음매) 그 뒤의 값을 정하는 마지막 것만
            while (slice.size() > 1 && slice[0].beat == 0.0 && slice[1].beat == 0.0)
                slice.erase (slice.begin());
            slice.back().curve = CurveShape::step;   // 끝을 알리는 점 (뒤에 오는 구간의 첫 점이 이어받는다)
            const bool last = k + 1 == all.size();
            for (size_t i = 0; i < slice.size(); ++i)
            {
                if (last && i + 1 == slice.size() && slice.size() > 1)
                    break;   // 곡의 끝 뒤는 마지막 값이 이어진다
                joined.points.push_back ({ tidy (slice[i].beat + at[k]), slice[i].value, slice[i].curve });
                added.push_back ((i == 0 && startAdded) || i + 1 == slice.size());
            }
        }
        simplifyJoins (joined, added);
        if (joined.points.size() == lane.points.size()
            && std::equal (joined.points.begin(), joined.points.end(), lane.points.begin(), [] (const AutomationPoint& a, const AutomationPoint& b)
                           { return a.beat == b.beat && a.value == b.value && a.curve == b.curve; }))
            return lane;
        return joined;
    };
    auto sameLane = [] (const AutomationLane& a, const AutomationLane& b)
    {
        return a.points.size() == b.points.size()
               && std::equal (a.points.begin(), a.points.end(), b.points.begin(), [] (const AutomationPoint& x, const AutomationPoint& y)
                              { return x.beat == y.beat && x.value == y.value && x.curve == y.curve; });
    };
    // 버스와 마스터: 바뀐 곡선만 쓴다 (JSON 의 숫자 모양을 지킨다)
    auto arrangeOwnerLanes = [&] (juce::var& owner)
    {
        std::map<std::string, AutomationLane> lanes;
        effectLanes (owner, lanes);
        for (const auto& [key, lane] : lanes)
            if (const auto arranged = arrangeLane (lane); ! sameLane (arranged, lane))
                setEffectLane (owner, key, arranged);
        lanes.clear();
        mixerLanes (owner, lanes);
        for (const auto& [key, lane] : lanes)
            if (const auto arranged = arrangeLane (lane); ! sameLane (arranged, lane))
                setMixerLane (owner, key, arranged);
    };

    // 템포 (먼저: 클립을 붙일 때 새 자리의 템포를 쓴다). 같은 박의 두 점은 뒤의 것을, 앞 점으로 다가가는 ramp 가 다른 값에 닿으면
    // 뒤의 것을 아주 조금 뒤로
    if (const auto tempoLane = tempoLaneOf (root); ! tempoLane.empty())
    {
        const auto joined = arrangeLane (tempoLane);
        std::vector<TempoPoint> points;
        for (const auto& p : joined.points)
        {
            double beat = p.beat;
            if (! points.empty() && points.back().beat >= beat)
            {
                const bool rampInto = points.size() >= 2 && points[points.size() - 2].ramp && points.back().bpm != p.value;
                if (rampInto)
                    beat = tidy (points.back().beat + 1.0e-6);
                else
                    points.pop_back();
            }
            points.push_back ({ beat, p.value, p.curve == CurveShape::linear });
        }
        // 이어지는 같은 템포의 계단은 하나로
        std::vector<TempoPoint> kept;
        for (const auto& p : points)
            if (kept.empty() || p.ramp || kept.back().ramp || kept.back().bpm != p.bpm)
                kept.push_back (p);
        if (! kept.empty() && kept.front().beat > 0.0)
            kept.insert (kept.begin(), { 0.0, kept.front().bpm, false });
        // 원래와 같으면 원래 JSON 을 둔다
        bool same = kept.size() == tempoLane.points.size();
        for (size_t i = 0; same && i < kept.size(); ++i)
            same = kept[i].beat == tempoLane.points[i].beat && kept[i].bpm == tempoLane.points[i].value
                   && kept[i].ramp == (tempoLane.points[i].curve == CurveShape::linear);
        if (! same)
            objectOf (out)->setProperty ("tempo", tempoJson (kept));
    }

    // 트랙: 음과 클립을 비우고 구간마다 원래 곡에서 잘라 그 자리에 붙인다. 곡선은 이어 붙인다
    if (const auto* tracks = arrayOf (root, "tracks"))
    {
        std::vector<size_t> every;
        for (int i = 0; i < tracks->size(); ++i)
            every.push_back ((size_t) i);
        if (auto* outTracks = arrayOf (out, "tracks"))
            for (auto& t : *outTracks)
                for (const char* key : { "notes", "clips" })
                    if (t.getProperty (key, juce::var()).isArray())
                        objectOf (t)->setProperty (key, juce::Array<juce::var>());
        for (size_t k = 0; k < all.size(); ++k)
        {
            auto clip = copyRange (root, every, all[k].first, all[k].second, folder);
            for (auto& ct : clip.tracks)
                ct.lanes.clear();
            out = withClipPasted (out, clip, 0, at[k], folder);
        }
        if (auto* outTracks = arrayOf (out, "tracks"))
            for (int i = 0; i < tracks->size() && i < outTracks->size(); ++i)
                for (const auto& [key, lane] : trackLanes ((*tracks)[i]))
                    if (const auto arranged = arrangeLane (lane); ! sameLane (arranged, lane))
                        setTrackLane (outTracks->getReference (i), key, arranged);
    }
    if (auto* buses = arrayOf (out, "buses"))
        for (auto& b : *buses)
            arrangeOwnerLanes (b);   // out 의 버스 곡선은 아직 원래 곡의 것이다
    if (auto master = out.getProperty ("master", juce::var()); objectOf (master) != nullptr)
        arrangeOwnerLanes (master);

    // 마커: 들어 있는 구간을 따라간다 (복제한 구간의 마커는 둘이 된다)
    {
        juce::Array<juce::var> moved;
        if (const auto* markers = arrayOf (root, "markers"))
            for (size_t k = 0; k < all.size(); ++k)
                for (const auto& m : *markers)
                {
                    const double b = (double) m.getProperty ("beat", 0.0);
                    if (b >= all[k].first && b < all[k].second)
                    {
                        auto copy = m.clone();
                        objectOf (copy)->setProperty ("beat", numberLike (m.getProperty ("beat", 0.0), tidy (at[k] + b - all[k].first)));
                        moved.add (copy);
                    }
                }
        const auto sorted = sortedMarkers (moved);
        if (sorted.isVoid())
            objectOf (out)->removeProperty ("markers");
        else
            objectOf (out)->setProperty ("markers", sorted);
    }

    // 박자 바꾸기와 조 바꾸기: 구간 안의 것은 따라가고, 구간마다 그 처음의 값을 다시 세운다. 처음 값과 같은 바꾸기는 지운다
    auto arrangeChanges = [&] (const char* listKey, const char* valueKey, const char* firstKey, const juce::var& fallback)
    {
        const auto written = root.getProperty (firstKey, juce::var());
        const auto first = written.isVoid() ? fallback : written;
        const auto* list = arrayOf (root, listKey);
        if (written.isVoid() && list == nullptr)
            return;
        // 원래 곡에서 x 에 쓰이는 값 (처음 값이나 그 앞의 마지막 바꾸기)
        auto valueAt = [&] (double x)
        {
            juce::var v = first;
            if (list != nullptr)
                for (const auto& c : *list)
                    if ((double) c.getProperty ("beat", 0.0) <= x + 1.0e-9)
                        v = c.getProperty (valueKey, juce::var());
            return v;
        };
        std::vector<std::pair<double, juce::var>> changes;   // (새 박, 바꾸기 객체)
        for (size_t k = 0; k < all.size(); ++k)
        {
            // 구간 처음: 원래 그 박에 바꾸기가 있으면 그 객체를, 없으면 그 자리에 쓰이던 값으로 새 바꾸기 (적히지 않은 조는 세우지 않는다)
            const juce::var* original = nullptr;
            if (list != nullptr)
                for (const auto& c : *list)
                    if ((double) c.getProperty ("beat", 0.0) == all[k].first)
                        original = &c;
            if (original != nullptr)
            {
                auto copy = original->clone();
                objectOf (copy)->setProperty ("beat", numberLike (original->getProperty ("beat", 0.0), at[k]));
                changes.push_back ({ at[k], copy });
            }
            else if (const auto value = valueAt (all[k].first); ! value.isVoid())
            {
                auto* start = new juce::DynamicObject();
                start->setProperty ("beat", numberLike (juce::var (0), at[k]));
                start->setProperty (valueKey, value.clone());
                changes.push_back ({ at[k], juce::var (start) });
            }
            if (list != nullptr)
                for (const auto& c : *list)
                {
                    const double b = (double) c.getProperty ("beat", 0.0);
                    if (b > all[k].first && b < all[k].second)
                    {
                        auto copy = c.clone();
                        objectOf (copy)->setProperty ("beat", numberLike (c.getProperty ("beat", 0.0), tidy (at[k] + b - all[k].first)));
                        changes.push_back ({ tidy (at[k] + b - all[k].first), copy });
                    }
                }
        }
        std::stable_sort (changes.begin(), changes.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
        // 0 박의 값이 곡의 처음 값이 된다 (적혀 있지 않았고 기본값과 같으면 적지 않는다)
        juce::var current = first;
        size_t from = 0;
        if (! changes.empty() && changes.front().first == 0.0)
        {
            current = changes.front().second.getProperty (valueKey, juce::var());
            from = 1;
        }
        if (! current.isVoid() && (! written.isVoid() || juce::JSON::toString (current, true) != juce::JSON::toString (fallback, true)))
            objectOf (out)->setProperty (firstKey, current.clone());
        juce::Array<juce::var> kept;
        for (size_t i = from; i < changes.size(); ++i)
        {
            const auto value = changes[i].second.getProperty (valueKey, juce::var());
            if (juce::JSON::toString (value, true) == juce::JSON::toString (current, true))
                continue;
            if (! kept.isEmpty() && (double) kept.getLast().getProperty ("beat", 0.0) == changes[i].first)
                kept.removeLast();   // 같은 박은 뒤의 것
            auto change = changes[i].second;
            if (! change.hasProperty ("beat") || (double) change.getProperty ("beat", 0.0) != changes[i].first)
                objectOf (change)->setProperty ("beat", numberLike (juce::var (0), changes[i].first));
            kept.add (change);
            current = value;
        }
        if (kept.isEmpty())
            objectOf (out)->removeProperty (listKey);
        else
            objectOf (out)->setProperty (listKey, kept);
    };
    juce::Array<juce::var> fourFour;
    fourFour.add (4);
    fourFour.add (4);
    arrangeChanges ("meterChanges", "timeSignature", "timeSignature", juce::var (fourFour));
    arrangeChanges ("keyChanges", "key", "key", juce::var());

    // 반복 구간과 곡의 끝
    if (auto* metadata = objectOf (out.getProperty ("metadata", juce::var())))
        if (auto* loop = objectOf (metadata->getProperty ("loop")))
        {
            const auto s = loop->getProperty ("startBeat"), e = loop->getProperty ("endBeat");
            if (json::isNumber (s) && json::isNumber (e))
            {
                const auto ns = mappedBeat (all, at, (double) s, false), ne = mappedBeat (all, at, (double) e, true);
                if (ns && ne && *ne > *ns)
                {
                    loop->setProperty ("startBeat", numberLike (s, *ns));
                    loop->setProperty ("endBeat", numberLike (e, *ne));
                }
                else
                    metadata->removeProperty ("loop");
            }
        }
    if (auto* render = objectOf (out.getProperty ("render", juce::var())))
        if (const auto e = render->getProperty ("endBeat"); json::isNumber (e))
            if (const auto moved = mappedBeat (all, at, (double) e, true))
                render->setProperty ("endBeat", numberLike (e, *moved));
    return out;
}

namespace
{

// 구간 순서로 곡을 다시 짠다 (첫 마커 앞과 곡의 끝 뒤는 그대로)
juce::var arrangeSections (const juce::var& root, const std::vector<size_t>& order, std::string* error, const juce::File& folder)
{
    const auto sections = sectionsOf (root, folder);
    const auto meters = metersOfRoot (root);
    for (const auto& sec : sections)
        if (! meters.isBarLine (sec.start) || ! meters.isBarLine (sec.end))
        {
            if (error != nullptr)
                *error = "section '" + sec.name + "' does not start and end on bar lines";
            return {};
        }
    std::vector<std::pair<double, double>> spans;
    if (sections.front().start > 0.0)
        spans.push_back ({ 0.0, sections.front().start });
    for (const size_t i : order)
        spans.push_back ({ sections[i].start, sections[i].end });
    return withSpansArranged (root, spans, sections.back().end, folder);
}

bool sectionIndexOk (const std::vector<Section>& sections, size_t section, std::string* error)
{
    if (section < sections.size())
        return true;
    if (error != nullptr)
        *error = sections.empty() ? "the song has no sections (add markers)" : "there are " + std::to_string (sections.size()) + " sections";
    return false;
}

} // namespace

juce::var withSectionMoved (const juce::var& root, size_t section, size_t before, std::string* error, const juce::File& folder)
{
    const auto sections = sectionsOf (root, folder);
    if (! sectionIndexOk (sections, section, error) || before > sections.size())
    {
        if (error != nullptr && error->empty())
            *error = "there are " + std::to_string (sections.size()) + " sections";
        return {};
    }
    std::vector<size_t> order;
    for (size_t i = 0; i <= sections.size(); ++i)
    {
        if (i == before)
            order.push_back (section);
        if (i < sections.size() && i != section)
            order.push_back (i);
    }
    return arrangeSections (root, order, error, folder);
}

juce::var withSectionDuplicated (const juce::var& root, size_t section, std::string* error, const juce::File& folder)
{
    const auto sections = sectionsOf (root, folder);
    if (! sectionIndexOk (sections, section, error))
        return {};
    std::vector<size_t> order;
    for (size_t i = 0; i < sections.size(); ++i)
    {
        order.push_back (i);
        if (i == section)
            order.push_back (i);
    }
    return arrangeSections (root, order, error, folder);
}

juce::var withSectionDeleted (const juce::var& root, size_t section, std::string* error, const juce::File& folder)
{
    const auto sections = sectionsOf (root, folder);
    if (! sectionIndexOk (sections, section, error))
        return {};
    std::vector<size_t> order;
    for (size_t i = 0; i < sections.size(); ++i)
        if (i != section)
            order.push_back (i);
    return arrangeSections (root, order, error, folder);
}

std::map<std::string, AutomationLane> trackAutomationLanes (const juce::var& track)
{
    return trackLanes (track);
}

juce::var withTrackAutomation (const juce::var& track, const std::string& key, const AutomationLane& lane)
{
    auto out = track.clone();
    setTrackLane (out, key, lane);
    return out;
}

AutomationLane automationLaneOf (const TrackSpec& t, const std::string& key)
{
    if (const auto id = parseControllerName (key))
    {
        const auto it = t.automation.find (*id);
        return it != t.automation.end() ? it->second : AutomationLane();
    }
    if (key == "mixer/gainDb")
        return t.mixer.gainLane;
    if (key == "mixer/pan")
        return t.mixer.panLane;
    if (key.rfind ("sends/", 0) == 0)
    {
        for (const auto& s : t.sends)
            if (s.bus == key.substr (6))
                return s.levelLane;
        return {};
    }
    // effects/<i>:<type>/<param>
    const auto colon = key.find (':'), slash = key.find ('/', 8);
    if (key.rfind ("effects/", 0) != 0 || colon == std::string::npos || slash == std::string::npos || slash < colon)
        return {};
    const int index = std::atoi (key.substr (8, colon - 8).c_str());
    if (index < 0 || (size_t) index >= t.effects.size() || t.effects[(size_t) index].type != key.substr (colon + 1, slash - colon - 1))
        return {};
    const auto points = t.effects[(size_t) index].params.getProperty ("automation", juce::var()).getProperty (juce::Identifier (key.substr (slash + 1)), juce::var());
    return points.isArray() ? parseLane (points) : AutomationLane();
}

std::vector<juce::Identifier> changedProperties (const juce::var& before, const juce::var& after)
{
    std::vector<juce::Identifier> out;
    std::set<juce::String> keys;
    for (const auto* o : { objectOf (before), objectOf (after) })
        if (o != nullptr)
            for (const auto& p : o->getProperties())
                keys.insert (p.name.toString());
    for (const auto& k : keys)
    {
        const juce::Identifier id (k);
        const auto a = before.getProperty (id, juce::var()), b = after.getProperty (id, juce::var());
        if (a.isVoid() != b.isVoid() || writeJson (a) != writeJson (b))
            out.push_back (id);
    }
    return out;
}

juce::var withTempoFollowingClip (const juce::var& root, size_t track, size_t clip, const std::vector<double>& beats, const MeterMap& meters)
{
    const auto* tracks = arrayOf (root, "tracks");
    if (tracks == nullptr || (int) track >= tracks->size() || beats.size() < 3)
        return {};
    const auto* clips = arrayOf ((*tracks)[(int) track], "clips");
    if (clips == nullptr || (int) clip >= clips->size())
        return {};
    const auto clipJsonValue = (*clips)[(int) clip];
    const auto spec = clipSpecOf (clipJsonValue);
    const auto oldTempo = tempoOf (root);
    std::vector<double> bpm;
    for (size_t i = 0; i + 1 < beats.size(); ++i)
    {
        const double gap = beats[i + 1] - beats[i];
        if (gap <= 1.0e-3)
            return {};
        bpm.push_back (std::round (60.0 / gap * 1.0e4) / 1.0e4);
    }
    // 첫 박을 놓을 마디: 지금 들리는 자리에 가까운 마디 머리, 클립이 곡의 0 초 앞으로 가지 않게
    const double lead = beats.front() - spec.offsetSeconds;   // 클립 머리에서 첫 박까지 (초)
    const double oldBeat = oldTempo.beatAt (clipSongSecondsAt (spec, oldTempo, beats.front()));
    double first = meters.roundToBar (std::max (0.0, oldBeat));
    while (first * 60.0 / bpm.front() < lead - 1.0e-9)
        first = meters.barStart (meters.barAt (first) + 1);
    std::vector<TempoPoint> points { { 0.0, bpm.front(), false } };
    for (size_t i = 1; i < bpm.size(); ++i)
        if (std::abs (bpm[i] - points.back().bpm) > 1.0e-4)
            points.push_back ({ first + (double) i, bpm[i], false });
    auto out = root.clone();
    objectOf (out)->setProperty ("tempo", tempoJson (points));
    auto moved = clipJsonValue.clone();
    auto* o = objectOf (moved);
    o->setProperty ("startBeat", tidy (first - lead * bpm.front() / 60.0));
    for (const char* key : { "sourceBpm", "stretch", "warp", "warpMode" })
        o->removeProperty (key);
    auto trackCopy = (*tracks)[(int) track].clone();
    arrayOf (trackCopy, "clips")->set ((int) clip, moved);
    arrayOf (out, "tracks")->set ((int) track, trackCopy);
    return out;
}

namespace
{
// 곡 안의 악기 정의 (instruments 의 객체들에서 그 이름)
juce::var inlineInstrumentOf (const juce::var& root, const std::string& name)
{
    const auto instruments = root.getProperty ("instruments", juce::var());
    juce::Array<juce::var> list;
    if (const auto* arr = instruments.getArray())
        list = *arr;
    else if (instruments.getDynamicObject() != nullptr)
        list.add (instruments);
    const juce::Identifier key (juce::String::fromUTF8 (name.c_str()));
    for (const auto& v : list)
        if (auto* o = v.getDynamicObject(); o != nullptr && o->hasProperty (key))
            return o->getProperty (key);
    return {};
}

void removeAutomation (juce::var& owner)
{
    if (auto* o = owner.getDynamicObject())
        o->removeProperty ("automation");
}
} // namespace

juce::var trackTemplateOf (const juce::var& root, size_t track, const std::string& name, const juce::File& songFolder)
{
    const auto* tracks = arrayOf (root, "tracks");
    if (tracks == nullptr || (int) track >= tracks->size())
        return {};
    auto copy = (*tracks)[(int) track].clone();
    auto* o = objectOf (copy);
    if (o == nullptr)
        return {};
    for (const char* key : { "notes", "clips", "automation", "folder" })
        o->removeProperty (key);
    juce::Array<juce::Identifier> controllers;
    for (const auto& p : o->getProperties())
        if (parseControllerName (p.name.toString().toStdString()))
            controllers.add (p.name);
    for (const auto& k : controllers)
        o->removeProperty (k);
    if (auto mixer = o->getProperty ("mixer"); mixer.getDynamicObject() != nullptr)
        removeAutomation (mixer);
    if (auto* effects = o->getProperty ("effects").getArray())
        for (auto& e : *effects)
            removeAutomation (e);
    if (auto* sends = o->getProperty ("sends").getArray())
        for (auto& sd : *sends)
            removeAutomation (sd);
    auto* t = new juce::DynamicObject();
    t->setProperty ("name", juce::String::fromUTF8 (name.c_str()));
    t->setProperty ("track", copy);
    const auto instrument = o->getProperty ("instrument").toString().toStdString();
    if (auto def = inlineInstrumentOf (root, instrument); def.getDynamicObject() != nullptr)
    {
        def = def.clone();
        const auto file = def.getProperty ("file", juce::var()).toString();
        if (file.isNotEmpty() && ! juce::File::isAbsolutePath (file) && songFolder != juce::File())
            def.getDynamicObject()->setProperty ("file", songFolder.getChildFile (file).getFullPathName());
        t->setProperty ("instrument", def);
    }
    return juce::var (t);
}

juce::var withTrackFromTemplate (const juce::var& root, size_t index, const juce::var& templ, int* dropped)
{
    auto track = templ.getProperty ("track", juce::var()).clone();
    auto* o = objectOf (track);
    if (o == nullptr || o->getProperty ("instrument").toString().isEmpty())
        return {};
    int removed = 0;
    std::set<std::string> buses, tracks;
    if (const auto* list = arrayOf (root, "buses"))
        for (const auto& b : *list)
            buses.insert (b.getProperty ("name", "").toString().toStdString());
    if (const auto* list = arrayOf (root, "tracks"))
        for (const auto& tr : *list)
            tracks.insert (tr.getProperty ("name", "").toString().toStdString());
    if (auto* sends = o->getProperty ("sends").getArray())
        for (int i = sends->size(); --i >= 0;)
            if (buses.count ((*sends)[i].getProperty ("bus", "").toString().toStdString()) == 0)
            {
                sends->remove (i);
                ++removed;
            }
    if (o->hasProperty ("output") && buses.count (o->getProperty ("output").toString().toStdString()) == 0)
    {
        o->removeProperty ("output");
        ++removed;
    }
    if (auto* effects = o->getProperty ("effects").getArray())
        for (auto& e : *effects)
            if (auto* eo = e.getDynamicObject(); eo != nullptr && eo->hasProperty ("sidechain")
                                                 && tracks.count (eo->getProperty ("sidechain").toString().toStdString()) == 0)
            {
                eo->removeProperty ("sidechain");
                ++removed;
            }
    o->setProperty ("notes", juce::Array<juce::var>());
    if (o->getProperty ("instrument").toString() == "audio")
    {
        o->removeProperty ("notes");
        o->setProperty ("clips", juce::Array<juce::var>());
    }
    auto out = withTrackInserted (root, index, track);
    const auto instrument = o->getProperty ("instrument").toString().toStdString();
    if (const auto def = templ.getProperty ("instrument", juce::var()); def.getDynamicObject() != nullptr && inlineInstrumentOf (out, instrument).isVoid())
        objectOf (out)->setProperty ("instruments", withInlineInstrument (out.getProperty ("instruments", juce::var()), instrument, def));
    if (dropped != nullptr)
        *dropped = removed;
    return out;
}

const std::vector<std::pair<std::string, juce::var>>& factoryTrackTemplates()
{
    static const std::vector<std::pair<std::string, juce::var>> list = []
    {
        std::vector<std::pair<std::string, juce::var>> v;
        auto add = [&v] (const char* id, const char* json) { v.push_back ({ id, juce::JSON::parse (juce::String::fromUTF8 (json)) }); };
        add ("vocal", R"({ "name": "Vocal", "track": { "name": "Vocal", "instrument": "audio", "effects": [
            { "type": "eq", "lowCutHz": 90, "lowCutSlope": 24, "bands": [ { "type": "peak", "freqHz": 300, "gainDb": -2, "q": 1.2 }, { "type": "highShelf", "freqHz": 8000, "gainDb": 2 } ] },
            { "type": "deesser", "frequencyHz": 6500, "thresholdDb": -32, "rangeDb": 6 },
            { "type": "compressor", "thresholdDb": -20, "ratio": 3, "attackMs": 5, "releaseMs": 120, "makeupDb": 3 } ] } })");
        add ("drum-kit", R"({ "name": "Drums", "track": { "name": "Drums", "instrument": "drum-kit", "effects": [
            { "type": "transient", "attackDb": 4, "sustainDb": -2 },
            { "type": "compressor", "thresholdDb": -18, "ratio": 4, "attackMs": 10, "releaseMs": 80, "mix": 0.5, "makeupDb": 4 } ] } })");
        add ("grand-piano", R"({ "name": "Piano", "track": { "name": "Piano", "instrument": "grand-piano", "effects": [
            { "type": "eq", "lowCutHz": 40, "bands": [ { "type": "peak", "freqHz": 250, "gainDb": -1.5, "q": 1 } ] },
            { "type": "compressor", "thresholdDb": -16, "ratio": 2, "attackMs": 20, "releaseMs": 200 } ] } })");
        add ("bass-guitar", R"({ "name": "Bass", "track": { "name": "Bass", "instrument": "bass-guitar", "effects": [
            { "type": "eq", "lowCutHz": 35, "lowCutSlope": 24, "bands": [ { "type": "peak", "freqHz": 800, "gainDb": 2, "q": 1 } ] },
            { "type": "compressor", "thresholdDb": -20, "ratio": 4, "attackMs": 15, "releaseMs": 100, "makeupDb": 3 } ] } })");
        add ("synth-lead", R"({ "name": "Lead", "track": { "name": "Lead", "instrument": "synth.lead", "effects": [
            { "type": "chorus", "rateHz": 0.6, "depthMs": 3, "delayMs": 12, "mix": 0.3, "phaseDeg": 90 },
            { "type": "delay", "timeBeats": 0.75, "feedback": 0.3, "mix": 0.2 } ] } })");
        add ("synth-pad", R"({ "name": "Pad", "track": { "name": "Pad", "instrument": "synth.saw-pad", "effects": [
            { "type": "eq", "lowCutHz": 150 }, { "type": "stereo", "width": 1.3 }, { "type": "reverb", "mode": "algorithmic", "mix": 0.35, "roomSize": 0.85 } ] } })");
        add ("strings", R"({ "name": "Strings", "track": { "name": "Strings", "instrument": "sso-violins-1", "effects": [
            { "type": "eq", "lowCutHz": 120, "bands": [ { "type": "peak", "freqHz": 2500, "gainDb": -1.5, "q": 1 } ] },
            { "type": "reverb", "mix": 0.25, "decaySeconds": 2.4, "preDelayMs": 20 } ] } })");
        return v;
    }();
    return list;
}

} // namespace mr
