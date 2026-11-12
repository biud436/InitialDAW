// 악곡을 표준 MIDI 파일(형식 1, 4분음표당 960틱)로 내보낸다. DAW 에서 열어 고칠 때 쓴다.
#pragma once

#include "composition/Composition.h"

#include <cstdint>
#include <optional>

namespace mr
{

struct MidiExportReport
{
    int tracks = 0;
    int notes = 0;
    int lyrics = 0;   // 가사 메타 이벤트 수
    int markers = 0;  // 마커 메타 이벤트 수
    double lengthBeats = 0.0;
};

// humanized 면 연주 처리(휴머나이즈, 스윙, 레가토)를 거친 시각을 박으로 되돌려 쓴다. 음의 가사는 음 켜짐 자리의 가사 메타 이벤트로, 곡의 마커는 첫 트랙의 마커 메타 이벤트로 쓴다.
MidiExportReport exportMidi (const Composition& composition, const juce::File& output, bool humanized, std::optional<uint64_t> seed);

} // namespace mr
