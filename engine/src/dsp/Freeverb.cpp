#include "dsp/Freeverb.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mr::dsp
{

namespace
{
// 거의 같은 두 값 (상대 오차가 float 의 엡실론 안)이면 목표를 바꾸지 않는다
bool nearlyEqual (float a, float b) noexcept
{
    if (! (std::isfinite (a) && std::isfinite (b)))
        return a == b;
    const float diff = std::abs (a - b);
    return diff <= std::numeric_limits<float>::min()
        || diff <= std::numeric_limits<float>::epsilon() * std::max (std::abs (a), std::abs (b));
}

// x86 에서는 비정규 수를 없애려고 작은 값을 더했다 뺀다 (ARM 은 하드웨어가 처리한다)
inline void flushDenormal (float& x) noexcept
{
#if defined(__x86_64__) || defined(__i386__)
    x += 0.1f;
    x -= 0.1f;
#else
    (void) x;
#endif
}

constexpr int combTunings[] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };   // 44100 Hz 의 샘플 수
constexpr int allPassTunings[] = { 556, 441, 341, 225 };
constexpr int stereoSpread = 23;
} // namespace

void LinearRamp::reset (double sampleRate, double seconds)
{
    steps_ = (int) std::floor (seconds * sampleRate);
    current_ = target_;
    countdown_ = 0;
}

void LinearRamp::setTarget (float value)
{
    if (nearlyEqual (value, target_))
        return;
    if (steps_ <= 0)
    {
        current_ = target_ = value;
        countdown_ = 0;
        return;
    }
    target_ = value;
    countdown_ = steps_;
    step_ = (target_ - current_) / (float) countdown_;
}

float LinearRamp::next()
{
    if (countdown_ <= 0)
        return target_;
    --countdown_;
    if (countdown_ > 0)
        current_ += step_;
    else
        current_ = target_;
    return current_;
}

void Freeverb::Delay::setSize (int size)
{
    if (size != (int) buffer_.size())
    {
        index_ = 0;
        buffer_.assign ((size_t) size, 0.0f);
    }
    clear();
}

void Freeverb::Delay::clear()
{
    last_ = 0.0f;
    std::fill (buffer_.begin(), buffer_.end(), 0.0f);
}

float Freeverb::Delay::comb (float input, float damp, float feedback) noexcept
{
    const float out = buffer_[(size_t) index_];
    last_ = (out * (1.0f - damp)) + (last_ * damp);
    flushDenormal (last_);
    float stored = input + (last_ * feedback);
    flushDenormal (stored);
    buffer_[(size_t) index_] = stored;
    index_ = (index_ + 1) % (int) buffer_.size();
    return out;
}

float Freeverb::Delay::allPass (float input) noexcept
{
    const float held = buffer_[(size_t) index_];
    float stored = input + (held * 0.5f);
    flushDenormal (stored);
    buffer_[(size_t) index_] = stored;
    index_ = (index_ + 1) % (int) buffer_.size();
    return held - input;
}

Freeverb::Freeverb()
{
    setParameters (Parameters());
    setSampleRate (44100.0);
}

void Freeverb::setParameters (const Parameters& p)
{
    const float wet = p.wetLevel * 3.0f;
    dry_.setTarget (p.dryLevel * 2.0f);
    wet1_.setTarget (0.5f * wet * (1.0f + p.width));
    wet2_.setTarget (0.5f * wet * (1.0f - p.width));
    gain_ = p.freezeMode >= 0.5f ? 0.0f : 0.015f;
    params_ = p;
    updateDamping();
}

void Freeverb::updateDamping()
{
    if (params_.freezeMode >= 0.5f)
    {
        damping_.setTarget (0.0f);
        feedback_.setTarget (1.0f);
    }
    else
    {
        damping_.setTarget (params_.damping * 0.4f);
        feedback_.setTarget (params_.roomSize * 0.28f + 0.7f);
    }
}

void Freeverb::setSampleRate (double sampleRate)
{
    const int rate = (int) sampleRate;
    for (int i = 0; i < combs; ++i)
    {
        comb_[0][i].setSize ((rate * combTunings[i]) / 44100);
        comb_[1][i].setSize ((rate * (combTunings[i] + stereoSpread)) / 44100);
    }
    for (int i = 0; i < allPasses; ++i)
    {
        allPass_[0][i].setSize ((rate * allPassTunings[i]) / 44100);
        allPass_[1][i].setSize ((rate * (allPassTunings[i] + stereoSpread)) / 44100);
    }
    for (auto* ramp : { &damping_, &feedback_, &dry_, &wet1_, &wet2_ })
        ramp->reset (sampleRate, 0.01);
}

void Freeverb::reset()
{
    for (int c = 0; c < 2; ++c)
    {
        for (auto& d : comb_[c])
            d.clear();
        for (auto& d : allPass_[c])
            d.clear();
    }
}

void Freeverb::processStereo (float* left, float* right, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const float input = (left[i] + right[i]) * gain_;
        float outL = 0.0f, outR = 0.0f;
        const float damp = damping_.next();
        const float feedback = feedback_.next();
        for (int j = 0; j < combs; ++j)
        {
            outL += comb_[0][j].comb (input, damp, feedback);
            outR += comb_[1][j].comb (input, damp, feedback);
        }
        for (int j = 0; j < allPasses; ++j)
        {
            outL = allPass_[0][j].allPass (outL);
            outR = allPass_[1][j].allPass (outR);
        }
        const float dry = dry_.next();
        const float wet1 = wet1_.next();
        const float wet2 = wet2_.next();
        left[i] = outL * wet1 + outR * wet2 + left[i] * dry;
        right[i] = outR * wet1 + outL * wet2 + right[i] * dry;
    }
}

} // namespace mr::dsp
