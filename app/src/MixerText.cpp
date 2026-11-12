#include "MixerText.h"

#include <cmath>
#include <map>

namespace daw
{

namespace faderlaw
{
namespace
{
// (dB, 위치) 점들 사이를 직선으로 잇는다. 0 dB 이 4분의 3 높이, -60 dB 아래는 좁다.
constexpr double points[][2] = { { -120.0, 0.0 }, { -60.0, 0.05 }, { -40.0, 0.17 }, { -30.0, 0.26 }, { -20.0, 0.38 }, { -10.0, 0.55 },
                                 { -5.0, 0.65 }, { 0.0, 0.75 }, { 6.0, 0.86 }, { 12.0, 0.93 }, { 24.0, 1.0 } };
constexpr int count = (int) (sizeof (points) / sizeof (points[0]));
} // namespace

double toPosition (double db)
{
    if (db <= points[0][0])
        return 0.0;
    for (int i = 1; i < count; ++i)
        if (db <= points[i][0])
        {
            const double t = (db - points[i - 1][0]) / (points[i][0] - points[i - 1][0]);
            return points[i - 1][1] + t * (points[i][1] - points[i - 1][1]);
        }
    return 1.0;
}

double toDb (double position)
{
    if (position <= 0.0)
        return minDb;
    for (int i = 1; i < count; ++i)
        if (position <= points[i][1])
        {
            const double t = (position - points[i - 1][1]) / (points[i][1] - points[i - 1][1]);
            return points[i - 1][0] + t * (points[i][0] - points[i - 1][0]);
        }
    return maxDb;
}
} // namespace faderlaw

namespace
{
juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }
} // namespace

juce::String effectSlotName (const mr::EffectSpec& e)
{
    if (e.type != "plugin")
        return effectName (e.type);
    const auto name = e.params.getProperty ("name", "").toString();
    if (name.isNotEmpty())
        return name;
    return juce::File (e.params.getProperty ("plugin", "").toString()).getFileNameWithoutExtension();
}

juce::String effectName (const std::string& type)
{
    static const std::map<std::string, const char*> names {
        { "eq", "EQ" }, { "compressor", "컴프레서" }, { "reverb", "리버브" }, { "delay", "딜레이" }, { "distortion", "디스토션" },
        { "stereo", "스테레오" }, { "gain", "게인" }, { "limiter", "리미터" }, { "filter", "필터" }, { "ducker", "덕킹" },
        { "chorus", "코러스" }, { "flanger", "플랜저" }, { "phaser", "페이저" }, { "tremolo", "트레몰로와 오토팬" }, { "gate", "노이즈 게이트" },
        { "multiband", "멀티밴드 컴프레서" }, { "transient", "트랜지언트 셰이퍼" }, { "deesser", "디에서" },
    };
    const auto it = names.find (type);
    return it != names.end() ? u8 (it->second) : juce::String::fromUTF8 (type.c_str());
}

