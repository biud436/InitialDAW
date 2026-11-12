#include "media/Timecode.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace mr::media
{

int FrameRate::nominal() const noexcept
{
    return valid() ? (int) std::lround (fps()) : 0;
}

bool FrameRate::dropFrame() const noexcept
{
    return den == 1001 && (num == 30000 || num == 60000);
}

FrameRate frameRateFromFps (double fps)
{
    if (! (fps > 0.0))
        return { 0, 1 };
    const double whole = std::round (fps);
    if (std::abs (fps - whole) < 1.0e-3)
        return { (int) whole, 1 };
    // NTSC 계열: n * 1000 / 1001
    const double ntsc = std::round (fps * 1.001);
    if (std::abs (fps - ntsc / 1.001) < 2.0e-3)
        return { (int) ntsc * 1000, 1001 };
    return { (int) std::lround (fps * 1000.0), 1000 };
}

int64_t frameAtSeconds (double seconds, FrameRate rate)
{
    if (! rate.valid())
        return 0;
    // 프레임 경계의 반올림 오차를 막는다 (1 마이크로초 안은 다음 프레임)
    return (int64_t) std::floor (seconds * rate.num / rate.den + 1.0e-6);
}

double secondsAtFrame (int64_t frame, FrameRate rate)
{
    return rate.valid() ? (double) frame * rate.den / rate.num : 0.0;
}

std::string timecode (int64_t frame, FrameRate rate, bool dropFrame)
{
    const int fps = rate.nominal();
    if (fps <= 0)
        return "--:--:--:--";
    const bool negative = frame < 0;
    int64_t n = std::llabs (frame);
    const bool drop = dropFrame && rate.dropFrame();
    if (drop)
    {
        // SMPTE 12M: 분마다 앞 프레임 번호 둘(59.94 는 넷)을 건너뛰고 10분째는 건너뛰지 않는다
        const int64_t dropped = fps / 15;                       // 2 또는 4
        const int64_t perMinute = fps * 60 - dropped;           // 1798, 3596
        const int64_t perTen = perMinute * 10 + dropped;        // 17982, 35964
        const int64_t tens = n / perTen, rest = n % perTen;
        n += dropped * 9 * tens;
        if (rest > dropped)
            n += dropped * ((rest - dropped) / perMinute);
    }
    const int64_t ff = n % fps;
    const int64_t totalSeconds = n / fps;
    const int64_t ss = totalSeconds % 60, mm = (totalSeconds / 60) % 60, hh = totalSeconds / 3600;
    char text[48];
    std::snprintf (text, sizeof (text), "%s%02lld:%02lld:%02lld%c%02lld", negative ? "-" : "", (long long) hh, (long long) mm, (long long) ss,
                   drop ? ';' : ':', (long long) ff);
    return text;
}

std::string timecodeAt (double seconds, FrameRate rate)
{
    return timecode (frameAtSeconds (seconds, rate), rate, rate.dropFrame());
}

} // namespace mr::media
