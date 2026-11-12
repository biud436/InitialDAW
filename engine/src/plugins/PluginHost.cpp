#include "plugins/PluginHost.h"

#include "core/Json.h"
#include "core/Log.h"
#include "timing/TempoMap.h"

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

namespace mr
{

namespace
{

std::unique_ptr<juce::ScopedJuceInitialiser_GUI>& runtime()
{
    static std::unique_ptr<juce::ScopedJuceInitialiser_GUI> r;
    return r;
}

std::unique_ptr<PluginHost>& hostInstance()
{
    static std::unique_ptr<PluginHost> h;
    return h;
}

} // namespace

HostSession::HostSession()
{
    if (runtime() == nullptr)
        runtime() = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
}

HostSession::~HostSession()
{
    hostInstance().reset();   // 플러그인 형식(과 불러온 모듈)을 먼저 내리고 JUCE 를 닫는다
    runtime().reset();
}

bool HostSession::active()
{
    return runtime() != nullptr;
}

PluginHost& PluginHost::get()
{
    if (! HostSession::active())
        throw Failure ("internal", "plugin hosting needs an active HostSession (create one in main)");
    if (hostInstance() == nullptr)
        hostInstance().reset (new PluginHost());
    return *hostInstance();
}

PluginHost::PluginHost()
{
#if JUCE_PLUGINHOST_VST3
    manager_.addFormat (new juce::VST3PluginFormat());
#endif
#if JUCE_PLUGINHOST_AU && JUCE_MAC
    manager_.addFormat (new juce::AudioUnitPluginFormat());
#endif
}

juce::AudioPluginFormat* PluginHost::formatFor (const std::string& format) const
{
    const juce::String wanted = format == "vst3" ? "VST3" : (format == "au" ? "AudioUnit" : juce::String (format));
    for (auto* f : manager_.getFormats())
        if (f->getName() == wanted)
            return f;
    return nullptr;
}

bool PluginHost::supports (const std::string& format) const
{
    return formatFor (format) != nullptr;
}

std::vector<juce::PluginDescription> PluginHost::scan (const std::string& format, const std::vector<std::string>& paths,
                                                       std::vector<Diagnostic>& problems)
{
    std::vector<juce::PluginDescription> out;
    auto* f = formatFor (format);
    if (f == nullptr)
    {
        problems.push_back ({ format, "plugin format '" + format + "' is not supported by this build", "use vst3 or au (macOS)" });
        return out;
    }

    juce::StringArray identifiers;
    if (paths.empty())
        identifiers = f->searchPathsForPlugins (f->getDefaultLocationsToSearch(), true, false);
    for (const auto& p : paths)
    {
        const juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (p.c_str())));
        if (format == "vst3" && file.isDirectory() && ! file.hasFileExtension ("vst3"))
            identifiers.addArray (f->searchPathsForPlugins (juce::FileSearchPath (file.getFullPathName()), true, false));
        else if (format == "vst3")
        {
            if (! file.exists())
                problems.push_back ({ p, "no such file or directory", {} });
            else
                identifiers.add (file.getFullPathName());
        }
        else
            identifiers.add (juce::String::fromUTF8 (p.c_str()));
    }

    for (const auto& id : identifiers)
    {
        juce::OwnedArray<juce::PluginDescription> found;
        f->findAllTypesForFile (found, id);
        if (found.isEmpty())
            problems.push_back ({ id.toStdString(), "no plugin could be loaded from here", {} });
        for (auto* d : found)
            out.push_back (*d);
    }
    return out;
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::load (const std::string& format, const std::string& fileOrIdentifier,
                                                             const std::string& nameFilter, double sampleRate, int blockSize,
                                                             juce::PluginDescription& chosen, std::string& error)
{
    auto* f = formatFor (format);
    if (f == nullptr)
    {
        error = "plugin format '" + format + "' is not supported by this build";
        return nullptr;
    }
    if (format == "vst3" && ! juce::File (juce::String::fromUTF8 (fileOrIdentifier.c_str())).exists())
    {
        error = "plugin not found: " + fileOrIdentifier;
        return nullptr;
    }
    juce::OwnedArray<juce::PluginDescription> found;
    f->findAllTypesForFile (found, juce::String::fromUTF8 (fileOrIdentifier.c_str()));
    if (found.isEmpty())
    {
        error = "no plugin could be loaded from " + fileOrIdentifier;
        return nullptr;
    }
    const juce::PluginDescription* pick = nullptr;
    for (auto* d : found)
        if (nameFilter.empty() || d->name.equalsIgnoreCase (juce::String::fromUTF8 (nameFilter.c_str())))
        {
            pick = d;
            break;
        }
    if (pick == nullptr)
    {
        juce::StringArray names;
        for (auto* d : found)
            names.add (d->name);
        error = "no plugin named '" + nameFilter + "' in " + fileOrIdentifier + " (found: " + names.joinIntoString (", ").toStdString() + ")";
        return nullptr;
    }
    juce::String message;
    auto instance = manager_.createPluginInstance (*pick, sampleRate, blockSize, message);
    if (instance == nullptr)
    {
        error = "cannot instantiate " + pick->name.toStdString() + ": " + message.toStdString();
        return nullptr;
    }
    chosen = *pick;
    return instance;
}

