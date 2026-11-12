// 박과 초의 변환. 템포는 구간마다 고정이거나(ramp=false) 다음 점까지 bpm 이 선형으로 바뀐다(ramp=true).
#pragma once

#include "composition/Composition.h"

#include <cstdint>
#include <vector>

namespace mr
{

class TempoMap
{
public:
    explicit TempoMap (const std::vector<TempoPoint>& points);

    double secondsAt (double beat) const;
    double beatAt (double seconds) const;
    double bpmAt (double beat) const;
    int64_t sampleAt (double beat, double sampleRate) const;
    // 곡 전체에서 가장 느린 템포 (점 사이의 램프는 양 끝 중 하나가 가장 느리다)
    double slowestBpm() const;
    const std::vector<TempoPoint>& points() const noexcept { return points_; }

private:
    struct Segment
    {
        double beat0 = 0.0, beat1 = 0.0;   // 마지막 구간의 beat1 은 무한대
        double bpm0 = 120.0, bpm1 = 120.0;
        double sec0 = 0.0;
        bool ramp = false;
    };

    static double secondsInto (const Segment& s, double beat);
    static double beatsInto (const Segment& s, double seconds);
    const Segment& segmentForBeat (double beat) const;

    std::vector<Segment> segments_;
    std::vector<TempoPoint> points_;
};

} // namespace mr
