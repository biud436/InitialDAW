#include "notation/MusicXml.h"

#include "core/Diagnostics.h"
#include "io/XmlWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mr
{

namespace
{

constexpr const char* stepNames[] = { "C", "D", "E", "F", "G", "A", "B" };

std::string num (int v) { return std::to_string (v); }

// 소수 자리를 고정한 표기 (C 로캘)
std::string fixed (double v, int places)
{
    char buffer[64];
    std::snprintf (buffer, sizeof (buffer), "%.*f", places, v);
    return buffer;
}

// 가장 가까운 정수, 가운데는 짝수 쪽으로
int nearestInt (double v) { return (int) std::nearbyint (v); }

const char* accidentalName (int alter)
{
    switch (alter)
    {
        case -2: return "flat-flat";
        case -1: return "flat";
        case 1: return "sharp";
        case 2: return "double-sharp";
        default: return "natural";
    }
}

void addClef (XmlNode& attributes, Clef clef, int number, bool numbered)
{
    auto& c = attributes.add ("clef");
    if (numbered)
        c.set ("number", number);
    switch (clef)
    {
        case Clef::treble: c.add ("sign", "G"); c.add ("line", "2"); break;
        case Clef::bass: c.add ("sign", "F"); c.add ("line", "4"); break;
        case Clef::percussion: c.add ("sign", "percussion"); break;
    }
}

void addNote (XmlNode& measure, const NotatedEvent& e, int staff, int staffCount, bool percussion)
{
    const int voice = staff == 0 ? 1 : 5;
    const size_t count = std::max<size_t> (1, e.pitches.size());
    for (size_t i = 0; i < count; ++i)
    {
        auto& note = measure.add ("note");
        if (e.velocity > 0 && i == 0 && ! e.rest)
            note.set ("dynamics", fixed (e.velocity / 90.0 * 100.0, 2));
        if (i > 0)
            note.add ("chord");
        const NotatedPitch* p = e.rest ? nullptr : &e.pitches[i];
        if (p == nullptr)
        {
            auto& rest = note.add ("rest");
            if (e.measureRest)
                rest.set ("measure", "yes");
        }
        else if (percussion)
        {
            auto& u = note.add ("unpitched");
            u.add ("display-step", stepNames[p->displayStep]);
            u.add ("display-octave", num (p->displayOctave));
        }
        else
        {
            auto& pitch = note.add ("pitch");
            pitch.add ("step", stepNames[p->spelling.step]);
            if (p->spelling.alter != 0)
                pitch.add ("alter", num (p->spelling.alter));
            pitch.add ("octave", num (p->spelling.octave));
        }
        note.add ("duration", num (e.duration));
        if (p != nullptr && p->tieStop)
            note.add ("tie").set ("type", "stop");
        if (p != nullptr && p->tieStart)
            note.add ("tie").set ("type", "start");
        note.add ("voice", num (voice));
        if (! e.measureRest)
        {
            note.add ("type", e.type);
            for (int d = 0; d < e.dots; ++d)
                note.add ("dot");
        }
        if (p != nullptr && p->accidental)
            note.add ("accidental", accidentalName (*p->accidental));
        if (e.triplet)
        {
            auto& tm = note.add ("time-modification");
            tm.add ("actual-notes", "3");
            tm.add ("normal-notes", "2");
        }
        if (p != nullptr && ! p->notehead.empty())
            note.add ("notehead", p->notehead);
        if (staffCount > 1)
            note.add ("staff", num (staff + 1));
        const bool tuplet = i == 0 && (e.tupletStart || e.tupletStop);
        if ((p != nullptr && (p->tieStart || p->tieStop)) || tuplet)
        {
            auto& notations = note.add ("notations");
            if (p != nullptr && p->tieStop)
                notations.add ("tied").set ("type", "stop");
            if (p != nullptr && p->tieStart)
                notations.add ("tied").set ("type", "start");
            if (i == 0 && e.tupletStart)
            {
                auto& t = notations.add ("tuplet");
                t.set ("type", "start");
                t.set ("bracket", "yes");
            }
            if (i == 0 && e.tupletStop)
                notations.add ("tuplet").set ("type", "stop");
        }
        if (i == 0 && ! e.lyric.empty() && e.lyric != "-")
        {
            auto& lyric = note.add ("lyric");
            lyric.set ("number", 1);
            lyric.add ("syllabic", "single");
            lyric.add ("text", e.lyric);
        }
    }
}

// 템포 숫자: 소수 셋째 자리까지, 끝의 0 과 점을 지운다
std::string tempoText (double bpm)
{
    auto s = fixed (bpm, 3);
    while (! s.empty() && s.back() == '0')
        s.pop_back();
    while (! s.empty() && s.back() == '.')
        s.pop_back();
    return s;
}

} // namespace

std::string musicXml (const Score& score)
{
    XmlNode root ("score-partwise");
    root.set ("version", "4.0");
    if (! score.title.empty())
        root.add ("work").add ("work-title", score.title);
    root.add ("identification").add ("encoding").add ("software", "InitialDAW music-render");
    auto& list = root.add ("part-list");
    for (size_t i = 0; i < score.parts.size(); ++i)
    {
        auto& sp = list.add ("score-part");
        sp.set ("id", "P" + num ((int) i + 1));
        sp.add ("part-name", score.parts[i].name);
    }
    for (size_t i = 0; i < score.parts.size(); ++i)
    {
        const auto& part = score.parts[i];
        auto& p = root.add ("part");
        p.set ("id", "P" + num ((int) i + 1));
        const int staves = (int) part.clefs.size();
        for (const auto& m : part.measures)
        {
            auto& measure = p.add ("measure");
            measure.set ("number", m.number);
            if (m.number == 1 || m.keyChanged || m.timeChanged)
            {
                auto& attributes = measure.add ("attributes");
                if (m.number == 1)
                    attributes.add ("divisions", num (divisionsPerQuarter));
                if (m.keyChanged && ! part.percussion)
                {
                    auto& key = attributes.add ("key");
                    key.add ("fifths", num (m.key.fifths));
                    key.add ("mode", m.key.minor ? "minor" : "major");
                }
                if (m.timeChanged)
                {
                    auto& time = attributes.add ("time");
                    time.add ("beats", num (m.beatsPerBar));
                    time.add ("beat-type", num (m.beatUnit));
                }
                if (m.number == 1)
                {
                    if (staves > 1)
                        attributes.add ("staves", num (staves));
                    for (int s = 0; s < staves; ++s)
                        addClef (attributes, part.clefs[(size_t) s], s + 1, staves > 1);
                }
            }
            if (i == 0)
                for (const auto& name : m.markers)
                {
                    auto& d = measure.add ("direction");
                    d.set ("placement", "above");
                    d.add ("direction-type").add ("rehearsal", name);
                }
            if (i == 0)
                for (const auto& t : m.tempo)
                {
                    auto& d = measure.add ("direction");
                    d.set ("placement", "above");
                    auto& type = d.add ("direction-type");
                    if (t.shown)
                    {
                        auto& metronome = type.add ("metronome");
                        metronome.add ("beat-unit", "quarter");
                        metronome.add ("per-minute", num (nearestInt (t.bpm)));
                    }
                    else
                        type.add ("words");   // 재생 템포만 (보이는 기호 없음)
                    if (t.offset > 0)
                        d.add ("offset", num (t.offset));
                    d.add ("sound").set ("tempo", tempoText (t.bpm));
                }
            for (int s = 0; s < staves; ++s)
            {
                if (s > 0)
                    measure.add ("backup").add ("duration", num (m.length));
                for (const auto& e : m.staves[(size_t) s])
                    addNote (measure, e, s, staves, part.percussion);
            }
        }
    }
    return root.document ("<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML 4.0 Partwise//EN\" \"http://www.musicxml.org/dtds/partwise.dtd\">");
}

void writeMusicXml (const Score& score, const juce::File& file)
{
    const auto text = musicXml (score);
    if (! file.getParentDirectory().createDirectory() || ! file.replaceWithData (text.data(), text.size()))
        throw Failure ("io", "cannot write " + file.getFullPathName().toStdString());
}

} // namespace mr
