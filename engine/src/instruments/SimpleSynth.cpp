#include "instruments/SimpleSynth.h"

#include "core/Diagnostics.h"
#include "core/Handoff.h"
#include "core/Json.h"
#include "core/Random.h"
#include "timing/TempoMap.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <map>

namespace mr
{

namespace
{

constexpr double twoPi = 6.283185307179586;
constexpr int maxVoices = 48;
constexpr int maxPartials = 16;
constexpr int maxOscs = 8;

enum class Engine { subtractive, piano, pluck, drums };
enum class Wave { sine, triangle, saw, square };

struct Osc
{
    Wave wave = Wave::sine;
    double detuneCents = 0.0;
    double octave = 0.0;
    double level = 1.0;
    double pan = 0.0;                  // -1..1: 유니즌을 좌우로 벌린다
    double ratio = 1.0;                // 2^(octave + detune/1200), 프리셋을 만들 때 한 번 계산한다
};

struct Preset
{
    std::string name;
    Engine engine = Engine::subtractive;
    std::vector<Osc> oscs;
    double noise = 0.0;
    double attack = 0.005, decay = 0.0, sustain = 1.0, release = 0.08;
    double filterBaseHz = 0.0;        // 0 이면 필터를 쓰지 않는다
    double filterEnvHz = 0.0, filterVelHz = 0.0, filterKeytrack = 0.0, filterEnvDecay = 0.3, resonance = 0.0;
    double vibratoRate = 0.0, vibratoCents = 0.0, vibratoDelay = 0.0;
    double decayLowSec = 6.0, decayHighSec = 1.2;   // piano, pluck: MIDI 21 과 108 의 T60
    double velocityCurve = 1.6;
    double gain = 0.25;
    double spread = 0.0;               // 음높이에 따른 팬 폭
    bool modDynamics = false;          // CC1 이 밝기와 크기를 바꾼다 (현악, 패드)
    double drive = 0.0;                // 필터 뒤 tanh 포화 (0 이면 끈다)
    int filterMode = 0;                // 0 저역 통과, 1 고역 통과, 2 대역 통과
    double bendRange = 2.0;            // 피치 벤드 끝(±1)의 반음
    double pitchEnv = 0.0;             // 음이 시작할 때 이 반음만큼 높게 시작해 pitchEnvDecay 에 걸쳐 제 높이로 내려앉는다
    double pitchEnvDecay = 0.05;       // 음높이 엔벨로프가 1% 로 줄어드는 시간 (초)
    bool mono = false;                 // 한 목소리: 겹친 음으로 미끄러지고, 겹치지 않은 새 음은 앞 음을 끊는다
    double glide = 0.0;                // mono: 겹친 음으로 미끄러지는 시간 (초, 반음 단위로 곧게)
    bool resetPhase = false;           // 오실레이터를 위상 0 에서 시작한다 (칠 때마다 같은 공격)
    double tune = 0.0;                 // drums: 북의 음높이를 옮기는 반음
    double tuneRatio = 1.0;            // 2^(tune/12), 값을 덮을 때 한 번 계산한다
    int kit = 0;                       // drums: 0 어쿠스틱에 가까운 키트, 1 전자 키트, 2 예고편 키트, 3 클럽 키트
};

std::map<std::string, Preset> buildPresets()
{
    std::map<std::string, Preset> p;

    Preset sine;
    sine.name = "sine";
    sine.oscs = { { Wave::sine, 0.0, 0.0, 1.0 } };
    sine.velocityCurve = 1.0;
    sine.gain = 0.3;
    p[sine.name] = sine;

    Preset piano;
    piano.name = "piano";
    piano.engine = Engine::piano;
    piano.decayLowSec = 9.0;
    piano.decayHighSec = 1.3;
    piano.release = 0.18;
    piano.gain = 0.42;
    piano.spread = 0.6;
    p[piano.name] = piano;

    Preset pad;
    pad.name = "pad";
    pad.oscs = { { Wave::saw, -7.0, 0.0, 0.5 }, { Wave::saw, 7.0, 0.0, 0.5 }, { Wave::triangle, 0.0, -1.0, 0.35 } };
    pad.attack = 0.8; pad.decay = 0.6; pad.sustain = 0.85; pad.release = 1.6;
    pad.filterBaseHz = 900.0; pad.filterEnvHz = 500.0; pad.filterVelHz = 900.0; pad.filterKeytrack = 0.3; pad.filterEnvDecay = 1.2; pad.resonance = 0.12;
    pad.vibratoRate = 4.5; pad.vibratoCents = 6.0; pad.vibratoDelay = 0.4;
    pad.velocityCurve = 1.0;
    pad.gain = 0.16;
    pad.spread = 0.4;
    pad.modDynamics = true;
    p[pad.name] = pad;

    Preset strings;
    strings.name = "strings";
    strings.oscs = { { Wave::saw, -9.0, 0.0, 0.4 }, { Wave::saw, 0.0, 0.0, 0.4 }, { Wave::saw, 9.0, 0.0, 0.4 } };
    strings.attack = 0.22; strings.decay = 0.3; strings.sustain = 0.9; strings.release = 0.45;
    strings.filterBaseHz = 1300.0; strings.filterEnvHz = 300.0; strings.filterVelHz = 2400.0; strings.filterKeytrack = 0.5; strings.filterEnvDecay = 0.5; strings.resonance = 0.05;
    strings.vibratoRate = 5.6; strings.vibratoCents = 14.0; strings.vibratoDelay = 0.25;
    strings.velocityCurve = 1.2;
    strings.gain = 0.14;
    strings.spread = 0.5;
    strings.modDynamics = true;
    p[strings.name] = strings;

    Preset bass;
    bass.name = "bass";
    bass.oscs = { { Wave::saw, 0.0, 0.0, 0.6 }, { Wave::sine, 0.0, -1.0, 0.7 } };
    bass.attack = 0.004; bass.decay = 0.25; bass.sustain = 0.6; bass.release = 0.09;
    bass.filterBaseHz = 420.0; bass.filterEnvHz = 900.0; bass.filterVelHz = 600.0; bass.filterKeytrack = 0.2; bass.filterEnvDecay = 0.18; bass.resonance = 0.25;
    bass.gain = 0.3;
    p[bass.name] = bass;

    Preset pluck;
    pluck.name = "pluck";
    pluck.engine = Engine::pluck;
    pluck.decayLowSec = 4.0;
    pluck.decayHighSec = 0.6;
    pluck.release = 0.12;
    pluck.gain = 0.5;
    pluck.spread = 0.5;
    p[pluck.name] = pluck;

    Preset drums;
    drums.name = "drums";
    drums.engine = Engine::drums;
    drums.gain = 0.55;
    p[drums.name] = drums;

    // ---- 전자 음악용 ----
    // 슈퍼소: 톱니파 일곱 개를 조금씩 어긋나게 겹치고 좌우로 벌린다
    auto unison = [] (Wave w, std::initializer_list<std::pair<double, double>> detunePan, double level)
    {
        std::vector<Osc> oscs;
        for (const auto& [cents, pan] : detunePan)
            oscs.push_back ({ w, cents, 0.0, level, pan });
        return oscs;
    };
    Preset supersaw;
    supersaw.name = "supersaw";
    supersaw.oscs = unison (Wave::saw, { { 0.0, 0.0 }, { -9.0, -0.35 }, { 9.0, 0.35 }, { -17.0, -0.7 }, { 17.0, 0.7 }, { -27.0, -1.0 }, { 27.0, 1.0 } }, 0.3);
    supersaw.attack = 0.008; supersaw.decay = 0.5; supersaw.sustain = 0.85; supersaw.release = 0.3;
    supersaw.filterBaseHz = 1600.0; supersaw.filterEnvHz = 3800.0; supersaw.filterVelHz = 1800.0; supersaw.filterKeytrack = 0.3;
    supersaw.filterEnvDecay = 0.45; supersaw.resonance = 0.12;
    supersaw.drive = 1.3;
    supersaw.velocityCurve = 1.2;
    supersaw.gain = 0.18;
    p[supersaw.name] = supersaw;

    Preset sawPad = supersaw;
    sawPad.name = "saw-pad";
    sawPad.attack = 0.35; sawPad.decay = 1.2; sawPad.sustain = 0.9; sawPad.release = 1.4;
    sawPad.filterBaseHz = 700.0; sawPad.filterEnvHz = 1400.0; sawPad.filterVelHz = 900.0; sawPad.filterEnvDecay = 1.6; sawPad.resonance = 0.1;
    sawPad.drive = 0.8;
    sawPad.vibratoRate = 0.25; sawPad.vibratoCents = 4.0;
    sawPad.gain = 0.08;
    p[sawPad.name] = sawPad;

    // 플럭: 필터가 빨리 닫히고 지속음이 없다
    Preset edmPluck;
    edmPluck.name = "edm-pluck";
    edmPluck.oscs = unison (Wave::saw, { { 0.0, 0.0 }, { -11.0, -0.5 }, { 11.0, 0.5 } }, 0.45);
    edmPluck.oscs.push_back ({ Wave::square, 0.0, -1.0, 0.25, 0.0 });
    edmPluck.attack = 0.002; edmPluck.decay = 0.28; edmPluck.sustain = 0.0; edmPluck.release = 0.2;
    edmPluck.filterBaseHz = 500.0; edmPluck.filterEnvHz = 7000.0; edmPluck.filterVelHz = 2500.0; edmPluck.filterKeytrack = 0.4;
    edmPluck.filterEnvDecay = 0.085; edmPluck.resonance = 0.28;
    edmPluck.drive = 1.0;
    edmPluck.gain = 0.34;
    p[edmPluck.name] = edmPluck;

    // 서브 베이스: 사인파에 아주 조금 포화
    Preset sub;
    sub.name = "sub";
    sub.oscs = { { Wave::sine, 0.0, 0.0, 1.0, 0.0 } };
    sub.attack = 0.004; sub.release = 0.07;
    sub.drive = 1.4;
    sub.velocityCurve = 1.0;
    sub.gain = 0.42;
    p[sub.name] = sub;

    // 808 베이스: 위상 0 에서 시작하는 사인파가 한 옥타브 위에서 내려앉고 길게 잦아든다. 한 목소리라 겹친 음은 미끄러진다
    Preset tr808;
    tr808.name = "808";
    tr808.oscs = { { Wave::sine, 0.0, 0.0, 1.0, 0.0 } };
    tr808.attack = 0.001; tr808.decay = 4.0; tr808.sustain = 0.25; tr808.release = 0.08;
    tr808.pitchEnv = 12.0; tr808.pitchEnvDecay = 0.06;
    tr808.mono = true; tr808.glide = 0.08; tr808.resetPhase = true;
    tr808.drive = 2.0;
    tr808.velocityCurve = 1.0;
    tr808.gain = 0.4;
    p[tr808.name] = tr808;

    // 톱니파 베이스: 두 톱니파와 한 옥타브 아래 사각파, 필터 엔벨로프
    Preset sawBass;
    sawBass.name = "saw-bass";
    sawBass.oscs = { { Wave::saw, -12.0, 0.0, 0.5, -0.3 }, { Wave::saw, 12.0, 0.0, 0.5, 0.3 }, { Wave::square, 0.0, -1.0, 0.4, 0.0 } };
    sawBass.attack = 0.003; sawBass.decay = 0.22; sawBass.sustain = 0.55; sawBass.release = 0.08;
    sawBass.filterBaseHz = 260.0; sawBass.filterEnvHz = 1700.0; sawBass.filterVelHz = 700.0; sawBass.filterKeytrack = 0.25;
    sawBass.filterEnvDecay = 0.16; sawBass.resonance = 0.3;
    sawBass.drive = 2.0;
    sawBass.gain = 0.35;
    p[sawBass.name] = sawBass;

    // 리드: 톱니파 둘과 한 옥타브 위 사각파, 늦게 들어오는 비브라토
    Preset lead;
    lead.name = "lead";
    lead.oscs = { { Wave::saw, -6.0, 0.0, 0.5, -0.25 }, { Wave::saw, 6.0, 0.0, 0.5, 0.25 }, { Wave::square, 0.0, 1.0, 0.18, 0.0 } };
    lead.attack = 0.006; lead.decay = 0.3; lead.sustain = 0.8; lead.release = 0.18;
    lead.filterBaseHz = 2200.0; lead.filterEnvHz = 3500.0; lead.filterVelHz = 1500.0; lead.filterKeytrack = 0.35;
    lead.filterEnvDecay = 0.25; lead.resonance = 0.15;
    lead.vibratoRate = 5.5; lead.vibratoCents = 18.0; lead.vibratoDelay = 0.28;
    lead.drive = 1.2;
    lead.gain = 0.16;
    p[lead.name] = lead;

    // 일렉트릭 피아노: 바탕음은 삼각파 하나(같은 높이의 오실레이터를 겹치면 시작 위상에 따라 서로 지워진다), 한 옥타브 위 사각파와
    // 두 옥타브 위 사인파가 위 배음을 맡는다. 세게 칠수록 필터가 열려 맑아지고, 서서히 사라진다
    Preset ep;
    ep.name = "ep";
    ep.oscs = { { Wave::triangle, 0.0, 0.0, 0.7, -0.05 }, { Wave::square, 3.0, 1.0, 0.16, 0.2 }, { Wave::sine, -3.0, 2.0, 0.08, -0.2 } };
    ep.attack = 0.002; ep.decay = 1.8; ep.sustain = 0.2; ep.release = 0.45;
    ep.filterBaseHz = 250.0; ep.filterEnvHz = 1200.0; ep.filterVelHz = 7000.0; ep.filterKeytrack = 0.2; ep.filterEnvDecay = 0.14; ep.resonance = 0.06;
    ep.vibratoRate = 4.6; ep.vibratoCents = 4.0;
    ep.velocityCurve = 1.5;
    ep.gain = 0.32;
    ep.spread = 0.4;
    p[ep.name] = ep;

    // 폴리 키: 조금 어긋난 톱니파 둘과 사각파, 필터 엔벨로프 (아날로그 폴리신스의 건반)
    Preset polyKeys;
    polyKeys.name = "poly-keys";
    polyKeys.oscs = { { Wave::saw, -8.0, 0.0, 0.5, -0.4 }, { Wave::saw, 8.0, 0.0, 0.5, 0.4 }, { Wave::square, 0.0, 0.0, 0.2, 0.0 } };
    polyKeys.attack = 0.004; polyKeys.decay = 0.9; polyKeys.sustain = 0.4; polyKeys.release = 0.35;
    polyKeys.filterBaseHz = 800.0; polyKeys.filterEnvHz = 2600.0; polyKeys.filterVelHz = 1600.0; polyKeys.filterKeytrack = 0.4;
    polyKeys.filterEnvDecay = 0.35; polyKeys.resonance = 0.18;
    polyKeys.drive = 0.5;
    polyKeys.velocityCurve = 1.3;
    polyKeys.gain = 0.14;
    polyKeys.spread = 0.3;
    p[polyKeys.name] = polyKeys;

    // 잡음: 상승음과 쓸어내림 (CC74 로 필터를 움직인다)
    Preset noise;
    noise.name = "noise";
    noise.noise = 1.0;
    noise.attack = 0.05; noise.release = 0.6;
    noise.filterBaseHz = 7000.0; noise.resonance = 0.4;
    noise.velocityCurve = 1.0;
    noise.gain = 0.1;
    p[noise.name] = noise;

    Preset edmDrums = drums;
    edmDrums.name = "edm-drums";
    edmDrums.kit = 1;
    edmDrums.gain = 0.6;
    p[edmDrums.name] = edmDrums;

    // 예고편 키트: 35 붐(땅이 울리는 저음 충격), 36 큰 북(타이코), 38 큰 타격, 41/43/45 북(낮음, 가운데, 높음), 56 모루, 39 클랩,
    // 44 끊기 (울리는 소리를 모두 멈춘다)
    Preset trailer = drums;
    trailer.name = "trailer-drums";
    trailer.kit = 2;
    trailer.gain = 0.6;
    p[trailer.name] = trailer;

    // 클럽 키트: 클릭과 조율하는 몸통의 킥, 겹친 클랩, 롤 스네어, 하이햇, 라이드, 탐, 그리고 박 단위 길이의 효과음 (모두 끝에서 잦아든다)
    Preset club = drums;
    club.name = "club-drums";
    club.kit = 3;
    club.gain = 0.6;
    p[club.name] = club;

    for (auto& [name, preset] : p)
        for (auto& o : preset.oscs)
            o.ratio = std::pow (2.0, o.octave + o.detuneCents / 1200.0);
    return p;
}

const std::map<std::string, Preset>& presets()
{
    static const auto table = buildPresets();
    return table;
}

// ---- 매개변수 표 ----

ParameterInfo info (const char* key, double lo, double hi, const char* unit, const char* section, bool log = false, double skewMid = 0.0)
{
    ParameterInfo p;
    p.key = key;
    p.minimum = lo;
    p.maximum = hi;
    p.unit = unit;
    p.section = section;
    p.logarithmic = log;
    p.skewMid = skewMid;
    return p;
}

const std::vector<std::string>& filterModeNames()
{
    static const std::vector<std::string> names { "lowpass", "highpass", "bandpass" };
    return names;
}

// 합성 방식마다 손잡이 (기본값은 프리셋에서 채운다)
const std::vector<ParameterInfo>& layoutFor (Engine e)
{
    static const auto bendRange = info ("bendRange", 0, 24, "semitones", "pitch");
    static const std::vector<ParameterInfo> subtractive = []
    {
        auto filter = info ("filter", 0, 1, "", "filter");
        filter.toggle = true;
        auto mode = info ("filterMode", 0, 2, "", "filter");
        mode.choices = { 0, 1, 2 };
        mode.choiceNames = filterModeNames();
        auto mono = info ("mono", 0, 1, "", "pitch");
        mono.toggle = true;
        return std::vector<ParameterInfo> {
            info ("detune", 0, 3, "percent", "oscillator"), info ("noise", 0, 1, "percent", "oscillator"),
            filter, mode, info ("cutoffHz", 20, 18000, "Hz", "filter", true), info ("resonance", 0, 0.95, "percent", "filter"),
            info ("filterEnvHz", 0, 12000, "Hz", "filter", false, 1500), info ("filterEnvDecay", 0.01, 5, "s", "filter", true),
            info ("filterVelHz", 0, 10000, "Hz", "filter", false, 1500), info ("keytrack", 0, 1, "percent", "filter"),
            info ("attack", 0.001, 5, "s", "amp", true), info ("decay", 0, 8, "s", "amp", false, 0.6),
            info ("sustain", 0, 1, "percent", "amp"), info ("release", 0.005, 8, "s", "amp", true),
            info ("vibratoRate", 0, 12, "Hz", "vibrato"), info ("vibratoCents", 0, 60, "cents", "vibrato"),
            info ("vibratoDelay", 0, 2, "s", "vibrato", false, 0.4), bendRange,
            info ("pitchEnv", -24, 24, "semitones", "pitch"), info ("pitchEnvDecay", 0.005, 2, "s", "pitch", true),
            mono, info ("glide", 0, 1, "s", "pitch", false, 0.15),
            info ("drive", 0, 4, "", "output", false, 1), info ("velocityCurve", 0.3, 3, "", "output"),
            info ("spread", 0, 1, "percent", "output"), info ("levelDb", -24, 12, "dB", "output"),
        };
    }();
    static const std::vector<ParameterInfo> pluck {
        info ("decayLow", 0.3, 20, "s", "decay", true), info ("decayHigh", 0.1, 8, "s", "decay", true),
        info ("release", 0.005, 8, "s", "decay", true),
        info ("velocityCurve", 0.3, 3, "", "output"), info ("spread", 0, 1, "percent", "output"), info ("levelDb", -24, 12, "dB", "output"),
    };
    static const std::vector<ParameterInfo> piano = []
    {
        auto table = pluck;
        table.insert (table.begin() + 3, bendRange);
        return table;
    }();
    static const std::vector<ParameterInfo> drums { info ("tune", -24, 24, "semitones", "pitch"), bendRange, info ("levelDb", -24, 12, "dB", "output") };
    switch (e)
    {
        case Engine::subtractive: return subtractive;
        case Engine::piano: return piano;
        case Engine::pluck: return pluck;
        case Engine::drums: break;
    }
    return drums;
}

// 프리셋에 그대로 있는 값 (매개변수의 기본값)
double presetValue (const Preset& p, const std::string& key)
{
    if (key == "detune") return 1.0;
    if (key == "noise") return p.noise;
    if (key == "filter") return p.filterBaseHz > 0.0 ? 1.0 : 0.0;
    if (key == "filterMode") return p.filterMode;
    if (key == "bendRange") return p.bendRange;
    if (key == "pitchEnv") return p.pitchEnv;
    if (key == "pitchEnvDecay") return p.pitchEnvDecay;
    if (key == "mono") return p.mono ? 1.0 : 0.0;
    if (key == "glide") return p.glide;
    if (key == "tune") return p.tune;
    if (key == "cutoffHz") return p.filterBaseHz > 0.0 ? p.filterBaseHz : 2000.0;
    if (key == "resonance") return p.resonance;
    if (key == "filterEnvHz") return p.filterEnvHz;
    if (key == "filterEnvDecay") return p.filterEnvDecay;
    if (key == "filterVelHz") return p.filterVelHz;
    if (key == "keytrack") return p.filterKeytrack;
    if (key == "attack") return p.attack;
    if (key == "decay") return p.decay;
    if (key == "sustain") return p.sustain;
    if (key == "release") return p.release;
    if (key == "vibratoRate") return p.vibratoRate;
    if (key == "vibratoCents") return p.vibratoCents;
    if (key == "vibratoDelay") return p.vibratoDelay;
    if (key == "drive") return p.drive;
    if (key == "velocityCurve") return p.velocityCurve;
    if (key == "spread") return p.spread;
    if (key == "decayLow") return p.decayLowSec;
    if (key == "decayHigh") return p.decayHighSec;
    return 0.0;   // levelDb
}

// 프리셋 위에 값을 덮는다. 표에 없는 키는 그 합성 방식에서 쓰지 않으므로 넘어간다.
Preset applyValues (Preset p, const SynthValues& values)
{
    auto get = [&values] (const char* key, double fallback)
    {
        const auto it = values.find (key);
        return it != values.end() ? it->second : fallback;
    };
    const double detune = get ("detune", 1.0);
    for (auto& o : p.oscs)
    {
        o.detuneCents *= detune;
        o.ratio = std::pow (2.0, o.octave + o.detuneCents / 1200.0);
    }
    p.noise = get ("noise", p.noise);
    const bool filterOn = get ("filter", p.filterBaseHz > 0.0 ? 1.0 : 0.0) >= 0.5;
    p.filterBaseHz = filterOn ? get ("cutoffHz", p.filterBaseHz > 0.0 ? p.filterBaseHz : 2000.0) : 0.0;
    p.resonance = get ("resonance", p.resonance);
    p.filterEnvHz = get ("filterEnvHz", p.filterEnvHz);
    p.filterEnvDecay = get ("filterEnvDecay", p.filterEnvDecay);
    p.filterVelHz = get ("filterVelHz", p.filterVelHz);
    p.filterKeytrack = get ("keytrack", p.filterKeytrack);
    p.attack = get ("attack", p.attack);
    p.decay = get ("decay", p.decay);
    p.sustain = get ("sustain", p.sustain);
    p.release = get ("release", p.release);
    p.vibratoRate = get ("vibratoRate", p.vibratoRate);
    p.vibratoCents = get ("vibratoCents", p.vibratoCents);
    p.vibratoDelay = get ("vibratoDelay", p.vibratoDelay);
    p.drive = get ("drive", p.drive);
    p.velocityCurve = get ("velocityCurve", p.velocityCurve);
    p.spread = get ("spread", p.spread);
    p.decayLowSec = get ("decayLow", p.decayLowSec);
    p.decayHighSec = get ("decayHigh", p.decayHighSec);
    p.filterMode = std::clamp ((int) std::lround (get ("filterMode", p.filterMode)), 0, 2);
    p.bendRange = get ("bendRange", p.bendRange);
    p.pitchEnv = get ("pitchEnv", p.pitchEnv);
    p.pitchEnvDecay = get ("pitchEnvDecay", p.pitchEnvDecay);
    p.mono = get ("mono", p.mono ? 1.0 : 0.0) >= 0.5;
    p.glide = get ("glide", p.glide);
    p.tune = get ("tune", p.tune);
    p.tuneRatio = std::pow (2.0, p.tune / 12.0);
    if (const double db = get ("levelDb", 0.0); db != 0.0)
        p.gain *= std::pow (10.0, db / 20.0);
    return p;
}

double polyBlep (double t, double dt)
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

double oscillator (Wave w, double phase, double dt)
{
    switch (w)
    {
        case Wave::sine: return std::sin (twoPi * phase);
        case Wave::triangle: return 4.0 * std::abs (phase - 0.5) - 1.0;
        case Wave::saw: return 2.0 * phase - 1.0 - polyBlep (phase, dt);
        case Wave::square:
        {
            double v = phase < 0.5 ? 1.0 : -1.0;
            v += polyBlep (phase, dt);
            v -= polyBlep (std::fmod (phase + 0.5, 1.0), dt);
            return v;
        }
    }
    return 0.0;
}

double midiToHz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

// 음높이에 따라 T60 을 로그로 보간한다 (낮은 음일수록 길다)
double pitchDecay (int note, double lowSec, double highSec)
{
    const double t = std::clamp ((note - 21) / 87.0, 0.0, 1.0);
    return std::exp (std::log (lowSec) + (std::log (highSec) - std::log (lowSec)) * t);
}

double decayMultiplier (double t60Seconds, double sampleRate)
{
    return std::exp (-6.907755278982137 / (std::max (0.001, t60Seconds) * sampleRate));
}

struct Svf
{
    double ic1 = 0.0, ic2 = 0.0, a1 = 0.0, a2 = 0.0, a3 = 0.0, k = 2.0;

