#include "analysis/Onsets.h"

#include "io/AudioFile.h"

#include "dsp/Fft.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace mr
{

namespace
{

constexpr int kWindowOrder = 10;   // 1024 샘플
constexpr int kWindow = 1 << kWindowOrder;
constexpr int kHop = 256;

// 스펙트럴 플럭스: 프레임 k 는 창이 신호의 [k * hop - window, k * hop) 를 볼 때 앞 창보다 오른 로그 크기의 합
// (앞에 무음 창 하나를 붙여 처음의 어택도 오르는 양이 된다). 어택은 프레임 k 의 자리 k * hop 바로 앞에서 보인다
std::vector<double> spectralFlux (const float* signal, int n)
{
    std::vector<double> flux;
    if (n < kWindow)
        return flux;
    std::vector<float> padded ((size_t) (n + kWindow), 0.0f);
    std::copy (signal, signal + n, padded.begin() + kWindow);
    const float* x = padded.data();
    n += kWindow;
    dsp::Fft fft (kWindowOrder);
    std::vector<float> hann ((size_t) kWindow), work ((size_t) kWindow * 2);
    for (int i = 0; i < kWindow; ++i)
        hann[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) kWindow);
    std::vector<float> previous ((size_t) kWindow / 2 + 1, 0.0f), current (previous.size());
    for (int start = 0; start + kWindow <= n; start += kHop)
    {
        std::fill (work.begin(), work.end(), 0.0f);
        for (int i = 0; i < kWindow; ++i)
            work[(size_t) i] = x[start + i] * hann[(size_t) i];
        fft.forwardMagnitudes (work.data(), true);
        double sum = 0.0;
        for (size_t k = 1; k < current.size(); ++k)
        {
            current[k] = std::log1p (100.0f * work[k]);
            sum += std::max (0.0f, current[k] - previous[k]);
        }
        flux.push_back (sum);
        std::swap (previous, current);
    }
    return flux;
}

std::vector<float> monoOf (const juce::File& audio, double startSeconds, std::optional<double> lengthSeconds, double& sampleRate, double& offset)
{
    const auto data = readAudioFile (audio);
    const auto& b = data.buffer;
    sampleRate = data.sampleRate;
    const int first = std::clamp ((int) std::lround (startSeconds * sampleRate), 0, b.getNumSamples());
    int last = b.getNumSamples();
    if (lengthSeconds)
        last = std::clamp ((int) std::lround ((startSeconds + *lengthSeconds) * sampleRate), first, last);
    offset = first / sampleRate;
    std::vector<float> mono ((size_t) (last - first), 0.0f);
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = first; i < last; ++i)
            mono[(size_t) (i - first)] += b.getSample (ch, i) / (float) b.getNumChannels();
    return mono;
}

} // namespace

