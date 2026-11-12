// 위쪽 트랜스포트: 처음으로, 재생과 정지, 녹음, 위치(마디.박과 시각), 템포와 박자, 모니터 게인, 자판 연주, 반복, 메트로놈, 상태.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

namespace daw
{

class IconButton final : public juce::Button
{
public:
    enum class Icon { play, stop, toStart, record, loop, metronome };
    IconButton (const juce::String& name, Icon icon);
    void setIcon (Icon icon);
    void setOn (bool on);   // 켠 상태 (녹음, 반복, 메트로놈)
    bool isOn() const noexcept { return on_; }
    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

private:
    Icon icon_;
    bool on_ = false;
};

class TransportBar final : public juce::Component
{
public:
    TransportBar();

    void setTitle (const juce::String& title);
    void setPlaying (bool playing);
    void setPosition (int bar, int beat, double seconds);
    void setTempo (double bpm, int beatsPerBar, int beatUnit);
    void setStatus (const juce::String& text, bool error = false);
    void setCpu (double load);
    void setMonitorGainDb (double db);
    void setTransportEnabled (bool enabled);
    void setTyping (bool on, const juce::String& range);   // 자판 연주 단추: 켬과 끔, Z 의 음 이름
    void setRecording (bool on);
    void setLooping (bool on);
    void setMetronome (bool on);

    std::function<void()> onPlayStop;
    std::function<void()> onToStart;
    std::function<void()> onRecord;
    std::function<void()> onLoop;
    std::function<void()> onMetronome;
    std::function<void (double)> onMonitorGainChanged;
    std::function<void()> onTempoClicked;   // 템포와 박자를 두 번 클릭했다
    std::function<void()> onTypingClicked;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

private:
    juce::Label title_;
    IconButton toStart_ { juce::String::fromUTF8 ("처음으로"), IconButton::Icon::toStart };
    IconButton playStop_ { juce::String::fromUTF8 ("재생"), IconButton::Icon::play };
    IconButton record_ { juce::String::fromUTF8 ("녹음 (⌥R)"), IconButton::Icon::record };
    IconButton loop_ { juce::String::fromUTF8 ("반복 구간 재생 (⌥L)"), IconButton::Icon::loop };
    IconButton metronome_ { juce::String::fromUTF8 ("메트로놈 (⌥M)"), IconButton::Icon::metronome };
    juce::Slider monitor_;
    juce::Label monitorLabel_;
    juce::TextButton typing_;
    juce::Label status_;
    juce::String positionText_, timeText_, tempoText_, signatureText_, cpuText_;
    juce::Rectangle<int> positionArea_, tempoArea_, cpuArea_;
    bool statusError_ = false;
};

} // namespace daw
