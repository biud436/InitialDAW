#include "composition/Document.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace mr
{

namespace
{

bool isScalar (const juce::var& v) { return ! v.isObject() && ! v.isArray(); }

// 한 줄에 쓰는 자식: 스칼라, 또는 스칼라만 든 배열
bool fitsOnOneLine (const juce::var& v)
{
    if (const auto* arr = v.getArray())
        return std::all_of (arr->begin(), arr->end(), isScalar);
    return isScalar (v);
}

void writeString (const juce::String& s, std::string& out)
{
    out += '"';
    for (const char c : s.toStdString())
    {
        const auto u = (unsigned char) c;
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (u < 0x20)
                {
                    char buffer[8];
                    std::snprintf (buffer, sizeof (buffer), "\\u%04x", u);
                    out += buffer;
                }
                else
                    out += c;
        }
    }
    out += '"';
}

void writeDouble (double x, std::string& out)
{
    if (! std::isfinite (x))
    {
        out += "null";
        return;
    }
    char buffer[40];
    if (x == std::floor (x) && std::abs (x) < 1.0e15)
    {
        std::snprintf (buffer, sizeof (buffer), "%.1f", x);
        out += buffer;
        return;
    }
    for (int precision = 15; precision <= 17; ++precision)
    {
        std::snprintf (buffer, sizeof (buffer), "%.*g", precision, x);
        if (std::strtod (buffer, nullptr) == x)
            break;
    }
    out += buffer;
}

void writeValue (const juce::var& v, int depth, std::string& out)
{
    if (v.isVoid() || v.isUndefined())
        out += "null";
    else if (v.isBool())
        out += (bool) v ? "true" : "false";
    else if (v.isInt() || v.isInt64())
        out += std::to_string ((juce::int64) v);
    else if (v.isDouble())
        writeDouble ((double) v, out);
    else if (v.isString())
        writeString (v.toString(), out);
    else if (const auto* arr = v.getArray())
    {
        if (arr->isEmpty())
        {
            out += "[]";
            return;
        }
        const bool flat = std::all_of (arr->begin(), arr->end(), fitsOnOneLine);
        out += '[';
        for (int i = 0; i < arr->size(); ++i)
        {
            if (flat)
                out += i > 0 ? ", " : "";
            else
                out += (i > 0 ? ",\n" : "\n") + std::string ((size_t) depth + 1, ' ');
            writeValue ((*arr)[i], depth + 1, out);
        }
        if (! flat)
            out += "\n" + std::string ((size_t) depth, ' ');
        out += ']';
    }
    else if (const auto* obj = v.getDynamicObject())
    {
        const auto& props = obj->getProperties();
        if (props.isEmpty())
        {
            out += "{}";
            return;
        }
        bool flat = true;
        for (const auto& p : props)
            flat &= fitsOnOneLine (p.value);
        out += '{';
        for (int i = 0; i < props.size(); ++i)
        {
            if (flat)
                out += i > 0 ? ", " : "";
            else
                out += (i > 0 ? ",\n" : "\n") + std::string ((size_t) depth + 1, ' ');
            writeString (props.getName (i).toString(), out);
            out += ": ";
            writeValue (props.getValueAt (i), depth + 1, out);
        }
        if (! flat)
            out += "\n" + std::string ((size_t) depth, ' ');
        out += '}';
    }
    else
        out += "null";
}

const juce::Array<juce::var>* notesArray (const juce::var& trackJson)
{
    return trackJson.getProperty ("notes", juce::var()).getArray();
}

// 원래 값이 정수였고 새 값도 정수면 정수로 쓴다 (손으로 쓴 파일의 표기를 지킨다)
juce::var numberLike (const juce::var& original, double value)
{
    if ((original.isInt() || original.isInt64()) && value == std::floor (value) && std::abs (value) < 1.0e9)
        return (int) value;
    return value;
}

const char* const sharpNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const char* const flatNames[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };

} // namespace

