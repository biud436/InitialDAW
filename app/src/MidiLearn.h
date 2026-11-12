// MIDI 배우기: 하드웨어 컨트롤러(CC)를 믹서의 페이더, 밸런스, 뮤트, 솔로, VCA, 효과 매개변수에 묶는다. 묶음은 앱 설정이고 채널은 순서로
// 가리킨다 ("트랙 1 페이더"). 컨트롤러를 움직이는 것은 손으로 끄는 것과 같다: 처음 값에서 끌기를 시작하고, 0.4초 조용하면 놓는다
// (끄는 동안 미리 듣고 놓으면 편집 하나, 곡선 쓰기 모드면 곡선으로 적힌다).
#pragma once

#include "Project.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace daw
{

class MixerPanel;

struct LearnTarget
{
    enum class Kind { fader, pan, mute, solo, vcaFader, vcaMute, effect };
    Kind kind = Kind::fader;
    ChannelRef channel = ChannelRef::master();
    size_t vca = 0;
    int effect = -1;      // 효과 매개변수: 채널의 효과 순서
    std::string key;      // 효과 매개변수의 키

    std::string text() const;   // 앱 설정의 글: "track:0/fader", "master/fader", "vca:1/mute", "bus:0/effect:2/cutoffHz"
    static std::optional<LearnTarget> parse (const std::string& text);
    bool operator== (const LearnTarget& o) const { return text() == o.text(); }
};

struct LearnBinding
{
    int midiChannel = 1;   // 1..16
    int controller = 0;    // 0..127
    LearnTarget target;
};

class MidiLearn final : private juce::Timer
{
public:
    MidiLearn (MixerPanel& mixer, Project& project);
    ~MidiLearn() override;

    bool learning() const noexcept { return learning_; }
    void setLearning (bool on);
    void arm (const LearnTarget& target);   // 다음에 움직인 컨트롤러를 이 자리에 묶는다
    const std::optional<LearnTarget>& armed() const noexcept { return armed_; }
    const std::vector<LearnBinding>& bindings() const noexcept { return bindings_; }
    std::optional<LearnBinding> bindingOf (const LearnTarget& target) const;
    void unbind (const LearnTarget& target);
    void clear();

    // 컨트롤러 메시지: 배우는 중이고 자리를 골랐으면 묶고, 묶인 컨트롤러면 그 자리를 움직인다. 쓴 메시지면 true (트랙으로 보내지 않는다)
    bool handle (const juce::MidiMessage& message);
    void finishGestures();   // 움직이던 컨트롤을 모두 놓는다 (시험이 기다리지 않고 부른다)

    juce::String toSettings() const;
    void fromSettings (const juce::String& json);
    juce::String describe (const LearnTarget& target) const;

    std::function<void()> onChanged;   // 묶음이나 고른 자리가 바뀌었다
    std::function<void (const juce::String& text, bool error)> onStatus;

private:
    struct Gesture
    {
        LearnTarget target;
        double lastMs = 0.0;
        juce::Array<juce::var> effects;   // 효과 매개변수: 미리 듣는 효과 목록
    };
    void timerCallback() override;
    void move (const LearnTarget& target, int value, int previous);
    void finish (Gesture& g);
    void status (const juce::String& text, bool error = false) { if (onStatus) onStatus (text, error); }

    MixerPanel& mixer_;
    Project& project_;
    bool learning_ = false;
    std::optional<LearnTarget> armed_;
    std::vector<LearnBinding> bindings_;
    std::vector<Gesture> gestures_;
    std::map<std::string, int> lastValues_;   // 컨트롤러마다 앞의 값 (단추는 오를 때만)
};

} // namespace daw
