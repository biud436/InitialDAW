// 믹서: 트랙, 버스, 마스터의 채널 스트립(효과 슬롯, 센드, 밸런스, 페이더, 미터, 뮤트, 솔로)과 효과 매개변수 칸.
// 끄는 동안은 onPreview 로 재생기에만 반영하고, 손을 떼면 편집 하나로 프로젝트에 넣는다 (실행 취소 한 번에 되돌아간다).
#pragma once

#include "AnalyzerPanel.h"
#include "MidiLearn.h"

#include "Project.h"
#include "realtime/RealtimePlayer.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>
#include <vector>

namespace daw
{

class MixerPanel;
class EffectEditor;

// 좌우 미터의 표시 상태: 올라갈 때 바로, 내려갈 때 초당 24 dB, 최고값 표시 1.5초
struct MeterState
{
    float level[2] { -120.0f, -120.0f };   // dB
    float hold[2] { -120.0f, -120.0f };
    double holdUntil[2] { 0.0, 0.0 };
    float maxPeak = 0.0f;                   // 지운 뒤의 최고값 (선형)
    bool clipped = false;
    double last = 0.0;

    bool update (mr::StereoPeak peak, double now);   // 그림이 바뀌었으면 true
    void reset();
};

class ChannelStrip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int width = 98;
    static constexpr int nameHeight = 46;
    static constexpr int slotHeight = 20;
    static constexpr int panHeight = 50;
    static constexpr int readoutHeight = 22;
    static constexpr int buttonHeight = 26;

    ChannelStrip (MixerPanel& panel, Project& project, ChannelRef channel);

    ChannelRef channel() const noexcept { return channel_; }
    void setRows (int effectRows, int sendRows);
    void update();   // 값과 이름을 프로젝트에서 다시 읽는다
    void showAutomationAt (double beat);   // 곡선이 있는 페이더와 밸런스를 그 박의 값으로 보인다
    void setSelected (bool selected);
    bool isSelectedStrip() const noexcept { return selected_; }
    void setLevel (mr::StereoPeak peak, double now);
    void clearLevel();

    // 시험과 스냅숏이 쓴다
    juce::Slider& fader() noexcept { return fader_; }
    juce::Slider& panKnob() noexcept { return pan_; }
    juce::Button& muteButton() noexcept { return mute_; }
    juce::Button& soloButton() noexcept { return solo_; }
    juce::Rectangle<int> effectSlot (int row) const;
    juce::Rectangle<int> sendSlot (int row) const;
    juce::Rectangle<int> nameArea() const { return { 0, 0, getWidth(), nameHeight }; }
    const MeterState& meter() const noexcept { return meter_; }

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    juce::String getTooltip() override;

    // 센드에 크기 곡선(automation.levelDb)이 있으면 곡선이 크기를 정한다 (끌기와 0 dB 로 돌리기를 막는다)
    bool sendAutomated (int row) const;

private:
    bool isTrack() const { return channel_.kind == ChannelRef::Kind::track; }
    bool isBus() const { return channel_.kind == ChannelRef::Kind::bus; }
    bool isMaster() const { return channel_.kind == ChannelRef::Kind::master; }
    juce::Rectangle<int> faderArea() const;
    juce::Rectangle<int> meterArea() const;
    juce::Rectangle<int> peakArea() const;
    int effectAt (juce::Point<int> p) const;   // 슬롯 줄 번호, 없으면 -1
    int sendAt (juce::Point<int> p) const;
    void showEffectMenu (int row);
    void showSendMenu (int row);
    void showNameMenu();
    void commitFader (const juce::String& name);
    mr::MixerSettings faderSettings() const;
    void paintEffects (juce::Graphics& g);
    void paintSends (juce::Graphics& g);
    void paintMeter (juce::Graphics& g);
    void paintScale (juce::Graphics& g);

    MixerPanel& panel_;
    Project& project_;
    ChannelRef channel_;
    int effectRows_ = 4, sendRows_ = 3;
    bool selected_ = false;

