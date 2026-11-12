#include "midi/MidiImport.h"

#include "composition/Composition.h"
#include "composition/Document.h"
#include "core/Diagnostics.h"

#include "midi/Smf.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

namespace mr
{

namespace
{

double tidyBeat (double beat)
{
    const double r = std::round (beat * 1.0e6) / 1.0e6;
    return r == 0.0 ? 0.0 : r;
}

// 가져오지 않는 컨트롤러: 뱅크, 데이터 입력과 RPN, 채널 모드
bool skippedController (int cc)
{
    return cc == 0 || cc == 32 || cc == 6 || cc == 38 || (cc >= 96 && cc <= 101) || cc >= 120;
}

juce::String textOf (const midi::Event& m) { return juce::String::fromUTF8 (m.text().data(), (int) m.text().size()); }

} // namespace

std::string instrumentForProgram (int program, bool drums, const Registry* registry)
{
    auto known = [registry] (const char* name) { return registry == nullptr || registry->find (name) != nullptr; };
    if (drums)
        return known ("drum-kit") ? "drum-kit" : "";
    // GM 묶음마다 가까운 악기 (앞에서부터 등록된 것)
    std::vector<const char*> candidates;
    const int p = std::clamp (program, 0, 127);
    if (p <= 7) candidates = { "grand-piano", "piano" };
    else if (p == 8) candidates = { "celeste" };
    else if (p == 9 || p == 10 || p == 14) candidates = { "glockenspiel", "celeste" };
    else if (p == 11 || p == 12 || p == 13) candidates = { "vibraphone" };
    else if (p <= 15) candidates = { "celeste" };
    else if (p <= 23) candidates = { "synth.pad", "pad" };
    else if (p == 24) candidates = { "guitar" };
    else if (p == 25) candidates = { "guitar" };
    else if (p == 26) candidates = { "jazz-guitar", "electric-guitar" };
    else if (p <= 31) candidates = { "electric-guitar", "guitar" };
    else if (p == 32) candidates = { "sso-basses", "basses" };
    else if (p <= 37) candidates = { "bass-guitar" };
    else if (p <= 39) candidates = { "synth.bass", "bass" };
    else if (p == 40) candidates = { "sso-violins-1", "violins" };
    else if (p == 41) candidates = { "sso-violas", "violas" };
    else if (p == 42) candidates = { "cello-solo", "sso-celli" };
    else if (p == 43) candidates = { "sso-basses", "basses" };
    else if (p <= 45) candidates = { "sso-violins-1", "violins" };
    else if (p == 46) candidates = { "harp" };
    else if (p == 47) candidates = { "timpani" };
    else if (p <= 49) candidates = { "sso-violins-1", "violins" };
    else if (p <= 51) candidates = { "synth.strings", "strings" };
    else if (p <= 54) candidates = { "choir", "choir-large" };
    else if (p == 55) candidates = { "horns" };
    else if (p <= 59) candidates = { "horns", "horn" };
    else if (p == 60) candidates = { "horn", "horns" };
    else if (p == 61) candidates = { "horns" };
    else if (p <= 63) candidates = { "synth.lead" };
    else if (p <= 67) candidates = { "tenor-sax" };
    else if (p == 68) candidates = { "oboe-solo", "oboe" };
    else if (p == 69) candidates = { "cor-anglais", "oboe-solo" };
    else if (p == 70) candidates = { "clarinet-solo" };
    else if (p == 71) candidates = { "clarinet-solo", "clarinet" };
    else if (p <= 79) candidates = { "flute-solo", "flute" };
    else if (p <= 87) candidates = { "synth.lead" };
    else if (p <= 95) candidates = { "synth.pad", "pad" };
    else if (p <= 103) candidates = { "synth.pad" };
    else if (p <= 107) candidates = { "guitar", "harp" };
    else if (p <= 111) candidates = { "oboe-solo" };
    else if (p == 112) candidates = { "glockenspiel" };
    else if (p <= 118) candidates = { "percussion" };
    else candidates = { "synth.noise" };
    for (const auto* name : candidates)
        if (known (name))
            return name;
    return {};
}

juce::var importMidi (const juce::File& file, const MidiImportOptions& options, MidiImportReport& report)
{
    std::ifstream stream (file.getFullPathName().toStdString(), std::ios::binary);
    if (! stream)
        throw Failure ("io", "cannot read " + file.getFullPathName().toStdString());
    const std::vector<uint8_t> bytes ((std::istreambuf_iterator<char> (stream)), std::istreambuf_iterator<char>());
    auto parsed = midi::read (bytes);
    if (! parsed)
        throw Failure ("midi", "not a standard MIDI file: " + file.getFullPathName().toStdString());
    auto& smf = *parsed;

    // 박 = 틱 / ppq. SMPTE 시간은 초로 바꿔 120 BPM 으로 읽는다
    const int timeFormat = smf.timeFormat;
    double ppq = timeFormat > 0 ? (double) timeFormat : 0.0;
    if (ppq <= 0.0)
    {
        midi::convertSmpteToSeconds (smf);
        ppq = 0.0;
        report.warnings.push_back ("SMPTE time; read at 120 BPM");
    }
    auto beatOf = [ppq] (double stamp) { return tidyBeat (ppq > 0.0 ? stamp / ppq : stamp * 2.0); };

    // ---- 템포, 박자, 마커 (모든 트랙에서) ----
    std::map<double, double> tempoAt;   // 박 -> bpm (같은 박은 뒤의 것)
    std::vector<std::pair<double, std::string>> markers;
    std::optional<std::pair<int, int>> timeSignature;
    std::map<double, std::pair<int, int>> meterAt;   // 곡 중간의 박자 바꾸기
    std::map<double, KeySignature> keyAt;             // 조표 (FF 59)
    for (const auto& seq : smf.tracks)
    {
        for (size_t i = 0; i < seq.size(); ++i)
        {
            const auto& m = seq[i];
            if (m.isTempo() && ppq > 0.0)
            {
                const double spq = m.tempoSecondsPerQuarter();
                if (spq > 0.0)
                    tempoAt[beatOf (m.time)] = std::clamp (60.0 / spq, 1.0, 999.0);
            }
            else if (m.isTimeSignature())
            {
                const auto d = m.metaData();
                const int num = d.size() > 0 ? d[0] : 4, den = d.size() > 1 ? 1 << d[1] : 4;
                const double beat = beatOf (m.time);
                if (beat <= 0.0)
                    timeSignature = std::make_pair (num, den);
                else
                    meterAt[beat] = std::make_pair (num, den);
            }
            else if (m.isKeySignature())
            {
                const auto d = m.metaData();
                const int fifths = std::clamp (d.size() > 0 ? (int) (int8_t) d[0] : 0, -7, 7);
                keyAt[beatOf (m.time)] = KeySignature { fifths, d.size() > 1 && d[1] != 0 };
            }
            else if (m.isText() && m.metaType() == 6)
                markers.push_back ({ beatOf (m.time), textOf (m).trim().toStdString() });
        }
    }
    std::vector<TempoPoint> tempo;
    for (const auto& [beat, bpm] : tempoAt)
    {
        const double rounded = std::round (bpm * 1000.0) / 1000.0;
        if (! tempo.empty() && tempo.back().bpm == rounded)
            continue;
        tempo.push_back ({ beat, rounded, false });
    }
    if (tempo.empty() || tempo.front().beat > 0.0)
        tempo.insert (tempo.begin(), { 0.0, tempo.empty() ? 120.0 : tempo.front().bpm, false });
    report.tempoPoints = (int) tempo.size();

    // ---- 트랙: MIDI 트랙과 채널의 쌍마다 ----
    struct Part
    {
        int midiTrack = 0;
        int channel = 1;
        juce::String name;
        int program = -1;
        juce::Array<juce::var> notes;
        std::map<int, AutomationLane> lanes;   // 컨트롤러 번호 -> 곡선
        std::map<double, std::vector<int>> noteIndexAt;   // 박 -> 음의 번호 (가사를 붙인다)
    };
    std::vector<Part> parts;
    juce::String conductorName;   // 음이 없는 첫 트랙의 이름 (형식 1 의 곡 제목)
    double lastBeat = 0.0;
    for (int t = 0; t < (int) smf.tracks.size(); ++t)
    {
        const auto& seq = smf.tracks[(size_t) t];
        juce::String trackName;
        std::map<int, size_t> partOf;   // 채널 -> parts 의 번호
        auto part = [&] (int channel) -> Part&
        {
            if (auto it = partOf.find (channel); it != partOf.end())
                return parts[it->second];
            Part p;
            p.midiTrack = t;
            p.channel = channel;
            partOf[channel] = parts.size();
            parts.push_back (std::move (p));
            return parts.back();
        };
        std::vector<std::pair<double, std::string>> lyrics;
        for (size_t i = 0; i < seq.size(); ++i)
        {
            const auto& m = seq[i];
            const double beat = beatOf (m.time);
            if (m.metaType() == 3 && trackName.isEmpty())
                trackName = textOf (m).trim();
            else if (m.isText() && m.metaType() == 5)
                lyrics.push_back ({ beat, textOf (m).toStdString() });
            else if (m.isNoteOn())
            {
                auto& p = part (m.channel());
                const auto* off = seq.noteOffFor (i);
                const double end = off != nullptr ? beatOf (off->time) : beat + 0.25;
                auto* o = new juce::DynamicObject();
                o->setProperty ("pitch", m.data1());
                o->setProperty ("startBeat", beat);
                o->setProperty ("durationBeat", std::max (1.0 / 960.0, tidyBeat (end - beat)));
                o->setProperty ("velocity", std::clamp (m.data2(), 1, 127));
                p.noteIndexAt[beat].push_back ((size_t) p.notes.size());
                p.notes.add (juce::var (o));
                lastBeat = std::max (lastBeat, end);
            }
            else if (m.isProgramChange())
            {
                auto& p = part (m.channel());
                if (p.program < 0)
                    p.program = m.data1();
            }
            else if (m.isController() && ! skippedController (m.data1()))
            {
                auto& lane = part (m.channel()).lanes[m.data1()];
                const double v = m.data2();
                if (lane.empty() || lane.points.back().value != v)
                {
                    if (! lane.empty() && lane.points.back().beat == beat)
                        lane.points.back().value = v;
                    else
                        lane.points.push_back ({ beat, v, CurveShape::step });
                }
            }
            else if (m.isPitchWheel())
            {
                auto& lane = part (m.channel()).lanes[kPitchBend];
                const double v = std::clamp ((m.pitchWheelValue() - 8192) / 8192.0, -1.0, 1.0);
                if (lane.empty() || lane.points.back().value != v)
                {
                    if (! lane.empty() && lane.points.back().beat == beat)
                        lane.points.back().value = v;
                    else
                        lane.points.push_back ({ beat, std::round (v * 10000.0) / 10000.0, CurveShape::step });
                }
            }
            else if (m.isChannelPressure())
            {
                auto& lane = part (m.channel()).lanes[kAftertouch];
                const double v = m.data1();
                if (lane.empty() || lane.points.back().value != v)
                    lane.points.push_back ({ beat, v, CurveShape::step });
            }
        }
        // 가사: 같은 박에서 시작하는 음 가운데 아직 가사가 없는 첫 음에
        for (const auto& [beat, text] : lyrics)
        {
            bool placed = false;
            for (auto& p : parts)
            {
                if (p.midiTrack != t || placed)
                    continue;
                if (auto it = p.noteIndexAt.find (beat); it != p.noteIndexAt.end())
                    for (const size_t n : it->second)
                        if (auto* o = p.notes[(int) n].getDynamicObject(); o != nullptr && ! o->hasProperty ("lyric"))
                        {
                            const auto trimmed = juce::String (text).trim();
                            if (trimmed.isNotEmpty())
                            {
                                o->setProperty ("lyric", trimmed);
                                ++report.lyrics;
                            }
                            placed = true;
                            break;
                        }
            }
        }
        if (t == 0 && partOf.empty())
            conductorName = trackName;
        for (auto& [channel, index] : partOf)
        {
            auto& p = parts[index];
            p.name = trackName;
            if (partOf.size() > 1 && trackName.isNotEmpty())
                p.name << " " << channel;
        }
    }

    // ---- 곡 트리 ----
    auto* root = new juce::DynamicObject();
    juce::var rootVar (root);
    auto title = juce::String::fromUTF8 (options.title.c_str());
    if (title.isEmpty())
        title = conductorName.isNotEmpty() ? conductorName : file.getFileNameWithoutExtension();
    root->setProperty ("title", title);
    {
        auto* meta = new juce::DynamicObject();
        meta->setProperty ("importedFrom", file.getFileName());
        root->setProperty ("metadata", juce::var (meta));
    }
    root->setProperty ("tempo", tempoJson (tempo));
    if (timeSignature)
    {
        int den = std::max (1, timeSignature->second);
        if ((den & (den - 1)) != 0)
            den = 4;
        root->setProperty ("timeSignature", timeSignatureJson (std::clamp (timeSignature->first, 1, 32), std::min (den, 32)));
    }
    // 박자 바꾸기: 앞 박자의 마디 줄에 맞추고, 같은 박자가 이어지면 넣지 않는다
    std::vector<MeterChange> importedMeters;
    if (! meterAt.empty())
    {
        auto pow2 = [] (int d) { return d >= 1 && d <= 32 && (d & (d - 1)) == 0 ? d : 4; };
        const int firstNum = timeSignature ? std::clamp (timeSignature->first, 1, 32) : 4;
        const int firstDen = timeSignature ? pow2 (timeSignature->second) : 4;
        std::vector<MeterChange> changes;
        juce::Array<juce::var> arr;
        for (const auto& [beat, sig] : meterAt)
        {
            const MeterMap so (firstNum, firstDen, changes);
            const auto& seg = so.segmentAt (beat);
            const int num = std::clamp (sig.first, 1, 32), den = pow2 (sig.second);
            if (num == seg.beatsPerBar && den == seg.beatUnit)
                continue;
            const double at = so.roundToBar (beat);
            if (at <= 0.0 || (! changes.empty() && at <= changes.back().beat))
                continue;
            if (std::abs (at - beat) > 1.0e-6)
                report.warnings.push_back ("time signature change at beat " + juce::String (beat).toStdString() + " moved to the bar line at beat "
                                           + juce::String (at).toStdString());
            changes.push_back ({ at, num, den });
            auto* o = new juce::DynamicObject();
            o->setProperty ("beat", at);
            o->setProperty ("timeSignature", timeSignatureJson (num, den));
            arr.add (juce::var (o));
        }
        if (! arr.isEmpty())
            root->setProperty ("meterChanges", arr);
        importedMeters = changes;
    }
    // 조: 0 박의 것은 처음 조, 뒤의 것은 마디 줄에 맞춘 조 바꾸기 (같은 조가 이어지면 넣지 않는다)
    if (! keyAt.empty())
    {
        auto pow2 = [] (int d) { return d >= 1 && d <= 32 && (d & (d - 1)) == 0 ? d : 4; };
        const auto meters = MeterMap (timeSignature ? std::clamp (timeSignature->first, 1, 32) : 4, timeSignature ? pow2 (timeSignature->second) : 4,
                                      importedMeters);
        std::optional<KeySignature> current;
        juce::Array<juce::var> arr;
        double lastAt = 0.0;
        for (const auto& [beat, key] : keyAt)
        {
            if (beat <= 1.0e-6)
            {
                root->setProperty ("key", juce::String (keyName (key)));
                current = key;
                continue;
            }
            if (current && *current == key)
                continue;
            if (! current)
            {
                root->setProperty ("key", juce::String (keyName (key)));   // 처음 조표가 늦게 왔다
                current = key;
                continue;
            }
            const double at = meters.roundToBar (beat);
            if (at <= lastAt)
                continue;
            auto* o = new juce::DynamicObject();
            o->setProperty ("beat", at);
            o->setProperty ("key", juce::String (keyName (key)));
            arr.add (juce::var (o));
            current = key;
            lastAt = at;
        }
        if (! arr.isEmpty())
            root->setProperty ("keyChanges", arr);
    }
    if (! markers.empty())
    {
        std::stable_sort (markers.begin(), markers.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
        juce::Array<juce::var> arr;
        for (const auto& [beat, name] : markers)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("beat", beat);
            o->setProperty ("name", juce::String::fromUTF8 (name.empty() ? "marker" : name.c_str()));
            arr.add (juce::var (o));
        }
        root->setProperty ("markers", arr);
        report.markers = arr.size();
    }

    juce::Array<juce::var> tracks;
    std::set<juce::String> names;
    for (auto& p : parts)
    {
        if (p.notes.isEmpty())
            continue;   // 음이 없는 트랙(지휘 트랙, 곡선만)은 넣지 않는다
        const bool drums = p.channel == 10;
        auto instrument = instrumentForProgram (p.program < 0 ? 0 : p.program, drums, options.registry);
        if (instrument.empty())
            instrument = options.defaultInstrument;
        if (p.program < 0 && ! drums)
            instrument = options.defaultInstrument;
        juce::String name = p.name;
        if (name.isEmpty())
            name = drums ? juce::String ("Drums") : juce::String ("Track ") + juce::String ((int) tracks.size() + 1);
        if (names.count (name) > 0)
        {
            int n = 2;
            while (names.count (name + " " + juce::String (n)) > 0)
                ++n;
            name = name + " " + juce::String (n);
        }
        names.insert (name);

        auto* o = new juce::DynamicObject();
        o->setProperty ("name", name);
        o->setProperty ("instrument", juce::String (instrument));
        if (! p.lanes.empty())
        {
            auto* lanes = new juce::DynamicObject();
            for (const auto& [id, lane] : p.lanes)
                if (! lane.empty())
                {
                    lanes->setProperty (juce::Identifier (controllerName (id)), laneJson (lane));
                    report.controllerPoints += (int) lane.points.size();
                }
            if (! lanes->getProperties().isEmpty())
                o->setProperty ("automation", juce::var (lanes));
        }
        o->setProperty ("notes", p.notes);
        tracks.add (juce::var (o));
        report.notes += p.notes.size();
    }
    if (tracks.isEmpty())
        throw Failure ("midi", "the MIDI file has no notes: " + file.getFullPathName().toStdString());
    root->setProperty ("tracks", tracks);
    report.tracks = tracks.size();
    report.lengthBeats = tidyBeat (lastBeat);
    return rootVar;
}

} // namespace mr
