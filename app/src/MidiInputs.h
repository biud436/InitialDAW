// MIDI 입력 장치. 켠 장치를 모두 열고, 받은 메시지를 그때의 재생 위치와 함께 메시지 스레드로 넘긴다.
// 메시지는 MIDI 스레드에서 고정 크기의 FIFO 에 넣고 AsyncUpdater 로 메시지 스레드가 비운다.
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace daw
{

class MidiInputs final : private juce::MidiInputCallback,
                         private juce::AsyncUpdater
{
public:
    struct Event
    {
        juce::MidiMessage message;
        int64_t position = 0;   // 받은 순간의 재생 샘플 (positionSource)
    };

    MidiInputs();
    ~MidiInputs() override;

    std::function<void (const Event&)> onEvent;    // 메시지 스레드
    std::function<int64_t()> positionSource;       // 아무 스레드에서 불러도 되는 재생 위치

    // 장치 목록을 다시 읽어 켠 장치를 열고 없어진 장치를 닫는다 (꽂거나 뺀 뒤)
    void refreshDevices();
    juce::Array<juce::MidiDeviceInfo> devices() const { return devices_; }
    size_t openCount() const noexcept { return open_.size(); }
    bool isEnabled (const juce::String& identifier) const;
    void setEnabled (const juce::String& identifier, bool on);
    // 앱 설정용: 끈 장치의 식별자 (기본은 모두 켬)
    juce::StringArray disabledIdentifiers() const { return disabled_; }
    void setDisabledIdentifiers (const juce::StringArray& ids);

    // 시험: 장치 없이 메시지를 넣는다 (메시지 스레드에서 바로 onEvent)
    void inject (const juce::MidiMessage& message);

private:
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;
    void handleAsyncUpdate() override;

    juce::Array<juce::MidiDeviceInfo> devices_;
    std::vector<std::unique_ptr<juce::MidiInput>> open_;
    juce::StringArray disabled_;
    juce::AbstractFifo fifo_ { 512 };
    std::vector<Event> events_ = std::vector<Event> (512);
};

} // namespace daw
