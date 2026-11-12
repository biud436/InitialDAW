#include "plugins/PluginEffect.h"

#include "core/Json.h"
#include "plugins/PluginHost.h"

#include <algorithm>

namespace mr
{

PluginEffect::PluginEffect (std::shared_ptr<juce::AudioPluginInstance> plugin, std::shared_ptr<const TempoMap> tempo, bool realtime)
    : plugin_ (std::move (plugin)), tempo_ (std::move (tempo)), realtime_ (realtime)
{
    plugin_->setPlayHead (this);
}

PluginEffect::~PluginEffect()
{
    plugin_->setPlayHead (nullptr);
}

void PluginEffect::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    plugin_->setNonRealtime (! realtime_);
    // 스테레오로 묶는다 (안 되면 플러그인의 배치 그대로)
    auto layout = plugin_->getBusesLayout();
    if (! layout.inputBuses.isEmpty())
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    if (! layout.outputBuses.isEmpty())
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    if (plugin_->checkBusesLayoutSupported (layout))
        plugin_->setBusesLayout (layout);
    plugin_->prepareToPlay (sampleRate, maxBlockSize);
    const int channels = std::max ({ 2, plugin_->getTotalNumInputChannels(), plugin_->getTotalNumOutputChannels() });
    scratch_.setSize (channels, maxBlockSize);
    midi_.ensureSize (256);
}

void PluginEffect::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    const int channels = scratch_.getNumChannels();
    const int inputs = plugin_->getTotalNumInputChannels();
    for (int ch = 0; ch < channels; ++ch)
    {
        if (ch < inputs)
            scratch_.copyFrom (ch, 0, buffer, std::min (ch, 1), 0, numSamples);
        else
            scratch_.clear (ch, 0, numSamples);
    }
    juce::AudioBuffer<float> view (scratch_.getArrayOfWritePointers(), channels, numSamples);
    midi_.clear();
    plugin_->processBlock (view, midi_);
    const int outputs = std::max (1, plugin_->getTotalNumOutputChannels());
    for (int ch = 0; ch < 2; ++ch)
        buffer.copyFrom (ch, 0, view, std::min (ch, outputs - 1), 0, numSamples);
}

juce::Optional<juce::AudioPlayHead::PositionInfo> PluginEffect::getPosition() const
{
    PositionInfo info;
    const int64_t sample = position_.start;
    const double seconds = (double) sample / sampleRate_;
    info.setTimeInSamples (sample);
    info.setTimeInSeconds (seconds);
    info.setIsPlaying (! position_.held);
    if (tempo_ != nullptr)
    {
        const double beat = tempo_->beatAt (seconds);
        info.setPpqPosition (beat);
        info.setBpm (tempo_->bpmAt (beat));
    }
    return info;
}

PluginSettings pluginEffectSettings (const juce::var& p)
{
    PluginSettings s;
    if (const auto state = p.getProperty ("state", juce::var()); state.isString())
        s.stateBase64 = state.toString().toStdString();
    if (const auto program = p.getProperty ("program", juce::var()); json::isNumber (program))
        s.program = (int) program;
    if (const auto* params = p.getProperty ("parameters", juce::var()).getDynamicObject())
        for (const auto& prop : params->getProperties())
            s.parameters[prop.name.toString().toStdString()] = prop.value;
    return s;
}

std::unique_ptr<Effect> makePluginEffect (const EffectSpec& spec, const juce::File& baseDir, double sampleRate, int blockSize,
                                          const TempoMap* tempo, bool realtime)
{
    const auto& p = spec.params;
    const auto format = p.getProperty ("format", "vst3").toString().toStdString();
    auto location = p.getProperty ("plugin", "").toString();
    if (format == "vst3" && ! juce::File::isAbsolutePath (location))
        location = baseDir.getChildFile (location).getFullPathName();
    juce::PluginDescription chosen;
    std::string error;
    std::unique_ptr<juce::AudioPluginInstance> instance = PluginHost::get().load (format, location.toStdString(),
                                                                                  p.getProperty ("name", "").toString().toStdString(),
                                                                                  sampleRate, blockSize, chosen, error);
    if (instance == nullptr)
        throw Failure ("instrument", spec.path + ": " + error);
    if (chosen.isInstrument)
        throw Failure ("invalid-input", spec.path + ": '" + chosen.name.toStdString() + "' is an instrument, not an effect");
    std::vector<Diagnostic> problems;
    applyPluginSettings (*instance, pluginEffectSettings (p), spec.path, problems);
    if (! problems.empty())
        throw Failure ("invalid-input", spec.path + ": the plugin does not accept these settings", problems);
    auto shared = std::shared_ptr<juce::AudioPluginInstance> (instance.release());
    auto tempoCopy = std::make_shared<const TempoMap> (tempo != nullptr ? *tempo : TempoMap ({ TempoPoint { 0.0, 120.0, false } }));
    return std::make_unique<PluginEffect> (std::move (shared), std::move (tempoCopy), realtime);
}

void validatePluginEffect (const EffectSpec& spec, Diagnostics& d)
{
    json::ObjectReader r (spec.params, spec.path, d);
    r.known ("type");
    r.known ("comment");
    r.boolean ("bypass", false);
    if (auto format = r.string ("format", false); format && *format != "vst3" && *format != "au")
        d.error (r.pathOf ("format"), "plugin format must be vst3 or au");
    if (auto plugin = r.string ("plugin", true); plugin && plugin->empty())
        d.error (r.pathOf ("plugin"), "plugin needs a VST3 file or an AU identifier");
    r.string ("name", false);
    if (auto state = r.string ("state", false))
    {
        juce::MemoryBlock block;
        if (! decodePluginState (*state, block))
            d.error (r.pathOf ("state"), "plugin state must be base64");
    }
    r.integer ("program", false, 0, 100000);
    r.object ("parameters", false);
    r.warnUnknown ({ "type", "comment", "bypass", "format", "plugin", "name", "state", "program", "parameters" });
}

} // namespace mr
