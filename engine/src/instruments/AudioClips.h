// 오디오 트랙의 클립 재생. 소리는 곡 위치(샘플)만의 함수라 탐색, 반복, 오프라인 렌더가 같은 샘플을 낸다.
// 파일은 메시지 스레드에서 한 번 읽어 렌더 샘플레이트의 스테레오로 바꿔 두고 (같은 파일은 함께 쓴다), 오디오 스레드는 읽기만 한다.
#pragma once

#include "composition/Composition.h"
#include "composition/Groove.h"
#include "core/Handoff.h"
#include "instruments/Instrument.h"
#include "timing/TempoMap.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace mr
{

struct AudioFileInfo
{
    double sampleRate = 0.0;
    int64_t frames = 0;
    int channels = 0;
    double seconds() const noexcept { return sampleRate > 0.0 ? (double) frames / sampleRate : 0.0; }
};

// 머리만 읽는다 (WAV, AIFF, FLAC, Ogg Vorbis, MP3). 읽을 수 없으면 nullopt
std::optional<AudioFileInfo> audioFileInfo (const juce::File& file);
// 오디오 파일로 읽을 수 있는 확장자인가
bool isAudioFileName (const juce::String& name);
juce::File clipFile (const AudioClip& clip, const juce::File& songFolder);
// 클립이 소리를 내는 길이 (초): lengthSeconds, 없으면 파일 끝까지. 파일을 읽을 수 없으면 lengthSeconds 나 0
double clipLengthSeconds (const AudioClip& clip, const juce::File& songFolder);
// 클립이 끝나는 곡 위치 (초). 늘인 클립은 늘인 길이
double clipEndSeconds (const AudioClip& clip, const TempoMap& tempo, const juce::File& songFolder);
// 곡의 초에서 파일의 초로 (offsetSeconds 포함), 그 거꾸로. 늘이지 않은 클립은 곡과 파일이 같은 빠르기다.
// sourceBpm 이면 클립의 시작 박에서 곡의 박을 세어 파일의 박(60/sourceBpm 초)으로, stretch 면 그 배수로 나눈다
// 워프 마커가 있으면 마커 사이를 곡의 박으로 잇는다 (파일의 offsetSeconds 가 곡의 startBeat 에 놓인다)
double clipSourceAt (const AudioClip& clip, const TempoMap& tempo, double songSeconds);
double clipSongSecondsAt (const AudioClip& clip, const TempoMap& tempo, double sourceSeconds);
// 워프의 박과 파일의 초 (마커가 둘보다 적으면 sourceBpm 의 박, 그것도 없으면 초 그대로)
double warpBeatAt (const AudioClip& clip, double sourceSeconds);
double warpSecondsAt (const AudioClip& clip, double beat);

// 워프 마커 편집. 고치지 않은 마커는 곡의 같은 박에 남는다 (클립 머리의 박이 바뀌면 startBeat 를 옮긴다).
// 더하기: 그 파일의 초에 지금 자리 그대로의 마커 (마커가 없으면 클립의 양 끝에도 넣고, 고정 늘이기를 마커로 바꾼다)
AudioClip withWarpMarkerAdded (const AudioClip& clip, const TempoMap& tempo, double lengthSeconds, double sourceSeconds);
// 옮기기: index 의 마커를 클립 안의 박 beat 로 (이웃 마커 사이로 묶는다). 곡의 박은 startBeat + beat - warpBeatAt (offsetSeconds)
AudioClip withWarpMarkerMoved (const AudioClip& clip, size_t index, double beat);
// 지우기: 둘보다 적게 남으면 마커를 모두 지운다
AudioClip withWarpMarkerRemoved (const AudioClip& clip, size_t index);
// 마커가 놓인 곡의 박
double warpMarkerSongBeat (const AudioClip& clip, size_t index);

// 오디오 퀀타이즈: 어택(파일의 초)을 곡의 격자에 끌어오는 워프 마커. 클립의 양 끝은 제자리에 둔다.
// 같은 격자로 가는 어택은 가까운 하나만, 이웃 마커 사이가 4배 넘게 늘거나 줄면 그 어택은 옮기지 않는다
struct AudioQuantizeOptions
{
    double grid = 0.25;       // 박
    double strength = 1.0;    // 0..1
    double swing = 0.0;       // 0..1: 둘째 격자 칸을 늦춘다 (1 은 grid/3)
    std::optional<Groove> groove;   // 있으면 격자와 스윙 대신 그루브의 자리로
};
std::vector<WarpPoint> quantizedWarp (const AudioClip& clip, const TempoMap& tempo, double lengthSeconds, const std::vector<double>& onsets,
                                      const AudioQuantizeOptions& options);
// 늘이기와 음높이 바꾸기를 쓸 수 있는가 (Signalsmith Stretch 와 함께 빌드했는가)
bool clipStretchAvailable();

// 페이드 모양의 게인 (t 는 0..1, 0 이 무음)
float fadeGain (FadeShape shape, double t);

// 파일 전체를 sampleRate 의 스테레오(모노는 두 채널에 같게)로. 같은 파일과 샘플레이트는 한 번만 읽는다 (수정 시각이 바뀌면 다시).
// reversed 면 원래 샘플레이트에서 거꾸로 돌린 뒤 바꾼다
std::shared_ptr<const juce::AudioBuffer<float>> loadClipAudio (const juce::File& file, double sampleRate, std::string& error, bool reversed = false);

// 클립의 파일의 초가 가리키는 소리의 파일: 뒤집지 않은 클립은 그 파일, 뒤집은 클립은 거꾸로 돌린 사본 (임시 폴더, 원래 파일이 바뀌면 다시 쓴다).
// 클립의 소리를 재는 곳(어택, 템포, 받아 적기, 음 덩어리, 샘플러)은 이 파일을 읽는다. 쓰지 못하면 원래 파일
juce::File clipSourceFile (const AudioClip& clip, const juce::File& songFolder);
// 거꾸로 돌린 사본이 놓이는 자리 (쓰지 않는다)
juce::File reversedCopyOf (const juce::File& file);

// 뒤집기를 켜고 끈다: 같은 곡 구간에서 같은 소리 조각을 거꾸로 튼다. 파일의 초(시작, 길이, 워프 마커, 높이 편집, 게인 곡선)를
// 파일 길이 fileSeconds 의 거울 자리로 옮기고, 워프 마커의 박은 클립 안에서 거울 자리로 (곡의 박 구간은 그대로)
AudioClip withClipReversed (const AudioClip& clip, double fileSeconds);
// 게인 곡선의 거울 (위치 x 를 length - x 로, 뛰기 모양은 같은 자리의 점 둘로)
AutomationLane mirroredLane (const AutomationLane& lane, double length);

// 같은 트랙의 두 클립 사이의 크로스페이드 (a 가 먼저 시작한다). 겹쳤으면 겹친 길이, 맞붙었으면 lengthSeconds 만큼 두 클립을 서로 쪽으로 늘여
// (파일에 소리가 있는 만큼, 이음매의 앞뒤 반씩) 겹치게 한다. 같은 파일의 이어지는 소리면 사인 제곱(크기의 합이 1), 아니면 등전력 페이드.
// 겹치지도 맞붙지도 않았거나 겹칠 소리가 1 ms 보다 짧으면 nullopt
std::optional<std::pair<AudioClip, AudioClip>> crossfadedClips (const AudioClip& a, const AudioClip& b, const TempoMap& tempo, const juce::File& songFolder,
                                                                double lengthSeconds = 0.02);

// 클립 소리의 파일의 초 sourceSeconds 에서 가장 가까운 영점 교차 (두 채널 합의 부호가 바뀌는 자리에서 0 에 가까운 샘플, searchSeconds 안).
// 없으면 sourceSeconds 그대로
double nearestZeroCrossing (const AudioClip& clip, const juce::File& songFolder, double sourceSeconds, double searchSeconds = 0.01);

// 파형 그리기용 봉우리: 파일을 bucketFrames 프레임씩 나눠 칸마다 채널을 합친 최솟값과 최댓값 (원래 샘플레이트). 같은 파일은 한 번만 잰다
struct ClipPeaks
{
    double sampleRate = 0.0;
    int bucketFrames = 256;
    std::vector<float> minimum, maximum;
};
std::shared_ptr<const ClipPeaks> clipPeaks (const juce::File& file, bool reversed = false);

class AudioClipPlayer final : public Instrument
{
public:
    // 읽지 못한 파일은 그 클립을 소리 없이 두고 warnings 에 알린다
    AudioClipPlayer (const std::vector<AudioClip>& clips, const TempoMap& tempo, const juce::File& songFolder, double sampleRate,
                     std::vector<std::string>& warnings);
    ~AudioClipPlayer() override;

    void prepare (double sampleRate, int maxBlockSize) override;
    void render (juce::AudioBuffer<float>& buffer, int numSamples, const juce::MidiBuffer& midi, int64_t blockStart) override;
    std::string description() const override { return "audio clips"; }
    // 뒤 스레드에서 준비할 때는 warnings 를 채우지 않는다 (가장 새 목록만 준비해 넘긴다)
    bool setClips (const std::vector<AudioClip>& clips, const TempoMap& tempo, std::vector<std::string>& warnings) override;
    void setTransportPlaying (bool playing) override { playing_.store (playing, std::memory_order_relaxed); }
    void prepareClipsInBackground (bool on) override;
    bool clipsPending() const override;

    struct Clip
    {
        int64_t start = 0;    // 곡의 샘플
        int64_t length = 0;
        int64_t offset = 0;   // 데이터의 샘플
        float gain = 1.0f;
        int fadeIn = 0, fadeOut = 0;
        FadeShape fadeInShape = FadeShape::sine, fadeOutShape = FadeShape::sine;
        std::vector<float> envelope;   // 게인 곡선: 클립 안의 envelopeStep 샘플마다의 게인 (비면 없음), 사이는 곧게 잇는다
        std::shared_ptr<const juce::AudioBuffer<float>> data;
    };
    static constexpr int envelopeStep = 32;
    struct ClipList
    {
        std::vector<Clip> clips;
    };

private:
    std::unique_ptr<ClipList> makeList (const std::vector<AudioClip>& clips, const TempoMap& tempo, std::vector<std::string>& warnings) const;
    void prepareLoop();
    void stopWorker();

    juce::File songFolder_;
    double sampleRate_;
    ClipList* current_ = nullptr;   // 오디오 스레드의 목록
    Handoff<ClipList> handoff_;
    std::atomic<bool> playing_ { true };
    int64_t lastEnd_ = -1;          // 앞 블록의 끝 (위치가 끊기면 짧게 키워 딸깍 소리를 막는다)

    // 뒤 스레드의 준비: 가장 새 요청 하나만 둔다. 뒤 스레드를 켜면 넘기기(handoff_)는 그 스레드만 한다
    struct Request
    {
        std::vector<AudioClip> clips;
        TempoMap tempo;
    };
    std::thread worker_;
    mutable std::mutex requestMutex_;
    std::condition_variable requestReady_;
    std::unique_ptr<Request> request_;
    bool working_ = false;     // 요청을 받아 준비하고 있다
    bool stopping_ = false;
};

} // namespace mr