    void set (double cutoff, double resonance, double sampleRate)
    {
        cutoff = std::clamp (cutoff, 20.0, sampleRate * 0.45);
        const double g = std::tan (3.141592653589793 * cutoff / sampleRate);
        k = 2.0 - 2.0 * std::clamp (resonance, 0.0, 0.95);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    // lowpass, bandpass 를 함께 돌려준다
    void process (double v0, double& lp, double& bp)
    {
        const double v3 = v0 - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        lp = v2;
        bp = v1;
    }
};

enum class Stage { attack, decay, sustain, release, off };

struct Voice
{
    bool active = false;
    bool noteHeld = false;     // note-off 를 아직 받지 않았다
    bool released = false;     // 소리가 사라지는 중
    int note = 0;
    double velocity = 0.0;
    uint64_t age = 0;
    double baseHz = 440.0;
    double time = 0.0;
    double panL = 0.7071, panR = 0.7071;

    Stage stage = Stage::off;
    double env = 0.0;
    double releaseMul = 0.999;

    std::array<double, maxOscs> phase {};
    Svf filter, filterR;
    double filterEnv = 1.0;
    double filterEnvMul = 1.0;   // 표본마다 곱하는 필터 엔벨로프 감쇠
    double keytrack = 1.0;       // (음높이 / C4)^filterKeytrack
    double levelGain = 1.0;      // velocity^velocityCurve
    double pitchEnvSemis = 0.0;  // 남은 음높이 엔벨로프 (반음)
    double pitchEnvMul = 1.0;    // 표본마다 곱하는 음높이 엔벨로프 감쇠
    double glideSemis = 0.0;     // 남은 글라이드 (반음, 0 으로 곧게 줄어든다)
    double glideStep = 0.0;      // 표본마다 줄이는 반음

