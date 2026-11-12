// 알고리즘 리버브: 빗살 필터 여덟과 올패스 넷을 좌우에 두는 Freeverb 구조 (Jezar 의 공개 알고리즘).
// 매개변수와 크기 배율, 0.01초 선형 변화는 지금까지 쓰던 JUCE Reverb 와 같아 같은 곡이 같은 샘플로 렌더링된다.
#pragma once

#include <vector>

namespace mr::dsp
{

// 목표값으로 정해진 샘플 수에 걸쳐 일정하게 옮겨 가는 값
class LinearRamp
{
public:
    void reset (double sampleRate, double seconds);
    void setTarget (float value);
    float next();

private:
    float current_ = 0.0f, target_ = 0.0f, step_ = 0.0f;
    int countdown_ = 0, steps_ = 0;
};

class Freeverb
{
public:
    struct Parameters
    {
        float roomSize = 0.5f;   // 0~1
        float damping = 0.5f;    // 0~1
        float wetLevel = 0.33f;  // 0~1 (내부에서 3 배)
        float dryLevel = 0.4f;   // 0~1 (내부에서 2 배)
        float width = 1.0f;      // 0~1
        float freezeMode = 0.0f; // 0.5 이상이면 잔향을 붙든다
    };

    Freeverb();

    void setParameters (const Parameters& p);
    const Parameters& parameters() const noexcept { return params_; }
    void setSampleRate (double sampleRate);
    void reset();
    void processStereo (float* left, float* right, int numSamples) noexcept;

private:
    class Delay
    {
    public:
        void setSize (int size);
        void clear();
        float comb (float input, float damp, float feedback) noexcept;
        float allPass (float input) noexcept;

    private:
        std::vector<float> buffer_;
        int index_ = 0;
        float last_ = 0.0f;
    };

    static constexpr int combs = 8, allPasses = 4;
    void updateDamping();

    Parameters params_;
    float gain_ = 0.0f;
    Delay comb_[2][combs], allPass_[2][allPasses];
    LinearRamp damping_, feedback_, dry_, wet1_, wet2_;
};

} // namespace mr::dsp
