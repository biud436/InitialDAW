#include "dsp/Resample.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mr::dsp
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
}

// 커널은 표로 만들어 두고 사이를 잇는다
void resample (const float* in, int64_t inFrames, double ratio, float* out, int64_t outFrames)
{
    constexpr int taps = 16;
    constexpr int steps = 1024;   // 입력 한 칸을 나눈 표의 칸
    const double cutoff = std::min (1.0, 1.0 / ratio);
    const int reach = (int) std::ceil (taps / cutoff);
    std::vector<float> table ((size_t) (reach * steps + 2));
    for (size_t i = 0; i < table.size(); ++i)
    {
        const double x = (double) i / steps * cutoff;   // 중심에서의 거리 (차단 주파수로 줄인 칸)
        if (x >= taps)
            continue;
        const double t = 0.5 + 0.5 * (x / taps);
        const double w = 0.42 - 0.5 * std::cos (2.0 * kPi * t) + 0.08 * std::cos (4.0 * kPi * t);
        const double sinc = x < 1.0e-12 ? 1.0 : std::sin (kPi * x) / (kPi * x);
        table[i] = (float) (sinc * w * cutoff);
    }
    auto kernel = [&table] (double distance)
    {
        const double pos = std::abs (distance) * steps;
        const auto i = (size_t) pos;
        if (i + 1 >= table.size())
            return 0.0f;
        const float frac = (float) (pos - (double) i);
        return table[i] + (table[i + 1] - table[i]) * frac;
    };
    for (int64_t j = 0; j < outFrames; ++j)
    {
        const double t = (double) j * ratio;
        const int64_t centre = (int64_t) std::floor (t);
        const int64_t k0 = std::max<int64_t> (0, centre - reach + 1), k1 = std::min<int64_t> (inFrames - 1, centre + reach);
        float sum = 0.0f;
        for (int64_t k = k0; k <= k1; ++k)
            sum += in[k] * kernel (t - (double) k);
        out[j] = sum;
    }
}


} // namespace mr::dsp