    // piano
    int partials = 0;
    std::array<double, maxPartials> pc {}, ps {}, cw {}, sw {}, amp {}, fast {}, slow {}, fastMul {}, slowMul {}, ratio {};
    double lastBend = 1.0;
    double hammer = 0.0;

    // pluck
    std::vector<float> line;
    int lineLen = 0, pos = 0;
    double lastX = 0.0, apX = 0.0, apY = 0.0, apC = 0.0, loopGain = 0.99;

    // drums
    int drum = 0;
    bool choked = false;
    double dPhase = 0.0, hpX = 0.0, hpY = 0.0, hpX2 = 0.0, hpY2 = 0.0;
    Svf bandpass;
    Svf bandpassR;               // 클럽 키트: 오른쪽 채널의 둘째 거르개
    double fxLength = 0.0;       // 클럽 키트의 효과음: 박 수를 시작 자리의 템포 지도로 바꾼 초
    std::array<double, 4> partialPhase {};
    uint32_t ticks = 0;          // 클럽 키트: 음이 시작한 뒤의 샘플 수 (쓸기 계수를 16 샘플마다)

    Random rng { 0 };
};

class SimpleSynth final : public Instrument
{
public:
    SimpleSynth (Preset base, const SynthValues& values, uint64_t seed, const TempoMap* tempo)
        : base_ (std::move (base)), preset_ (applyValues (base_, values)), seed_ (seed),
          tempo_ (tempo != nullptr ? std::make_shared<const TempoMap> (*tempo) : nullptr)
    {
    }

    bool setSynthParameters (const SynthValues& values) override
    {
        next_.collect();
        next_.post (std::make_unique<Preset> (applyValues (base_, values)));
        return true;
    }

    void prepare (double sampleRate, int) override
    {
        sr_ = sampleRate;
        smoothing_ = 1.0 - std::exp (-1.0 / (0.01 * sr_));
        for (auto& v : voices_)
            v = Voice {};
    }

    std::string description() const override { return "synth:" + preset_.name; }

    void render (juce::AudioBuffer<float>& buffer, int numSamples, const juce::MidiBuffer& midi, int64_t blockStart) override
    {
        if (auto* fresh = next_.take())
        {
            std::swap (preset_, *fresh);   // 문자열과 벡터는 옮기기만 한다 (할당 없음)
            next_.retire (fresh);
            retuneVoices();
        }
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, numSamples);
        int position = 0;
        for (const auto meta : midi)
        {
            const int at = std::clamp (meta.samplePosition, 0, numSamples);
            renderRange (buffer, position, at);
            position = at;
            handle (meta.getMessage(), blockStart + at);
        }
        renderRange (buffer, position, numSamples);
    }

private:
    // 바꿔 끼운 프리셋을 울리는 음에도 쓴다 (음이 시작할 때 계산해 둔 값)
    void retuneVoices()
    {
        if (preset_.engine == Engine::drums)
            return;
        for (auto& v : voices_)
        {
            if (! v.active || v.choked)
                continue;
            v.releaseMul = decayMultiplier (preset_.release, sr_);
            if (preset_.engine == Engine::subtractive)
            {
                v.filterEnvMul = std::exp (-1.0 / (preset_.filterEnvDecay * sr_));
                v.pitchEnvMul = pitchEnvMultiplier();
                v.keytrack = std::pow (v.baseHz / 261.6, preset_.filterKeytrack);
                v.levelGain = std::pow (v.velocity, preset_.velocityCurve);
            }
        }
    }

    void handle (const juce::MidiMessage& m, int64_t sample)
    {
        if (m.isNoteOn())
            noteOn (m.getNoteNumber(), m.getFloatVelocity(), sample);
        else if (m.isNoteOff())
            noteOff (m.getNoteNumber());
        else if (m.isController())
        {
            const int cc = m.getControllerNumber();
            const double value = m.getControllerValue() / 127.0;
            if (cc == 64)
            {
                const bool down = m.getControllerValue() >= 64;
                if (sustain_ && ! down)
                    for (auto& v : voices_)
                        if (v.active && ! v.noteHeld && ! v.released)
                            releaseVoice (v);
                sustain_ = down;
            }
            else if (cc == 1)
                modTarget_ = value;
            else if (cc == 11)
                expressionTarget_ = value * value;
            else if (cc == 74)
                brightnessTarget_ = value;
            else if (cc == 120 || cc == 123)
            {
                heldCount_ = 0;
                for (auto& v : voices_)
                    if (v.active)
                    {
                        v.noteHeld = false;
                        releaseVoice (v);
                    }
            }
        }
        else if (m.isPitchWheel())
            bendTarget_ = (m.getPitchWheelValue() - 8192) / 8192.0;
    }

    Voice& allocate()
    {
        for (auto& v : voices_)
            if (! v.active)
                return v;
        Voice* oldest = nullptr;
        for (auto& v : voices_)
            if (v.released && (oldest == nullptr || v.age < oldest->age))
                oldest = &v;
        if (oldest == nullptr)
            for (auto& v : voices_)
                if (oldest == nullptr || v.age < oldest->age)
                    oldest = &v;
        return *oldest;
    }

