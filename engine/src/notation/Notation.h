// 악보 모델: 곡의 음을 마디, 보표, 음가(점, 셋잇단), 붙임줄, 쉼표, 조표와 임시표로 옮긴다. MusicXML 쓰기와 앱의 악보 보기가 쓴다.
// 보표마다 성부 하나: 음의 시작과 끝마다 나눠 그 사이에 울리는 음을 화음으로 적고, 이어지는 음은 붙임줄로 잇는다.
// 마디 안의 4분음표 칸마다 16분음표(필요하면 32분음표) 격자와 셋잇단 격자 가운데 음의 자리에 더 맞는 쪽으로 양자화한다.
#pragma once

#include "composition/Composition.h"
#include "notation/Key.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mr
{

constexpr int divisionsPerQuarter = 24;   // 32분음표 3, 셋잇단 16분음표 4

enum class Clef { treble, bass, percussion };

struct NotatedPitch
{
    int pitch = 60;            // 울리는 음높이 (트랙의 transpose 포함)
    Spelling spelling;
    bool tieStart = false;     // 다음 음표로 이어진다
    bool tieStop = false;      // 앞 음표에서 이어졌다
    std::optional<int> accidental;   // 적는 임시표 (-2..2, 0 은 제자리표)
    // 타악기 보표의 자리와 머리 (GM 드럼 건반)
    int displayStep = 0, displayOctave = 5;
    std::string notehead;      // "x", "circle-x", "diamond", 비면 보통
};

struct NotatedEvent
{
    int start = 0;             // 마디 안의 자리 (divisions)
    int duration = 0;          // divisions
    std::string type;          // "whole", "half", "quarter", "eighth", "16th", "32nd"
    int dots = 0;
    bool triplet = false;      // 3:2
    bool tupletStart = false, tupletStop = false;
    bool rest = false;
    bool measureRest = false;  // 마디 전체의 쉼표
    std::vector<NotatedPitch> pitches;   // 낮은 음부터 (쉼표는 비었다)
    int velocity = 0;          // 이 음표에서 시작한 음의 가장 큰 세기 (이어진 음만이면 0)
    std::string lyric;         // 이 음표에서 시작한 음의 가사 ("-" 는 앞 음절을 잇는다)
};

struct TempoMark
{
    int offset = 0;            // 마디 안의 자리 (divisions)
    double bpm = 120.0;        // 4분음표
    bool shown = true;         // 템포 기호로 적는다 (첫 템포와 5% 넘게 바뀐 템포, 마디에 하나). 아니면 재생 템포만
};

struct NotatedMeasure
{
    int number = 1;
    double startBeat = 0.0;
    int length = 96;                              // divisions
    int beatsPerBar = 4, beatUnit = 4;
    bool timeChanged = false;                     // 박자를 적는다 (첫 마디와 바뀐 마디)
    KeySignature key;
    bool keyChanged = false;                      // 조표를 적는다
    std::vector<TempoMark> tempo;
    std::vector<std::vector<NotatedEvent>> staves;   // 보표마다
    std::vector<std::string> markers;             // 이 마디에서 시작하는 마커 이름
};

struct NotatedPart
{
    std::string name;
    std::string instrument;
    size_t track = 0;
    bool percussion = false;
    std::vector<Clef> clefs;                      // 보표마다
    std::vector<NotatedMeasure> measures;
};

struct Score
{
    std::string title;
    KeySignature key;              // 처음 조
    bool keyEstimated = false;     // 곡에 key 가 없어 음으로 추정했다
    std::vector<NotatedPart> parts;
};

struct ScoreOptions
{
    std::vector<size_t> tracks;                                  // 비면 오디오 트랙을 뺀 모든 트랙
    std::function<bool (const TrackSpec&)> percussion;            // 건반 이름이 있는 악기 (타악기 보표)
    std::function<bool (const TrackSpec&)> grandStaff;            // 높은음자리와 낮은음자리 두 보표 (건반 악기). 없으면 이름으로
    std::optional<double> endBeat;                               // 비면 고른 트랙의 마지막 음 끝
};

Score buildScore (const Composition& composition, const ScoreOptions& options = {});

// 조: 곡의 key 와 keyChanges, 없으면 고른 트랙의 음 길이로 추정한 조
KeySignature scoreKey (const Composition& composition, const std::vector<size_t>& tracks, bool* estimated = nullptr);
bool isGrandStaffInstrument (const std::string& instrument);   // grand-piano, piano, harp, celeste, synth.piano, synth.ep ...

} // namespace mr
