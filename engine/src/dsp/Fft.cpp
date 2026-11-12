#include "dsp/Fft.h"

#include <Accelerate/Accelerate.h>

#include <cmath>
#include <complex>
#include <cstring>

namespace mr::dsp
{

struct Fft::Setup
{
    FFTSetup handle = nullptr;
    ~Setup()
    {
        if (handle != nullptr)
            vDSP_destroy_fftsetup (handle);
    }
};

namespace
{
// 실수와 허수가 번갈아 놓인 버퍼를 vDSP 의 나뉜 복소수로 본다 (간격 2)
DSPSplitComplex interleaved (float* data) noexcept { return { data, data + 1 }; }
} // namespace

Fft::Fft (int order) : order_ (order), size_ (1 << order), setup_ (std::make_unique<Setup>())
{
    // 기수 5 설정은 기수 2 표도 함께 만든다 (이전 엔진과 같은 설정)
    setup_->handle = vDSP_create_fftsetup ((vDSP_Length) order, kFFTRadix5);
}

Fft::~Fft() = default;
Fft::Fft (Fft&&) noexcept = default;
Fft& Fft::operator= (Fft&&) noexcept = default;

void Fft::forwardReal (float* data, bool ignoreNegative) const noexcept
{
    const auto n = (vDSP_Length) size_;
    auto split = interleaved (data);
    data[size_] = 0.0f;
    vDSP_fft_zrip (setup_->handle, &split, 2, (vDSP_Length) order_, kFFTDirection_Forward);
    const float half = 0.5f;
    vDSP_vsmul (data, 1, &half, data, 1, n * 2);

    // vDSP 는 나이퀴스트의 실수부를 0 번 칸의 허수부에 둔다. 제자리로 옮긴다.
    auto* bins = reinterpret_cast<std::complex<float>*> (data);
    const int middle = size_ / 2;
    bins[middle] = { bins[0].imag(), 0.0f };
    bins[0] = { bins[0].real(), 0.0f };
    if (! ignoreNegative)
        for (int i = middle + 1; i < size_; ++i)
            bins[i] = std::conj (bins[size_ - i]);
}

void Fft::inverseReal (float* data) const noexcept
{
    const auto n = (vDSP_Length) size_;
    auto* bins = reinterpret_cast<std::complex<float>*> (data);
    if (size_ != 1)
        bins[0] = { bins[0].real(), bins[size_ / 2].real() };
    auto split = interleaved (data);
    vDSP_fft_zrip (setup_->handle, &split, 2, (vDSP_Length) order_, kFFTDirection_Inverse);
    const float scale = 1.0f / (float) size_;
    vDSP_vsmul (data, 1, &scale, data, 1, n * 2);
    vDSP_vclr (data + size_, 1, n);
}

void Fft::forwardMagnitudes (float* data, bool ignoreNegative) const noexcept
{
    if (size_ == 1)
        return;
    forwardReal (data, ignoreNegative);
    const auto* bins = reinterpret_cast<const std::complex<float>*> (data);
    const int limit = ignoreNegative ? size_ / 2 + 1 : size_;
    for (int i = 0; i < limit; ++i)
        data[i] = std::abs (bins[i]);
    std::memset (data + limit, 0, (size_t) (size_ * 2 - limit) * sizeof (float));
}

} // namespace mr::dsp