juce::var describeEntry (const juce::PluginDescription& d)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", d.name);
    o->setProperty ("format", d.pluginFormatName == "VST3" ? juce::String ("vst3") : (d.pluginFormatName == "AudioUnit" ? juce::String ("au") : d.pluginFormatName));
    o->setProperty ("manufacturer", d.manufacturerName);
    o->setProperty ("version", d.version);
    o->setProperty ("category", d.category);
    o->setProperty ("isInstrument", d.isInstrument);
    o->setProperty ("plugin", d.fileOrIdentifier);
    o->setProperty ("uid", d.createIdentifierString());
    o->setProperty ("inputs", d.numInputChannels);
    o->setProperty ("outputs", d.numOutputChannels);
    return juce::var (o);
}

namespace
{

juce::String parameterId (juce::AudioProcessorParameter* p)
{
    if (auto* withId = dynamic_cast<juce::HostedAudioProcessorParameter*> (p))
        return withId->getParameterID();
    return {};
}

} // namespace

juce::var describeInstance (juce::AudioPluginInstance& instance, const juce::PluginDescription& d)
{
    auto entry = describeEntry (d);
    auto* o = entry.getDynamicObject();
    o->setProperty ("latencySamples", instance.getLatencySamples());
    o->setProperty ("outputChannels", instance.getTotalNumOutputChannels());
    o->setProperty ("acceptsMidi", instance.acceptsMidi());
    juce::Array<juce::var> programs;
    for (int i = 0; i < instance.getNumPrograms(); ++i)
        programs.add (instance.getProgramName (i));
    o->setProperty ("programs", programs);
    o->setProperty ("currentProgram", instance.getCurrentProgram());
    juce::Array<juce::var> params;
    for (auto* p : instance.getParameters())
    {
        auto* po = new juce::DynamicObject();
        po->setProperty ("index", p->getParameterIndex());
        po->setProperty ("id", parameterId (p));
        po->setProperty ("name", p->getName (128));
        po->setProperty ("label", p->getLabel());
        po->setProperty ("value", std::round (p->getValue() * 1.0e6) / 1.0e6);
        po->setProperty ("default", std::round (p->getDefaultValue() * 1.0e6) / 1.0e6);
        po->setProperty ("text", p->getCurrentValueAsText());
        po->setProperty ("steps", p->getNumSteps());
        po->setProperty ("discrete", p->isDiscrete());
        params.add (juce::var (po));
    }
    o->setProperty ("parameters", params);
    return entry;
}

