#include "analysis/MixMeasure.h"

#include "dsp/Fft.h"

#include <algorithm>
#include <cmath>

namespace mr
{

namespace
{
constexpr int frameOrder = 13;   // 8192 (5.9 Hz 칸이라 30, 60, 120 Hz 경계를 가른다)
} // namespace

double powerDb (double power)
{
    return power > 1.0e-12 ? 10.0 * std::log10 (power) : -120.0;
}

const std::array<double, measureBandCount + 1>& measureBandEdges (double sampleRate)
{
    static thread_local std::array<double, measureBandCount + 1> edges {};
    edges = { 0.0, 30.0, 60.0, 120.0, 250.0, 400.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0, sampleRate / 2.0 };
    for (size_t i = 1; i < edges.size(); ++i)
        edges[i] = std::min (edges[i], sampleRate / 2.0);
    return edges;
}

StemMeter::StemMeter (double sampleRate)
    : sampleRate_ (sampleRate), hop_ (std::max (1, (int) std::lround (0.01 * sampleRate)))
{
    for (auto& p : pending_)
        p.reserve ((size_t) frame_);
    fftData_.assign ((size_t) frame_ * 2, 0.0f);
    window_.resize ((size_t) frame_);
    for (int i = 0; i < frame_; ++i)
        window_[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * 3.141592653589793 * (i + 0.5) / frame_));
    edges_ = measureBandEdges (sampleRate);
}

void StemMeter::add (const juce::AudioBuffer<float>& buffer, int start, int count)
{
    const float* l = buffer.getReadPointer (0);
    const float* r = buffer.getReadPointer (buffer.getNumChannels() > 1 ? 1 : 0);
    for (int i = start; i < start + count; ++i)
    {
        const double a = l[i], b = r[i];
        hopSum_ += 0.5 * (a * a + b * b);
        const double side = 0.5 * (a - b);
        hopSideSum_ += side * side;
        if (++hopFill_ == hop_)
            flushHop();
        pending_[0].push_back (l[i]);
        pending_[1].push_back (r[i]);
        ++written_;
        if ((int) pending_[0].size() == frame_)
            flushFrame();
    }
}

void StemMeter::flushHop()
{
    hopPower_.push_back (hopSum_);
    hopSide_.push_back (hopSideSum_);
    hopSum_ = hopSideSum_ = 0.0;
    hopFill_ = 0;
}

void StemMeter::flushFrame()
{
    const int n = (int) pending_[0].size();
    if (n == 0)
        return;
    static thread_local dsp::Fft fft (frameOrder);
    std::array<double, measureBandCount> energy {};
    for (const auto& ch : pending_)
    {
        double timeEnergy = 0.0;
        for (int i = 0; i < n; ++i)
            timeEnergy += (double) ch[(size_t) i] * ch[(size_t) i];
        if (timeEnergy <= 0.0)
            continue;
        std::fill (fftData_.begin(), fftData_.end(), 0.0f);
        for (int i = 0; i < n; ++i)
            fftData_[(size_t) i] = ch[(size_t) i] * window_[(size_t) i];
        fft.forwardReal (fftData_.data(), true);
        std::array<double, measureBandCount> bins {};
        double total = 0.0;
        for (int k = 0; k <= frame_ / 2; ++k)
        {
            const double re = fftData_[(size_t) (2 * k)], im = fftData_[(size_t) (2 * k + 1)];
            const double p = re * re + im * im;
            const double hz = k * sampleRate_ / frame_;
            int band = measureBandCount - 1;
            for (int b = 0; b < measureBandCount; ++b)
                if (hz < edges_[(size_t) b + 1])
                {
                    band = b;
                    break;
                }
            bins[(size_t) band] += p;
            total += p;
        }
        if (total <= 0.0)
            continue;
        for (int b = 0; b < measureBandCount; ++b)    // 창을 씌운 스펙트럼의 몫으로 시간 영역의 에너지를 나눈다
            energy[(size_t) b] += 0.5 * timeEnergy * bins[(size_t) b] / total;
    }
    frameCentre_.push_back (written_ - n + n / 2);
    frameEnergy_.push_back (energy);
    frameLength_.push_back (n);
    for (auto& p : pending_)
        p.clear();
}

void StemMeter::finish()
{
    if (finished_)
        return;
    finished_ = true;
    if (hopFill_ > 0)
        flushHop();
    if (! pending_[0].empty())
        flushFrame();
}

double StemMeter::power (int64_t s0, int64_t s1) const
{
    const int64_t h0 = std::max<int64_t> (0, s0 / hop_), h1 = std::min<int64_t> ((int64_t) hopPower_.size(), (s1 + hop_ - 1) / hop_);
    double sum = 0.0;
    int64_t count = 0;
    for (int64_t h = h0; h < h1; ++h)
    {
        sum += hopPower_[(size_t) h];
        count += std::min<int64_t> (hop_, written_ - h * hop_);
    }
    return count > 0 ? sum / (double) count : 0.0;
}

double StemMeter::sidePower (int64_t s0, int64_t s1) const
{
    const int64_t h0 = std::max<int64_t> (0, s0 / hop_), h1 = std::min<int64_t> ((int64_t) hopSide_.size(), (s1 + hop_ - 1) / hop_);
    double sum = 0.0;
    int64_t count = 0;
    for (int64_t h = h0; h < h1; ++h)
    {
        sum += hopSide_[(size_t) h];
        count += std::min<int64_t> (hop_, written_ - h * hop_);
    }
    return count > 0 ? sum / (double) count : 0.0;
}

std::array<double, measureBandCount> StemMeter::bandPower (int64_t s0, int64_t s1) const
{
    std::array<double, measureBandCount> out {};
    int64_t count = 0;
    for (size_t f = 0; f < frameCentre_.size(); ++f)
        if (frameCentre_[f] >= s0 && frameCentre_[f] < s1)
        {
            for (int b = 0; b < measureBandCount; ++b)
                out[(size_t) b] += frameEnergy_[f][(size_t) b];
            count += frameLength_[f];
        }
    for (auto& v : out)
        v = count > 0 ? v / (double) count : 0.0;
    return out;
}

} // namespace mr