std::vector<double> detectOnsets (const float* x, int n, double fs, const OnsetOptions& o)
{
    std::vector<double> onsets;
    const auto flux = spectralFlux (x, n);
    if (flux.size() < 3)
        return onsets;
    const double peak = *std::max_element (flux.begin(), flux.end());
    if (peak <= 1.0e-9)
        return onsets;
    // 봉우리: 앞뒤 세 프레임에서 가장 크고, 둘레(앞 0.1초, 뒤 0.05초)의 중앙값 위로 충분히 크다
    const int before = (int) std::ceil (0.1 * fs / kHop), after = (int) std::ceil (0.05 * fs / kHop);
    const double floor = 0.06 * peak / std::clamp (o.sensitivity, 0.25, 4.0);
    // 1 ms 엔벨로프 (어택의 자리를 찾는다)
    const int envHalf = std::max (1, (int) std::lround (0.001 * fs));
    std::vector<double> squares ((size_t) n + 1, 0.0);
    for (int i = 0; i < n; ++i)
        squares[(size_t) i + 1] = squares[(size_t) i] + (double) x[i] * x[i];
    auto env = [&] (int i)
    {
        const int a = std::clamp (i - envHalf, 0, n), b = std::clamp (i + envHalf, 0, n);
        return b > a ? std::sqrt ((squares[(size_t) b] - squares[(size_t) a]) / (b - a)) : 0.0;
    };
    double last = -1.0e9;
    for (int k = 1; k + 1 < (int) flux.size(); ++k)
    {
        bool isPeak = true;
        for (int j = std::max (0, k - 3); j <= std::min ((int) flux.size() - 1, k + 3); ++j)
            if (flux[(size_t) j] > flux[(size_t) k] || (flux[(size_t) j] == flux[(size_t) k] && j < k))
                isPeak = false;
        if (! isPeak)
            continue;
        std::vector<double> around (flux.begin() + std::max (0, k - before), flux.begin() + std::min ((int) flux.size(), k + after + 1));
        std::nth_element (around.begin(), around.begin() + (long) around.size() / 2, around.end());
        const double local = around[around.size() / 2];
        if (flux[(size_t) k] < floor + 1.5 * local)
            continue;
        // 어택의 자리: 이 창이 새로 본 마지막 홉 둘레에서 엔벨로프의 봉우리를 찾고, -12 dB (가파른 머리는 더) 나 뚜렷한 골까지 거슬러 간다
        const int regionStart = std::max (0, k * kHop - 2 * kHop), regionEnd = std::min (n, k * kHop + kHop);
        int top = regionStart;
        double topValue = 0.0;
        for (int i = regionStart; i < regionEnd; i += std::max (1, envHalf / 2))
            if (const double v = env (i); v > topValue)
            {
                topValue = v;
                top = i;
            }
        int at = top;
        const int step = std::max (1, envHalf / 2);
        double previous = topValue;
        while (at - step >= std::max (0, regionStart - kWindow))
        {
            const double v = env (at - step);
            // -12 dB 아래에서는 가파르게 오르는 머리(한 걸음에 2 dB 넘게)만 -40 dB 까지 따라간다 (천천히 차는 킥의 첫 몇 ms)
            if (v > previous * 1.3 || (v < 0.25 * topValue && (v > previous * 0.8 || v < 0.01 * topValue)))
                break;
            previous = v;
            at -= step;
        }
        const double seconds = std::max (0.0, (at - (int) std::lround (0.001 * fs)) / fs);   // 1 ms 앞에서 자른다
        if (seconds - last < o.minGapSeconds)
            continue;
        onsets.push_back (seconds);
        last = seconds;
    }
    return onsets;
}

