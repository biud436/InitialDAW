#include "rendering/Expression.h"

#include "mixer/Mixer.h"

#include <cmath>

namespace mr
{

void ExpressionStage::configure (const TrackSpec& track, const InstrumentProfile& profile, const std::string& type,
                                 const TempoMap& tempo, double sampleRate)
{
    tempo_ = &tempo;
    sampleRate_ = sampleRate;
    spec_ = profile.dynamics;
    brightness_ = spec_.brightnessLowHz.has_value() && spec_.brightnessHighHz.has_value();
    for (const auto& [id, lane] : track.automation)
    {
        if (id >= 128 || lane.empty())
            continue;
        const auto mode = profile.modeFor (id, type);
        if (mode != ControllerMode::engine && mode != ControllerMode::both)
            continue;
        if (id == spec_.controller)
            dynamics_ = &lane;
        else if (id == 7 || id == 11)
            gainLanes_.push_back (&lane);
        else if (id == 10)
            pan_ = &lane;
    }
}

ExpressionStage::Gains ExpressionStage::valuesAt (int64_t sample) const
{
    const double beat = tempo_->beatAt (std::max<int64_t> (0, sample) / sampleRate_);
    double gain = 1.0;
    for (const auto* lane : gainLanes_)
    {
        const double v = std::clamp (lane->valueAt (beat) / 127.0, 0.0, 1.0);
        gain *= v * v;
    }
    Gains g;
    if (dynamics_ != nullptr)
    {
        const double v = std::clamp (dynamics_->valueAt (beat) / 127.0, 0.0, 1.0);
        gain *= std::pow (10.0, (spec_.gainDbLow + (spec_.gainDbHigh - spec_.gainDbLow) * v) / 20.0);
        if (brightness_)
            g.cutoff = std::exp (std::log (*spec_.brightnessLowHz) + (std::log (*spec_.brightnessHighHz) - std::log (*spec_.brightnessLowHz)) * v);
    }
    ChannelGains pan;
    if (pan_ != nullptr)
        pan = balanceGains ((pan_->valueAt (beat) - 64.0) / 63.0);
    g.left = (float) (gain * pan.left);
    g.right = (float) (gain * pan.right);
    return g;
}

void ExpressionStage::process (juce::AudioBuffer<float>& buffer, int numSamples, int64_t musicSample, bool held)
{
    if (! active())
        return;
    constexpr int step = 32;
    if (! started_)
    {
        last_ = valuesAt (musicSample);
        started_ = true;
    }
    for (int start = 0; start < numSamples; start += step)
    {
        const int n = std::min (step, numSamples - start);
        const auto next = valuesAt (held ? musicSample : musicSample + start + n);
        if (brightness_ && dynamics_ != nullptr)
        {
            // 2극 저역 통과 (TPT SVF, Q 0.707). 차단 주파수는 덩어리 끝 값으로 쓴다.
            const double cutoff = std::clamp (next.cutoff, 20.0, sampleRate_ * 0.45);
            const double g = std::tan (3.141592653589793 * cutoff / sampleRate_);
            const double k = 1.4142135623730951;
            const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
            for (int ch = 0; ch < 2; ++ch)
            {
                float* x = buffer.getWritePointer (ch, start);
                for (int i = 0; i < n; ++i)
                {
                    const double v3 = x[i] - ic2_[ch];
                    const double v1 = a1 * ic1_[ch] + a2 * v3;
                    const double v2 = ic2_[ch] + a2 * ic1_[ch] + a3 * v3;
                    ic1_[ch] = 2.0 * v1 - ic1_[ch];
                    ic2_[ch] = 2.0 * v2 - ic2_[ch];
                    x[i] = (float) v2;
                }
            }
        }
        buffer.applyGainRamp (0, start, n, last_.left, next.left);
        buffer.applyGainRamp (1, start, n, last_.right, next.right);
        last_ = next;
    }
}

} // namespace mr
