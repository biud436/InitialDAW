#include "TestFramework.h"
#include "TestUtil.h"

#include "composition/Document.h"
#include "core/Random.h"
#include "effects/Automation.h"
#include "effects/Effects.h"
#include "midi/MidiExport.h"

#include <cmath>

using namespace mr;

namespace
{

constexpr double fs = 48000.0;
constexpr double pi = 3.141592653589793;

EffectSpec spec (const std::string& json)
{
    EffectSpec e;
    e.params = juce::JSON::parse (juce::String::fromUTF8 (json.c_str()));
    e.type = e.params.getProperty ("type", "").toString().toStdString();
    e.path = "$.test";
    return e;
}

juce::AudioBuffer<float> sine (double freq, double amplitude, double seconds)
{
    juce::AudioBuffer<float> b (2, (int) (fs * seconds));
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float v = (float) (amplitude * std::sin (2.0 * pi * freq * i / fs));
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    return b;
}

juce::AudioBuffer<float> constant (float value, int samples)
{
    juce::AudioBuffer<float> b (2, samples);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (b.getWritePointer (ch), value, samples);
    return b;
}

juce::AudioBuffer<float> noise (uint64_t seed, int samples)
{
    Random rng (seed);
    juce::AudioBuffer<float> b (2, samples);
    for (int i = 0; i < samples; ++i)
    {
        b.setSample (0, i, (float) rng.gaussian() * 0.25f);
        b.setSample (1, i, (float) rng.gaussian() * 0.2f);
    }
    return b;
}

// 블록 단위로 사슬을 지난다. from 은 곡의 몇째 샘플에서 시작하는지 (버퍼의 0 번 샘플이 곡의 0 번 샘플)
void processBlocks (EffectChain& chain, juce::AudioBuffer<float>& b, int from, int to, int block)
{
    juce::AudioBuffer<float> work (2, block);
    for (int start = from; start < to; start += block)
    {
        const int n = std::min (block, to - start);
        for (int ch = 0; ch < 2; ++ch)
            work.copyFrom (ch, 0, b, ch, start, n);
        chain.process (work, n, start);
        for (int ch = 0; ch < 2; ++ch)
            b.copyFrom (ch, start, work, ch, 0, n);
    }
}

juce::AudioBuffer<float> processed (const std::string& json, juce::AudioBuffer<float> b, int block, const TempoMap* tempo = nullptr, int from = 0)
{
    auto chain = createEffectChain ({ spec (json) }, mrtest::sourceDir(), fs, block, tempo);
    processBlocks (chain, b, from, b.getNumSamples(), block);
    return b;
}

double maxDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from = 0, int to = -1)
{
    double d = 0.0;
    const int end = to < 0 ? std::min (a.getNumSamples(), b.getNumSamples()) : to;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = from; i < end; ++i)
            d = std::max (d, (double) std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
    return d;
}

double rmsDb (const juce::AudioBuffer<float>& b, double fromSec, double toSec)
{
    const int from = (int) (fromSec * fs), count = (int) ((toSec - fromSec) * fs);
    return juce::Decibels::gainToDecibels ((double) b.getRMSLevel (0, from, count), -200.0);
}

std::string errorsOf (const std::string& json)
{
    std::string all;
    for (const auto& e : mrtest::parse (json).diagnostics.errors)
        all += e.path + " " + e.message + " | " + e.hint + "\n";
    return all;
}