std::optional<TempoEstimate> estimateTempo (const float* x, int n, double fs, double minBpm, double maxBpm)
{
    auto flux = spectralFlux (x, n);
    const auto onsets = detectOnsets (x, n, fs);
    if (flux.size() < 16 || onsets.size() < 4)
        return std::nullopt;
    const double frameRate = fs / kHop;
    const double mean = std::accumulate (flux.begin(), flux.end(), 0.0) / (double) flux.size();
    for (auto& v : flux)
        v = std::max (0.0, v - mean);
    const int frames = (int) flux.size();
    auto acf = [&] (double lag)
    {
        // 소수 지연의 자기상관 (직선 보간), 겹친 길이로 나눈다
        const int whole = (int) std::floor (lag);
        const double f = lag - whole;
        double sum = 0.0;
        int count = 0;
        for (int i = 0; i + whole + 1 < frames; ++i)
        {
            sum += flux[(size_t) i] * ((1.0 - f) * flux[(size_t) (i + whole)] + f * flux[(size_t) (i + whole + 1)]);
            ++count;
        }
        return count > 0 ? sum / count : 0.0;
    };
    auto comb = [&] (double bpm, int multiples)
    {
        const double beat = 60.0 / bpm * frameRate;
        double s = 0.0;
        int used = 0;
        for (int m = 1; m <= multiples && m * beat < frames * 0.75; ++m, ++used)
            s += acf (m * beat);
        return used > 0 ? s / used : 0.0;
    };
    // 0.25 BPM 칸으로 훑어 사람의 박 감각(120 둘레)을 약하게 곱한 값으로 봉우리를 고르고 (반 템포와 두 배 템포 가운데),
    // 고른 봉우리는 박의 여덟 배수까지 더한 빗살만으로 0.005 BPM 까지 다듬는다 (가중이 봉우리를 120 쪽으로 끌지 않게)
    auto prior = [] (double bpm) { const double o = std::log2 (bpm / 120.0); return std::exp (-0.5 * o * o / (0.9 * 0.9)); };
    std::vector<std::pair<double, double>> scores;
    for (double bpm = minBpm; bpm <= maxBpm + 1.0e-9; bpm += 0.25)
        scores.push_back ({ comb (bpm, 4) * prior (bpm), bpm });
    const auto best = std::max_element (scores.begin(), scores.end());
    if (best->first <= 0.0)
        return std::nullopt;
    double bpm = best->second, value = comb (bpm, 8);
    for (double step : { 0.05, 0.005 })
    {
        const double centre = bpm;
        for (double b = centre - 20.0 * step; b <= centre + 20.0 * step + 1.0e-9; b += step)
            if (const double v = comb (b, 8); v > value)
            {
                value = v;
                bpm = b;
            }
    }
    // 어택의 짝(8 박 안)이 박의 배수에 얼마나 맞는가 (6 ms 가우스)로 ±3% 안에서 다듬는다. 플럭스의 자기상관은 봉우리가 넓고
    // 스윙과 하이햇 무늬에 따라 치우친다 (실제 드럼 세트의 94 BPM 이 95.2 로 나왔다). 어택은 1~2 ms 안에서 정확하다
    auto pairs = [&] (double b)
    {
        const double beatSeconds = 60.0 / b;
        double score = 0.0;
        for (size_t i = 0; i < onsets.size(); ++i)
            for (size_t j = i + 1; j < onsets.size(); ++j)
            {
                const double d = onsets[j] - onsets[i];
                if (d > 8.5 * beatSeconds)
                    break;
                const double k = std::round (d / beatSeconds);
                if (k >= 1.0)
                {
                    const double e = (d - k * beatSeconds) / 0.006;
                    score += std::exp (-0.5 * e * e);
                }
            }
        return score;
    };
    {
        double refined = bpm, top = pairs (bpm);
        for (double step : { 0.05, 0.005 })
        {
            const double centre = refined, half = step == 0.05 ? bpm * 0.03 : 0.1;
            for (double b = centre - half; b <= centre + half + 1.0e-9; b += step)
                if (const double v = pairs (b); v > top)
                {
                    top = v;
                    refined = b;
                }
        }
        bpm = refined;
    }
    value = best->first;
    // 확신: 반 템포, 두 배 템포와 멀리 떨어진 가장 좋은 다른 후보
    double other = 0.0;
    for (const auto& [v, b] : scores)
        if (std::abs (b - bpm) > 3.0)
            other = std::max (other, v);
    TempoEstimate t;
    t.confidence = std::clamp (1.0 - other / value, 0.0, 1.0);
    // 길이가 정수 마디에 가까우면 그 길이에 맞춘다 (반복 루프)
    const double seconds = n / fs;
    const double beats = seconds * bpm / 60.0;
    const double bars = std::round (beats / 4.0);
    if (bars >= 1.0 && std::abs (beats - bars * 4.0) / (bars * 4.0) < 0.005)
        bpm = bars * 4.0 * 60.0 / seconds;
    t.bpm = std::round (bpm * 1000.0) / 1000.0;
    // 박의 자리: 한 박 안의 위상마다 박 자리의 플럭스를 더해 가장 큰 곳
    const double beat = 60.0 / t.bpm * frameRate;
    double bestPhase = 0.0, bestSum = -1.0;
    for (int p = 0; p < (int) std::ceil (beat); ++p)
    {
        double s = 0.0;
        for (double at = p; at < frames; at += beat)
        {
            const int i = (int) std::lround (at);
            double v = 0.0;
            for (int j = std::max (0, i - 1); j <= std::min (frames - 1, i + 1); ++j)
                v = std::max (v, flux[(size_t) j]);
            s += v;
        }
        if (s > bestSum)
        {
            bestSum = s;
            bestPhase = p;
        }
    }
    t.firstBeatSeconds = std::max (0.0, (bestPhase - 1.0) * kHop / fs);
    return t;
}

