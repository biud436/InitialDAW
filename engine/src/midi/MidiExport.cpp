#include "midi/MidiExport.h"

#include "core/Diagnostics.h"
#include "performance/Performance.h"
#include "timing/TempoMap.h"

#include "midi/Smf.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace mr
{

MidiExportReport exportMidi (const Composition& c, const juce::File& output, bool humanized, std::optional<uint64_t> seed)
{
    constexpr int ppq = 960;
    MidiExportReport report;
    const TempoMap tempo (c.tempo);
    auto ticks = [] (double beat) { return std::max (0.0, std::round (beat * ppq)); };

    midi::File file;
    file.format = 1;
    file.timeFormat = ppq;

    midi::Sequence conductor;
    conductor.add (midi::Event::textMeta (3, c.title.empty() ? std::string ("music-render") : c.title, 0));
    conductor.add (midi::Event::timeSignature (c.beatsPerBar, c.beatUnit, 0));
    for (const auto& change : c.meterChanges)
        conductor.add (midi::Event::timeSignature (change.beatsPerBar, change.beatUnit, ticks (change.beat)));
    // 조표 (FF 59)
    if (c.key)
        conductor.add (midi::Event::keySignature (c.key->fifths, c.key->minor, 0));
    for (const auto& change : c.keyChanges)
        conductor.add (midi::Event::keySignature (change.key.fifths, change.key.minor, ticks (change.beat)));
    const auto meters = c.meters();
    for (size_t i = 0; i < c.tempo.size(); ++i)
    {
        const auto& p = c.tempo[i];
        const bool rampToNext = p.ramp && i + 1 < c.tempo.size();
        const double end = rampToNext ? c.tempo[i + 1].beat : p.beat;
        // 램프는 16분음표마다 계단으로 적는다
        for (double b = p.beat; b <= end + 1.0e-9; b += 0.25)
        {
            conductor.add (midi::Event::tempo ((int) std::lround (60000000.0 / tempo.bpmAt (b)), ticks (b)));
            if (! rampToNext)
                break;
        }
    }
    // 마커 (FF 06)
    for (const auto& marker : c.markers)
    {
        conductor.add (midi::Event::textMeta (6, marker.name, ticks (marker.beat)));
        ++report.markers;
    }
    conductor.sort();
    file.tracks.push_back (std::move (conductor));

    double lastBeat = 0.0;
    for (const auto& t : c.tracks)
    {
        midi::Sequence seq;
        seq.add (midi::Event::textMeta (3, t.name, 0));

        const auto perf = resolvePerformance (c.performance, t.performance, seed);
        PerformanceContext ctx { tempo, c.barBeats(), t.name, t.transpose };
        ctx.meters = &meters;
        // 가사 메타 이벤트(FF 05, UTF-8)를 음 켜짐 앞에 둔다. 이어 부르는 "-" 는 적지 않는다
        auto addLyric = [&seq, &report] (const std::string& lyric, double tick)
        {
            if (lyric.empty() || isLyricExtension (lyric))
                return;
            seq.add (midi::Event::textMeta (5, lyric, tick));
            ++report.lyrics;
        };
        if (humanized)
        {
            for (const auto& n : perform (t.notes, perf, ctx))
            {
                const double b0 = tempo.beatAt (n.startSec), b1 = tempo.beatAt (n.endSec);
                if (n.sourceIndex < t.notes.size())
                    addLyric (t.notes[n.sourceIndex].lyric, ticks (b0));
                seq.add (midi::Event::noteOn (n.channel, n.pitch, n.velocity, ticks (b0)));
                seq.add (midi::Event::noteOff (n.channel, n.pitch, std::max (ticks (b1), ticks (b0) + 1)));
                lastBeat = std::max (lastBeat, b1);
                ++report.notes;
            }
        }
        else
        {
            for (const auto& n : t.notes)
            {
                const int pitch = std::clamp (n.pitch + t.transpose, 0, 127);
                addLyric (n.lyric, ticks (n.startBeat));
                seq.add (midi::Event::noteOn (n.channel, pitch, n.velocity, ticks (n.startBeat)));
                seq.add (midi::Event::noteOff (n.channel, pitch, std::max (ticks (n.startBeat + n.durationBeat), ticks (n.startBeat) + 1)));
                lastBeat = std::max (lastBeat, n.startBeat + n.durationBeat);
                ++report.notes;
            }
        }
        // 컨트롤러 곡선: 32분음표마다와 꺾이는 점에서 읽어 값이 바뀔 때 적는다
        for (const auto& [id, lane] : t.automation)
        {
            if (lane.empty())
                continue;
            std::vector<double> beats;
            for (double b = lane.points.front().beat; b <= lane.points.back().beat + 1.0e-9; b += 0.125)
                beats.push_back (b);
            for (const auto& p : lane.points)
                beats.push_back (p.beat);
            beats.push_back (0.0);
            std::sort (beats.begin(), beats.end());
            int last = -1;
            for (double b : beats)
            {
                const double v = lane.valueAt (b);
                int q = 0;
                if (id == kPitchBend)
                    q = std::clamp ((int) std::lround ((v + 1.0) * 8191.5), 0, 16383);
                else
                    q = std::clamp ((int) std::lround (v), 0, 127);
                if (q == last)
                    continue;
                last = q;
                double at = b;
                if (humanized)
                    at = tempo.beatAt (tempo.secondsAt (b));
                if (id == kPitchBend)
                    seq.add (midi::Event::pitchWheel (1, q, ticks (at)));
                else if (id == kAftertouch)
                    seq.add (midi::Event::channelPressure (1, q, ticks (at)));
                else
                    seq.add (midi::Event::controller (1, id, q, ticks (at)));
            }
        }
        seq.sort();
        seq.matchNoteOffs();
        file.tracks.push_back (std::move (seq));
        ++report.tracks;
    }
    report.lengthBeats = lastBeat;

    const auto parent = output.getParentDirectory();
    if (! parent.isDirectory() && ! parent.createDirectory())
        throw Failure ("io", "cannot create directory " + parent.getFullPathName().toStdString());
    const auto bytes = midi::write (file);
    std::ofstream stream (output.getFullPathName().toStdString(), std::ios::binary | std::ios::trunc);
    stream.write (reinterpret_cast<const char*> (bytes.data()), (std::streamsize) bytes.size());
    if (! stream)
        throw Failure ("io", "cannot write " + output.getFullPathName().toStdString());
    return report;
}

} // namespace mr