juce::String presetName (const std::string& id)
{
    static const std::map<std::string, const char*> names {
        { "vocal-polish", "보컬 다듬기" }, { "low-cleanup", "저음 정리" }, { "bright", "밝게" }, { "telephone", "전화 소리" },
        { "vocal-level", "보컬 고르게" }, { "drum-glue", "드럼 묶기 (병렬)" }, { "bass-firm", "베이스 단단하게" }, { "gentle-bus", "부드러운 버스" },
        { "small-room", "작은 방" }, { "plate", "플레이트" }, { "large-hall", "큰 홀" }, { "send-hall", "버스용 홀 (잔향만)" },
        { "slapback", "슬랩백" }, { "dotted-eighth", "점8분음표 메아리" }, { "ping-pong-eighth", "8분음표 핑퐁" },
        { "warm-saturation", "따뜻한 포화" }, { "hard-drive", "거친 드라이브" }, { "mono", "모노" }, { "wide", "넓게" },
        { "master-ceiling", "마스터 -1 dBTP" }, { "lo-fi", "로파이" }, { "rumble-cut", "저역 울림 자르기" },
        { "quarter-pump", "4분음표 펌핑" }, { "eighth-pump", "8분음표 펌핑" }, { "wide-chorus", "넓은 코러스" }, { "doubling", "얕은 더블링" },
        { "jet", "제트" }, { "slow-phaser", "느린 페이저" }, { "sixteenth-tremolo", "16분음표 트레몰로" }, { "slow-autopan", "느린 오토팬" },
        { "drum-gate", "드럼 게이트" },
        { "master-multiband", "마스터 대역별 묶기" }, { "bass-even", "저음 고르게" }, { "punchy-drums", "드럼 어택 살리기" },
        { "tight-drums", "드럼 울림 줄이기" }, { "long-sustain", "울림 길게" }, { "vocal-deesser", "보컬 치찰음" }, { "bright-deesser", "밝은 치찰음" },
    };
    const auto it = names.find (id);
    return it != names.end() ? u8 (it->second) : juce::String::fromUTF8 (id.c_str());
}

