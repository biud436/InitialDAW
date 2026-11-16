// 녹음의 복원: 클릭 메우기, 전기 험(기본 주파수와 배음) 지우기, 넓은 대역 잡음(쉿 소리) 줄이기. 오프라인 처리 (클립을 준비할 때 한 번).
// 차례는 클릭, 험, 잡음이다 (클릭과 험이 잡음 지문에 섞이지 않게)
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mr::dsp
{

struct RestoreSettings
{
    double noiseDb = 0.0;                  // 잡음을 줄이는 양의 상한 (dB). 0 이면 잡음을 줄이지 않는다
    std::optional<double> profileStart;    // 잡음 지문을 재는 구간 (버퍼의 초). 없으면 가장 조용한 10% 프레임의 칸마다 중앙값
    double profileLength = 0.0;
    double humHz = 0.0;                    // 0 이면 끄기, 50 이나 60 이면 그 근처에서 잰다, -1 이면 50 과 60 가운데 찾는다
    bool declick = false;
};

struct RestoreReport
{
    int clicks = 0;                   // 메운 클릭
    double humHz = 0.0;               // 잰 험의 기본 주파수 (찾지 못했으면 0)
    std::vector<double> humNotches;   // 노치를 건 주파수
    bool denoised = false;
    std::string noiseSkipped;         // 잡음을 줄이지 않은 까닭 (조용한 곳이 없어 잡음 지문을 잴 수 없을 때)
    double noiseFloorDb = -200.0;     // 잡음 지문의 크기 (샘플의 RMS, dBFS)
};

RestoreReport restoreAudio (juce::AudioBuffer<float>& buffer, double sampleRate, const RestoreSettings& settings);

// 클릭: 2차 차분이 둘레의 중앙값보다 크게 튀는 짧은 자리 (시작 샘플, 길이). 2 ms 보다 긴 자리는 어택으로 보고 두지 않는다
struct ClickSpan
{
    int64_t start = 0;
    int length = 0;
};
std::vector<ClickSpan> findClicks (const float* mono, int64_t frames, double sampleRate);
// 클릭 자리를 메운다: 양쪽 2048 샘플이 있으면 예측 오차의 제곱합이 가장 작은 값 (짧은 예측과 음높이 한 주기 앞의 샘플),
// 없으면 앞쪽 예측과 뒤쪽의 거꾸로 예측을 잇는다
void fillGap (float* samples, int64_t frames, const ClickSpan& gap, double sampleRate);

// 험: nominal 이 50 이나 60 이면 그 둘레 ±0.3 Hz, -1 이면 둘 다에서 기본 주파수를 찾고 (둘레보다 10 dB 넘게 솟고, 녹음 내내 고르게 울려야 한다),
// 배음 열 개의 자리 (뚜렷한 배음은 잰 주파수) 를 준다. 찾지 못하면 fundamental 이 0
struct HumEstimate
{
    double fundamental = 0.0;
    std::vector<double> harmonics;
};
HumEstimate measureHum (const float* mono, int64_t frames, double sampleRate, double nominal);

} // namespace mr::dsp
