// 곡의 뼈대를 바꾸는 편집의 순수 함수: 트랙 목록, 마커, 시간 구간. 모두 곡 JSON 트리(root)를 받아 고친 깊은 사본을 돌려준다.
// 원래 객체의 다른 필드(주석, 가사, 모르는 키)는 그대로 남는다.
#pragma once

#include "composition/Composition.h"

#include <juce_core/juce_core.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mr
{

// ---- 트랙 목록 ----

// base 가 비어 있으면 그대로, 아니면 "base 2", "base 3" 가운데 처음 비어 있는 이름 (끝의 번호는 떼고 센다)
std::string uniqueTrackName (const juce::var& root, const std::string& base);
juce::var newTrackJson (const std::string& name, const std::string& instrument);
// index 자리에 넣는다 (index 가 트랙 수면 맨 뒤). 이름이 겹치면 uniqueTrackName 으로 바꾼다.
juce::var withTrackInserted (const juce::var& root, size_t index, const juce::var& track);
// 지운 트랙을 키로 쓰던 압축기의 sidechain 을 지운다 (지운 수를 clearedSidechains 에)
juce::var withTrackRemoved (const juce::var& root, size_t index, int* clearedSidechains = nullptr);
// 사본을 바로 아래에 넣는다 (이름은 uniqueTrackName)
juce::var withTrackDuplicated (const juce::var& root, size_t index);
// 압축기의 sidechain 이 옛 이름을 가리키면 새 이름으로 바꾼다 (트랙, 버스, 마스터)
juce::var withTrackRenamed (const juce::var& root, size_t index, const std::string& name);
juce::var withTrackMoved (const juce::var& root, size_t from, size_t to);
// 트랙의 color ("#rrggbb", 비면 지운다)
juce::var withTrackColour (const juce::var& root, size_t index, const std::string& colour);

// ---- 마커 (최상위 markers: [{ "beat", "name" }], 박 오름차순) ----

// 새 markers 값 (비면 void). 고친 마커의 새 번호를 index 에 (지우기는 쓰지 않는다)
juce::var withMarkerAdded (const juce::var& root, double beat, const std::string& name, size_t* index = nullptr);
juce::var withMarkerMoved (const juce::var& root, size_t marker, double beat, size_t* index = nullptr);
juce::var withMarkerRenamed (const juce::var& root, size_t marker, const std::string& name);
juce::var withMarkerRemoved (const juce::var& root, size_t marker);

// ---- 시간 구간 ----

// 곡선 하나의 시간 편집 (점의 박은 그대로 두고 구간 밖의 값을 지킨다)
AutomationLane sliceLane (const AutomationLane& lane, double from, double to);   // 구간 처음이 0
// [from, to) 를 content(길이 length, 처음이 0)로 바꾸고 뒤를 length - (to - from) 만큼 민다. content 가 비면 그 자리는 앞뒤 값을 잇는다
AutomationLane replaceLaneSpan (const AutomationLane& lane, double from, double to, const AutomationLane& content, double length);

// 트랙 하나의 구간 내용: 음 객체(시작은 구간 처음부터의 박), 오디오 클립(구간 안의 부분), 곡선. 곡선의 열쇠는 "cc1", "pitchBend" (컨트롤러),
// "effects/<번호>:<종류>/<매개변수>" (효과 매개변수), "sends/<버스>" (센드 크기)
struct ClipTrack
{
    juce::Array<juce::var> notes;
    juce::Array<juce::var> clips;
    std::map<std::string, AutomationLane> lanes;
};

struct ArrangeClip
{
    double lengthBeats = 0.0;
    std::vector<ClipTrack> tracks;   // 고른 트랙의 순서
    bool empty() const noexcept { return tracks.empty() || lengthBeats <= 0.0; }
    size_t noteCount() const noexcept;
};

// 오디오 클립은 구간 경계에서 나눈다 (파일 안의 시작을 옮긴다). 길이가 적히지 않은 클립은 songFolder 로 파일 길이를 읽는다 (비면 끝없이 본다).

// 트랙들의 [from, to) 구간: 그 안에서 시작한 음 (끝은 to 에서 자른다), 클립의 그 부분, 곡선
ArrangeClip copyRange (const juce::var& root, const std::vector<size_t>& tracks, double from, double to, const juce::File& songFolder = {});
// 트랙들의 [from, to) 내용을 지운다: 그 안에서 시작한 음, 클립의 그 부분, 곡선의 점 (앞뒤 값은 지킨다). 앞에서 들어오는 음은 from 에서 자른다
juce::var withRangeCleared (const juce::var& root, const std::vector<size_t>& tracks, double from, double to, const juce::File& songFolder = {});
// clip 을 firstTrack 부터 차례로 at 에 붙인다 (그 자리의 내용은 지우고 넣는다). 트랙이 모자라면 남는 것은 버린다.
// 효과 매개변수 곡선은 같은 번호에 같은 종류의 효과가 있을 때만, 센드는 같은 버스로 가는 센드가 있을 때만
juce::var withClipPasted (const juce::var& root, const ArrangeClip& clip, size_t firstTrack, double at, const juce::File& songFolder = {});
// 곡 전체에 at 부터 length 박을 넣는다: 음, 클립, 곡선(트랙, 버스, 마스터), 템포, 마커, 반복 구간, render.endBeat
juce::var withTimeInserted (const juce::var& root, double at, double length, const juce::File& songFolder = {});
// 곡 전체에서 [from, to) 를 지우고 뒤를 당긴다
juce::var withTimeDeleted (const juce::var& root, double from, double to, const juce::File& songFolder = {});

// ---- 구간 (마커에서 다음 마커까지, 마지막은 곡의 끝까지) ----

struct Section
{
    std::string name;
    double start = 0.0, end = 0.0;   // 박
    size_t marker = 0;               // markers 의 번호
};

// 곡의 끝 박: render.endBeat, 없으면 가장 늦은 음표와 클립의 끝을 마디 줄로 올린 박
double songEndBeat (const juce::var& root, const juce::File& songFolder = {});
// 곡의 구간 (길이가 0 인 것은 뺀다)
std::vector<Section> sectionsOf (const juce::var& root, const juce::File& songFolder = {});
// 원래 곡의 박 구간 spans 를 차례로 0 박부터 이어 붙이고 tailFrom 뒤를 그 뒤에 둔 곡. 음, 클립, 곡선(트랙, 버스, 마스터), 템포,
// 박자 바꾸기, 조 바꾸기, 마커가 구간을 따라가고, 구간마다 그 처음의 박자, 조, 템포를 다시 세운다. 반복 구간과 render.endBeat 는 들어 있던
// 구간을 따라간다 (빠진 구간에 있던 반복 구간은 지운다). 구간 경계에 걸친 음은 끝에서 자른다
juce::var withSpansArranged (const juce::var& root, const std::vector<std::pair<double, double>>& spans, double tailFrom,
                             const juce::File& songFolder = {});
// 구간을 before 번째 구간 앞으로 (sections 수면 맨 뒤로), 바로 뒤에 복제, 지우기. 구간의 경계가 마디 줄이 아니거나 번호가 틀리면 void 와 error
juce::var withSectionMoved (const juce::var& root, size_t section, size_t before, std::string* error = nullptr, const juce::File& songFolder = {});
juce::var withSectionDuplicated (const juce::var& root, size_t section, std::string* error = nullptr, const juce::File& songFolder = {});
juce::var withSectionDeleted (const juce::var& root, size_t section, std::string* error = nullptr, const juce::File& songFolder = {});

// 트랙 템플릿: { "name", "track", "instrument"? }. track 은 소리와 처리(악기, 아티큘레이션, 조옮김, synth, plugin, mixer, effects, sends, output,
// color, performance)만 남긴 사본이다 (음표, 클립, 컨트롤러 곡선, 믹서와 효과와 센드의 곡선, 폴더는 뺀다). 곡 안에서 정의한 악기면 그 정의를
// instrument 에 담고 상대 경로의 file 은 songFolder 기준 절대 경로로 바꾼다
juce::var trackTemplateOf (const juce::var& root, size_t track, const std::string& name, const juce::File& songFolder = {});
// 템플릿으로 index 자리에 새 트랙 (이름은 비어 있는 이름). 곡에 없는 버스로 가는 센드와 출력, 곡에 없는 트랙을 키로 쓰는 사이드체인은 빼고
// dropped 에 센다. 곡에 없는 악기 정의는 곡의 instruments 에 더한다. 템플릿이 아니면 void
juce::var withTrackFromTemplate (const juce::var& root, size_t index, const juce::var& templ, int* dropped = nullptr);
// 기본 트랙 템플릿 (id 와 템플릿): 보컬, 드럼 세트, 그랜드 피아노, 베이스 기타, 신스 리드, 신스 패드, 현악 합주
const std::vector<std::pair<std::string, juce::var>>& factoryTrackTemplates();

// 곡의 템포를 오디오 클립의 연주에 맞춘다: beats (파일의 초, 오름차순)의 첫 박을 마디 머리에 두고 박마다 템포 점을 둬 곡의 박이
// 연주의 박에 놓이게 한다. 첫 박 앞은 첫 간격의 템포다. 클립은 늘이지 않고 (sourceBpm, stretch, warp 를 지운다) 첫 박이 그 마디
// 머리에 오게 옮긴다 (마디는 클립의 지금 자리에 가까운 마디, 곡의 처음보다 앞이면 뒤로). 다른 트랙의 음과 곡선은 박에 남는다.
// 박이 셋보다 적으면 void
juce::var withTempoFollowingClip (const juce::var& root, size_t track, size_t clip, const std::vector<double>& beats, const MeterMap& meters);

// ---- 곡선 열쇠 ----
// 트랙의 모든 곡선. 열쇠는 "cc1", "pitchBend" (컨트롤러), "mixer/gainDb", "mixer/pan" (페이더와 밸런스), "sends/<버스>" (센드 크기),
// "effects/<번호>:<종류>/<매개변수>" (효과 매개변수)
std::map<std::string, AutomationLane> trackAutomationLanes (const juce::var& track);
// 그 열쇠의 곡선을 바꾼 트랙 (깊은 사본, 빈 곡선은 지운다). 열쇠의 센드나 효과가 없으면 그대로
juce::var withTrackAutomation (const juce::var& track, const std::string& key, const AutomationLane& lane);
// 파싱한 트랙에서 그 열쇠의 곡선 (없으면 빈 곡선)
AutomationLane automationLaneOf (const TrackSpec& track, const std::string& key);

// 두 트리의 최상위 속성 가운데 바뀐 것 (편집 하나로 넣을 때)
std::vector<juce::Identifier> changedProperties (const juce::var& before, const juce::var& after);

} // namespace mr
