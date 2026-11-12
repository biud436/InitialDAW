// 내장 신스의 패널: 프리셋 이름 표시, 단면(오실레이터, 필터, 앰프 엔벨로프, 비브라토, 음높이, 출력, 울림)마다 손잡이와 그래프.
// 손잡이는 엔진의 매개변수 표(synthParameters)로 만들고, 그래프는 엔진이 계산한다. 값은 곡의 트랙 synth 에 쓴다.
// 끄는 동안은 onPreview 로 재생기에만, 손을 떼면 편집 하나.
#pragma once

#include "Project.h"
#include "instruments/SimpleSynth.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace daw
{

class SynthPanel final : public juce::Component, private Project::Listener
{
public:
    // preset 은 트랙이 연주하는 내장 신스 프리셋, base 는 레지스트리 항목의 값 (트랙 값이 없을 때의 기본값)
    SynthPanel (Project& project, size_t track, std::string preset, mr::SynthValues base);
    ~SynthPanel() override;

    size_t track() const noexcept { return track_; }
    const std::string& preset() const noexcept { return preset_; }
    juce::Slider* knob (const juce::String& key);     // 시험용
    juce::ComboBox* choice (const juce::String& key); // 시험용: 고르는 매개변수 (filterMode)
    juce::Button* toggle (const juce::String& key);   // 시험용: 켜고 끄는 값 (filter, mono)
    juce::Button* filterSwitch() { return toggle ("filter"); }
    juce::Button& resetButton() noexcept { return reset_; }
    bool reset();                                      // 트랙의 synth 를 지운다 (편집 하나)

    std::function<void (const mr::Composition&)> onPreview;
    std::function<void (const juce::String& text, bool error)> onStatus;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    struct Knob;
    struct Choice;
    struct Section
    {
        std::string name;
        juce::Rectangle<int> area, graph;
        std::vector<Knob*> knobs;
    };

    void projectEdited (int track) override;
    void updateValues();
    double valueOf (const std::string& key) const;
    mr::SynthValues currentValues() const;            // 기본값 위에 곡의 값과 끄는 중인 값
    void preview (const std::string& key, double value);
    void commit (const std::string& key, double value, const juce::String& label);
    void layoutSections();
    void paintSection (juce::Graphics& g, const Section& s);
    void paintWaveform (juce::Graphics& g, juce::Rectangle<float> r);
    void paintFilter (juce::Graphics& g, juce::Rectangle<float> r);
    void paintEnvelope (juce::Graphics& g, juce::Rectangle<float> r);
    void paintDecay (juce::Graphics& g, juce::Rectangle<float> r);
    void paintKit (juce::Graphics& g, juce::Rectangle<float> r);
    void paintVelocity (juce::Graphics& g, juce::Rectangle<float> r);

    Project& project_;
    size_t track_;
    std::string preset_, engine_;
    mr::SynthValues base_;
    std::vector<mr::ParameterInfo> params_;
    std::vector<std::unique_ptr<Knob>> knobs_;
    std::vector<std::unique_ptr<Choice>> choices_;
    std::vector<Section> sections_;
    struct Switch
    {
        mr::ParameterInfo info;
        std::unique_ptr<juce::ToggleButton> button;
    };
    std::vector<Switch> switches_;
    juce::TextButton reset_;
    std::map<std::string, double> dragging_;           // 끄는 중인 값 (그래프가 따라간다)
    bool updating_ = false;
    juce::Colour accent_;
};

} // namespace daw
