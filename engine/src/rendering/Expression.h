// 엔진이 처리하는 컨트롤러 (악기 출력 뒤, 페이더 앞): cc7 볼륨과 cc11 익스프레션은 게인((값/127)^2),
// cc10 은 밸런스, 다이내믹스 컨트롤러는 프로필의 게인 범위(dB 선형)와 밝기(2극 저역 통과, 차단 주파수는 로그 보간).
// 값은 32 샘플마다 곡선에서 읽고 그 사이는 선형으로 잇는다.
#pragma once

#include "composition/Composition.h"
#include "instruments/Profile.h"
#include "timing/TempoMap.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace mr
{

class ExpressionStage
{
public:
    void configure (const TrackSpec& track, const InstrumentProfile& profile, const std::string& instrumentType,
                    const TempoMap& tempo, double sampleRate);
    bool active() const noexcept { return ! gainLanes_.empty() || dynamics_ != nullptr || pan_ != nullptr; }

    // musicSample 은 이 블록의 첫 샘플이 곡에서 놓이는 위치다 (악기 지연을 뺀 값). held 이면 블록 내내 그 위치의 값이다 (멈춘 동안).
    void process (juce::AudioBuffer<float>& buffer, int numSamples, int64_t musicSample, bool held = false);

private:
    struct Gains
    {
        float left = 1.0f, right = 1.0f;
        double cutoff = 0.0;
    };
    Gains valuesAt (int64_t sample) const;

    const TempoMap* tempo_ = nullptr;
    double sampleRate_ = 48000.0;
    std::vector<const AutomationLane*> gainLanes_;
    const AutomationLane* dynamics_ = nullptr;
    const AutomationLane* pan_ = nullptr;
    DynamicsSpec spec_;
    bool brightness_ = false;
    Gains last_;
    bool started_ = false;
    double ic1_[2] = { 0.0, 0.0 }, ic2_[2] = { 0.0, 0.0 };
};

} // namespace mr
