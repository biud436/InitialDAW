// 코드와 스케일 넣기 메뉴: 엔진의 화음표와 음계표를 묶음별 하위 메뉴로 보이고, 고르면 넣는다.
// 화음 이름은 한국어(여기의 표), 음계와 그 묶음은 엔진의 영어 이름을 쓴다.
#pragma once

#include "composition/Harmony.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace daw
{

struct HarmonyOptions
{
    enum class ChordLength { grid, beat, twoBeats, bar };
    int inversion = 0;                       // 0 기본형, 1 첫째 자리바꿈, ...
    ChordLength chordLength = ChordLength::bar;
    mr::ScaleDirection direction = mr::ScaleDirection::up;
    int octaves = 1;
};

juce::String chordGroupLabel (const std::string& group);
juce::String chordLabel (const std::string& id);   // 비면 이름이 없는 것
juce::String scaleGroupLabel (const std::string& group);   // 영어
juce::String scaleLabel (const std::string& id);           // 영어 ("Dorian")
juce::StringArray missingHarmonyLabels();          // 표에 있는데 화면 이름이 없는 것 (시험)

// rootPitch 위의 화음 메뉴 (묶음별 하위 메뉴, 자리바꿈과 길이 설정). 설정 항목은 options 를 바꾼다.
juce::PopupMenu chordMenu (int rootPitch, std::shared_ptr<HarmonyOptions> options, std::function<void (const mr::ChordType&)> insert);
// rootPitch 에서 시작하는 음계 메뉴 (묶음별 하위 메뉴, 방향과 옥타브 설정)
juce::PopupMenu scaleMenu (int rootPitch, std::shared_ptr<HarmonyOptions> options, std::function<void (const mr::ScaleType&)> insert);
// 음계를 고르는 메뉴 (묶음별, 설정 항목 없음): 음계에 맞추기
juce::PopupMenu scaleChoiceMenu (int rootPitch, std::function<void (const mr::ScaleType&)> choose);
// 근음(C4~B4)을 먼저 고르는 메뉴 (메뉴 막대용)
juce::PopupMenu rootMenu (std::function<juce::PopupMenu (int rootPitch)> make);

} // namespace daw
