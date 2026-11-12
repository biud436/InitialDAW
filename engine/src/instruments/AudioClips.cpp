#include "instruments/AudioClips.h"

#include "analysis/NoteSegments.h"
#include "analysis/PitchTrack.h"

#include "io/AudioFile.h"
#include "io/Codecs.h"

#include <juce_audio_basics/juce_audio_basics.h>

#if MUSIC_RENDER_HAS_STRETCH
 #include <signalsmith-stretch/signalsmith-stretch.h>
#endif

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <vector>

namespace mr
{

namespace
{

constexpr int minimumFade = 32;   // 클립 양 끝의 짧은 페이드 (샘플)
constexpr int jumpFade = 64;      // 위치가 끊긴 블록의 처음

std::mutex& cacheMutex()
{
    static std::mutex m;
    return m;
}

juce::String cacheKey (const juce::File& file, double rate)
{
    return file.getFullPathName() + "|" + juce::String (rate) + "|" + juce::String (file.getLastModificationTime().toMilliseconds());
}

// 창을 씌운 sinc 로 샘플레이트를 바꾼다 (낮출 때는 차단 주파수를 함께 낮춘다). 커널은 표로 만들어 두고 사이를 잇는다
void resample (const float* in, int64_t inFrames, double ratio, float* out, int64_t outFrames)
{
    constexpr int taps = 16;
    constexpr int steps = 1024;   // 입력 한 칸을 나눈 표의 칸
    const double cutoff = std::min (1.0, 1.0 / ratio);
    const int reach = (int) std::ceil (taps / cutoff);
    std::vector<float> table ((size_t) (reach * steps + 2));
    for (size_t i = 0; i < table.size(); ++i)
    {
        const double x = (double) i / steps * cutoff;   // 중심에서의 거리 (차단 주파수로 줄인 칸)
        if (x >= taps)
            continue;
        const double t = 0.5 + 0.5 * (x / taps);
        const double w = 0.42 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * t) + 0.08 * std::cos (4.0 * juce::MathConstants<double>::pi * t);
        const double sinc = x < 1.0e-12 ? 1.0 : std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
        table[i] = (float) (sinc * w * cutoff);
    }
    auto kernel = [&table] (double distance)
    {
        const double pos = std::abs (distance) * steps;
        const auto i = (size_t) pos;
        if (i + 1 >= table.size())
            return 0.0f;
        const float frac = (float) (pos - (double) i);
        return table[i] + (table[i + 1] - table[i]) * frac;
    };
    for (int64_t j = 0; j < outFrames; ++j)
    {
        const double t = (double) j * ratio;
        const int64_t centre = (int64_t) std::floor (t);
        const int64_t k0 = std::max<int64_t> (0, centre - reach + 1), k1 = std::min<int64_t> (inFrames - 1, centre + reach);
        float sum = 0.0f;
        for (int64_t k = k0; k <= k1; ++k)
            sum += in[k] * kernel (t - (double) k);
        out[j] = sum;
    }
}

float fadeShape (double t)   // 0..1 -> 0..1 (사인 제곱)
{
    const double s = std::sin (0.5 * juce::MathConstants<double>::pi * std::clamp (t, 0.0, 1.0));
    return (float) (s * s);
}

} // namespace

float fadeGain (FadeShape shape, double t)
{
    t = std::clamp (t, 0.0, 1.0);
    const double halfPi = 0.5 * juce::MathConstants<double>::pi;
    switch (shape)
    {
        case FadeShape::linear: return (float) t;
        case FadeShape::equalPower: return (float) std::sin (halfPi * t);
        case FadeShape::exponential:
        {
            constexpr double floor = 1.0e-3;   // -60 dB
            return (float) ((std::pow (10.0, -3.0 * (1.0 - t)) - floor) / (1.0 - floor));
        }
        case FadeShape::sine: break;
    }
    const double s = std::sin (halfPi * t);
    return (float) (s * s);
}

bool isAudioFileName (const juce::String& name)
{
    for (const auto* ext : { ".wav", ".wave", ".aif", ".aiff", ".flac", ".ogg", ".mp3" })
        if (name.endsWithIgnoreCase (ext))
            return true;
    return false;
}

std::optional<AudioFileInfo> audioFileInfo (const juce::File& file)
{
    if (! file.existsAsFile())
        return std::nullopt;
    const auto probe = codec::probeFile (file.getFullPathName().toStdString());
    if (! probe || probe->sampleRate <= 0.0)
        return std::nullopt;
    return AudioFileInfo { probe->sampleRate, probe->frames, probe->channels };
}

juce::File clipFile (const AudioClip& clip, const juce::File& songFolder)
{
    const auto path = juce::String::fromUTF8 (clip.file.c_str());
    if (juce::File::isAbsolutePath (path))
        return juce::File (path);
    return songFolder.getChildFile (path);
}

double clipLengthSeconds (const AudioClip& clip, const juce::File& songFolder)
{
    if (clip.lengthSeconds)
        return *clip.lengthSeconds;
    if (const auto info = audioFileInfo (clipFile (clip, songFolder)))
        return std::max (0.0, info->seconds() - clip.offsetSeconds);
    return 0.0;
}

namespace
{
// 마커 밖의 빠르기 (박/초): sourceBpm, 없으면 끝 쪽 마커 사이
double outerRate (const AudioClip& clip, const WarpPoint& a, const WarpPoint& b)
{
    return clip.sourceBpm ? *clip.sourceBpm / 60.0 : (b.beat - a.beat) / (b.seconds - a.seconds);
}
} // namespace

double warpBeatAt (const AudioClip& clip, double sourceSeconds)
{
    const auto& w = clip.warp;
    if (w.size() < 2)
        return clip.sourceBpm ? sourceSeconds * *clip.sourceBpm / 60.0 : sourceSeconds;
    if (sourceSeconds <= w.front().seconds)
        return w.front().beat + (sourceSeconds - w.front().seconds) * outerRate (clip, w[0], w[1]);
    if (sourceSeconds >= w.back().seconds)
        return w.back().beat + (sourceSeconds - w.back().seconds) * outerRate (clip, w[w.size() - 2], w.back());
    const auto it = std::upper_bound (w.begin(), w.end(), sourceSeconds, [] (double s, const WarpPoint& p) { return s < p.seconds; });
    const auto& a = *(it - 1);
    const auto& b = *it;
    return a.beat + (sourceSeconds - a.seconds) * (b.beat - a.beat) / (b.seconds - a.seconds);
}

