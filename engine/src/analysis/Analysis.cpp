#include "analysis/Analysis.h"
#include "analysis/KWeighting.h"

#include <cmath>
#include <vector>

namespace mr
{

double gainToDb (double gain)
{
    return gain > 0.0 ? 20.0 * std::log10 (gain) : -1.0e9;
}

std::optional<double> integratedLoudness (const juce::AudioBuffer<float>& buffer, int numSamples, double fs)
{
    const int channels = buffer.getNumChannels();
    const int sub = (int) std::lround (fs * 0.1);
    if (channels == 0 || sub <= 0 || numSamples < sub * 4)
        return std::nullopt;

    const int subBlocks = numSamples / sub;
    std::vector<double> energy ((size_t) subBlocks, 0.0);   // 100ms 마다 채널 합 제곱합
    for (int ch = 0; ch < channels; ++ch)
    {
        KBiquad shelf, hp;
        kWeighting (fs, shelf, hp);
        const float* x = buffer.getReadPointer (ch);
        for (int b = 0; b < subBlocks; ++b)
        {
            double acc = 0.0;
            for (int i = b * sub, end = i + sub; i < end; ++i)
            {
                const double y = hp.process (shelf.process (x[i]));
                acc += y * y;
            }
            energy[(size_t) b] += acc;
        }
    }

    std::vector<double> blocks;   // 400ms 블록의 평균 제곱 (채널 합)
    for (int b = 0; b + 3 < subBlocks; ++b)
        blocks.push_back ((energy[(size_t) b] + energy[(size_t) b + 1] + energy[(size_t) b + 2] + energy[(size_t) b + 3]) / (4.0 * sub));

    auto loudness = [] (double z) { return -0.691 + 10.0 * std::log10 (z); };
    double sum = 0.0;
    int count = 0;
    for (double z : blocks)
        if (z > 0.0 && loudness (z) > -70.0)
        {
            sum += z;
            ++count;
        }
    if (count == 0)
        return std::nullopt;
    const double relativeGate = loudness (sum / count) - 10.0;
    sum = 0.0;
    count = 0;
    for (double z : blocks)
        if (z > 0.0 && loudness (z) > -70.0 && loudness (z) > relativeGate)
        {
            sum += z;
            ++count;
        }
    if (count == 0)
        return std::nullopt;
    return loudness (sum / count);
}

const std::vector<std::vector<double>>& truePeakInterpolator()
{
    constexpr int half = 6;   // 위상마다 탭 [-5, 6]
    static const auto coefficients = []
    {
        std::vector<std::vector<double>> c (3, std::vector<double> (2 * half, 0.0));
        for (int p = 1; p <= 3; ++p)
        {
            double sum = 0.0;
            for (int j = -half + 1; j <= half; ++j)
            {
                const double t = (double) j - p / 4.0;   // 보간점에서 j 샘플까지
                const double sinc = std::abs (t) < 1.0e-12 ? 1.0 : std::sin (3.141592653589793 * t) / (3.141592653589793 * t);
                const double w = 0.42 + 0.5 * std::cos (3.141592653589793 * t / half) + 0.08 * std::cos (2.0 * 3.141592653589793 * t / half);
                c[(size_t) p - 1][(size_t) (j + half - 1)] = sinc * w;
                sum += sinc * w;
            }
            for (auto& v : c[(size_t) p - 1])
                v /= sum;
        }
        return c;
    }();
    return coefficients;
}

double truePeak (const juce::AudioBuffer<float>& buffer, int numSamples)
{
    constexpr int half = 6;
    const auto& coefficients = truePeakInterpolator();
    double peak = 0.0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* x = buffer.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            peak = std::max (peak, std::abs ((double) x[i]));
            for (const auto& c : coefficients)
            {
                double y = 0.0;
                for (int j = -half + 1; j <= half; ++j)
                {
                    const int k = i + j;
                    if (k >= 0 && k < numSamples)
                        y += x[k] * c[(size_t) (j + half - 1)];
                }
                peak = std::max (peak, std::abs (y));
            }
        }
    }
    return peak;
}

AudioStats analyseAudio (const juce::AudioBuffer<float>& buffer, int numSamples, double sampleRate, float clipThreshold)
{
    AudioStats s;
    s.sampleRate = sampleRate;
    s.channels = buffer.getNumChannels();
    s.samples = numSamples;
    s.durationSeconds = sampleRate > 0.0 ? numSamples / sampleRate : 0.0;

    double peak = 0.0, power = 0.0;
    for (int ch = 0; ch < s.channels; ++ch)
    {
        const float* x = buffer.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            const double a = std::abs ((double) x[i]);
            peak = std::max (peak, a);
            power += a * a;
            if (a >= clipThreshold)
                ++s.clippedSamples;
        }
    }
    s.peakDb = gainToDb (peak);
    s.truePeakDb = gainToDb (truePeak (buffer, numSamples));
    if (s.channels > 0 && numSamples > 0 && power > 0.0)
        s.rmsDb = 10.0 * std::log10 (power / ((double) s.channels * numSamples));
    s.clipping = s.clippedSamples > 0;
    s.lufs = integratedLoudness (buffer, numSamples, sampleRate);
    return s;
}

} // namespace mr
