#include "dsp/Restore.h"

#include "dsp/Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace mr::dsp
{

namespace
{

constexpr double pi = 3.14159265358979323846;

double medianOf (std::vector<float> v)
{
    if (v.empty())
        return 0.0;
    auto mid = v.begin() + (long) (v.size() / 2);
    std::nth_element (v.begin(), mid, v.end());
    return *mid;
}

// 최소 제곱 선형 예측의 계수: x[t] 를 Σ c[k] x[t-1-k] 로 (작은 능선 값으로 사인파처럼 차수가 남아도 풀린다)
std::vector<double> predictor (const double* x, int length, int order)
{
    std::vector<double> c ((size_t) order, 0.0);
    if (order <= 0 || length <= order * 2)
        return c;
    std::vector<double> m ((size_t) (order * order), 0.0), r ((size_t) order, 0.0);
    for (int t = order; t < length; ++t)
        for (int i = 0; i < order; ++i)
        {
            const double xi = x[t - 1 - i];
            r[(size_t) i] += xi * x[t];
            for (int j = 0; j <= i; ++j)
                m[(size_t) (i * order + j)] += xi * x[t - 1 - j];
        }
    double trace = 0.0;
    for (int i = 0; i < order; ++i)
        trace += m[(size_t) (i * order + i)];
    if (trace <= 0.0)
        return c;
    const double ridge = 1.0e-7 * trace / order;
    for (int i = 0; i < order; ++i)
        m[(size_t) (i * order + i)] += ridge;
    // 숄레스키 분해 (아래 삼각만 쓴다)
    std::vector<double> l ((size_t) (order * order), 0.0);
    for (int i = 0; i < order; ++i)
        for (int j = 0; j <= i; ++j)
        {
            double sum = m[(size_t) (i * order + j)];
            for (int k = 0; k < j; ++k)
                sum -= l[(size_t) (i * order + k)] * l[(size_t) (j * order + k)];
            if (i == j)
                l[(size_t) (i * order + i)] = std::sqrt (std::max (sum, 1.0e-300));
            else
                l[(size_t) (i * order + j)] = sum / l[(size_t) (j * order + j)];
        }
    std::vector<double> y ((size_t) order);
    for (int i = 0; i < order; ++i)
    {
        double sum = r[(size_t) i];
        for (int k = 0; k < i; ++k)
            sum -= l[(size_t) (i * order + k)] * y[(size_t) k];
        y[(size_t) i] = sum / l[(size_t) (i * order + i)];
    }
    for (int i = order - 1; i >= 0; --i)
    {
        double sum = y[(size_t) i];
        for (int k = i + 1; k < order; ++k)
            sum -= l[(size_t) (k * order + i)] * c[(size_t) k];
        c[(size_t) i] = sum / l[(size_t) (i * order + i)];
    }
    return c;
}

// history 뒤로 count 개를 예측해 붙인다
std::vector<double> extrapolate (std::vector<double> history, int count)
{
    const int length = (int) history.size();
    const int order = std::min (32, length / 4);
    const auto c = predictor (history.data(), length, order);
    history.resize ((size_t) (length + count));
    for (int t = length; t < length + count; ++t)
    {
        double v = 0.0;
        for (int k = 0; k < order; ++k)
            v += c[(size_t) k] * history[(size_t) (t - 1 - k)];
        history[(size_t) t] = v;
    }
    return std::vector<double> (history.begin() + length, history.end());
}

// 예측을 쓰는 최소 제곱 메우기: y 의 [gap, gap+length) 를 모르고 그 밖은 안다. 예측기는 y[t] 를 Σ c_j y[t - lags_j] 로 놓고,
// 계수는 모르는 샘플이 없는 자리에서 최소 제곱으로 잰다. 빈 곳은 예측 오차의 제곱합이 가장 작은 값으로 채운다
bool fillWithPredictor (std::vector<double>& y, int gap, int length, const std::vector<int>& lags)
{
    const int n = (int) y.size(), q = (int) lags.size(), maxLag = lags.back();
    if (gap < maxLag || gap + length + maxLag > n)
        return false;
    auto unknown = [&] (int k) { return k >= gap && k < gap + length; };
    // 계수
    std::vector<double> m ((size_t) (q * q), 0.0), r ((size_t) q, 0.0), x ((size_t) q);
    for (int t = maxLag; t < n; ++t)
    {
        if (unknown (t) || (t - maxLag < gap + length && t - 1 >= gap))
            continue;   // 예측에 모르는 샘플이 들어가는 자리
        for (int a = 0; a < q; ++a)
            x[(size_t) a] = y[(size_t) (t - lags[(size_t) a])];
        for (int a = 0; a < q; ++a)
        {
            r[(size_t) a] += x[(size_t) a] * y[(size_t) t];
            for (int b = 0; b <= a; ++b)
                m[(size_t) (a * q + b)] += x[(size_t) a] * x[(size_t) b];
        }
    }
    auto solve = [] (std::vector<double>& mat, std::vector<double>& rhs, int size, double ridgeShare)
    {
        double trace = 0.0;
        for (int a = 0; a < size; ++a)
            trace += mat[(size_t) (a * size + a)];
        if (trace <= 0.0)
            return false;
        for (int a = 0; a < size; ++a)
            mat[(size_t) (a * size + a)] += ridgeShare * trace / size;
        for (int a = 0; a < size; ++a)
            for (int b = 0; b <= a; ++b)
            {
                double sum = mat[(size_t) (a * size + b)];
                for (int k = 0; k < b; ++k)
                    sum -= mat[(size_t) (a * size + k)] * mat[(size_t) (b * size + k)];
                if (a == b)
                {
                    if (sum <= 0.0)
                        return false;
                    mat[(size_t) (a * size + a)] = std::sqrt (sum);
                }
                else
                    mat[(size_t) (a * size + b)] = sum / mat[(size_t) (b * size + b)];
            }
        for (int a = 0; a < size; ++a)
        {
            double sum = rhs[(size_t) a];
            for (int k = 0; k < a; ++k)
                sum -= mat[(size_t) (a * size + k)] * rhs[(size_t) k];
            rhs[(size_t) a] = sum / mat[(size_t) (a * size + a)];
        }
        for (int a = size - 1; a >= 0; --a)
        {
            double sum = rhs[(size_t) a];
            for (int k = a + 1; k < size; ++k)
                sum -= mat[(size_t) (k * size + a)] * rhs[(size_t) k];
            rhs[(size_t) a] = sum / mat[(size_t) (a * size + a)];
        }
        return true;
    };
    if (! solve (m, r, q, 1.0e-7))
        return false;
    const auto& c = r;
    // 빈 곳: e[t] = y[t] - Σ c_j y[t - lag_j] 가운데 모르는 샘플이 들어가는 t 의 제곱합
    std::vector<double> mm ((size_t) (length * length), 0.0), rhs ((size_t) length, 0.0);
    std::vector<std::pair<int, double>> entries;
    for (int t = gap; t < gap + length + maxLag; ++t)
    {
        entries.clear();
        double known = 0.0;
        if (unknown (t))
            entries.push_back ({ t - gap, 1.0 });
        else
            known += y[(size_t) t];
        for (int a = 0; a < q; ++a)
        {
            const int k = t - lags[(size_t) a];
            if (unknown (k))
                entries.push_back ({ k - gap, -c[(size_t) a] });
            else
                known -= c[(size_t) a] * y[(size_t) k];
        }
        for (const auto& [u, cu] : entries)
        {
            rhs[(size_t) u] -= cu * known;
            for (const auto& [v, cv] : entries)
                if (v <= u)
                    mm[(size_t) (u * length + v)] += cu * cv;
        }
    }
    if (! solve (mm, rhs, length, 1.0e-12))
        return false;
    for (int a = 0; a < length; ++a)
        y[(size_t) (gap + a)] = rhs[(size_t) a];
    return true;
}

// 빈 곳 앞의 소리에서 음높이 주기 (샘플): 정규화 자기상관이 가장 큰 지연 (50~500 Hz). 0.6 보다 작으면 0
int pitchPeriod (const std::vector<double>& y, int gap, double sampleRate)
{
    const int minLag = (int) (sampleRate / 500.0), maxLag = (int) (sampleRate / 50.0);
    const int window = 1024;
    if (gap < window + maxLag)
        return 0;
    double best = 0.6;
    int period = 0;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double xy = 0.0, xx = 0.0, yy = 0.0;
        for (int t = gap - window; t < gap; ++t)
        {
            xy += y[(size_t) t] * y[(size_t) (t - lag)];
            xx += y[(size_t) t] * y[(size_t) t];
            yy += y[(size_t) (t - lag)] * y[(size_t) (t - lag)];
        }
        const double corr = xx > 0.0 && yy > 0.0 ? xy / std::sqrt (xx * yy) : 0.0;
        if (corr > best)
        {
            best = corr;
            period = lag;
        }
    }
    return period;
}

double rmsOf (const float* x, int64_t from, int64_t to)
{
    double sum = 0.0;
    int64_t n = 0;
    for (int64_t i = from; i < to; ++i, ++n)
        sum += (double) x[i] * x[i];
    return n > 0 ? std::sqrt (sum / (double) n) : 0.0;
}

// 험 지우기: 배음마다 노치 (RBJ, 대역폭 2 Hz) 둘을 앞으로 잇달아 건다 (깊이는 앞뒤로 거는 것과 같고, 울림은 소리의 뒤에 생겨 그 소리에 묻힌다).
// 필터가 처음부터 자리를 잡도록, 처음 0.5 초에서 잰 험 (배음마다 크기와 위상) 을 1 초 앞에 이어 붙여 데운다
void removeHum (float* x, int64_t n, double sampleRate, const std::vector<double>& frequencies)
{
    const int count = (int) frequencies.size();
    if (count == 0 || n <= 0)
        return;
    const int fit = (int) std::min<int64_t> (n, (int64_t) (0.5 * sampleRate));
    const int pre = (int) sampleRate;
    // 처음 fit 샘플에 배음의 사인과 코사인을 최소 제곱으로 맞춘다
    const int q = 2 * count;
    std::vector<double> m ((size_t) (q * q), 0.0), r ((size_t) q, 0.0), basis ((size_t) q);
    for (int t = 0; t < fit; ++t)
    {
        for (int k = 0; k < count; ++k)
        {
            const double w = 2.0 * pi * frequencies[(size_t) k] * t / sampleRate;
            basis[(size_t) (2 * k)] = std::cos (w);
            basis[(size_t) (2 * k + 1)] = std::sin (w);
        }
        for (int a = 0; a < q; ++a)
        {
            r[(size_t) a] += basis[(size_t) a] * x[t];
            for (int b = 0; b < q; ++b)
                m[(size_t) (a * q + b)] += basis[(size_t) a] * basis[(size_t) b];
        }
    }
    std::vector<double> c ((size_t) q, 0.0);
    {
        for (int a = 0; a < q; ++a)
            m[(size_t) (a * q + a)] += 1.0e-9 * fit;
        // 가우스 소거 (작은 행렬)
        for (int col = 0; col < q; ++col)
        {
            int pivot = col;
            for (int row = col + 1; row < q; ++row)
                if (std::abs (m[(size_t) (row * q + col)]) > std::abs (m[(size_t) (pivot * q + col)]))
                    pivot = row;
            for (int k = 0; k < q; ++k)
                std::swap (m[(size_t) (col * q + k)], m[(size_t) (pivot * q + k)]);
            std::swap (r[(size_t) col], r[(size_t) pivot]);
            const double d = m[(size_t) (col * q + col)];
            if (std::abs (d) < 1.0e-300)
                continue;
            for (int row = col + 1; row < q; ++row)
            {
                const double f = m[(size_t) (row * q + col)] / d;
                for (int k = col; k < q; ++k)
                    m[(size_t) (row * q + k)] -= f * m[(size_t) (col * q + k)];
                r[(size_t) row] -= f * r[(size_t) col];
            }
        }
        for (int row = q - 1; row >= 0; --row)
        {
            double sum = r[(size_t) row];
            for (int k = row + 1; k < q; ++k)
                sum -= m[(size_t) (row * q + k)] * c[(size_t) k];
            const double d = m[(size_t) (row * q + row)];
            c[(size_t) row] = std::abs (d) > 1.0e-300 ? sum / d : 0.0;
        }
    }
    std::vector<double> warm ((size_t) pre);
    for (int t = -pre; t < 0; ++t)
    {
        double v = 0.0;
        for (int k = 0; k < count; ++k)
        {
            const double w = 2.0 * pi * frequencies[(size_t) k] * t / sampleRate;
            v += c[(size_t) (2 * k)] * std::cos (w) + c[(size_t) (2 * k + 1)] * std::sin (w);
        }
        warm[(size_t) (t + pre)] = v;
    }
    for (const double frequency : frequencies)
    {
        const double w0 = 2.0 * pi * frequency / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * (frequency / 2.0));   // Q = 주파수 / 대역폭 2 Hz
        const double cosw = std::cos (w0), a0 = 1.0 + alpha;
        const double b0 = 1.0 / a0, b1 = -2.0 * cosw / a0, b2 = 1.0 / a0, a1 = -2.0 * cosw / a0, a2 = (1.0 - alpha) / a0;
        for (int pass = 0; pass < 2; ++pass)
        {
            double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
            auto step = [&] (double in)
            {
                const double out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                x2 = x1;
                x1 = in;
                y2 = y1;
                y1 = out;
                return out;
            };
            for (auto& v : warm)
                v = step (v);
            for (int64_t i = 0; i < n; ++i)
                x[i] = (float) step (x[i]);
        }
    }
}