double warpSecondsAt (const AudioClip& clip, double beat)
{
    const auto& w = clip.warp;
    if (w.size() < 2)
        return clip.sourceBpm ? beat * 60.0 / *clip.sourceBpm : beat;
    if (beat <= w.front().beat)
        return w.front().seconds + (beat - w.front().beat) / outerRate (clip, w[0], w[1]);
    if (beat >= w.back().beat)
        return w.back().seconds + (beat - w.back().beat) / outerRate (clip, w[w.size() - 2], w.back());
    const auto it = std::upper_bound (w.begin(), w.end(), beat, [] (double b, const WarpPoint& p) { return b < p.beat; });
    const auto& a = *(it - 1);
    const auto& b = *it;
    return a.seconds + (beat - a.beat) * (b.seconds - a.seconds) / (b.beat - a.beat);
}

double clipSourceAt (const AudioClip& clip, const TempoMap& tempo, double songSeconds)
{
    if (clip.warp.size() >= 2)
        return warpSecondsAt (clip, tempo.beatAt (songSeconds) - clip.startBeat + warpBeatAt (clip, clip.offsetSeconds));
    if (clip.sourceBpm)
        return clip.offsetSeconds + (tempo.beatAt (songSeconds) - clip.startBeat) * 60.0 / *clip.sourceBpm;
    return clip.offsetSeconds + (songSeconds - tempo.secondsAt (clip.startBeat)) / clip.stretch;
}

double clipSongSecondsAt (const AudioClip& clip, const TempoMap& tempo, double sourceSeconds)
{
    if (clip.warp.size() >= 2)
        return tempo.secondsAt (clip.startBeat + warpBeatAt (clip, sourceSeconds) - warpBeatAt (clip, clip.offsetSeconds));
    if (clip.sourceBpm)
        return tempo.secondsAt (clip.startBeat + (sourceSeconds - clip.offsetSeconds) * *clip.sourceBpm / 60.0);
    return tempo.secondsAt (clip.startBeat) + (sourceSeconds - clip.offsetSeconds) * clip.stretch;
}

namespace
{
// 마커를 바꾼 뒤 클립 머리(offsetSeconds)의 박이 바뀐 만큼 startBeat 를 옮겨, 다른 자리가 곡의 같은 박에 남게 한다
AudioClip keepingPlace (const AudioClip& before, AudioClip after)
{
    after.startBeat = std::max (0.0, before.startBeat + warpBeatAt (after, after.offsetSeconds) - warpBeatAt (before, before.offsetSeconds));
    return after;
}
} // namespace

double warpMarkerSongBeat (const AudioClip& clip, size_t index)
{
    return index < clip.warp.size() ? clip.startBeat + clip.warp[index].beat - warpBeatAt (clip, clip.offsetSeconds) : clip.startBeat;
}

AudioClip withWarpMarkerAdded (const AudioClip& clip, const TempoMap& tempo, double lengthSeconds, double sourceSeconds)
{
    auto out = clip;
    if (clip.warp.size() < 2)
    {
        // 지금의 지도 그대로: 클립 머리가 0 박
        auto localBeat = [&] (double s) { return tempo.beatAt (clipSongSecondsAt (clip, tempo, s)) - clip.startBeat; };
        out.warp.clear();
        for (const double s : { clip.offsetSeconds, sourceSeconds, clip.offsetSeconds + lengthSeconds })
            out.warp.push_back ({ s, localBeat (s) });
        out.stretch = 1.0;
    }
    else
        out.warp.push_back ({ sourceSeconds, warpBeatAt (clip, sourceSeconds) });
    std::sort (out.warp.begin(), out.warp.end(), [] (const WarpPoint& a, const WarpPoint& b) { return a.seconds < b.seconds; });
    // 같은 자리(1 ms 안)의 마커는 하나로
    std::vector<WarpPoint> unique;
    for (const auto& w : out.warp)
        if (unique.empty() || (w.seconds > unique.back().seconds + 1.0e-3 && w.beat > unique.back().beat + 1.0e-6))
            unique.push_back (w);
    out.warp = unique;
    if (out.warp.size() < 2)
        out.warp.clear();
    return out;
}

AudioClip withWarpMarkerMoved (const AudioClip& clip, size_t index, double beat)
{
    if (index >= clip.warp.size())
        return clip;
    auto out = clip;
    constexpr double gap = 1.0e-3;
    double lo = -1.0e300, hi = 1.0e300;
    if (index > 0)
        lo = clip.warp[index - 1].beat + gap;
    if (index + 1 < clip.warp.size())
        hi = clip.warp[index + 1].beat - gap;
    out.warp[index].beat = std::round (std::clamp (beat, lo, hi) * 1.0e6) / 1.0e6;
    return keepingPlace (clip, out);
}

AudioClip withWarpMarkerRemoved (const AudioClip& clip, size_t index)
{
    if (index >= clip.warp.size())
        return clip;
    auto out = clip;
    out.warp.erase (out.warp.begin() + (long) index);
    if (out.warp.size() < 2)
    {
        out.warp.clear();
        return out;   // 지도가 sourceBpm 이나 파일의 초로 돌아간다 (클립 머리는 제자리)
    }
    return keepingPlace (clip, out);
}