std::string pluginStateBase64 (juce::AudioPluginInstance& instance)
{
    juce::MemoryBlock state;
    instance.getStateInformation (state);
    return encodePluginState (state);
}

void checkPluginSettings (juce::AudioPluginInstance& instance, const PluginSettings& s, const std::string& path, std::vector<Diagnostic>& problems)
{
    if (! s.stateBase64.empty())
    {
        juce::MemoryBlock state;
        if (! decodePluginState (s.stateBase64, state))
            problems.push_back ({ path + ".state", "plugin state is not valid base64", {} });
    }
    if (s.program && (*s.program < 0 || *s.program >= instance.getNumPrograms()))
        problems.push_back ({ path + ".program", "program " + std::to_string (*s.program) + " is out of range (the plugin has "
                              + std::to_string (instance.getNumPrograms()) + ")", {} });
    for (const auto& [key, value] : s.parameters)
    {
        bool found = false;
        for (auto* p : instance.getParameters())
            found |= p->getName (128).equalsIgnoreCase (juce::String::fromUTF8 (key.c_str())) || parameterId (p) == juce::String::fromUTF8 (key.c_str());
        if (! found)
            problems.push_back ({ path + ".parameters." + key, "the plugin has no parameter '" + key + "'", {} });
        else if (json::isNumber (value) && ((double) value < 0.0 || (double) value > 1.0))
            problems.push_back ({ path + ".parameters." + key, "numeric parameter values are normalised 0..1", {} });
    }
}

void applyPluginSettings (juce::AudioPluginInstance& instance, const PluginSettings& s, const std::string& path, std::vector<Diagnostic>& problems,
                          bool onlyChanges)
{
    juce::MemoryBlock state;
    if (s.stateFile != juce::File())
    {
        if (! s.stateFile.loadFileAsData (state))
            problems.push_back ({ path + ".stateFile", "cannot read plugin state file " + s.stateFile.getFullPathName().toStdString(), {} });
    }
    else if (! s.stateBase64.empty())
    {
        if (! decodePluginState (s.stateBase64, state))
            problems.push_back ({ path + ".state", "plugin state is not valid base64", {} });
    }
    if (state.getSize() > 0)
    {
        juce::MemoryBlock current;
        if (onlyChanges)
            instance.getStateInformation (current);
        if (! onlyChanges || current != state)
            instance.setStateInformation (state.getData(), (int) state.getSize());
    }

    if (s.program)
    {
        if (*s.program < 0 || *s.program >= instance.getNumPrograms())
            problems.push_back ({ path + ".program", "program " + std::to_string (*s.program) + " is out of range (the plugin has "
                                  + std::to_string (instance.getNumPrograms()) + ")", {} });
        else if (! onlyChanges || instance.getCurrentProgram() != *s.program)
            instance.setCurrentProgram (*s.program);
    }

    std::vector<std::string> names;
    for (auto* p : instance.getParameters())
        names.push_back (p->getName (128).toStdString());
    for (const auto& [key, value] : s.parameters)
    {
        juce::AudioProcessorParameter* match = nullptr;
        for (auto* p : instance.getParameters())
            if (p->getName (128).equalsIgnoreCase (juce::String::fromUTF8 (key.c_str())) || parameterId (p) == juce::String::fromUTF8 (key.c_str()))
            {
                match = p;
                break;
            }
        const auto ppath = path + ".parameters." + key;
        if (match == nullptr)
        {
            const auto suggestion = closestMatch (key, names);
            problems.push_back ({ ppath, "the plugin has no parameter '" + key + "'",
                                  suggestion.empty() ? "run `music-render plugins describe` to list them" : "did you mean '" + suggestion + "'?" });
            continue;
        }
        float normalised = 0.0f;
        if (value.isString())
            normalised = match->getValueForText (value.toString());
        else if (value.isInt() || value.isInt64() || value.isDouble())
        {
            const double v = (double) value;
            if (v < 0.0 || v > 1.0)
            {
                problems.push_back ({ ppath, "numeric parameter values are normalised 0..1, got " + formatNumber (v), "give the value as text (e.g. \"-12 dB\") to use the plugin's units" });
                continue;
            }
            normalised = (float) v;
        }
        else
        {
            problems.push_back ({ ppath, "parameter value must be a number 0..1 or text", {} });
            continue;
        }
        if (! onlyChanges || std::abs (match->getValue() - normalised) > 1.0e-6f)
            match->setValueNotifyingHost (normalised);
    }
}