bool contains (const std::string& text, const std::string& fragment)
{
    if (text.find (fragment) != std::string::npos)
        return true;
    std::fprintf (stderr, "    missing \"%s\" in:\n%s", fragment.c_str(), text.c_str());
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// 곡선
// ---------------------------------------------------------------------------

TEST (sample_lanes_follow_point_shapes_on_the_grid)
{
    // 120 BPM: 한 박이 24000 샘플 (32 의 배수라 박이 격자 점에 놓인다)
    auto tempo = std::make_shared<const TempoMap> (std::vector<TempoPoint> { { 0.0, 120.0, false } });
    auto lane = [] (const std::string& curve)
    {
        AutomationLane l;
        AutomationPoint a { 1.0, -24.0, CurveShape::linear }, b { 3.0, 0.0, CurveShape::linear };
        if (curve == "step") a.curve = CurveShape::step;
        if (curve == "smooth") a.curve = CurveShape::smooth;
        if (curve == "exponential") a.curve = CurveShape::exponential;
        l.points = { a, b };
        return l;
    };
    const SampleLane linear (lane ("linear"), tempo, fs, false), step (lane ("step"), tempo, fs, false), smooth (lane ("smooth"), tempo, fs, false),
        exponential (lane ("exponential"), tempo, fs, false);
    CHECK_NEAR (linear.atGrid (0), -24.0, 1e-12);            // 첫 점 앞은 첫 값
    CHECK_NEAR (linear.atGrid (48000), -12.0, 1e-12);        // 2박 (가운데)
    CHECK_NEAR (linear.atGrid (24000 * 10), 0.0, 1e-12);     // 마지막 점 뒤는 마지막 값
    CHECK_NEAR (step.atGrid (48000), -24.0, 1e-12);
    CHECK_NEAR (step.atGrid (72000), 0.0, 1e-12);
    CHECK_NEAR (smooth.atGrid (36000), -24.0 + 24.0 * (0.25 * 0.25 * (3.0 - 0.5)), 1e-9);
    CHECK_NEAR (exponential.atGrid (48000), -12.0, 1e-12);   // 음수 값의 지수 곡선은 직선
    // 격자 점 사이는 직선
    const int64_t g = 48000 + 32 * 7;
    CHECK_NEAR (linear.at (g + 8), linear.atGrid (g) + (linear.atGrid (g + 32) - linear.atGrid (g)) * 0.25, 1e-12);
    CHECK_EQ (automationGridStart (-1), (int64_t) -32);
    CHECK_EQ (automationGridStart (31), (int64_t) 0);
    CHECK_EQ (automationGridStart (32), (int64_t) 32);

    // 로그 매개변수: linear 는 옥타브마다 같은 시간 (100 Hz 에서 1600 Hz 로 4박, 2박에 400 Hz)
    AutomationLane hz;
    hz.points = { { 0.0, 100.0, CurveShape::linear }, { 4.0, 1600.0, CurveShape::linear } };
    const SampleLane cutoff (hz, tempo, fs, true);
    CHECK_NEAR (cutoff.atGrid (48000), 400.0, 1e-9);
    CHECK_NEAR (cutoff.atGrid (24000), 200.0, 1e-9);
    const SampleLane plain (hz, tempo, fs, false);
    CHECK_NEAR (plain.atGrid (48000), 850.0, 1e-9);

    // 템포 지도를 따른다: 60 BPM 으로 바뀐 뒤의 한 박은 48000 샘플
    auto slower = std::make_shared<const TempoMap> (std::vector<TempoPoint> { { 0.0, 120.0, false }, { 2.0, 60.0, false } });
    AutomationLane ramp;
    ramp.points = { { 2.0, 0.0, CurveShape::linear }, { 4.0, 10.0, CurveShape::linear } };
    const SampleLane tempoAware (ramp, slower, fs, false);
    CHECK_NEAR (tempoAware.atGrid (48000 + 48000), 5.0, 1e-9);   // 2박(1초) 뒤 1초 = 3박
}

TEST (automated_gain_is_block_size_independent_and_continues_across_swaps)
{
    const TempoMap tempo ({ { 0.0, 128.0, false } });
    const char* json = R"({ "type": "gain", "gainDb": 0, "automation": { "gainDb": [
        { "beat": 0.5, "value": 0 }, { "beat": 2, "value": -30, "curve": "smooth" }, { "beat": 3, "value": -6, "curve": "step" }, { "beat": 3.5, "value": 6 } ] } })";
    const auto input = noise (3, 96000);
    const auto a = processed (json, input, 512, &tempo), b = processed (json, input, 64, &tempo), c = processed (json, input, 300, &tempo);
    CHECK_EQ (maxDifference (a, b), 0.0);
    CHECK_EQ (maxDifference (a, c), 0.0);
    // 2박 (0.9375 초) 은 -30 dB, 3.5박 뒤는 +6 dB
    const int at2 = (int) std::llround (2.0 * 60.0 / 128.0 * fs);
    CHECK_NEAR (a.getSample (0, at2) / input.getSample (0, at2), std::pow (10.0, -30.0 / 20.0), 1e-4);
    const int at4 = (int) std::llround (4.0 * 60.0 / 128.0 * fs);
    CHECK_NEAR (a.getSample (1, at4) / input.getSample (1, at4), std::pow (10.0, 6.0 / 20.0), 1e-4);

    // 처리 도중 같은 매개변수의 새 사슬로 바꿔 끼워도 같다
    auto swapped = input;
    auto first = createEffectChain ({ spec (json) }, mrtest::sourceDir(), fs, 512, &tempo);
    processBlocks (first, swapped, 0, 40000, 512);
    auto second = createEffectChain ({ spec (json) }, mrtest::sourceDir(), fs, 512, &tempo);
    second.continueFrom (first);
    processBlocks (second, swapped, 40000, swapped.getNumSamples(), 512);
    CHECK_EQ (maxDifference (a, swapped), 0.0);
}