std::vector<WarpPoint> quantizedWarp (const AudioClip& clip, const TempoMap& tempo, double lengthSeconds, const std::vector<double>& onsets,
                                      const AudioQuantizeOptions& o)
{
    if ((o.grid <= 0.0 && ! o.groove) || lengthSeconds <= 0.0)
        return {};
    const double s0 = clip.offsetSeconds, s1 = s0 + lengthSeconds;
    auto songBeat = [&] (double s) { return tempo.beatAt (clipSongSecondsAt (clip, tempo, s)); };
    auto target = [&o] (double beat)
    {
        if (o.groove)
            return grooveTarget (*o.groove, beat);
        const double index = std::round (beat / o.grid);
        double t = index * o.grid;
        if (((long long) index) % 2 != 0)
            t += o.grid / 3.0 * std::clamp (o.swing, 0.0, 1.0);
        return t;
    };
    struct Mark
    {
        double seconds, original, beat, distance;
        bool fixed;
    };
    std::vector<Mark> marks;
    marks.push_back ({ s0, songBeat (s0), songBeat (s0), 0.0, true });
    const double strength = std::clamp (o.strength, 0.0, 1.0);
    constexpr double edge = 0.01;   // 클립 끝에 붙은 어택은 옮기지 않는다
    for (const double s : onsets)
    {
        if (s <= s0 + edge || s >= s1 - edge)
            continue;
        const double b = songBeat (s);
        const double t = target (b);
        marks.push_back ({ s, b, b + (t - b) * strength, std::abs (t - b), false });
    }
    marks.push_back ({ s1, songBeat (s1), songBeat (s1), 0.0, true });
    // 순서가 뒤집히거나 같은 자리로 가거나, 마커 사이가 4배 넘게 늘고 줄면 옮길 거리가 큰 쪽을 뺀다
    for (bool changed = true; changed;)
    {
        changed = false;
        for (size_t i = 0; i + 1 < marks.size(); ++i)
        {
            const auto& a = marks[i];
            const auto& b = marks[i + 1];
            const double was = b.original - a.original, now = b.beat - a.beat;
            if (now > 1.0e-6 && now <= was * 4.0 && now >= was * 0.25)
                continue;
            if (a.fixed && b.fixed)
                continue;
            const size_t drop = a.fixed ? i + 1 : b.fixed ? i : (a.distance >= b.distance ? i : i + 1);
            marks.erase (marks.begin() + (long) drop);
            changed = true;
            break;
        }
    }
    const bool moved = std::any_of (marks.begin(), marks.end(), [] (const Mark& m) { return std::abs (m.beat - m.original) > 1.0e-6; });
    if (! moved)
        return {};
    std::vector<WarpPoint> out;
    auto tidy = [] (double v) { return std::round (v * 1.0e6) / 1.0e6; };
    for (const auto& m : marks)
    {
        const WarpPoint p { tidy (m.seconds), tidy (m.beat - clip.startBeat) };
        if (out.empty() || (p.seconds > out.back().seconds && p.beat > out.back().beat))
            out.push_back (p);
    }
    return out.size() >= 2 ? out : std::vector<WarpPoint>();
}

double clipEndSeconds (const AudioClip& clip, const TempoMap& tempo, const juce::File& songFolder)
{
    return clipSongSecondsAt (clip, tempo, clip.offsetSeconds + clipLengthSeconds (clip, songFolder));
}

bool clipStretchAvailable()
{
#if MUSIC_RENDER_HAS_STRETCH
    return true;
#else
    return false;
#endif
}

std::shared_ptr<const juce::AudioBuffer<float>> loadClipAudio (const juce::File& file, double sampleRate, std::string& error, bool reversed)
{
    static std::map<juce::String, std::weak_ptr<const juce::AudioBuffer<float>>> cache;
    const auto key = cacheKey (file, sampleRate) + (reversed ? "|reversed" : "");
    {
        std::lock_guard<std::mutex> lock (cacheMutex());
        if (auto it = cache.find (key); it != cache.end())
            if (auto alive = it->second.lock())
                return alive;
    }
    if (! file.existsAsFile())
    {
        error = "audio file not found: " + file.getFullPathName().toStdString();
        return nullptr;
    }
    const auto probe = codec::probeFile (file.getFullPathName().toStdString());
    if (! probe || probe->sampleRate <= 0.0)
    {
        error = "not a readable audio file: " + file.getFullPathName().toStdString();
        return nullptr;
    }
    if (probe->frames > (int64_t) 60 * 60 * 192000)
    {
        error = "audio file is longer than an hour: " + file.getFullPathName().toStdString();
        return nullptr;
    }
    std::string decodeError;
    const auto decoded = codec::decodeFile (file.getFullPathName().toStdString(), decodeError);
    if (! decoded)
    {
        error = "not a readable audio file: " + file.getFullPathName().toStdString();
        return nullptr;
    }
    const int frames = (int) decoded->frames;
    juce::AudioBuffer<float> raw ((int) std::max<size_t> (1, decoded->channels.size()), std::max (1, frames));
    raw.clear();
    for (size_t c = 0; c < decoded->channels.size(); ++c)
        raw.copyFrom ((int) c, 0, decoded->channels[c].data(), frames);
    if (reversed)
        for (int c = 0; c < raw.getNumChannels(); ++c)
            std::reverse (raw.getWritePointer (c), raw.getWritePointer (c) + frames);

    auto out = std::make_shared<juce::AudioBuffer<float>>();
    const double ratio = decoded->sampleRate / sampleRate;
    const int outFrames = std::abs (ratio - 1.0) < 1.0e-9 ? frames : (int) std::ceil ((double) frames / ratio);
    out->setSize (2, std::max (1, outFrames));
    out->clear();
    for (int ch = 0; ch < 2; ++ch)
    {
        const int source = std::min (ch, raw.getNumChannels() - 1);
        if (std::abs (ratio - 1.0) < 1.0e-9)
            out->copyFrom (ch, 0, raw, source, 0, frames);
        else
            resample (raw.getReadPointer (source), frames, ratio, out->getWritePointer (ch), outFrames);
    }
    std::shared_ptr<const juce::AudioBuffer<float>> result = out;
    std::lock_guard<std::mutex> lock (cacheMutex());
    cache[key] = result;
    return result;
}

