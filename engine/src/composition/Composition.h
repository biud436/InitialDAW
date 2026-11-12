// Composition JSON 의 모델과 파서. 박(beat)은 언제나 4분음표 하나다 (MIDI 관례).
#pragma once

#include "core/Diagnostics.h"
#include "plugins/PluginSettings.h"
#include "notation/Key.h"
#include "timing/MeterMap.h"

#include <juce_core/juce_core.h>

#include <cstdint>
#include <map>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace mr
{

enum class CurveShape { linear, step, exponential, smooth };

struct AutomationPoint
{
    double beat = 0.0;
    double value = 0.0;
    CurveShape curve = CurveShape::linear;   // 이 점에서 다음 점까지의 모양
    bool operator== (const AutomationPoint& o) const noexcept { return beat == o.beat && value == o.value && curve == o.curve; }
};

struct AutomationLane
{
    std::vector<AutomationPoint> points;   // beat 오름차순

    bool empty() const noexcept { return points.empty(); }
    double valueAt (double beat) const;
    bool operator== (const AutomationLane& o) const { return points == o.points; }
};

// 컨트롤러 번호. 0..127 은 CC, 그 위는 특수 컨트롤러다.
constexpr int kPitchBend = 128;   // 값 -1..1 (벤드 범위는 악기가 정한다)
constexpr int kAftertouch = 129;  // 채널 애프터터치 0..127
std::string controllerName (int id);
std::optional<int> parseControllerName (const std::string& name);
// 곡선 모양의 이름 (linear, step, exponential, smooth)
std::optional<CurveShape> parseCurveShape (const std::string& name);

struct Note
{
    int pitch = 60;
    double startBeat = 0.0;
    double durationBeat = 1.0;
    int velocity = 100;
    int channel = 1;
    std::string articulation;   // 비면 트랙 기본값
    bool humanize = true;
    std::string lyric;          // 이 음에 부르는 음절 (UTF-8), "-" 는 앞 음절을 잇는다. 소리에는 쓰지 않는다
};

// 가사를 이어 부르는 음의 표시
inline bool isLyricExtension (const std::string& lyric) { return lyric == "-"; }

struct PerformanceSettings
{
    std::optional<double> timingHumanizeMs;
    std::optional<double> velocityHumanize;
    std::optional<double> noteLengthHumanize;
    std::optional<double> legatoOverlapMs;
    std::optional<double> swing;
    std::optional<double> swingGrid;
    std::optional<std::vector<int>> accentPattern;
    std::optional<double> accentGrid;
    std::optional<uint64_t> humanizeSeed;
};

struct MixerSettings
{
    double gainDb = 0.0;
    double pan = 0.0;
    bool mute = false;
    bool solo = false;
    AutomationLane gainLane;   // automation.gainDb: 있으면 gainDb 대신 이 곡선 (dB)
    AutomationLane panLane;    // automation.pan: 있으면 pan 대신 (-1..1)
    // 채널이 속한 VCA 에서 푼 값 (곡을 읽을 때 resolveVcas 가 채운다, 채널의 JSON 에는 쓰지 않는다)
    double vcaDb = 0.0;
    bool vcaMute = false;
    AutomationLane vcaLane;    // VCA 의 automation.gainDb (있으면 vcaDb 대신)

    double totalGainDb() const noexcept { return gainDb + vcaDb; }
    bool muted() const noexcept { return mute || vcaMute; }
};

// VCA: 소리를 모으지 않고 속한 채널(트랙, 버스)의 페이더에 게인을 더하고 뮤트를 건다
struct VcaSpec
{
    std::string name;
    double gainDb = 0.0;
    bool mute = false;
    AutomationLane gainLane;
    std::string path;
};

struct EffectSpec
{
    std::string type;
    juce::var params;   // 효과 객체 전체 (type, bypass, comment 포함)
    bool bypass = false;
    std::string path;
};

struct SendSpec
{
    std::string bus;
    double levelDb = 0.0;
    bool preFader = false;
    AutomationLane levelLane;   // automation.levelDb: 있으면 levelDb 대신 이 곡선 (dB)
};

// 채널의 vca 이름에서 VCA 의 값을 MixerSettings 의 vcaDb, vcaMute, vcaLane 에 푼다 (곡을 읽을 때, 앱이 미리 듣기 전에)
struct Composition;
void resolveVcas (Composition& composition);

// 워프 마커: 파일의 초 seconds 가 클립 안의 박 beat 에 놓인다. 박은 차이만 쓴다 (클립의 offsetSeconds 자리가 startBeat 다)
struct WarpPoint
{
    double seconds = 0.0;
    double beat = 0.0;
    bool operator== (const WarpPoint& o) const { return seconds == o.seconds && beat == o.beat; }
};

// 클립의 음높이 교정: 잰 음높이를 가까운 허락된 음(반음, 또는 조나 음계의 음)으로 끌어온다
struct PitchCorrection
{
    double strength = 1.0;   // 0..1: 목표까지의 거리 가운데 옮기는 몫
    double speedMs = 30.0;   // 교정이 따라가는 시간 (0 은 바로, 길수록 비브라토와 꾸밈이 남는다)
    bool formant = true;     // 포먼트를 지킨다 (목소리의 음색)
    std::string key;         // "A minor" (비면 scale 과 root, 둘 다 비면 반음마다)
    std::string scale;       // 음계 id ("dorian")
    std::string root;        // 음계의 근음 ("A")
    std::array<bool, 12> allowed {};   // 허락된 음높이 갈래 (0 = C), 읽을 때 채운다
    bool operator== (const PitchCorrection& o) const
    {
        return strength == o.strength && speedMs == o.speedMs && formant == o.formant && key == o.key && scale == o.scale && root == o.root;
    }
};

// 음 하나의 높이 편집: 파일의 [seconds, +lengthSeconds) 를 semitones 옮기고, 느린 흔들림을 flatten 만큼 펴고, 빠른 흔들림(비브라토)을 vibrato 배로
struct PitchEdit
{
    double seconds = 0.0;
    double lengthSeconds = 0.0;
    double semitones = 0.0;
    double flatten = 0.0;   // 0..1
    double vibrato = 1.0;   // 0..2 (1 은 그대로)
    double endSeconds() const noexcept { return seconds + lengthSeconds; }
    bool operator== (const PitchEdit& o) const
    {
        return seconds == o.seconds && lengthSeconds == o.lengthSeconds && semitones == o.semitones && flatten == o.flatten && vibrato == o.vibrato;
    }
};

// 클립 페이드의 모양 (t 는 0..1): sine 은 사인 제곱 (맞선 두 페이드의 크기 합이 1), linear 는 t, equalPower 는 사인 (맞선 두 페이드의 힘 합이 1),
// exponential 은 -60 dB 에서 0 dB 까지 데시벨로 곧게 (처음은 0)
enum class FadeShape { sine, linear, equalPower, exponential };
std::optional<FadeShape> parseFadeShape (const std::string& name);
const char* fadeShapeName (FadeShape shape);

// 오디오 트랙의 클립: 곡의 박 자리에서 파일의 [offsetSeconds, +lengthSeconds) 를 튼다. 소리는 템포로 늘거나 줄지 않는다
struct AudioClip
{
    std::string file;                       // 곡 파일의 폴더 기준 (절대 경로도 된다)
    double startBeat = 0.0;
    double offsetSeconds = 0.0;
    std::optional<double> lengthSeconds;    // 없으면 파일 끝까지
    double gainDb = 0.0;
    double fadeInSeconds = 0.0;
    double fadeOutSeconds = 0.0;
    FadeShape fadeInShape = FadeShape::sine;
    FadeShape fadeOutShape = FadeShape::sine;
    // 뒤집기: 클립의 소리는 파일 전체를 거꾸로 돌린 것이고, 클립의 모든 파일의 초(offsetSeconds, lengthSeconds, warp, pitchEdits, gainCurve)는
    // 거꾸로 돌린 파일의 초다 (파일 끝에서 센다)
    bool reverse = false;
    // 클립 안의 게인 곡선 (데시벨): 점의 beat 칸이 파일의 초다. 곡선 밖은 끝 점의 값. gainDb 에 더한다
    AutomationLane gainCurve;
    // 늘이기와 음높이 (offsetSeconds 와 lengthSeconds 는 늘 파일의 초다)
    std::optional<double> sourceBpm;        // 파일의 템포: 파일의 한 박이 곡의 한 박에 놓이게 늘인다 (곡의 템포를 따른다)
    double stretch = 1.0;                   // 소리 길이의 배수 (sourceBpm 이 없을 때)
    double pitchSemitones = 0.0;            // 음높이 (반음, 길이는 그대로)
    // 워프 마커 (둘 이상, 초와 박이 함께 오름차순): 마커 사이를 곡의 박에 맞춰 늘인다. 마커 밖은 sourceBpm, 없으면 가장 가까운 마커 사이의 빠르기
    std::vector<WarpPoint> warp;
    // 워프 방식: stretch 는 마커 사이를 늘이고 줄인다 (음높이 그대로), slice 는 마커마다 잘라 그 자리에서 원래 빠르기로 튼다 (타악기의 어택을 지킨다)
    bool warpSlices = false;
    std::optional<PitchCorrection> pitchCorrection;
    std::vector<PitchEdit> pitchEdits;      // 음마다의 높이 편집 (파일의 초, 시작 순서, 겹치지 않는다)
    bool muted = false;                     // 들리지 않는 클립 (겹쳐 녹음한 테이크 가운데 고르지 않은 것)
    std::string path;

    bool stretched() const noexcept
    {
        return sourceBpm.has_value() || stretch != 1.0 || pitchSemitones != 0.0 || ! warp.empty() || pitchCorrection.has_value() || ! pitchEdits.empty();
    }
    bool operator== (const AudioClip& o) const
    {
        return file == o.file && startBeat == o.startBeat && offsetSeconds == o.offsetSeconds && lengthSeconds == o.lengthSeconds
               && gainDb == o.gainDb && fadeInSeconds == o.fadeInSeconds && fadeOutSeconds == o.fadeOutSeconds && fadeInShape == o.fadeInShape
               && fadeOutShape == o.fadeOutShape && reverse == o.reverse && gainCurve == o.gainCurve && sourceBpm == o.sourceBpm
               && stretch == o.stretch && pitchSemitones == o.pitchSemitones && warp == o.warp && warpSlices == o.warpSlices
               && pitchCorrection == o.pitchCorrection && pitchEdits == o.pitchEdits && muted == o.muted;
    }
    bool operator!= (const AudioClip& o) const { return ! (*this == o); }
};

struct TrackSpec
{
    std::string name;
    std::string instrument;
    std::string articulation;
    int transpose = 0;
    std::vector<Note> notes;
    std::vector<AudioClip> clips;   // 악기가 audio 인 트랙 (박 오름차순이 아니어도 된다)
    std::map<int, AutomationLane> automation;
    PerformanceSettings performance;
    MixerSettings mixer;
    bool mixerSpecified = false;
    std::vector<EffectSpec> effects;
    std::vector<SendSpec> sends;
    std::string output;      // 페이더 뒤의 소리를 보낼 버스 (비면 마스터): 서브그룹
    std::string vca;      // 속한 VCA 의 이름 (비면 없음)
    std::string folder;      // 트랙 보기의 폴더 이름 (비면 폴더 밖). 소리에는 쓰지 않는다
    std::string colour;      // 화면의 트랙 색 "#rrggbb" (비면 앱이 순서로 정한다). 소리에는 쓰지 않는다
    PluginSettings plugin;   // 악기가 플러그인이면 레지스트리 설정 뒤에 적용한다
    std::map<std::string, double> synth;   // 악기가 내장 신스면 프리셋 값을 덮는다 (레지스트리의 parameters 뒤에)
    std::string path;
};

struct BusSpec
{
    std::string name;
    std::string vca;   // 속한 VCA 의 이름 (비면 없음)
    MixerSettings mixer;
    std::vector<EffectSpec> effects;
    std::string path;
};

struct MasterSpec
{
    MixerSettings mixer;
    bool mixerSpecified = false;
    std::vector<EffectSpec> effects;
    std::optional<double> targetLufs;
};

struct TempoPoint
{
    double beat = 0.0;
    double bpm = 120.0;
    bool ramp = false;   // 다음 점까지 bpm 을 선형으로 바꾼다
};

struct RenderSpec
{
    std::optional<double> endBeat;
    double tailSeconds = 2.0;
    double fadeOutSeconds = 0.0;
};

// 게임에서 되풀이할 구간 (metadata.loop). 끝 박에서 시작 박으로 돌아간다.
struct LoopSpec
{
    double startBeat = 0.0;
    double endBeat = 0.0;
};

// 구간 이름 (최상위 markers). 소리에는 쓰지 않는다
struct Marker
{
    double beat = 0.0;
    std::string name;
};

// 곡에 붙인 영상 (최상위 video). 소리에는 쓰지 않는다. 영상의 0초가 곡의 offsetSeconds 초에 놓인다 (음수면 영상 앞이 곡 앞으로 나간다)
struct VideoSpec
{
    std::string file;              // 곡 파일의 폴더 기준 (절대 경로도 된다)
    double offsetSeconds = 0.0;
};

struct Composition
{
    std::string title;
    std::optional<LoopSpec> loop;
    std::vector<Marker> markers;   // 박 오름차순
    std::optional<VideoSpec> video;
    std::vector<TempoPoint> tempo;
    int beatsPerBar = 4;               // 처음 박자
    int beatUnit = 4;
    std::vector<MeterChange> meterChanges;   // 곡 중간의 박자 바꾸기 (박 오름차순)
    std::optional<KeySignature> key;         // 처음 조 ("key": "Bb minor"). 없으면 악보가 음으로 추정한다
    std::vector<KeyChange> keyChanges;       // 곡 중간의 조 바꾸기 (마디 줄, 박 오름차순)
    PerformanceSettings performance;
    std::vector<juce::File> registries;
    struct InlineInstruments
    {
        juce::var table;   // 이름에서 정의로 (레지스트리 파일의 instruments 와 같은 모양)
        std::string path;
    };
    std::vector<InlineInstruments> inlineInstruments;   // 곡 안의 악기 정의 (파일 레지스트리 뒤에 읽는다)
    std::vector<TrackSpec> tracks;
    std::vector<BusSpec> buses;
    std::vector<VcaSpec> vcas;
    MasterSpec master;
    RenderSpec render;
    juce::File sourceFile;

    // 처음 박자의 한 마디 길이 (4분음표 박). 박자가 바뀌는 곡은 meters() 로 센다
    double barBeats() const noexcept { return beatsPerBar * 4.0 / beatUnit; }
    MeterMap meters() const { return MeterMap (beatsPerBar, beatUnit, meterChanges); }
};

struct ParseResult
{
    std::optional<Composition> composition;   // 오류가 없을 때만
    std::optional<Composition> partial;       // JSON 이 읽히면 오류가 있어도 (validate 가 악기 이름까지 보도록)
    Diagnostics diagnostics;
};

ParseResult parseComposition (const juce::String& text, const juce::File& sourceFile);

// 그 박의 조 (곡에 적힌 key 와 keyChanges, 없으면 nullopt. 적히지 않은 조를 추정하지 않는다)
std::optional<KeySignature> keyAt (const Composition& composition, double beat);

// 곡의 영상 파일 (video.file 을 곡 파일의 폴더 기준으로, 영상이 없으면 빈 File)
juce::File videoFileOf (const Composition& composition);

// 곡선 하나를 읽는다: 점의 배열 {beat, value, curve}, 박은 오름차순, 값은 [lo, hi]. 컨트롤러, 효과 매개변수, 센드가 같이 쓴다.
AutomationLane readAutomationLane (const juce::var& value, const std::string& path, double lo, double hi, Diagnostics& diagnostics);
ParseResult loadComposition (const juce::File& file);

// juce::JSON 의 오류 문구를 "invalid JSON at line L, column C: ..." 로 바꾼다.
std::string describeJsonError (const juce::String& juceMessage);

// C4 = 60. "F#3", "Bb2", "c-1" 를 받는다.
std::optional<int> parseNoteName (const std::string& name);

} // namespace mr
