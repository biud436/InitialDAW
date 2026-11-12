// 퀵 샘플러: 오디오 파일 하나(또는 그 한 구간)를 건반에 펼친 SFZ 악기로 만든다. 음높이를 재서 기준 건반과 센트 보정을 정한다.
#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <vector>

namespace mr
{

struct PitchEstimate
{
    double frequency = 0.0;   // Hz
    double midi = 0.0;        // 소수 (69 = A4)
    double clarity = 0.0;     // 0..1 (YIN 의 1 - 비주기성), 높을수록 음높이가 분명하다
};

// 어택 뒤(앞 50 ms 를 건너뛴다) 2048 샘플 창마다 YIN 으로 재서 분명한 창의 중앙값. 40~2000 Hz. 분명한 창이 없으면 nullopt
std::optional<PitchEstimate> estimatePitch (const juce::File& audio, double startSeconds = 0.0, std::optional<double> lengthSeconds = std::nullopt);

struct SamplerOptions
{
    std::optional<int> rootKey;       // 비면 음높이를 재서 (재지 못하면 C4)
    bool oneShot = false;             // 북처럼: 건반을 떼도 끝까지 울리고 음높이를 따르지 않는다 (모든 건반이 같은 소리)
    double startSeconds = 0.0;        // 파일 안의 구간 (클립에서 만들 때)
    std::optional<double> lengthSeconds;
    double releaseSeconds = 0.25;
    juce::File relativeWithin;        // 소리와 SFZ 가 이 폴더(곡 폴더) 안이면 상대 경로로 적는다
};

struct SamplerResult
{
    int rootKey = 60;
    double tuneCents = 0.0;           // SFZ 의 tune (재서 고친 값)
    std::optional<PitchEstimate> pitch;
};

// sfzFile 곁의 sample 을 가리키는 SFZ 를 쓴다 (sample 경로는 sfzFile 의 폴더 기준). 실패하면 Failure
SamplerResult writeSamplerSfz (const juce::File& sfzFile, const juce::File& sample, const SamplerOptions& options);

// 어택마다 나눈 SFZ: 조각 i (starts[i] 에서 다음 조각이나 endSeconds 까지, 파일의 초)가 건반 firstKey + i 에서 원래 높이로 울린다.
// 건반을 떼면 5 ms 에 잦아든다. 128 건반을 넘는 조각은 쓰지 않는다. 실패하면 Failure
void writeSliceSfz (const juce::File& sfzFile, const juce::File& sample, const std::vector<double>& starts, double endSeconds, int firstKey = 36,
                    const juce::File& relativeWithin = {});

} // namespace mr