TEST (effect_automation_is_validated)
{
    auto errorsFor = [] (const std::string& effectJson)
    {
        return errorsOf (R"({ "tempo": 120, "tracks": [ { "name": "A", "instrument": "synth.sine", "notes": [],
            "effects": [ )" + effectJson + R"( ] } ] })");
    };
    CHECK (contains (errorsFor (R"({ "type": "filter", "automation": { "cutof": [ { "beat": 0, "value": 100 } ] } })"),
                     "$.tracks[0].effects[0].automation.cutof unknown parameter 'cutof' for filter | did you mean 'cutoffHz'?"));
    CHECK (contains (errorsFor (R"({ "type": "filter", "automation": { "slope": [ { "beat": 0, "value": 12 } ] } })"),
                     "'slope' cannot be automated | automatable: cutoffHz, resonance, mix"));
    CHECK (contains (errorsFor (R"({ "type": "filter", "automation": { "mode": [ { "beat": 0, "value": 1 } ] } })"),
                     "automation.mode 'mode' cannot be automated"));
    CHECK (contains (errorsFor (R"({ "type": "compressor", "automation": { "ratio": [ { "beat": 0, "value": 2 } ] } })"),
                     "compressor has no automatable parameters"));
    CHECK (contains (errorsFor (R"({ "type": "filter", "automation": { "cutoffHz": [ { "beat": 0, "value": 10 } ] } })"),
                     "automation.cutoffHz[0].value"));
    CHECK (contains (errorsFor (R"({ "type": "gain", "automation": { "gainDb": [ { "beat": 4, "value": 0 }, { "beat": 2, "value": -6 } ] } })"),
                     "ascending beat order"));
    CHECK (contains (errorsFor (R"({ "type": "gain", "automation": { "gainDb": [ { "beat": 0, "value": 0, "curve": "sine" } ] } })"),
                     "unknown curve 'sine'"));
    CHECK (contains (errorsFor (R"({ "type": "gain", "automation": { "gainDb": { "beat": 0 } } })"), "must be an array of points"));
    CHECK (contains (errorsFor (R"({ "type": "filter", "mode": "bandstop" })"), "filter mode must be lowpass, highpass, bandpass or notch"));
    CHECK (contains (errorsFor (R"({ "type": "filter", "slope": 18 })"), "slope must be 12 or 24"));
    CHECK (contains (errorsFor (R"({ "type": "filter", "slope": 12.5 })"), "slope must be 12 or 24"));
    // 박에 맞춘 딜레이가 가장 느린 템포에서 16초를 넘으면 읽을 때 알린다 (렌더를 만들 때 거절하기 전에)
    CHECK (contains (errorsOf (R"({ "tempo": [ { "beat": 0, "bpm": 120, "ramp": true }, { "beat": 8, "bpm": 40 } ],
        "master": { "effects": [ { "type": "delay", "timeBeats": 16 } ] }, "tracks": [ { "name": "A", "instrument": "synth.sine", "notes": [] } ] })"),
                     "$.master.effects[0].timeBeats the echo would be 24 s long at the slowest tempo (at most 16 s)"));
    CHECK (errorsOf (R"({ "tempo": 40, "tracks": [ { "name": "A", "instrument": "synth.sine", "notes": [], "effects": [ { "type": "delay", "timeBeats": 10 } ] } ] })").empty());
    const auto both = mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "A", "instrument": "synth.sine", "notes": [],
        "effects": [ { "type": "delay", "timeMs": 300, "timeBeats": 0.5 } ] } ] })");
    CHECK (both.diagnostics.ok());
    CHECK (mrtest::hasWarning (both.diagnostics, "$.tracks[0].effects[0].timeMs", "remove one"));
    CHECK (contains (errorsFor (R"({ "type": "ducker", "rateBeats": 0 })"), "rateBeats"));
    // 받는 것: 버스와 마스터의 곡선, 센드 곡선
    const auto good = mrtest::parse (R"({ "tempo": 120, "buses": [ { "name": "fx", "effects": [ { "type": "filter", "mode": "highpass",
        "automation": { "cutoffHz": [ { "beat": 0, "value": 20 }, { "beat": 8, "value": 800, "curve": "exponential" } ] } } ] } ],
      "master": { "effects": [ { "type": "gain", "automation": { "gainDb": [ { "beat": 0, "value": 0 }, { "beat": 4, "value": -96 } ] } },
                               { "type": "ducker", "automation": { "depthDb": [ { "beat": 0, "value": 0 }, { "beat": 4, "value": -12, "curve": "step" } ] } } ] },
      "tracks": [ { "name": "A", "instrument": "synth.sine", "notes": [],
        "sends": [ { "bus": "fx", "levelDb": -6, "automation": { "levelDb": [ { "beat": 0, "value": -120 }, { "beat": 2, "value": 0 } ] } } ] } ] })");
    CHECK (good.diagnostics.ok());
    CHECK (good.diagnostics.warnings.empty());
    REQUIRE (good.composition.has_value());
    CHECK_EQ (good.composition->tracks[0].sends[0].levelLane.points.size(), (size_t) 2);
    CHECK (contains (errorsOf (R"({ "tempo": 120, "buses": [ { "name": "fx" } ], "tracks": [ { "name": "A", "instrument": "synth.sine", "notes": [],
        "sends": [ { "bus": "fx", "automation": { "preFader": [], "levelDb": [ { "beat": 0, "value": 20 } ] } } ] } ] })"),
                     "a send can automate only levelDb, not 'preFader'"));
    CHECK (contains (errorsOf (R"({ "tempo": 120, "buses": [ { "name": "fx" } ], "tracks": [ { "name": "A", "instrument": "synth.sine", "notes": [],
        "sends": [ { "bus": "fx", "automation": { "levelDb": [ { "beat": 0, "value": 20 } ] } } ] } ] })"),
                     "sends[0].automation.levelDb[0].value"));
    // 마스터 압축기의 키도 있는 트랙이어야 한다
    CHECK (contains (errorsOf (R"({ "tempo": 120, "master": { "effects": [ { "type": "compressor", "sidechain": "Kik" } ] },
        "tracks": [ { "name": "Kick", "instrument": "synth.edm-drums", "notes": [] } ] })"),
                     "$.master.effects[0].sidechain unknown track 'Kik'"));
}

