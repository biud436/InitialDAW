// 음 편집의 순수 함수. 음 목록과 고른 음의 번호(오름차순)를 받아 새 목록과 새 선택을 돌려준다.
// 시작은 0 아래로, 높이는 0..127 밖으로, 세기는 1..127 밖으로 나가지 않는다.
#pragma once

#include "composition/Document.h"

#include <cstdint>
#include <vector>

namespace mr
{

using NoteSelection = std::vector<size_t>;

struct NoteEditResult
{
    std::vector<EditNote> notes;
    NoteSelection selection;
};

// 고른 음 전체를 같은 양만큼 옮긴다. 범위를 넘으면 넘지 않는 데까지만 옮긴다.
NoteEditResult moveNotes (const std::vector<EditNote>& notes, const NoteSelection& selection, double deltaBeats, int deltaPitch);
// 원본은 두고 옮긴 사본을 더한다. 선택은 사본.
NoteEditResult copyNotes (const std::vector<EditNote>& notes, const NoteSelection& selection, double deltaBeats, int deltaPitch);
// 길이에 delta 를 더한다 (minLength 아래로 내려가지 않는다)
NoteEditResult resizeNotes (const std::vector<EditNote>& notes, const NoteSelection& selection, double deltaBeats, double minLength);
NoteEditResult setVelocity (const std::vector<EditNote>& notes, const NoteSelection& selection, int velocity);
// 고른 음의 아티큘레이션 (빈 문자열은 트랙의 기본값)
NoteEditResult setArticulation (const std::vector<EditNote>& notes, const NoteSelection& selection, const std::string& articulation);
NoteEditResult offsetVelocity (const std::vector<EditNote>& notes, const NoteSelection& selection, int delta);
// 시작을 가장 가까운 격자에 맞춘다 (길이는 그대로). 선택이 비면 모든 음.
NoteEditResult quantizeNotes (const std::vector<EditNote>& notes, const NoteSelection& selection, double grid);
NoteEditResult deleteNotes (const std::vector<EditNote>& notes, const NoteSelection& selection);

// ---- 음표 다듬기 (선택이 비면 모든 음) ----

struct QuantizeOptions
{
    double grid = 0.25;
    double strength = 1.0;   // 0..1: 격자까지의 거리 가운데 끌어오는 몫
    double swing = 0.0;      // 0..1: 둘째 격자 칸을 늦춘다 (1 은 셋잇단의 자리, grid/3)
    bool ends = false;       // 끝도 격자에 (길이는 격자 하나보다 짧아지지 않는다)
};
NoteEditResult quantizeNotes (const std::vector<EditNote>& notes, const NoteSelection& selection, const QuantizeOptions& options);
// 벨로시티를 고른 음의 평균을 가운데로 factor 배 (1 위는 넓히고, 아래는 좁힌다)
NoteEditResult scaleVelocities (const std::vector<EditNote>& notes, const NoteSelection& selection, double factor);
// 시간 차례로 from 에서 to 까지 고르게 (같은 시작의 음은 같은 값)
NoteEditResult rampVelocities (const std::vector<EditNote>& notes, const NoteSelection& selection, int from, int to);
// 결정적인 흔들기: 벨로시티 ±velocity, 시작 ±beats (seed 가 같으면 같은 결과)
NoteEditResult humanizeNotes (const std::vector<EditNote>& notes, const NoteSelection& selection, int velocity, double beats, uint64_t seed);
// 다음 음(더 늦게 시작하는 고른 음)의 시작까지 길이를 맞춘다. 마지막 시작의 음들은 그대로
NoteEditResult legatoNotes (const std::vector<EditNote>& notes, const NoteSelection& selection);
NoteEditResult setLengths (const std::vector<EditNote>& notes, const NoteSelection& selection, double length);
// 음계 밖의 음을 가까운 음계 음으로 (같은 거리면 아래로). intervals 는 root 에서 한 옥타브 안의 반음
NoteEditResult snapToScale (const std::vector<EditNote>& notes, const NoteSelection& selection, int root, const std::vector<int>& intervals);
// 역행: 고른 음들이 차지한 시간 [처음, 끝) 안에서 거꾸로
NoteEditResult retrogradeNotes (const std::vector<EditNote>& notes, const NoteSelection& selection);
// 반행: 가장 이른 음(같으면 낮은 음)의 높이를 축으로 뒤집는다
NoteEditResult invertNotes (const std::vector<EditNote>& notes, const NoteSelection& selection);

// ---- 화음 펼치기: 같은 시작의 음이 둘 넘으면 화음 (홑음은 그대로) ----

enum class ArpeggioOrder { up, down, upDown };
// 화음을 그 길이(가장 긴 음) 동안 step 박마다 한 음씩 order 의 차례로 되풀이한다. 음마다의 세기와 아티큘레이션을 지킨다. 선택은 새 음과 홑음
NoteEditResult arpeggiateNotes (const std::vector<EditNote>& notes, const NoteSelection& selection, ArpeggioOrder order, double step);
// 화음을 낮은 음부터(upward 가 거짓이면 높은 음부터) spread 박씩 늦춘다. 끝은 그대로
NoteEditResult strumNotes (const std::vector<EditNote>& notes, const NoteSelection& selection, double spread, bool upward);
// 새 음을 시작 순서의 자리에 넣는다. 선택은 새 음.
NoteEditResult addNotes (const std::vector<EditNote>& notes, const std::vector<Note>& added);
// clip 을 가장 이른 음이 atBeat 에 오도록 옮겨 넣는다
NoteEditResult pasteNotes (const std::vector<EditNote>& notes, const std::vector<Note>& clip, double atBeat);

// 박 [beat0, beat1) 과 높이 [pitchLow, pitchHigh] 에 걸치는 음
NoteSelection notesInRange (const std::vector<EditNote>& notes, double beat0, double beat1, int pitchLow, int pitchHigh);
std::vector<Note> selectedNotes (const std::vector<EditNote>& notes, const NoteSelection& selection);

} // namespace mr