namespace
{

// 클립 소리의 음높이 프레임 (60~1500 Hz): 높이를 편집할 때마다 다시 재지 않게 최근 여덟 클립을 둔다
std::vector<PitchFrame> clipPitchFrames (const juce::String& key, double sampleRate, const std::function<std::vector<float>()>& mono)
{
    static std::vector<std::pair<juce::String, std::shared_ptr<const std::vector<PitchFrame>>>> recent;
    {
        std::lock_guard<std::mutex> lock (cacheMutex());
        for (const auto& [k, frames] : recent)
            if (k == key)
                return *frames;
    }
    const auto samples = mono();
    PitchTrackOptions options;
    options.minHz = 60.0;
    options.maxHz = 1500.0;
    auto frames = std::make_shared<const std::vector<PitchFrame>> (trackPitch (samples.data(), (int) samples.size(), sampleRate, 0,
                                                                               (int) samples.size(), options));
    std::lock_guard<std::mutex> lock (cacheMutex());
    recent.insert (recent.begin(), { key, frames });
    if (recent.size() > 8)
        recent.pop_back();
    return *frames;
}

// 늘이고 음높이를 바꾼 클립의 소리: 곡의 클립 시작부터 끝까지 (렌더 샘플레이트, 스테레오). 같은 클립과 템포는 한 번만 만든다.
// 블록마다 곡 위치에 맞는 파일 위치까지 입력을 앞서 넣어 (늘이기의 지연만큼) 늘이는 비율이 템포를 따라 바뀐다. 씨앗이 고정이라 결과가 같다
std::shared_ptr<const juce::AudioBuffer<float>> renderStretched (const AudioClip& spec, const TempoMap& tempo, const juce::File& folder,
                                                                 double sampleRate, std::string& error)
{
#if MUSIC_RENDER_HAS_STRETCH
    static std::map<juce::String, std::weak_ptr<const juce::AudioBuffer<float>>> cache;
    const auto file = clipFile (spec, folder);
    juce::String key = cacheKey (file, sampleRate) + (spec.reverse ? "|reversed" : "") + "|" + juce::String (spec.offsetSeconds, 9) + "|"
                       + juce::String (clipLengthSeconds (spec, folder), 9) + "|" + juce::String (spec.stretch, 9) + "|" + juce::String (spec.pitchSemitones, 9);
    for (const auto& w : spec.warp)
        key << "|w" << juce::String (w.seconds, 9) << "," << juce::String (w.beat, 9);
    if (const auto& pc = spec.pitchCorrection)
    {
        key << "|tune " << juce::String (pc->strength, 9) << "," << juce::String (pc->speedMs, 9) << (pc->formant ? "f" : "") << ",";
        for (const bool on : pc->allowed)
            key << (on ? "1" : "0");
    }
    for (const auto& e : spec.pitchEdits)
        key << "|e" << juce::String (e.seconds, 9) << "," << juce::String (e.lengthSeconds, 9) << "," << juce::String (e.semitones, 9) << ","
            << juce::String (e.flatten, 9) << "," << juce::String (e.vibrato, 9);
    if (spec.sourceBpm || ! spec.warp.empty())
    {
        key << "|bpm " << juce::String (spec.sourceBpm.value_or (0.0), 9) << "@" << juce::String (spec.startBeat, 9);
        for (const auto& p : tempo.points())
            key << ";" << juce::String (p.beat, 9) << "," << juce::String (p.bpm, 9) << (p.ramp ? "r" : "");
    }
    {
        std::lock_guard<std::mutex> lock (cacheMutex());
        if (auto it = cache.find (key); it != cache.end())
            if (auto alive = it->second.lock())
                return alive;
    }
    const auto source = loadClipAudio (file, sampleRate, error, spec.reverse);
    if (source == nullptr)
        return nullptr;
    const double sr = sampleRate;
    const int64_t srcStart = (int64_t) std::llround (spec.offsetSeconds * sr);
    const int64_t srcFrames = std::max<int64_t> (0, std::min<int64_t> ((int64_t) source->getNumSamples() - srcStart,
                                                                     (int64_t) std::llround (clipLengthSeconds (spec, folder) * sr)));
    const double startSec = tempo.secondsAt (spec.startBeat);
    const int64_t first = (int64_t) std::llround (startSec * sr);
    const int64_t frames = std::max<int64_t> (0, (int64_t) std::llround (clipSongSecondsAt (spec, tempo, spec.offsetSeconds + (double) srcFrames / sr) * sr) - first);
    if (srcFrames <= 0 || frames <= 0)
    {
        error = "the clip is empty";
        return nullptr;
    }
    // 파일 쪽: 클립의 소리 뒤에 넉넉한 무음 (늘이기가 앞서 읽는다)
    const int pad = (int) (sr * 4.0);
    juce::AudioBuffer<float> input (2, (int) srcFrames + pad);
    input.clear();
    for (int ch = 0; ch < 2; ++ch)
        input.copyFrom (ch, 0, *source, ch, (int) srcStart, (int) srcFrames);
    auto out = std::make_shared<juce::AudioBuffer<float>> (2, (int) frames);
    out->clear();

    // 음높이 교정과 음마다의 높이 편집: 클립 소리의 모노에서 잰 곡선 (input 의 0 번 샘플이 offsetSeconds)
    std::vector<CorrectionPoint> correction;
    if (spec.pitchCorrection || ! spec.pitchEdits.empty())
    {
        auto mono = [&]
        {
            std::vector<float> m ((size_t) srcFrames);
            for (int64_t i = 0; i < srcFrames; ++i)
                m[(size_t) i] = 0.5f * (input.getSample (0, (int) i) + input.getSample (1, (int) i));
            return m;
        };
        const auto frames = clipPitchFrames (cacheKey (file, sampleRate) + (spec.reverse ? "|reversed|" : "|") + juce::String (srcStart) + "|" + juce::String (srcFrames),
                                             sr, mono);
        if (const auto& pc = spec.pitchCorrection)
            correction = pitchCorrectionCurve (frames, pc->allowed, pc->strength, pc->speedMs, spec.pitchSemitones);
        if (! spec.pitchEdits.empty())
            correction = pitchEditCurve (frames, spec.pitchEdits, spec.offsetSeconds, correction);
    }
    // 포먼트는 교정의 설정을 따르고, 교정 없이 높이를 편집했으면 지킨다
    const bool formant = spec.pitchCorrection ? spec.pitchCorrection->formant : ! spec.pitchEdits.empty();
    signalsmith::stretch::SignalsmithStretch<float> stretch (1);
    stretch.presetDefault (2, (float) sr);
    stretch.setTransposeSemitones ((float) spec.pitchSemitones);
    if (formant)
        stretch.setFormantFactor (1.0f, true);   // 음높이를 바꿔도 포먼트는 그대로
    auto srcAt = [&] (int64_t j) { return (clipSourceAt (spec, tempo, (double) (first + j) / sr) - spec.offsetSeconds) * sr; };
    auto lead = [&] (double rate) { return (double) stretch.inputLatency() + rate * stretch.outputLatency(); };
    constexpr int block = 256;
    const double rate0 = (srcAt (std::min<int64_t> (block, frames)) - srcAt (0)) / (double) std::min<int64_t> (block, frames);
    const int seek = std::min ((int) std::llround (lead (rate0)), input.getNumSamples());
    {
        const float* in[2] = { input.getReadPointer (0), input.getReadPointer (1) };
        stretch.outputSeek (in, seek);
    }
    int64_t fed = seek;
    for (int64_t j = 0; j < frames; j += block)
    {
        const int n = (int) std::min<int64_t> (block, frames - j);
        // 이번에 바꾼 높이는 출력 지연만큼 뒤의 소리에 들린다: 그 자리의 교정을 쓴다
        if (! correction.empty())
        {
            const double at = srcAt (std::min (frames, j + n / 2 + stretch.outputLatency())) / sr;
            stretch.setTransposeSemitones ((float) (spec.pitchSemitones + correctionAt (correction, at)));
            if (formant)
            {
                // 포먼트를 재는 기본 주파수는 잰 음높이로 준다 (스스로 재게 두면 높이가 원래 쪽으로 돌아간다)
                const double midi = measuredPitchAt (correction, at);
                stretch.setFormantBase (midi > 0.0 ? (float) (440.0 * std::pow (2.0, (midi - 69.0) / 12.0) / sr) : 0.0f);
            }
        }
        const double rate = std::max (1.0e-3, (srcAt (j + n) - srcAt (j)) / n);
        const int64_t target = std::min<int64_t> (input.getNumSamples(), (int64_t) std::llround (srcAt (j + n) + lead (rate)));
        const int count = (int) std::max<int64_t> (0, target - fed);
        const float* in[2] = { input.getReadPointer (0) + fed, input.getReadPointer (1) + fed };
        float* o[2] = { out->getWritePointer (0) + j, out->getWritePointer (1) + j };
        stretch.process (in, count, o, n);
        fed += count;
    }
    std::shared_ptr<const juce::AudioBuffer<float>> result = out;
    std::lock_guard<std::mutex> lock (cacheMutex());
    cache[key] = result;
    return result;
#else
    juce::ignoreUnused (spec, tempo, folder, sampleRate);
    error = "this build has no time stretching (Signalsmith Stretch); the clip plays unstretched";
    return nullptr;
#endif
}

// 마커마다 자른 클립의 소리: 마커 사이 조각을 곡의 그 자리에서 원래 빠르기로 튼다. 조각이 자리보다 길면 다음 자리에서 자르고,
// 짧으면 다음 조각 앞에서 멈춘다 (틈은 무음). 자르는 자리는 마커의 10 ms 앞이다 (마커보다 먼저 시작한 어택의 머리가 제 조각에 남는다).
// 조각의 끝은 3 ms, 머리는 1 ms 로 키우고 줄인다
std::shared_ptr<const juce::AudioBuffer<float>> renderSliced (const AudioClip& spec, const TempoMap& tempo, const juce::File& folder,
                                                               double sampleRate, std::string& error)
{
    static std::map<juce::String, std::weak_ptr<const juce::AudioBuffer<float>>> cache;
    const auto file = clipFile (spec, folder);
    const double length = clipLengthSeconds (spec, folder);
    juce::String key = cacheKey (file, sampleRate) + (spec.reverse ? "|reversed" : "") + "|slice|" + juce::String (spec.offsetSeconds, 9) + "|" + juce::String (length, 9) + "|"
                       + juce::String (spec.sourceBpm.value_or (0.0), 9) + "@" + juce::String (spec.startBeat, 9);
    for (const auto& w : spec.warp)
        key << "|w" << juce::String (w.seconds, 9) << "," << juce::String (w.beat, 9);
    for (const auto& p : tempo.points())
        key << ";" << juce::String (p.beat, 9) << "," << juce::String (p.bpm, 9) << (p.ramp ? "r" : "");
    {
        std::lock_guard<std::mutex> lock (cacheMutex());
        if (auto it = cache.find (key); it != cache.end())
            if (auto alive = it->second.lock())
                return alive;
    }
    const auto source = loadClipAudio (file, sampleRate, error, spec.reverse);
    if (source == nullptr)
        return nullptr;
    const double sr = sampleRate;
    const double s0 = spec.offsetSeconds, s1 = std::min (s0 + length, (double) source->getNumSamples() / sr);
    std::vector<double> cuts { s0 };
    for (const auto& w : spec.warp)
        if (w.seconds > s0 + 1.0e-6 && w.seconds < s1 - 1.0e-6)
            cuts.push_back (w.seconds);
    cuts.push_back (s1);
    const int64_t first = (int64_t) std::llround (tempo.secondsAt (spec.startBeat) * sr);
    auto songSample = [&] (double s) { return (int64_t) std::llround (clipSongSecondsAt (spec, tempo, s) * sr) - first; };
    const int64_t frames = songSample (s1);
    if (s1 <= s0 || frames <= 0)
    {
        error = "the clip is empty";
        return nullptr;
    }
    auto out = std::make_shared<juce::AudioBuffer<float>> (2, (int) frames);
    out->clear();
    const int fadeIn = std::max (1, (int) std::lround (0.001 * sr)), fadeOut = std::max (1, (int) std::lround (0.003 * sr));
    const int64_t pre = (int64_t) std::lround (0.01 * sr);
    const int64_t clipStart = (int64_t) std::llround (s0 * sr);
    // 조각 k 가 시작하는 파일의 샘플과 곡의 샘플 (클립 머리는 앞당기지 않는다)
    auto fileStart = [&] (size_t k) { return k == 0 ? clipStart : std::max (clipStart, (int64_t) std::llround (cuts[k] * sr) - pre); };
    auto songStart = [&] (size_t k) { return k == 0 ? (int64_t) 0 : songSample (cuts[k]) - ((int64_t) std::llround (cuts[k] * sr) - fileStart (k)); };
    for (size_t k = 0; k + 1 < cuts.size(); ++k)
    {
        const bool last = k + 2 == cuts.size();
        const int64_t at = std::max<int64_t> (0, songStart (k));
        const int64_t room = (last ? frames : std::min (frames, songStart (k + 1))) - at;
        const int64_t from = fileStart (k);
        const int64_t available = (last ? (int64_t) std::llround (s1 * sr) : fileStart (k + 1)) - from;
        const int64_t n = std::min (room, available);
        if (n <= 0)
            continue;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* in = source->getReadPointer (ch) + from;
            float* o = out->getWritePointer (ch) + at;
            for (int64_t i = 0; i < n; ++i)
            {
                float g = 1.0f;
                if (k > 0 && i < fadeIn)
                    g *= fadeShape ((double) i / fadeIn);
                if (! last && i >= n - fadeOut)
                    g *= fadeShape ((double) (n - i) / fadeOut);
                o[i] = in[i] * g;
            }
        }
    }
    std::shared_ptr<const juce::AudioBuffer<float>> result = out;
    std::lock_guard<std::mutex> lock (cacheMutex());
    cache[key] = result;
    return result;
}

} // namespace

