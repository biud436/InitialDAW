// 단선율의 음높이 따라가기와 멜로디 받아 적기. 음높이는 YIN (차이 함수를 FFT 상관으로 구하고 누적 평균으로 나눈 값의 문턱)으로
// 프레임마다 재고, 받아 적기는 소리 없는 틈, 음높이 바뀜, 다시 치는 어택에서 음을 나눈다.
#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <functional>
#include <optional>
#include <vector>

namespace mr
{

struct PitchFrame
{
    double seconds = 0.0;      // 창의 가운데 (신호의 0 번 샘플 기준)
    double midi = 0.0;         // 소수 (69 = A4), 0 이면 높이 없음
    double clarity = 0.0;      // 1 - YIN 의 비주기성 (1 에 가까울수록 분명하다)
    double levelDb = -120.0;   // 가운데 10 ms 의 RMS
    // 버금 후보: 비주기성의 다른 골(0.5 아래, 고른 주기의 배수는 빼고)의 높이. 두 음이 겹친 자리에서 약한 쪽이 여기 보인다
    std::array<float, 3> others {};
    int otherCount = 0;
};

struct PitchTrackOptions
{
    double minHz = 50.0, maxHz = 2000.0;
    double hopSeconds = 0.005;
    double threshold = 0.15;   // YIN 의 비주기성 문턱
};

// mono[from, to) 를 hop 마다 잰다. 창(40 ms)이 신호 밖으로 나가면 0 으로 본다
std::vector<PitchFrame> trackPitch (const float* mono, int numSamples, double sampleRate, int from, int to, const PitchTrackOptions& options = {});

struct TranscribedNote
{
    double startSeconds = 0.0, endSeconds = 0.0;   // 신호(파일)의 초
    int pitch = 60;
    int velocity = 80;
    double cents = 0.0;   // 적은 음에서 잰 음높이(중앙값)까지
};

struct TranscribeOptions
{
    PitchTrackOptions pitch;
    double minNoteSeconds = 0.06;
    double minClarity = 0.8;
    double rangeDb = 35.0;    // 가장 큰 프레임보다 이만큼 작으면 소리 없음
    double floorDb = -60.0;   // 이보다 작으면 소리 없음
    double backtrackSeconds = 0.15;   // 높이가 바뀐 음의 시작을 버금 후보로 거슬러 찾는 한도
};

// 받아 적기가 쓴 프레임: 옥타브 튐을 고친 높이와 소리 있음 (음 덩어리의 높이 곡선)
struct TranscribeFrames
{
    std::vector<PitchFrame> frames;
    std::vector<double> pitch;
    std::vector<bool> voiced;
};
std::vector<TranscribedNote> transcribeMelody (const float* mono, int numSamples, double sampleRate, const TranscribeOptions& options = {},
                                               TranscribeFrames* detail = nullptr);
// 파일(그 구간)을 모노로 읽어서. 음의 시각은 파일의 초다. 읽을 수 없으면 Failure
std::vector<TranscribedNote> transcribeMelody (const juce::File& audio, double startSeconds = 0.0, std::optional<double> lengthSeconds = std::nullopt,
                                               const TranscribeOptions& options = {});

// 음높이 교정의 곡선: hop 마다 (신호의 초, 반음). 높이가 분명한 프레임은 가까운 허락된 음(앞 목표에서 0.65 반음 안이면 그대로)까지의 거리에 strength 를 곱하고,
// 높이가 없는 프레임은 앞 값을 50 ms 붙들다 0 으로 간다. speedMs 의 한 극 거르개로 따라간다 (0 은 바로)
struct CorrectionPoint
{
    double seconds = 0.0;
    double semitones = 0.0;
    double midi = 0.0;   // 잰 음높이 (0 은 높이 없음)
};
// transpose 는 소리를 옮기는 반음 (잰 높이에 더해 목표를 고른다)
std::vector<CorrectionPoint> pitchCorrectionCurve (const float* mono, int numSamples, double sampleRate, int from, int to,
                                                   const std::array<bool, 12>& allowed, double strength, double speedMs, double transpose = 0.0);
// 이미 잰 프레임에서 (60~1500 Hz 로 잰 trackPitch)
std::vector<CorrectionPoint> pitchCorrectionCurve (const std::vector<PitchFrame>& frames, const std::array<bool, 12>& allowed, double strength,
                                                   double speedMs, double transpose = 0.0);
// 교정과 높이 편집이 쓰는 소리 있음: 분명함 0.8 위, 가장 큰 프레임에서 40 dB 안이고 -60 dBFS 위
bool correctionVoiced (const PitchFrame& frame, double loudestDb);
// 곡선의 그 초의 값 (사이는 선형, 밖은 끝 값)
double correctionAt (const std::vector<CorrectionPoint>& curve, double seconds);
// 그 초에 가까운 프레임의 잰 음높이 (없으면 0)
double measuredPitchAt (const std::vector<CorrectionPoint>& curve, double seconds);

// 받아 적은 음을 곡의 음표 객체로. beatOf 가 파일의 초를 곡의 박으로 바꾼다 (소수 5자리). 연주의 흔들림이 이미 있어 "humanize": false
juce::Array<juce::var> transcribedNotesJson (const std::vector<TranscribedNote>& notes, const std::function<double (double)>& beatOf, int transpose = 0);

} // namespace mr