juce::String parameterLabel (const std::string& type, const std::string& key)
{
    static const std::map<std::string, const char*> labels {
        { "eq.lowCutHz", "저역 차단" }, { "eq.lowCutSlope", "저역 차단 기울기" }, { "eq.highCutHz", "고역 차단" },
        { "eq.highCutSlope", "고역 차단 기울기" },
        { "compressor.thresholdDb", "스레숄드" }, { "compressor.ratio", "압축비" }, { "compressor.attackMs", "어택" },
        { "compressor.releaseMs", "릴리스" }, { "compressor.kneeDb", "니" }, { "compressor.makeupDb", "메이크업 게인" },
        { "compressor.mix", "병렬 비율" },
        { "reverb.mix", "잔향 비율" }, { "reverb.decaySeconds", "잔향 시간" }, { "reverb.preDelayMs", "프리딜레이" },
        { "reverb.roomSize", "방 크기" }, { "reverb.damping", "고역 감쇠" }, { "reverb.width", "스테레오 폭" },
        { "reverb.lowCutHz", "잔향 저역 차단" }, { "reverb.highCutHz", "잔향 고역 차단" }, { "reverb.dryDb", "원음 레벨" },
        { "reverb.wetDb", "잔향 레벨" }, { "reverb.seed", "잡음 씨앗" },
        { "delay.timeMs", "지연 시간" }, { "delay.timeBeats", "지연 박" }, { "delay.feedback", "피드백" }, { "delay.mix", "지연음 비율" },
        { "delay.pingPong", "핑퐁" },
        { "delay.lowCutHz", "피드백 저역 차단" }, { "delay.highCutHz", "피드백 고역 차단" },
        { "distortion.driveDb", "드라이브" }, { "distortion.toneHz", "톤" }, { "distortion.mix", "왜곡음 비율" },
        { "distortion.outputDb", "출력 게인" },
        { "stereo.width", "스테레오 폭" }, { "stereo.pan", "위치" },
        { "gain.gainDb", "게인" },
        { "limiter.ceilingDb", "실링" }, { "limiter.releaseMs", "릴리스" }, { "limiter.lookaheadMs", "미리 보기" },
        { "limiter.truePeak", "트루 피크" },
        { "filter.cutoffHz", "차단 주파수" }, { "filter.resonance", "공진" }, { "filter.slope", "기울기" }, { "filter.mix", "필터음 비율" },
        { "ducker.rateBeats", "주기" }, { "ducker.depthDb", "깊이" }, { "ducker.attackMs", "어택" }, { "ducker.holdMs", "유지" },
        { "ducker.releaseBeats", "돌아오는 시간" }, { "ducker.curve", "돌아오는 곡선" }, { "ducker.offsetBeats", "시작 박" },
        { "ducker.mix", "덕킹 비율" },
        { "chorus.rateHz", "빠르기" }, { "chorus.rateBeats", "주기 (박)" }, { "chorus.depthMs", "깊이" }, { "chorus.delayMs", "지연" },
        { "chorus.feedback", "피드백" }, { "chorus.mix", "효과음 비율" }, { "chorus.phaseDeg", "좌우 위상차" },
        { "flanger.rateHz", "빠르기" }, { "flanger.rateBeats", "주기 (박)" }, { "flanger.depthMs", "깊이" }, { "flanger.delayMs", "지연" },
        { "flanger.feedback", "피드백" }, { "flanger.mix", "효과음 비율" }, { "flanger.phaseDeg", "좌우 위상차" },
        { "phaser.rateHz", "빠르기" }, { "phaser.rateBeats", "주기 (박)" }, { "phaser.lowHz", "낮은 주파수" }, { "phaser.highHz", "높은 주파수" },
        { "phaser.stages", "단 수" }, { "phaser.feedback", "피드백" }, { "phaser.mix", "효과음 비율" }, { "phaser.phaseDeg", "좌우 위상차" },
        { "tremolo.rateHz", "빠르기" }, { "tremolo.rateBeats", "주기 (박)" }, { "tremolo.depth", "깊이" }, { "tremolo.phaseDeg", "좌우 위상차" },
        { "gate.thresholdDb", "스레숄드" }, { "gate.rangeDb", "닫힘 레벨" }, { "gate.attackMs", "어택" }, { "gate.holdMs", "유지" },
        { "gate.releaseMs", "릴리스" },
        { "multiband.lowHz", "낮은 나눔 주파수" }, { "multiband.highHz", "높은 나눔 주파수" },
        { "multiband.lowThresholdDb", "저역 스레숄드" }, { "multiband.lowRatio", "저역 압축비" }, { "multiband.lowGainDb", "저역 게인" },
        { "multiband.midThresholdDb", "중역 스레숄드" }, { "multiband.midRatio", "중역 압축비" }, { "multiband.midGainDb", "중역 게인" },
        { "multiband.highThresholdDb", "고역 스레숄드" }, { "multiband.highRatio", "고역 압축비" }, { "multiband.highGainDb", "고역 게인" },
        { "multiband.attackMs", "어택" }, { "multiband.releaseMs", "릴리스" }, { "multiband.kneeDb", "니" },
        { "transient.attackDb", "어택" }, { "transient.sustainDb", "서스테인" }, { "transient.outputDb", "출력 게인" },
        { "deesser.frequencyHz", "나눔 주파수" }, { "deesser.thresholdDb", "스레숄드" }, { "deesser.rangeDb", "최대 감소" },
        { "band.freqHz", "주파수" }, { "band.gainDb", "게인" }, { "band.q", "Q" },
        { "synth.detune", "디튠 폭" }, { "synth.noise", "잡음" }, { "synth.filter", "켜기" }, { "synth.cutoffHz", "컷오프" },
        { "synth.resonance", "레조넌스" }, { "synth.filterEnvHz", "엔벨로프 양" }, { "synth.filterEnvDecay", "엔벨로프 감쇠" },
        { "synth.filterVelHz", "벨로시티" }, { "synth.keytrack", "키 추적" }, { "synth.attack", "어택" }, { "synth.decay", "디케이" },
        { "synth.sustain", "서스테인" }, { "synth.release", "릴리스" }, { "synth.vibratoRate", "빠르기" }, { "synth.vibratoCents", "깊이" },
        { "synth.vibratoDelay", "지연" }, { "synth.drive", "드라이브" }, { "synth.velocityCurve", "벨로시티 곡선" },
        { "synth.spread", "스테레오 폭" }, { "synth.levelDb", "레벨" }, { "synth.decayLow", "저음 울림" }, { "synth.decayHigh", "고음 울림" },
        { "synth.filterMode", "필터 방식" }, { "synth.bendRange", "벤드 범위" }, { "synth.tune", "음높이" },
        { "synth.pitchEnv", "피치 엔벨로프" }, { "synth.pitchEnvDecay", "피치 엔벨로프 감쇠" }, { "synth.mono", "모노" }, { "synth.glide", "글라이드" },
    };
    const auto it = labels.find (type + "." + key);
    return it != labels.end() ? u8 (it->second) : juce::String::fromUTF8 (key.c_str());
}