std::shared_ptr<const ClipPeaks> clipPeaks (const juce::File& file, bool reversed)
{
    static std::map<juce::String, std::shared_ptr<const ClipPeaks>> cache;
    const auto key = cacheKey (file, 0.0) + (reversed ? "|reversed" : "");
    {
        std::lock_guard<std::mutex> lock (cacheMutex());
        if (auto it = cache.find (key); it != cache.end())
            return it->second;
    }
    std::string error;
    const auto decoded = codec::decodeFile (file.getFullPathName().toStdString(), error);
    if (! decoded)
        return nullptr;
    auto peaks = std::make_shared<ClipPeaks>();
    peaks->sampleRate = decoded->sampleRate;
    const int64_t frames = decoded->frames;
    const int bucket = peaks->bucketFrames;
    const int chunk = bucket * 512;
    juce::AudioBuffer<float> buffer ((int) std::max<size_t> (1, decoded->channels.size()), chunk);
    for (int64_t at = 0; at < frames; at += chunk)
    {
        const int n = (int) std::min<int64_t> (chunk, frames - at);
        buffer.clear();
        for (size_t c = 0; c < decoded->channels.size(); ++c)
        {
            if (reversed)
            {
                // 거꾸로 돌린 파일의 [at, at + n) 은 원래 파일의 [frames - at - n, frames - at) 를 뒤집은 것
                const float* src = decoded->channels[c].data() + (frames - at - n);
                float* dst = buffer.getWritePointer ((int) c);
                for (int i = 0; i < n; ++i)
                    dst[i] = src[n - 1 - i];
            }
            else
                buffer.copyFrom ((int) c, 0, decoded->channels[c].data() + at, n);
        }
        for (int b = 0; b < n; b += bucket)
        {
            float lo = 0.0f, hi = 0.0f;
            const int m = std::min (bucket, n - b);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                const auto range = juce::FloatVectorOperations::findMinAndMax (buffer.getReadPointer (ch, b), m);
                lo = std::min (lo, range.getStart());
                hi = std::max (hi, range.getEnd());
            }
            peaks->minimum.push_back (lo);
            peaks->maximum.push_back (hi);
        }
    }
    std::lock_guard<std::mutex> lock (cacheMutex());
    cache[key] = peaks;
    return peaks;
}

