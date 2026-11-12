// 효과의 기본 프리셋 (형식마다 id 와 효과 객체). 프리셋을 입히면 곡에는 값이 그대로 남는다 (곡이 프리셋을 가리키지 않는다).
#pragma once

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

namespace mr
{

struct EffectPreset
{
    std::string id;
    juce::var effect;   // "type" 을 포함한 효과 객체
};

// 형식의 기본 프리셋 (화면 순서). 없으면 빈 목록
const std::vector<EffectPreset>& effectPresets (const std::string& type);

// effect 에 preset(같은 형식의 효과 객체)의 값을 입힌다: 매개변수는 프리셋의 것으로 바꾸고, 우회, 주석, 압축기의 사이드체인 키,
// 프리셋 뒤에도 곡선을 둘 수 있는 매개변수의 곡선은 남긴다
juce::var withPreset (const juce::var& effect, const juce::var& preset);

} // namespace mr
