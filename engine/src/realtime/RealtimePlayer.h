// 실시간 재생기. 오프라인 렌더러와 같은 트랙 사슬(buildTrackChain)을 오디오 콜백의 블록마다 돌린다.
// 다른 점: 버스와 마스터 효과는 블록 단위로 처리하고(지연을 되돌리지 않는다), 목표 LUFS 대신 모니터 게인을 쓰고,
// 곡 끝의 페이드아웃을 하지 않는다. 그 밖의 신호(악기, 표현 단계, 트랙 효과, 페이더, 센드)는 오프라인과 같다.
//
// 스레드: 생성은 메시지 스레드(또는 플러그인 악기가 없으면 아무 스레드)에서 한다. process() 는 오디오 스레드에서만
// 부르고, 재생, 정지, 위치 옮기기, 뮤트, 솔로, 모니터 게인은 어느 스레드에서나 원자 변수로 요청한다.
// apply(), updateTrack(), collectRetired(), audition*() 은 한 스레드(보통 메시지 스레드)에서만 부른다. 새 내용과 효과 사슬은 그 스레드에서
// 만들고, 오디오 스레드가 다음 블록 처음에 바꿔 끼운다. 옛것은 collectRetired() 가 지운다.
#pragma once

#include "analysis/LoudnessMeter.h"