juce::String parameterTooltip (const std::string& type, const std::string& key)
{
    static const std::map<std::string, const char*> tips {
        { "compressor.mix", "압축한 소리와 원래 소리를 섞는 비율입니다. 100% 면 압축한 소리만 냅니다." },
        { "compressor.kneeDb", "스레숄드 앞뒤로 이 폭 안에서 압축이 서서히 걸립니다." },
        { "reverb.mix", "원음 레벨과 잔향 레벨을 따로 정하지 않으면 원음은 (100% - 비율), 잔향은 비율만큼 냅니다." },
        { "reverb.decaySeconds", "잔향이 60 dB 줄어드는 시간(RT60)입니다." },
        { "reverb.damping", "높은 대역의 잔향을 더 빨리 줄입니다." },
        { "reverb.dryDb", "켜면 잔향 비율 대신 이 값으로 원음의 크기를 정합니다." },
        { "reverb.wetDb", "켜면 잔향 비율 대신 이 값으로 잔향의 크기를 정합니다." },
        { "delay.pingPong", "메아리가 왼쪽과 오른쪽을 번갈아 오갑니다." },
        { "delay.feedback", "메아리가 다시 지연선으로 돌아가는 비율입니다." },
        { "limiter.lookaheadMs", "미리 보기의 두 배만큼 지연이 생깁니다. 트랙에서 바꾸면 재생을 다시 준비합니다." },
        { "limiter.truePeak", "샘플 사이의 봉우리까지 보고 줄입니다." },
        { "stereo.pan", "밸런스와 달리 한쪽 채널을 끄지 않고 소리의 위치를 옮깁니다." },
        { "eq.lowCutHz", "켜면 이 주파수 아래를 깎습니다." },
        { "eq.highCutHz", "켜면 이 주파수 위를 깎습니다." },
        { "delay.timeBeats", "켜면 지연 시간 대신 박 수로 정합니다. 템포가 바뀌면 길이도 따라 바뀝니다." },
        { "filter.cutoffHz", "대역 통과와 노치에서는 가운데 주파수입니다." },
        { "filter.resonance", "0% 면 차단 주파수에서 -3 dB 이고, 100% 면 Q 10 으로 봉우리가 +20 dB 입니다." },
        { "ducker.rateBeats", "이 박 수마다 소리를 깊이만큼 눌렀다가 되돌립니다. 키 트랙이 필요 없습니다." },
        { "ducker.releaseBeats", "유지가 끝난 뒤 0 dB 로 돌아오는 데 걸리는 박 수입니다." },
        { "ducker.curve", "1 이면 dB 로 곧게 돌아옵니다. 클수록 처음에 빨리 돌아오고, 작을수록 오래 눌러 둡니다." },
        { "ducker.offsetBeats", "누르기 시작하는 박입니다. 0.5 면 엇박에서 누릅니다." },
        { "chorus.rateBeats", "켜면 빠르기 대신 이 박 수를 한 주기로 흔듭니다. 템포가 바뀌면 따라 바뀝니다." },
        { "flanger.rateBeats", "켜면 빠르기 대신 이 박 수를 한 주기로 흔듭니다. 템포가 바뀌면 따라 바뀝니다." },
        { "phaser.rateBeats", "켜면 빠르기 대신 이 박 수를 한 주기로 흔듭니다. 템포가 바뀌면 따라 바뀝니다." },
        { "tremolo.rateBeats", "켜면 빠르기 대신 이 박 수를 한 주기로 흔듭니다. 0.25 면 16분음표마다입니다." },
        { "chorus.depthMs", "지연 시간이 지연에서 지연 + 깊이 사이를 오갑니다." },
        { "flanger.depthMs", "지연 시간이 지연에서 지연 + 깊이 사이를 오갑니다." },
        { "chorus.phaseDeg", "오른쪽 채널의 흔들림이 이만큼 앞섭니다. 0 이면 모노로 흔들립니다." },
        { "flanger.phaseDeg", "오른쪽 채널의 흔들림이 이만큼 앞섭니다. 0 이면 모노로 흔들립니다." },
        { "phaser.phaseDeg", "오른쪽 채널의 흔들림이 이만큼 앞섭니다. 0 이면 모노로 흔들립니다." },
        { "tremolo.phaseDeg", "180 이면 왼쪽과 오른쪽이 번갈아 커지는 오토팬이 됩니다." },
        { "flanger.feedback", "음수면 지연음을 뒤집어 돌려 빈 소리가 납니다." },
        { "phaser.lowHz", "흔들림이 지나는 가장 낮은 꺾임 주파수입니다. 2단이면 이 주파수가 지워집니다." },
        { "gate.thresholdDb", "소리가 이 위로 올라오면 열고, 3 dB 아래로 내려가 유지 시간이 지나면 닫습니다." },
        { "gate.rangeDb", "닫혔을 때 줄이는 만큼입니다. -96 dB 면 소리를 끕니다." },
        { "multiband.lowHz", "낮은 대역과 가운데 대역을 나누는 주파수입니다." },
        { "multiband.highHz", "가운데 대역과 높은 대역을 나누는 주파수입니다. 낮은 나눔 주파수의 두 배 위여야 합니다." },
        { "multiband.lowRatio", "1 이면 이 대역을 누르지 않습니다." },
        { "transient.attackDb", "소리가 차오르는 첫 몇 ms 를 키우거나 줄입니다." },
        { "transient.sustainDb", "어택 뒤 잦아드는 울림을 키우거나 줄입니다." },
        { "deesser.frequencyHz", "이 위의 대역(치찰음)만 줄입니다." },
        { "deesser.rangeDb", "치찰음 대역을 가장 많이 줄이는 만큼입니다." },
        { "synth.detune", "여러 오실레이터가 서로 어긋난 정도를 늘이거나 줄입니다. 100% 가 프리셋 그대로입니다." },
        { "synth.filterEnvHz", "음이 시작할 때 컷오프를 이만큼 올렸다가 엔벨로프 감쇠 시간에 걸쳐 내립니다." },
        { "synth.filterVelHz", "세게 칠수록 컷오프를 이만큼까지 더 엽니다." },
        { "synth.keytrack", "높은 음일수록 컷오프를 엽니다. 100% 면 음높이를 그대로 따릅니다." },
        { "synth.drive", "필터 뒤의 포화입니다. 0 이면 끕니다." },
        { "synth.velocityCurve", "세기에 따른 크기의 곡선입니다. 1 이면 직선이고, 클수록 여린 음이 더 작아집니다." },
        { "synth.spread", "음높이에 따라 소리를 좌우로 벌립니다." },
        { "synth.decayLow", "가장 낮은 건반(A0)의 소리가 60 dB 줄어드는 시간입니다." },
        { "synth.decayHigh", "가장 높은 건반(C8)의 소리가 60 dB 줄어드는 시간입니다." },
        { "synth.vibratoDelay", "음이 시작하고 이만큼 지난 뒤에 비브라토가 들어옵니다." },
        { "synth.filterMode", "저역 통과는 컷오프 위를, 고역 통과는 컷오프 아래를 깎고, 대역 통과는 컷오프 둘레만 남깁니다. CC74 가 컷오프를 옮깁니다." },
        { "synth.bendRange", "피치 벤드를 끝까지 올리거나 내렸을 때 움직이는 반음 수입니다." },
        { "synth.pitchEnv", "음이 시작할 때 이 반음만큼 높게 시작해 제 높이로 내려앉습니다. 음수면 아래에서 올라옵니다." },
        { "synth.pitchEnvDecay", "피치 엔벨로프가 1% 로 줄어드는 시간입니다." },
        { "synth.mono", "한 번에 한 음만 냅니다. 누르고 있는 음에 겹친 음은 글라이드로 미끄러지고, 띄어 친 음은 앞 음을 끊습니다." },
        { "synth.glide", "모노에서 겹친 음으로 미끄러지는 시간입니다. 0 이면 바로 바뀝니다." },
        { "synth.tune", "모든 북의 음높이를 이 반음만큼 옮깁니다. 클럽 키트의 킥은 0 에서 A1(55 Hz)입니다." },
    };
    const auto it = tips.find (type + "." + key);
    return it != tips.end() ? u8 (it->second) : juce::String();
}