void denoise (juce::AudioBuffer<float>& buffer, double sampleRate, const RestoreSettings& settings, RestoreReport& report)
{
    const int order = sampleRate > 60000.0 ? 12 : 11;
    const int size = 1 << order, hop = size / 4, bins = size / 2 + 1;
    const int channels = buffer.getNumChannels();
    const int64_t frames = buffer.getNumSamples();
    if (frames < size || channels <= 0)
        return;
    const int64_t pad = size - hop;
    const int64_t frameCount = (frames + pad + hop - 1) / hop;
    std::vector<float> window ((size_t) size);
    for (int i = 0; i < size; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / size));
    Fft fft (order);
    std::vector<float> work ((size_t) size * 2);
    auto frameStart = [&] (int64_t f) { return f * hop - pad; };
    // 프레임 f, 채널 ch 의 창을 씌운 스펙트럼을 work 에 (N/2+1 칸의 복소수)
    auto spectrum = [&] (int64_t f, int ch)
    {
        const float* x = buffer.getReadPointer (ch);
        const int64_t s = frameStart (f);
        for (int i = 0; i < size; ++i)
        {
            const int64_t at = s + i;
            work[(size_t) i] = at >= 0 && at < frames ? x[at] * window[(size_t) i] : 0.0f;
        }
        std::fill (work.begin() + size, work.end(), 0.0f);
        fft.forwardReal (work.data(), true);
    };
    auto power = [&] (int k) { return (double) work[(size_t) (2 * k)] * work[(size_t) (2 * k)] + (double) work[(size_t) (2 * k + 1)] * work[(size_t) (2 * k + 1)]; };

    // 잡음 지문: 고른 구간의 프레임 평균, 없으면 소리 안의 가장 조용한 10% 프레임의 칸마다 중앙값 (지수 분포의 중앙값이라 ln 2 로 나눈다)
    std::vector<int64_t> chosen;
    bool median = false;
    if (settings.profileStart)
        for (int64_t f = 0; f < frameCount; ++f)
        {
            const double centre = (double) (frameStart (f) + size / 2) / sampleRate;
            if (centre >= *settings.profileStart && centre <= *settings.profileStart + settings.profileLength && frameStart (f) >= 0
                && frameStart (f) + size <= frames)
                chosen.push_back (f);
        }
    if (chosen.empty())
    {
        median = true;
        std::vector<std::pair<double, int64_t>> energy;
        for (int64_t f = 0; f < frameCount; ++f)
        {
            const int64_t s = frameStart (f);
            if (s < 0 || s + size > frames)
                continue;
            double e = 0.0;
            for (int ch = 0; ch < channels; ++ch)
            {
                const float* x = buffer.getReadPointer (ch);
                for (int i = 0; i < size; ++i)
                    e += (double) x[s + i] * x[s + i] * window[(size_t) i] * window[(size_t) i];
            }
            if (e > 1.0e-14 * size)   // 디지털 무음은 잡음이 아니다
                energy.push_back ({ e, f });
        }
        if (energy.empty())
            return;
        std::sort (energy.begin(), energy.end());
        const size_t count = std::max<size_t> (1, energy.size() / 10);
        double quiet = 0.0;
        for (size_t i = 0; i < count; ++i)
        {
            chosen.push_back (energy[i].second);
            quiet += energy[i].first / (double) count;
        }
        // 큰 쪽 10% 프레임이 조용한 쪽보다 6 dB 넘게 크지 않으면 조용한 곳이 없다 (쉬지 않고 이어지는 음, 또는 잡음뿐)
        if (energy[energy.size() * 9 / 10].first < 4.0 * quiet)
        {
            report.noiseSkipped = "no quiet part to measure the noise from; give a profile range where only the noise is heard";
            return;
        }
    }
    std::vector<double> noise ((size_t) bins, 0.0);
    {
        std::vector<std::vector<float>> values ((size_t) bins);
        for (const auto f : chosen)
        {
            std::vector<double> p ((size_t) bins, 0.0);
            for (int ch = 0; ch < channels; ++ch)
            {
                spectrum (f, ch);
                for (int k = 0; k < bins; ++k)
                    p[(size_t) k] += power (k) / channels;
            }
            for (int k = 0; k < bins; ++k)
                values[(size_t) k].push_back ((float) p[(size_t) k]);
        }
        for (int k = 0; k < bins; ++k)
        {
            if (median)
                noise[(size_t) k] = medianOf (values[(size_t) k]) / std::log (2.0);
            else
            {
                double sum = 0.0;
                for (const float v : values[(size_t) k])
                    sum += v;
                noise[(size_t) k] = sum / (double) values[(size_t) k].size();
            }
        }
    }
    // 잡음의 샘플 RMS: 프레임의 창 씌운 에너지 (1/N)Σ|X|² 를 창의 제곱합 (3N/8) 으로 나눈다
    {
        double total = noise[0] + noise[(size_t) (bins - 1)];
        for (int k = 1; k < bins - 1; ++k)
            total += 2.0 * noise[(size_t) k];
        const double variance = total / size / (0.375 * size);
        if (variance <= 0.0)
            return;
        if (median && variance > 1.0e-3)
        {
            // -30 dBFS 보다 큰 잡음은 쉿 소리가 아니라 음악이다
            report.noiseSkipped = "the quietest part is louder than -30 dBFS, so it is music, not noise; give a profile range where only the noise is heard";
            return;
        }
        report.noiseFloorDb = 10.0 * std::log10 (variance);
    }

    // 판정 지향 위너 이득, 주파수 방향으로 두 번 다듬고, 줄어들 때는 60 ms 에 걸쳐, 아래는 noiseDb 에서 멈춘다
    const double floorGain = std::pow (10.0, -settings.noiseDb / 20.0);
    const double alpha = 0.98;
    const double release = std::exp (-(double) hop / (sampleRate * 0.06));
    std::vector<double> lastGainGamma ((size_t) bins, 0.0), lastGain ((size_t) bins, 1.0), gain ((size_t) bins), smooth ((size_t) bins);
    std::vector<std::vector<std::complex<float>>> spectra ((size_t) channels, std::vector<std::complex<float>> ((size_t) bins));
    std::vector<std::vector<double>> sum ((size_t) channels, std::vector<double> ((size_t) size, 0.0));
    const double norm = 1.0 / 1.5;   // 홉 N/4 의 한 창을 두 번 씌운 합
    for (int64_t f = 0; f < frameCount; ++f)
    {
        std::vector<double> p ((size_t) bins, 0.0);
        for (int ch = 0; ch < channels; ++ch)
        {
            spectrum (f, ch);
            for (int k = 0; k < bins; ++k)
            {
                spectra[(size_t) ch][(size_t) k] = { work[(size_t) (2 * k)], work[(size_t) (2 * k + 1)] };
                p[(size_t) k] += power (k) / channels;
            }
        }
        for (int k = 0; k < bins; ++k)
        {
            const double gamma = p[(size_t) k] / std::max (noise[(size_t) k], 1.0e-30);
            const double instant = std::max (gamma - 1.0, 0.0);
            const double xi = f == 0 ? instant : alpha * lastGainGamma[(size_t) k] + (1.0 - alpha) * instant;
            gain[(size_t) k] = xi / (1.0 + xi);
        }
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int k = 0; k < bins; ++k)
            {
                const double left = gain[(size_t) std::max (0, k - 1)], right = gain[(size_t) std::min (bins - 1, k + 1)];
                smooth[(size_t) k] = 0.25 * left + 0.5 * gain[(size_t) k] + 0.25 * right;
            }
            gain.swap (smooth);
        }
        for (int k = 0; k < bins; ++k)
        {
            double g = std::max (gain[(size_t) k], lastGain[(size_t) k] * release);
            g = std::clamp (g, floorGain, 1.0);
            const double gamma = p[(size_t) k] / std::max (noise[(size_t) k], 1.0e-30);
            lastGainGamma[(size_t) k] = g * g * gamma;
            lastGain[(size_t) k] = g;
            gain[(size_t) k] = g;
        }
        const int64_t s = frameStart (f);
        for (int ch = 0; ch < channels; ++ch)
        {
            for (int k = 0; k < bins; ++k)
            {
                const auto v = spectra[(size_t) ch][(size_t) k] * (float) gain[(size_t) k];
                work[(size_t) (2 * k)] = v.real();
                work[(size_t) (2 * k + 1)] = v.imag();
            }
            fft.inverseReal (work.data());
            auto& acc = sum[(size_t) ch];
            for (int i = 0; i < size; ++i)
                acc[(size_t) i] += (double) work[(size_t) i] * window[(size_t) i] * norm;
            // 앞의 hop 칸은 더 겹칠 프레임이 없다: 버퍼에 쓴다 (다음 프레임은 그 뒤만 읽는다)
            float* x = buffer.getWritePointer (ch);
            const bool last = f == frameCount - 1;
            const int done = last ? size : hop;
            for (int i = 0; i < done; ++i)
                if (s + i >= 0 && s + i < frames)
                    x[s + i] = (float) acc[(size_t) i];
            std::move (acc.begin() + hop, acc.end(), acc.begin());
            std::fill (acc.end() - hop, acc.end(), 0.0);
        }
    }
    report.denoised = true;
}

} // namespace