    // 목소리의 난수는 음높이와 시작 샘플로 정한다: 앞의 음이 몇 개였든(재생 위치를 옮겨도) 같은 음은 같게 울린다
    void noteOn (int note, float velocity, int64_t sample)
    {
        if (preset_.engine == Engine::drums && preset_.kit == 2 && note == 44)
        {
            // 예고편 키트의 끊기(44): 울리는 북을 모두 20ms 안에 멈춘다 (정적을 만들 때)
            for (auto& v : voices_)
                if (v.active)
                {
                    v.choked = true;
                    v.releaseMul = decayMultiplier (0.02, sr_);
                }
            return;
        }
        double beats = 0.0;
        if (preset_.engine == Engine::drums && preset_.kit == 3 && clubDrumFor (note, beats) < 0)
            return;   // 클럽 키트에 없는 건반은 소리가 없고 울리는 목소리도 빼앗지 않는다
        if (preset_.engine == Engine::drums && (note == 42 || note == 44))
            for (auto& v : voices_)   // 닫힌 하이햇이 열린 하이햇을 끊는다
                if (v.active && v.note == 46)
                {
                    v.choked = true;
                    v.releaseMul = decayMultiplier (0.03, sr_);
                }

        if (preset_.engine == Engine::subtractive && preset_.mono && monoNoteOn (note))
            return;

        Voice& v = allocate();
        v = Voice {};
        v.active = true;
        v.noteHeld = true;
        v.note = note;
        v.velocity = std::clamp ((double) velocity, 0.0, 1.0);
        v.age = ++counter_;
        v.baseHz = midiToHz (note);
        v.rng = Random (combineSeed (combineSeed (seed_, (uint64_t) note), (uint64_t) sample));
        v.stage = Stage::attack;
        v.env = 0.0;
        v.releaseMul = decayMultiplier (preset_.release, sr_);

        const double pan = std::clamp (0.5 + preset_.spread * (note - 64) / 128.0, 0.05, 0.95);
        v.panL = std::cos (pan * 1.5707963267948966) * 1.4142135623730951;
        v.panR = std::sin (pan * 1.5707963267948966) * 1.4142135623730951;

        if (preset_.engine == Engine::piano)
            startPiano (v);
        else if (preset_.engine == Engine::pluck)
            startPluck (v);
        else if (preset_.engine == Engine::drums && preset_.kit == 3)
            startClubDrum (v, sample);
        else if (preset_.engine == Engine::drums)
            startDrum (v);
        else
        {
            for (size_t i = 0; i < v.phase.size(); ++i)
                v.phase[i] = preset_.resetPhase ? 0.0 : v.rng.uniform();
            v.filterEnvMul = std::exp (-1.0 / (preset_.filterEnvDecay * sr_));
            v.keytrack = std::pow (v.baseHz / 261.6, preset_.filterKeytrack);
            v.levelGain = std::pow (v.velocity, preset_.velocityCurve);
            v.pitchEnvSemis = preset_.pitchEnv;
            v.pitchEnvMul = pitchEnvMultiplier();
        }
    }

    double pitchEnvMultiplier() const { return std::exp (-4.6 / (std::max (0.001, preset_.pitchEnvDecay) * sr_)); }

    // 한 목소리: 놓지 않은 음이 울리고 있으면 그 목소리가 새 음으로 미끄러진다 (엔벨로프를 이어 간다).
    // 없으면 남은 소리를 10ms 에 끊고 false (새 목소리로 친다).
    bool monoNoteOn (int note)
    {
        if (heldCount_ == (int) held_.size())
        {
            std::move (held_.begin() + 1, held_.end(), held_.begin());
            --heldCount_;
        }
        held_[(size_t) heldCount_++] = note;
        for (auto& v : voices_)
            if (v.active && ! v.released)
            {
                glideTo (v, note);
                return true;
            }
        for (auto& v : voices_)
            if (v.active)
            {
                v.noteHeld = false;
                v.released = true;
                v.choked = true;
                v.stage = Stage::release;
                v.releaseMul = decayMultiplier (0.01, sr_);
            }
        return false;
    }

    // 지금 높이(남은 글라이드와 음높이 엔벨로프 포함)에서 새 음까지 glide 초에 걸쳐 곧게 옮긴다
    void glideTo (Voice& v, int note)
    {
        const double current = v.note + v.glideSemis + v.pitchEnvSemis;
        v.note = note;
        v.noteHeld = true;
        v.baseHz = midiToHz (note);
        v.keytrack = std::pow (v.baseHz / 261.6, preset_.filterKeytrack);
        v.pitchEnvSemis = 0.0;
        v.glideSemis = preset_.glide > 0.0 ? current - note : 0.0;
        v.glideStep = preset_.glide > 0.0 ? v.glideSemis / (preset_.glide * sr_) : 0.0;
    }

    void noteOff (int note)
    {
        if (preset_.engine == Engine::subtractive && preset_.mono)
        {
            // 같은 음을 겹쳐 눌렀으면 먼저 누른 것부터 놓는다
            if (auto it = std::find (held_.begin(), held_.begin() + heldCount_, note); it != held_.begin() + heldCount_)
            {
                std::move (it + 1, held_.begin() + heldCount_, it);
                --heldCount_;
            }
            for (auto& v : voices_)
                if (v.active && v.noteHeld && v.note == note)
                {
                    if (heldCount_ > 0)
                        glideTo (v, held_[(size_t) heldCount_ - 1]);   // 아직 누르고 있는 음으로 돌아간다
                    else
                    {
                        v.noteHeld = false;
                        if (! sustain_)
                            releaseVoice (v);
                    }
                    break;
                }
            return;
        }
        // 같은 음높이 중 가장 먼저 눌린 소리를 놓는다
        Voice* target = nullptr;
        for (auto& v : voices_)
            if (v.active && v.noteHeld && v.note == note && (target == nullptr || v.age < target->age))
                target = &v;
        if (target == nullptr || preset_.engine == Engine::drums)
            return;
        target->noteHeld = false;
        if (! sustain_)
            releaseVoice (*target);
    }

    void releaseVoice (Voice& v)
    {
        if (v.released)
            return;
        v.released = true;
        v.stage = Stage::release;
    }

    void startPiano (Voice& v)
    {
        const double t60 = pitchDecay (v.note, preset_.decayLowSec, preset_.decayHighSec);
        const double b = 0.00012 * std::pow (2.0, (v.note - 60) / 18.0);
        const double brightness = 0.25 + 0.75 * v.velocity;
        double sum = 0.0;
        v.partials = 0;
        for (int n = 1; n <= maxPartials; ++n)
        {
            const double r = n * std::sqrt (1.0 + b * n * n);
            if (v.baseHz * r > sr_ * 0.45)
                break;
            const int i = v.partials++;
            v.ratio[i] = r;
            v.amp[i] = std::pow ((double) n, -1.1) * std::exp (-(n - 1) * (1.25 - brightness) * 0.55);
            sum += v.amp[i];
            const double tn = t60 / (1.0 + 0.35 * (n - 1));
            v.fast[i] = 0.55;
            v.slow[i] = 0.45;
            v.fastMul[i] = decayMultiplier (tn * 0.22, sr_);
            v.slowMul[i] = decayMultiplier (tn, sr_);
            v.pc[i] = 1.0;
            v.ps[i] = 0.0;
        }
        for (int i = 0; i < v.partials; ++i)
            v.amp[i] /= std::max (1.0, sum);
        v.lastBend = -1.0;
        v.hammer = 1.0;
    }

    void startPluck (Voice& v)
    {
        const double period = sr_ / v.baseHz;
        v.lineLen = std::max (2, (int) std::floor (period - 0.5));
        const double frac = std::clamp (period - 0.5 - v.lineLen, 0.0, 0.999);
        v.apC = (1.0 - frac) / (1.0 + frac);
        v.line.assign ((size_t) v.lineLen, 0.0f);
        double lp = 0.0;
        const double smooth = 0.15 + 0.7 * (1.0 - v.velocity);
        for (auto& s : v.line)
        {
            lp += (v.rng.symmetric() - lp) * (1.0 - smooth);
            s = (float) lp;
        }
        const double t60 = pitchDecay (v.note, preset_.decayLowSec, preset_.decayHighSec);
        v.loopGain = std::pow (0.001, 1.0 / (v.baseHz * t60));
    }

    void startDrum (Voice& v)
    {
        if (preset_.kit == 2)
        {
            startTrailerDrum (v);
            return;
        }
        switch (v.note)
        {
            case 35: case 36: v.drum = 1; break;               // kick
            case 38: case 40: v.drum = 2; break;               // snare
            case 37: v.drum = 3; break;                        // side stick
            case 42: case 44: v.drum = 4; break;               // closed hat
            case 46: v.drum = 5; break;                        // open hat
            case 39: v.drum = 9; break;                        // clap
            case 49: case 57: case 52: case 55: v.drum = 6; break; // crash
            case 51: case 59: case 53: v.drum = 7; break;      // ride
            case 41: case 43: case 45: case 47: case 48: case 50: v.drum = 8; break; // toms
            default: v.drum = 3; break;
        }
        const double pans[] = { 0.5, 0.5, 0.5, 0.45, 0.62, 0.62, 0.35, 0.65, 0.5, 0.5 };
        double pan = pans[v.drum];
        if (v.drum == 8)
            pan = 0.3 + (v.note - 41) / 9.0 * 0.4;
        v.panL = std::cos (pan * 1.5707963267948966) * 1.4142135623730951;
        v.panR = std::sin (pan * 1.5707963267948966) * 1.4142135623730951;
        const double pitch = drumPitch();
        if (v.drum == 2) v.bandpass.set (1900.0 * pitch, 0.3, sr_);
        if (v.drum == 3) v.bandpass.set (1200.0 * pitch, 0.5, sr_);
        if (v.drum == 9) v.bandpass.set (1150.0 * pitch, 0.55, sr_);
        v.released = true;   // 원샷: 아래 사운드가 스스로 사라진다
        v.stage = Stage::release;
    }

    void startTrailerDrum (Voice& v)
    {
        switch (v.note)
        {
            case 35: v.drum = 20; break;            // 붐
            case 36: v.drum = 21; break;            // 큰 북
            case 38: case 40: v.drum = 22; break;   // 큰 타격
            case 41: v.drum = 23; break;            // 낮은 북
            case 43: v.drum = 24; break;            // 가운데 북
            case 45: case 47: v.drum = 25; break;   // 높은 북
            case 56: v.drum = 26; break;            // 모루
            case 39: v.drum = 9; break;             // 클랩
            default: v.drum = 21; break;
        }
        double pan = 0.5;
        if (v.drum == 23) pan = 0.38;
        if (v.drum == 25) pan = 0.62;
        if (v.drum == 26) pan = 0.68;
        v.panL = std::cos (pan * 1.5707963267948966) * 1.4142135623730951;
        v.panR = std::sin (pan * 1.5707963267948966) * 1.4142135623730951;
        const double skin[] = { 0.0, 380.0, 900.0, 430.0, 520.0, 640.0 };   // 21~25 의 가죽 소리 대역
        const double pitch = drumPitch();
        if (v.drum >= 21 && v.drum <= 25)
            v.bandpass.set (skin[v.drum - 20] * pitch, 0.3, sr_);
        if (v.drum == 9)
            v.bandpass.set (1150.0 * pitch, 0.55, sr_);
        v.released = true;
        v.stage = Stage::release;
    }