std::optional<CompositionDocument> CompositionDocument::fromText (const juce::String& text, const juce::File& file, std::string& error)
{
    juce::var root;
    const auto parsed = juce::JSON::parse (text, root);
    if (parsed.failed())
    {
        error = describeJsonError (parsed.getErrorMessage());
        return std::nullopt;
    }
    if (root.getDynamicObject() == nullptr)
    {
        error = "the top level of a composition must be a JSON object";
        return std::nullopt;
    }
    return CompositionDocument (std::move (root), file);
}

std::optional<CompositionDocument> CompositionDocument::load (const juce::File& file, std::string& error)
{
    if (! file.existsAsFile())
    {
        error = "file not found: " + file.getFullPathName().toStdString();
        return std::nullopt;
    }
    return fromText (file.loadFileAsString(), file, error);
}

ParseResult CompositionDocument::parse() const { return parseCompositionValue (root_, file_); }

std::string CompositionDocument::toJson() const { return writeJson (root_) + "\n"; }

bool CompositionDocument::save (const juce::File& file, std::string& error)
{
    const auto text = toJson();
    juce::TemporaryFile temp (file);
    if (! temp.getFile().replaceWithData (text.data(), text.size()))
    {
        error = "cannot write " + temp.getFile().getFullPathName().toStdString();
        return false;
    }
    if (! temp.overwriteTargetFileWithTemporary())
    {
        error = "cannot replace " + file.getFullPathName().toStdString();
        return false;
    }
    file_ = file;
    return true;
}

size_t CompositionDocument::trackCount() const
{
    const auto* tracks = root_.getProperty ("tracks", juce::var()).getArray();
    return tracks != nullptr ? (size_t) tracks->size() : 0;
}

juce::var CompositionDocument::track (size_t index) const
{
    const auto* tracks = root_.getProperty ("tracks", juce::var()).getArray();
    jassert (tracks != nullptr && index < (size_t) tracks->size());
    return (*tracks)[(int) index].clone();
}

void CompositionDocument::setTrack (size_t index, const juce::var& track)
{
    auto* tracks = root_.getProperty ("tracks", juce::var()).getArray();
    jassert (tracks != nullptr && index < (size_t) tracks->size());
    tracks->set ((int) index, track);
}

juce::var CompositionDocument::property (const juce::Identifier& name) const
{
    return root_.getProperty (name, juce::var()).clone();
}

void CompositionDocument::setProperty (const juce::Identifier& name, const juce::var& value)
{
    auto* obj = root_.getDynamicObject();
    if (value.isVoid())
        obj->removeProperty (name);
    else
        obj->setProperty (name, value);
}

std::string writeJson (const juce::var& value)
{
    std::string out;
    writeValue (value, 0, out);
    return out;
}

std::vector<EditNote> editNotes (const juce::var& trackJson, const TrackSpec& parsed)
{
    std::vector<EditNote> out;
    const auto* arr = notesArray (trackJson);
    out.reserve (parsed.notes.size());
    for (size_t i = 0; i < parsed.notes.size(); ++i)
        out.push_back ({ parsed.notes[i], arr != nullptr && (int) i < arr->size() ? (*arr)[(int) i] : juce::var() });
    return out;
}

juce::var withNotes (const juce::var& trackJson, const std::vector<EditNote>& notes)
{
    juce::var track = trackJson.clone();
    juce::Array<juce::var> arr;
    arr.ensureStorageAllocated ((int) notes.size());
    for (const auto& e : notes)
    {
        const bool fresh = e.source.getDynamicObject() == nullptr;
        juce::var obj = fresh ? juce::var (new juce::DynamicObject()) : e.source.clone();
        auto* o = obj.getDynamicObject();
        const auto oldPitch = o->getProperty ("pitch");
        if (oldPitch.isString())
        {
            const auto name = oldPitch.toString();
            const bool flats = name.length() > 1 && name[1] == 'b';
            const auto old = parseNoteName (name.toStdString());
            if (! old || *old != e.note.pitch)
                o->setProperty ("pitch", juce::String (noteName (e.note.pitch, flats)));
        }
        else
            o->setProperty ("pitch", e.note.pitch);
        o->setProperty ("startBeat", numberLike (o->getProperty ("startBeat"), e.note.startBeat));
        o->setProperty ("durationBeat", numberLike (o->getProperty ("durationBeat"), e.note.durationBeat));
        o->setProperty ("velocity", e.note.velocity);
        if (! fresh && o->getProperty ("articulation").toString().toStdString() != e.note.articulation)
        {
            if (e.note.articulation.empty())
                o->removeProperty ("articulation");
            else
                o->setProperty ("articulation", juce::String (e.note.articulation));
        }
        if (! fresh && o->getProperty ("lyric").toString().toStdString() != e.note.lyric)
        {
            if (e.note.lyric.empty())
                o->removeProperty ("lyric");
            else
                o->setProperty ("lyric", juce::String (e.note.lyric));
        }
        if (fresh)
        {
            if (e.note.channel != 1)
                o->setProperty ("channel", e.note.channel);
            if (! e.note.articulation.empty())
                o->setProperty ("articulation", juce::String (e.note.articulation));
            if (! e.note.humanize)
                o->setProperty ("humanize", false);
            if (! e.note.lyric.empty())
                o->setProperty ("lyric", juce::String (e.note.lyric));
        }
        arr.add (obj);
    }
    track.getDynamicObject()->setProperty ("notes", arr);
    return track;
}