std::vector<ClickSpan> findClicks (const float* x, int64_t frames, double sampleRate)
{
    std::vector<ClickSpan> out;
    if (frames < 16)
        return out;
    std::vector<float> d ((size_t) frames, 0.0f);
    for (int64_t i = 1; i + 1 < frames; ++i)
        d[(size_t) i] = std::abs (x[i + 1] - 2.0f * x[i] + x[i - 1]);
    // 둘레의 크기: 256 샘플 칸마다 중앙값, 앞뒤 넷까지 아홉 칸의 중앙값
    constexpr int block = 256;
    const int64_t blocks = (frames + block - 1) / block;
    std::vector<float> blockMedian ((size_t) blocks);
    for (int64_t b = 0; b < blocks; ++b)
    {
        const int64_t from = b * block, to = std::min (frames, from + block);
        blockMedian[(size_t) b] = (float) medianOf (std::vector<float> (d.begin() + from, d.begin() + to));
    }
    std::vector<float> scale ((size_t) blocks);
    for (int64_t b = 0; b < blocks; ++b)
    {
        const int64_t from = std::max<int64_t> (0, b - 4), to = std::min (blocks, b + 5);
        scale[(size_t) b] = (float) medianOf (std::vector<float> (blockMedian.begin() + from, blockMedian.begin() + to));
    }
    const int maxLength = std::max (8, (int) std::lround (0.002 * sampleRate));
    const int context = std::max (64, (int) std::lround (0.005 * sampleRate));
    struct Candidate
    {
        int64_t start, end;
        float peak;
    };
    std::vector<Candidate> candidates;
    int64_t i = 1;
    while (i + 1 < frames)
    {
        auto above = [&] (int64_t k) { return d[(size_t) k] > std::max (1.0e-4f, 10.0f * scale[(size_t) (k / block)]); };
        if (! above (i))
        {
            ++i;
            continue;
        }
        int64_t first = i, last = i;
        float peak = d[(size_t) i];
        for (int64_t k = i + 1; k + 1 < frames && k <= last + 8; ++k)
            if (above (k))
            {
                last = k;
                peak = std::max (peak, d[(size_t) k]);
            }
        i = last + 1;
        // 꼬리까지: 둘레의 세 배 위로 이어지는 앞뒤 샘플을 더한다 (사라지는 클릭의 끝이 남지 않게)
        auto tail = [&] (int64_t k) { return d[(size_t) k] > std::max (0.3e-4f, 3.0f * scale[(size_t) (k / block)]); };
        while (first > 1 && last - first < maxLength && tail (first - 1))
            --first;
        while (last + 2 < frames && last - first < maxLength && tail (last + 1))
            ++last;
        i = std::max (i, last + 1);
        candidates.push_back ({ std::max<int64_t> (1, first - 2), std::min<int64_t> (frames - 1, last + 3), peak });
    }
    const int64_t period = (int64_t) std::lround (0.025 * sampleRate);
    for (size_t c = 0; c < candidates.size(); ++c)
    {
        const auto& k = candidates[c];
        if (k.end - k.start > maxLength)
            continue;   // 길게 튀는 자리는 어택이다
        int alike = 0;
        for (size_t o = c; o-- > 0 && candidates[o].end > k.start - period;)
            alike += candidates[o].peak > k.peak / 3.0f ? 1 : 0;
        for (size_t o = c + 1; o < candidates.size() && candidates[o].start < k.end + period; ++o)
            alike += candidates[o].peak > k.peak / 3.0f ? 1 : 0;
        if (alike >= 2)
            continue;   // 되풀이되는 튐은 소리의 일부다
        // 뒤의 소리가 앞보다 12 dB 넘게 크면 소리의 시작이다 (북의 어택 따위): 두지 않는다
        const double before = rmsOf (x, std::max<int64_t> (0, k.start - context), k.start);
        const double after = rmsOf (x, k.end, std::min (frames, k.end + context));
        if (after > 4.0 * before + 1.0e-6)
            continue;
        out.push_back ({ k.start, (int) (k.end - k.start) });
    }
    return out;
}

