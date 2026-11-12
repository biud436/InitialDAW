// 실시간 라우드니스 미터 (ITU-R BS.1770-4, EBU R128): 순간(400ms), 단기(3초), 통합(절대 -70, 상대 -10 게이트), 트루 피크.
// 오디오 스레드가 process 를 부르고(할당, 잠금 없음) 다른 스레드는 값을 읽는다. 통합 값은 0.1 LU 칸의 막대그래프로 게이트한다.
#pragma once

#include "analysis/KWeighting.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace mr
{

class LoudnessMeter
{
public:
    static constexpr float silent = -1000.0f;

    void prepare (double sampleRate);            // 처음과 샘플레이트가 바뀔 때 (오디오 스레드가 돌지 않을 때)
    void requestReset() noexcept { resetRequested_.store (true); }   // 어느 스레드든: 다음 process 에서 처음부터
    void process (const float* left, const float* right, int numSamples) noexcept;

    float momentary() const noexcept { return momentary_.load (std::memory_order_relaxed); }    // LUFS, 없으면 silent
    float shortTerm() const noexcept { return shortTerm_.load (std::memory_order_relaxed); }
    float integrated() const noexcept { return integrated_.load (std::memory_order_relaxed); }
    float truePeakDb() const noexcept { return truePeak_.load (std::memory_order_relaxed); }    // 다시 재기부터의 가장 큰 값
    double measuredSeconds() const noexcept { return measured_.load (std::memory_order_relaxed); }

private:
    static constexpr int binsPerLu = 10;
    static constexpr float lowest = -70.0f, highest = 10.0f;
    static constexpr int histogramSize = (int) ((highest - lowest) * binsPerLu) + 1;

    void resetState() noexcept;
    void finishSubBlock() noexcept;

    double sampleRate_ = 48000.0;
    int subLength_ = 4800;                       // 100ms
    KBiquad shelf_[2], highpass_[2];
    double subEnergy_ = 0.0;
    int subCount_ = 0;
    std::array<double, 30> recent_ {};           // 지난 100ms 칸들의 평균 제곱 (3초)
    int recentCount_ = 0, recentNext_ = 0;
    std::array<uint32_t, histogramSize> counts_ {};
    std::array<double, histogramSize> energy_ {};
    std::array<float, 12> history_[2] {};        // 트루 피크 보간의 지난 샘플
    int historyNext_ = 0;
    float peak_ = 0.0f;
    int64_t samples_ = 0;

    std::atomic<bool> resetRequested_ { false };
    std::atomic<float> momentary_ { silent }, shortTerm_ { silent }, integrated_ { silent }, truePeak_ { silent };
    std::atomic<double> measured_ { 0.0 };
};

} // namespace mr
