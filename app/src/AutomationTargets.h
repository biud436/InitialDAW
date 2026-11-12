// 곡선을 둘 수 있는 대상: 컨트롤러, 페이더와 밸런스, 센드 크기, 효과 매개변수. 열쇠는 엔진(mr::trackAutomationLanes)과 같다.
// 피아노 롤의 곡선 칸이 대상마다 값의 범위와 눈금, 표기를 여기서 얻는다.
#pragma once

#include "composition/Composition.h"
#include "effects/Effects.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <vector>

namespace daw
{

struct LaneTarget
{
    enum class Scale { linear, logarithmic, fader };   // fader 는 믹서의 dB 눈금
    enum class Group { controller, mixer, send, effect, song };

    std::string key;
    juce::String label;
    Group group = Group::controller;
    double minimum = 0.0, maximum = 127.0, defaultValue = 100.0;
    Scale scale = Scale::linear;
    int controller = -1;                          // 컨트롤러 대상이면 그 번호
    std::optional<mr::EffectParameter> parameter; // 효과 매개변수면 그 설명

    double toNorm (double value) const;   // 화면 높이 0..1
    double fromNorm (double norm) const;
    double round (double value) const;
    juce::String format (double value) const;
};

// 곡 전체의 템포 (열쇠 "tempo", BPM). 눈금은 곡의 가장 느린 템포와 빠른 템포의 40 BPM 바깥까지. 곡선의 모양은 계단과 직선(램프)만
LaneTarget tempoTarget (const std::vector<mr::TempoPoint>& tempo);
// 템포 점과 곡선 사이 (ramp 는 직선, 아니면 계단). 곡선에서 템포로: 같은 박은 뒤의 것, 첫 점은 0 박에
mr::AutomationLane tempoLane (const std::vector<mr::TempoPoint>& tempo);
std::vector<mr::TempoPoint> tempoPoints (const mr::AutomationLane& lane);

// 트랙이 속한 VCA 의 게인 (열쇠 "vca", 곡의 vcas 에 쓴다). 트랙이 VCA 에 속하면 곡선 칸에 늘 있다
LaneTarget vcaTarget (const mr::VcaSpec& vca);
const mr::VcaSpec* vcaOf (const mr::Composition& composition, const mr::TrackSpec& track);

// 트랙이 고를 수 있는 대상 (컨트롤러 몇, 페이더, 밸런스, 센드마다, 효과마다 곡선을 둘 수 있는 매개변수)
std::vector<LaneTarget> automationTargets (const mr::TrackSpec& track);
std::optional<LaneTarget> automationTarget (const mr::TrackSpec& track, const std::string& key);
// 트랙에 이미 곡선이 있는 대상 (차례: 컨트롤러, 믹서, 센드, 효과)
std::vector<LaneTarget> automatedTargets (const mr::TrackSpec& track);

} // namespace daw