void fillGap (float* x, int64_t frames, const ClickSpan& gap, double sampleRate)
{
    const int64_t a = gap.start, b = gap.start + gap.length;
    const int length = gap.length;
    if (length <= 0 || a < 0 || b > frames)
        return;
    // 양쪽에 문맥이 넉넉하면 최소 제곱 메우기: 짧은 예측(40차)과 음높이 한 주기 앞의 다섯 샘플, 주기가 없으면 짧은 예측 80차
    {
        constexpr int side = 2048;
        if (a >= side && frames - b >= side)
        {
            std::vector<double> y ((size_t) (side + length + side));
            for (int i = 0; i < (int) y.size(); ++i)
                y[(size_t) i] = x[a - side + i];
            std::vector<int> lags;
            const int period = pitchPeriod (y, side, sampleRate);
            for (int k = 1; k <= (period > 42 ? 40 : 80); ++k)
                lags.push_back (k);
            if (period > 42)
                for (int k = period - 2; k <= period + 2; ++k)
                    lags.push_back (k);
            if (fillWithPredictor (y, side, length, lags))
            {
                for (int j = 0; j < length; ++j)
                    x[a + j] = (float) y[(size_t) (side + j)];
                return;
            }
        }
    }
    constexpr int context = 1024;
    const int beforeLength = (int) std::min<int64_t> (context, a);
    const int afterLength = (int) std::min<int64_t> (context, frames - b);
    std::vector<double> forward, backward;
    if (beforeLength >= 32)
        forward = extrapolate (std::vector<double> (x + a - beforeLength, x + a), length);
    if (afterLength >= 32)
    {
        std::vector<double> reversed ((size_t) afterLength);
        for (int k = 0; k < afterLength; ++k)
            reversed[(size_t) k] = x[b + afterLength - 1 - k];
        backward = extrapolate (std::move (reversed), length);   // backward[j] 는 x[b - 1 - j]
    }
    for (int j = 0; j < length; ++j)
    {
        double v;
        if (! forward.empty() && ! backward.empty())
        {
            const double w = std::pow (std::sin (0.5 * pi * (j + 0.5) / length), 2.0);
            v = (1.0 - w) * forward[(size_t) j] + w * backward[(size_t) (length - 1 - j)];
        }
        else if (! forward.empty())
            v = forward[(size_t) j];
        else if (! backward.empty())
            v = backward[(size_t) (length - 1 - j)];
        else
        {
            const double left = a > 0 ? x[a - 1] : 0.0, right = b < frames ? x[b] : 0.0;
            v = left + (right - left) * (j + 1) / (length + 1);
        }
        x[a + j] = (float) v;
    }
}