juce::String synthSectionName (const std::string& section)
{
    static const std::map<std::string, const char*> names {
        { "oscillator", "오실레이터" }, { "filter", "필터" }, { "amp", "앰프 엔벨로프" }, { "vibrato", "비브라토" },
        { "output", "출력" }, { "decay", "울림" }, { "pitch", "음높이" },
    };
    const auto it = names.find (section);
    return it != names.end() ? u8 (it->second) : juce::String::fromUTF8 (section.c_str());
}

juce::String synthChoiceLabel (const std::string& key, const std::string& name)
{
    if (key == "filterMode")
    {
        if (name == "highpass")
            return u8 ("고역 통과");
        if (name == "bandpass")
            return u8 ("대역 통과");
        return u8 ("저역 통과");
    }
    return juce::String::fromUTF8 (name.c_str());
}

juce::String synthKitName (const std::string& preset)
{
    if (preset == "club-drums")
        return u8 ("클럽 키트: 킥 36, 긴 킥 35, 스네어 38, 롤 스네어 40, 클랩 39\n하이햇 42, 44, 46, 크래시 49, 라이드 51, 탐 41~50\n"
                   "효과음: 임팩트 60, 다운리프터 62와 63, 상승음 64~66, 역방향 심벌 67~69 (길이는 박 수)");
    if (preset == "edm-drums")
        return u8 ("전자 키트: 킥 36, 스네어 38, 클랩 39, 하이햇 42와 46, 크래시 49");
    if (preset == "trailer-drums")
        return u8 ("예고편 키트: 붐 35, 큰 북 36, 큰 타격 38, 북 41, 43, 45, 모루 56, 끊기 44");
    return u8 ("어쿠스틱 키트: 킥 36, 스네어 38, 하이햇 42와 46, 크래시 49, 라이드 51, 탐 41~50");
}

