#include "timing/MeterMap.h"

#include <algorithm>
#include <cmath>

namespace mr
{

namespace
{
constexpr double epsilon = 1.0e-6;
}

MeterMap::MeterMap (int beatsPerBar, int beatUnit, const std::vector<MeterChange>& changes)
{
    segments_.push_back ({ 0.0, 0, std::max (1, beatsPerBar), std::max (1, beatUnit) });
    auto sorted = changes;
    std::stable_sort (sorted.begin(), sorted.end(), [] (const MeterChange& a, const MeterChange& b) { return a.beat < b.beat; });
    for (const auto& c : sorted)
    {
        if (c.beat <= segments_.back().startBeat + epsilon)
        {
            if (c.beat <= epsilon && segments_.size() == 1)
                segments_.back() = { 0.0, 0, std::max (1, c.beatsPerBar), std::max (1, c.beatUnit) };   // 0 박의 바꾸기는 처음 박자
            continue;
        }
        const auto& last = segments_.back();
        // 앞 박자의 마디 줄에 맞춘다 (검사를 지나지 않은 곡도 지도는 만든다)
        const int bars = (int) std::llround ((c.beat - last.startBeat) / last.barBeats());
        const double start = last.startBeat + bars * last.barBeats();
        if (start <= last.startBeat + epsilon)
            continue;
        segments_.push_back ({ start, last.firstBar + bars, std::max (1, c.beatsPerBar), std::max (1, c.beatUnit) });
    }
}

const MeterMap::Segment& MeterMap::segmentAt (double beat) const
{
    for (size_t i = segments_.size(); i-- > 1;)
        if (beat >= segments_[i].startBeat - epsilon)
            return segments_[i];
    return segments_.front();
}

int MeterMap::barAt (double beat) const
{
    const auto& s = segmentAt (beat);
    return s.firstBar + (int) std::floor ((beat - s.startBeat) / s.barBeats() + epsilon);
}

double MeterMap::barStart (int bar) const
{
    const Segment* s = &segments_.front();
    for (const auto& seg : segments_)
        if (bar >= seg.firstBar)
            s = &seg;
    return s->startBeat + (bar - s->firstBar) * s->barBeats();
}

double MeterMap::beatInBar (double beat) const
{
    return std::max (0.0, beat - barStart (barAt (beat)));
}

double MeterMap::roundToBar (double beat) const
{
    const int bar = barAt (beat);
    const double a = barStart (bar), b = barStart (bar + 1);
    return beat - a <= b - beat ? a : b;
}

bool MeterMap::isBarLine (double beat) const
{
    return std::abs (beat - barStart (barAt (beat))) < epsilon;
}

std::vector<double> MeterMap::barLines (double from, double to) const
{
    std::vector<double> out;
    for (int bar = std::max (0, barAt (from)); ; ++bar)
    {
        const double b = barStart (bar);
        if (b >= to)
            break;
        if (b >= from - epsilon)
            out.push_back (b);
        if (out.size() > 100000)
            break;
    }
    return out;
}

} // namespace mr