    double trailerDrum (Voice& v, double pitch, double& length)
    {
        const double t = v.time;
        auto tom = [&] (double baseHz, double drop, double decay, double skinLevel)
        {
            const double f = (baseHz + drop * std::exp (-t / 0.035)) * pitch;
            v.dPhase += f / sr_;
            double lp = 0.0, bp = 0.0;
            v.bandpass.process (v.rng.symmetric(), lp, bp);
            const double body = std::sin (twoPi * v.dPhase) * std::exp (-t / decay);
            const double hit = skinLevel * bp * std::exp (-t / 0.06);
            return std::tanh (1.4 * (body + hit)) / std::tanh (1.4);
        };
        switch (v.drum)
        {
            case 20:   // 붐: 60 Hz 에서 32 Hz 로 가라앉는 긴 저음과 둔탁한 충격
            {
                const double f = (32.0 + 28.0 * std::exp (-t / 0.25)) * pitch;
                v.dPhase += f / sr_;
                v.hpY += (v.rng.symmetric() - v.hpY) * 0.02;   // 아주 낮게 거른 잡음
                const double body = std::sin (twoPi * v.dPhase) * std::exp (-t / 1.8);
                const double thump = 3.0 * v.hpY * std::exp (-t / 0.05);
                length = 5.0;
                return std::tanh (1.6 * (body + thump)) / std::tanh (1.6);
            }
            case 21: length = 1.6; return tom (62.0, 45.0, 0.42, 0.9);
            case 23: length = 1.3; return tom (85.0, 50.0, 0.35, 0.8);
            case 24: length = 1.1; return tom (108.0, 55.0, 0.3, 0.8);
            case 25: length = 1.0; return tom (138.0, 60.0, 0.26, 0.8);
            case 22:   // 큰 타격: 짧은 몸통과 저역을 깎은 잡음의 긴 꼬리
            {
                v.dPhase += (140.0 + 40.0 * std::exp (-t / 0.03)) * pitch / sr_;
                double lp = 0.0, bp = 0.0;
                v.bandpass.process (v.rng.symmetric(), lp, bp);
                const double body = 0.7 * std::sin (twoPi * v.dPhase) * std::exp (-t / 0.25);
                const double noise = 1.0 * highpass (v, v.rng.symmetric(), 800.0 * pitch) * std::exp (-t / 0.45);
                length = 2.0;
                return std::tanh (1.3 * (body + noise + 0.6 * bp * std::exp (-t / 0.02)));
            }
            case 26:   // 모루: 서로 어긋난 배음 넷이 각자의 빠르기로 사라진다
            {
                static const double ratios[4] = { 1.0, 2.76, 5.40, 8.93 };
                static const double decays[4] = { 1.4, 0.9, 0.6, 0.4 };
                double s = 0.0;
                for (int k = 0; k < 4; ++k)
                {
                    v.phase[(size_t) k] += 620.0 * ratios[k] * pitch / sr_;
                    if (v.phase[(size_t) k] >= 1.0)
                        v.phase[(size_t) k] -= 1.0;
                    s += std::sin (twoPi * v.phase[(size_t) k]) * std::exp (-t / decays[k]) / (1.0 + k * 0.6);
                }
                if (t < 0.004)
                    s += 0.6 * v.rng.symmetric() * (1.0 - t / 0.004);
                length = 2.5;
                return 0.5 * s;
            }
            default:
                return electronicDrum (v, pitch, length);
        }
    }

    double ampEnvelope (Voice& v)
    {
        switch (v.stage)
        {
            case Stage::attack:
                v.env += 1.0 / (std::max (0.001, preset_.attack) * sr_);
                if (v.env >= 1.0)
                {
                    v.env = 1.0;
                    v.stage = preset_.decay > 0.0 ? Stage::decay : Stage::sustain;
                }
                break;
            case Stage::decay:
            {
                const double coef = std::exp (-4.6 / (preset_.decay * sr_));
                v.env = preset_.sustain + (v.env - preset_.sustain) * coef;
                if (std::abs (v.env - preset_.sustain) < 1.0e-4)
                    v.stage = Stage::sustain;
                break;
            }
            case Stage::sustain:
                v.env = preset_.sustain;
                break;
            case Stage::release:
                v.env *= v.releaseMul;
                if (v.env < 1.0e-5)
                {
                    v.stage = Stage::off;
                    v.active = false;
                }
                break;
            case Stage::off:
                v.active = false;
                break;
        }
        return v.env;
    }

    // CC74 (밝기): 127 이면 프리셋 그대로, 내릴수록 필터를 닫는다 (0 이면 6 옥타브 아래)
    double brightnessMultiplier() const { return std::pow (2.0, 6.0 * (brightness_ - 1.0)); }

    void subtractiveSample (Voice& v, double bend, double& outL, double& outR)
    {
        double vibrato = 1.0;
        if (preset_.vibratoCents > 0.0 && v.time > preset_.vibratoDelay)
        {
            const double depth = preset_.vibratoCents * std::min (1.0, (v.time - preset_.vibratoDelay) / 0.4) * (preset_.modDynamics ? (0.4 + 0.6 * mod_) : 1.0);
            vibrato = std::pow (2.0, depth * std::sin (twoPi * preset_.vibratoRate * v.time) / 1200.0);
        }
        double hz = v.baseHz * bend * vibrato;
        if (v.pitchEnvSemis != 0.0 || v.glideSemis != 0.0)
        {
            hz *= std::pow (2.0, (v.pitchEnvSemis + v.glideSemis) / 12.0);
            v.pitchEnvSemis *= v.pitchEnvMul;
            if (std::abs (v.pitchEnvSemis) < 1.0e-4)
                v.pitchEnvSemis = 0.0;
            v.glideSemis = std::abs (v.glideSemis) > std::abs (v.glideStep) ? v.glideSemis - v.glideStep : 0.0;
        }
        double mixL = 0.0, mixR = 0.0;
        for (size_t i = 0; i < preset_.oscs.size() && i < v.phase.size(); ++i)
        {
            const auto& o = preset_.oscs[i];
            const double dt = hz * o.ratio / sr_;
            const double x = o.level * oscillator (o.wave, v.phase[i], dt);
            mixL += o.pan > 0.0 ? x * (1.0 - o.pan) : x;
            mixR += o.pan < 0.0 ? x * (1.0 + o.pan) : x;
            v.phase[i] += dt;
            if (v.phase[i] >= 1.0)
                v.phase[i] -= 1.0;
        }
        if (preset_.noise > 0.0)
        {
            const double nz = preset_.noise * v.rng.symmetric();
            mixL += nz;
            mixR += nz;
        }

        if (preset_.filterBaseHz > 0.0)
        {
            if (((int) (v.time * sr_) & 15) == 0)
            {
                double cutoff = (preset_.filterBaseHz + preset_.filterEnvHz * v.filterEnv + preset_.filterVelHz * v.velocity) * v.keytrack;
                if (preset_.modDynamics)
                    cutoff *= 0.35 + 0.9 * mod_;
                cutoff *= brightnessMultiplier();
                v.filter.set (cutoff, preset_.resonance, sr_);
                v.filterR.set (cutoff, preset_.resonance, sr_);
            }
            v.filterEnv *= v.filterEnvMul;
            double lp = 0.0, bp = 0.0;
            if (preset_.filterMode == 0)
            {
                v.filter.process (mixL, lp, bp);
                mixL = lp;
                v.filterR.process (mixR, lp, bp);
                mixR = lp;
            }
            else
            {
                mixL = svfOut (v.filter, mixL, preset_.filterMode);
                mixR = svfOut (v.filterR, mixR, preset_.filterMode);
            }
        }
        if (preset_.drive > 0.0)
        {
            const double norm = std::tanh (preset_.drive);
            mixL = std::tanh (preset_.drive * mixL) / norm;
            mixR = std::tanh (preset_.drive * mixR) / norm;
        }
        double level = v.levelGain;
        if (preset_.modDynamics)
            level *= 0.45 + 0.55 * mod_;
        const double a = ampEnvelope (v) * level;
        outL = mixL * a;
        outR = mixR * a;
    }

    double pianoSample (Voice& v, double bend)
    {
        if (std::abs (bend - v.lastBend) > 1.0e-6)
        {
            for (int i = 0; i < v.partials; ++i)
            {
                const double w = twoPi * v.baseHz * v.ratio[i] * bend / sr_;
                v.cw[i] = std::cos (w);
                v.sw[i] = std::sin (w);
            }
            v.lastBend = bend;
        }
        double s = 0.0;
        for (int i = 0; i < v.partials; ++i)
        {
            const double c = v.pc[i] * v.cw[i] - v.ps[i] * v.sw[i];
            const double sn = v.pc[i] * v.sw[i] + v.ps[i] * v.cw[i];
            v.pc[i] = c;
            v.ps[i] = sn;
            v.fast[i] *= v.fastMul[i];
            v.slow[i] *= v.slowMul[i];
            s += sn * v.amp[i] * (v.fast[i] + v.slow[i]);
        }
        if ((((int) (v.time * sr_)) & 1023) == 0)
            for (int i = 0; i < v.partials; ++i)
            {
                const double r = 1.0 / std::sqrt (v.pc[i] * v.pc[i] + v.ps[i] * v.ps[i]);
                v.pc[i] *= r;
                v.ps[i] *= r;
            }
        if (v.hammer > 1.0e-4)
        {
            s += v.hammer * 0.08 * v.velocity * v.rng.symmetric();
            v.hammer *= decayMultiplier (0.012, sr_);
        }
        const double attack = std::min (1.0, v.time / 0.002);
        if (v.released)
        {
            v.env = (v.env == 0.0 ? 1.0 : v.env) * v.releaseMul;
            if (v.env < 1.0e-5)
                v.active = false;
        }
        else
            v.env = 1.0;
        if (v.slow[0] < 1.0e-5 && v.partials > 0)
            v.active = false;
        return s * attack * v.env * std::pow (v.velocity, preset_.velocityCurve);
    }

    double pluckSample (Voice& v)
    {
        const double x = v.line[(size_t) v.pos];
        const double y = 0.5 * (x + v.lastX) * v.loopGain;
        const double out = v.apC * y + v.apX - v.apC * v.apY;
        v.apX = y;
        v.apY = out;
        v.line[(size_t) v.pos] = (float) out;
        v.lastX = x;
        v.pos = (v.pos + 1) % v.lineLen;
        if (v.released)
        {
            v.env = (v.env == 0.0 ? 1.0 : v.env) * v.releaseMul;
            if (v.env < 1.0e-5)
                v.active = false;
        }
        else
            v.env = 1.0;
        if (v.time > 12.0)
            v.active = false;
        return x * v.env * std::pow (v.velocity, preset_.velocityCurve);
    }

    double highpass (Voice& v, double x, double cutoff)
    {
        const double rc = 1.0 / (twoPi * cutoff);
        const double a = rc / (rc + 1.0 / sr_);
        const double y = a * (v.hpY + x - v.hpX);
        v.hpX = x;
        v.hpY = y;
        return y;
    }

    double highpass2 (Voice& v, double x, double cutoff)
    {
        const double y1 = highpass (v, x, cutoff);
        const double rc = 1.0 / (twoPi * cutoff);
        const double a = rc / (rc + 1.0 / sr_);
        const double y = a * (v.hpY2 + y1 - v.hpX2);
        v.hpX2 = y1;
        v.hpY2 = y;
        return y;
    }