juce::String bandTypeName (const std::string& type)
{
    if (type == "lowShelf")
        return u8 ("로우 셸프");
    if (type == "highShelf")
        return u8 ("하이 셸프");
    return u8 ("피크");
}

juce::String formatParameter (const mr::EffectParameter& p, double v)
{
    const auto& u = p.unit;
    if (u == "dB")
        return (v > 0.0 ? "+" : "") + juce::String (v, 1) + " dB";
    if (u == "Hz")
    {
        if (v >= 1000.0)
            return juce::String (v / 1000.0, v >= 10000.0 ? 1 : 2) + " kHz";
        return juce::String (v, v < 100.0 ? 1 : 0) + " Hz";
    }
    if (u == "ms")
        return juce::String (v, v < 10.0 ? 2 : (v < 100.0 ? 1 : 0)) + " ms";
    if (u == "s")
        return v == 0.0 ? u8 ("0초") : juce::String (v, v < 0.1 ? 3 : 2) + u8 ("초");
    if (u == "cents")
        return juce::String (v, 1) + u8 (" 센트");
    if (u == "semitones")
        return (std::abs (v - std::round (v)) < 1.0e-9 ? juce::String ((int) std::lround (v)) : juce::String (v, 2)) + u8 (" 반음");
    if (u == "ratio")
        return juce::String (v, 1) + ":1";
    if (u == "percent")
        return juce::String (juce::roundToInt (v * 100.0)) + "%";
    if (u == "pan")
        return formatPan (v);
    if (u == "dB/oct")
        return juce::String (juce::roundToInt (v)) + u8 (" dB/옥타브");
    if (u == "beats")
        return juce::String (v, v < 1.0 ? 3 : 2) + u8 (" 박");
    if (u == "degrees")
        return juce::String (juce::roundToInt (v)) + u8 ("°");
    if (u == "stages")
        return juce::String (juce::roundToInt (v)) + u8 ("단");
    if (p.toggle)
        return u8 (v >= 0.5 ? "켜짐" : "꺼짐");
    return juce::String (v, 2);
}