juce::var laneJson (const AutomationLane& lane)
{
    if (lane.empty())
        return {};
    juce::Array<juce::var> arr;
    for (const auto& p : lane.points)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("beat", p.beat);
        o->setProperty ("value", p.value);
        switch (p.curve)
        {
            case CurveShape::step: o->setProperty ("curve", "step"); break;
            case CurveShape::exponential: o->setProperty ("curve", "exponential"); break;
            case CurveShape::smooth: o->setProperty ("curve", "smooth"); break;
            case CurveShape::linear: break;
        }
        arr.add (juce::var (o));
    }
    return arr;
}

juce::var withLane (const juce::var& trackJson, int controller, const AutomationLane& lane)
{
    juce::var track = trackJson.clone();
    auto* t = track.getDynamicObject();
    const juce::Identifier key (controllerName (controller));

    const juce::var points = laneJson (lane);

    if (t->hasProperty (key))   // 트랙 바로 아래에 있던 곡선
    {
        if (points.isVoid())
            t->removeProperty (key);
        else
            t->setProperty (key, points);
        return track;
    }
    auto automation = t->getProperty ("automation");
    if (automation.getDynamicObject() == nullptr)
    {
        if (points.isVoid())
            return track;
        automation = juce::var (new juce::DynamicObject());
        t->setProperty ("automation", automation);
    }
    if (points.isVoid())
    {
        automation.getDynamicObject()->removeProperty (key);
        if (automation.getDynamicObject()->getProperties().isEmpty())
            t->removeProperty ("automation");
    }
    else
        automation.getDynamicObject()->setProperty (key, points);
    return track;
}

juce::var withMixer (const juce::var& owner, const MixerSettings& mixer)
{
    juce::var result = owner.getDynamicObject() != nullptr ? owner.clone() : juce::var (new juce::DynamicObject());
    auto* o = result.getDynamicObject();
    juce::var m = o->getProperty ("mixer");
    const bool existed = m.getDynamicObject() != nullptr;
    if (! existed)
        m = juce::var (new juce::DynamicObject());
    auto* mo = m.getDynamicObject();
    auto setNumber = [mo] (const char* key, double value)
    {
        if (mo->hasProperty (key) || value != 0.0)
            mo->setProperty (key, numberLike (mo->getProperty (key), value));
    };
    auto setFlag = [mo] (const char* key, bool value)
    {
        if (mo->hasProperty (key) || value)
            mo->setProperty (key, value);
    };
    setNumber ("gainDb", mixer.gainDb);
    setNumber ("pan", mixer.pan);
    setFlag ("mute", mixer.mute);
    setFlag ("solo", mixer.solo);
    if (existed || ! mo->getProperties().isEmpty())
        o->setProperty ("mixer", m);
    return result;
}