juce::File reversedCopyOf (const juce::File& file)
{
    // 파일마다 (경로, 수정 시각, 크기) 한 폴더: 이름은 "<원래 이름> reversed.wav" (샘플러가 곡 폴더에 복사해도 알아보게)
    const auto stamp = juce::String::toHexString (file.getFullPathName().hashCode64()) + "-"
                       + juce::String::toHexString (file.getLastModificationTime().toMilliseconds()) + "-" + juce::String::toHexString (file.getSize());
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("InitialDAW-reversed").getChildFile (stamp)
        .getChildFile (file.getFileNameWithoutExtension() + " reversed.wav");
}

juce::File clipSourceFile (const AudioClip& clip, const juce::File& songFolder)
{
    const auto file = clipFile (clip, songFolder);
    if (! clip.reverse || ! file.existsAsFile())
        return file;
    const auto out = reversedCopyOf (file);
    const auto folder = out.getParentDirectory();
    static std::mutex writing;
    std::lock_guard<std::mutex> lock (writing);
    if (out.existsAsFile())
        return out;
    std::string error;
    const auto decoded = codec::decodeFile (file.getFullPathName().toStdString(), error);
    if (! decoded || decoded->channels.empty() || decoded->frames <= 0 || decoded->frames > (int64_t) std::numeric_limits<int>::max())
        return file;
    const int frames = (int) decoded->frames;
    juce::AudioBuffer<float> buffer ((int) decoded->channels.size(), frames);
    for (size_t c = 0; c < decoded->channels.size(); ++c)
    {
        const float* src = decoded->channels[c].data();
        float* dst = buffer.getWritePointer ((int) c);
        for (int i = 0; i < frames; ++i)
            dst[i] = src[frames - 1 - i];
    }
    if (! folder.createDirectory())
        return file;
    const auto partial = folder.getChildFile (out.getFileName() + ".part");
    try
    {
        writeWav (partial, buffer, frames, decoded->sampleRate, 32);
    }
    catch (...)
    {
        partial.deleteFile();
        return file;
    }
    return partial.moveFileTo (out) ? out : file;
}

AutomationLane mirroredLane (const AutomationLane& lane, double length)
{
    AutomationLane out;
    const auto& p = lane.points;
    const size_t n = p.size();
    for (size_t j = 0; j < n; ++j)
    {
        const auto& from = p[n - 1 - j];
        AutomationPoint q { length - from.beat, from.value, CurveShape::linear };
        if (j + 1 < n)
        {
            // 거꾸로 지나는 칸 [q, 다음 점] 은 원래의 칸 [p[n-2-j], p[n-1-j]] 이다. 모양은 그 칸의 것 (사인, 지수, 곧은 선은 거울에도 같다)
            const auto& segment = p[n - 2 - j];
            q.curve = segment.curve;
            if (segment.curve == CurveShape::step)
            {
                // 뛰기는 칸의 끝 값을 붙든다: 같은 자리에 그 값의 점을 하나 더
                out.points.push_back ({ q.beat, q.value, CurveShape::step });
                q.value = segment.value;
            }
        }
        out.points.push_back (q);
    }
    return out;
}

AudioClip withClipReversed (const AudioClip& clip, double fileSeconds)
{
    auto tidy = [] (double v) { return std::round (v * 1.0e6) / 1.0e6; };
    const double L = std::max (fileSeconds, clip.offsetSeconds);
    auto mirror = [L] (double s) { return L - s; };
    auto out = clip;
    out.reverse = ! clip.reverse;
    const double length = std::max (0.0, std::min (clip.lengthSeconds.value_or (L - clip.offsetSeconds), L - clip.offsetSeconds));
    out.offsetSeconds = tidy (std::max (0.0, mirror (clip.offsetSeconds + length)));
    if (clip.lengthSeconds || clip.offsetSeconds > 0.0)
        out.lengthSeconds = tidy (length);
    else
        out.lengthSeconds.reset();   // 파일 전체
    // 워프 마커: 파일의 초는 거울 자리, 박은 클립 안에서 거울 자리 (박의 차이만 쓰이니 같은 박 범위에 둔다)
    out.warp.clear();
    if (! clip.warp.empty())
    {
        const double sum = clip.warp.front().beat + clip.warp.back().beat;
        for (auto it = clip.warp.rbegin(); it != clip.warp.rend(); ++it)
            out.warp.push_back ({ tidy (mirror (it->seconds)), tidy (sum - it->beat) });
    }
    out.pitchEdits.clear();
    for (auto it = clip.pitchEdits.rbegin(); it != clip.pitchEdits.rend(); ++it)
    {
        auto e = *it;
        e.seconds = tidy (std::max (0.0, mirror (it->endSeconds())));
        out.pitchEdits.push_back (e);
    }
    out.gainCurve = mirroredLane (clip.gainCurve, L);
    for (auto& p : out.gainCurve.points)
        p.beat = tidy (std::max (0.0, p.beat));
    return out;
}

