// 단선율 녹음의 음 덩어리 (받아 적기의 나누기에 음마다의 높이 곡선을 붙인다)와 음마다의 높이 편집 곡선.
// 높이 곡선은 가운데 높이, 느린 흔들림(0.2초 이동 평균 두 번), 빠른 흔들림(비브라토)으로 나눈다.
#pragma once

#include "analysis/PitchTrack.h"
#include "composition/Composition.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace mr
{

struct NoteSegment
{
    double startSeconds = 0.0, endSeconds = 0.0;   // 신호(파일)의 초
    double midi = 0.0;           // 가운데 높이: 잰 높이의 중앙값 (소수, 69 = A4)
    double curveStart = 0.0;     // 높이 곡선의 첫 프레임 (초)
    double hopSeconds = 0.005;
    std::vector<float> curve;    // 프레임마다 잰 높이 (0 은 높이 없음)
    double driftCents = 0.0;     // 느린 흔들림의 표준 편차
    double vibratoCents = 0.0;   // 빠른 흔들림의 표준 편차
    int velocity = 80;
};

std::vector<NoteSegment> noteSegments (const float* mono, int numSamples, double sampleRate, const TranscribeOptions& options = {});
// 파일(그 구간)을 모노로 읽어서. 시각은 파일의 초다. 읽을 수 없으면 Failure
std::vector<NoteSegment> noteSegments (const juce::File& audio, double startSeconds = 0.0, std::optional<double> lengthSeconds = std::nullopt);
// 파일 전체의 음 덩어리. 파일과 고친 시각마다 한 번만 잰다 (여러 스레드에서 불러도 된다). 읽을 수 없으면 빈 목록.
// 한 번 넣은 목록은 바꾸거나 지우지 않는다 (앱이 음을 포인터로 가리킨다)
std::shared_ptr<const std::vector<NoteSegment>> fileNoteSegments (const juce::File& audio);
// 이미 잰 것만 (아직 재지 않았으면 nullptr)
std::shared_ptr<const std::vector<NoteSegment>> cachedFileNoteSegments (const juce::File& audio);
// 파일의 [from, to) 초를 트는 클립의 음: 가운데가 그 안에 드는 음의 [첫 번호, 끝 번호). 클립의 음 번호는 이 안에서 0 부터 센다
std::pair<size_t, size_t> segmentRange (const std::vector<NoteSegment>& all, double from, double to);

// 음 하나의 높이 곡선을 나눈다. 높이 없는 프레임은 이웃 높이를 잇고, 가운데에서 9 반음 넘게 떨어진 프레임은 옥타브를 접는다.
// 음의 처음과 끝에서 가운데와 반음의 반 넘게 다른 프레임(이웃 음에서 넘어오는 자리)은 소리 없음으로 보고 잇는다
struct PitchParts
{
    double centre = 0.0;              // 소리 있는 프레임의 중앙값
    std::vector<double> slow, fast;   // 프레임마다: 느린 흔들림까지의 높이, 나머지 (반음)
    std::vector<bool> voiced;
};
PitchParts splitPitch (const std::vector<float>& curve, double hopSeconds);

// 편집이 그 프레임에서 바꾸는 반음 가운데 펴기와 비브라토의 몫: -flatten x (느린 - 가운데) + (vibrato - 1) x 빠른 (각각 ±3, ±1.5 반음으로 묶는다).
// 소리 없음으로 본 프레임은 0
double pitchEditShape (const PitchParts& parts, size_t frame, const PitchEdit& edit);
// 편집을 입힌 높이 곡선 (화면): 프레임마다, 높이 없는 프레임은 0
std::vector<float> editedPitchCurve (const NoteSegment& segment, const PitchEdit& edit);

// 클립 렌더의 높이 곡선: frames (신호의 0 번 샘플이 파일의 fileOffsetSeconds) 마다 옮길 반음과 잰 높이.
// 편집 밖은 correction (비면 0), 편집 안은 편집. 반음 옮김은 경계 앞뒤 10 ms, 펴기와 비브라토는 음 안의 처음과 끝 20 ms 에 걸쳐 잇는다
std::vector<CorrectionPoint> pitchEditCurve (const std::vector<PitchFrame>& frames, const std::vector<PitchEdit>& edits, double fileOffsetSeconds,
                                             const std::vector<CorrectionPoint>& correction);

} // namespace mr
