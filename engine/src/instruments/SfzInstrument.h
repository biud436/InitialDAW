// sfizz 로 SFZ 악기를 연주한다. 아티큘레이션마다 SFZ 파일이 따로 있으면 파일마다 MIDI 채널 하나를 배정하고
// (이벤트 빌더가 음의 채널을 바꾼다) 그 채널의 음을 그 파일로 보낸다. 컨트롤러는 모든 파일에 보낸다.
// 프로필의 dynamics.layers 가 있고 이 트랙에 다이내믹스 곡선이 있으면, 각 음을 레이어 벨로시티마다 함께 울리고
// 다이내믹스 값으로 등전력 크로스페이드한다.
#pragma once

#include "instruments/Instrument.h"

#include <memory>
#include <string>

namespace mr
{

struct InstrumentDef;
struct InstrumentContext;

std::unique_ptr<Instrument> makeSfzInstrument (const InstrumentDef& def, const InstrumentContext& context, std::string& error);

// 이벤트 빌더가 쓰는 채널 배정: 파일이 있는 아티큘레이션의 채널(1부터). 없으면 0.
int sfzChannelFor (const InstrumentDef& def, const std::string& articulation);

bool sfzAvailable();

} // namespace mr