// ---------------------------------------------------------------------------
// filter
// ---------------------------------------------------------------------------

TEST (filter_matches_its_analytic_response)
{
    const std::vector<double> freqs { 60.0, 200.0, 700.0, 1000.0, 1400.0, 4000.0, 9000.0 };
    int checked = 0;
    for (const char* mode : { "lowpass", "highpass", "bandpass", "notch" })
        for (int slope : { 12, 24 })
            for (double resonance : { 0.0, 0.5 })
            {
                const auto json = "{ \"type\": \"filter\", \"mode\": \"" + std::string (mode) + "\", \"slope\": " + std::to_string (slope)
                                  + ", \"cutoffHz\": 1000, \"resonance\": " + std::to_string (resonance) + " }";
                const auto response = filterResponseDb (juce::JSON::parse (juce::String (json)), fs, freqs);
                for (size_t i = 0; i < freqs.size(); ++i)
                {
                    if (response[i] < -60.0)
                        continue;   // 아주 깊은 곳은 잡음이 재기를 흐린다
                    const auto in = sine (freqs[i], 0.25, 1.0);
                    const auto out = processed (json, in, 512);
                    const int from = 24000, count = 12000;
                    const double gain = juce::Decibels::gainToDecibels ((double) out.getRMSLevel (0, from, count) / in.getRMSLevel (0, from, count));
                    if (! CHECK_NEAR (gain, response[i], 0.05))
                        std::fprintf (stderr, "    %s at %.0f Hz\n", json.c_str(), freqs[i]);
                    ++checked;
                }
            }
    CHECK (checked > 80);
    // 공진 0 의 저역과 고역 통과는 차단 주파수에서 -3 dB (12 와 24 모두), 대역 통과는 가운데에서 0 dB, 노치는 깊게 판다
    auto at1k = [] (const std::string& json) { return filterResponseDb (juce::JSON::parse (juce::String (json)), fs, { 1000.0 })[0]; };
    CHECK_NEAR (at1k (R"({ "type": "filter", "mode": "lowpass", "cutoffHz": 1000 })"), -3.0103, 0.001);
    CHECK_NEAR (at1k (R"({ "type": "filter", "mode": "lowpass", "slope": 24, "cutoffHz": 1000 })"), -3.0103, 0.001);
    CHECK_NEAR (at1k (R"({ "type": "filter", "mode": "highpass", "slope": 24, "cutoffHz": 1000 })"), -3.0103, 0.001);
    CHECK_NEAR (at1k (R"({ "type": "filter", "mode": "bandpass", "cutoffHz": 1000, "resonance": 0.7 })"), 0.0, 0.001);
    CHECK (at1k (R"({ "type": "filter", "mode": "notch", "cutoffHz": 1000 })") < -100.0);
    // 공진 1 은 Q 10: 12 dB 저역 통과의 봉우리가 +20 dB
    CHECK_NEAR (at1k (R"({ "type": "filter", "mode": "lowpass", "cutoffHz": 1000, "resonance": 1 })"), 20.0, 0.01);
    // mix 0 은 원래 소리
    const auto in = noise (5, 20000);
    CHECK (maxDifference (processed (R"({ "type": "filter", "mode": "highpass", "cutoffHz": 5000, "mix": 0 })", in, 512), in) < 1e-7);
}