double parseParameter (const mr::EffectParameter& p, const juce::String& text)
{
    auto t = text.trim();
    if (p.unit == "pan")
    {
        const auto upper = t.toUpperCase();
        if (upper.startsWith ("C"))
            return 0.0;
        const double amount = upper.retainCharacters ("0123456789.").getDoubleValue() / 100.0;
        if (upper.startsWith ("L"))
            return -amount;
        if (upper.startsWith ("R"))
            return amount;
        const double v = t.getDoubleValue();
        return std::abs (v) > 1.0 ? v / 100.0 : v;
    }
    double v = t.retainCharacters ("-+0123456789.").getDoubleValue();
    if (p.unit == "Hz" && t.containsIgnoreCase ("k"))
        v *= 1000.0;
    if (p.unit == "percent")
        v /= 100.0;
    return v;
}

double roundParameter (const mr::EffectParameter& p, double v)
{
    auto step = [] (double value, double s) { return std::round (value / s) * s; };
    const auto& u = p.unit;
    double r = v;
    if (u == "dB")
        r = step (v, 0.1);
    else if (u == "Hz")
        r = step (v, v < 100.0 ? 0.1 : (v < 1000.0 ? 1.0 : (v < 10000.0 ? 10.0 : 100.0)));
    else if (u == "ms")
        r = step (v, v < 10.0 ? 0.01 : (v < 100.0 ? 0.1 : 1.0));
    else if (u == "s")
        r = step (v, v < 0.1 ? 0.001 : 0.01);
    else if (u == "cents")
        r = step (v, 0.1);
    else if (u == "semitones")
        r = step (v, 0.01);
    else if (u == "ratio")
        r = step (v, 0.1);
    else if (u == "percent" || u == "pan")
        r = step (v, 0.01);
    else if (u == "dB/oct" || u == "degrees" || u == "stages" || p.toggle)
        r = std::round (v);
    else if (u == "beats")
        r = step (v, 1.0 / 192.0);   // 64분음표와 셋잇단이 모두 맞는다
    else if (u.empty())
        r = step (v, 0.01);
    // 반올림한 값의 이진 오차를 줄인다 (저장 형식이 가장 짧은 표기를 쓴다)
    r = std::stod (juce::String (r, 6).toStdString());
    return juce::jlimit (p.minimum, p.maximum, r);
}

juce::String formatGainDb (double db)
{
    if (db <= faderlaw::minDb + 0.05)
        return juce::String::fromUTF8 ("-∞");
    if (std::abs (db) < 0.05)
        return "0.0";
    return (db > 0.0 ? "+" : "") + juce::String (db, 1);
}

juce::String formatPan (double pan)
{
    const int amount = juce::roundToInt (std::abs (pan) * 100.0);
    if (amount == 0)
        return "C";
    return (pan < 0.0 ? "L" : "R") + juce::String (amount);
}

juce::String formatPeakDb (float gain)
{
    if (gain <= 1.0e-6f)
        return juce::String::fromUTF8 ("-∞");
    const double db = 20.0 * std::log10 ((double) gain);
    return (db > 0.0 ? "+" : "") + juce::String (db, 1);
}

} // namespace daw