namespace
{

// 험은 녹음 내내 고르게 울린다: 0.7 초 남짓의 짧은 구간마다 f0, 2f0, 3f0 가운데 하나가 둘레보다 6 dB 넘게 솟아야 하고,
// 그런 구간이 소리가 있는 구간의 4분의 3 이상이어야 한다 (사라지는 음과 북은 처음에만 솟는다)
bool steadyThroughout (const float* x, int64_t frames, double sampleRate, double f0)
{
    int order = 12;
    while ((1 << order) < sampleRate * 0.6 && order < 16)
        ++order;
    const int size = 1 << order;
    if ((int64_t) size > frames)
        return true;   // 짧으면 긴 구간의 봉우리를 믿는다
    const double binHz = sampleRate / size;
    Fft fft (order);
    std::vector<float> work ((size_t) size * 2);
    int counted = 0, steady = 0;
    const int64_t hop = size / 2;
    const int64_t step = std::max<int64_t> (hop, (frames - size) / 200);
    for (int64_t s = 0; s + size <= frames; s += step)
    {
        double energy = 0.0;
        for (int i = 0; i < size; ++i)
        {
            const float v = x[s + i];
            energy += (double) v * v;
            work[(size_t) i] = v * (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / size));
        }
        if (energy < 1.0e-12 * size)
            continue;   // 디지털 무음
        std::fill (work.begin() + size, work.end(), 0.0f);
        fft.forwardReal (work.data(), true);
        auto power = [&] (int k) { return (double) work[(size_t) (2 * k)] * work[(size_t) (2 * k)] + (double) work[(size_t) (2 * k + 1)] * work[(size_t) (2 * k + 1)]; };
        ++counted;
        for (int h = 1; h <= 3; ++h)
        {
            const double f = h * f0;
            const int centre = (int) std::lround (f / binHz);
            if (centre + 1 >= size / 2)
                break;
            const double top = std::max ({ power (centre - 1), power (centre), power (centre + 1) });
            std::vector<float> around;
            for (int k = std::max (1, (int) ((f - 20.0) / binHz)); k <= std::min (size / 2, (int) ((f + 20.0) / binHz)); ++k)
                if (std::abs (k * binHz - f) > 4.0)
                    around.push_back ((float) power (k));
            if (top > 4.0 * std::max (medianOf (around), 1.0e-30))
            {
                ++steady;
                break;
            }
        }
    }
    return counted == 0 || steady * 4 >= counted * 3;
}

} // namespace

