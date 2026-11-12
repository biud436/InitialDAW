#include "Exporter.h"

#include "core/Diagnostics.h"
#include "midi/MidiExport.h"

#include <juce_events/juce_events.h>

namespace daw
{

const char* extensionFor (mr::AudioFileType type)
{
    switch (type)
    {
        case mr::AudioFileType::wav: return ".wav";
        case mr::AudioFileType::flac: return ".flac";
        case mr::AudioFileType::ogg: return ".ogg";
        case mr::AudioFileType::mp3: return ".mp3";
    }
    return ".wav";
}

juce::String extensionFor (const ExportSettings& settings)
{
    return settings.video ? juce::String (".mp4") : juce::String (extensionFor (settings.type));
}

juce::File exportableVideo (const mr::Composition& composition)
{
    if (! mr::media::videoSupported())
        return {};
    const auto file = mr::videoFileOf (composition);
    return file.existsAsFile() ? file : juce::File();
}

juce::File stemsFolderFor (const juce::File& output)
{
    return output.getSiblingFile (output.getFileNameWithoutExtension() + "_stems");
}

juce::File midiFileFor (const juce::File& output)
{
    return output.withFileExtension (".mid");
}

mr::RenderOptions renderOptionsFor (const mr::Composition& composition, const ExportSettings& s)
{
    mr::RenderOptions o;
    o.output = s.output;
    o.fileType = s.type;
    o.bitDepth = s.bitDepth;
    o.quality = s.quality;
    o.sampleRate = s.sampleRate;
    if (s.video)
    {
        o.output = juce::File();   // 소리는 영상에 입힌다 (ExportJob)
        o.fileType = mr::AudioFileType::wav;
        return o;
    }
    o.loop = s.range == ExportRange::loop;
    if (s.range == ExportRange::bars)
    {
        const auto meters = composition.meters();
        o.range = std::make_pair (meters.barStart (s.firstBar - 1), meters.barStart (s.lastBar));
        o.rangeFadeSeconds = s.fadeSeconds;
    }
    if (s.stems && s.range == ExportRange::song)
        o.stemsDir = stemsFolderFor (s.output);
    return o;
}

bool usesPluginInstruments (const mr::Composition& composition)
{
    mr::RenderOptions ro;
    mr::Diagnostics d;
    const auto registry = mr::buildRegistry (composition, ro, d);
    for (const auto& t : composition.tracks)
        if (const auto* def = registry.find (t.instrument); def != nullptr && (def->type == "vst3" || def->type == "au"))
            return true;
    return false;
}

ExportJob::ExportJob (mr::Composition composition, ExportSettings settings, std::function<void (const ExportResult&)> onDone)
    : juce::Thread ("InitialDAW export"), composition_ (std::move (composition)), settings_ (std::move (settings)), onDone_ (std::move (onDone))
{
}

ExportJob::~ExportJob()
{
    *alive_ = false;
    cancel_.store (true);
    stopThread (-1);
}

mr::RenderOptions ExportJob::options()
{
    auto o = renderOptionsFor (composition_, settings_);
    o.cancel = &cancel_;
    o.progress = &progress_;
    if (settings_.video)
    {
        const auto source = exportableVideo (composition_);
        const double offset = composition_.video ? composition_.video->offsetSeconds : 0.0;
        o.onAudio = [this, source, offset] (const juce::AudioBuffer<float>& audio, double sampleRate)
        {
            if (source == juce::File())
                throw mr::Failure ("invalid-input", "the song has no video file to put the music on");
            const mr::media::ScoreAudio music { audio.getArrayOfReadPointers(), audio.getNumChannels(), audio.getNumSamples(), sampleRate, offset };
            mr::media::ScoreVideoOptions so;
            so.mixOriginal = settings_.videoMixOriginal;
            if (settings_.quality > 0)
                so.aacBitrate = settings_.quality * 1000;
            std::string error;
            video_ = mr::media::writeScoredVideo (source.getFullPathName().toStdString(), music, settings_.output.getFullPathName().toStdString(), so, error);
            if (! video_)
                throw mr::Failure ("io", error);
        };
    }
    return o;
}

void ExportJob::start()
{
    stemsExisted_ = stemsFolderFor (settings_.output).exists();
    if (usesPluginInstruments (composition_))
    {
        // 플러그인 악기는 메시지 스레드에서만 만들 수 있다
        builtOnMessageThread_ = true;
        try
        {
            render_ = std::make_unique<mr::OfflineRender> (composition_, options());
        }
        catch (const std::exception& e)
        {
            buildError_ = juce::String::fromUTF8 (e.what());
        }
    }
    startThread();
}

void ExportJob::run()
{
    ExportResult result;
    result.output = settings_.output;
    try
    {
        if (buildError_.isNotEmpty())
            throw mr::Failure ("instrument", buildError_.toStdString());
        if (render_ == nullptr)
            render_ = std::make_unique<mr::OfflineRender> (composition_, options());
        result.report = render_->run();
        result.video = video_;
        if (settings_.stems && settings_.range == ExportRange::song)
            result.stems = stemsFolderFor (settings_.output);
        if (settings_.midi)
        {
            result.midi = midiFileFor (settings_.output);
            mr::exportMidi (composition_, result.midi, false, std::nullopt);
        }
        result.ok = true;
    }
    catch (const mr::Failure& e)
    {
        result.cancelled = e.code() == "cancelled";
        result.error = juce::String::fromUTF8 (e.what());
    }
    catch (const std::exception& e)
    {
        result.error = juce::String::fromUTF8 (e.what());
    }
    // 멈췄거나 실패하면 이번에 만든 스템 폴더를 지운다 (블록마다 쓰다 만 파일)
    if (! result.ok && ! stemsExisted_ && settings_.stems)
        stemsFolderFor (settings_.output).deleteRecursively();

    std::weak_ptr<bool> alive = alive_;
    juce::MessageManager::callAsync ([this, alive, result]
    {
        if (auto a = alive.lock(); a == nullptr || ! *a || ! onDone_)
            return;
        auto done = onDone_;   // onDone 이 이 작업을 지울 수 있다
        done (result);
    });
}

} // namespace daw
