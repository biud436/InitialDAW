// 렌더링 결과를 잰다. RMS 는 모든 채널의 평균 전력(dBFS, 최대 진폭 사인파 = -3.01)이고,
// LUFS 는 ITU-R BS.1770-4 통합 라우드니스(K 가중, 400ms 블록, 절대 -70 과 상대 -10 게이트)다.
#pragma once

#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>
#include <optional>

namespace mr
{

struct AudioStats
{
    double sampleRate = 0.0;
    int channels = 0;
    int64_t samples = 0;
    double durationSeconds = 0.0;
    double peakDb = -1.0e9;          // 소리가 없으면 아주 작은 값 (JSON 에서는 null)
    double truePeakDb = -1.0e9;      // 4배 오버샘플 (샘플 사이의 피크)
    double rmsDb = -1.0e9;
    std::optional<double> lufs;      // 400ms 보다 짧거나 무음이면 없다
    bool clipping = false;
    int64_t clippedSamples = 0;
};

AudioStats analyseAudio (const juce::AudioBuffer<float>& buffer, int numSamples, double sampleRate, float clipThreshold = 0.99997f);
std::optional<double> integratedLoudness (const juce::AudioBuffer<float>& buffer, int numSamples, double sampleRate);

double gainToDb (double gain);   // 0 이면 -1e9

// 4배 오버샘플(창 씌운 sinc, 위상마다 12탭)로 샘플 사이의 피크까지 잰다
// 4배 보간의 세 위상 (샘플 사이 1/4, 2/4, 3/4). 위상마다 12탭, 탭 j 는 기준 샘플에서 j-5 번째 (j = 0..11)
const std::vector<std::vector<double>>& truePeakInterpolator();
double truePeak (const juce::AudioBuffer<float>& buffer, int numSamples);

} // namespace mr