HumEstimate measureHum (const float* x, int64_t frames, double sampleRate, double nominal)
{
    HumEstimate out;
    if (frames < (int64_t) sampleRate)
        return out;
    int order = 12;
    while ((1 << order) < sampleRate / 0.4 && order < 18)
        ++order;
    while ((int64_t) (1 << order) > frames && order > 13)
        --order;
    const int size = 1 << order;
    if ((int64_t) size > frames)
        return out;
    const double binHz = sampleRate / size;
    Fft fft (order);
    std::vector<float> work ((size_t) size * 2);
    std::vector<double> power ((size_t) (size / 2 + 1), 0.0);
    int segments = 0;
    for (int64_t s = 0; s + size <= frames && segments < 40; s += size / 2, ++segments)
    {
        for (int i = 0; i < size; ++i)
            work[(size_t) i] = x[s + i] * (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / size));
        std::fill (work.begin() + size, work.end(), 0.0f);
        fft.forwardReal (work.data(), true);
        for (int k = 0; k <= size / 2; ++k)
            power[(size_t) k] += (double) work[(size_t) (2 * k)] * work[(size_t) (2 * k)] + (double) work[(size_t) (2 * k + 1)] * work[(size_t) (2 * k + 1)];
    }
    // f 둘레 ±halfWidth 의 봉우리: 로그 크기의 포물선으로 주파수를 다듬고, 둘레(3~20 Hz 밖)의 중앙값과 견준 높이
    struct Peak
    {
        double frequency = 0.0, prominence = 0.0;
    };
    auto peakNear = [&] (double f, double halfWidth) -> Peak
    {
        const int lo = std::max (1, (int) std::floor ((f - halfWidth) / binHz)), hi = std::min (size / 2 - 1, (int) std::ceil ((f + halfWidth) / binHz));
        if (hi <= lo)
            return {};
        int m = lo;
        for (int k = lo; k <= hi; ++k)
            if (power[(size_t) k] > power[(size_t) m])
                m = k;
        std::vector<float> around;
        for (int k = std::max (1, (int) ((f - 20.0) / binHz)); k <= std::min (size / 2, (int) ((f + 20.0) / binHz)); ++k)
            if (std::abs (k * binHz - f) > 3.0)
                around.push_back ((float) power[(size_t) k]);
        const double floor = std::max (medianOf (around), 1.0e-30);
        const double a = std::log (power[(size_t) (m - 1)] + 1.0e-30), b = std::log (power[(size_t) m] + 1.0e-30), c = std::log (power[(size_t) (m + 1)] + 1.0e-30);
        const double den = a - 2.0 * b + c;
        const double shift = std::abs (den) > 1.0e-12 ? std::clamp (0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
        return { (m + shift) * binHz, power[(size_t) m] / floor };
    };
    double bestScore = 0.0;
    for (const double candidate : nominal < 0.0 ? std::vector<double> { 50.0, 60.0 } : std::vector<double> { nominal })
    {
        // 기본 주파수를 낮은 배음에서 먼저 정한다 (가장 솟은 것)
        double f0 = 0.0, strongest = 10.0;
        for (int k = 1; k <= 3; ++k)
        {
            const auto p = peakNear (k * candidate, 0.3 * k);   // 전기의 주파수는 공칭값에서 0.3 Hz 안이다
            if (p.prominence > strongest)
            {
                strongest = p.prominence;
                f0 = p.frequency / k;
            }
        }
        if (f0 <= 0.0 || ! steadyThroughout (x, frames, sampleRate, f0))
            continue;
        // 뚜렷한 배음(둘레보다 10 dB 위)으로 기본 주파수를 다듬고, 노치는 배음 열 개 모두에 건다 (뚜렷하지 않은 배음은 k 배 자리)
        std::vector<std::pair<int, double>> measured;
        std::vector<float> fundamentals;
        double score = 0.0;
        for (int k = 1; k <= 10 && k * f0 + 25.0 < sampleRate / 2.0; ++k)
        {
            const auto p = peakNear (k * f0, 1.0);
            if (p.prominence < 10.0)
                continue;
            measured.push_back ({ k, p.frequency });
            fundamentals.push_back ((float) (p.frequency / k));
            score += std::log10 (p.prominence);
        }
        if (score > bestScore)
        {
            bestScore = score;
            HumEstimate e;
            e.fundamental = medianOf (fundamentals);
            for (int k = 1; k <= 10 && k * e.fundamental + 25.0 < sampleRate / 2.0; ++k)
            {
                double f = k * e.fundamental;
                for (const auto& [mk, mf] : measured)
                    if (mk == k)
                        f = mf;
                e.harmonics.push_back (f);
            }
            out = e;
        }
    }
    return out;
}

