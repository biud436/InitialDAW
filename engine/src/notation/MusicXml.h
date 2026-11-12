// 악보 모델을 MusicXML 4.0 (partwise) 로 쓴다. 악보 프로그램(MuseScore, Finale, Sibelius, Dorico)이 읽는다.
#pragma once

#include "notation/Notation.h"

#include <juce_core/juce_core.h>

#include <string>

namespace mr
{

std::string musicXml (const Score& score);
// 실패하면 Failure("io", ...)
void writeMusicXml (const Score& score, const juce::File& file);

} // namespace mr
