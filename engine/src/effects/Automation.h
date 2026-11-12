// 효과 매개변수와 센드 크기의 곡선을 곡의 샘플 위치에서 읽는다. 곡선 값은 곡 위치의 32 샘플 격자 점에서 재고
// 그 사이는 직선으로 잇는다. 같은 위치는 늘 같은 값이라 블록 크기, 실시간과 오프라인, 위치 옮기기와 상관없이 같다.
#pragma once

#include "composition/Composition.h"
#include "timing/TempoMap.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>

namespace mr
{

constexpr int kAutomationGrid = 32;

// s 가 든 격자 칸의 첫 샘플 (음수는 아래로 내린다)
inline int64_t automationGridStart (int64_t s) noexcept
{
    return s - (((s % kAutomationGrid) + kAutomationGrid) % kAutomationGrid);
}

// 블록의 첫 샘플이 곡의 몇째 샘플인지. held 이면 블록 안에서 나아가지 않는다 (재생을 멈춘 동안, 카운트인 동안).
struct BlockPosition
{
    int64_t start = 0;
    bool held = false;

    int64_t at (int i) const noexcept { return held ? start : start + i; }
};

// [position, position + n) 을 격자 칸으로 나눠 fn (블록 안의 시작, 샘플 수, 칸의 첫 샘플) 을 부른다
template <typename Fn>
void forEachGridStep (int64_t position, int n, Fn&& fn)
{
    for (int i = 0; i < n;)
    {
        const int64_t s = position + i;
        const int64_t grid = automationGridStart (s);
        const int count = (int) std::min<int64_t> (n - i, grid + kAutomationGrid - s);
        fn (i, count, grid);
        i += count;
    }
}

// held 이면 블록 전체가 그 위치가 든 칸 하나다
template <typename Fn>
void forEachGridStep (BlockPosition position, int n, Fn&& fn)
{
    if (position.held)
    {
        if (n > 0)
            fn (0, n, automationGridStart (position.start));
        return;
    }
    forEachGridStep (position.start, n, std::forward<Fn> (fn));
}

// 격자 칸 안의 샘플 (블록의 offset + j) 이 칸의 첫 샘플에서 떨어진 몫 (0 이상 1 미만)
inline double gridFraction (BlockPosition position, int offset, int j, int64_t grid) noexcept
{
    return (double) (position.at (offset + j) - grid) * (1.0 / kAutomationGrid);
}

// 실시간 재생의 시계(처리한 샘플 수)에서 곡 위치로. 위치 옮기기, 반복의 되돌아가기, 재생과 멈춤에서 끊긴다.
// 지연이 있는 신호(트랙 정렬, 효과의 지연)는 시계를 그만큼 거슬러 이 기록에서 제 곡 위치를 찾는다. 할당하지 않는다.
class SongTimeline
{
public:
    // 시계 clock 의 샘플부터 곡의 song 이다 (held 이면 나아가지 않는다). 앞 구간에서 그대로 이어지면 남기지 않는다.
    // clock 은 부를 때마다 앞보다 커야 한다.
    void record (int64_t clock, int64_t song, bool held) noexcept
    {
        if (count_ > 0)
        {
            const auto& last = segments_[(size_t) newest_];
            if (last.held == held && (held ? last.song == song : last.song + (clock - last.clock) == song))
                return;
        }
        newest_ = (newest_ + 1) % kSegments;
        segments_[(size_t) newest_] = { clock, song, held };
        count_ = std::min (count_ + 1, kSegments);
    }

    void clear() noexcept { count_ = 0; newest_ = -1; }

    // 시계 [clock, clock + n) 을 끊긴 자리에서 나눠 fn (블록 안의 시작, 샘플 수, 곡 위치) 을 부른다.
    // 가장 오래된 기록 앞은 그 구간을 거꾸로 늘인다 (처음 재생하는 곡의 지연된 신호는 0 번 샘플 앞이다).
    template <typename Fn>
    void forEachSpan (int64_t clock, int n, Fn&& fn) const
    {
        if (count_ == 0)
        {
            fn (0, n, BlockPosition { clock, false });
            return;
        }
        int age = 0;   // 0 이 가장 새 구간
        while (age < count_ - 1 && segment (age).clock > clock)
            ++age;
        for (int done = 0; done < n;)
        {
            const auto& s = segment (age);
            const int64_t at = clock + done;
            int count = n - done;
            if (age > 0)
                count = (int) std::min<int64_t> (count, segment (age - 1).clock - at);
            if (count > 0)
            {
                fn (done, count, BlockPosition { s.held ? s.song : s.song + (at - s.clock), s.held });
                done += count;
            }
            if (age == 0)
                break;
            --age;
        }
    }

private:
    struct Segment
    {
        int64_t clock = 0, song = 0;
        bool held = false;
    };
    static constexpr int kSegments = 128;

    const Segment& segment (int age) const noexcept { return segments_[(size_t) ((newest_ - age + kSegments) % kSegments)]; }

    std::array<Segment, kSegments> segments_ {};
    int newest_ = -1, count_ = 0;
};

class SampleLane
{
public:
    SampleLane() = default;
    // logarithmic 이면 점 사이를 값의 로그로 잇는다 (주파수의 linear 는 옥타브마다 같은 시간). 값은 0 보다 커야 한다.
    SampleLane (const AutomationLane& lane, std::shared_ptr<const TempoMap> tempo, double sampleRate, bool logarithmic);

    bool empty() const noexcept { return lane_.empty(); }
    // 격자 점의 곡선 값 (첫 점 앞은 첫 값, 마지막 점 뒤는 마지막 값)
    double atGrid (int64_t grid) const;
    // 샘플의 값: 양쪽 격자 점 값 사이의 직선
    double at (int64_t sample) const;

private:
    AutomationLane lane_;
    std::shared_ptr<const TempoMap> tempo_;
    double sampleRate_ = 48000.0;
    bool log_ = false;
    mutable int64_t cachedGrid_[2] { std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::min() };
    mutable double cachedValue_[2] { 0.0, 0.0 };
};

} // namespace mr
