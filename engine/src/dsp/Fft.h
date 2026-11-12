// 실수 신호의 FFT (2의 거듭제곱 길이). macOS 의 vDSP 로 계산한다.
// 버퍼는 길이의 두 배(float 2N 개)이고, 결과는 복소수 (실수, 허수) 쌍으로 N/2+1 칸(음의 주파수까지 채우면 N 칸)에 놓인다.
// 칸의 배치와 배율은 지금까지 쓰던 JUCE dsp::FFT 의 vDSP 엔진과 같다 (같은 곡이 같은 샘플로 렌더링된다).
#pragma once

#include <cstddef>
#include <memory>

namespace mr::dsp
{

class Fft
{
public:
    explicit Fft (int order);
    ~Fft();
    Fft (Fft&&) noexcept;
    Fft& operator= (Fft&&) noexcept;
    Fft (const Fft&) = delete;
    Fft& operator= (const Fft&) = delete;

    int size() const noexcept { return size_; }

    // data: 앞 N 칸이 실수 신호. 뒤 N 칸은 작업 공간이다. ignoreNegative 면 N/2+1 칸만 채운다.
    void forwardReal (float* data, bool ignoreNegative) const noexcept;
    // forwardReal 의 결과(N/2+1 칸)를 실수 신호 N 칸으로 되돌리고 뒤 N 칸을 0 으로 지운다
    void inverseReal (float* data) const noexcept;
    // forwardReal 뒤 칸마다 크기 |X|를 data[i] 에 쓴다 (나머지는 0)
    void forwardMagnitudes (float* data, bool ignoreNegative) const noexcept;

private:
    struct Setup;
    int order_ = 0, size_ = 1;
    std::unique_ptr<Setup> setup_;
};

} // namespace mr::dsp
