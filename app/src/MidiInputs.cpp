#include "MidiInputs.h"

namespace daw
{

MidiInputs::MidiInputs() = default;

MidiInputs::~MidiInputs()
{
    cancelPendingUpdate();
    for (auto& in : open_)
        in->stop();
    open_.clear();
}

bool MidiInputs::isEnabled (const juce::String& identifier) const { return ! disabled_.contains (identifier); }

void MidiInputs::setEnabled (const juce::String& identifier, bool on)
{
    if (on)
        disabled_.removeString (identifier);
    else if (! disabled_.contains (identifier))
        disabled_.add (identifier);
    refreshDevices();
}

void MidiInputs::setDisabledIdentifiers (const juce::StringArray& ids)
{
    disabled_ = ids;
    refreshDevices();
}

void MidiInputs::refreshDevices()
{
    devices_ = juce::MidiInput::getAvailableDevices();
    // 없어졌거나 끈 장치는 닫는다
    open_.erase (std::remove_if (open_.begin(), open_.end(), [this] (const std::unique_ptr<juce::MidiInput>& in)
    {
        const auto id = in->getIdentifier();
        bool present = false;
        for (const auto& d : devices_)
            present |= d.identifier == id;
        if (present && isEnabled (id))
            return false;
        in->stop();
        return true;
    }), open_.end());
    // 켠 장치 가운데 아직 열지 않은 것을 연다
    for (const auto& d : devices_)
    {
        if (! isEnabled (d.identifier))
            continue;
        bool opened = false;
        for (const auto& in : open_)
            opened |= in->getIdentifier() == d.identifier;
        if (opened)
            continue;
        if (auto in = juce::MidiInput::openDevice (d.identifier, this))
        {
            in->start();
            open_.push_back (std::move (in));
        }
    }
}

void MidiInputs::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    if (! (message.isNoteOnOrOff() || message.isController() || message.isPitchWheel() || message.isChannelPressure() || message.isAftertouch()))
        return;
    Event e { message, positionSource ? positionSource() : 0 };
    const auto scope = fifo_.write (1);   // 가득 차면 버린다
    if (scope.blockSize1 > 0)
        events_[(size_t) scope.startIndex1] = std::move (e);
    else if (scope.blockSize2 > 0)
        events_[(size_t) scope.startIndex2] = std::move (e);
    else
        return;
    triggerAsyncUpdate();
}

void MidiInputs::handleAsyncUpdate()
{
    const auto scope = fifo_.read (fifo_.getNumReady());
    auto take = [this] (int start, int count)
    {
        for (int i = start; i < start + count; ++i)
            if (onEvent)
                onEvent (events_[(size_t) i]);
    };
    take (scope.startIndex1, scope.blockSize1);
    take (scope.startIndex2, scope.blockSize2);
}

void MidiInputs::inject (const juce::MidiMessage& message)
{
    if (onEvent)
        onEvent ({ message, positionSource ? positionSource() : 0 });
}

} // namespace daw
