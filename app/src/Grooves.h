// 그루브 목록: 엔진의 기본 그루브와 앱 데이터 폴더의 내 그루브 (InitialDAW/Grooves/<이름>.json)
#pragma once

#include "composition/Groove.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace daw
{

// 기본 그루브 다음에 폴더의 그루브 (이름 차례). 폴더가 비어 있으면 기본 그루브만
std::vector<mr::Groove> availableGrooves (const juce::File& folder);
// 이름으로 저장한다 (같은 이름은 덮어쓴다)
bool saveGroove (const juce::File& folder, mr::Groove groove, const juce::String& name, juce::String& error);
// 화면 이름: 기본 그루브는 한국어, 내 그루브는 저장한 이름
juce::String grooveName (const mr::Groove& groove);

} // namespace daw
