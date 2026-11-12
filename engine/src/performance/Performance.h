// 악보의 음표를 연주된 음표로 바꾼다: 스윙, 악센트, 휴머나이즈(시드 고정), 레가토 겹침.
// 휴머나이즈의 난수는 음표의 내용(음높이와 박)과 트랙 이름에서 씨앗을 얻으므로, 다른 음표를 고쳐도
// 고치지 않은 음표의 흔들림은 그대로다.
#pragma once

#include "composition/Composition.h"
#include "instruments/Profile.h"
#include "timing/MeterMap.h"
#include "timing/TempoMap.h"

#include <cstdint>
#include <optional>
#include <string>
#include <map>
#include <vector>

namespace mr
{

struct ResolvedPerformance
{
    double timingHumanizeMs = 0.0;
    double velocityHumanize = 0.0;
    double noteLengthHumanize = 0.0;
    double legatoOverlapMs = 0.0;
    double swing = 0.0;
    double swingGrid = 0.5;
    std::vector<int> accentPattern;
    double accentGrid = 1.0;
    uint64_t humanizeSeed = 0;
};

// 트랙 설정이 곡 설정을 덮는다. seedOverride 는 곡의 humanizeSeed 만 바꾼다 (트랙에 적은 씨앗은 그대로).
ResolvedPerformance resolvePerformance (const PerformanceSettings& composition, const PerformanceSettings& track,
                                        std::optional<uint64_t> seedOverride);

struct PerformedNote
{
    int pitch = 60;
    int velocity = 100;
    double startSec = 0.0;
    double endSec = 0.0;
    int channel = 1;
    std::string articulation;
    bool legato = false;
    size_t sourceIndex = 0;
};

struct PerformanceContext
{
    const TempoMap& tempo;
    double barBeats = 4.0;
    std::string trackName;
    int transpose = 0;
    const InstrumentProfile* profile = nullptr;   // 있으면 아티큘레이션을 풀고 길이, 벨로시티, 레가토를 맞춘다
    std::string trackArticulation;
    const std::map<int, int>* keymap = nullptr;   // 이조한 뒤의 건반 -> 악기의 건반 (악기 정의의 keymap)
    const MeterMap* meters = nullptr;             // 있으면 강세 무늬를 마디마다 (박자가 바뀌는 곡)
};

// 이조와 악기의 건반 바꾸기를 거친 건반 (0..127)
int instrumentKey (int pitch, int transpose, const std::map<int, int>* keymap);

std::vector<PerformedNote> perform (const std::vector<Note>& notes, const ResolvedPerformance& settings,
                                    const PerformanceContext& context);

// 스윙이 옮긴 박 (테스트와 MIDI 내보내기가 쓴다)
double swingBeat (double beat, double swing, double grid);

} // namespace mr