namespace
{

class OfflinePlayHead final : public juce::AudioPlayHead
{
public:
    OfflinePlayHead (const PluginHostContext& c) : ctx_ (c) {}

    void setPosition (int64_t sample) { sample_ = sample; }

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        const double seconds = sample_ / ctx_.sampleRate;
        info.setTimeInSamples (sample_);
        info.setTimeInSeconds (seconds);
        info.setIsPlaying (true);
        if (ctx_.tempo != nullptr)
        {
            const double beat = ctx_.tempo->beatAt (seconds);
            info.setBpm (ctx_.tempo->bpmAt (beat));
            info.setPpqPosition (beat);
            if (ctx_.meters != nullptr)
            {
                const auto& segment = ctx_.meters->segmentAt (beat);
                info.setPpqPositionOfLastBarStart (ctx_.meters->floorToBar (beat));
                info.setBarCount ((int64_t) ctx_.meters->barAt (beat));
                info.setTimeSignature (juce::AudioPlayHead::TimeSignature { segment.beatsPerBar, segment.beatUnit });
            }
            else
            {
                const double bar = ctx_.beatsPerBar * 4.0 / ctx_.beatUnit;
                info.setPpqPositionOfLastBarStart (std::floor (beat / bar) * bar);
                info.setBarCount ((int64_t) std::floor (beat / bar));
                info.setTimeSignature (juce::AudioPlayHead::TimeSignature { ctx_.beatsPerBar, ctx_.beatUnit });
            }
        }
        return info;
    }

private:
    PluginHostContext ctx_;
    int64_t sample_ = 0;
};

class PluginInstrument final : public Instrument
{
public:
    PluginInstrument (std::unique_ptr<juce::AudioPluginInstance> instance, juce::PluginDescription d, const PluginHostContext& ctx)
        : instance_ (std::move (instance)), description_ (std::move (d)), playHead_ (ctx), realtime_ (ctx.realtime)
    {
        // 악기는 스테레오 주 출력 하나로 쓴다. 입력 버스는 끈다.
        auto layout = instance_->getBusesLayout();
        for (auto& in : layout.inputBuses)
            in = juce::AudioChannelSet::disabled();
        if (! layout.outputBuses.isEmpty())
            layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        if (instance_->checkBusesLayoutSupported (layout))
            instance_->setBusesLayout (layout);
        instance_->setPlayHead (&playHead_);
    }

    ~PluginInstrument() override
    {
        instance_->releaseResources();
        instance_->setPlayHead (nullptr);
    }

    void prepare (double sampleRate, int maxBlockSize) override
    {
        instance_->setNonRealtime (! realtime_);
        instance_->setRateAndBufferSizeDetails (sampleRate, maxBlockSize);
        instance_->prepareToPlay (sampleRate, maxBlockSize);
        const int channels = std::max ({ 2, instance_->getTotalNumInputChannels(), instance_->getTotalNumOutputChannels() });
        scratch_.setSize (channels, maxBlockSize);
    }

    void render (juce::AudioBuffer<float>& buffer, int numSamples, const juce::MidiBuffer& midi, int64_t blockStart) override
    {
        playHead_.setPosition (blockStart);
        scratch_.clear();
        juce::AudioBuffer<float> view (scratch_.getArrayOfWritePointers(), scratch_.getNumChannels(), numSamples);
        juce::MidiBuffer events (midi);
        instance_->processBlock (view, events);
        const int outs = std::max (1, instance_->getTotalNumOutputChannels());
        buffer.copyFrom (0, 0, view, 0, 0, numSamples);
        buffer.copyFrom (1, 0, view, outs > 1 ? 1 : 0, 0, numSamples);
    }