juce::var withSends (const juce::var& track, const std::vector<SendSpec>& sends)
{
    juce::var result = track.clone();
    auto* o = result.getDynamicObject();
    const auto* old = o->getProperty ("sends").getArray();
    if (sends.empty())
    {
        o->removeProperty ("sends");
        return result;
    }
    juce::Array<juce::var> arr;
    for (size_t i = 0; i < sends.size(); ++i)
    {
        const bool reuse = old != nullptr && (int) i < old->size() && (*old)[(int) i].getDynamicObject() != nullptr;
        juce::var obj = reuse ? (*old)[(int) i].clone() : juce::var (new juce::DynamicObject());
        auto* so = obj.getDynamicObject();
        so->setProperty ("bus", juce::String::fromUTF8 (sends[i].bus.c_str()));
        so->setProperty ("levelDb", numberLike (so->getProperty ("levelDb"), sends[i].levelDb));
        if (so->hasProperty ("preFader") || sends[i].preFader)
            so->setProperty ("preFader", sends[i].preFader);
        // 센드 크기의 곡선은 SendSpec 을 따른다 (같은 곡선이면 원래 표기를 둔다)
        Diagnostics ignored;
        const auto oldLane = readAutomationLane (so->getProperty ("automation").getProperty ("levelDb", juce::var()), "$", -120.0, 12.0, ignored);
        const bool sameLane = std::equal (oldLane.points.begin(), oldLane.points.end(), sends[i].levelLane.points.begin(), sends[i].levelLane.points.end(),
                                          [] (const AutomationPoint& a, const AutomationPoint& b) { return a.beat == b.beat && a.value == b.value && a.curve == b.curve; });
        if (sends[i].levelLane.empty())
            so->removeProperty ("automation");
        else if (! sameLane)
        {
            auto* automation = new juce::DynamicObject();
            automation->setProperty ("levelDb", laneJson (sends[i].levelLane));
            so->setProperty ("automation", juce::var (automation));
        }
        arr.add (obj);
    }
    o->setProperty ("sends", arr);
    return result;
}

juce::var withEffects (const juce::var& owner, const juce::Array<juce::var>& effects)
{
    juce::var result = owner.getDynamicObject() != nullptr ? owner.clone() : juce::var (new juce::DynamicObject());
    auto* o = result.getDynamicObject();
    if (effects.isEmpty() && ! o->hasProperty ("effects"))
        return result;
    juce::Array<juce::var> arr;
    for (const auto& e : effects)
        arr.add (e.clone());
    o->setProperty ("effects", arr);
    return result;
}

juce::var withEffectParameter (const juce::var& effect, const juce::Identifier& key, const juce::var& value)
{
    juce::var result = effect.clone();
    auto* o = result.getDynamicObject();
    if (value.isVoid())
        o->removeProperty (key);
    else if (value.isDouble())
        o->setProperty (key, numberLike (o->getProperty (key), (double) value));
    else
        o->setProperty (key, value);
    return result;
}

juce::var withPluginState (const juce::var& track, const std::string& stateBase64)
{
    juce::var result = track.clone();
    auto* o = result.getDynamicObject();
    juce::var plugin = o->getProperty ("plugin");
    if (plugin.getDynamicObject() == nullptr)
        plugin = juce::var (new juce::DynamicObject());
    auto* po = plugin.getDynamicObject();
    // 상태가 모든 값을 담는다: 남은 program 과 parameters 는 다시 열 때 상태를 덮으므로 지운다
    po->removeProperty ("program");
    po->removeProperty ("parameters");
    po->setProperty ("state", juce::String (stateBase64));
    o->setProperty ("plugin", plugin);
    return result;
}

juce::var withInstrument (const juce::var& track, const std::string& instrument)
{
    juce::var result = track.clone();
    auto* o = result.getDynamicObject();
    o->setProperty ("instrument", juce::String::fromUTF8 (instrument.c_str()));
    o->removeProperty ("plugin");
    o->removeProperty ("synth");
    return result;
}

juce::var withSynthValue (const juce::var& track, const std::string& key, const juce::var& value)
{
    juce::var result = track.clone();
    auto* o = result.getDynamicObject();
    juce::var synth = o->getProperty ("synth");
    if (synth.getDynamicObject() == nullptr)
    {
        if (value.isVoid())
            return result;
        synth = juce::var (new juce::DynamicObject());
    }
    auto* so = synth.getDynamicObject();
    const juce::Identifier id (juce::String::fromUTF8 (key.c_str()));
    if (value.isVoid())
        so->removeProperty (id);
    else
        so->setProperty (id, value.isDouble() ? numberLike (so->getProperty (id), (double) value) : value);
    if (so->getProperties().isEmpty())
        o->removeProperty ("synth");
    else
        o->setProperty ("synth", synth);
    return result;
}

