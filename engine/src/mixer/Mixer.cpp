#include "mixer/Mixer.h"

#include <cmath>

namespace mr
{

ChannelGains balanceGains (double pan)
{
    ChannelGains g;
    const double p = std::max (-1.0, std::min (1.0, pan));
    if (p > 0.0)
        g.left = (float) std::cos (p * 1.5707963267948966);
    else if (p < 0.0)
        g.right = (float) std::cos (-p * 1.5707963267948966);
    if (std::abs (g.left) < 1.0e-7f) g.left = 0.0f;
    if (std::abs (g.right) < 1.0e-7f) g.right = 0.0f;
    return g;
}

ChannelGains faderGains (const MixerSettings& m)
{
    auto g = balanceGains (m.pan);
    const double db = m.totalGainDb();
    const float gain = m.gainDb <= -120.0 || db <= -120.0 ? 0.0f : (float) std::pow (10.0, db / 20.0);
    g.left *= gain;
    g.right *= gain;
    return g;
}

FaderLanes makeFaderLanes (const MixerSettings& m, const TempoMap& tempo, double sampleRate)
{
    FaderLanes f;
    f.gainDb = m.gainDb;
    f.vcaDb = m.vcaDb;
    f.panValue = m.pan;
    if (m.gainLane.empty() && m.panLane.empty() && m.vcaLane.empty())
        return f;
    const auto shared = std::make_shared<const TempoMap> (tempo);
    if (! m.gainLane.empty())
        f.gain = SampleLane (m.gainLane, shared, sampleRate, false);
    if (! m.vcaLane.empty())
        f.vca = SampleLane (m.vcaLane, shared, sampleRate, false);
    if (! m.panLane.empty())
        f.pan = SampleLane (m.panLane, shared, sampleRate, false);
    return f;
}

namespace
{

ChannelGains gainsAtGrid (const FaderLanes& f, int64_t grid)
{
    MixerSettings m;
    m.gainDb = f.gain.empty() ? f.gainDb : f.gain.atGrid (grid);
    m.vcaDb = f.vca.empty() ? f.vcaDb : f.vca.atGrid (grid);
    m.pan = f.pan.empty() ? f.panValue : f.pan.atGrid (grid);
    return faderGains (m);
}

} // namespace

ChannelGains faderGainsAt (const FaderLanes& f, int64_t position)
{
    const int64_t grid = automationGridStart (position);
    const auto g0 = gainsAtGrid (f, grid), g1 = gainsAtGrid (f, grid + kAutomationGrid);
    const float t = (float) ((double) (position - grid) * (1.0 / kAutomationGrid));
    return { g0.left + (g1.left - g0.left) * t, g0.right + (g1.right - g0.right) * t };
}

void applyFaderLanes (juce::AudioBuffer<float>& b, int start, int n, const FaderLanes& f, BlockPosition position)
{
    if (position.held)
    {
        const auto g = faderGainsAt (f, position.start);
        b.applyGain (0, start, n, g.left);
        b.applyGain (1, start, n, g.right);
        return;
    }
    forEachGridStep (position.start, n, [&] (int offset, int count, int64_t)
    {
        const auto g0 = faderGainsAt (f, position.start + offset), g1 = faderGainsAt (f, position.start + offset + count);
        b.applyGainRamp (0, start + offset, count, g0.left, g1.left);
        b.applyGainRamp (1, start + offset, count, g0.right, g1.right);
    });
}

void DelayLine::prepare (int channels, int delaySamples)
{
    delay_ = std::max (0, delaySamples);
    position_ = 0;
    lines_.assign ((size_t) channels, std::vector<float> ((size_t) std::max (1, delay_), 0.0f));
}

void DelayLine::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (delay_ == 0)
        return;
    for (int ch = 0; ch < (int) lines_.size() && ch < buffer.getNumChannels(); ++ch)
    {
        auto& line = lines_[(size_t) ch];
        float* x = buffer.getWritePointer (ch);
        int pos = position_;
        for (int i = 0; i < numSamples; ++i)
        {
            const float out = line[(size_t) pos];
            line[(size_t) pos] = x[i];
            x[i] = out;
            pos = (pos + 1) % delay_;
        }
    }
    position_ = (position_ + numSamples) % delay_;
}

std::string stemFileName (const std::string& name)
{
    std::string out;
    for (unsigned char c : name)
        out += (std::isalnum (c) || c == '-' || c == '_' || c == '.' || c >= 0x80) ? (char) c : '_';
    if (out.empty() || out == "." || out == "..")
        out = "track";
    return out + ".wav";
}

} // namespace mr
