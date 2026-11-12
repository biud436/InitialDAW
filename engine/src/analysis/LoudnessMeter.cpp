#include "analysis/LoudnessMeter.h"

#include "analysis/Analysis.h"

#include <algorithm>
#include <cmath>

namespace mr
{

namespace
{

float loudnessOf (double meanSquare) { return meanSquare > 0.0 ? (float) (-0.691 + 10.0 * std::log10 (meanSquare)) : LoudnessMeter::silent; }

} // namespace

void LoudnessMeter::prepare (double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    subLength_ = std::max (1, (int) std::lround (sampleRate_ * 0.1));
    (void) truePeakInterpolator();   // 정적 표를 오디오 스레드 밖에서 만든다
    resetState();
}

void LoudnessMeter::resetState() noexcept
{
    for (int ch = 0; ch < 2; ++ch)
    {
        kWeighting (sampleRate_, shelf_[ch], highpass_[ch]);
        history_[ch].fill (0.0f);
    }
    historyNext_ = 0;
    subEnergy_ = 0.0;
    subCount_ = 0;
    recent_.fill (0.0);
    recentCount_ = recentNext_ = 0;
    counts_.fill (0);
    energy_.fill (0.0);
    peak_ = 0.0f;
    samples_ = 0;
    momentary_.store (silent);
    shortTerm_.store (silent);
    integrated_.store (silent);
    truePeak_.store (silent);
    measured_.store (0.0);
}

void LoudnessMeter::finishSubBlock() noexcept
{
    recent_[(size_t) recentNext_] = subEnergy_ / (double) subLength_;
    recentNext_ = (recentNext_ + 1) % (int) recent_.size();
    recentCount_ = std::min (recentCount_ + 1, (int) recent_.size());
    subEnergy_ = 0.0;
    subCount_ = 0;
    auto meanOfLast = [this] (int n)
    {
        double sum = 0.0;
        for (int k = 1; k <= n; ++k)
            sum += recent_[(size_t) ((recentNext_ - k + (int) recent_.size()) % (int) recent_.size())];
        return sum / n;
    };
    if (recentCount_ >= 4)
    {
        // 400ms 블록 (100ms 마다 겹친다): 순간 값이고, 통합 값의 게이트 칸에 넣는다
        const double block = meanOfLast (4);
        const float l = loudnessOf (block);
        momentary_.store (l, std::memory_order_relaxed);
        if (l > lowest)
        {
            const int bin = std::clamp ((int) std::floor ((std::min (l, highest) - lowest) * binsPerLu), 0, histogramSize - 1);
            ++counts_[(size_t) bin];
            energy_[(size_t) bin] += block;
            // 통합: 절대 게이트를 지난 블록의 평균에서 10 LU 아래가 상대 게이트
            double sum = 0.0;
            uint64_t count = 0;
            for (int b = 0; b < histogramSize; ++b)
            {
                sum += energy_[(size_t) b];
                count += counts_[(size_t) b];
            }
            if (count > 0)
            {
                const float relative = loudnessOf (sum / (double) count) - 10.0f;
                double gatedSum = 0.0;
                uint64_t gatedCount = 0;
                for (int b = 0; b < histogramSize; ++b)
                {
                    const float binTop = lowest + (float) (b + 1) / binsPerLu;
                    if (counts_[(size_t) b] == 0 || binTop <= relative)
                        continue;
                    gatedSum += energy_[(size_t) b];
                    gatedCount += counts_[(size_t) b];
                }
                integrated_.store (gatedCount > 0 ? loudnessOf (gatedSum / (double) gatedCount) : silent, std::memory_order_relaxed);
            }
        }
    }
    if (recentCount_ >= (int) recent_.size())
        shortTerm_.store (loudnessOf (meanOfLast ((int) recent_.size())), std::memory_order_relaxed);
}

void LoudnessMeter::process (const float* left, const float* right, int numSamples) noexcept
{
    if (resetRequested_.exchange (false))
        resetState();
    if (left == nullptr || numSamples <= 0)
        return;
    if (right == nullptr)
        right = left;
    const auto& phases = truePeakInterpolator();   // prepare 가 만든 정적 표
    for (int i = 0; i < numSamples; ++i)
    {
        const float in[2] = { left[i], right[i] };
        for (int ch = 0; ch < 2; ++ch)
        {
            const double y = highpass_[ch].process (shelf_[ch].process (in[ch]));
            subEnergy_ += y * y;
            history_[ch][(size_t) historyNext_] = in[ch];
            peak_ = std::max (peak_, std::abs (in[ch]));
        }
        historyNext_ = (historyNext_ + 1) % 12;
        // 트루 피크: 지난 12 샘플로 샘플 사이 세 자리를 보간한다 (6 샘플 늦게)
        for (int ch = 0; ch < 2; ++ch)
            for (const auto& taps : phases)
            {
                double v = 0.0;
                for (int j = 0; j < 12; ++j)
                    v += taps[(size_t) j] * history_[ch][(size_t) ((historyNext_ + j) % 12)];
                peak_ = std::max (peak_, (float) std::abs (v));
            }
        if (++subCount_ >= subLength_)
            finishSubBlock();
    }
    samples_ += numSamples;
    truePeak_.store (peak_ > 0.0f ? (float) (20.0 * std::log10 (peak_)) : silent, std::memory_order_relaxed);
    measured_.store ((double) samples_ / sampleRate_, std::memory_order_relaxed);
}

} // namespace mr
