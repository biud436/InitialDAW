// 믹서 계산. 팬은 스테레오 밸런스다: 가운데는 두 채널 모두 그대로이고, 한쪽으로 갈수록 반대쪽을
// 등전력 곡선(cos)으로 줄인다. 끝(-1, 1)에서는 반대쪽이 꺼진다.
#pragma once

#include "composition/Composition.h"
#include "effects/Automation.h"
#include "timing/TempoMap.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace mr
{

struct ChannelGains
{
    float left = 1.0f;
    float right = 1.0f;
};

ChannelGains faderGains (const MixerSettings& mixer);
ChannelGains balanceGains (double pan);

// 페이더와 밸런스의 곡선 (곡 위치의 32 샘플 격자). 곡선이 없는 쪽은 고정값
struct FaderLanes
{
    SampleLane gain, pan, vca;   // vca: 채널이 속한 VCA 의 게인 곡선 (게인에 더한다)
    double gainDb = 0.0, panValue = 0.0, vcaDb = 0.0;
    bool empty() const noexcept { return gain.empty() && pan.empty() && vca.empty(); }
};
FaderLanes makeFaderLanes (const MixerSettings& mixer, const TempoMap& tempo, double sampleRate);
// 곡 위치의 채널 게인 (격자 점의 게인 사이를 직선으로)
ChannelGains faderGainsAt (const FaderLanes& lanes, int64_t position);
// buffer 의 [start, start + n) 에 곡 위치 position 부터의 페이더를 곱한다 (held 면 그 자리의 값)
void applyFaderLanes (juce::AudioBuffer<float>& buffer, int start, int n, const FaderLanes& lanes, BlockPosition position);

// 지연선 (지연 보상 정렬용). 블록을 제자리에서 delay 샘플 늦춘다.
class DelayLine
{
public:
    void prepare (int channels, int delaySamples);
    void process (juce::AudioBuffer<float>& buffer, int numSamples);
    int delay() const noexcept { return delay_; }

private:
    int delay_ = 0;
    int position_ = 0;
    std::vector<std::vector<float>> lines_;
};

// 스템 파일 이름: 경로에 못 쓰는 글자는 '_' 로 바꾼다.
std::string stemFileName (const std::string& name);

} // namespace mr