std::vector<double> detectOnsets (const juce::File& audio, double startSeconds, std::optional<double> lengthSeconds, const OnsetOptions& o)
{
    double fs = 0.0, offset = 0.0;
    const auto mono = monoOf (audio, startSeconds, lengthSeconds, fs, offset);
    auto onsets = detectOnsets (mono.data(), (int) mono.size(), fs, o);
    for (auto& t : onsets)
        t += offset;
    return onsets;
}

std::vector<double> trackBeats (const float* x, int n, double fs, const BeatTrackOptions& o)
{
    const auto onsets = detectOnsets (x, n, fs);
    const auto tempo = estimateTempo (x, n, fs, o.minBpm, o.maxBpm);
    auto flux = spectralFlux (x, n);
    if (! tempo || onsets.size() < 4 || flux.size() < 16)
        return {};
    // 세기: 평균을 빼고 표준 편차로 나눈다 (0 아래는 0)
    const double mean = std::accumulate (flux.begin(), flux.end(), 0.0) / (double) flux.size();
    double var = 0.0;
    for (const double v : flux)
        var += (v - mean) * (v - mean);
    const double sd = std::sqrt (var / (double) flux.size()) + 1.0e-12;
    for (auto& v : flux)
        v = std::max (0.0, (v - mean) / sd);
    const int frames = (int) flux.size();
    const double period = 60.0 / tempo->bpm * fs / kHop;
    std::vector<double> score ((size_t) frames, 0.0);
    std::vector<int> back ((size_t) frames, -1);
    for (int t = 0; t < frames; ++t)
    {
        double best = 0.0;
        int from = -1;
        for (int tau = t - (int) std::lround (2.0 * period); tau <= t - (int) std::lround (0.5 * period); ++tau)
        {
            if (tau < 0)
                continue;
            const double r = std::log ((t - tau) / period);
            const double v = score[(size_t) tau] - o.tightness * r * r;
            if (from < 0 || v > best)
            {
                best = v;
                from = tau;
            }
        }
        score[(size_t) t] = flux[(size_t) t] + (from >= 0 ? std::max (0.0, best) : 0.0);
        back[(size_t) t] = from >= 0 && best > 0.0 ? from : -1;
    }
    int end = frames - 1;
    for (int t = std::max (0, frames - (int) std::ceil (period)); t < frames; ++t)
        if (score[(size_t) t] > score[(size_t) end])
            end = t;
    std::vector<int> chain;
    for (int t = end; t >= 0; t = back[(size_t) t])
        chain.push_back (t);
    std::reverse (chain.begin(), chain.end());
    // 프레임의 자리에서 어택으로: 어택이 둘레에 있으면 그 자리, 없으면 옮긴 박들의 중앙값만큼 옮긴다
    std::vector<double> beats, shifts;
    std::vector<bool> snapped;
    for (const int k : chain)
    {
        const double at = (double) k * kHop / fs;
        double best = -1.0;
        for (const double t : onsets)
            if (t >= at - 0.035 && t <= at + 0.01 && (best < 0.0 || std::abs (t - at) < std::abs (best - at)))
                best = t;
        beats.push_back (best >= 0.0 ? best : at);
        snapped.push_back (best >= 0.0);
        if (best >= 0.0)
            shifts.push_back (best - at);
    }
    if (! shifts.empty())
    {
        std::sort (shifts.begin(), shifts.end());
        const double shift = shifts[shifts.size() / 2];
        for (size_t i = 0; i < beats.size(); ++i)
            if (! snapped[i])
                beats[i] += shift;
    }
    // 첫 어택 앞과 마지막 어택 뒤 (반 박 넘게)의 박은 소리가 없는 자리다
    const double half = 0.5 * 60.0 / tempo->bpm;
    std::vector<double> kept;
    for (const double b : beats)
        if (b >= onsets.front() - half && b <= onsets.back() + half && (kept.empty() || b > kept.back() + 1.0e-3))
            kept.push_back (std::max (0.0, b));
    // 140 BPM 보다 빠르게 잡았고 하나 걸러 크기가 3 dB 넘게 다르면 박 사이의 음까지 센 것이다: 큰 쪽만 박으로
    if (kept.size() >= 8)
    {
        std::vector<double> gaps;
        for (size_t i = 1; i < kept.size(); ++i)
            gaps.push_back (kept[i] - kept[i - 1]);
        std::nth_element (gaps.begin(), gaps.begin() + (long) gaps.size() / 2, gaps.end());
        if (60.0 / gaps[gaps.size() / 2] > 140.0)
        {
            const auto levels = onsetLevelsDb (x, n, fs, kept);
            double even = 0.0, odd = 0.0;
            for (size_t i = 0; i < levels.size(); ++i)
                (i % 2 == 0 ? even : odd) += levels[i];
            even /= (double) ((levels.size() + 1) / 2);
            odd /= (double) (levels.size() / 2);
            if (std::abs (even - odd) > 3.0)
            {
                std::vector<double> half;
                for (size_t i = even > odd ? 0 : 1; i < kept.size(); i += 2)
                    half.push_back (kept[i]);
                kept = half;
            }
        }
    }
    return kept;
}

