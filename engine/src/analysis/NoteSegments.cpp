#include "analysis/NoteSegments.h"

#include "core/Diagnostics.h"
#include "io/AudioFile.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

namespace mr
{

namespace
{

constexpr double edgeRamp = 0.01;    // 반음 옮김이 경계 앞뒤로 들어오고 나가는 시간 (초)
constexpr double innerRamp = 0.02;   // 펴기와 비브라토가 음 안에서 들어오고 나가는 시간
constexpr double slowWindow = 0.2;   // 느린 흔들림의 이동 평균 (초, 두 번)
constexpr double edgeTolerance = 0.5;   // 음 머리와 꼬리에서 넘어오는 높이로 보는 거리 (반음)

double median (std::vector<double> v)
{
    if (v.empty())
        return 0.0;
    const auto mid = v.begin() + (long) v.size() / 2;
    std::nth_element (v.begin(), mid, v.end());
    return *mid;
}

double sinSquared (double t)   // 0..1 -> 0..1
{
    const double s = std::sin (0.5 * juce::MathConstants<double>::pi * std::clamp (t, 0.0, 1.0));
    return s * s;
}

// 가운데 맞춘 이동 평균 (양 끝은 있는 프레임만)
std::vector<double> movingAverage (const std::vector<double>& x, int half)
{
    const size_t n = x.size();
    std::vector<double> sums (n + 1, 0.0), out (n, 0.0);
    for (size_t i = 0; i < n; ++i)
        sums[i + 1] = sums[i] + x[i];
    for (size_t i = 0; i < n; ++i)
    {
        const size_t lo = i >= (size_t) half ? i - (size_t) half : 0, hi = std::min (n - 1, i + (size_t) half);
        out[i] = (sums[hi + 1] - sums[lo]) / (double) (hi + 1 - lo);
    }
    return out;
}

double deviation (const std::vector<double>& x, const std::vector<bool>& voiced, double around)
{
    double sum = 0.0;
    int count = 0;
    for (size_t i = 0; i < x.size(); ++i)
        if (voiced[i])
        {
            sum += (x[i] - around) * (x[i] - around);
            ++count;
        }
    return count > 0 ? std::sqrt (sum / count) : 0.0;
}

std::vector<NoteSegment> segmentsOf (const std::vector<TranscribedNote>& notes, const TranscribeFrames& d)
{
    std::vector<NoteSegment> out;
    const auto& frames = d.frames;
    if (frames.empty())
        return out;
    const double hop = frames.size() > 1 ? frames[1].seconds - frames[0].seconds : 0.005;
    for (const auto& n : notes)
    {
        NoteSegment s;
        s.startSeconds = n.startSeconds;
        s.endSeconds = n.endSeconds;
        s.velocity = n.velocity;
        s.hopSeconds = hop;
        const auto first = std::lower_bound (frames.begin(), frames.end(), n.startSeconds - 0.5 * hop,
                                             [] (const PitchFrame& f, double t) { return f.seconds < t; });
        std::vector<double> voicedPitch;
        for (auto it = first; it != frames.end() && it->seconds < n.endSeconds; ++it)
        {
            const auto i = (size_t) (it - frames.begin());
            if (s.curve.empty())
                s.curveStart = it->seconds;
            const bool on = d.voiced[i] && d.pitch[i] > 0.0;
            s.curve.push_back (on ? (float) d.pitch[i] : 0.0f);
            if (on)
                voicedPitch.push_back (d.pitch[i]);
        }
        s.midi = voicedPitch.empty() ? n.pitch + n.cents / 100.0 : median (voicedPitch);
        const auto parts = splitPitch (s.curve, hop);
        std::vector<double> drift (parts.slow.size());
        for (size_t i = 0; i < drift.size(); ++i)
            drift[i] = parts.slow[i] - parts.centre;
        s.driftCents = 100.0 * deviation (drift, parts.voiced, 0.0);
        s.vibratoCents = 100.0 * deviation (parts.fast, parts.voiced, 0.0);
        out.push_back (std::move (s));
    }
    return out;
}

std::mutex& segmentMutex()
{
    static std::mutex m;
    return m;
}

std::map<juce::String, std::shared_ptr<const std::vector<NoteSegment>>>& segmentCache()
{
    static std::map<juce::String, std::shared_ptr<const std::vector<NoteSegment>>> cache;
    return cache;
}

juce::String segmentKey (const juce::File& file)
{
    return file.getFullPathName() + "|" + juce::String (file.getLastModificationTime().toMilliseconds()) + "|" + juce::String (file.getSize());
}

} // namespace

PitchParts splitPitch (const std::vector<float>& curve, double hop)
{
    PitchParts p;
    const size_t n = curve.size();
    p.voiced.resize (n);
    p.slow.assign (n, 0.0);
    p.fast.assign (n, 0.0);
    std::vector<double> heard;
    for (size_t i = 0; i < n; ++i)
    {
        p.voiced[i] = curve[i] > 0.0f;
        if (p.voiced[i])
            heard.push_back (curve[i]);
    }
    if (heard.empty())
        return p;
    p.centre = median (heard);
    // 옥타브를 접고, 높이 없는 프레임은 앞뒤의 소리 있는 프레임을 곧게 잇는다 (양 끝은 붙든다)
    std::vector<double> x (n, 0.0);
    for (size_t i = 0; i < n; ++i)
        if (p.voiced[i])
        {
            double v = curve[i];
            if (std::abs (v - p.centre) >= 9.0)
                v -= 12.0 * std::round ((v - p.centre) / 12.0);
            x[i] = v;
        }
    // 음의 처음과 끝에서 가운데와 반음의 반 넘게 다른 프레임은 이웃 음에서 넘어오는 자리라 흔들림에 넣지 않는다
    auto transition = [&] (size_t i) { return ! p.voiced[i] || std::abs (x[i] - p.centre) > edgeTolerance; };
    for (size_t i = 0; i < n && transition (i); ++i)
        p.voiced[i] = false;
    for (size_t i = n; i-- > 0 && transition (i);)
        p.voiced[i] = false;
    long previous = -1;
    for (size_t i = 0; i <= n; ++i)
    {
        if (i < n && ! p.voiced[i])
            continue;
        const long next = (long) i;
        for (long k = previous + 1; k < next; ++k)
        {
            if (previous < 0)
                x[(size_t) k] = x[(size_t) next];
            else if (next >= (long) n)
                x[(size_t) k] = x[(size_t) previous];
            else
                x[(size_t) k] = x[(size_t) previous] + (x[(size_t) next] - x[(size_t) previous]) * (double) (k - previous) / (double) (next - previous);
        }
        previous = next;
    }
    const int half = std::max (1, (int) std::lround (0.5 * slowWindow / std::max (1.0e-4, hop)));
    p.slow = movingAverage (movingAverage (x, half), half);
    for (size_t i = 0; i < n; ++i)
        p.fast[i] = x[i] - p.slow[i];
    return p;
}

double pitchEditShape (const PitchParts& parts, size_t frame, const PitchEdit& edit)
{
    if (frame >= parts.slow.size() || ! parts.voiced[frame])
        return 0.0;   // 높이 없는 프레임과 이웃 음에서 넘어오는 자리
    const double drift = std::clamp (parts.slow[frame] - parts.centre, -3.0, 3.0);
    const double fast = std::clamp (parts.fast[frame], -1.5, 1.5);
    return -edit.flatten * drift + (edit.vibrato - 1.0) * fast;
}

std::vector<float> editedPitchCurve (const NoteSegment& s, const PitchEdit& e)
{
    const auto parts = splitPitch (s.curve, s.hopSeconds);
    std::vector<float> out (s.curve.size(), 0.0f);
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (s.curve[i] <= 0.0f)
            continue;
        const double t = s.curveStart + (double) i * s.hopSeconds;
        const double r = std::min (sinSquared ((t - e.seconds) / innerRamp), sinSquared ((e.endSeconds() - t) / innerRamp));
        const bool inside = t >= e.seconds && t <= e.endSeconds();
        out[i] = (float) (s.curve[i] + (inside ? e.semitones + r * pitchEditShape (parts, i, e) : 0.0));
    }
    return out;
}

