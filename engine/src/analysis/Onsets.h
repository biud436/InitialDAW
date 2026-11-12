// 어택 찾기와 템포 재기. 어택은 로그 크기 스펙트럼의 오르는 양(스펙트럴 플럭스)의 봉우리이고, 시각은 그 자리의
// 1 ms 엔벨로프가 봉우리의 -12 dB 아래로 내려가는(또는 골이 되는) 데까지 거슬러 찾는다 (가파르게 차오르는 머리는 -40 dB 까지). 템포는 플럭스의 자기상관에서
// 박의 배수 자리를 더한 값(빗살)이 가장 큰 BPM 이다.
#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace mr
{

struct OnsetOptions
{
    double minGapSeconds = 0.03;   // 어택 사이의 가장 짧은 틈
    double sensitivity = 1.0;      // 클수록 여린 어택도 잡는다 (0.25~4)
};

// 어택 시각 (신호의 0 번 샘플 기준 초, 오름차순)
std::vector<double> detectOnsets (const float* mono, int numSamples, double sampleRate, const OnsetOptions& options = {});

struct TempoEstimate
{
    double bpm = 120.0;
    double confidence = 0.0;          // 0..1 (빗살 값이 가장 큰 다른 후보보다 얼마나 큰가)
    double firstBeatSeconds = 0.0;    // 박이 놓이는 첫 자리 (신호의 처음에서)
};

// 60~200 BPM. 신호의 길이가 정수 마디(4 박의 배수)에 가까우면 (0.5% 안, 잘라 만든 루프) 그 길이에 맞춘다. 어택이 넷보다 적으면 nullopt
std::optional<TempoEstimate> estimateTempo (const float* mono, int numSamples, double sampleRate, double minBpm = 60.0, double maxBpm = 200.0);

// 박 따라가기: 템포가 바뀌는 연주의 박 자리 (초, 오름차순). 동적 계획으로 박마다 플럭스의 세기와, 앞 박과의 간격이 전체 템포에서
// 벗어난 만큼(로그 비의 제곱 x tightness)의 벌점을 더해 가장 좋은 박 줄을 찾고, 박 둘레(앞 35 ms, 뒤 10 ms)의 어택이 있으면 그 자리로 옮긴다.
// 첫 어택 앞과 마지막 어택 뒤의 박은 뺀다. 어택이 넷보다 적으면 비어 있다
struct BeatTrackOptions
{
    double minBpm = 60.0, maxBpm = 200.0;
    double tightness = 100.0;   // 클수록 템포가 덜 바뀐다
};
std::vector<double> trackBeats (const float* mono, int numSamples, double sampleRate, const BeatTrackOptions& options = {});
std::vector<double> trackBeats (const juce::File& audio, double startSeconds = 0.0, std::optional<double> lengthSeconds = std::nullopt,
                                const BeatTrackOptions& options = {});

// 어택마다 그 뒤 30 ms (다음 어택 앞까지)의 피크 (dBFS)
std::vector<double> onsetLevelsDb (const float* mono, int numSamples, double sampleRate, const std::vector<double>& onsets);

// 파일(그 구간)을 모노로 읽는다. 시각은 파일의 초다
std::vector<double> detectOnsets (const juce::File& audio, double startSeconds, std::optional<double> lengthSeconds, const OnsetOptions& options = {});
std::optional<TempoEstimate> estimateTempo (const juce::File& audio, double startSeconds = 0.0, std::optional<double> lengthSeconds = std::nullopt);
std::vector<double> onsetLevelsDb (const juce::File& audio, const std::vector<double>& onsets);

} // namespace mr