juce::var withInlineInstrument (const juce::var& instruments, const std::string& name, const juce::var& definition)
{
    juce::Array<juce::var> list;
    if (const auto* arr = instruments.getArray())
        for (const auto& v : *arr)
            list.add (v.clone());
    else if (! instruments.isVoid())
        list.add (instruments.clone());
    const juce::Identifier key (juce::String::fromUTF8 (name.c_str()));
    for (auto& v : list)
        if (auto* o = v.getDynamicObject())
        {
            o->setProperty (key, definition.clone());
            return list;
        }
    auto* table = new juce::DynamicObject();
    table->setProperty (key, definition.clone());
    list.add (juce::var (table));
    return list;
}

juce::var effectsJson (const juce::var& owner)
{
    if (const auto* arr = owner.getProperty ("effects", juce::var()).getArray())
        return juce::var (*arr).clone();
    return juce::var (juce::Array<juce::var>());
}

juce::var clipJson (const AudioClip& clip, const juce::var& original)
{
    juce::var out = original.getDynamicObject() != nullptr ? original.clone() : juce::var (new juce::DynamicObject());
    auto* o = out.getDynamicObject();
    auto optional = [o] (const char* key, double value)
    {
        if (value != 0.0 || o->hasProperty (key))
            o->setProperty (key, value);
    };
    o->setProperty ("file", juce::String::fromUTF8 (clip.file.c_str()));
    o->setProperty ("startBeat", numberLike (o->getProperty ("startBeat"), clip.startBeat));
    optional ("offsetSeconds", clip.offsetSeconds);
    if (clip.lengthSeconds)
        o->setProperty ("lengthSeconds", *clip.lengthSeconds);
    else
        o->removeProperty ("lengthSeconds");
    optional ("gainDb", clip.gainDb);
    optional ("fadeInSeconds", clip.fadeInSeconds);
    optional ("fadeOutSeconds", clip.fadeOutSeconds);
    auto shape = [o] (const char* key, FadeShape value)
    {
        if (value != FadeShape::sine)
            o->setProperty (key, fadeShapeName (value));
        else
            o->removeProperty (key);
    };
    shape ("fadeInShape", clip.fadeInShape);
    shape ("fadeOutShape", clip.fadeOutShape);
    if (clip.reverse)
        o->setProperty ("reverse", true);
    else
        o->removeProperty ("reverse");
    if (clip.gainCurve.empty())
        o->removeProperty ("gainCurve");
    else
    {
        auto tidy = [] (double v) { return std::round (v * 1.0e6) / 1.0e6; };
        juce::Array<juce::var> points;
        for (const auto& p : clip.gainCurve.points)
        {
            auto* q = new juce::DynamicObject();
            q->setProperty ("seconds", tidy (p.beat));
            q->setProperty ("value", std::round (p.value * 100.0) / 100.0);
            switch (p.curve)
            {
                case CurveShape::step: q->setProperty ("curve", "step"); break;
                case CurveShape::exponential: q->setProperty ("curve", "exponential"); break;
                case CurveShape::smooth: q->setProperty ("curve", "smooth"); break;
                case CurveShape::linear: break;
            }
            points.add (juce::var (q));
        }
        o->setProperty ("gainCurve", points);
    }
    if (clip.sourceBpm)
        o->setProperty ("sourceBpm", *clip.sourceBpm);
    else
        o->removeProperty ("sourceBpm");
    if (clip.stretch != 1.0)
        o->setProperty ("stretch", clip.stretch);
    else
        o->removeProperty ("stretch");
    optional ("pitchSemitones", clip.pitchSemitones);
    if (clip.pitchSemitones == 0.0)
        o->removeProperty ("pitchSemitones");
    if (clip.pitchCorrection)
    {
        const auto& pc = *clip.pitchCorrection;
        auto* t = new juce::DynamicObject();
        t->setProperty ("strength", pc.strength);
        t->setProperty ("speedMs", pc.speedMs);
        if (! pc.formant)
            t->setProperty ("formant", false);
        if (! pc.key.empty())
            t->setProperty ("key", juce::String::fromUTF8 (pc.key.c_str()));
        if (! pc.scale.empty())
        {
            t->setProperty ("scale", juce::String (pc.scale));
            t->setProperty ("root", juce::String (pc.root));
        }
        o->setProperty ("pitchCorrection", juce::var (t));
    }
    else
        o->removeProperty ("pitchCorrection");
    if (clip.pitchEdits.empty())
        o->removeProperty ("pitchEdits");
    else
    {
        auto tidy = [] (double v) { return std::round (v * 1.0e6) / 1.0e6; };
        juce::Array<juce::var> edits;
        for (const auto& e : clip.pitchEdits)
        {
            auto* p = new juce::DynamicObject();
            p->setProperty ("seconds", tidy (e.seconds));
            p->setProperty ("lengthSeconds", tidy (e.lengthSeconds));
            p->setProperty ("semitones", tidy (e.semitones));
            if (e.flatten != 0.0)
                p->setProperty ("flatten", tidy (e.flatten));
            if (e.vibrato != 1.0)
                p->setProperty ("vibrato", tidy (e.vibrato));
            edits.add (juce::var (p));
        }
        o->setProperty ("pitchEdits", edits);
    }
    if (clip.warpSlices)
        o->setProperty ("warpMode", "slice");
    else if (o->getProperty ("warpMode") != juce::var ("stretch"))
        o->removeProperty ("warpMode");
    if (clip.warp.empty())
        o->removeProperty ("warp");
    else
    {
        juce::Array<juce::var> points;
        for (const auto& w : clip.warp)
        {
            auto* p = new juce::DynamicObject();
            p->setProperty ("seconds", w.seconds);
            p->setProperty ("beat", w.beat);
            points.add (juce::var (p));
        }
        o->setProperty ("warp", points);
    }
    if (clip.muted)
        o->setProperty ("muted", true);
    else
        o->removeProperty ("muted");
    return out;
}

