// 연주된 음과 컨트롤러 곡선을 샘플 위치의 MIDI 이벤트로 바꾼다. 아티큘레이션은 악기 프로필에 따라
// 키스위치(바뀔 때만), 프로그램 체인지, CC, SFZ 파일 채널로 바뀐다.
// 같은 샘플에서의 순서: 컨트롤러와 프로그램 0, note-off 1, 키스위치 2, note-on 3.
#pragma once

#include "composition/Composition.h"
#include "core/Diagnostics.h"
#include "instruments/Registry.h"
#include "performance/Performance.h"
#include "timing/TempoMap.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace mr
{

struct ScheduledEvent
{
    int64_t sample = 0;
    int priority = 0;
    size_t order = 0;
    juce::MidiMessage message;
};

struct EventBuildContext
{
    const TempoMap& tempo;
    double sampleRate = 48000.0;
    const InstrumentDef& instrument;
    const TrackSpec& track;
    double controlIntervalMs = 5.0;   // 컨트롤러 곡선을 이 간격으로 읽어 값이 바뀔 때 보낸다
};

// 아티큘레이션과 컨트롤러가 악기 프로필과 맞는지 본다 (validate 와 render 가 함께 쓴다).
void checkTrackAgainstInstrument (const TrackSpec& track, const InstrumentDef& instrument, Diagnostics& diagnostics);

// 음의 아티큘레이션 (음 > 트랙 > 프로필 기본값)
std::string resolveArticulation (const std::string& noteArticulation, const TrackSpec& track, const InstrumentProfile& profile);

std::vector<ScheduledEvent> buildTrackEvents (const std::vector<PerformedNote>& notes, const EventBuildContext& context);

} // namespace mr