TEST (filter_sweep_attenuates_and_has_no_steps)
{
    // 120 BPM, 고역 통과를 0박 20 Hz 에서 8박(4초) 1200 Hz 로: 100 Hz 사인이 끝에서 24 dB 넘게 줄어든다
    const TempoMap tempo ({ { 0.0, 120.0, false } });
    const char* json = R"({ "type": "filter", "mode": "highpass", "slope": 24, "cutoffHz": 20,
        "automation": { "cutoffHz": [ { "beat": 0, "value": 20 }, { "beat": 8, "value": 1200 } ] } })";
    const auto in = sine (100.0, 0.5, 5.0);
    const auto out = processed (json, in, 512, &tempo);
    CHECK (rmsDb (out, 0.2, 0.4) > rmsDb (in, 0.2, 0.4) - 0.5);
    CHECK (rmsDb (out, 4.2, 4.8) < rmsDb (in, 4.2, 4.8) - 24.0);
    // 블록 크기와 상관없이 같다
    CHECK_EQ (maxDifference (out, processed (json, in, 64, &tempo)), 0.0);
    CHECK_EQ (maxDifference (out, processed (json, in, 300, &tempo)), 0.0);
    // 계단 소리가 없다: 샘플 사이 차이가 입력의 가장 큰 차이를 넘지 않는다
    double inStep = 0.0, outStep = 0.0;
    for (int i = 4800; i < in.getNumSamples(); ++i)   // 사인이 0 에서 갑자기 시작하는 첫 0.1초는 뺀다
    {
        inStep = std::max (inStep, (double) std::abs (in.getSample (0, i) - in.getSample (0, i - 1)));
        outStep = std::max (outStep, (double) std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
    }
    CHECK (outStep <= inStep * 1.02);
    // 차단 주파수의 중간(2.5 옥타브 위, 4박)에서 응답은 고정 필터와 같다
    const char* fixed = R"({ "type": "filter", "mode": "highpass", "slope": 24, "cutoffHz": 154.92 })";
    const double expected = filterResponseDb (juce::JSON::parse (fixed), fs, { 100.0 })[0];
    CHECK_NEAR (rmsDb (out, 1.98, 2.02) - rmsDb (in, 1.98, 2.02), expected, 0.6);
}

// ---------------------------------------------------------------------------
// ducker
// ---------------------------------------------------------------------------

TEST (ducker_gain_matches_the_closed_form)
{
    // 120 BPM, 한 박마다: 1 ms 에 -12 dB 로, 30 ms 머물고, 0.5박(250 ms) 동안 (1 - r)^2 로 돌아온다
    const TempoMap tempo ({ { 0.0, 120.0, false } });
    const char* json = R"({ "type": "ducker", "rateBeats": 1, "depthDb": -12, "attackMs": 1, "holdMs": 30, "releaseBeats": 0.5, "curve": 2 })";
    const auto out = processed (json, constant (0.5f, 96000), 512, &tempo);
    auto expectedDb = [] (double t)
    {
        const double u = std::fmod (t, 0.5);
        if (u < 0.001) return -12.0 * u / 0.001;
        if (u < 0.031) return -12.0;
        const double r = (u - 0.031) / 0.25;
        return r >= 1.0 ? 0.0 : -12.0 * (1.0 - r) * (1.0 - r);
    };
    double worst = 0.0;
    for (int i = 0; i < 96000; i += 37)
    {
        const double db = juce::Decibels::gainToDecibels ((double) out.getSample (0, i) / 0.5, -200.0);
        worst = std::max (worst, std::abs (db - expectedDb (i / fs)));
    }
    CHECK (worst < 1.0e-3);
    CHECK_NEAR (juce::Decibels::gainToDecibels (out.getSample (1, 24000 + 480) / 0.5), -12.0, 1e-4);   // 박 뒤 10 ms
    CHECK_NEAR (out.getSample (0, 24000 + 18000) / 0.5, 1.0, 1e-6);                                    // 박 뒤 375 ms 는 열려 있다
    // offsetBeats 0.5: 엇박에서 누른다. mix 0.5 는 원래 소리와 반씩
    const auto offbeat = processed (R"({ "type": "ducker", "depthDb": -12, "offsetBeats": 0.5, "mix": 0.5 })", constant (0.5f, 48000), 512, &tempo);
    CHECK_NEAR (offbeat.getSample (0, 9600) / 0.5, 1.0, 1e-6);   // 0.4박은 열려 있다 (-0.5박의 주기는 0.031초에 돌아왔다)
    CHECK_NEAR (offbeat.getSample (0, 12000 + 480) / 0.5, 0.5 + 0.5 * std::pow (10.0, -12.0 / 20.0), 1e-4);
    // 앞 주기가 아직 돌아오는 중이면 깊은 쪽: 주기 0.25박, 돌아오기 1박이면 박마다 바닥에서 시작하지 않는다
    const auto overlap = processed (R"({ "type": "ducker", "rateBeats": 0.25, "depthDb": -12, "attackMs": 5, "holdMs": 0, "releaseBeats": 1 })",
                                    constant (0.5f, 48000), 512, &tempo);
    double maxStep = 0.0;
    for (int i = 1; i < 48000; ++i)
        maxStep = std::max (maxStep, (double) std::abs (overlap.getSample (0, i) - overlap.getSample (0, i - 1)));
    CHECK (maxStep < 0.01);   // 주기의 머리에서 0 dB 로 튀지 않는다
}