    int latencySamples() const override { return instance_->getLatencySamples(); }
    juce::AudioPluginInstance* plugin() override { return instance_.get(); }
    std::string pluginBaseState() const override { return baseState_; }
    void setBaseState (std::string state) { baseState_ = std::move (state); }

    std::string description() const override
    {
        return (description_.pluginFormatName == "VST3" ? "vst3:" : "au:") + description_.name.toStdString();
    }

private:
    std::unique_ptr<juce::AudioPluginInstance> instance_;
    juce::PluginDescription description_;
    OfflinePlayHead playHead_;
    bool realtime_ = false;
    juce::AudioBuffer<float> scratch_;
    std::string baseState_;
};

} // namespace

std::unique_ptr<Instrument> makePluginInstrument (const std::string& format, const std::string& fileOrIdentifier,
                                                  const std::string& nameFilter, const PluginSettings& settings,
                                                  const PluginHostContext& ctx, const std::string& path, std::string& error,
                                                  const PluginSettings* extra, const std::string& extraPath)
{
    juce::PluginDescription chosen;
    auto instance = PluginHost::get().load (format, fileOrIdentifier, nameFilter, ctx.sampleRate, ctx.blockSize, chosen, error);
    if (instance == nullptr)
        return nullptr;
    if (! chosen.isInstrument)
        log::warn ("plugin " + chosen.name.toStdString() + " does not declare itself an instrument");
    auto inst = std::make_unique<PluginInstrument> (std::move (instance), chosen, ctx);
    std::vector<Diagnostic> problems;
    applyPluginSettings (*inst->plugin(), settings, path, problems);
    inst->setBaseState (pluginStateBase64 (*inst->plugin()));
    if (extra != nullptr)
        applyPluginSettings (*inst->plugin(), *extra, extraPath, problems);
    if (! problems.empty())
    {
        error = problems.front().path + ": " + problems.front().message + (problems.front().hint.empty() ? std::string {} : " (" + problems.front().hint + ")");
        return nullptr;
    }
    return inst;
}

juce::File defaultPluginCache()
{
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".cache/music-render/plugins.json");
}

void writePluginCache (const juce::File& file, const std::vector<juce::PluginDescription>& plugins)
{
    juce::Array<juce::var> list;
    for (const auto& d : plugins)
    {
        auto entry = describeEntry (d);
        if (auto xml = d.createXml())
            entry.getDynamicObject()->setProperty ("description", xml->toString (juce::XmlElement::TextFormat().singleLine()));
        list.add (entry);
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("version", 1);
    root->setProperty ("plugins", list);
    file.getParentDirectory().createDirectory();
    if (! file.replaceWithText (juce::JSON::toString (juce::var (root))))
        throw Failure ("io", "cannot write plugin cache " + file.getFullPathName().toStdString());
}

std::optional<juce::PluginDescription> findCachedPlugin (const juce::File& cache, const std::string& format, const std::string& name)
{
    if (! cache.existsAsFile())
        return std::nullopt;
    const auto root = juce::JSON::parse (cache);
    if (auto* list = root.getProperty ("plugins", juce::var()).getArray())
        for (const auto& e : *list)
            if (e.getProperty ("name", "").toString().equalsIgnoreCase (juce::String::fromUTF8 (name.c_str()))
                && e.getProperty ("format", "").toString() == juce::String (format))
                if (auto xml = juce::parseXML (e.getProperty ("description", "").toString()))
                {
                    juce::PluginDescription d;
                    if (d.loadFromXml (*xml))
                        return d;
                }
    return std::nullopt;
}

} // namespace mr
