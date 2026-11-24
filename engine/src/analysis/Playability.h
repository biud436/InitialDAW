// 연주 가능성 검사: 트랙 하나를 손 하나로 보고 건반 악기에서 칠 수 없는 자리를 찾는다.
// 함께 누르는 음의 벌림과 수, 자리 옮김의 거리와 시간, 음 사이의 시간, 페달 없이 붙드는 음의 벌림, 두 손의 겹침.
#pragma once

#include "composition/Composition.h"

#include <string>
#include <vector>

namespace mr
{

struct PlayabilityOptions
{
    std::vector<size_t> tracks;          // 손마다 트랙 하나. 비면 scorePart 가 있는 트랙, 그것도 없으면 음이 있는 모든 트랙 (오디오 제외)
    int span = 14;                       // 함께 치는 음의 벌림 (반음, 14 는 장9도)
    int rolledSpan = 17;                 // 굴려 치는 화음의 벌림 (17 은 완전11도)
    int maxNotes = 5;                    // 한 손이 함께 치는 음 수
    double minGapSeconds = 0.085;        // 이어 치는 음 사이
    double repeatSeconds = 0.10;         // 같은 건반을 다시 치는 사이
    double shiftSeconds = 0.09;          // 자리를 옮기는 데 드는 시간: shiftSeconds + 반음마다 shiftPerSemitone
    double shiftPerSemitone = 0.010;
    int pedalDown = 40;                  // CC64 가 이 값부터면 페달이 음을 붙든다 (반 페달 포함)
};

struct PlayabilityIssue
{
    size_t track = 0;
    int bar = 1;                         // 1부터
    double beat = 1.0;                   // 마디 안의 박 (1부터)
    double startBeat = 0.0;
    std::string kind;                    // span, notes, shift, fast, repeat, held, overlap
    std::vector<int> pitches;
    double value = 0.0, limit = 0.0;     // 잰 값과 한계 (span, held, notes 는 반음과 음 수, shift, fast, repeat 은 초)
    int semitones = 0;                   // shift 의 옮긴 거리
};

struct PlayabilityHand
{
    size_t track = 0;
    size_t notes = 0;
    int widestSpan = 0;                  // 함께 친 음의 가장 넓은 벌림
    double fastestSeconds = 0.0;         // 이어 친 음 사이의 가장 짧은 시간 (없으면 0)
};

struct PlayabilityReport
{
    std::vector<PlayabilityHand> hands;
    std::vector<PlayabilityIssue> issues;   // 곡의 차례
};

PlayabilityReport checkPlayability (const Composition& composition, const PlayabilityOptions& options = {});

} // namespace mr
