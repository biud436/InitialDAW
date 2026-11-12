// 표준 MIDI 파일(형식 0, 1)을 곡 JSON 트리로 읽는다. 다른 곳에서 친 연주와 스케치를 곡으로 가져올 때 쓴다.
#pragma once

#include "instruments/Registry.h"

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

namespace mr
{

struct MidiImportOptions
{
    std::string title;                             // 비면 첫 트랙의 이름이나 파일 이름
    std::string defaultInstrument = "grand-piano"; // 프로그램 체인지가 없거나 맞는 악기가 없을 때
    const Registry* registry = nullptr;            // 있으면 GM 프로그램에 맞는 악기 가운데 등록된 것만 고른다
};

struct MidiImportReport
{
    int tracks = 0;
    int notes = 0;
    int lyrics = 0;
    int markers = 0;
    int controllerPoints = 0;
    int tempoPoints = 0;
    double lengthBeats = 0.0;
    std::vector<std::string> warnings;
};

// 트랙마다(한 트랙에 채널이 여럿이면 채널마다) 곡의 트랙 하나. 채널 10 은 드럼 세트. 음의 가사(FF 05)는 같은 자리의 음에,
// 마커(FF 06)는 곡의 markers 로, 컨트롤러와 피치 벤드와 채널 애프터터치는 step 곡선으로. 첫 박자만 쓴다 (뒤의 박자 바꾸기는 경고).
// 읽을 수 없으면 Failure ("io" 또는 "midi").
juce::var importMidi (const juce::File& file, const MidiImportOptions& options, MidiImportReport& report);

// GM 프로그램 번호(0..127)에 맞는 악기 이름 (드럼 채널은 drum-kit). 등록된 악기가 아니면 비어 있다
std::string instrumentForProgram (int program, bool drums, const Registry* registry);

} // namespace mr
