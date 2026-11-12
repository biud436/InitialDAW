// 효과: eq, compressor, reverb, limiter, gain, stereo, delay, distortion, filter, ducker, chorus, flanger, phaser, tremolo, gate.
// 모두 스테레오이고 결정적이다. 모듈레이션 효과(chorus, flanger, phaser, tremolo)의 LFO 위상은 곡 위치만의 함수다.
// compressor 는 sidechain 으로 다른 트랙의 신호를 검출기에 쓸 수 있다 (킥이 패드를 누르는 펌핑).
// 효과는 블록을 제자리에서 처리하고 자기 지연(샘플)을 알린다. 트랙에서는 그 지연을 트랙 정렬이 받고,
// 버스와 마스터에서는 전체 버퍼를 처리하며 지연만큼 당겨 되돌린다 (processOffline).
// 효과 객체의 "automation": { "<매개변수>": [점, ...] } 은 곡 위치(템포 지도)에 따라 매개변수를 바꾼다 (effects/Automation.h).
#pragma once

#include "composition/Composition.h"
#include "core/Diagnostics.h"
#include "core/Parameter.h"
#include "effects/Automation.h"
#include "mixer/Mixer.h"
#include "timing/TempoMap.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include "dsp/Fft.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mr
{

class Effect
{
public:
    virtual ~Effect() = default;
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void process (juce::AudioBuffer<float>& buffer, int numSamples) = 0;
    virtual int latencySamples() const { return 0; }
    virtual std::string type() const = 0;
    // 사이드체인 키가 되는 트랙 이름 (없으면 빈 문자열)
    virtual std::string sidechainSource() const { return {}; }
    // process 직전에 부른다: 이번 블록의 키 신호(numSamples 만큼). 부르지 않은 블록은 키를 무음으로 본다.
    virtual void setSidechainInput (const float* left, const float* right) { juce::ignoreUnused (left, right); }
    // 바꿔 끼우기 전의 같은 형식 효과에서 처리 상태를 이어받는다 (오디오 스레드, 할당하지 않는다).
    virtual void continueFrom (Effect& previous) { juce::ignoreUnused (previous); }
    // 다음 process 가 내보내는 첫 샘플이 곡의 몇째 샘플인지 (곡선과 박을 따르는 효과가 쓴다). 효과의 지연을 뺀 자리다.
    virtual void setPosition (BlockPosition position) { juce::ignoreUnused (position); }
    // setPosition 의 위치를 쓰는가 (실시간 재생이 위치가 끊긴 자리에서 블록을 나눠 처리한다)
    virtual bool followsPosition() const { return false; }
    // 사이드체인 키를 이 자리의 소리에 맞춘다: 키를 keyDelay, 소리를 audioDelay 샘플 늦춘다 (소리의 지연은 latencySamples 에 든다)
    virtual void setSidechainAlignment (int keyDelay, int audioDelay) { juce::ignoreUnused (keyDelay, audioDelay); }
};

// 효과 사슬. 우회한 효과는 처리하지 않고 그 효과의 지연만큼 늦춰 내보낸다 (사슬의 지연이 바뀌지 않는다).
class EffectChain
{
public:
    void add (std::unique_ptr<Effect> e, bool bypass = false);
    bool empty() const noexcept { return effects_.empty(); }
    size_t size() const noexcept { return effects_.size(); }
    Effect& at (size_t i) { return *effects_[i].effect; }
    bool bypassed (size_t i) const { return effects_[i].bypass; }
    void prepare (double sampleRate, int maxBlockSize);
    // position: 이 블록의 첫 샘플이 사슬 입력에서 곡의 몇째 샘플인지. 효과마다 앞 효과와 자기의 지연을 빼고 넘긴다.
    void process (juce::AudioBuffer<float>& buffer, int numSamples, int64_t position);
    // 실시간: clock 은 사슬 입력의 첫 샘플의 시계. 효과마다 지연만큼 거슬러 timeline 에서 곡 위치를 읽고,
    // 위치가 끊긴 자리(되돌아가기, 멈춤)에서는 위치를 따르는 효과의 블록을 나눈다.
    void process (juce::AudioBuffer<float>& buffer, int numSamples, const SongTimeline& timeline, int64_t clock);
    int latencySamples() const;
    // 끝의 리미터를 떼어 낸다 (목표 LUFS 맞추기가 리미터 앞에서 잰다)
    std::unique_ptr<Effect> takeTrailingLimiter();
    // 이 체인의 효과들이 키로 쓰는 트랙 이름 (중복 없이, 나온 순서대로)
    const std::vector<std::string>& sidechainSources() const noexcept { return sources_; }
    // source 를 키로 쓰는 효과마다 이번 블록의 키를 준다
    void setSidechainInput (const std::string& source, const float* left, const float* right);
    // 앞 사슬에서 같은 형식의 효과(그 형식 안에서 같은 차례)의 상태를 이어받는다. 우회한 효과끼리는 잇지 않는다.
    // 오디오 스레드에서 부른다 (할당하지 않는다).
    void continueFrom (EffectChain& previous);
    // 사이드체인 키를 압축기 자리의 소리와 같은 곡 시각에 맞춘다. inputLatency 는 사슬 입력이 곡보다 늦은 샘플 수
    // (트랙은 악기 지연, 버스와 마스터는 0), keyLatency 는 키 신호가 늦은 샘플 수. 키가 더 늦으면 압축기 앞에서 소리를 늦춘다.
    // 만든 스레드에서 처리 전에 부른다.
    void alignSidechains (int inputLatency, const std::function<int (const std::string&)>& keyLatency);

private:
    struct Slot
    {
        std::unique_ptr<Effect> effect;
        std::string type;
        bool bypass = false;
        std::string sidechain;
        DelayLine delay;   // 우회할 때 효과의 지연만큼
        int offset = 0;    // 앞 효과들의 지연 합 (이 효과가 보는 곡 위치를 사슬 입력에서 뺀다)
    };
    void updateOffsets();
    std::vector<Slot> effects_;
    std::vector<std::string> sources_;
};

// 효과 매개변수 하나의 설명. 검사(validateEffectSpec)와 앱의 매개변수 화면이 같은 표를 쓴다.
using EffectParameter = ParameterInfo;

// 효과 형식 (화면 순서)
const std::vector<std::string>& effectTypes();
// 형식의 매개변수 (화면 순서). 모르는 형식이면 빈 목록. eq 의 bands, compressor 의 sidechain, reverb 와 filter 의 mode, ir 는 따로 다룬다.
const std::vector<EffectParameter>& effectParameters (const std::string& type);
const EffectParameter* findEffectParameter (const std::string& type, const std::string& key);

// eq 효과 객체의 주파수 응답 (dB). 처리와 같은 바이쿼드 계수로 계산한다.
std::vector<double> eqResponseDb (const juce::var& params, double sampleRate, const std::vector<double>& frequenciesHz);

// filter 의 방식 (화면 순서)
const std::vector<std::string>& filterModes();
// tremolo 의 shape (화면 순서): sine, triangle, square
const std::vector<std::string>& tremoloShapes();

// 악곡을 읽을 때 매개변수를 검사한다 (모르는 효과, 범위, 모르는 매개변수, 곡선).
void validateEffectSpec (const EffectSpec& spec, Diagnostics& diagnostics);

// 효과 객체의 곡선 (automation 의 매개변수 이름에서 곡선으로). 검사한 객체에 쓴다.
std::map<std::string, AutomationLane> effectAutomation (const juce::var& params);

// baseDir 은 IR 파일 같은 상대 경로의 기준(악곡 파일의 폴더)이다. tempo 는 곡선과 박을 따르는 효과가 쓴다 (nullptr 이면 120 BPM).
// 실패하면 Failure("invalid-input"|"io").
std::unique_ptr<Effect> createEffect (const EffectSpec& spec, const juce::File& baseDir, double sampleRate, int blockSize,
                                      const TempoMap* tempo = nullptr, bool realtime = false);
// realtime 이면 플러그인 효과를 실시간 모드로 준비한다 (오프라인은 non-realtime)
EffectChain createEffectChain (const std::vector<EffectSpec>& specs, const juce::File& baseDir, double sampleRate, int blockSize,
                               const TempoMap* tempo = nullptr, bool realtime = false);

// 사이드체인 키: 트랙 이름에서 곡 전체 길이의 신호로 (buffer 와 같은 시간축)
using SidechainKeys = std::map<std::string, const juce::AudioBuffer<float>*>;

// 전체 버퍼를 처리하고 체인의 지연만큼 당긴다. 결과는 지연이 없는 처리와 같은 시각에 놓인다. buffer 의 0 번 샘플이 곡의 0 번 샘플이다.
void processOffline (EffectChain& chain, juce::AudioBuffer<float>& buffer, int numSamples, int blockSize,
                     const SidechainKeys* keys = nullptr);

// filter 효과 객체의 주파수 응답 (dB, 곡선은 보지 않고 고정값으로). 처리와 같은 계수로 계산한다.
std::vector<double> filterResponseDb (const juce::var& params, double sampleRate, const std::vector<double>& frequenciesHz);

// ducker 의 한 박 모양: 곡의 샘플에서 내리는 깊이의 몫 (0 은 그대로, 1 은 depthDb 만큼). 처리와 같은 식이다.
double duckerShape (const juce::var& params, const TempoMap& tempo, double sampleRate, int64_t musicSample);

// 홀 IR 을 만든다: 초기 반사, 대역별 감쇠(낮은 대역은 길게, 높은 대역은 damping 만큼 짧게), 쌓이는 확산음.
// 채널마다 씨앗이 다른 잡음이라 좌우가 어긋나고 width 로 폭을 줄인다. 에너지는 1 로 맞춘다.
juce::AudioBuffer<float> generateHallImpulse (double sampleRate, double decaySeconds, double preDelayMs, double damping,
                                              double width, uint64_t seed);

// 균일 분할 FFT 컨볼루션 (오버랩 세이브). 모노 입력 하나를 IR 두 개로 스테레오로 낸다. 지연 = 분할 크기.
class StereoConvolver
{
public:
    void prepare (const juce::AudioBuffer<float>& impulse, int partitionSize);
    void process (const float* input, float* outLeft, float* outRight, int numSamples);
    int latencySamples() const noexcept { return partition_; }
    // 같은 분할 크기의 앞 컨볼버에서 입력 기록과 출력 대기열을 옮긴다 (할당하지 않는다)
    void continueFrom (StereoConvolver& previous);

private:
    void processPartition();

    int partition_ = 0, fftSize_ = 0, partitions_ = 0, bins_ = 0;
    std::unique_ptr<dsp::Fft> fft_;
    float scale_ = 1.0f;
    std::vector<std::vector<float>> irRe_[2], irIm_[2];   // [채널][분할][빈]
    std::vector<std::vector<float>> inRe_, inIm_;          // 주파수 영역 지연선 [분할][빈]
    int head_ = 0;
    std::vector<float> previous_, current_, work_, accRe_, accIm_;
    std::vector<float> outFifo_[2];
    int fifoPos_ = 0;
};

} // namespace mr
