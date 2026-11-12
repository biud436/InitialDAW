// 트랙 악기의 창. 플러그인이면 그 자체 화면(없으면 매개변수 목록, JUCE 일반 편집기), 내장 신스면 앱이 그린 신스 패널.
// 플러그인이 값 변경을 알리고 0.5초 조용해지면 플러그인 상태를 곡의 트랙 plugin.state 로 쓴다 (편집 하나).
#pragma once

#include "Project.h"
#include "SynthPanel.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <string>
#include <vector>

namespace daw
{

class InstrumentPanel final : public juce::Component,
                              private juce::AudioProcessorListener,
                              private juce::Timer
{
public:
    InstrumentPanel (Project& project, size_t track, juce::AudioPluginInstance& plugin);
    // 효과 칸의 플러그인: 바뀐 소리를 그 효과 객체의 state 로 쓴다. 플러그인을 함께 쥔다 (사슬이 바뀌어도 창을 닫을 때까지)
    InstrumentPanel (Project& project, ChannelRef channel, size_t effect, std::shared_ptr<juce::AudioPluginInstance> plugin);
    ~InstrumentPanel() override;

    bool isEffect() const noexcept { return effect_.has_value(); }

    size_t track() const noexcept { return track_; }
    juce::AudioPluginInstance& plugin() noexcept { return plugin_; }
    juce::AudioProcessorEditor* editor() noexcept { return editor_.get(); }
    bool hasOwnEditor() const noexcept { return ownEditor_; }

    // 플러그인 상태가 마지막으로 맞춘 상태와 다르면 곡에 쓴다 (편집 하나). 썼으면 true.
    bool captureNow();
    // 곡의 상태를 플러그인에 넣은 뒤(실행 취소, 다시 실행) 부른다: 지금 상태를 맞춘 상태로 삼는다
    void resync();

    std::function<void (const juce::String& text, bool error)> onStatus;

    void resized() override;
    void childBoundsChanged (juce::Component* child) override;

private:
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { markDirty(); }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details) override
    {
        if (details.parameterInfoChanged || details.programChanged || details.nonParameterStateChanged)
            markDirty();
    }
    void markDirty();   // 아무 스레드에서나 온다
    void timerCallback() override;

    void open();

    Project& project_;
    size_t track_ = 0;
    std::optional<std::pair<ChannelRef, size_t>> effect_;   // 효과 칸이면 채널과 번호
    std::shared_ptr<juce::AudioPluginInstance> owned_;       // 효과 칸이면 플러그인을 쥔다
    juce::AudioPluginInstance& plugin_;
    std::unique_ptr<juce::AudioProcessorEditor> editor_;
    bool ownEditor_ = false;
    std::string known_;                          // 곡과 맞는 플러그인 상태 (base64)
    std::atomic<bool> dirty_ { false };
    std::atomic<double> lastChange_ { 0.0 };     // 밀리초
};

// before 와 after 사이에 트랙의 플러그인 설정(plugin)이 바뀐 트랙 (트랙 목록이 같을 때)
std::vector<size_t> tracksWithChangedPluginSettings (const mr::Composition& before, const mr::Composition& after);

class InstrumentWindow final : public juce::DocumentWindow
{
public:
    InstrumentWindow (Project& project, size_t track, juce::AudioPluginInstance& plugin, juce::Component* centreAround);
    InstrumentWindow (Project& project, ChannelRef channel, size_t effect, std::shared_ptr<juce::AudioPluginInstance> plugin,
                      juce::Component* centreAround);
    InstrumentWindow (std::unique_ptr<SynthPanel> panel, const juce::String& title, juce::Component* centreAround);
    InstrumentPanel* pluginPanel() noexcept { return pluginPanel_; }   // 플러그인 창이 아니면 nullptr
    SynthPanel* synthPanel() noexcept { return synthPanel_; }          // 신스 창이 아니면 nullptr
    std::function<void()> onClose;   // 닫기 단추 (주인이 지운다)
    void closeButtonPressed() override;

private:
    void show (juce::Component* centreAround, bool resizable);

    InstrumentPanel* pluginPanel_ = nullptr;
    SynthPanel* synthPanel_ = nullptr;
};

} // namespace daw
