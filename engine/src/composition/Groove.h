// 그루브: 격자 칸마다의 시간 어긋남(박)과 세기 배수를 한 주기(칸 수 x 격자) 동안 적은 것. 곡의 0 박에서 주기가 시작한다.
// 음표나 오디오의 어택에서 뽑고, 음표(NoteEdits)와 오디오 클립의 워프 마커(quantizedWarp)에 입힌다.
#pragma once

#include "composition/NoteEdits.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <vector>

namespace mr
{

struct GrooveStep
{
    double offset = 0.0;     // 격자 자리에서 박 (+ 는 늦게)
    double velocity = 1.0;   // 세기 배수
    bool operator== (const GrooveStep& o) const { return offset == o.offset && velocity == o.velocity; }
};

struct Groove
{
    std::string id;       // 기본 그루브의 id (사용자 그루브는 비어 있다)
    std::string name;
    double grid = 0.25;
    std::vector<GrooveStep> steps;
    double lengthBeats() const noexcept { return grid * (double) steps.size(); }
};

// 기본 그루브: 8분음표와 16분음표 스윙 (짝의 둘째 음이 박의 몇 % 에 오는가), 느긋하게, 서두르게
const std::vector<Groove>& factoryGrooves();
const Groove* findFactoryGroove (const std::string& id);

// { "name", "grid", "steps": [ { "offset", "velocity" } ] }. 잘못되면 nullopt 와 error
std::optional<Groove> grooveFromJson (const juce::var& json, std::string& error);
juce::var grooveJson (const Groove& groove);

// 박의 자리 그루브: 가까운 격자 자리 + 그 칸의 어긋남
double grooveTarget (const Groove& groove, double beat);
// 그 박이 가까운 칸의 세기 배수
double grooveVelocity (const Groove& groove, double beat);

// 음표에서: [from, to) 안에서 시작하는 음을 가까운 격자 자리에 모아 칸마다 어긋남(중앙값)과 세기(평균에 대한 배수)를 잰다. 음이 없는 칸은 0 과 1
Groove grooveFromNotes (const std::vector<Note>& notes, double from, double to, double grid, double lengthBeats);
// 어택에서: 곡의 박과 크기(dB). 세기 배수는 크기 차이의 반 (10^(dB 차이/40), 세기의 제곱이 크기라고 본다)
Groove grooveFromHits (const std::vector<double>& beats, const std::vector<double>& levelsDb, double grid, double lengthBeats);

// 고른 음(비면 모두)의 시작을 그루브 자리로 timing 만큼, 세기를 그 칸의 배수 쪽으로 velocity 만큼 (길이는 그대로)
NoteEditResult applyGroove (const std::vector<EditNote>& notes, const NoteSelection& selection, const Groove& groove, double timing, double velocity);

} // namespace mr
