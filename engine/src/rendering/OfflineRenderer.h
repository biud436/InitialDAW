// 오프라인 렌더러. 트랙마다 악기를 만들고, 연주 처리한 음표를 샘플 위치의 MIDI 로 바꿔 블록 단위로
// 렌더링하고, 합쳐 오디오 파일(WAV, FLAC, Ogg, MP3)로 쓴다. 실시간이 아니므로 블록 크기와 상관없이 결과는 같다.
#pragma once

#include "analysis/Analysis.h"
#include "analysis/MixMeasure.h"
#include "composition/Composition.h"
#include "instruments/Registry.h"
#include "io/AudioFile.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mr
{

struct RenderOptions
{
    double sampleRate = 48000.0;
    int bitDepth = 24;                    // WAV 와 FLAC 의 출력, 스템
    AudioFileType fileType = AudioFileType::wav;
    int quality = -1;                     // Ogg 품질 0~10, MP3 kbps (0 아래면 기본값)
    int blockSize = 512;
    std::optional<uint64_t> seed;
    std::vector<juce::File> registries;   // 명령행 --registry (뒤가 앞을 덮는다)
    bool useDefaultRegistry = true;
    juce::File output;                    // 비면 파일을 쓰지 않는다 (음량만 잴 때)
    std::optional<juce::File> stemsDir;
    std::vector<std::string> stemTracks;  // 비지 않으면 이 트랙들의 스템만 쓴다 (버스와 마스터의 스템은 쓰지 않는다)
    bool stemsPreFader = false;           // 트랙 스템을 페이더와 밸런스 앞(악기와 트랙 효과, 지연 정렬 뒤)에서
    const std::atomic<bool>* cancel = nullptr;   // 참이 되면 블록 사이에서 Failure("cancelled") 를 던진다
    std::atomic<float>* progress = nullptr;      // 0 에서 1 로 (블록 처리가 0.9 까지)
    bool measure = false;                 // 트랙, 버스, 마스터마다 마디의 크기와 대역, 음표 머리의 크기를 잰다 (스템 파일 없이)
    juce::File pluginCache;               // 비면 기본 캐시

    // 곡 전체를 렌더링한 뒤 출력만 자른다 (목표 LUFS 맞춤은 곡 전체 기준). 스템은 늘 곡 전체다.
    bool loop = false;                    // metadata.loop: 처음부터 반복 끝까지, 끝을 반복 시작 앞의 소리로 잇고 반복 자리를 적는다
    std::optional<std::pair<double, double>> range;   // 박 [시작, 끝): 시작부터 끝 뒤 rangeFadeSeconds 까지, 그동안 줄인다
    double rangeFadeSeconds = 2.0;

    // 자른 뒤의 마지막 소리를 받는다 (영상에 입히기). output 이 비어도 부른다. 렌더링하는 스레드에서 부른다
    std::function<void (const juce::AudioBuffer<float>& audio, double sampleRate)> onAudio;
};

struct TrackReport
{
    std::string name;
    std::string instrument;        // 악곡이 부른 이름
    std::string resolved;          // 실제로 쓴 레지스트리 이름
    std::string description;       // synth:piano 등
    size_t notes = 0;
    int latencySamples = 0;        // 악기와 트랙 효과의 지연 합
    bool audible = true;           // 뮤트되었거나 다른 트랙의 솔로에 밀려나면 false (렌더링하지 않는다)
    double peakDb = -1.0e9;
    double rmsDb = -1.0e9;
    std::string stem;
};

struct BusReport
{
    std::string name;
    double peakDb = -1.0e9;
    double rmsDb = -1.0e9;
    std::string stem;
};

struct NormalisationReport
{
    bool applied = false;
    double targetLufs = 0.0;
    double gainDb = 0.0;
    int passes = 0;
    bool limited = false;
};

struct LoopReport
{
    bool applied = false;
    double startBeat = 0.0, endBeat = 0.0;
    int64_t startSample = 0, endSample = 0;
    int crossfadeSamples = 0;
    double seamWindowSeconds = 0.0;
    std::optional<double> seamDb;      // 반복 끝 바로 앞이 반복 시작 바로 앞과 다른 정도 (끝 쪽 크기에 대한 dB, 같으면 -1e9)
    std::optional<double> seamLevelDb; // 그 창의 크기 (RMS dBFS)
};

struct RangeReport
{
    bool applied = false;
    double startBeat = 0.0, endBeat = 0.0;
    double startSeconds = 0.0, endSeconds = 0.0, fadeSeconds = 0.0;
};

struct RenderReport
{
    AudioStats stats;                  // 쓴 오디오 (자른 뒤, 인코딩 전)
    std::string format = "wav";
    int quality = 0;                   // Ogg 품질, MP3 kbps
    LoopReport loop;
    RangeReport range;
    NormalisationReport normalisation;
    std::vector<BusReport> buses;
    std::string masterStem;
    int bitDepth = 24;
    uint64_t seed = 0;
    std::string output;
    std::vector<TrackReport> tracks;
    std::vector<Diagnostic> warnings;
    MeasureReport measure;             // options.measure 일 때
    double renderSeconds = 0.0;
};

// 기본 레지스트리, 악곡의 instruments, 명령행 --registry 순으로 읽는다.
Registry buildRegistry (const Composition& composition, const RenderOptions& options, Diagnostics& diagnostics);

// 연주 처리 전의 음표 끝과 render 설정으로 길이를 어림한다 (validate 가 쓴다).
double estimateDurationSeconds (const Composition& composition);

// 렌더링을 두 단계로 나눈다: 만들기(악기와 트랙 사슬, 플러그인 악기는 메시지 스레드에서)와 돌리기(어느 스레드에서나, 한 번).
// 만들기가 실패하면 Failure 를 던진다.
class OfflineRender
{
public:
    OfflineRender (const Composition& composition, const RenderOptions& options);
    ~OfflineRender();
    OfflineRender (const OfflineRender&) = delete;
    OfflineRender& operator= (const OfflineRender&) = delete;

    RenderReport run();

private:
    struct State;
    std::unique_ptr<State> state_;
};

RenderReport renderComposition (const Composition& composition, const RenderOptions& options);

} // namespace mr
