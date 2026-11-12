// 믹서 화면의 이름과 값 표기, 페이더 눈금. 효과와 매개변수의 화면 이름은 여기에만 둔다.
#pragma once

#include "effects/Effects.h"

#include <juce_core/juce_core.h>

#include <string>

namespace daw
{

// 페이더와 미터의 눈금: dB 와 위치(0 아래, 1 위). -120 dB 이하는 끝(-∞)이다.
namespace faderlaw
{
constexpr double minDb = -120.0;
constexpr double maxDb = 24.0;
double toPosition (double db);
double toDb (double position);
} // namespace faderlaw

juce::String effectName (const std::string& type);
// 기본 프리셋의 이름 (엔진의 id 에서)
juce::String presetName (const std::string& id);
juce::String effectSlotName (const mr::EffectSpec& effect);   // 플러그인 효과는 플러그인 이름
juce::String parameterLabel (const std::string& type, const std::string& key);
juce::String parameterTooltip (const std::string& type, const std::string& key);
juce::String bandTypeName (const std::string& type);
// 내장 신스 패널: 단면 이름, 키트 이름 (드럼 프리셋)
juce::String synthSectionName (const std::string& section);
juce::String synthKitName (const std::string& preset);
// 고르는 신스 매개변수의 화면 이름 (filterMode 의 lowpass 는 "저역 통과")
juce::String synthChoiceLabel (const std::string& key, const std::string& name);

// 매개변수 값의 화면 표기 (단위 포함)와 글에서 값 읽기
juce::String formatParameter (const mr::EffectParameter& p, double value);
double parseParameter (const mr::EffectParameter& p, const juce::String& text);
// 저장할 값: 단위에 맞는 자리에서 반올림한다 (끌기의 미리 듣기와 저장이 같은 값을 쓴다)
double roundParameter (const mr::EffectParameter& p, double value);

juce::String formatGainDb (double db);      // -3.0, +2.5, -∞
juce::String formatPan (double pan);        // C, L30, R30
juce::String formatPeakDb (float gain);     // 미터 아래 숫자

} // namespace daw
