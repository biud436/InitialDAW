// 영상의 프레임 빠르기와 타임코드 (시:분:초:프레임). 29.97 과 59.94 는 드롭 프레임 (프레임 앞의 구분이 ;).
#pragma once

#include <cstdint>
#include <string>

namespace mr::media
{

// 프레임 빠르기를 분수로 (29.97 은 30000/1001)
struct FrameRate
{
    int num = 30;
    int den = 1;

    double fps() const noexcept { return den > 0 ? (double) num / den : 0.0; }
    double frameSeconds() const noexcept { return num > 0 ? (double) den / num : 0.0; }
    int nominal() const noexcept;      // 타임코드의 한 초 프레임 수 (29.97 은 30)
    bool dropFrame() const noexcept;   // 29.97 과 59.94 (분마다 프레임 번호를 건너뛴다)
    bool valid() const noexcept { return num > 0 && den > 0; }
};

// 초에 가까운 분수를 고른다 (23.976, 29.97, 59.94 는 1001 분모, 그 밖은 정수나 1000 분모)
FrameRate frameRateFromFps (double fps);

// 그 시각에 보이는 프레임 (0 부터)
int64_t frameAtSeconds (double seconds, FrameRate rate);
double secondsAtFrame (int64_t frame, FrameRate rate);

// 프레임 번호의 타임코드. 음수는 앞에 '-'. dropFrame 이면 드롭 프레임 (rate 가 29.97 이나 59.94 일 때만)
std::string timecode (int64_t frame, FrameRate rate, bool dropFrame);
// 시각의 타임코드 (29.97, 59.94 는 드롭 프레임)
std::string timecodeAt (double seconds, FrameRate rate);

} // namespace mr::media