TEST (ducker_follows_tempo_ramps_and_is_exact_under_seeking)
{
    // 100 BPM 에서 160 BPM 으로 8박 동안 빨라진다: 누르는 자리는 박의 시각, 돌아오는 길이는 박으로 잰다
    const TempoMap tempo ({ { 0.0, 100.0, true }, { 8.0, 160.0, false } });
    const char* json = R"({ "type": "ducker", "rateBeats": 1, "depthDb": -18, "attackMs": 0.5, "holdMs": 10, "releaseBeats": 0.5, "curve": 1.5 })";
    const int total = 5 * 48000;
    const auto whole = processed (json, constant (0.25f, total), 512, &tempo);
    for (int beat = 1; beat < 8; ++beat)
    {
        const auto at = tempo.sampleAt (beat, fs);
        CHECK (whole.getSample (0, (int) at - 2) > 0.249f);                                    // 박 바로 앞은 열려 있다
        CHECK_NEAR (juce::Decibels::gainToDecibels (whole.getSample (0, (int) at + 240) / 0.25), -18.0, 1e-3);   // 박 뒤 5 ms
    }
    // 처리 결과는 duckerShape 와 같다
    const auto params = juce::JSON::parse (json);
    for (int i = 1000; i < total; i += 4321)
        CHECK_NEAR (whole.getSample (0, i) / 0.25, std::pow (10.0, -18.0 * duckerShape (params, tempo, fs, i) / 20.0), 1e-5);
    // 가운데에서 새로 시작한 효과(다른 블록 크기)가 같은 샘플을 낸다
    const int from = 77777;
    const auto late = processed (json, constant (0.25f, total), 300, &tempo, from);
    CHECK_EQ (maxDifference (whole, late, from), 0.0);
    // 깊이 곡선: 0 dB 에서 -18 dB 로 바뀌는 4박 전에는 그대로
    const auto ramped = processed (R"({ "type": "ducker", "depthDb": 0, "automation": { "depthDb": [ { "beat": 4, "value": 0, "curve": "step" }, { "beat": 4, "value": -18 } ] } })",
                                   constant (0.25f, total), 512, &tempo);
    CHECK_NEAR (ramped.getSample (0, (int) tempo.sampleAt (2.0, fs) + 240), 0.25, 1e-6);
    CHECK_NEAR (juce::Decibels::gainToDecibels (ramped.getSample (0, (int) tempo.sampleAt (5.0, fs) + 480) / 0.25), -18.0, 1e-3);
}

// ---------------------------------------------------------------------------
// delay timeBeats, 다른 곡선 매개변수
// ---------------------------------------------------------------------------