std::vector<CorrectionPoint> pitchEditCurve (const std::vector<PitchFrame>& frames, const std::vector<PitchEdit>& edits, double offset,
                                             const std::vector<CorrectionPoint>& correction)
{
    const size_t n = frames.size();
    std::vector<CorrectionPoint> out (n);
    double loudest = -120.0;
    for (const auto& f : frames)
        loudest = std::max (loudest, f.levelDb);
    for (size_t i = 0; i < n; ++i)
    {
        out[i].seconds = frames[i].seconds;
        out[i].midi = correctionVoiced (frames[i], loudest) ? frames[i].midi : 0.0;
        out[i].semitones = correction.size() == n ? correction[i].semitones : correctionAt (correction, frames[i].seconds);
    }
    if (n == 0 || edits.empty())
        return out;
    const double hop = n > 1 ? frames[1].seconds - frames[0].seconds : 0.005;
    std::vector<double> weight (n, 0.0), sum (n, 0.0);
    auto at = [&frames] (double t)
    {
        return (size_t) (std::lower_bound (frames.begin(), frames.end(), t, [] (const PitchFrame& f, double s) { return f.seconds < s; }) - frames.begin());
    };
    for (const auto& e : edits)
    {
        const double s0 = e.seconds - offset, s1 = e.endSeconds() - offset;
        if (s1 + edgeRamp < frames.front().seconds || s0 - edgeRamp > frames.back().seconds)
            continue;
        // 음 안의 잰 높이로 흔들림을 나눈다
        const size_t first = at (s0), last = at (s1);
        std::vector<float> curve;
        for (size_t i = first; i < last && i < n; ++i)
            curve.push_back ((float) out[i].midi);
        const auto parts = splitPitch (curve, hop);
        for (size_t i = at (s0 - edgeRamp); i < n && frames[i].seconds <= s1 + edgeRamp; ++i)
        {
            const double t = frames[i].seconds;
            const double w = std::min (sinSquared ((t - (s0 - edgeRamp)) / (2.0 * edgeRamp)), sinSquared (((s1 + edgeRamp) - t) / (2.0 * edgeRamp)));
            double shape = 0.0;
            if (i >= first && i < last && out[i].midi > 0.0)
            {
                const double r = std::min (sinSquared ((t - s0) / innerRamp), sinSquared ((s1 - t) / innerRamp));
                shape = r * pitchEditShape (parts, i - first, e);
            }
            sum[i] += w * (e.semitones + shape);
            weight[i] += w;
        }
    }
    for (size_t i = 0; i < n; ++i)
    {
        if (weight[i] <= 0.0)
            continue;
        if (weight[i] > 1.0)
        {
            sum[i] /= weight[i];
            weight[i] = 1.0;
        }
        out[i].semitones = (1.0 - weight[i]) * out[i].semitones + sum[i];
    }
    return out;
}