std::optional<std::pair<AudioClip, AudioClip>> crossfadedClips (const AudioClip& a, const AudioClip& b, const TempoMap& tempo, const juce::File& songFolder,
                                                                double lengthSeconds)
{
    auto tidy = [] (double v) { return std::round (v * 1.0e6) / 1.0e6; };
    const double aStart = tempo.secondsAt (a.startBeat), aEnd = clipEndSeconds (a, tempo, songFolder);
    const double bStart = tempo.secondsAt (b.startBeat), bEnd = clipEndSeconds (b, tempo, songFolder);
    if (bStart < aStart || bEnd <= aEnd)
        return std::nullopt;
    auto x = a, y = b;
    double from = bStart, to = aEnd;   // 겹친 곡의 초
    if (aEnd <= bStart + 1.0e-3)
    {
        if (bStart - aEnd > 1.0e-3 || lengthSeconds <= 0.0)
            return std::nullopt;
        // 맞붙은 두 클립: a 는 파일의 뒤쪽 소리로, b 는 앞쪽 소리로 늘인다 (이음매의 앞뒤 반씩, 한쪽이 모자라면 다른 쪽에서)
        double fileA = 0.0, fileB = 0.0;
        if (const auto info = audioFileInfo (clipFile (a, songFolder)))
            fileA = info->seconds();
        if (const auto info = audioFileInfo (clipFile (b, songFolder)))
            fileB = info->seconds();
        const double availA = std::max (0.0, clipSongSecondsAt (a, tempo, fileA) - aEnd);
        const double availB = std::max (0.0, bStart - std::max (0.0, clipSongSecondsAt (b, tempo, 0.0)));
        double hb = std::min (availB, 0.5 * lengthSeconds);
        const double ha = std::min (availA, lengthSeconds - hb);
        hb = std::min (availB, lengthSeconds - ha);
        if (ha + hb < 1.0e-3)
            return std::nullopt;
        x.lengthSeconds = tidy (clipSourceAt (a, tempo, aEnd + ha) - a.offsetSeconds);
        const double offset = std::max (0.0, clipSourceAt (b, tempo, bStart - hb));
        const double bLength = clipLengthSeconds (b, songFolder);
        y.lengthSeconds = tidy (b.offsetSeconds + bLength - offset);
        y.offsetSeconds = tidy (offset);
        y.startBeat = tidy (tempo.beatAt (bStart - hb));
        from = tempo.secondsAt (y.startBeat);
        to = clipEndSeconds (x, tempo, songFolder);
    }
    if (to - from < 1.0e-3)
        return std::nullopt;
    // 같은 파일의 같은 자리가 겹치면 (나눈 클립) 두 소리가 같아 크기를 더하고, 아니면 힘을 더한다
    bool same = a.file == b.file && a.reverse == b.reverse;
    for (const double t : { from, 0.5 * (from + to), to })
        same = same && std::abs (clipSourceAt (x, tempo, t) - clipSourceAt (y, tempo, t)) < 1.0e-4;
    const auto shape = same ? FadeShape::sine : FadeShape::equalPower;
    x.fadeOutSeconds = tidy (clipEndSeconds (x, tempo, songFolder) - from);
    x.fadeOutShape = shape;
    y.fadeInSeconds = tidy (to - tempo.secondsAt (y.startBeat));
    y.fadeInShape = shape;
    return std::make_pair (x, y);
}

double nearestZeroCrossing (const AudioClip& clip, const juce::File& songFolder, double sourceSeconds, double searchSeconds)
{
    const auto file = clipFile (clip, songFolder);
    const auto info = audioFileInfo (file);
    if (! info || searchSeconds <= 0.0)
        return sourceSeconds;
    std::string error;
    const auto data = loadClipAudio (file, info->sampleRate, error, clip.reverse);
    if (data == nullptr)
        return sourceSeconds;
    const double sr = info->sampleRate;
    const int64_t n = data->getNumSamples();
    const int64_t centre = (int64_t) std::llround (sourceSeconds * sr), reach = (int64_t) std::llround (searchSeconds * sr);
    const float* l = data->getReadPointer (0);
    const float* r = data->getReadPointer (1);
    int64_t best = -1;
    for (int64_t i = std::max<int64_t> (1, centre - reach); i <= std::min (n - 1, centre + reach); ++i)
    {
        const float p = l[i - 1] + r[i - 1], q = l[i] + r[i];
        if (! ((p < 0.0f && q >= 0.0f) || (p > 0.0f && q <= 0.0f)))
            continue;
        const int64_t at = std::abs (p) < std::abs (q) ? i - 1 : i;
        if (best < 0 || std::abs (at - centre) < std::abs (best - centre))
            best = at;
    }
    return best >= 0 ? (double) best / sr : sourceSeconds;
}

AudioClipPlayer::AudioClipPlayer (const std::vector<AudioClip>& clips, const TempoMap& tempo, const juce::File& songFolder, double sampleRate,
                                  std::vector<std::string>& warnings)
    : songFolder_ (songFolder), sampleRate_ (sampleRate)
{
    current_ = makeList (clips, tempo, warnings).release();
}

AudioClipPlayer::~AudioClipPlayer()
{
    stopWorker();
    delete current_;
}

void AudioClipPlayer::prepareClipsInBackground (bool on)
{
    if (on == worker_.joinable())
        return;
    if (! on)
    {
        stopWorker();
        return;
    }
    stopping_ = false;
    worker_ = std::thread ([this] { prepareLoop(); });
}