TEST (delay_time_beats_follows_the_tempo_map)
{
    auto echoAt = [] (const TempoMap& tempo, double impulseBeat, const char* json)
    {
        juce::AudioBuffer<float> b (2, 4 * 48000);
        b.clear();
        const int at = (int) tempo.sampleAt (impulseBeat, fs);
        b.setSample (0, at, 1.0f);
        b.setSample (1, at, 1.0f);
        const auto out = processed (json, b, 512, &tempo);
        int best = 0;
        float peak = 0.0f;
        for (int i = at + 10; i < out.getNumSamples(); ++i)
            if (std::abs (out.getSample (0, i)) > peak)
            {
                peak = std::abs (out.getSample (0, i));
                best = i;
            }
        return best;
    };
    const char* json = R"({ "type": "delay", "timeBeats": 0.75, "feedback": 0, "mix": 1, "lowCutHz": 10, "highCutHz": 24000 })";
    const TempoMap steady ({ { 0.0, 120.0, false } });
    CHECK (std::abs (echoAt (steady, 1.0, json) - (24000 + 18000)) <= 1);
    const TempoMap slow ({ { 0.0, 90.0, false } });
    CHECK (std::abs (echoAt (slow, 1.0, json) - (int) slow.sampleAt (1.75, fs)) <= 1);
    // 램프: 소리를 낸 박에서 0.75박 뒤에 메아리가 온다
    const TempoMap ramp ({ { 0.0, 80.0, true }, { 4.0, 160.0, false } });
    CHECK (std::abs (echoAt (ramp, 2.0, json) - (int) ramp.sampleAt (2.75, fs)) <= 1);
    // 너무 길면 만들 때 거절한다
    const TempoMap crawl ({ { 0.0, 10.0, false } });
    bool refused = false;
    try
    {
        createEffect (spec (R"({ "type": "delay", "timeBeats": 4 })"), {}, fs, 512, &crawl);
    }
    catch (const Failure& f)
    {
        refused = std::string (f.what()).find ("at most 16 s") != std::string::npos;
    }
    CHECK (refused);
}

TEST (other_automated_parameters_move_with_their_lanes)
{
    const TempoMap tempo ({ { 0.0, 120.0, false } });
    const auto input = noise (9, 96000);
    // 모든 곡선 효과가 블록 크기와 상관없이 같은 샘플을 낸다
    const char* everyLane[] = {
        R"({ "type": "delay", "timeMs": 90, "automation": { "mix": [ { "beat": 0, "value": 0 }, { "beat": 3, "value": 1 } ], "feedback": [ { "beat": 1, "value": 0.1 }, { "beat": 3, "value": 0.8 } ] } })",
        R"({ "type": "delay", "timeBeats": 0.25, "feedback": 0.4, "mix": 0.5 })",
        R"({ "type": "reverb", "mix": 0.3, "decaySeconds": 0.8, "automation": { "wetDb": [ { "beat": 0, "value": -60 }, { "beat": 2, "value": 0 } ] } })",
        R"({ "type": "reverb", "mode": "algorithmic", "automation": { "mix": [ { "beat": 0, "value": 0 }, { "beat": 3, "value": 1 } ] } })",
        R"({ "type": "stereo", "automation": { "width": [ { "beat": 0, "value": 0 }, { "beat": 3, "value": 2 } ] } })",
        R"({ "type": "distortion", "automation": { "driveDb": [ { "beat": 0, "value": 0 }, { "beat": 3, "value": 30 } ], "mix": [ { "beat": 0, "value": 1 }, { "beat": 3, "value": 0.2 } ] } })",
        R"({ "type": "filter", "mode": "bandpass", "slope": 24, "automation": { "cutoffHz": [ { "beat": 0, "value": 300 }, { "beat": 3, "value": 6000, "curve": "smooth" } ], "resonance": [ { "beat": 0, "value": 0 }, { "beat": 3, "value": 0.8 } ], "mix": [ { "beat": 2, "value": 1 }, { "beat": 3, "value": 0.5 } ] } })",
    };
    for (const char* json : everyLane)
    {
        const auto a = processed (json, input, 512, &tempo), b = processed (json, input, 100, &tempo);
        if (! CHECK_EQ (maxDifference (a, b), 0.0))
            std::fprintf (stderr, "    effect %s\n", json);
        // 같은 매개변수의 새 사슬로 바꿔 끼워도 같다
        auto swapped = input;
        auto first = createEffectChain ({ spec (json) }, mrtest::sourceDir(), fs, 512, &tempo);
        processBlocks (first, swapped, 0, 30720, 512);
        auto second = createEffectChain ({ spec (json) }, mrtest::sourceDir(), fs, 512, &tempo);
        second.continueFrom (first);
        processBlocks (second, swapped, 30720, swapped.getNumSamples(), 512);
        if (! CHECK_EQ (maxDifference (a, swapped), 0.0))
            std::fprintf (stderr, "    swapped %s\n", json);
    }
    // 리버브 잔향 곡선: -60 dB 에서 0 dB 로 오르는 동안 잔향이 커진다 (마른 소리 없이)
    juce::AudioBuffer<float> clicks (2, 96000);
    clicks.clear();
    for (int i = 0; i < 96000; i += 4800)
        clicks.setSample (0, i, 0.5f), clicks.setSample (1, i, 0.5f);
    const auto wet = processed (R"({ "type": "reverb", "dryDb": -120, "decaySeconds": 0.5, "automation": { "wetDb": [ { "beat": 0, "value": -40 }, { "beat": 2, "value": 0 } ] } })",
                                clicks, 512, &tempo);
    CHECK (rmsDb (wet, 0.1, 0.3) < rmsDb (wet, 1.1, 1.3) - 25.0);
    // 스테레오 폭 0 은 모노
    const auto narrow = processed (R"({ "type": "stereo", "automation": { "width": [ { "beat": 0, "value": 1 }, { "beat": 1, "value": 0 } ] } })", input, 512, &tempo);
    CHECK_NEAR (narrow.getSample (0, 50000), narrow.getSample (1, 50000), 1e-7);
}