juce::var withClip (const juce::var& track, size_t index, const AudioClip& clip)
{
    juce::var t = track.clone();
    if (auto* clips = t.getProperty ("clips", juce::var()).getArray(); clips != nullptr && (int) index < clips->size())
        clips->set ((int) index, clipJson (clip, (*clips)[(int) index]));
    return t;
}

juce::var withClipAdded (const juce::var& track, const AudioClip& clip)
{
    juce::var t = track.clone();
    auto clips = t.getProperty ("clips", juce::var());
    if (! clips.isArray())
    {
        clips = juce::var (juce::Array<juce::var>());
        t.getDynamicObject()->setProperty ("clips", clips);
    }
    t.getProperty ("clips", juce::var()).getArray()->add (clipJson (clip));
    return t;
}

juce::var withClipRemoved (const juce::var& track, size_t index)
{
    juce::var t = track.clone();
    if (auto* clips = t.getProperty ("clips", juce::var()).getArray(); clips != nullptr && (int) index < clips->size())
        clips->remove ((int) index);
    return t;
}

juce::var tempoJson (const std::vector<TempoPoint>& tempo)
{
    if (tempo.size() == 1 && ! tempo[0].ramp)
        return tempo[0].bpm;
    juce::Array<juce::var> arr;
    for (const auto& p : tempo)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("beat", p.beat);
        o->setProperty ("bpm", p.bpm);
        if (p.ramp)
            o->setProperty ("ramp", true);
        arr.add (juce::var (o));
    }
    return arr;
}

juce::var timeSignatureJson (int beatsPerBar, int beatUnit)
{
    juce::Array<juce::var> arr;
    arr.add (beatsPerBar);
    arr.add (beatUnit);
    return arr;
}

std::string noteName (int pitch, bool flats)
{
    pitch = std::clamp (pitch, 0, 127);
    return std::string ((flats ? flatNames : sharpNames)[pitch % 12]) + std::to_string (pitch / 12 - 1);
}

} // namespace mr