    // TR-808 의 심벌 회로처럼 여섯 사각파를 겹친 금속성 소리
    double metal (Voice& v, double pitch)
    {
        static const double freqs[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
        double s = 0.0;
        for (int k = 0; k < 6; ++k)
        {
            v.phase[(size_t) k] += freqs[k] * pitch / sr_;
            if (v.phase[(size_t) k] >= 1.0)
                v.phase[(size_t) k] -= 1.0;
            s += v.phase[(size_t) k] < 0.5 ? 1.0 : -1.0;
        }
        return s / 6.0;
    }

    double electronicDrum (Voice& v, double pitch, double& length)
    {
        const double t = v.time;
        switch (v.drum)
        {
            case 1:   // 킥: 음높이가 빨리 내려앉는 사인파, 짧은 클릭, 포화
            {
                const double f = (46.0 + 165.0 * std::exp (-t / 0.032) + 40.0 * std::exp (-t / 0.004)) * pitch;
                v.dPhase += f / sr_;
                const double amp = t < 0.012 ? 1.0 : std::exp (-(t - 0.012) / 0.24);
                double s = std::sin (twoPi * v.dPhase) * amp;
                if (t < 0.003)
                    s += 0.45 * v.rng.symmetric() * (1.0 - t / 0.003);
                length = 0.9;
                return std::tanh (1.8 * s) / std::tanh (1.8);
            }
            case 2:   // 스네어: 짧은 음과 저역을 깎은 잡음
            {
                v.dPhase += (175.0 + 30.0 * std::exp (-t / 0.02)) * pitch / sr_;
                const double tone = 0.55 * std::sin (twoPi * v.dPhase) * std::exp (-t / 0.07);
                const double nz = 0.85 * highpass (v, v.rng.symmetric(), 1200.0 * pitch) * std::exp (-t / 0.14);
                length = 0.6;
                return tone + nz;
            }
            case 9:   // 클랩: 잡음 다발 셋과 꼬리
            {
                double e = 0.0;
                for (int k = 0; k < 3; ++k)
                    if (const double tk = t - k * 0.011; tk >= 0.0)
                        e += std::exp (-tk / 0.006);
                if (t >= 0.022)
                    e += 0.9 * std::exp (-(t - 0.022) / 0.11);
                double lp = 0.0, bp = 0.0;
                v.bandpass.process (v.rng.symmetric(), lp, bp);
                length = 0.6;
                return 1.7 * bp * e;
            }
            case 4:   // 닫힌 하이햇
                length = 0.25;
                return 4.0 * highpass2 (v, 0.7 * metal (v, pitch) + 0.3 * v.rng.symmetric(), 6500.0 * pitch) * std::exp (-t / 0.035);
            case 5:   // 열린 하이햇
                length = 1.2;
                return 2.5 * highpass2 (v, 0.7 * metal (v, pitch) + 0.3 * v.rng.symmetric(), 6000.0 * pitch) * std::exp (-t / 0.28);
            case 6:   // 크래시
                length = 4.0;
                return 1.4 * highpass2 (v, 0.55 * v.rng.symmetric() + 0.45 * metal (v, pitch), 4500.0 * pitch) * std::exp (-t / 0.9) * std::min (1.0, t / 0.001);
            case 7:   // 라이드
            {
                v.dPhase += 3100.0 * pitch / sr_;
                length = 4.0;
                return (0.4 * highpass2 (v, metal (v, pitch), 3000.0 * pitch) + 0.08 * std::sin (twoPi * v.dPhase)) * std::exp (-t / 1.1);
            }
            default:
                length = 0.0;
                return 0.0;
        }
    }

    // 북의 음높이 배율: 트랙의 tune 과 지금의 피치 벤드
    double drumPitch() const { return preset_.tuneRatio * std::pow (2.0, bend_ * preset_.bendRange / 12.0); }

    void drumSample (Voice& v, double pitch, double& outL, double& outR)
    {
        const double t = v.time;
        double s = 0.0, length = 1.0, l = 0.0, r = 0.0;
        bool stereo = false;
        if (preset_.kit == 3)
            stereo = clubDrum (v, pitch, s, l, r, length);
        else if (preset_.kit == 2)
            s = trailerDrum (v, pitch, length);
        else if (preset_.kit == 1 && v.drum != 3 && v.drum != 8)
            s = electronicDrum (v, pitch, length);
        else
        switch (v.drum)
        {
            case 1:
            {
                const double f = (45.0 + 110.0 * std::exp (-t / 0.03)) * pitch;
                v.dPhase += f / sr_;
                s = std::sin (twoPi * v.dPhase) * std::exp (-t / 0.28);
                if (t < 0.004)
                    s += 0.3 * v.rng.symmetric() * (1.0 - t / 0.004);
                length = 1.2;
                break;
            }
            case 2:
            {
                v.dPhase += 185.0 * pitch / sr_;
                double lp = 0.0, bp = 0.0;
                v.bandpass.process (v.rng.symmetric(), lp, bp);
                s = 0.45 * std::sin (twoPi * v.dPhase) * std::exp (-t / 0.06) + 1.1 * bp * std::exp (-t / 0.13);
                length = 0.8;
                break;
            }
            case 3:
            {
                v.dPhase += 420.0 * pitch / sr_;
                double lp = 0.0, bp = 0.0;
                v.bandpass.process (v.rng.symmetric(), lp, bp);
                s = (0.4 * std::sin (twoPi * v.dPhase) + 0.9 * bp) * std::exp (-t / 0.018);
                length = 0.2;
                break;
            }
            case 4: s = 0.55 * highpass (v, v.rng.symmetric(), 7000.0 * pitch) * std::exp (-t / 0.035); length = 0.3; break;
            case 5: s = 0.5 * highpass (v, v.rng.symmetric(), 6500.0 * pitch) * std::exp (-t / 0.28); length = 1.5; break;
            case 6: s = 0.45 * highpass (v, v.rng.symmetric(), 4500.0 * pitch) * std::exp (-t / 1.3) * std::min (1.0, t / 0.002); length = 6.0; break;
            case 7:
            {
                v.dPhase += 3100.0 * pitch / sr_;
                s = (0.3 * highpass (v, v.rng.symmetric(), 6000.0 * pitch) + 0.12 * std::sin (twoPi * v.dPhase)) * std::exp (-t / 0.6);
                length = 3.5;
                break;
            }
            case 9:   // 클랩 (어쿠스틱 키트에서는 짧은 잡음)
            {
                double lp = 0.0, bp = 0.0;
                v.bandpass.process (v.rng.symmetric(), lp, bp);
                s = 1.2 * bp * std::exp (-t / 0.05);
                length = 0.4;
                break;
            }
            case 8:
            {
                const double baseHz = (80.0 + (v.note - 41) * 16.0) * pitch;
                const double f = baseHz * (1.0 + 0.6 * std::exp (-t / 0.04));
                v.dPhase += f / sr_;
                s = std::sin (twoPi * v.dPhase) * std::exp (-t / 0.32);
                length = 1.6;
                break;
            }
            default: break;
        }
        if (v.choked)
        {
            v.env = (v.env == 0.0 ? 1.0 : v.env) * v.releaseMul;
            s *= v.env;
            l *= v.env;
            r *= v.env;
        }
        if (t > length)
            v.active = false;
        const double g = std::pow (v.velocity, 1.5);
        if (stereo)
        {
            outL = l * g;
            outR = r * g;
        }
        else
            outL = outR = s * g;
    }

    // ---- 클럽 키트 (kit 3) ----

    enum ClubVoice { clubKick = 40, clubLongKick, clubSnare, clubRollSnare, clubClap, clubClosedHat, clubPedalHat, clubOpenHat, clubRide,
                     clubCrash, clubTom, clubImpact, clubRiser, clubDownlifter, clubReverse };

    // 클럽 키트의 건반에서 소리와 효과음의 박 수. 배치에 없는 건반은 -1.
    static int clubDrumFor (int note, double& beats)
    {
        beats = 0.0;
        switch (note)
        {
            case 35: return clubLongKick;
            case 36: return clubKick;
            case 38: return clubSnare;
            case 39: return clubClap;
            case 40: return clubRollSnare;
            case 42: return clubClosedHat;
            case 44: return clubPedalHat;
            case 46: return clubOpenHat;
            case 49: return clubCrash;
            case 51: return clubRide;
            case 41: case 43: case 45: case 47: case 48: case 50: return clubTom;
            case 60: return clubImpact;
            case 62: beats = 4.0; return clubDownlifter;
            case 63: beats = 8.0; return clubDownlifter;
            case 64: beats = 4.0; return clubRiser;
            case 65: beats = 8.0; return clubRiser;
            case 66: beats = 16.0; return clubRiser;
            case 67: beats = 1.0; return clubReverse;
            case 68: beats = 2.0; return clubReverse;
            case 69: beats = 4.0; return clubReverse;
            default: return -1;
        }
    }

    // 효과음(62~69)의 길이는 박 수다: 음이 시작하는 자리에서 템포 지도로 초를 구해 그 박에서 끝난다
    void startClubDrum (Voice& v, int64_t sample)
    {
        double beats = 0.0;
        v.drum = clubDrumFor (v.note, beats);
        if (v.drum < 0)
        {
            v.active = false;
            return;
        }
        double pan = 0.5;
        if (v.drum == clubClosedHat || v.drum == clubPedalHat || v.drum == clubOpenHat)
            pan = 0.6;
        if (v.drum == clubTom)
            pan = 0.3 + (v.note - 41) / 9.0 * 0.4;
        v.panL = std::cos (pan * 1.5707963267948966) * 1.4142135623730951;
        v.panR = std::sin (pan * 1.5707963267948966) * 1.4142135623730951;
        if (beats > 0.0)
        {
            const double start = (double) sample / sr_;
            v.fxLength = tempo_ != nullptr ? tempo_->secondsAt (tempo_->beatAt (start) + beats) - start : beats * 0.5;
        }
        const double pitch = drumPitch();
        switch (v.drum)
        {
            case clubSnare: v.bandpass.set (4200.0 * pitch, 0.25, sr_); break;
            case clubClap:
                v.filter.set (1100.0 * pitch, 0.5, sr_);
                v.filterR.set (1100.0 * pitch, 0.5, sr_);
                v.bandpass.set (2600.0 * pitch, 0.35, sr_);
                v.bandpassR.set (2600.0 * pitch, 0.35, sr_);
                break;
            case clubCrash: v.filter.set (3800.0 * pitch, 0.0, sr_); v.filterR.set (3800.0 * pitch, 0.0, sr_); break;
            case clubRide: v.filter.set (4500.0 * pitch, 0.0, sr_); v.filterR.set (4500.0 * pitch, 0.0, sr_); break;
            case clubTom: v.bandpass.set ((300.0 + (v.note - 41) * 30.0) * pitch, 0.3, sr_); break;
            case clubImpact: v.filter.set (1500.0 * pitch, 0.1, sr_); v.filterR.set (1500.0 * pitch, 0.1, sr_); break;
            case clubReverse: v.filter.set (3000.0 * pitch, 0.0, sr_); v.filterR.set (3000.0 * pitch, 0.0, sr_); break;
            default: break;
        }
        v.released = true;   // 원샷: 아래 사운드가 스스로 사라진다
        v.stage = Stage::release;
    }

    // from 초부터 to 초까지 1 에서 0 으로 (올림 코사인)
    static double fadeOut (double t, double from, double to)
    {
        if (t <= from)
            return 1.0;
        if (t >= to)
            return 0.0;
        return 0.5 * (1.0 + std::cos (3.141592653589793 * (t - from) / (to - from)));
    }

    double sine (double& phase, double hz)
    {
        const double s = std::sin (twoPi * phase);
        phase += hz / sr_;
        phase -= std::floor (phase);
        return s;
    }

    // SVF 의 저역(0), 고역(1), 대역(2, 가운데에서 0 dB) 출력
    static double svfOut (Svf& f, double x, int mode)
    {
        double lp = 0.0, bp = 0.0;
        f.process (x, lp, bp);
        if (mode == 1)
            return x - f.k * bp - lp;
        if (mode == 2)
            return f.k * bp;
        return lp;
    }

    static double dbGain (double db) { return std::exp (db * 0.11512925464970229); }

    // 클럽 키트의 한 샘플. 좌우가 다른 소리면 true (l, r), 아니면 s.
    bool clubDrum (Voice& v, double pitch, double& s, double& l, double& r, double& length)
    {
        const double t = v.time;
        const bool update = (v.ticks++ & 15) == 0;   // 쓸기 거르개의 계수는 16 샘플마다
        switch (v.drum)
        {
            case clubKick:   // 클릭(3 kHz 위 잡음 1ms)과 몸통: 852.5 Hz 에서 A1(55 Hz)으로 50ms 안에 내려앉고 0.36초에 사라진다
            {
                const double f = 55.0 * pitch * (1.0 + 4.5 * std::exp (-t / 0.030) + 10.0 * std::exp (-t / 0.003));
                const double amp = (t < 0.02 ? 1.0 : std::exp (-(t - 0.02) / 0.075)) * fadeOut (t, 0.24, 0.36);
                const double body = std::tanh (2.0 * sine (v.dPhase, f) * amp) / std::tanh (2.0);
                s = 0.92 * body + 1.0 * highpass2 (v, v.rng.symmetric(), 3000.0 * pitch) * std::exp (-t / 0.0022);
                length = 0.37;
                return false;
            }
            case clubLongKick:   // 같은 높이의 긴 몸통 (808 식), 1.35초에 사라진다
            {
                const double f = 55.0 * pitch * (1.0 + 2.0 * std::exp (-t / 0.045) + 8.0 * std::exp (-t / 0.003));
                const double amp = (t < 0.03 ? 1.0 : std::exp (-(t - 0.03) / 0.30)) * fadeOut (t, 0.85, 1.35);
                const double body = std::tanh (1.7 * sine (v.dPhase, f) * amp) / std::tanh (1.7);
                s = 0.92 * body + 0.3 * highpass2 (v, v.rng.symmetric(), 2500.0 * pitch) * std::exp (-t / 0.0015);
                length = 1.36;
                return false;
            }
            case clubSnare:   // 190 Hz 와 1.62배 음, 1.8 kHz 위 잡음과 4.2 kHz 대역, 0.3초에 사라진다
            {
                const double f = 190.0 * pitch * (1.0 + 0.5 * std::exp (-t / 0.012));
                const double tone = 0.9 * sine (v.dPhase, f) * std::exp (-t / 0.05) + 0.35 * sine (v.phase[0], f * 1.62) * std::exp (-t / 0.03);
                const double x = v.rng.symmetric();
                const double snap = t < 0.003 ? 0.6 * (1.0 - t / 0.003) : 0.0;
                const double noise = (0.5 * highpass (v, x, 1800.0 * pitch) + 0.5 * svfOut (v.bandpass, x, 2)) * (std::exp (-t / 0.075) + snap);
                s = 0.78 * (tone + noise) * fadeOut (t, 0.20, 0.30);
                length = 0.31;
                return false;
            }
            case clubRollSnare:   // 32분음표 롤용: 0.11초에 사라진다
            {
                const double f = 210.0 * pitch * (1.0 + 0.4 * std::exp (-t / 0.008));
                const double tone = 0.45 * sine (v.dPhase, f) * std::exp (-t / 0.018);
                const double noise = 0.85 * highpass (v, v.rng.symmetric(), 2200.0 * pitch) * std::exp (-t / 0.024);
                s = (tone + noise) * fadeOut (t, 0.07, 0.11);
                length = 0.115;
                return false;
            }
            case clubClap:   // 잡음 다발 셋과 꼬리를 1.1 kHz 와 2.6 kHz 두 대역으로 겹친다. 오른쪽은 1.3ms 늦고 다른 잡음이다
            {
                auto bursts = [] (double at)
                {
                    static const double starts[3] = { 0.0, 0.0085, 0.0175 };
                    static const double levels[3] = { 0.75, 0.85, 1.0 };
                    double e = 0.0;
                    for (int k = 0; k < 3; ++k)
                        if (const double tk = at - starts[k]; tk >= 0.0)
                            e += levels[k] * std::exp (-tk / 0.0045);
                    return e;
                };
                auto tail = [] (double at, double tau) { return at >= 0.026 ? std::exp (-(at - 0.026) / tau) : 0.0; };
                const double tr = t - 0.0013;
                const double common = v.rng.symmetric();
                const double nl = 0.55 * common + 0.45 * v.rng.symmetric();
                const double nr = 0.55 * common + 0.45 * v.rng.symmetric();
                const double fade = 1.9 * fadeOut (t, 0.32, 0.46);
                l = (1.5 * svfOut (v.filter, nl, 2) * (bursts (t) + 0.9 * tail (t, 0.12)) + svfOut (v.bandpass, nl, 2) * (bursts (t) + 0.5 * tail (t, 0.07))) * fade;
                r = (1.5 * svfOut (v.filterR, nr, 2) * (bursts (tr) + 0.9 * tail (tr, 0.12)) + svfOut (v.bandpassR, nr, 2) * (bursts (tr) + 0.5 * tail (tr, 0.07))) * fade;
                length = 0.47;
                return true;
            }
            case clubClosedHat:
            case clubPedalHat:   // 여섯 사각파와 잡음을 7 kHz 위로, 닫힌 하이햇 0.1초, 페달 하이햇 0.06초에 사라진다
            {
                const bool pedal = v.drum == clubPedalHat;
                const double m = metal (v, pitch);
                const double x = 0.6 * m + 0.4 * v.rng.symmetric();
                s = (pedal ? 2.0 : 2.6) * highpass2 (v, x, 7000.0 * pitch) * std::exp (-t / (pedal ? 0.012 : 0.022)) * std::min (1.0, t / 0.0005)
                    * fadeOut (t, pedal ? 0.035 : 0.06, pedal ? 0.06 : 0.10);
                length = pedal ? 0.061 : 0.101;
                return false;
            }
            case clubOpenHat:   // 0.85초에 사라진다. 닫힌 하이햇(42, 44)이 끊는다
            {
                const double m = metal (v, pitch);
                const double x = 0.6 * m + 0.4 * v.rng.symmetric();
                s = 2.0 * highpass2 (v, x, 6500.0 * pitch) * std::exp (-t / 0.16) * std::min (1.0, t / 0.0005) * fadeOut (t, 0.55, 0.85);
                length = 0.86;
                return false;
            }
            case clubCrash:   // 잡음과 사각파를 좌우 따로 3.8 kHz 위로, 3.8초에 사라진다
            {
                const double m = 0.45 * metal (v, pitch);
                const double xl = 0.55 * v.rng.symmetric() + m;
                const double xr = 0.55 * v.rng.symmetric() + m;
                const double env = 1.4 * std::exp (-t / 0.7) * std::min (1.0, t / 0.001) * fadeOut (t, 2.8, 3.8);
                l = svfOut (v.filter, xl, 1) * env;
                r = svfOut (v.filterR, xr, 1) * env;
                length = 3.81;
                return true;
            }
            case clubRide:   // 스틱 소리, 금속 배음 넷의 종소리, 4.5 kHz 위의 울림, 3.2초에 사라진다
            {
                static const double partials[4] = { 523.0, 1187.0, 2381.0, 3447.0 };
                double bell = 0.0;
                for (size_t k = 0; k < 4; ++k)
                    bell += sine (v.partialPhase[k], partials[k] * pitch) * std::exp (-t / (1.4 - 0.25 * (double) k)) / (1.0 + (double) k);
                const double ping = 0.5 * highpass (v, v.rng.symmetric(), 5000.0 * pitch) * std::exp (-t / 0.012);
                const double m = 0.5 * metal (v, pitch);
                const double xl = m + 0.5 * v.rng.symmetric();
                const double xr = m + 0.5 * v.rng.symmetric();
                const double wash = 0.35 * std::exp (-t / 0.9);
                const double fade = fadeOut (t, 2.4, 3.2);
                l = (svfOut (v.filter, xl, 1) * wash + 0.12 * bell + ping) * fade;
                r = (svfOut (v.filterR, xr, 1) * wash + 0.12 * bell + ping) * fade;
                length = 3.21;
                return true;
            }
            case clubTom:   // 41~50: 80~224 Hz, 0.8초에 사라진다
            {
                const double baseHz = (80.0 + (v.note - 41) * 16.0) * pitch;
                const double body = sine (v.dPhase, baseHz * (1.0 + 0.5 * std::exp (-t / 0.05))) * std::exp (-t / 0.2);
                const double skin = 0.5 * svfOut (v.bandpass, v.rng.symmetric(), 2) * std::exp (-t / 0.012);
                s = 0.75 * std::tanh (1.3 * (body + skin)) / std::tanh (1.3) * fadeOut (t, 0.55, 0.8);
                length = 0.81;
                return false;
            }
            case clubImpact:   // E1(41.2 Hz)로 가라앉는 서브 붐, 둔탁한 충격, 1.5 kHz 위 잡음, 3.6초에 사라진다
            {
                const double f = 41.2 * pitch * (1.0 + 1.3 * std::exp (-t / 0.11));
                const double boom = sine (v.dPhase, f) * (t < 0.04 ? 1.0 : std::exp (-(t - 0.04) / 0.85));
                v.hpY += (v.rng.symmetric() - v.hpY) * 0.02;
                const double body = std::tanh (1.5 * (boom + 2.5 * v.hpY * std::exp (-t / 0.05))) / std::tanh (1.5);
                const double crack = 0.45 * std::exp (-t / 0.2);
                const double fade = fadeOut (t, 2.6, 3.6);
                l = (body + crack * svfOut (v.filter, v.rng.symmetric(), 1)) * fade;
                r = (body + crack * svfOut (v.filterR, v.rng.symmetric(), 1)) * fade;
                length = 3.61;
                return true;
            }
            case clubRiser:   // 백색 잡음의 대역(가운데의 3분의 1 에서 1.5배)이 220 Hz 에서 12 kHz 로 오르고 크기가 30 dB 커진다. 끝 박에서 12ms 안에 멈춘다
            {
                const double len = std::max (0.05, v.fxLength), p = std::clamp (t / len, 0.0, 1.0);
                const double centre = 220.0 * std::pow (12000.0 / 220.0, std::pow (p, 1.3)) * pitch;
                if (update)
                {
                    v.filter.set (1.5 * centre, 0.35, sr_);
                    v.filterR.set (1.5 * centre, 0.35, sr_);
                    v.bandpass.set (centre / 3.0, 0.0, sr_);
                    v.bandpassR.set (centre / 3.0, 0.0, sr_);
                }
                // 대역이 넓어지는 만큼(가운데 주파수에 비례하는 힘) 덜어 크기의 곡선이 dB 로 곧다
                const double env = dbGain (-30.0 + 30.0 * std::pow (p, 1.2)) * std::sqrt (12000.0 * pitch / centre) * std::min (1.0, t / 0.01)
                                   * fadeOut (t, len - 0.012, len);
                l = 0.9 * svfOut (v.bandpass, svfOut (v.filter, v.rng.symmetric(), 0), 1) * env;
                r = 0.9 * svfOut (v.bandpassR, svfOut (v.filterR, v.rng.symmetric(), 0), 1) * env;
                length = len;
                return true;
            }
            case clubDownlifter:   // 저역 통과가 11 kHz 에서 150 Hz 로 내려가며 26 dB 작아지고, 마지막 5분의 1 동안 사라진다
            {
                const double len = std::max (0.05, v.fxLength), p = std::clamp (t / len, 0.0, 1.0);
                if (update)
                {
                    const double cutoff = 11000.0 * std::pow (150.0 / 11000.0, p) * pitch;
                    v.filter.set (cutoff, 0.35, sr_);
                    v.filterR.set (cutoff, 0.35, sr_);
                }
                const double env = dbGain (-26.0 * p) * std::min (1.0, t / 0.004) * fadeOut (t, 0.8 * len, len);
                l = 0.9 * svfOut (v.filter, v.rng.symmetric(), 0) * env;
                r = 0.9 * svfOut (v.filterR, v.rng.symmetric(), 0) * env;
                length = len;
                return true;
            }
            case clubReverse:   // 뒤집은 심벌: 크기가 끝으로 지수로 커지고(시작 -39 dB) 밝아진다, 끝 박에서 8ms 안에 멈춘다
            {
                const double len = std::max (0.05, v.fxLength), p = std::clamp (t / len, 0.0, 1.0);
                if (update)
                {
                    const double top = (5000.0 + 11000.0 * p) * pitch;
                    v.bandpass.set (top, 0.0, sr_);
                    v.bandpassR.set (top, 0.0, sr_);
                }
                const double m = 0.4 * metal (v, pitch);
                const double xl = 0.6 * v.rng.symmetric() + m;
                const double xr = 0.6 * v.rng.symmetric() + m;
                const double env = std::exp ((t - len) / (len / 4.5)) * fadeOut (t, len - 0.008, len);
                l = 1.3 * svfOut (v.bandpass, svfOut (v.filter, xl, 1), 0) * env;
                r = 1.3 * svfOut (v.bandpassR, svfOut (v.filterR, xr, 1), 0) * env;
                length = len;
                return true;
            }
            default:
                length = 0.0;
                return false;
        }
    }

    void renderRange (juce::AudioBuffer<float>& buffer, int start, int end)
    {
        if (end <= start)
            return;
        float* left = buffer.getWritePointer (0);
        float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
        const double range = preset_.bendRange;   // 피치 벤드 범위 (반음)
        for (int i = start; i < end; ++i)
        {
            bend_ += (bendTarget_ - bend_) * smoothing_;
            mod_ += (modTarget_ - mod_) * smoothing_;
            expression_ += (expressionTarget_ - expression_) * smoothing_;
            brightness_ += (brightnessTarget_ - brightness_) * smoothing_;
            const double bendFactor = std::pow (2.0, bend_ * range / 12.0);
            const double drumPitchNow = preset_.tuneRatio * bendFactor;

            double l = 0.0, r = 0.0;
            for (auto& v : voices_)
            {
                if (! v.active)
                    continue;
                double s = 0.0, sl = 0.0, sr = 0.0;
                switch (preset_.engine)
                {
                    case Engine::subtractive: subtractiveSample (v, bendFactor, sl, sr); break;
                    case Engine::piano: s = pianoSample (v, bendFactor); sl = sr = s; break;
                    case Engine::pluck: s = pluckSample (v); sl = sr = s; break;
                    case Engine::drums: drumSample (v, drumPitchNow, sl, sr); break;
                }
                v.time += 1.0 / sr_;
                l += sl * v.panL;
                r += sr * v.panR;
            }
            const double g = preset_.gain * expression_;
            left[i] += (float) (l * g);
            if (right != nullptr)
                right[i] += (float) (r * g);
        }
    }

    Preset base_;     // 표의 프리셋 그대로 (값을 바꿀 때 여기서 다시 만든다)
    Preset preset_;   // 지금 쓰는 프리셋 (오디오 스레드)
    Handoff<Preset> next_;
    uint64_t seed_ = 0;
    std::shared_ptr<const TempoMap> tempo_;   // 박 단위 효과음의 길이 (없으면 120 BPM)
    double sr_ = 48000.0;
    double smoothing_ = 0.01;
    std::array<Voice, maxVoices> voices_ {};
    uint64_t counter_ = 0;
    bool sustain_ = false;
    std::array<int, 16> held_ {};   // mono: 누르고 있는 음 (누른 순서)
    int heldCount_ = 0;
    double bend_ = 0.0, bendTarget_ = 0.0;
    double mod_ = 100.0 / 127.0, modTarget_ = 100.0 / 127.0;
    double expression_ = 1.0, expressionTarget_ = 1.0;
    double brightness_ = 1.0, brightnessTarget_ = 1.0;
};

} // namespace

std::vector<std::string> simpleSynthPresets()
{
    std::vector<std::string> names;
    for (const auto& [name, preset] : presets())
        names.push_back (name);
    return names;
}

std::string synthEngineName (const std::string& preset)
{
    const auto it = presets().find (preset);
    if (it == presets().end())
        return {};
    switch (it->second.engine)
    {
        case Engine::subtractive: return "subtractive";
        case Engine::piano: return "piano";
        case Engine::pluck: return "pluck";
        case Engine::drums: break;
    }
    return "drums";
}

std::vector<ParameterInfo> synthParameters (const std::string& preset, const SynthValues* base)
{
    const auto it = presets().find (preset);
    if (it == presets().end())
        return {};
    auto table = layoutFor (it->second.engine);
    for (auto& p : table)
    {
        p.defaultValue = presetValue (it->second, p.key);
        if (base != nullptr)
            if (const auto b = base->find (p.key); b != base->end())
                p.defaultValue = b->second;
    }
    return table;
}

const ParameterInfo* findSynthParameter (const std::string& key)
{
    for (const auto e : { Engine::subtractive, Engine::piano, Engine::pluck, Engine::drums })
        for (const auto& p : layoutFor (e))
            if (p.key == key)
                return &p;
    return nullptr;
}

std::vector<std::string> synthParameterKeys()
{
    std::vector<std::string> keys;
    for (const auto e : { Engine::subtractive, Engine::piano, Engine::pluck, Engine::drums })
        for (const auto& p : layoutFor (e))
            if (std::find (keys.begin(), keys.end(), p.key) == keys.end())
                keys.push_back (p.key);
    return keys;
}

std::vector<float> synthWaveform (const std::string& preset, const SynthValues& values, int points)
{
    const auto it = presets().find (preset);
    std::vector<float> out;
    if (it == presets().end() || it->second.engine != Engine::subtractive || points < 2)
        return out;
    const auto p = applyValues (it->second, values);
    // 옥타브는 반영하고 디튠은 뺀다 (어긋난 오실레이터는 주기가 맞지 않는다). 한 옥타브 아래 오실레이터가 있으면 주기가 두 배다.
    double lowest = 0.0;
    for (const auto& o : p.oscs)
        lowest = std::min (lowest, o.octave);
    const double cycles = std::pow (2.0, -lowest);
    float peak = 1.0e-6f;
    for (int i = 0; i < points; ++i)
    {
        const double t = cycles * (double) i / (double) points;
        double s = 0.0;
        for (const auto& o : p.oscs)
        {
            const double phase = std::fmod (t * std::pow (2.0, o.octave), 1.0);
            s += o.level * oscillator (o.wave, phase, 0.0);
        }
        out.push_back ((float) s);
        peak = std::max (peak, std::abs ((float) s));
    }
    for (auto& v : out)
        v /= peak;
    return out;
}

std::optional<double> synthChoiceValue (const ParameterInfo& p, const std::string& name)
{
    for (size_t i = 0; i < p.choiceNames.size() && i < p.choices.size(); ++i)
        if (p.choiceNames[i] == name)
            return p.choices[i];
    return std::nullopt;
}

// 곡과 레지스트리의 신스 값 하나: 수, 또는 고르는 매개변수의 이름 (filterMode: "highpass")
std::optional<double> readSynthValue (json::ObjectReader& r, const std::string& key, const ParameterInfo& p, Diagnostics& d)
{
    const auto* v = r.get (key);
    if (v != nullptr && v->isString() && ! p.choiceNames.empty())
    {
        if (auto value = synthChoiceValue (p, v->toString().toStdString()))
            return value;
        std::string names;
        for (const auto& n : p.choiceNames)
            names += (names.empty() ? "" : ", ") + n;
        d.error (r.pathOf (key), key + " must be one of " + names, "e.g. \"" + key + "\": \"" + p.choiceNames.back() + "\"");
        return std::nullopt;
    }
    auto n = r.number (key, true, p.minimum, p.maximum);
    if (n && ! p.choices.empty() && std::find (p.choices.begin(), p.choices.end(), *n) == p.choices.end())
    {
        d.error (r.pathOf (key), key + " must be one of its choices", p.choiceNames.empty() ? std::string {} : "use the names, e.g. \"" + p.choiceNames.back() + "\"");
        return std::nullopt;
    }
    return n;
}


std::vector<double> synthFilterResponseDb (double cutoffHz, double resonance, double sampleRate, const std::vector<double>& frequenciesHz, int mode)
{
    // TPT SVF = 아날로그 1/(s^2 + k s + 1) (저역), s^2/(...) (고역), k s/(...) (대역) 의 쌍선형 변환 (Svf::set 과 같은 g, k)
    const double fc = std::clamp (cutoffHz, 20.0, sampleRate * 0.45);
    const double g = std::tan (3.141592653589793 * fc / sampleRate);
    const double k = 2.0 - 2.0 * std::clamp (resonance, 0.0, 0.95);
    std::vector<double> out;
    for (const double f : frequenciesHz)
    {
        const double w = twoPi * std::clamp (f, 1.0, sampleRate * 0.5) / sampleRate;
        const std::complex<double> z1 = std::polar (1.0, -w);
        const auto num = mode == 1 ? (1.0 - z1) * (1.0 - z1) : mode == 2 ? g * k * (1.0 - z1 * z1) : g * g * (1.0 + z1) * (1.0 + z1);
        const auto den = (1.0 + g * k + g * g) + 2.0 * (g * g - 1.0) * z1 + (1.0 - g * k + g * g) * z1 * z1;
        out.push_back (20.0 * std::log10 (std::max (1.0e-12, std::abs (num / den))));
    }
    return out;
}

std::unique_ptr<Instrument> makeSimpleSynth (const std::string& preset, uint64_t seed, std::string& error, const SynthValues* base,
                                             const SynthValues* track, const TempoMap* tempo)
{
    const auto& table = presets();
    auto it = table.find (preset);
    if (it == table.end())
    {
        error = "unknown synth preset '" + preset + "'";
        return nullptr;
    }
    SynthValues values;
    if (base != nullptr)
        values = *base;
    if (track != nullptr)
        for (const auto& [key, value] : *track)
            values[key] = value;
    return std::make_unique<SimpleSynth> (it->second, values, seed, tempo);
}

} // namespace mr