TEST (effect_and_send_automation_survive_a_document_round_trip)
{
    const char* json = R"({ "tempo": 120,
  "buses": [ { "name": "fx", "effects": [ { "type": "filter", "mode": "highpass", "cutoffHz": 20, "comment": "빌드업",
                                          "automation": { "cutoffHz": [ { "beat": 0, "value": 20 }, { "beat": 64, "value": 900, "curve": "exponential" } ] } } ] } ],
  "tracks": [
    { "name": "A", "instrument": "synth.sine", "sends": [ { "bus": "fx", "levelDb": -6 },
                                                         { "bus": "fx", "levelDb": -3, "automation": { "levelDb": [ { "beat": 0, "value": -120 }, { "beat": 8, "value": 0 } ] } } ],
      "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 90 } ] } ] })";
    std::string error;
    auto doc = CompositionDocument::fromText (juce::String::fromUTF8 (json), mrtest::sourceDir().getChildFile ("tests/inline.json"), error);
    REQUIRE (doc.has_value());
    const auto text = doc->toJson();
    auto again = CompositionDocument::fromText (juce::String::fromUTF8 (text.c_str()), mrtest::sourceDir().getChildFile ("tests/inline.json"), error);
    REQUIRE (again.has_value());
    CHECK_EQ (again->toJson(), text);
    CHECK (text.find (R"({"beat": 64, "value": 900, "curve": "exponential"})") != std::string::npos);
    CHECK (text.find (R"("comment": "빌드업",)") != std::string::npos);
    // 효과 매개변수를 바꿔도 곡선이 남는다
    auto buses = doc->property ("buses");
    auto effects = effectsJson (buses[0]);
    effects[0] = withEffectParameter (effects[0], "cutoffHz", 30.0);
    CHECK (effects[0].getProperty ("automation", juce::var()).hasProperty ("cutoffHz"));
    // 앞 센드를 지우면 곡선은 그 센드를 따라간다 (자리를 물려받은 객체에 옮겨 쓴다)
    const auto parsed = doc->parse();
    REQUIRE (parsed.composition.has_value());
    auto sends = parsed.composition->tracks[0].sends;
    sends.erase (sends.begin());
    const auto track = withSends (doc->track (0), sends);
    const auto written = writeJson (track["sends"]);
    CHECK_EQ (track["sends"].getArray()->size(), 1);
    CHECK (written.find (R"("levelDb": -3,)") != std::string::npos);
    CHECK (written.find (R"({"beat": 0.0, "value": -120.0},)") != std::string::npos);
    // 곡선을 지우면 automation 도 지운다
    sends[0].levelLane = {};
    CHECK (! withSends (track, sends)["sends"][0].hasProperty ("automation"));
    // 같은 곡선이면 원래 표기를 둔다
    const auto same = withSends (doc->track (0), parsed.composition->tracks[0].sends);
    CHECK (writeJson (same["sends"]).find (R"({"beat": 8, "value": 0})") != std::string::npos);

    // MIDI 내보내기는 효과와 센드의 곡선과 상관없다
    auto bare = *parsed.composition;
    bare.buses[0].effects.clear();
    for (auto& s : bare.tracks[0].sends)
        s.levelLane = {};
    const auto withLanes = mrtest::outputDir().getChildFile ("lanes.mid"), without = mrtest::outputDir().getChildFile ("lanes_bare.mid");
    exportMidi (*parsed.composition, withLanes, false, std::nullopt);
    exportMidi (bare, without, false, std::nullopt);
    juce::MemoryBlock x, y;
    withLanes.loadFileAsData (x);
    without.loadFileAsData (y);
    CHECK (x.getSize() > 0 && x == y);
}