RestoreReport restoreAudio (juce::AudioBuffer<float>& buffer, double sampleRate, const RestoreSettings& settings)
{
    RestoreReport report;
    const int channels = buffer.getNumChannels();
    const int64_t frames = buffer.getNumSamples();
    if (channels <= 0 || frames <= 0)
        return report;
    auto mono = [&]
    {
        std::vector<float> m ((size_t) frames, 0.0f);
        for (int ch = 0; ch < channels; ++ch)
        {
            const float* x = buffer.getReadPointer (ch);
            for (int64_t i = 0; i < frames; ++i)
                m[(size_t) i] += x[i] / (float) channels;
        }
        return m;
    };
    if (settings.declick)
    {
        // 클릭은 채널 평균에서 찾고 채널마다 같은 자리를 메운다
        const auto m = mono();
        const auto clicks = findClicks (m.data(), frames, sampleRate);
        for (const auto& c : clicks)
            for (int ch = 0; ch < channels; ++ch)
                fillGap (buffer.getWritePointer (ch), frames, c, sampleRate);
        report.clicks = (int) clicks.size();
    }
    if (settings.humHz != 0.0)
    {
        const auto m = mono();
        const auto hum = measureHum (m.data(), frames, sampleRate, settings.humHz);
        report.humHz = hum.fundamental;
        for (int ch = 0; ch < channels; ++ch)
            removeHum (buffer.getWritePointer (ch), frames, sampleRate, hum.harmonics);
        report.humNotches = hum.harmonics;
    }
    if (settings.noiseDb > 0.0)
        denoise (buffer, sampleRate, settings, report);
    return report;
}

} // namespace mr::dsp
