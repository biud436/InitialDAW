// 효과 매개변수 칸: 믹서 오른쪽에 붙는다. 엔진의 매개변수 표(effectParameters)로 줄을 만들고, eq 는 대역과 주파수 응답 곡선을 그린다.
#pragma once

#include "Project.h"
#include "effects/Effects.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>
#include <string>
#include <vector>

namespace daw
{

class MixerPanel;

// eq 의 주파수 응답 (엔진이 계산한 dB)
class EqCurve final : public juce::Component
{
public:
    void setParams (const juce::var& params);
    void paint (juce::Graphics& g) override;

private:
    juce::var params_;
};

class EffectEditor final : public juce::Component
{
public:
    static constexpr int width = 360;

    EffectEditor (MixerPanel& panel, Project& project);
    ~EffectEditor() override;

    void show (ChannelRef channel, int index);
    void refresh();   // 프로젝트가 바뀌었다: 효과가 그대로면 값만, 모양이 바뀌었으면 줄을 다시 만든다
    ChannelRef channel() const noexcept { return channel_; }
    int index() const noexcept { return index_; }
    bool valid() const;

    // 시험용: 매개변수 키(대역은 "bands.0.gainDb")의 슬라이더, 없으면 nullptr
    juce::Slider* slider (const juce::String& key);
    juce::Button* toggle (const juce::String& key);
    juce::ComboBox* combo (const juce::String& key);   // 방식, 사이드체인 키, 기울기
    juce::String rowLabel (const juce::String& key) const;   // 줄의 이름 (곡선이 있으면 "(곡선)" 이 붙는다)

    // 프리셋: 기본 프리셋(엔진의 id), 사용자 프리셋(프리셋 폴더의 Effects/<형식>/<이름>.json). 입히면 편집 하나
    bool applyFactoryPreset (const std::string& id);
    bool applyUserPreset (const juce::String& name);
    bool saveUserPreset (const juce::String& name);
    bool deleteUserPreset (const juce::String& name);
    juce::StringArray userPresets() const;
    void showPresetMenu();
    // MIDI 배우기: 대역이 아닌 숫자 매개변수 줄의 (키, 슬라이더)
    std::vector<std::pair<std::string, juce::Slider*>> learnableSliders();

    std::function<void()> onClose;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    struct Row;

    juce::var effectJson() const;              // 지금 프로젝트의 효과 객체 (사본)
    std::string effectType() const;
    juce::String shapeOf (const juce::var& effect) const;   // 줄 구성을 정하는 것 (형식, 방식, 대역 수, 켠 선택적 매개변수)
    void build();
    void updateValues();
    void layoutRows();
    void addNumberRow (const std::string& key, const mr::EffectParameter& info, const juce::String& label, int band = -1);
    void addChoiceRow (const std::string& key, const juce::String& label, const juce::StringArray& items, int band = -1);
    void addButtonRow (const juce::String& text, std::function<void()> click);
    void addHeadingRow (const juce::String& text);

    // 효과 객체에 값 하나를 쓴 효과 목록 (band >= 0 이면 그 대역의 키)
    juce::Array<juce::var> withValue (const std::string& key, const juce::var& value, int band) const;
    void preview (const std::string& key, const juce::var& value, int band);
    void commit (const std::string& key, const juce::var& value, int band, const juce::String& what);
    // 모듈레이션 효과의 박 주기: 켜면 rateHz 를 지우고, 끄면 첫 템포에서 같은 주기의 rateHz 를 쓴다
    void setRateBeats (bool on, double beats, const juce::String& what);
    void commitEffects (juce::Array<juce::var> effects, const juce::String& what);
    // 딜레이의 박 단위 시간을 켜면 timeMs 를 지우고, 끄면 첫 템포에서 같은 길이의 timeMs 를 쓴다
    void setDelayBeats (bool on, double beats, const juce::String& what);

    MixerPanel& panel_;
    Project& project_;
    ChannelRef channel_;
    int index_ = -1;
    juce::String shape_;

    juce::Label title_;
    juce::ToggleButton bypass_;
    juce::TextButton remove_, close_, preset_;
    juce::File userPresetFolder() const;
    bool applyPresetObject (const juce::var& preset, const juce::String& name);
    EqCurve curve_;
    juce::Viewport viewport_;
    juce::Component content_;
    std::vector<std::unique_ptr<Row>> rows_;
    bool updating_ = false;
};

} // namespace daw