    juce::Slider fader_, pan_;
    juce::TextButton mute_ { "M" }, solo_ { "S" };
    bool faderDragging_ = false, panDragging_ = false;
    // 곡선 쓰기: 끄는 동안의 (박, 값). 쓰기 모드에서 재생 중일 때만
    struct Writing
    {
        std::string parameter;   // "gainDb" 나 "pan"
        std::vector<std::pair<double, double>> points;
        mr::AutomationLane before;
        double fixed = 0.0;
    };
    std::optional<Writing> writing_;
    void beginWriting (const std::string& parameter);
    void recordWriting (double value);
    bool finishWriting();
    bool updating_ = false;

    // 센드 끌기
    int sendDrag_ = -1;
    double sendStartDb_ = 0.0, sendDb_ = 0.0;
    bool sendMoved_ = false;

    MeterState meter_;
};

// VCA: 소리를 모으지 않고 속한 트랙과 버스의 페이더에 게인을 더하고 뮤트를 건다 (곡의 "vcas")
class VcaStrip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int width = 78;

    VcaStrip (MixerPanel& panel, Project& project, size_t index);
    void update();
    void showAutomationAt (double beat);   // 곡선이 있으면 그 박의 값으로
    size_t index() const noexcept { return index_; }
    juce::Slider& fader() noexcept { return fader_; }
    juce::Button& muteButton() noexcept { return mute_; }

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    double faderDb() const;

    MixerPanel& panel_;
    Project& project_;
    size_t index_;
    juce::Slider fader_;
    juce::TextButton mute_ { "M" };
    bool dragging_ = false, updating_ = false;
};

