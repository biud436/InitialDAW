#include "rendering/Bounce.h"

#include "core/Diagnostics.h"
#include "io/AudioFile.h"
#include "timing/TempoMap.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace mr
{

namespace
{

std::vector<std::string> sidechainSources (const TrackSpec& t)
{
    std::vector<std::string> out;
    for (const auto& e : t.effects)
        if (e.type == "compressor")
            if (const auto v = e.params.getProperty ("sidechain", juce::var()); v.isString())
                out.push_back (v.toString().toStdString());
    return out;
}

} // namespace

Composition bounceComposition (const Composition& composition, size_t track)
{
    if (track >= composition.tracks.size())
        throw Failure ("invalid-input", "no track " + std::to_string (track));
    Composition c = composition;
    // 그 트랙과 키 트랙들 (키의 키도)
    std::set<std::string> keep { c.tracks[track].name };
    std::vector<std::string> todo { c.tracks[track].name };
    while (! todo.empty())
    {
        const auto name = todo.back();
        todo.pop_back();
        for (const auto& t : c.tracks)
            if (t.name == name)
                for (const auto& source : sidechainSources (t))
                    if (keep.insert (source).second)
                        todo.push_back (source);
    }
    for (auto& t : c.tracks)
    {
        t.mixer.solo = false;
        t.mixer.mute = keep.count (t.name) == 0;
    }
    for (auto& b : c.buses)
        b.effects.clear();
    c.master.effects.clear();
    c.master.targetLufs.reset();
    c.render.fadeOutSeconds = 0.0;
    return c;
}

BounceRender::BounceRender (const Composition& composition, size_t track, const BounceOptions& options)
    : composition_ (bounceComposition (composition, track)), trackName_ (composition.tracks[track].name), options_ (options)
{
    stemsDir_ = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("music-render-bounce-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    RenderOptions ro;
    ro.sampleRate = options.sampleRate;
    ro.bitDepth = 32;
    ro.stemsDir = stemsDir_;
    ro.stemTracks = { trackName_ };
    ro.stemsPreFader = ! options.postFader;
    ro.cancel = options.cancel;
    ro.progress = options.progress;
    ro.pluginCache = options.pluginCache;
    ro.useDefaultRegistry = options.useDefaultRegistry;
    render_ = std::make_unique<OfflineRender> (composition_, ro);
}

BounceRender::~BounceRender()
{
    render_.reset();
    stemsDir_.deleteRecursively();
}

BounceResult BounceRender::run (const juce::File& output)
{
    if (output.exists())
        throw Failure ("io", "file exists: " + output.getFullPathName().toStdString());
    BounceResult result;
    const auto report = render_->run();
    result.warnings = report.warnings;
    std::string stem;
    for (const auto& t : report.tracks)
        if (t.name == trackName_)
            stem = t.stem;
    if (stem.empty())
        throw Failure ("render", "the track was not rendered");
    auto data = readAudioFile (juce::File (juce::String::fromUTF8 (stem.c_str())));
    const auto& buffer = data.buffer;
    const int64_t total = buffer.getNumSamples();
    const double sr = options_.sampleRate;

    // 소리가 있는 처음과 끝 (-120 dBFS 위)
    constexpr float threshold = 1.0e-6f;
    int64_t first = -1, last = -1;
    float peak = 0.0f;
    for (int64_t i = 0; i < total; ++i)
    {
        float m = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            m = std::max (m, std::abs (buffer.getSample (ch, (int) i)));
        peak = std::max (peak, m);
        if (m > threshold)
        {
            if (first < 0)
                first = i;
            last = i;
        }
    }
    if (first < 0)
    {
        result.silent = true;
        return result;
    }
    // 앞은 그 소리가 든 마디의 첫 박에서. 클립 재생기와 같은 식으로 샘플을 정한다
    const TempoMap tempo (composition_.tempo);
    const auto meters = composition_.meters();
    double startBeat = meters.floorToBar (std::max (0.0, tempo.beatAt ((double) first / sr)) + 1.0e-9);
    int64_t start = (int64_t) std::llround (tempo.secondsAt (startBeat) * sr);
    while (start > first && startBeat > 0.0)   // 반올림으로 첫 소리를 넘으면 한 마디 앞으로
    {
        startBeat = meters.barStart (std::max (0, meters.barAt (startBeat) - 1));
        start = (int64_t) std::llround (tempo.secondsAt (startBeat) * sr);
    }
    start = std::min (start, first);
    const int64_t end = std::min (total, last + 1 + (int64_t) std::llround (0.1 * sr));

    float* const* channels = data.buffer.getArrayOfWritePointers();
    const juce::AudioBuffer<float> part (channels, data.buffer.getNumChannels(), (int) start, (int) (end - start));
    if (! output.getParentDirectory().createDirectory())
        throw Failure ("io", "cannot create directory " + output.getParentDirectory().getFullPathName().toStdString());
    writeWav (output, part, part.getNumSamples(), sr, options_.bitDepth);
    result.file = output;
    result.startBeat = startBeat;
    result.lengthSeconds = (double) (end - start) / sr;
    result.peakDb = 20.0 * std::log10 (std::max (peak, 1.0e-12f));
    return result;
}

} // namespace mr