std::vector<NoteSegment> noteSegments (const float* mono, int numSamples, double sampleRate, const TranscribeOptions& options)
{
    TranscribeFrames detail;
    const auto notes = transcribeMelody (mono, numSamples, sampleRate, options, &detail);
    return segmentsOf (notes, detail);
}

std::vector<NoteSegment> noteSegments (const juce::File& audio, double startSeconds, std::optional<double> lengthSeconds)
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
    auto segments = noteSegments (mono.data(), (int) mono.size(), data.sampleRate);
    const double shift = first / data.sampleRate;
    for (auto& s : segments)
    {
        s.startSeconds += shift;
        s.endSeconds += shift;
        s.curveStart += shift;
    }
    return segments;
}

std::pair<size_t, size_t> segmentRange (const std::vector<NoteSegment>& all, double from, double to)
{
    size_t first = 0;
    while (first < all.size() && 0.5 * (all[first].startSeconds + all[first].endSeconds) < from)
        ++first;
    size_t last = first;
    while (last < all.size() && 0.5 * (all[last].startSeconds + all[last].endSeconds) < to)
        ++last;
    return { first, last };
}

std::shared_ptr<const std::vector<NoteSegment>> cachedFileNoteSegments (const juce::File& audio)
{
    const auto key = segmentKey (audio);
    std::lock_guard<std::mutex> lock (segmentMutex());
    const auto it = segmentCache().find (key);
    return it != segmentCache().end() ? it->second : nullptr;
}

std::shared_ptr<const std::vector<NoteSegment>> fileNoteSegments (const juce::File& audio)
{
    if (auto cached = cachedFileNoteSegments (audio))
        return cached;
    std::vector<NoteSegment> segments;
    try
    {
        segments = noteSegments (audio);
    }
    catch (const Failure&)
    {
    }
    auto result = std::make_shared<const std::vector<NoteSegment>> (std::move (segments));
    std::lock_guard<std::mutex> lock (segmentMutex());
    // 다른 스레드가 먼저 넣었으면 그것을 쓴다 (넣은 목록은 바꾸지 않는다: 음을 가리키는 포인터가 살아 있다)
    return segmentCache().emplace (segmentKey (audio), result).first->second;
}

} // namespace mr