class MixerPanel final : public juce::Component,
                         private TimelineState::Listener,
                         private Project::Listener
{
public:
    MixerPanel (Project& project, TimelineState& timeline);
    AnalyzerPanel& analyzer() noexcept { return analyzer_; }   // 마스터 옆의 분석 칸 (믹서가 넓을 때)
    ~MixerPanel() override;

    void projectChanged();   // 새 프로젝트: 스트립을 다시 만든다
    void setLevels (const std::vector<mr::StereoPeak>& tracks, const std::vector<mr::StereoPeak>& buses, mr::StereoPeak master, double now);
    void clearLevels();
    void showAutomationAt (double beat);   // 곡선이 정하는 페이더와 밸런스를 재생 위치의 값으로 움직인다

    std::function<void (const mr::Composition&)> onPreview;   // 끄는 동안 재생기에만 반영한다
    std::function<void (size_t track)> onOpenInstrument;       // 스트립의 악기 줄을 눌렀다
    std::function<void (size_t track)> onInstrumentMenu;       // 트랙 이름을 오른쪽 클릭했다
    std::function<void (ChannelRef channel, size_t effect)> onOpenPluginEffect;   // 플러그인 효과의 창을 연다
    std::function<void (const juce::String& text, bool error)> onStatus;
    // 사용자 프리셋 폴더 (비면 저장하지 않는다). 효과는 Effects/<형식>/, 효과 사슬은 Chains/
    juce::File presetFolder;
    // MIDI 배우기: 켜면 믹서 위에 덮개를 씌워 누른 컨트롤을 고른다 (MainComponent 가 learn 을 넣는다)
    MidiLearn* learn = nullptr;
    void setLearnMode (bool on);
    bool learnMode() const;
    void learnChanged();   // 묶음이 바뀌었다 (덮개를 다시 그린다)
    juce::Component* learnOverlay() noexcept { return learnOverlay_.get(); }   // 시험이 누른다
    std::vector<std::pair<LearnTarget, juce::Rectangle<int>>> learnableControls();   // 이 칸 좌표

    // ---- 편집 (스트립과 매개변수 칸이 부른다). 실패하면 상태 줄에 알리고 false ----
    void previewMixer (ChannelRef channel, const mr::MixerSettings& mixer);
    // 곡선 쓰기: 쓰기 모드에서 재생 중이면 그 순간의 박 (아니면 nullopt). MainComponent 가 넣는다
    std::function<std::optional<double>()> writeBeat;
    bool writeMode = false;   // 곡선이 있는 페이더도 끌 수 있다
    void setWriteMode (bool on);
    // 끄는 동안 모은 점으로 채널의 gainDb 나 pan 곡선을 [첫 박, 끝 박] 에서 바꾼다 (앞뒤는 원래 곡선이나 고정값으로 돌아간다). 편집 하나
    bool commitWrittenLane (ChannelRef channel, const std::string& parameter, const mr::AutomationLane& before, double fixed,
                            const std::vector<std::pair<double, double>>& points);
    bool commitMixer (ChannelRef channel, const mr::MixerSettings& mixer, const juce::String& name);
    void previewSends (size_t track, const std::vector<mr::SendSpec>& sends);
    bool commitSends (size_t track, const std::vector<mr::SendSpec>& sends, const juce::String& name);
    void previewEffects (ChannelRef channel, const juce::Array<juce::var>& effects);
    bool commitEffects (ChannelRef channel, const juce::Array<juce::var>& effects, const juce::String& name);
    bool addEffect (ChannelRef channel, const std::string& type);   // 끝에 더하고 매개변수 칸을 연다
    bool addPluginEffect (ChannelRef channel, const juce::var& effect);   // 플러그인 효과 객체를 끝에 더하고 그 창을 연다
    bool removeEffect (ChannelRef channel, int index);
    bool moveEffect (ChannelRef channel, int index, int delta);
    bool toggleBypass (ChannelRef channel, int index);
    bool addSend (size_t track, size_t bus);
    bool removeSend (size_t track, int index);
    bool setSendPreFader (size_t track, int index, bool preFader);
    bool addBus();
    bool renameBus (size_t bus, const juce::String& name);
    bool removeBus (size_t bus);
    void askBusName (size_t bus);
    // VCA (곡의 "vcas"). 지우거나 이름을 바꾸면 속한 채널의 "vca" 도 함께 (편집 하나)
    bool addVca (std::optional<ChannelRef> member = std::nullopt);   // member 를 넣어 만든다
    bool renameVca (size_t vca, const juce::String& name);
    bool removeVca (size_t vca);
    void askVcaName (size_t vca);
    void previewVca (size_t vca, double gainDb);
    bool setVca (size_t vca, double gainDb, bool mute, const juce::String& name);
    bool assignVca (ChannelRef channel, const std::string& vca);   // 비면 빼기
    juce::PopupMenu vcaMenu (ChannelRef channel);                   // 트랙과 버스 이름 메뉴의 "VCA" 하위 메뉴
    // 효과 사슬 프리셋 (Chains/<이름>.json, 효과 객체의 배열). 불러오면 채널의 효과를 바꾼다 (편집 하나)
    bool saveChain (ChannelRef channel, const juce::String& name);
    bool loadChain (ChannelRef channel, const juce::String& name);
    juce::StringArray chainPresets() const;
    void askChainName (ChannelRef channel);
    VcaStrip* vcaStrip (size_t vca) { return vca < vcaStrips_.size() ? vcaStrips_[vca].get() : nullptr; }

    void openEffect (ChannelRef channel, int index);   // index < 0 이면 닫는다
    bool isEffectOpen (ChannelRef channel, int index) const;
    void selectTrack (size_t track) { timeline_.setSelectedTrack ((int) track); }
    void status (const juce::String& text, bool error = false) { if (onStatus) onStatus (text, error); }

    ChannelStrip* strip (ChannelRef channel);
    EffectEditor& effectEditor() noexcept { return *editor_; }

    void paint (juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

private:
    void timelineChanged() override;
    void projectEdited (int track) override;
    void rebuildStrips();
    void scrollToShow (ChannelRef channel);
    void layoutRows();
    void preview (const std::function<void (mr::Composition&)>& change);
    static std::vector<mr::EffectSpec> specsFrom (const juce::Array<juce::var>& effects, const std::string& basePath);

    Project& project_;
    TimelineState& timeline_;
    juce::Viewport viewport_;
    juce::Component row_;
    std::vector<std::unique_ptr<ChannelStrip>> strips_;   // 트랙, 그다음 버스
    std::vector<std::unique_ptr<VcaStrip>> vcaStrips_;
    std::unique_ptr<ChannelStrip> master_;
    AnalyzerPanel analyzer_;
    juce::TextButton addBus_, addVca_;
    juce::var vcasJson() const;   // 곡의 "vcas" 배열 (깊은 사본, 없으면 빈 배열)
    bool editVcas (const juce::var& vcas, std::vector<Project::Change> extra, const juce::String& name);
    std::unique_ptr<EffectEditor> editor_;
    std::unique_ptr<juce::AlertWindow> nameDialog_;
    std::unique_ptr<juce::Component> learnOverlay_;
    int effectRows_ = 4, sendRows_ = 3;
};

} // namespace daw
