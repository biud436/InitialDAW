// 트랙 하나의 신호 사슬: 악기, 연주 처리한 이벤트, 표현 단계, 트랙 효과, 페이더, 센드.
// 오프라인 렌더러와 실시간 재생기가 같은 함수로 만들어 두 결과가 갈라지지 않는다.
#pragma once

#include "composition/Composition.h"
#include "core/Diagnostics.h"
#include "effects/Automation.h"
#include "effects/Effects.h"
#include "instruments/Registry.h"
#include "mixer/Mixer.h"
#include "rendering/EventBuilder.h"
#include "rendering/Expression.h"
#include "timing/TempoMap.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mr
{

struct TrackChainOptions
{
    double sampleRate = 48000.0;
    int blockSize = 512;
    uint64_t seed = 0;                     // 악곡 또는 명령행의 씨앗 (트랙 이름과 섞는다)
    std::optional<uint64_t> seedOverride;  // 명령행 --seed (연주 처리의 씨앗을 덮는다)
    juce::File pluginCache;
    bool realtime = false;
};

struct TrackSend
{
    size_t bus = 0;
    float gain = 1.0f;
    bool preFader = false;
    SampleLane level;   // automation.levelDb (dB). 있으면 gain 대신 쓴다.
};

// 센드 하나를 만든다 (곡선이 있으면 tempo 를 복사해 둔다)
TrackSend makeTrackSend (const SendSpec& send, size_t bus, const TempoMap& tempo, double sampleRate);

// source 의 [sourceStart, +n) 를 센드 크기로 destination 의 [destStart, +n) 에 더한다. position 은 source 의 첫 샘플이 곡의 몇째
// 샘플인지 (정렬된 신호). 곡선이 있는 센드만 position 을 쓴다.
void addSend (juce::AudioBuffer<float>& destination, int destStart, const juce::AudioBuffer<float>& source, int sourceStart, int n,
              const TrackSend& send, BlockPosition position);
// 센드의 곡 위치에서의 게인 (곡선이 없으면 고정값)
float sendGainAt (const TrackSend& send, int64_t position);

// 음과 컨트롤러 곡선에서 나오는 부분: 연주 처리한 이벤트와 표현 단계. 편집하면 이것만 새로 만든다.
struct TrackContent
{
    TrackSpec spec;                      // 표현 단계가 이 안의 곡선을 가리킨다 (만든 뒤 옮기지 않는다)
    std::vector<ScheduledEvent> events;
    ExpressionStage expression;
    size_t notes = 0;
    double lastNoteEndSec = 0.0;
};

struct TrackChain
{
    const TrackSpec* spec = nullptr;
    CreatedInstrument created;
    std::unique_ptr<TrackContent> content;
    int instrumentLatency = 0;
    int latency = 0;                 // 악기와 트랙 효과의 지연 합
    float instrumentGain = 1.0f;
    ChannelGains fader;
    FaderLanes faderLanes;           // mixer.automation (비면 fader)
    std::vector<TrackSend> sends;
    EffectChain effects;
    std::vector<Diagnostic> warnings;
};

// 실패하면 Failure 를 던진다 (악기를 만들 수 없음, 트랙이 악기 프로필과 맞지 않음).
TrackChain buildTrackChain (const Composition& composition, const TrackSpec& track, const Registry& registry,
                            const TempoMap& tempo, const std::map<std::string, size_t>& busIndex, const TrackChainOptions& options);

// track 의 음과 곡선으로 내용을 만든다. 트랙이 악기 프로필과 맞지 않으면 Failure 를 던진다. 경고는 warnings 에 더한다.
std::unique_ptr<TrackContent> buildTrackContent (const Composition& composition, const TrackSpec& track, const InstrumentDef& instrument,
                                                 const TempoMap& tempo, const TrackChainOptions& options,
                                                 std::vector<Diagnostic>& warnings);

// 트랙을 처리할 순서: 사이드체인 키가 되는 트랙이 그 키를 쓰는 트랙보다 먼저 온다 (같은 조건이면 원래 순서).
std::vector<size_t> trackProcessOrder (const Composition& composition);

// 트랙 효과의 사이드체인 키를 압축기 자리의 소리에 맞추고 트랙의 지연(latency)을 다시 잰다. 키는 키 트랙의 정렬 전 신호다.
// chains 는 곡의 트랙 차례, nullptr 은 만들지 않은 트랙 (키는 무음, 지연 0).
void alignTrackSidechains (const Composition& composition, const std::vector<TrackChain*>& chains);

// 곡이 끝나는 시각(초, 꼬리 앞): 마지막 음의 끝과 컨트롤러 곡선의 마지막 점 중 늦은 쪽, 또는 render.endBeat.
double songEndSeconds (const Composition& composition, const TempoMap& tempo, double lastNoteEndSec,
                       std::vector<Diagnostic>* warnings = nullptr);

} // namespace mr