std::vector<double> trackBeats (const juce::File& audio, double startSeconds, std::optional<double> lengthSeconds, const BeatTrackOptions& o)
{
    double fs = 0.0, offset = 0.0;
    const auto mono = monoOf (audio, startSeconds, lengthSeconds, fs, offset);
    auto beats = trackBeats (mono.data(), (int) mono.size(), fs, o);
    for (auto& b : beats)
        b += offset;
    return beats;
}

std::vector<double> onsetLevelsDb (const float* mono, int n, double fs, const std::vector<double>& onsets)
{
    std::vector<double> levels;
    for (size_t i = 0; i < onsets.size(); ++i)
    {
        const int a = std::clamp ((int) std::lround (onsets[i] * fs), 0, n);
        int b = std::min (n, a + (int) std::lround (0.03 * fs));
        if (i + 1 < onsets.size())
            b = std::min (b, std::max (a + 1, (int) std::lround (onsets[i + 1] * fs)));
        float peak = 0.0f;
        for (int k = a; k < b; ++k)
            peak = std::max (peak, std::abs (mono[k]));
        levels.push_back (20.0 * std::log10 ((double) peak + 1.0e-9));
    }
    return levels;
}

std::vector<double> onsetLevelsDb (const juce::File& audio, const std::vector<double>& onsets)
{
    double fs = 0.0, offset = 0.0;
    const auto mono = monoOf (audio, 0.0, std::nullopt, fs, offset);
    return onsetLevelsDb (mono.data(), (int) mono.size(), fs, onsets);
}

std::optional<TempoEstimate> estimateTempo (const juce::File& audio, double startSeconds, std::optional<double> lengthSeconds)
{
    double fs = 0.0, offset = 0.0;
    const auto mono = monoOf (audio, startSeconds, lengthSeconds, fs, offset);
    auto t = estimateTempo (mono.data(), (int) mono.size(), fs);
    if (t)
        t->firstBeatSeconds += offset;
    return t;
}

} // namespace mr
