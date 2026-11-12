#include "instruments/QuickSampler.h"

#include "analysis/PitchTrack.h"

#include "core/Diagnostics.h"
#include "io/AudioFile.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mr
{

namespace
{
// SFZ 가 가리킬 소리의 경로: SFZ 의 폴더 안이거나, 둘 다 within(곡 폴더) 안이면 상대 경로 (폴더째 옮겨진다), 아니면 절대 경로
juce::String samplePath (const juce::File& sfzFile, const juce::File& sample, const juce::File& within)
{
    const auto folder = sfzFile.getParentDirectory();
    const bool together = within != juce::File() && sample.isAChildOf (within) && sfzFile.isAChildOf (within);
    return sample.isAChildOf (folder) || together ? sample.getRelativePathFrom (folder).replaceCharacter ('\\', '/') : sample.getFullPathName();
}

// sfizz 는 전체 크기(-7.35 dB)와 팬 법칙으로 샘플을 11.51 dB 작게 낸다 (모노와 스테레오 모두). 세기 127 이 녹음 크기 그대로 울리게 되돌린다
const char* const kUnityVolume = "11.51";
} // namespace

std::optional<PitchEstimate> estimatePitch (const juce::File& audio, double startSeconds, std::optional<double> lengthSeconds)
{
    AudioData data;
    try
    {
        data = readAudioFile (audio);
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
    const double sr = data.sampleRate;
    const auto& b = data.buffer;
    if (sr <= 0.0 || b.getNumSamples() == 0)
        return std::nullopt;
    std::vector<float> mono ((size_t) b.getNumSamples(), 0.0f);
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            mono[(size_t) i] += b.getSample (ch, i) / (float) b.getNumChannels();
    // 어택 뒤 3초까지
    const int first = (int) std::lround ((startSeconds + 0.05) * sr);
    int last = std::min (b.getNumSamples(), first + (int) std::lround (3.0 * sr));
    if (lengthSeconds)
        last = std::min (last, (int) std::lround ((startSeconds + *lengthSeconds) * sr));
    PitchTrackOptions options;
    options.minHz = 40.0;
    options.hopSeconds = 0.02;
    std::vector<double> estimates;
    double clarity = 0.0;
    for (const auto& f : trackPitch (mono.data(), (int) mono.size(), sr, first, last, options))
        if (f.midi > 0.0 && f.clarity >= 0.85)
        {
            estimates.push_back (f.midi);
            clarity += f.clarity;
        }
    if (estimates.empty())
        return std::nullopt;
    const auto mid = estimates.begin() + (long) estimates.size() / 2;
    std::nth_element (estimates.begin(), mid, estimates.end());
    const double midi = *mid;
    return PitchEstimate { 440.0 * std::pow (2.0, (midi - 69.0) / 12.0), midi, clarity / (double) estimates.size() };
}

SamplerResult writeSamplerSfz (const juce::File& sfzFile, const juce::File& sample, const SamplerOptions& o)
{
    if (! sample.existsAsFile())
        throw Failure ("io", "sample not found: " + sample.getFullPathName().toStdString());
    SamplerResult result;
    if (o.rootKey)
        result.rootKey = std::clamp (*o.rootKey, 0, 127);
    else if (! o.oneShot)
    {
        result.pitch = estimatePitch (sample, o.startSeconds, o.lengthSeconds);
        if (result.pitch)
        {
            result.rootKey = std::clamp ((int) std::lround (result.pitch->midi), 0, 127);
            result.tuneCents = std::round ((result.rootKey - result.pitch->midi) * 1000.0) / 10.0;   // 잰 높이를 건반에 맞춘다
            if (std::abs (result.tuneCents) < 0.05)
                result.tuneCents = 0.0;
        }
    }
    const auto info = readAudioFile (sample);   // 샘플레이트 (구간을 샘플로)
    juce::String text;
    text << "// InitialDAW quick sampler: " << sample.getFileName() << "\n";
    text << "<global>\namp_veltrack=100\nvolume=" << kUnityVolume << "\nampeg_release=" << juce::String (o.releaseSeconds, 3) << "\n";
    text << "<region>\nsample=" << samplePath (sfzFile, sample, o.relativeWithin) << "\nlokey=0 hikey=127\n";
    if (o.oneShot)
        text << "pitch_keycenter=" << result.rootKey << "\npitch_keytrack=0\nloop_mode=one_shot\n";
    else
        text << "pitch_keycenter=" << result.rootKey << "\n";
    if (result.tuneCents != 0.0)
        text << "tune=" << juce::String (result.tuneCents, 1) << "\n";
    if (o.startSeconds > 0.0)
        text << "offset=" << (juce::int64) std::llround (o.startSeconds * info.sampleRate) << "\n";
    if (o.lengthSeconds)
        text << "end=" << (juce::int64) std::llround ((o.startSeconds + *o.lengthSeconds) * info.sampleRate) - 1 << "\n";
    if (! sfzFile.getParentDirectory().createDirectory() || ! sfzFile.replaceWithText (text))
        throw Failure ("io", "cannot write " + sfzFile.getFullPathName().toStdString());
    return result;
}

void writeSliceSfz (const juce::File& sfzFile, const juce::File& sample, const std::vector<double>& starts, double endSeconds, int firstKey,
                    const juce::File& relativeWithin)
{
    if (! sample.existsAsFile())
        throw Failure ("io", "sample not found: " + sample.getFullPathName().toStdString());
    const auto info = readAudioFile (sample);
    const auto folder = sfzFile.getParentDirectory();
    const auto path = samplePath (sfzFile, sample, relativeWithin);
    juce::String text;
    text << "// InitialDAW slices: " << sample.getFileName() << "\n";
    text << "<global>\nsample=" << path << "\namp_veltrack=100\nvolume=" << kUnityVolume << "\nampeg_release=0.005\n";
    for (size_t i = 0; i < starts.size() && firstKey + (int) i <= 127; ++i)
    {
        const double end = i + 1 < starts.size() ? starts[i + 1] : endSeconds;
        const auto key = firstKey + (int) i;
        text << "<region> key=" << key << " offset=" << (juce::int64) std::llround (starts[i] * info.sampleRate)
             << " end=" << std::max ((juce::int64) std::llround (starts[i] * info.sampleRate), (juce::int64) std::llround (end * info.sampleRate) - 1) << "\n";
    }
    if (! folder.createDirectory() || ! sfzFile.replaceWithText (text))
        throw Failure ("io", "cannot write " + sfzFile.getFullPathName().toStdString());
}

} // namespace mr
