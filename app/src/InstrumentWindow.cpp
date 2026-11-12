#include "InstrumentWindow.h"

#include "Theme.h"
#include "composition/Document.h"
#include "plugins/PluginHost.h"

namespace daw
{

InstrumentPanel::InstrumentPanel (Project& project, size_t track, juce::AudioPluginInstance& plugin)
    : project_ (project), track_ (track), plugin_ (plugin)
{
    open();
}

InstrumentPanel::InstrumentPanel (Project& project, ChannelRef channel, size_t effect, std::shared_ptr<juce::AudioPluginInstance> plugin)
    : project_ (project), effect_ (std::make_pair (channel, effect)), owned_ (std::move (plugin)), plugin_ (*owned_)
{
    open();
}

void InstrumentPanel::open()
{
    if (plugin_.hasEditor())
    {
        editor_.reset (plugin_.createEditorIfNeeded());
        ownEditor_ = editor_ != nullptr;
    }
    if (editor_ == nullptr)
    {
        editor_ = std::make_unique<juce::GenericAudioProcessorEditor> (plugin_);
        editor_->setSize (std::max (420, editor_->getWidth()), std::min (640, std::max (160, editor_->getHeight())));
    }
    addAndMakeVisible (*editor_);
    setSize (std::max (200, editor_->getWidth()), std::max (100, editor_->getHeight()));
    known_ = mr::pluginStateBase64 (plugin_);
    plugin_.addListener (this);
    startTimerHz (10);
}

InstrumentPanel::~InstrumentPanel()
{
    stopTimer();
    plugin_.removeListener (this);
    editor_.reset();   // 편집기는 플러그인보다 먼저 사라져야 한다
}

void InstrumentPanel::markDirty()
{
    lastChange_.store (juce::Time::getMillisecondCounterHiRes());
    dirty_.store (true);
}

void InstrumentPanel::timerCallback()
{
    if (dirty_.load() && juce::Time::getMillisecondCounterHiRes() - lastChange_.load() > 500.0)
        captureNow();
}

bool InstrumentPanel::captureNow()
{
    dirty_.store (false);
    if (! project_.loaded())
        return false;
    if (effect_)
    {
        const auto [channel, index] = *effect_;
        const auto& effects = project_.effectsOf (channel);
        if (index >= effects.size() || effects[index].type != "plugin")
            return false;
        const auto current = mr::pluginStateBase64 (plugin_);
        if (current == known_)
            return false;
        known_ = current;
        if (effects[index].params.getProperty ("state", "").toString().toStdString() == current)
            return false;
        auto list = mr::effectsJson (project_.channelJson (channel));
        list.getArray()->set ((int) index, mr::withEffectParameter (list[(int) index], "state", juce::String (current)));
        juce::String error;
        const auto name = juce::String::fromUTF8 ("효과 소리: ") + project_.channelName (channel) + ": " + plugin_.getName();
        if (! project_.editChannel (channel, mr::withEffects (project_.channelJson (channel), *list.getArray()), name, error))
        {
            if (onStatus)
                onStatus (error.replaceCharacter ('\n', ' '), true);
            return false;
        }
        return true;
    }
    if (track_ >= project_.trackCount())
        return false;
    const auto current = mr::pluginStateBase64 (plugin_);
    if (current == known_)
        return false;
    known_ = current;
    if (project_.composition().tracks[track_].plugin.stateBase64 == current)
        return false;
    juce::String error;
    const auto name = juce::String::fromUTF8 ("악기 소리: ") + project_.channelName (ChannelRef::track (track_));
    if (! project_.editTrack (track_, mr::withPluginState (project_.trackJson (track_), current), name, error))
    {
        if (onStatus)
            onStatus (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    return true;
}

void InstrumentPanel::resync()
{
    known_ = mr::pluginStateBase64 (plugin_);
    dirty_.store (false);
}

void InstrumentPanel::resized()
{
    if (editor_ != nullptr && ! ownEditor_)
        editor_->setBounds (getLocalBounds());
}

void InstrumentPanel::childBoundsChanged (juce::Component* child)
{
    // 플러그인이 자기 화면의 크기를 바꾸면 창도 따른다
    if (child == editor_.get() && ownEditor_ && (child->getWidth() != getWidth() || child->getHeight() != getHeight()))
        setSize (child->getWidth(), child->getHeight());
}

std::vector<size_t> tracksWithChangedPluginSettings (const mr::Composition& before, const mr::Composition& after)
{
    std::vector<size_t> out;
    if (before.tracks.size() != after.tracks.size())
        return out;
    for (size_t i = 0; i < after.tracks.size(); ++i)
    {
        const auto& a = before.tracks[i].plugin;
        const auto& b = after.tracks[i].plugin;
        bool same = a.stateBase64 == b.stateBase64 && a.program == b.program && a.parameters.size() == b.parameters.size();
        for (auto x = a.parameters.begin(), y = b.parameters.begin(); same && x != a.parameters.end(); ++x, ++y)
            same = x->first == y->first && juce::JSON::toString (x->second, true) == juce::JSON::toString (y->second, true);
        if (! same)
            out.push_back (i);
    }
    return out;
}

InstrumentWindow::InstrumentWindow (Project& project, size_t track, juce::AudioPluginInstance& plugin, juce::Component* centreAround)
    : juce::DocumentWindow (project.channelName (ChannelRef::track (track)) + ": " + plugin.getName(), colours::panel,
                            juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
{
    auto panel = std::make_unique<InstrumentPanel> (project, track, plugin);
    pluginPanel_ = panel.get();
    const bool generic = ! pluginPanel_->hasOwnEditor();
    setContentOwned (panel.release(), true);
    show (centreAround, generic);
}

InstrumentWindow::InstrumentWindow (Project& project, ChannelRef channel, size_t effect, std::shared_ptr<juce::AudioPluginInstance> plugin,
                                    juce::Component* centreAround)
    : juce::DocumentWindow (project.channelName (channel) + ": " + plugin->getName(), colours::panel,
                            juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
{
    auto panel = std::make_unique<InstrumentPanel> (project, channel, effect, std::move (plugin));
    pluginPanel_ = panel.get();
    const bool generic = ! pluginPanel_->hasOwnEditor();
    setContentOwned (panel.release(), true);
    show (centreAround, generic);
}

InstrumentWindow::InstrumentWindow (std::unique_ptr<SynthPanel> panel, const juce::String& title, juce::Component* centreAround)
    : juce::DocumentWindow (title, colours::background, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
{
    synthPanel_ = panel.get();
    setContentOwned (panel.release(), true);
    show (centreAround, false);
}

void InstrumentWindow::show (juce::Component* centreAround, bool resizable)
{
    setUsingNativeTitleBar (true);
    setResizable (resizable, false);
    if (centreAround != nullptr)
        centreAroundComponent (centreAround, getWidth(), getHeight());
    setVisible (true);
    toFront (true);
}

void InstrumentWindow::closeButtonPressed()
{
    if (onClose)
        onClose();
}

} // namespace daw