#include "composition/Composition.h"
#include "core/Diagnostics.h"
#include "core/Handoff.h"
#include "effects/Effects.h"
#include "mixer/Mixer.h"
#include "rendering/TrackChain.h"
#include "timing/TempoMap.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mr
{

class Registry;
class WorkerPool;

struct PlayerOptions
{
    double sampleRate = 48000.0;
    int blockSize = 512;                    // process() 가 한 번에 처리하는 최대 샘플 수 (넘으면 나눈다)
    std::vector<juce::File> registries;     // 기본 레지스트리 뒤에 읽는다
    bool useDefaultRegistry = true;
    std::optional<uint64_t> seed;
    juce::File pluginCache;
    int threads = -1;                       // 트랙과 버스를 나눠 처리할 일꾼 수 (-1 은 코어 수에 맞춘다, 0 은 오디오 스레드 혼자)
    // 앱: 오디오 클립을 고칠 때 늘이기와 높이 렌더를 뒤 스레드에서 한다 (준비하는 동안은 앞 소리). 오프라인 렌더와 시험은 바로 바꾼다
    bool prepareClipsInBackground = false;
};

struct StereoPeak
{
    float left = 0.0f;
    float right = 0.0f;
};

struct PlayerTrackInfo
{
    std::string name;
    std::string instrument;    // 악곡이 부른 이름
    std::string resolved;      // 실제로 쓴 레지스트리 이름
    std::string description;   // sfz:violins 등
    size_t notes = 0;
    int latencySamples = 0;
};

class RealtimePlayer
{
public:
    // done/total 은 악기를 만든 트랙 수. 실패하면 Failure 를 던진다.
    using Progress = std::function<void (size_t done, size_t total, const std::string& trackName)>;

    RealtimePlayer (const Composition& composition, const PlayerOptions& options, const Progress& progress = {});
    ~RealtimePlayer();

    // ---- 오디오 스레드 ----
    // out 의 채널 1, 2 에 스테레오를 쓴다 (채널이 하나면 왼쪽만, 셋 이상이면 나머지는 비운다).
    void process (float* const* out, int numChannels, int numSamples);
    // 입력과 함께: 입력 모니터링 트랙이 있으면 입력(앞 두 채널, 하나면 양쪽에)을 그 트랙의 악기 자리에 더한다
    void process (float* const* out, int numChannels, int numSamples, const float* const* in, int numInputs);
    // 입력 모니터링: 그 트랙(번호, -1 은 끄기)이 입력 소리를 트랙 사슬(효과, 페이더, 센드)로 낸다. 어느 스레드든
    void setInputMonitor (int track) noexcept { inputMonitor_.store (track); }
    int inputMonitor() const noexcept { return inputMonitor_.load(); }

    // ---- 아무 스레드 ----
    void play();
    void stop();
    // from 에서 재생해 to 샘플에서 정확히 멈춘다 (위치는 to 에 남는다). play, stop, seek 는 이 끝을 지운다.
    void playRange (int64_t from, int64_t to);
    bool playingRange() const noexcept { return stopAt_.load() >= 0 && playing_.load(); }
    bool isPlaying() const noexcept { return playing_.load(); }
    bool clipsPending() const;   // 뒤 스레드가 고친 클립을 아직 준비하고 있다 (prepareClipsInBackground)
    void seek (int64_t sample);                    // 다음 블록에서 옮긴다
    void seekSeconds (double seconds) { seek ((int64_t) std::llround (seconds * sampleRate_)); }
    int64_t position() const noexcept { return position_.load(); }
    // 곡 위치에서 그 소리가 출력에 나오기까지의 샘플 (트랙 정렬과 마스터 효과의 지연). 녹음한 입력을 제자리에 놓을 때 뺀다
    int outputLatencySamples() const noexcept { return outputLatency_.load(); }
    double positionSeconds() const noexcept { return (double) position_.load() / sampleRate_; }
    int64_t length() const noexcept { return length_.load(); }
    double lengthSeconds() const noexcept { return (double) length_.load() / sampleRate_; }
    double sampleRate() const noexcept { return sampleRate_; }
    int blockSize() const noexcept { return blockSize_; }

    size_t trackCount() const noexcept { return tracks_.size(); }
    size_t busCount() const noexcept { return buses_.size(); }
    const PlayerTrackInfo& trackInfo (size_t index) const { return infos_.at (index); }
    void setTrackMute (size_t index, bool mute);
    void setTrackSolo (size_t index, bool solo);
    bool trackMute (size_t index) const;
    bool trackSolo (size_t index) const;
    // 마지막으로 읽은 뒤의 최고값 (읽으면 0 으로 돌린다). 미터용. 트랙과 버스는 페이더 뒤, 마스터는 모니터 게인 뒤.
    StereoPeak takeTrackPeaks (size_t index);
    float takeTrackPeak (size_t index);   // 좌우 중 큰 쪽
    StereoPeak takeBusPeaks (size_t index);
    StereoPeak takeMasterPeaks();
    // 마스터의 라우드니스 (리미터 뒤, 메트로놈 앞). 다시 재기는 requestReset
    LoudnessMeter& loudness() noexcept { return loudness_; }
    // 마스터의 가장 최근 소리 (좌우 평균) count 샘플을 dest 에 (스펙트럼). count 는 16384 까지, 쓴 수를 돌려준다
    int copyMasterSamples (float* dest, int count) const noexcept;
    float takeMasterPeak (int channel);

    void setMonitorGainDb (double db);

    // 트랙의 음과 컨트롤러 곡선을 바꾼다 (악기와 효과는 그대로). 새 내용은 이 스레드에서 만들고, 오디오 스레드가
    // 다음 블록 처음에 바꿔 끼운다. 그때 그 트랙의 울리는 음을 끄고 컨트롤러를 지금 위치의 값으로 다시 보낸다.
    // 트랙이 악기 프로필과 맞지 않으면 Failure 를 던지고 아무것도 바꾸지 않는다.
    void updateTrack (size_t index, const TrackSpec& track);

    // 고친 곡을 재생에 반영한다. 재생기를 만든 곡(또는 마지막으로 반영한 곡)과 비교해 바뀐 부분만 바꿔 끼운다:
    // 음과 곡선(updateTrack 과 같다), 페이더와 밸런스, 뮤트와 솔로, 센드, 트랙과 버스와 마스터의 효과.
    // 페이더와 센드의 변화는 재생 중이면 블록 하나에 걸쳐 잇는다. 효과 사슬은 앞 사슬의 상태를 이어받는다.
    // 트랙의 플러그인 설정(state, program, parameters)이 바뀌었으면 그 플러그인에 넣는다 (지금 값과 같은 것은 넣지 않는다).
    // 플러그인이 있는 곡이면 메시지 스레드에서 부른다.
    // 템포, 박자, 곡 전체의 연주 처리, 트랙과 버스의 목록과 이름, 악기와 악기 정의, 트랙 효과의 지연이나 사이드체인 연결, 목표 LUFS 의
    // 유무가 바뀌었으면 아무것도 바꾸지 않고 false (재생기를 다시 만들어야 한다). 만들다 실패하면 Failure 를 던지고 아무것도 바꾸지 않는다.
    bool apply (const Composition& edited);

    // 오디오 스레드가 내려놓은 옛 내용을 지운다 (apply 와 updateTrack 도 부른다)
    void collectRetired();

    // 음 하나를 바로 들려준다 (피아노 롤의 건반과 음표). 다음 블록 처음에 그 트랙의 악기로 보내며, 멈춰 있어도 울린다.
    // 재생과 같은 이조, 아티큘레이션(SFZ 파일의 채널, CC)과 벨로시티 보정을 쓰고, 소리는 그 트랙의 표현 단계, 효과, 페이더, 센드를 지난다.
    // 키스위치와 프로그램 체인지 아티큘레이션은 보내지 않는다 (재생이 기억하는 악기 상태가 어긋난다). CC 아티큘레이션은 음을 뗄 때
    // 지금 위치의 컨트롤러 값으로 되돌린다. pitch 는 곡의 음높이(이조 전), 뗄 때도 같은 값을 준다.
    void auditionNoteOn (size_t track, int pitch, int velocity, const std::string& articulation = {}, int channel = 1);
    void auditionNoteOff (size_t track, int pitch);
    void auditionAllOff();

    // 컨트롤러를 바로 보낸다 (MIDI 입력의 페달, 모듈레이션: 0..127, kPitchBend 는 0..16383). 위치를 옮기면 곡의 값으로 돌아간다.
    void auditionController (size_t track, int controller, int value, int channel = 1);

    // ---- 반복 재생, 메트로놈, 카운트인 (아무 스레드) ----
    // 반복 구간 (샘플). 켜져 있으면 끝 샘플에 닿을 때 시작 샘플로 돌아간다 (울리는 음을 끄고 컨트롤러를 그 자리의 값으로 다시 보낸다).
    // 끝이 시작보다 크지 않으면 구간을 지운다.
    void setLoopRange (int64_t startSample, int64_t endSample);
    void setLooping (bool on) { looping_.store (on); }
    bool looping() const noexcept { return looping_.load(); }
    bool hasLoopRange() const noexcept { return loopEnd_.load() > loopStart_.load(); }
    int64_t loopStart() const noexcept { return loopStart_.load(); }
    int64_t loopEnd() const noexcept { return loopEnd_.load(); }

    // 메트로놈: 재생 중 박마다 짧은 소리(마디 첫 박은 높은 소리)를 모니터 게인과 리미터 뒤에 더한다. 내보내기에는 없다.
    void setMetronome (bool on) { metronome_.store (on); }
    bool metronome() const noexcept { return metronome_.load(); }
    void setMetronomeGainDb (double db) { metronomeGainDb_.store (std::clamp (db, -40.0, 12.0)); }
    double metronomeGainDb() const noexcept { return metronomeGainDb_.load(); }

    // 지금 위치 앞의 bars 마디 동안 메트로놈만 울린 뒤(카운트인) 재생을 시작한다. 0 이면 play() 와 같다.
    // 카운트인 동안 isPlaying() 은 false 이고 위치는 그대로다. stop() 과 seek() 은 카운트인을 지운다.
    void playWithCountIn (int bars);
    bool countingIn() const noexcept { return countInLeft_.load() > 0; }

    // 트랙의 악기가 플러그인이면 그 인스턴스 (편집기 창, 상태 읽기). 메시지 스레드에서 쓴다. 재생기와 함께 사라진다.
    juce::AudioPluginInstance* pluginInstance (size_t track) const;
    // 효과 칸의 플러그인 (메시지 스레드). owner 는 "track:<번호>", "bus:<번호>", "master". 플러그인 효과가 아니면 nullptr.
    // 사슬을 바꿔 끼우면 새 플러그인이 된다 (설정만 바뀐 편집은 같은 플러그인에 넣는다)
    std::shared_ptr<juce::AudioPluginInstance> effectPlugin (const std::string& owner, size_t effect) const;
    double monitorGainDb() const noexcept { return monitorGainDb_.load(); }

    const std::vector<Diagnostic>& warnings() const noexcept { return warnings_; }
    const Composition& composition() const noexcept { return composition_; }
    const TempoMap& tempo() const noexcept { return tempo_; }

private:
    struct Track;
    struct Bus;
    struct MasterEffects;
    struct Fader;
    struct SendRouting;

    struct LiveEvent
    {
        size_t track = 0;
        bool resync = false;   // 지금 위치의 컨트롤러 값을 다시 보낸다
        juce::MidiMessage message;
    };

    struct HeldNote
    {
        int channel = 1;
        int pitch = 60;
        bool controllers = false;   // 아티큘레이션 CC 를 보냈다
    };

    void processChunk (float* const* out, int numChannels, int offset, int n);
    void renderTrack (size_t index);   // 악기에서 페이더 뒤까지 (합치기는 하지 않는다), 일꾼 스레드에서도 부른다
    void renderBus (size_t index);
    void applySeek (int64_t target);
    void applyFader (juce::AudioBuffer<float>& buffer, int n, Fader& fader, int64_t clock) const;   // clock: 정렬된 신호의 시계 (곡선을 읽는 곡 위치)
    void mixSends (Track& t, int n);
    std::unique_ptr<SendRouting> makeRouting (const std::vector<SendSpec>& sends) const;
    void releaseAll();   // 울리는 음을 모두 끄고 서스테인 페달을 뗀다 (다음 블록 처음에 보낸다)
    void releaseTrack (Track& t);
    void resyncTrack (Track& t, int64_t target);   // 커서를 옮기고 그 앞의 마지막 컨트롤러 값을 다시 보낸다
    void takeUpdates (int64_t pos);                // 오디오 스레드: 바꿀 내용이 있으면 바꿔 끼운다
    void updateLength();
    void pushLive (const LiveEvent& event);
    void takeLiveEvents();                         // 오디오 스레드: 들려줄 음을 트랙의 다음 블록에 넣는다

    struct Click
    {
        int64_t start = 0;   // 클릭 축의 샘플 (재생 중에는 곡 샘플, 카운트인 중에는 카운트인 시작부터)
        bool accent = false;
        bool active = false;
    };
    void addClick (int64_t start, bool accent);
    void scheduleBeatClicks (int64_t pos, int n);     // 오디오 스레드: [pos, pos+n) 의 박에 클릭을 둔다
    void scheduleCountInClicks (int64_t elapsed, int n);
    void renderClicks (int64_t axisPos, int n);     // master_ 에 더한다
    void clearClicks();

    Composition composition_;
    TempoMap tempo_;
    MeterMap meters_;   // 메트로놈과 카운트인의 마디
    double sampleRate_ = 48000.0;
    int blockSize_ = 512;
    std::atomic<int64_t> length_ { 0 };
    int maxLatency_ = 0;
    // 버스 효과의 지연 맞추기: 트랙의 직접 신호는 가장 긴 버스 지연만큼, 버스마다 그 차이만큼 늦춰 마스터에서 만난다
    int maxBusLatency_ = 0;
    DelayLine directAlign_;
    juce::AudioBuffer<float> direct_;                  // 트랙 직접 신호의 합 (마스터 앞)
    struct MasterKey { std::string source; DelayLine align; juce::AudioBuffer<float> signal; };
    std::vector<std::unique_ptr<MasterKey>> masterKeys_;   // 마스터 압축기의 키 (버스 지연만큼 늦춘 트랙 신호)
    void prepareMasterKeys (const EffectChain& chain);
    std::map<std::string, std::vector<std::shared_ptr<juce::AudioPluginInstance>>> effectPlugins_;   // 메시지 스레드만 본다
    void notePlugins (const std::string& owner, EffectChain& chain);
    // 플러그인 효과의 설정을 지금 플러그인에 넣는다 (지금 값과 다른 것만)
    void applyEffectPluginSettings (const std::string& owner, const std::vector<EffectSpec>& effects, std::vector<Diagnostic>& warnings);
    std::atomic<int> outputLatency_ { 0 };
    static int masterLatency (const MasterEffects& m);
    TrackChainOptions chainOptions_;
    std::vector<double> lastNoteEnd_;   // 트랙마다 마지막 음의 끝 (초)

    std::unique_ptr<Registry> registry_;   // 악기 정의(def)를 가리키는 포인터가 살아 있도록 함께 둔다
    std::vector<std::unique_ptr<Track>> tracks_;
    std::vector<std::unique_ptr<Bus>> buses_;
    std::vector<size_t> order_;                       // 사이드체인 키 트랙이 먼저 (합치는 순서)
    std::vector<std::vector<size_t>> levels_;         // 함께 처리해도 되는 트랙 묶음 (키 트랙의 묶음이 먼저)
    std::unique_ptr<WorkerPool> pool_;
    const std::vector<size_t>* currentLevel_ = nullptr;
    int64_t chunkPos_ = 0;
    int64_t chunkClock_ = 0;          // 이 블록의 첫 샘플의 시계
    int chunkN_ = 0;
    int chunkOffset_ = 0;                        // 콜백 안의 이 조각의 자리 (입력을 읽는다)
    const float* const* input_ = nullptr;        // 이 콜백의 입력 (process 안에서만)
    int numInputs_ = 0;
    std::atomic<int> inputMonitor_ { -1 };
    bool chunkPlaying_ = false, chunkAnySolo_ = false;
    std::map<std::string, size_t> trackIndex_;
    std::map<std::string, size_t> busIndex_;
    bool chunkRamp_ = false;          // 이번 블록에서 페이더와 센드의 변화를 잇는다 (앞 블록이 재생 중이었다)
    juce::AudioBuffer<float> silence_;
    std::vector<PlayerTrackInfo> infos_;
    std::vector<Diagnostic> warnings_;

    // 시계(처리한 샘플 수)와 곡 위치의 기록. 지연된 신호의 곡선과 덕킹이 제 곡 위치를 읽는다 (되돌아간 뒤, 멈춘 동안).
    int64_t clock_ = 0;
    SongTimeline timeline_;

    std::unique_ptr<Fader> masterFader_;
    std::unique_ptr<MasterEffects> masterEffects_;   // 목표 LUFS 가 있을 때 끝의 리미터는 모니터 게인 뒤에 둔다
    std::unique_ptr<Handoff<MasterEffects>> nextMasterEffects_;
    juce::AudioBuffer<float> master_;

    juce::AbstractFifo liveFifo_ { 1024 };
    std::vector<LiveEvent> liveEvents_ = std::vector<LiveEvent> (1024);
    std::map<std::pair<size_t, int>, HeldNote> held_;   // 메시지 스레드: 누르고 있는 음 (트랙, 곡의 음높이)

    std::atomic<bool> playing_ { false };
    std::atomic<int64_t> position_ { 0 };
    std::atomic<int64_t> seekRequest_ { -1 };
    std::atomic<int64_t> stopAt_ { -1 };   // playRange 의 끝 (없으면 -1)
    std::atomic<int64_t> loopStart_ { 0 }, loopEnd_ { 0 };
    std::atomic<bool> looping_ { false };
    std::atomic<bool> metronome_ { false };
    std::atomic<double> metronomeGainDb_ { -6.0 };
    std::atomic<int64_t> countInLeft_ { 0 };   // 남은 카운트인 샘플 (0 이면 카운트인이 아니다)
    int64_t countInTotal_ = 0;                 // 아래는 playWithCountIn 이 쓰고 카운트인 동안 오디오 스레드가 읽는다
    std::array<int64_t, 64> countInClickAt_ {};   // 카운트인 시작부터의 샘플
    std::array<bool, 64> countInAccent_ {};
    int countInClicks_ = 0;
    std::array<Click, 8> clicks_ {};
    std::atomic<double> monitorGainDb_ { 0.0 };
    std::atomic<float> masterPeak_[2];
    LoudnessMeter loudness_;
    static constexpr int tapSize = 32768;
    std::unique_ptr<float[]> tap_;               // 마스터 소리의 고리 (좌우 평균)
    std::atomic<uint32_t> tapWrite_ { 0 };
    bool wasPlaying_ = false;
    float monitorGain_ = 1.0f;
};

} // namespace mr
