#include "analysis/PitchTrack.h"

#include "io/AudioFile.h"

#include "dsp/Fft.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace mr
{

namespace
{

double median (std::vector<double> v)
{
    if (v.empty())
        return 0.0;
    const auto mid = v.begin() + (long) v.size() / 2;
    std::nth_element (v.begin(), mid, v.end());
    return *mid;
}

} // namespace

std::vector<PitchFrame> trackPitch (const float* x, int numSamples, double fs, int from, int to, const PitchTrackOptions& o)
{
    std::vector<PitchFrame> frames;
    from = std::max (0, from);
    to = std::min (numSamples, to);
    if (fs <= 0.0 || from >= to)
        return frames;
    const int window = (int) std::lround (fs * 0.04);
    const int maxLag = std::min (window, (int) std::ceil (fs / o.minHz));
    const int minLag = std::max (2, (int) std::floor (fs / o.maxHz));
    const int hop = std::max (1, (int) std::lround (o.hopSeconds * fs));
    const int levelHalf = std::max (1, (int) std::lround (fs * 0.005));
    int order = 1;
    while ((1 << order) < 2 * window + maxLag)
        ++order;
    const int n = 1 << order;
    dsp::Fft fft (order);
    std::vector<float> a ((size_t) n * 2), b ((size_t) n * 2);
    std::vector<double> diff ((size_t) maxLag + 2), cmnd ((size_t) maxLag + 2);
    // 제곱의 누적 합 (창의 에너지와 RMS)
    std::vector<double> squares ((size_t) numSamples + 1, 0.0);
    for (int i = 0; i < numSamples; ++i)
        squares[(size_t) i + 1] = squares[(size_t) i] + (double) x[i] * x[i];
    auto energy = [&] (int begin, int end)
    {
        begin = std::clamp (begin, 0, numSamples);
        end = std::clamp (end, 0, numSamples);
        return end > begin ? squares[(size_t) end] - squares[(size_t) begin] : 0.0;
    };
    auto sample = [&] (int i) { return i >= 0 && i < numSamples ? x[i] : 0.0f; };

    for (int centre = from; centre < to; centre += hop)
    {
        PitchFrame f;
        f.seconds = centre / fs;
        const double level = energy (centre - levelHalf, centre + levelHalf) / (2.0 * levelHalf);
        f.levelDb = level > 1.0e-12 ? 10.0 * std::log10 (level) : -120.0;
        const int start = centre - window / 2;
        const double e1 = energy (start, start + window);
        if (e1 / window < 1.0e-10)
        {
            frames.push_back (f);
            continue;
        }
        // r(tau) = sum a[i] b[i + tau]: 두 FFT 의 곱 (a 의 켤레)
        std::fill (a.begin(), a.end(), 0.0f);
        std::fill (b.begin(), b.end(), 0.0f);
        for (int i = 0; i < window; ++i)
            a[(size_t) i] = sample (start + i);
        for (int i = 0; i < window + maxLag; ++i)
            b[(size_t) i] = sample (start + i);
        fft.forwardReal (a.data(), true);
        fft.forwardReal (b.data(), true);
        for (int k = 0; k <= n / 2; ++k)
        {
            const float ar = a[(size_t) 2 * k], ai = a[(size_t) 2 * k + 1], br = b[(size_t) 2 * k], bi = b[(size_t) 2 * k + 1];
            b[(size_t) 2 * k] = ar * br + ai * bi;
            b[(size_t) 2 * k + 1] = ar * bi - ai * br;
        }
        fft.inverseReal (b.data());
        // 차이 함수와 누적 평균으로 나눈 값
        double running = 0.0;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            const double e2 = energy (start + tau, start + tau + window);
            diff[(size_t) tau] = std::max (0.0, e1 + e2 - 2.0 * (double) b[(size_t) tau]);
            running += diff[(size_t) tau];
            cmnd[(size_t) tau] = running > 0.0 ? diff[(size_t) tau] * tau / running : 1.0;
        }
        int best = -1;
        double lowest = 1.0;
        for (int tau = minLag; tau < maxLag; ++tau)
        {
            lowest = std::min (lowest, cmnd[(size_t) tau]);
            if (cmnd[(size_t) tau] < o.threshold)
            {
                while (tau + 1 < maxLag && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
        {
            f.clarity = 1.0 - lowest;
            frames.push_back (f);
            continue;
        }
        // 포물선으로 사이 값을 어림한다
        double shift = 0.0;
        const double l = cmnd[(size_t) best - 1], m = cmnd[(size_t) best], r = cmnd[(size_t) best + 1];
        if (const double denom = l - 2.0 * m + r; std::abs (denom) > 1.0e-12)
            shift = std::clamp (0.5 * (l - r) / denom, -0.5, 0.5);
        const double frequency = fs / (best + shift);
        f.midi = 69.0 + 12.0 * std::log2 (frequency / 440.0);
        f.clarity = 1.0 - m;
        // 버금 후보 (고른 주기와 그 배수가 아닌 골, 깊은 것부터)
        std::array<std::pair<double, int>, 3> found {};
        int foundCount = 0;
        for (int tau = minLag + 1; tau < maxLag; ++tau)
        {
            const double v = cmnd[(size_t) tau];
            if (v >= 0.5 || v > cmnd[(size_t) tau - 1] || v > cmnd[(size_t) tau + 1])
                continue;
            const double ratio = (double) tau / best;
            if (std::abs (ratio - std::round (ratio)) * best < 2.0 + 0.02 * tau)
                continue;
            if (foundCount < 3)
                found[(size_t) foundCount++] = { v, tau };
            else if (v < found[2].first)
                found[2] = { v, tau };
            std::sort (found.begin(), found.begin() + foundCount);
        }
        for (int k = 0; k < foundCount; ++k)
            f.others[(size_t) k] = (float) (69.0 + 12.0 * std::log2 (fs / found[(size_t) k].second / 440.0));
        f.otherCount = foundCount;
        frames.push_back (f);
    }
    return frames;
}

std::vector<TranscribedNote> transcribeMelody (const float* mono, int numSamples, double fs, const TranscribeOptions& o, TranscribeFrames* detail)
{
    const auto frames = trackPitch (mono, numSamples, fs, 0, numSamples, o.pitch);
    std::vector<TranscribedNote> notes;
    if (frames.empty())
        return notes;
    const double hop = o.pitch.hopSeconds;
    double loudest = -120.0;
    for (const auto& f : frames)
        loudest = std::max (loudest, f.levelDb);
    const double floor = std::max (o.floorDb, loudest - o.rangeDb);
    const size_t count = frames.size();
    std::vector<bool> voiced (count);
    std::vector<double> pitch (count, 0.0);
    for (size_t i = 0; i < count; ++i)
    {
        voiced[i] = frames[i].midi > 0.0 && frames[i].clarity >= o.minClarity && frames[i].levelDb >= floor;
        pitch[i] = frames[i].midi;
    }
    // 옥타브 튐: 앞뒤 세 프레임의 중앙값과 옥타브만큼 다르면 그쪽으로 옮긴다
    {
        const auto raw = pitch;
        for (size_t i = 0; i < count; ++i)
        {
            if (! voiced[i])
                continue;
            std::vector<double> around;
            for (size_t j = i >= 3 ? i - 3 : 0; j <= std::min (count - 1, i + 3); ++j)
                if (voiced[j])
                    around.push_back (raw[j]);
            const double d = raw[i] - median (around);
            const double octaves = std::round (d / 12.0);
            if (octaves != 0.0 && std::abs (d - 12.0 * octaves) < 0.7)
                pitch[i] = raw[i] - 12.0 * octaves;
        }
    }

    const int gapFrames = std::max (1, (int) std::ceil (0.03 / hop));
    // 이어짐: 앞 높이보다 한 옥타브 넘게 낮은 프레임은 두 음이 겹친 자리의 긴 공통 주기다. 앞 높이 가까운(3 반음 안) 버금 후보가
    // 있으면 그것으로, 없으면 높이 없음으로 둔다. 30 ms 넘게 이어지면 진짜 도약으로 받아들인다
    {
        double previous = 0.0;
        size_t previousIndex = 0;
        for (size_t i = 0; i < count; ++i)
        {
            if (! voiced[i])
                continue;
            if (previous > 0.0 && i - previousIndex <= (size_t) gapFrames && pitch[i] < previous - 12.5)
            {
                double best = 0.0;
                for (int k = 0; k < frames[i].otherCount; ++k)
                {
                    const double c = frames[i].others[(size_t) k];
                    if (std::abs (c - previous) < 3.0 && (best == 0.0 || std::abs (c - previous) < std::abs (best - previous)))
                        best = c;
                }
                if (best == 0.0)
                {
                    voiced[i] = false;
                    continue;
                }
                pitch[i] = best;
            }
            previous = pitch[i];
            previousIndex = i;
        }
    }
    const int changeFrames = std::max (1, (int) std::ceil (0.04 / hop));
    const int riseFrames = std::max (1, (int) std::lround (0.02 / hop));
    const int reachBack = (int) std::ceil (0.02 / hop);   // 창의 반이 지난 뒤에야 높이가 잡힌다
    struct Open
    {
        size_t first = 0, last = 0;
        std::vector<double> pitches;
        double peak = -120.0;
        bool afterSilence = true;
    };
    std::optional<Open> open;
    std::vector<std::pair<Open, size_t>> closed;   // 음과 끝 프레임 (포함하지 않는다)
    auto close = [&] (size_t end)
    {
        closed.push_back ({ *open, end });
        open.reset();
    };
    auto begin = [&] (size_t first, bool afterSilence)
    {
        Open n;
        n.first = n.last = first;
        n.afterSilence = afterSilence;
        open = n;
    };
    int unvoiced = 0;
    int changeFrom = -1;
    for (size_t i = 0; i < count; ++i)
    {
        if (! voiced[i])
        {
            if (open && ++unvoiced >= gapFrames)
                close (open->last + 1);
            continue;
        }
        unvoiced = 0;
        if (! open)
        {
            begin (i, true);
            changeFrom = -1;
        }
        else
        {
            const double reference = median (open->pitches);
            if (std::abs (pitch[i] - reference) > 0.7)
            {
                if (changeFrom < 0)
                    changeFrom = (int) i;
                // 바뀐 높이가 자리 잡으면 (마지막 네 프레임이 1 반음 안) 확정한다. 두 음이 겹친 동안의 엉뚱한 높이로는 나누지 않는다
                std::vector<double> tail;
                for (size_t j = i + 1; j-- > (size_t) changeFrom && tail.size() < 4;)
                    if (voiced[j])
                        tail.push_back (pitch[j]);
                const bool settled = tail.size() == 4 && *std::max_element (tail.begin(), tail.end()) - *std::min_element (tail.begin(), tail.end()) <= 1.0;
                if ((int) i - changeFrom + 1 >= changeFrames && settled)
                {
                    // 앞 음의 높이가 끝난 자리에서 나누고, 새 높이가 버금 후보로 먼저 들리면 그 자리까지 거슬러 간다 (두 프레임까지 건너뛴다)
                    const double target = median (tail);
                    auto heard = [&] (size_t j)
                    {
                        if (std::abs (pitch[j] - target) < 0.6)
                            return true;
                        for (int k = 0; k < frames[j].otherCount; ++k)
                            if (std::abs (frames[j].others[(size_t) k] - target) < 0.6)
                                return true;
                        return false;
                    };
                    size_t from = (size_t) changeFrom;
                    const size_t earliest = open->first + (size_t) std::ceil (o.minNoteSeconds / hop);
                    int misses = 0;
                    for (size_t j = (size_t) changeFrom; j > earliest && (double) ((size_t) changeFrom - (j - 1)) * hop <= o.backtrackSeconds;)
                    {
                        --j;
                        if (heard (j))
                        {
                            from = j;
                            misses = 0;
                        }
                        else if (++misses > 2)
                            break;
                    }
                    close (from);
                    begin (from, false);
                    for (size_t j = (size_t) changeFrom; j < i; ++j)
                        if (voiced[j] && std::abs (pitch[j] - target) < 0.7)
                        {
                            open->pitches.push_back (pitch[j]);
                            open->peak = std::max (open->peak, frames[j].levelDb);
                        }
                    changeFrom = -1;
                }
            }
            else
                changeFrom = -1;
            // 다시 치는 어택: 20 ms 안에 6 dB 넘게 오른다 (앞의 가장 작은 자리에서 나눈다)
            if (open && i >= (size_t) riseFrames && (double) (i - open->first) * hop >= o.minNoteSeconds)
            {
                size_t lowest = i - (size_t) riseFrames;
                for (size_t j = lowest; j < i; ++j)
                    if (frames[j].levelDb < frames[lowest].levelDb)
                        lowest = j;
                if (lowest > open->first && frames[i].levelDb - frames[lowest].levelDb >= 6.0)
                {
                    close (lowest + 1);
                    begin (lowest + 1, false);
                    for (size_t j = lowest + 1; j < i; ++j)
                        if (voiced[j])
                            open->pitches.push_back (pitch[j]);
                    changeFrom = -1;
                }
            }
        }
        open->pitches.push_back (pitch[i]);
        open->peak = std::max (open->peak, frames[i].levelDb);
        open->last = i;
    }
    if (open)
        close (open->last + 1);
    if (detail != nullptr)
    {
        detail->frames = frames;
        detail->pitch = pitch;
        detail->voiced = voiced;
    }

    double loudestNote = -120.0;
    for (auto& [n, end] : closed)
    {
        if (n.pitches.empty() || end <= n.first)
            continue;
        double start = frames[n.first].seconds;
        if (n.afterSilence)
        {
            // 쉼 뒤의 음: 높이가 잡히기 전 소리가 차오른 자리까지 거슬러 간다
            const double threshold = n.peak - 15.0;
            size_t j = n.first;
            while (j > 0 && n.first - (j - 1) <= (size_t) reachBack && frames[j - 1].levelDb >= threshold)
                --j;
            start = frames[j].seconds;
        }
        const double end2 = frames[end - 1].seconds + hop;
        if (end2 - start < o.minNoteSeconds)
            continue;
        const double m = median (n.pitches);
        TranscribedNote t;
        t.startSeconds = start;
        t.endSeconds = end2;
        t.pitch = std::clamp ((int) std::lround (m), 0, 127);
        t.cents = std::round ((m - t.pitch) * 1000.0) / 10.0;
        t.velocity = (int) std::lround (n.peak * 100.0);   // 잠시 봉우리 dB 를 담는다 (아래에서 바꾼다)
        loudestNote = std::max (loudestNote, n.peak);
        notes.push_back (t);
    }
    // 세기: 가장 큰 음을 100 으로, 1 dB 에 1.5 씩
    for (auto& t : notes)
        t.velocity = std::clamp ((int) std::lround (100.0 + (t.velocity / 100.0 - loudestNote) * 1.5), 20, 127);
    return notes;
}

juce::Array<juce::var> transcribedNotesJson (const std::vector<TranscribedNote>& notes, const std::function<double (double)>& beatOf, int transpose)
{
    auto round5 = [] (double v) { return std::round (v * 100000.0) / 100000.0; };
    juce::Array<juce::var> out;
    for (const auto& n : notes)
    {
        const double start = round5 (beatOf (n.startSeconds)), end = round5 (beatOf (n.endSeconds));
        if (end <= start)
            continue;
        auto* o = new juce::DynamicObject();
        o->setProperty ("pitch", std::clamp (n.pitch + transpose, 0, 127));
        o->setProperty ("startBeat", start);
        o->setProperty ("durationBeat", round5 (end - start));
        o->setProperty ("velocity", n.velocity);
        o->setProperty ("humanize", false);
        out.add (juce::var (o));
    }
    return out;
}

std::vector<TranscribedNote> transcribeMelody (const juce::File& audio, double startSeconds, std::optional<double> lengthSeconds, const TranscribeOptions& o)
{
    const auto data = readAudioFile (audio);
    const auto& b = data.buffer;
    const int first = std::clamp ((int) std::lround (startSeconds * data.sampleRate), 0, b.getNumSamples());
    int last = b.getNumSamples();
    if (lengthSeconds)
        last = std::clamp ((int) std::lround ((startSeconds + *lengthSeconds) * data.sampleRate), first, last);
    std::vector<float> mono ((size_t) (last - first), 0.0f);
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = first; i < last; ++i)
            mono[(size_t) (i - first)] += b.getSample (ch, i) / (float) b.getNumChannels();
    auto notes = transcribeMelody (mono.data(), (int) mono.size(), data.sampleRate, o);
    for (auto& n : notes)
    {
        n.startSeconds += first / data.sampleRate;
        n.endSeconds += first / data.sampleRate;
    }
    return notes;
}

bool correctionVoiced (const PitchFrame& f, double loudestDb)
{
    return f.midi > 0.0 && f.clarity >= 0.8 && f.levelDb > std::max (-60.0, loudestDb - 40.0);
}

std::vector<CorrectionPoint> pitchCorrectionCurve (const float* mono, int numSamples, double fs, int from, int to,
                                                   const std::array<bool, 12>& allowed, double strength, double speedMs, double transpose)
{
    PitchTrackOptions options;
    options.minHz = 60.0;
    options.maxHz = 1500.0;
    return pitchCorrectionCurve (trackPitch (mono, numSamples, fs, from, to, options), allowed, strength, speedMs, transpose);
}

std::vector<CorrectionPoint> pitchCorrectionCurve (const std::vector<PitchFrame>& frames, const std::array<bool, 12>& allowed, double strength,
                                                   double speedMs, double transpose)
{
    if (frames.empty() || std::none_of (allowed.begin(), allowed.end(), [] (bool a) { return a; }))
        return {};
    double loudest = -120.0;
    for (const auto& f : frames)
        loudest = std::max (loudest, f.levelDb);
    // 가까운 허락된 음
    auto nearest = [&allowed] (double midi)
    {
        const int base = (int) std::floor (midi);
        double best = 0.0, distance = 1.0e9;
        for (int k = base - 6; k <= base + 7; ++k)
            if (allowed[(size_t) (((k % 12) + 12) % 12)] && std::abs (k - midi) < distance)
            {
                distance = std::abs (k - midi);
                best = k;
            }
        return best;
    };
    std::vector<CorrectionPoint> curve;
    curve.reserve (frames.size());
    const double hop = frames.size() > 1 ? frames[1].seconds - frames[0].seconds : 0.005;
    const double a = speedMs > 0.0 ? 1.0 - std::exp (-hop / (speedMs * 0.001)) : 1.0;
    double smoothed = 0.0, held = 0.0, target = -1.0, silentFor = 1.0e9;
    for (const auto& f : frames)
    {
        double raw = 0.0;
        const bool voiced = correctionVoiced (f, loudest);
        if (voiced)
        {
            const double heard = f.midi + transpose;
            if (target < 0.0 || std::abs (heard - target) > 0.65 || ! allowed[(size_t) (((int) target % 12 + 12) % 12)])
                target = nearest (heard);
            raw = (target - heard) * std::clamp (strength, 0.0, 1.0);
            held = raw;
            silentFor = 0.0;
        }
        else
        {
            silentFor += hop;
            raw = silentFor < 0.05 ? held : 0.0;
            if (silentFor >= 0.05)
                target = -1.0;
        }
        smoothed += (raw - smoothed) * a;
        curve.push_back ({ f.seconds, smoothed, voiced ? f.midi : 0.0 });
    }
    return curve;
}

double measuredPitchAt (const std::vector<CorrectionPoint>& curve, double seconds)
{
    if (curve.empty())
        return 0.0;
    const auto it = std::lower_bound (curve.begin(), curve.end(), seconds, [] (const CorrectionPoint& p, double s) { return p.seconds < s; });
    if (it == curve.end())
        return curve.back().midi;
    if (it != curve.begin() && seconds - (it - 1)->seconds < it->seconds - seconds)
        return (it - 1)->midi;
    return it->midi;
}

double correctionAt (const std::vector<CorrectionPoint>& curve, double seconds)
{
    if (curve.empty())
        return 0.0;
    if (seconds <= curve.front().seconds)
        return curve.front().semitones;
    if (seconds >= curve.back().seconds)
        return curve.back().semitones;
    const auto it = std::upper_bound (curve.begin(), curve.end(), seconds, [] (double s, const CorrectionPoint& p) { return s < p.seconds; });
    const auto& p1 = *it;
    const auto& p0 = *(it - 1);
    return p0.semitones + (p1.semitones - p0.semitones) * (seconds - p0.seconds) / (p1.seconds - p0.seconds);
}

} // namespace mr
