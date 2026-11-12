#include "midi/MidiRecorder.h"

#include <algorithm>
#include <cmath>

namespace mr
{

namespace
{
constexpr double kMinLength = 0.05;
}

void MidiRecorder::start (const RecordOptions& options, double startBeat)
{
    options_ = options;
    startBeat_ = startBeat;
    notes_.clear();
    held_.clear();
    lanes_.clear();
    recording_ = true;
}

Note MidiRecorder::closedNote (int pitch, const Held& held, double endBeat) const
{
    double end = endBeat;
    if (end < held.start)   // 반복이 되돌아간 뒤에 뗐다
        end = options_.loopEndBeat && *options_.loopEndBeat > held.start ? *options_.loopEndBeat : held.start + kMinLength;
    Note n;
    n.pitch = pitch;
    n.startBeat = held.start;
    n.durationBeat = std::max (kMinLength, end - held.start);
    n.velocity = held.velocity;
    n.channel = options_.channel;
    n.humanize = false;
    return n;
}

void MidiRecorder::noteOn (double beat, int pitch, int velocity)
{
    if (! recording_)
        return;
    pitch = std::clamp (pitch, 0, 127);
    noteOff (beat, pitch);
    held_[pitch] = { std::max (0.0, beat), std::clamp (velocity, 1, 127) };
}

void MidiRecorder::noteOff (double beat, int pitch)
{
    if (! recording_)
        return;
    const auto it = held_.find (std::clamp (pitch, 0, 127));
    if (it == held_.end())
        return;
    notes_.push_back (closedNote (it->first, it->second, beat));
    held_.erase (it);
}

void MidiRecorder::controller (double beat, int controller, double value)
{
    if (! recording_ || ! options_.controllers)
        return;
    auto& lane = lanes_[controller];
    const auto curve = controller == 64 ? CurveShape::step : CurveShape::linear;
    beat = std::max (0.0, beat);
    if (! lane.points.empty())
    {
        auto& last = lane.points.back();
        if (last.value == value)
            return;
        if (std::abs (last.beat - beat) < 1.0e-9)
        {
            last.value = value;
            return;
        }
        if (beat < last.beat)   // 반복이 되돌아갔다: 박 순서로 끼워 넣는다
        {
            mergeLanePoints (lane, { { beat, value, curve } });
            return;
        }
    }
    lane.points.push_back ({ beat, value, curve });
}

std::vector<Note> MidiRecorder::notesSoFar (double nowBeat) const
{
    auto out = notes_;
    for (const auto& [pitch, held] : held_)
        out.push_back (closedNote (pitch, held, nowBeat));
    return out;
}

RecordedTake MidiRecorder::finish (double endBeat)
{
    RecordedTake take;
    if (! recording_)
        return take;
    for (const auto& [pitch, held] : held_)
        notes_.push_back (closedNote (pitch, held, endBeat));
    held_.clear();

    for (auto& n : notes_)
    {
        if (options_.quantizeGrid && *options_.quantizeGrid > 0.0)
            n.startBeat = std::max (0.0, std::round (n.startBeat / *options_.quantizeGrid) * *options_.quantizeGrid);
        if (options_.punch && (n.startBeat < options_.punch->first || n.startBeat >= options_.punch->second))
            continue;
        take.notes.push_back (n);
    }
    std::stable_sort (take.notes.begin(), take.notes.end(), [] (const Note& a, const Note& b)
    {
        return a.startBeat < b.startBeat || (a.startBeat == b.startBeat && a.pitch < b.pitch);
    });
    for (auto& [id, lane] : lanes_)
        if (! lane.points.empty())
            take.lanes[id] = std::move (lane);
    notes_.clear();
    lanes_.clear();
    recording_ = false;
    return take;
}

void MidiRecorder::cancel()
{
    notes_.clear();
    held_.clear();
    lanes_.clear();
    recording_ = false;
}

void mergeLanePoints (AutomationLane& lane, const std::vector<AutomationPoint>& points)
{
    for (const auto& p : points)
    {
        auto it = std::lower_bound (lane.points.begin(), lane.points.end(), p.beat,
                                    [] (const AutomationPoint& a, double beat) { return a.beat < beat; });
        if (it != lane.points.end() && std::abs (it->beat - p.beat) < 1.0e-9)
            *it = p;
        else
            lane.points.insert (it, p);
    }
}

} // namespace mr
