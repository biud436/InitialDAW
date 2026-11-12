// 박자 지도: 곡의 처음 박자와 곡 중간의 박자 바꾸기. 박(4분음표)과 마디(0부터) 사이를 바꾼다.
// 바꾸는 자리는 앞 박자의 마디 줄에 놓인다 (파서가 검사한다). 마지막 박자는 곡 끝 뒤로도 이어진다.
#pragma once

#include <vector>

namespace mr
{

struct MeterChange
{
    double beat = 0.0;    // 이 박부터 (앞 박자의 마디 줄)
    int beatsPerBar = 4;
    int beatUnit = 4;
};

class MeterMap
{
public:
    struct Segment
    {
        double startBeat = 0.0;
        int firstBar = 0;          // 이 박자의 첫 마디 번호 (0부터)
        int beatsPerBar = 4;
        int beatUnit = 4;
        double barBeats() const noexcept { return beatsPerBar * 4.0 / beatUnit; }
    };

    MeterMap (int beatsPerBar = 4, int beatUnit = 4, const std::vector<MeterChange>& changes = {});

    const std::vector<Segment>& segments() const noexcept { return segments_; }
    const Segment& segmentAt (double beat) const;
    double barBeatsAt (double beat) const { return segmentAt (beat).barBeats(); }
    int barAt (double beat) const;          // 그 박이 든 마디 (0부터, 0 박 앞은 첫 박자로 거꾸로 센다)
    double barStart (int bar) const;        // 마디(0부터)가 시작하는 박
    double beatInBar (double beat) const;   // 마디 처음부터의 박
    double floorToBar (double beat) const { return barStart (barAt (beat)); }
    double roundToBar (double beat) const;
    bool isBarLine (double beat) const;
    // [from, to) 의 마디 줄 박 (차례대로)
    std::vector<double> barLines (double from, double to) const;
    bool constant() const noexcept { return segments_.size() == 1; }

private:
    std::vector<Segment> segments_;
};

} // namespace mr
