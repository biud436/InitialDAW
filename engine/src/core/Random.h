// 결정적 난수. 같은 씨앗은 어느 기계에서나 같은 수열을 낸다 (표준 라이브러리 분포를 쓰지 않는다).
#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

namespace mr
{

inline uint64_t splitmix64 (uint64_t x) noexcept
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

inline uint64_t hashString (std::string_view s) noexcept
{
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s)
    {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

inline uint64_t combineSeed (uint64_t a, uint64_t b) noexcept
{
    return splitmix64 (a ^ splitmix64 (b + 0x632BE59BD9B4E019ull));
}

class Random
{
public:
    explicit Random (uint64_t seed) noexcept : state_ (seed) {}

    uint64_t next() noexcept
    {
        state_ += 0x9E3779B97F4A7C15ull;
        uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    // [0, 1)
    double uniform() noexcept { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }

    // [-1, 1)
    double symmetric() noexcept { return uniform() * 2.0 - 1.0; }

    // 표준 정규 분포 (Box-Muller, 한 쌍 중 하나만 쓴다)
    double gaussian() noexcept
    {
        double u1 = uniform();
        if (u1 < 1.0e-300)
            u1 = 1.0e-300;
        const double u2 = uniform();
        return std::sqrt (-2.0 * std::log (u1)) * std::cos (6.283185307179586 * u2);
    }

private:
    uint64_t state_;
};

} // namespace mr