void AudioClipPlayer::stopWorker()
{
    {
        std::lock_guard<std::mutex> lock (requestMutex_);
        stopping_ = true;
        request_.reset();
    }
    requestReady_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

bool AudioClipPlayer::clipsPending() const
{
    std::lock_guard<std::mutex> lock (requestMutex_);
    return request_ != nullptr || working_;
}

void AudioClipPlayer::prepareLoop()
{
    for (;;)
    {
        std::unique_ptr<Request> request;
        {
            std::unique_lock<std::mutex> lock (requestMutex_);
            requestReady_.wait (lock, [this] { return stopping_ || request_ != nullptr; });
            if (stopping_)
                return;
            request = std::move (request_);
            working_ = true;
        }
        std::vector<std::string> warnings;
        auto list = makeList (request->clips, request->tempo, warnings);
        std::lock_guard<std::mutex> lock (requestMutex_);
        working_ = false;
        if (stopping_)
            return;
        if (request_ != nullptr)
            continue;   // 준비하는 동안 더 새 목록이 왔다
        handoff_.collect();
        handoff_.post (std::move (list));
    }
}

std::unique_ptr<AudioClipPlayer::ClipList> AudioClipPlayer::makeList (const std::vector<AudioClip>& clips, const TempoMap& tempo,
                                                                      std::vector<std::string>& warnings) const
{
    auto list = std::make_unique<ClipList>();
    for (const auto& spec : clips)
    {
        if (spec.muted)
            continue;   // 고르지 않은 테이크
        std::string error;
        Clip c;
        c.start = (int64_t) std::llround (tempo.secondsAt (spec.startBeat) * sampleRate_);
        bool rendered = false;   // data 가 곡의 클립 시작부터의 소리다 (늘이고 음높이를 바꾼 소리)
        if (spec.stretched())
        {
            // 늘이고 음높이를 바꾼 소리는 클립의 처음부터 끝까지 미리 만들어 둔다
            c.data = spec.warpSlices && spec.warp.size() >= 2 ? renderSliced (spec, tempo, songFolder_, sampleRate_, error)
                                                              : renderStretched (spec, tempo, songFolder_, sampleRate_, error);
            if (c.data != nullptr)
            {
                c.offset = 0;
                c.length = c.data->getNumSamples();
                rendered = true;
            }
            else
                warnings.push_back (spec.path + ": " + error);
        }
        if (c.data == nullptr)
        {
            c.data = loadClipAudio (clipFile (spec, songFolder_), sampleRate_, error, spec.reverse);
            if (c.data == nullptr)
            {
                warnings.push_back (spec.path + ": " + error);
                continue;
            }
            c.offset = (int64_t) std::llround (spec.offsetSeconds * sampleRate_);
            const int64_t available = std::max<int64_t> (0, (int64_t) c.data->getNumSamples() - c.offset);
            c.length = spec.lengthSeconds ? std::min (available, (int64_t) std::llround (*spec.lengthSeconds * sampleRate_)) : available;
        }
        if (c.length <= 0)
            continue;
        c.gain = (float) juce::Decibels::decibelsToGain (spec.gainDb, -96.0);
        const int64_t half = c.length / 2;
        c.fadeIn = (int) std::min<int64_t> (half, std::max<int64_t> (minimumFade, std::llround (spec.fadeInSeconds * sampleRate_)));
        c.fadeOut = (int) std::min<int64_t> (half, std::max<int64_t> (minimumFade, std::llround (spec.fadeOutSeconds * sampleRate_)));
        c.fadeInShape = spec.fadeInShape;
        c.fadeOutShape = spec.fadeOutShape;
        if (! spec.gainCurve.empty())
        {
            // 게인 곡선: 칸마다 그 자리 소리의 파일의 초에서 읽는다 (늘인 클립은 곡의 초에서 파일의 초로)
            c.envelope.resize ((size_t) (c.length / envelopeStep + 2));
            for (size_t g = 0; g < c.envelope.size(); ++g)
            {
                const int64_t k = (int64_t) g * envelopeStep;
                const double source = rendered ? clipSourceAt (spec, tempo, (double) (c.start + k) / sampleRate_)
                                                    : (double) (c.offset + k) / sampleRate_;
                c.envelope[g] = (float) juce::Decibels::decibelsToGain (spec.gainCurve.valueAt (source), -96.0);
            }
        }
        list->clips.push_back (std::move (c));
    }
    std::sort (list->clips.begin(), list->clips.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    return list;
}

bool AudioClipPlayer::setClips (const std::vector<AudioClip>& clips, const TempoMap& tempo, std::vector<std::string>& warnings)
{
    if (worker_.joinable())
    {
        {
            std::lock_guard<std::mutex> lock (requestMutex_);
            request_ = std::make_unique<Request> (Request { clips, tempo });
        }
        requestReady_.notify_one();
        return true;
    }
    handoff_.collect();
    handoff_.post (makeList (clips, tempo, warnings));
    return true;
}

void AudioClipPlayer::prepare (double sampleRate, int)
{
    jassert (std::abs (sampleRate - sampleRate_) < 1.0e-6);
    juce::ignoreUnused (sampleRate);
}

void AudioClipPlayer::render (juce::AudioBuffer<float>& buffer, int numSamples, const juce::MidiBuffer&, int64_t blockStart)
{
    if (auto* fresh = handoff_.take())
    {
        handoff_.retire (current_);
        current_ = fresh;
    }
    buffer.clear (0, numSamples);
    if (! playing_.load (std::memory_order_relaxed))
    {
        lastEnd_ = -1;
        return;
    }
    const int64_t end = blockStart + numSamples;
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);
    for (const auto& c : current_->clips)
    {
        if (c.start >= end)
            break;
        const int64_t s0 = std::max (blockStart, c.start), s1 = std::min (end, c.start + c.length);
        if (s1 <= s0)
            continue;
        const float* inL = c.data->getReadPointer (0);
        const float* inR = c.data->getReadPointer (1);
        const bool envelope = ! c.envelope.empty();
        for (int64_t s = s0; s < s1; ++s)
        {
            const int64_t k = s - c.start;
            float g = c.gain;
            if (envelope)
            {
                const auto e = (size_t) (k / envelopeStep);
                const float frac = (float) (k % envelopeStep) / (float) envelopeStep;
                g *= c.envelope[e] + (c.envelope[e + 1] - c.envelope[e]) * frac;
            }
            if (k < c.fadeIn)
                g *= fadeGain (c.fadeInShape, (double) k / c.fadeIn);
            if (k >= c.length - c.fadeOut)
                g *= fadeGain (c.fadeOutShape, (double) (c.length - k) / c.fadeOut);
            const int64_t src = c.offset + k;
            const int i = (int) (s - blockStart);
            left[i] += inL[src] * g;
            right[i] += inR[src] * g;
        }
    }
    // 처음 블록과 탐색이나 반복으로 위치가 끊긴 블록은 짧게 키운다
    if (blockStart != lastEnd_)
    {
        const int n = std::min (jumpFade, numSamples);
        for (int i = 0; i < n; ++i)
        {
            const float g = fadeShape ((double) i / jumpFade);
            left[i] *= g;
            right[i] *= g;
        }
    }
    lastEnd_ = end;
}

} // namespace mr
