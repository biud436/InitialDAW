#include "timing/TempoMap.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mr
{

TempoMap::TempoMap (const std::vector<TempoPoint>& points)
{
    points_ = points;
    std::vector<TempoPoint> pts = points;
    if (pts.empty())
        pts.push_back ({ 0.0, 120.0, false });
    if (pts.front().beat > 0.0)
        pts.insert (pts.begin(), { 0.0, pts.front().bpm, false });

    double sec = 0.0;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        Segment s;
        s.beat0 = pts[i].beat;
        s.bpm0 = pts[i].bpm;
        const bool last = i + 1 == pts.size();
        s.beat1 = last ? std::numeric_limits<double>::infinity() : pts[i + 1].beat;
        s.ramp = ! last && pts[i].ramp && pts[i + 1].bpm != pts[i].bpm;
        s.bpm1 = s.ramp ? pts[i + 1].bpm : s.bpm0;
        s.sec0 = sec;
        if (! last)
            sec += secondsInto (s, s.beat1);
        segments_.push_back (s);
    }
}

double TempoMap::secondsInto (const Segment& s, double beat)
{
    const double db = beat - s.beat0;
    if (! s.ramp)
        return 60.0 * db / s.bpm0;
    const double k = (s.bpm1 - s.bpm0) / (s.beat1 - s.beat0);   // bpm per beat
    const double bpm = s.bpm0 + k * db;
    return 60.0 / k * std::log (bpm / s.bpm0);
}

double TempoMap::beatsInto (const Segment& s, double seconds)
{
    if (! s.ramp)
        return seconds * s.bpm0 / 60.0;
    const double k = (s.bpm1 - s.bpm0) / (s.beat1 - s.beat0);
    return (s.bpm0 * std::exp (k * seconds / 60.0) - s.bpm0) / k;
}

const TempoMap::Segment& TempoMap::segmentForBeat (double beat) const
{
    for (size_t i = segments_.size(); i-- > 0;)
        if (beat >= segments_[i].beat0)
            return segments_[i];
    return segments_.front();
}

double TempoMap::secondsAt (double beat) const
{
    if (beat <= 0.0)
        return 60.0 * beat / segments_.front().bpm0;
    const auto& s = segmentForBeat (beat);
    return s.sec0 + secondsInto (s, beat);
}

double TempoMap::beatAt (double seconds) const
{
    if (seconds <= 0.0)
        return seconds * segments_.front().bpm0 / 60.0;
    for (size_t i = segments_.size(); i-- > 0;)
    {
        const auto& s = segments_[i];
        if (seconds >= s.sec0)
            return s.beat0 + beatsInto (s, seconds - s.sec0);
    }
    return 0.0;
}

double TempoMap::bpmAt (double beat) const
{
    const auto& s = segmentForBeat (beat);
    if (! s.ramp)
        return s.bpm0;
    return s.bpm0 + (s.bpm1 - s.bpm0) * (beat - s.beat0) / (s.beat1 - s.beat0);
}

double TempoMap::slowestBpm() const
{
    double slowest = segments_.front().bpm0;
    for (const auto& s : segments_)
        slowest = std::min ({ slowest, s.bpm0, s.bpm1 });
    return slowest;
}

int64_t TempoMap::sampleAt (double beat, double sampleRate) const
{
    return (int64_t) std::llround (secondsAt (beat) * sampleRate);
}

} // namespace mr
