#include "TestFramework.h"
#include "TestUtil.h"

#include "analysis/Analysis.h"
#include "core/Random.h"
#include "effects/Effects.h"
#include "io/AudioFile.h"
#include "rendering/OfflineRenderer.h"

using namespace mr;

namespace
{

constexpr double fs = 48000.0;

EffectSpec spec (const std::string& json)
{
    EffectSpec e;
    e.params = juce::JSON::parse (juce::String::fromUTF8 (json.c_str()));
    e.type = e.params.getProperty ("type", "").toString().toStdString();
    e.path = "$.test";
    return e;
}

juce::AudioBuffer<float> sine (double freq, double amplitude, double seconds = 1.0)
{
    juce::AudioBuffer<float> b (2, (int) (fs * seconds));
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float v = (float) (amplitude * std::sin (2.0 * 3.141592653589793 * freq * i / fs));
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    return b;
}

double steadyPeakDb (const juce::AudioBuffer<float>& b)
{
    return gainToDb (b.getMagnitude (0, b.getNumSamples() / 2, b.getNumSamples() / 4));
}

double run (const std::string& json, juce::AudioBuffer<float> b)
{
    auto chain = createEffectChain ({ spec (json) }, mrtest::sourceDir(), fs, 512);
    processOffline (chain, b, b.getNumSamples(), 512);
    return steadyPeakDb (b);
}

// 정상 상태 RMS 의 비 (dB). 높은 주파수는 한 주기의 샘플이 적어 샘플 피크로는 진폭을 잴 수 없다.
double gainOf (const std::string& json, juce::AudioBuffer<float> b)
{
    const int from = b.getNumSamples() / 2, count = b.getNumSamples() / 4;
    const double before = b.getRMSLevel (0, from, count);
    auto chain = createEffectChain ({ spec (json) }, mrtest::sourceDir(), fs, 512);
    processOffline (chain, b, b.getNumSamples(), 512);
    return gainToDb (b.getRMSLevel (0, from, count) / before);
}

} // namespace

TEST (eq_cuts_and_boosts)
{
    const char* cut = R"({ "type": "eq", "lowCutHz": 1000, "lowCutSlope": 24 })";
    CHECK (gainOf (cut, sine (100.0, 0.5)) < -70.0);                 // 24 dB/oct 로 3.3 옥타브 아래 (약 -80 dB)
    CHECK_NEAR (gainOf (cut, sine (8000.0, 0.5)), 0.0, 0.1);
    CHECK_NEAR (gainOf (cut, sine (1000.0, 0.5)), -3.01, 0.1);       // 차단 주파수는 -3 dB
    CHECK_NEAR (gainOf (R"({ "type": "eq", "bands": [ { "type": "peak", "freqHz": 1000, "gainDb": 6, "q": 1 } ] })", sine (1000.0, 0.25)), 6.0, 0.05);
    CHECK_NEAR (gainOf (R"({ "type": "eq", "bands": [ { "type": "highShelf", "freqHz": 3000, "gainDb": -9 } ] })", sine (16000.0, 0.25)), -9.0, 0.3);
    CHECK_NEAR (gainOf (R"({ "type": "eq", "bands": [ { "type": "lowShelf", "freqHz": 200, "gainDb": 4 } ] })", sine (40.0, 0.25, 2.0)), 4.0, 0.3);
    CHECK_NEAR (gainOf (R"({ "type": "eq", "highCutHz": 2000 })", sine (2000.0, 0.5)), -3.01, 0.1);
}

TEST (compressor_follows_its_static_curve)
{
    // -6 dBFS 사인, 문턱 -20, 비 4, 니 없음: 14 dB 넘침이 3.5 dB 로 → -16.5 dBFS
    CHECK_NEAR (run (R"({ "type": "compressor", "thresholdDb": -20, "ratio": 4, "kneeDb": 0, "attackMs": 1, "releaseMs": 200 })", sine (1000.0, 0.5)), -16.5, 0.6);
    // 문턱 아래는 그대로, makeup 은 더해진다
    CHECK_NEAR (run (R"({ "type": "compressor", "thresholdDb": -10, "ratio": 4, "kneeDb": 0, "makeupDb": 3 })", sine (1000.0, 0.1)), -20.0 + 3.0, 0.2);
}

TEST (limiter_never_exceeds_its_ceiling_and_is_transparent_below)
{
    Random rng (5);
    juce::AudioBuffer<float> loud (2, 96000);
    for (int i = 0; i < loud.getNumSamples(); ++i)
    {
        const double burst = (i / 4800) % 3 == 0 ? 4.0 : 0.5;
        loud.setSample (0, i, (float) (rng.gaussian() * burst));
        loud.setSample (1, i, (float) (rng.gaussian() * burst * 0.7));
    }
    auto chain = createEffectChain ({ spec (R"({ "type": "limiter", "ceilingDb": -1, "releaseMs": 50 })") }, mrtest::sourceDir(), fs, 512);
    processOffline (chain, loud, loud.getNumSamples(), 512);
    CHECK (loud.getMagnitude (0, 0, loud.getNumSamples()) <= (float) std::pow (10.0, -1.0 / 20.0) + 1.0e-6f);
    CHECK (loud.getMagnitude (1, 0, loud.getNumSamples()) <= (float) std::pow (10.0, -1.0 / 20.0) + 1.0e-6f);

    auto quiet = sine (440.0, 0.1, 0.5);
    const auto original = quiet;
    auto chain2 = createEffectChain ({ spec (R"({ "type": "limiter", "ceilingDb": -1 })") }, mrtest::sourceDir(), fs, 300);
    processOffline (chain2, quiet, quiet.getNumSamples(), 300);
    double maxDiff = 0.0;
    for (int i = 0; i < quiet.getNumSamples(); ++i)
        maxDiff = std::max (maxDiff, (double) std::abs (quiet.getSample (0, i) - original.getSample (0, i)));
    CHECK (maxDiff < 1.0e-6);   // 지연을 되돌려 같은 자리에 같은 값
}

TEST (convolution_matches_direct_convolution)
{
    Random rng (9);
    juce::AudioBuffer<float> ir (2, 3000);
    for (int i = 0; i < 3000; ++i)
    {
        ir.setSample (0, i, (float) (rng.gaussian() * std::exp (-i / 600.0)));
        ir.setSample (1, i, (float) (rng.gaussian() * std::exp (-i / 400.0)));
    }
    std::vector<float> x (10000);
    for (auto& v : x)
        v = (float) rng.gaussian();
    StereoConvolver conv;
    conv.prepare (ir, 256);
    std::vector<float> l (x.size() + 256), r (x.size() + 256), in (x.size() + 256, 0.0f);
    std::copy (x.begin(), x.end(), in.begin());
    // 블록 크기를 들쭉날쭉하게 넣어도 같다
    for (size_t pos = 0, k = 0; pos < in.size(); ++k)
    {
        const size_t n = std::min (in.size() - pos, (size_t) (37 + (k * 97) % 400));
        conv.process (in.data() + pos, l.data() + pos, r.data() + pos, (int) n);
        pos += n;
    }
    double maxErr = 0.0, maxRef = 0.0;
    for (int n = 0; n < 6000; n += 7)
    {
        double ref[2] = { 0.0, 0.0 };
        for (int k = 0; k < 3000 && k <= n; ++k)
        {
            ref[0] += (double) x[(size_t) (n - k)] * ir.getSample (0, k);
            ref[1] += (double) x[(size_t) (n - k)] * ir.getSample (1, k);
        }
        maxRef = std::max (maxRef, std::abs (ref[0]));
        maxErr = std::max (maxErr, std::abs (ref[0] - l[(size_t) n + 256]));
        maxErr = std::max (maxErr, std::abs (ref[1] - r[(size_t) n + 256]));
    }
    CHECK (maxErr < 1.0e-4 * std::max (1.0, maxRef));
}

TEST (generated_hall_decays_at_the_requested_rate)
{
    const auto ir = generateHallImpulse (fs, 2.0, 0.0, 0.0, 1.0, 42);
    const auto again = generateHallImpulse (fs, 2.0, 0.0, 0.0, 1.0, 42);
    const auto other = generateHallImpulse (fs, 2.0, 0.0, 0.0, 1.0, 43);
    CHECK (ir.getSample (0, 5000) == again.getSample (0, 5000));
    CHECK (ir.getSample (0, 5000) != other.getSample (0, 5000));
    // 슈뢰더 역적분으로 -5 dB 에서 -35 dB 까지의 기울기(T30)를 재고 60 dB 로 늘린다
    std::vector<double> energy ((size_t) ir.getNumSamples(), 0.0);
    double acc = 0.0;
    for (int i = ir.getNumSamples() - 1; i >= 0; --i)
    {
        acc += (double) ir.getSample (0, i) * ir.getSample (0, i);
        energy[(size_t) i] = acc;
    }
    auto timeAt = [&] (double db)
    {
        for (size_t i = 0; i < energy.size(); ++i)
            if (10.0 * std::log10 (energy[i] / energy[0]) <= db)
                return i / fs;
        return energy.size() / fs;
    };
    const double rt = (timeAt (-35.0) - timeAt (-5.0)) * 2.0;
    CHECK (rt > 1.5 && rt < 2.6);
    // 쌓이는 확산음 + 초기 반사: 처음 5ms 는 거의 비어 있다
    CHECK (ir.getMagnitude (0, 0, 200) < ir.getMagnitude (0, 2400, 4800));
}

TEST (reverb_keeps_the_dry_signal_in_place)
{
    auto dry = sine (330.0, 0.3, 0.5);
    const auto original = dry;
    auto chain = createEffectChain ({ spec (R"({ "type": "reverb", "mix": 0 })") }, mrtest::sourceDir(), fs, 512);
    CHECK_EQ (chain.latencySamples(), 1024);
    processOffline (chain, dry, dry.getNumSamples(), 512);
    double maxDiff = 0.0;
    for (int i = 0; i < dry.getNumSamples(); ++i)
        maxDiff = std::max (maxDiff, (double) std::abs (dry.getSample (1, i) - original.getSample (1, i)));
    CHECK (maxDiff < 1.0e-7);

    // 젖은 소리만: 임펄스 뒤 프리딜레이 동안 조용하고 그 뒤에 울린다
    juce::AudioBuffer<float> impulse (2, 96000);
    impulse.clear();
    impulse.setSample (0, 0, 1.0f);
    impulse.setSample (1, 0, 1.0f);
    auto wet = createEffectChain ({ spec (R"({ "type": "reverb", "mix": 1, "preDelayMs": 40, "decaySeconds": 1.5 })") }, mrtest::sourceDir(), fs, 512);
    processOffline (wet, impulse, impulse.getNumSamples(), 512);
    CHECK (impulse.getMagnitude (0, 0, 1800) < 1.0e-4f);
    CHECK (impulse.getMagnitude (0, 2400, 9600) > 1.0e-3f);
    CHECK (impulse.getMagnitude (0, 90000, 6000) < 0.1f * impulse.getMagnitude (0, 2400, 9600));
}

TEST (effect_specs_are_validated)
{
    Diagnostics d;
    validateEffectSpec (spec (R"({ "type": "revrb" })"), d);
    CHECK (! d.errors.empty() && d.errors[0].hint.find ("reverb") != std::string::npos);
    Diagnostics d2;
    validateEffectSpec (spec (R"({ "type": "compressor", "ratio": 0.5, "treshold": -10 })"), d2);
    CHECK (mrtest::hasError (d2, "$.test.ratio", "out of range"));
    CHECK (mrtest::hasWarning (d2, "$.test.treshold", "thresholdDb"));
    Diagnostics d3;
    validateEffectSpec (spec (R"({ "type": "eq", "lowCutSlope": 18, "bands": [ { "type": "notch", "freqHz": 100 } ] })"), d3);
    CHECK (mrtest::hasError (d3, "$.test.lowCutSlope", "12 or 24"));
    CHECK (mrtest::hasError (d3, "$.test.bands[0].type"));
}

TEST (stereo_effect_places_and_narrows_the_image)
{
    // 왼쪽만 있는 소리와 오른쪽만 있는 소리를 섞은 넓은 상
    juce::AudioBuffer<float> wide (2, 4800);
    for (int i = 0; i < 4800; ++i)
    {
        wide.setSample (0, i, (float) (0.4 * std::sin (i * 0.05)));
        wide.setSample (1, i, (float) (0.4 * std::sin (i * 0.031)));
    }
    auto process = [] (const std::string& json, juce::AudioBuffer<float> b)
    {
        auto chain = createEffectChain ({ spec (json) }, mrtest::sourceDir(), fs, 512);
        processOffline (chain, b, b.getNumSamples(), 512);
        return b;
    };
    // 폭 1, 가운데: 그대로
    auto same = process (R"({ "type": "stereo", "width": 1, "pan": 0 })", wide);
    CHECK_NEAR (same.getSample (0, 1234), wide.getSample (0, 1234), 1e-6);
    CHECK_NEAR (same.getSample (1, 1234), wide.getSample (1, 1234), 1e-6);
    // 폭 0: 모노가 된다 (두 채널이 같다)
    auto mono = process (R"({ "type": "stereo", "width": 0, "pan": 0 })", wide);
    CHECK_NEAR (mono.getSample (0, 777), mono.getSample (1, 777), 1e-6);
    // 폭 0, 왼쪽 끝: 오른쪽이 꺼지고 왼쪽에 등전력으로 모인다
    auto left = process (R"({ "type": "stereo", "width": 0, "pan": -1 })", wide);
    CHECK (left.getMagnitude (1, 0, 4800) < 1.0e-6f);
    CHECK_NEAR (left.getSample (0, 777), std::sqrt (2.0) * 0.5 * (wide.getSample (0, 777) + wide.getSample (1, 777)), 1e-5);
    // 밸런스와 달리 오른쪽에만 있던 소리(0.031 rad 사인)가 왼쪽으로 옮겨 온다: 진폭 0.4 * sqrt(2) / 2
    double proj = 0.0;
    for (int i = 0; i < 4800; ++i)
        proj += left.getSample (0, i) * std::sin (i * 0.031);
    CHECK_NEAR (2.0 * proj / 4800.0, 0.4 * std::sqrt (2.0) / 2.0, 0.01);
}

TEST (true_peak_sees_between_samples)
{
    // fs/4 의 사인을 45도 어긋나게 찍으면 샘플은 진폭의 0.707 에만 놓인다
    juce::AudioBuffer<float> b (1, 4800);
    for (int i = 0; i < 4800; ++i)
        b.setSample (0, i, (float) (0.5 * std::sin (2.0 * 3.141592653589793 * 0.25 * i + 3.141592653589793 / 4.0)));
    CHECK_NEAR (b.getMagnitude (0, 0, 4800), 0.5 * std::sqrt (0.5), 1e-4);
    CHECK_NEAR (truePeak (b, 4800), 0.5, 0.01);
}

TEST (mastering_reaches_the_target_loudness)
{
    auto r = mrtest::parse (R"({ "tempo": 96, "render": { "tailSeconds": 2 },
      "performance": { "timingHumanizeMs": 8, "humanizeSeed": 4 },
      "buses": [ { "name": "hall", "effects": [ { "type": "reverb", "mix": 1, "decaySeconds": 2.4 } ] } ],
      "master": { "targetLufs": -16, "effects": [ { "type": "eq", "lowCutHz": 30 }, { "type": "compressor", "thresholdDb": -18, "ratio": 2 },
                                                   { "type": "limiter", "ceilingDb": -1 } ] },
      "tracks": [
        { "name": "Pad", "instrument": "synth.pad", "sends": [ { "bus": "hall", "levelDb": -6 } ],
          "notes": [ { "pitch": "E3", "startBeat": 0, "durationBeat": 8, "velocity": 90 }, { "pitch": "B3", "startBeat": 0, "durationBeat": 8, "velocity": 90 },
                     { "pitch": "G4", "startBeat": 0, "durationBeat": 8, "velocity": 90 } ] },
        { "name": "Kit", "instrument": "synth.drums", "notes": [
          { "pitch": 36, "startBeat": 0, "durationBeat": 0.5, "velocity": 127 }, { "pitch": 38, "startBeat": 1, "durationBeat": 0.5, "velocity": 127 },
          { "pitch": 36, "startBeat": 2, "durationBeat": 0.5, "velocity": 127 }, { "pitch": 38, "startBeat": 3, "durationBeat": 0.5, "velocity": 127 },
          { "pitch": 36, "startBeat": 4, "durationBeat": 0.5, "velocity": 127 }, { "pitch": 38, "startBeat": 5, "durationBeat": 0.5, "velocity": 127 } ] } ] })");
    REQUIRE (r.composition.has_value());
    RenderOptions o;
    o.useDefaultRegistry = false;
    o.bitDepth = 32;
    o.output = mrtest::outputDir().getChildFile ("mastered.wav");
    o.stemsDir = mrtest::outputDir().getChildFile ("mastered_stems");
    const auto report = renderComposition (*r.composition, o);
    REQUIRE (report.stats.lufs.has_value());
    CHECK_NEAR (*report.stats.lufs, -16.0, 0.3);
    CHECK (report.stats.peakDb <= -1.0 + 1.0e-4);
    CHECK (report.normalisation.applied && report.normalisation.limited);
    CHECK (report.normalisation.passes >= 1);
    // 버스 스템에는 음이 끝난 뒤에도 잔향이 남는다
    const auto hall = readAudioFile (o.stemsDir->getChildFile ("bus-hall.wav"));
    CHECK (hall.buffer.getMagnitude (0, (int) (5.2 * fs), (int) (0.5 * fs)) > 1.0e-4f);
}

TEST (track_effect_latency_is_aligned)
{
    auto make = [] (bool withReverb)
    {
        auto r = mrtest::parse (std::string (R"({ "tempo": 120, "render": { "tailSeconds": 0.3 }, "tracks": [
            { "name": "A", "instrument": "synth.sine" )") + (withReverb ? R"(, "effects": [ { "type": "reverb", "mix": 0 }, { "type": "limiter", "ceilingDb": -0.1 } ])" : "") + R"(,
              "notes": [ { "pitch": "A4", "startBeat": 0.5, "durationBeat": 1, "velocity": 100 } ] },
            { "name": "B", "instrument": "synth.sine", "notes": [ { "pitch": "E5", "startBeat": 0.5, "durationBeat": 1, "velocity": 100 } ] } ] })");
        return *r.composition;
    };
    RenderOptions o;
    o.useDefaultRegistry = false;
    o.bitDepth = 32;
    o.output = mrtest::outputDir().getChildFile ("fx_aligned_plain.wav");
    renderComposition (make (false), o);
    const auto plain = readAudioFile (o.output).buffer;
    o.output = mrtest::outputDir().getChildFile ("fx_aligned_reverb.wav");
    const auto report = renderComposition (make (true), o);
    const auto aligned = readAudioFile (o.output).buffer;
    CHECK_EQ (report.tracks[0].latencySamples, 1024 + 2 * 240);
    REQUIRE (plain.getNumSamples() == aligned.getNumSamples());
    double maxDiff = 0.0;
    for (int i = 0; i < plain.getNumSamples(); ++i)
        maxDiff = std::max (maxDiff, (double) std::abs (plain.getSample (0, i) - aligned.getSample (0, i)));
    CHECK (maxDiff < 1.0e-6);
}

namespace
{

juce::AudioBuffer<float> noise (uint64_t seed, int samples, float amplitude = 0.3f)
{
    Random rng (seed);
    juce::AudioBuffer<float> b (2, samples);
    for (int i = 0; i < samples; ++i)
    {
        // 소리와 쉼이 번갈아 나와 꼬리와 회복도 지난다
        const float gate = (i / 9000) % 2 == 0 ? 1.0f : 0.0f;
        b.setSample (0, i, (float) rng.gaussian() * amplitude * gate);
        b.setSample (1, i, (float) rng.gaussian() * amplitude * 0.6f * gate);
    }
    return b;
}

// 블록 단위로 체인을 지난다 (지연을 되돌리지 않는다, 실시간과 같다)
void processBlocks (EffectChain& chain, juce::AudioBuffer<float>& b, int from, int to, int block = 512)
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

double maxDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from = 0)
{
    double d = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = from; i < a.getNumSamples(); ++i)
            d = std::max (d, (double) std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
    return d;
}

const char* const everyEffect[] = {
    R"({ "type": "eq", "lowCutHz": 90, "lowCutSlope": 24, "highCutHz": 9000, "bands": [ { "type": "peak", "freqHz": 900, "gainDb": 5, "q": 2 },
         { "type": "lowShelf", "freqHz": 200, "gainDb": -3 } ] })",
    R"({ "type": "compressor", "thresholdDb": -24, "ratio": 6, "attackMs": 2, "releaseMs": 80 })",
    R"({ "type": "distortion", "driveDb": 18, "toneHz": 5000, "mix": 0.7 })",
    R"({ "type": "delay", "timeMs": 120, "feedback": 0.6, "mix": 0.5, "pingPong": 1 })",
    R"({ "type": "limiter", "ceilingDb": -6, "releaseMs": 40, "truePeak": 1 })",
    R"({ "type": "reverb", "mix": 0.4, "decaySeconds": 1.1, "preDelayMs": 12 })",
    R"({ "type": "reverb", "mode": "algorithmic", "mix": 0.4, "roomSize": 0.8 })",
    R"({ "type": "stereo", "width": 0.5, "pan": 0.3 })",
    R"({ "type": "gain", "gainDb": -4 })",
    R"({ "type": "filter", "mode": "bandpass", "slope": 24, "cutoffHz": 900, "resonance": 0.5, "mix": 0.8 })",
    R"({ "type": "ducker", "rateBeats": 0.5, "depthDb": -12, "releaseBeats": 0.3 })",
    R"({ "type": "delay", "timeBeats": 0.375, "feedback": 0.5, "mix": 0.5 })",
    R"({ "type": "chorus", "rateHz": 1.3, "depthMs": 4, "feedback": 0.3, "mix": 0.5 })",
    R"({ "type": "flanger", "rateBeats": 2, "feedback": -0.7 })",
    R"({ "type": "phaser", "rateHz": 0.7, "stages": 8, "feedback": 0.6 })",
    R"({ "type": "tremolo", "rateBeats": 0.25, "depth": 0.8, "shape": "square", "phaseDeg": 180 })",
    R"({ "type": "gate", "thresholdDb": -20, "holdMs": 5, "releaseMs": 30 })",
    R"({ "type": "multiband", "lowThresholdDb": -30, "lowRatio": 4, "midGainDb": 2, "highThresholdDb": -24, "highRatio": 3 })",
    R"({ "type": "transient", "attackDb": 8, "sustainDb": -6 })",
    R"({ "type": "deesser", "frequencyHz": 5000, "thresholdDb": -40, "rangeDb": 10 })",
};

} // namespace

TEST (effect_defaults_come_from_the_parameter_table)
{
    for (const auto& type : effectTypes())
    {
        const auto& params = effectParameters (type);
        REQUIRE (! params.empty());
        // 빈 객체와 기본값을 모두 쓴 객체가 같은 소리를 낸다 (선택적 매개변수는 빼고)
        auto* full = new juce::DynamicObject();
        full->setProperty ("type", juce::String (type));
        for (const auto& p : params)
        {
            CHECK (p.minimum < p.maximum);
            CHECK (p.defaultValue >= p.minimum && p.defaultValue <= p.maximum);
            if (! p.optional)
                full->setProperty (juce::Identifier (p.key), p.defaultValue);
        }
        EffectSpec bare, explicitDefaults;
        bare.type = explicitDefaults.type = type;
        bare.path = explicitDefaults.path = "$.test";
        bare.params = juce::JSON::parse ("{ \"type\": \"" + juce::String (type) + "\" }");
        explicitDefaults.params = juce::var (full);
        Diagnostics d;
        validateEffectSpec (explicitDefaults, d);
        CHECK (d.ok());
        auto a = noise (3, 30000), b = a;
        auto chainA = createEffectChain ({ bare }, mrtest::sourceDir(), fs, 512);
        auto chainB = createEffectChain ({ explicitDefaults }, mrtest::sourceDir(), fs, 512);
        processBlocks (chainA, a, 0, a.getNumSamples());
        processBlocks (chainB, b, 0, b.getNumSamples());
        CHECK_EQ (maxDifference (a, b), 0.0);
    }
    CHECK (findEffectParameter ("compressor", "ratio") != nullptr);
    CHECK (findEffectParameter ("compressor", "nonsense") == nullptr);
    CHECK (effectParameters ("nonsense").empty());
}

TEST (continuing_a_chain_with_the_same_parameters_changes_nothing)
{
    // 처리 도중 같은 매개변수로 새로 만든 사슬로 바꿔 끼워도 출력이 한 샘플도 달라지지 않는다 (상태를 그대로 이어받는다)
    for (const char* json : everyEffect)
    {
        const auto input = noise (11, 48000);
        auto whole = input;
        auto chain = createEffectChain ({ spec (json) }, mrtest::sourceDir(), fs, 512);
        processBlocks (chain, whole, 0, whole.getNumSamples());

        auto swapped = input;
        auto first = createEffectChain ({ spec (json) }, mrtest::sourceDir(), fs, 512);
        processBlocks (first, swapped, 0, 20480);
        auto second = createEffectChain ({ spec (json) }, mrtest::sourceDir(), fs, 512);
        second.continueFrom (first);
        processBlocks (second, swapped, 20480, swapped.getNumSamples());
        const double diff = maxDifference (whole, swapped);
        if (! CHECK_EQ (diff, 0.0))
            std::fprintf (stderr, "    effect %s\n", json);
    }
}

TEST (continuing_a_reverb_with_a_new_decay_keeps_the_tail)
{
    // 짧은 소리 뒤에 잔향의 길이를 바꾼다: 이어받은 사슬은 지난 소리의 꼬리를 새 길이로 낸다 (처음부터 새 길이로 처리한 것과 거의 같다)
    juce::AudioBuffer<float> input (2, 96000);
    input.clear();
    Random rng (4);
    for (int i = 0; i < 4800; ++i)
        for (int ch = 0; ch < 2; ++ch)
            input.setSample (ch, i, (float) rng.gaussian() * 0.3f);
    const char* shortHall = R"({ "type": "reverb", "mix": 1, "decaySeconds": 1.0, "seed": 5 })";
    const char* longHall = R"({ "type": "reverb", "mix": 1, "decaySeconds": 2.5, "seed": 5 })";

    auto reference = input;
    auto fresh = createEffectChain ({ spec (longHall) }, mrtest::sourceDir(), fs, 512);
    processBlocks (fresh, reference, 0, reference.getNumSamples());

    auto continued = input, cut = input;
    auto a = createEffectChain ({ spec (shortHall) }, mrtest::sourceDir(), fs, 512);
    processBlocks (a, continued, 0, 24576);
    auto b = createEffectChain ({ spec (longHall) }, mrtest::sourceDir(), fs, 512);
    b.continueFrom (a);
    processBlocks (b, continued, 24576, continued.getNumSamples());
    auto c = createEffectChain ({ spec (longHall) }, mrtest::sourceDir(), fs, 512);   // 잇지 않으면 꼬리가 끊긴다
    processBlocks (c, cut, 24576, cut.getNumSamples());

    const int from = 24576 + 2048 + 9600, count = 24000;
    const double tail = reference.getRMSLevel (0, from, count);
    CHECK (tail > 1.0e-3);
    CHECK_NEAR (continued.getRMSLevel (0, from, count) / tail, 1.0, 0.02);
    CHECK (cut.getRMSLevel (0, from, count) < tail * 0.01);
}

TEST (bypassed_effects_pass_the_signal_with_their_latency)
{
    auto make = [] (bool bypass)
    {
        EffectSpec limiter = spec (R"({ "type": "limiter", "ceilingDb": -20, "lookaheadMs": 2 })");
        limiter.bypass = bypass;
        EffectSpec eq = spec (R"({ "type": "eq", "highCutHz": 1000 })");
        eq.bypass = bypass;
        return createEffectChain ({ limiter, eq }, mrtest::sourceDir(), fs, 512);
    };
    auto active = make (false), bypassed = make (true);
    CHECK_EQ (active.latencySamples(), bypassed.latencySamples());
    CHECK (bypassed.bypassed (0) && bypassed.bypassed (1));
    const int latency = bypassed.latencySamples();
    REQUIRE (latency == 2 * 96);
    const auto input = noise (8, 20000);
    auto out = input;
    processBlocks (bypassed, out, 0, out.getNumSamples(), 300);
    double maxDiff = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = latency; i < out.getNumSamples(); ++i)
            maxDiff = std::max (maxDiff, (double) std::abs (out.getSample (ch, i) - input.getSample (ch, i - latency)));
    CHECK_EQ (maxDiff, 0.0);
    // 우회한 리미터는 끝의 리미터로 떼어 내지 않는다 (목표 LUFS 맞추기가 그것을 쓰지 않는다)
    CHECK (bypassed.takeTrailingLimiter() == nullptr);

    // 파서가 bypass 를 읽는다
    auto r = mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "A", "instrument": "synth.sine",
        "effects": [ { "type": "eq", "highCutHz": 2000, "bypass": true } ], "notes": [] } ] })");
    REQUIRE (r.composition.has_value());
    CHECK (r.composition->tracks[0].effects[0].bypass);
    auto bad = mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "A", "instrument": "synth.sine",
        "effects": [ { "type": "eq", "bypass": 1 } ], "notes": [] } ] })");
    CHECK (! bad.diagnostics.ok());
}

TEST (eq_response_matches_the_processed_signal)
{
    const char* json = R"({ "type": "eq", "lowCutHz": 120, "lowCutSlope": 24, "highCutHz": 8000,
                            "bands": [ { "type": "peak", "freqHz": 1000, "gainDb": 6, "q": 1.5 }, { "type": "highShelf", "freqHz": 4000, "gainDb": -4 } ] })";
    const std::vector<double> freqs { 60.0, 120.0, 400.0, 1000.0, 2500.0, 6000.0, 12000.0 };
    const auto response = eqResponseDb (juce::JSON::parse (json), fs, freqs);
    REQUIRE (response.size() == freqs.size());
    for (size_t i = 0; i < freqs.size(); ++i)
        CHECK_NEAR (gainOf (json, sine (freqs[i], 0.25, 1.5)), response[i], 0.15);
    CHECK_NEAR (eqResponseDb (juce::JSON::parse (R"({ "type": "eq" })"), fs, { 1000.0 })[0], 0.0, 1.0e-9);
}

namespace
{

// 효과 하나를 만들어 곡의 at 샘플에서 (held 이면 그 자리에 멈춘 채) 블록 하나를 지나게 한다
juce::AudioBuffer<float> runAt (const std::string& json, juce::AudioBuffer<float> b, int64_t at, bool held = false)
{
    const TempoMap tempo (std::vector<TempoPoint> { { 0.0, 120.0, false } });
    auto e = createEffect (spec (json), mrtest::sourceDir(), fs, b.getNumSamples(), &tempo);
    e->setPosition ({ at, held });
    e->process (b, b.getNumSamples());
    return b;
}

juce::AudioBuffer<float> impulse (int samples)
{
    juce::AudioBuffer<float> b (2, samples);
    b.clear();
    b.setSample (0, 0, 1.0f);
    b.setSample (1, 0, 1.0f);
    return b;
}

int peakIndex (const juce::AudioBuffer<float>& b, int ch, int from = 1)
{
    int best = from;
    for (int i = from; i < b.getNumSamples(); ++i)
        if (std::abs (b.getSample (ch, i)) > std::abs (b.getSample (ch, best)))
            best = i;
    return best;
}

} // namespace

TEST (chorus_and_flanger_sweep_their_delay_with_the_song_position)
{
    // 멈춘 자리의 LFO 위상으로 지연이 정해진다: 위상 0 은 delayMs, 반 주기는 delayMs + depthMs. 오른쪽은 phaseDeg 만큼 앞선다
    const std::string chorus = R"({ "type": "chorus", "rateHz": 1, "depthMs": 4, "delayMs": 10, "mix": 1, "phaseDeg": 180 })";
    const auto atZero = runAt (chorus, impulse (2048), 0, true);
    CHECK_EQ (peakIndex (atZero, 0), 480);        // 10 ms
    CHECK_EQ (peakIndex (atZero, 1), 480 + 192);  // 오른쪽은 반 주기 앞: 14 ms
    const auto atHalf = runAt (chorus, impulse (2048), 24000, true);   // 0.5초 = 반 주기
    CHECK_EQ (peakIndex (atHalf, 0), 672);
    CHECK_EQ (peakIndex (atHalf, 1), 480);
    // 박에 맞춘 플랜저: 120 BPM 에서 rateBeats 2 는 1초, 0.25초(위상 1/4)에서 지연은 1 + 2 * 0.5 = 2 ms
    const auto beat = runAt (R"({ "type": "flanger", "rateBeats": 2, "depthMs": 2, "delayMs": 1, "feedback": 0, "mix": 1 })", impulse (512), 12000, true);
    CHECK_EQ (peakIndex (beat, 0), 96);
    // 되먹임이 있으면 같은 간격으로 메아리가 이어진다
    const auto fed = runAt (R"({ "type": "flanger", "rateHz": 0.5, "depthMs": 0, "delayMs": 5, "feedback": 0.5, "mix": 1 })", impulse (2048), 0, true);
    CHECK_NEAR (fed.getSample (0, 240), 1.0, 1.0e-3);
    CHECK_NEAR (fed.getSample (0, 480), 0.5, 1.0e-3);
    CHECK_NEAR (fed.getSample (0, 720), 0.25, 1.0e-3);
}

TEST (phaser_cancels_its_corner_frequency)
{
    // 2단 전역 통과는 꺾임 주파수에서 위상이 반 바퀴라 원음과 반씩 섞으면 지워진다. 위상 0 의 꺾임 주파수는 lowHz
    const std::string json = R"({ "type": "phaser", "rateHz": 0.1, "lowHz": 600, "highHz": 4000, "stages": 2, "feedback": 0, "mix": 0.5 })";
    auto level = [&] (double freq)
    {
        const auto in = sine (freq, 0.5, 0.5);
        const auto out = runAt (json, in, 0, true);
        return gainToDb (out.getRMSLevel (0, 12000, 12000) / in.getRMSLevel (0, 12000, 12000));
    };
    const double notch = level (600.0), away = level (5000.0), low = level (60.0);
    std::printf ("    phaser: 600 Hz %.1f dB, 5 kHz %.1f dB, 60 Hz %.1f dB\n", notch, away, low);
    CHECK (notch < -40.0);
    CHECK (away > -6.0 && low > -1.0);
    // 위상이 반 주기면 꺾임 주파수가 highHz 로 옮겨 600 Hz 는 지워지지 않는다
    const auto in = sine (4000.0, 0.5, 0.5);
    const auto moved = runAt (json, in, (int64_t) (5.0 * fs), true);
    CHECK (gainToDb (moved.getRMSLevel (0, 12000, 12000) / in.getRMSLevel (0, 12000, 12000)) < -40.0);
}

TEST (tremolo_depends_only_on_the_song_position)
{
    // 오토팬 (180도): 사인 모양이면 두 채널 게인의 합이 늘 2 - depth
    juce::AudioBuffer<float> ones (2, 48000);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (ones.getWritePointer (ch), 1.0f, ones.getNumSamples());
    const std::string pan = R"({ "type": "tremolo", "rateHz": 3, "depth": 1, "phaseDeg": 180 })";
    auto panned = ones;
    auto chain = createEffectChain ({ spec (pan) }, mrtest::sourceDir(), fs, 512);
    processBlocks (chain, panned, 0, panned.getNumSamples());
    double sumError = 0.0, minLeft = 1.0;
    for (int i = 0; i < panned.getNumSamples(); ++i)
    {
        sumError = std::max (sumError, std::abs (panned.getSample (0, i) + panned.getSample (1, i) - 1.0));
        minLeft = std::min (minLeft, (double) panned.getSample (0, i));
    }
    CHECK (sumError < 1.0e-4);
    CHECK (minLeft < 1.0e-4);
    CHECK_NEAR (panned.getSample (0, 8000), 0.0, 1.0e-4);   // 1/6 초 = 반 주기에서 왼쪽이 0
    // 곡 위치만의 함수: 다른 자리에서 시작해도, 블록 크기가 달라도 같은 샘플
    const std::string beats = R"({ "type": "tremolo", "rateBeats": 0.75, "depth": 0.6, "shape": "triangle" })";
    const auto input = noise (5, 48000);
    auto whole = input, late = input;
    auto a = createEffectChain ({ spec (beats) }, mrtest::sourceDir(), fs, 512);
    processBlocks (a, whole, 0, whole.getNumSamples(), 512);
    auto b = createEffectChain ({ spec (beats) }, mrtest::sourceDir(), fs, 512);
    processBlocks (b, late, 7777, late.getNumSamples(), 300);
    double diff = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 7777; i < input.getNumSamples(); ++i)
            diff = std::max (diff, (double) std::abs (whole.getSample (ch, i) - late.getSample (ch, i)));
    CHECK_EQ (diff, 0.0);
    // 120 BPM 에서 0.75 박 = 0.375초 주기의 반에서 가장 작다 (1 - 0.6)
    CHECK_NEAR (runAt (beats, ones, 9000, true).getSample (0, 0), 0.4, 1.0e-6);
}

TEST (gate_closes_below_its_threshold)
{
    // -20 dBFS 의 소리 0.5초 뒤 -60 dBFS 의 잡음: 문턱 -40 이면 소리는 지나고 잡음은 rangeDb 만큼 내려간다
    juce::AudioBuffer<float> b (2, 48000);
    Random rng (9);
    for (int i = 0; i < 48000; ++i)
    {
        const float v = i < 24000 ? (float) (0.1 * std::sin (2.0 * 3.141592653589793 * 220.0 * i / fs)) : (float) (rng.gaussian() * 0.001);
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    const auto in = b;
    auto chain = createEffectChain ({ spec (R"({ "type": "gate", "thresholdDb": -40, "rangeDb": -80, "holdMs": 20, "releaseMs": 30 })") },
                                    mrtest::sourceDir(), fs, 512);
    processBlocks (chain, b, 0, b.getNumSamples());
    const double open = gainToDb (b.getRMSLevel (0, 2400, 19200) / in.getRMSLevel (0, 2400, 19200));
    const double closed = gainToDb (b.getRMSLevel (0, 38400, 9600) / in.getRMSLevel (0, 38400, 9600));
    std::printf ("    gate: open %.2f dB, closed %.1f dB\n", open, closed);
    CHECK (open > -0.05);
    CHECK (closed < -75.0);
    // 문턱 아래로 내려간 뒤 hold 동안은 열려 있다 (잡음이 시작한 뒤 10 ms 는 거의 그대로)
    const double held = gainToDb (b.getRMSLevel (0, 24000 + 240, 240) / in.getRMSLevel (0, 24000 + 240, 240));
    CHECK (held > -1.0);
}

namespace
{
// 사인파의 크기 (괴르첼): 그 주파수 성분의 진폭
double toneLevel (const juce::AudioBuffer<float>& b, double hz, int from, int count)
{
    const double w = 2.0 * 3.141592653589793 * hz / fs, c = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = from; i < from + count; ++i)
    {
        const double s0 = b.getSample (0, i) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return 2.0 * std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / count;
}

juce::AudioBuffer<float> tones (std::initializer_list<std::pair<double, double>> parts, int n)   // (Hz, 진폭)
{
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i)
    {
        double v = 0.0;
        for (const auto& [hz, a] : parts)
            v += a * std::sin (2.0 * 3.141592653589793 * hz * i / fs);
        b.setSample (0, i, (float) v);
        b.setSample (1, i, (float) v);
    }
    return b;
}

juce::AudioBuffer<float> processed (const std::string& json, juce::AudioBuffer<float> b)
{
    auto chain = createEffectChain ({ spec (json) }, mrtest::sourceDir(), fs, 512);
    processBlocks (chain, b, 0, b.getNumSamples());
    return b;
}
} // namespace

TEST (multiband_splits_flat_and_compresses_each_band)
{
    // 압축하지 않으면 크기가 평평하다 (올패스)
    double worst = 0.0;
    for (const double hz : { 40.0, 120.0, 200.0, 700.0, 2500.0, 6000.0, 15000.0 })
    {
        const auto in = tones ({ { hz, 0.25 } }, 48000);
        const auto out = processed (R"({ "type": "multiband", "lowRatio": 1, "midRatio": 1, "highRatio": 1 })", in);
        worst = std::max (worst, std::abs (gainToDb (toneLevel (out, hz, 24000, 24000) / toneLevel (in, hz, 24000, 24000))));
    }
    std::printf ("    multiband neutral: worst %.3f dB\n", worst);
    CHECK (worst < 0.05);
    // 큰 80 Hz 와 작은 10 kHz: 낮은 대역만 누른다. 높은 대역의 게인은 그 대역만 올린다
    const auto mix = tones ({ { 80.0, 0.5 }, { 10000.0, 0.03 } }, 48000);
    const auto pressed = processed (R"({ "type": "multiband", "lowThresholdDb": -30, "lowRatio": 4, "midRatio": 1, "highRatio": 1, "highGainDb": 6 })", mix);
    const double low = gainToDb (toneLevel (pressed, 80.0, 24000, 24000) / toneLevel (mix, 80.0, 24000, 24000));
    const double high = gainToDb (toneLevel (pressed, 10000.0, 24000, 24000) / toneLevel (mix, 10000.0, 24000, 24000));
    std::printf ("    multiband: 80 Hz %.1f dB, 10 kHz %+.2f dB\n", low, high);
    CHECK (low < -10.0);
    CHECK_NEAR (high, 6.0, 0.3);
    Diagnostics d;
    validateEffectSpec (spec (R"({ "type": "multiband", "lowHz": 800, "highHz": 1000 })"), d);
    CHECK (d.errors.size() == 1);
}

TEST (transient_shaper_changes_attacks_and_tails)
{
    // 북 같은 소리: 잡음이 바로 차올라 50 ms 로 잦아든다 (0.4초마다)
    juce::AudioBuffer<float> hit (2, 48000);
    Random rng (21);
    for (int i = 0; i < 48000; ++i)
    {
        const int k = i % 19200;
        const float v = (float) (rng.gaussian() * 0.3 * std::exp (-k / (0.05 * fs)));
        hit.setSample (0, i, v);
        hit.setSample (1, i, v);
    }
    auto gainIn = [&hit] (const juce::AudioBuffer<float>& out, double from, double to)
    {
        const int a = 19200 + (int) (from * fs), n = (int) ((to - from) * fs);
        return gainToDb (out.getRMSLevel (0, a, n) / hit.getRMSLevel (0, a, n));
    };
    const auto punch = processed (R"({ "type": "transient", "attackDb": 12 })", hit);
    const auto dry = processed (R"({ "type": "transient", "sustainDb": -12 })", hit);
    std::printf ("    transient: attack +12 -> head %+.1f, tail %+.1f dB; sustain -12 -> head %+.1f, tail %+.1f dB\n", gainIn (punch, 0.0, 0.005),
                 gainIn (punch, 0.12, 0.25), gainIn (dry, 0.0, 0.005), gainIn (dry, 0.12, 0.25));
    CHECK (gainIn (punch, 0.0, 0.005) > 4.0);
    CHECK (std::abs (gainIn (punch, 0.12, 0.25)) < 1.0);
    CHECK (gainIn (dry, 0.12, 0.25) < -6.0);
    CHECK (std::abs (gainIn (dry, 0.0, 0.005)) < 1.5);
    // 고른 소리는 그대로
    const auto steady = tones ({ { 300.0, 0.3 } }, 48000);
    CHECK (std::abs (gainToDb (toneLevel (processed (R"({ "type": "transient", "attackDb": 12, "sustainDb": -12 })", steady), 300.0, 24000, 24000)
                               / toneLevel (steady, 300.0, 24000, 24000))) < 0.1);
}

TEST (deesser_reduces_only_loud_highs)
{
    const auto loud = tones ({ { 500.0, 0.3 }, { 10000.0, 0.3 } }, 48000);
    const auto out = processed (R"({ "type": "deesser", "frequencyHz": 5000, "thresholdDb": -30, "rangeDb": 8 })", loud);
    const double sibilance = gainToDb (toneLevel (out, 10000.0, 24000, 24000) / toneLevel (loud, 10000.0, 24000, 24000));
    const double body = gainToDb (toneLevel (out, 500.0, 24000, 24000) / toneLevel (loud, 500.0, 24000, 24000));
    std::printf ("    de-esser: 10 kHz %.2f dB, 500 Hz %+.2f dB\n", sibilance, body);
    CHECK_NEAR (sibilance, -8.0, 0.5);
    CHECK (std::abs (body) < 0.2);
    const auto quiet = tones ({ { 10000.0, 0.005 } }, 48000);
    CHECK (std::abs (gainToDb (toneLevel (processed (R"({ "type": "deesser", "frequencyHz": 5000, "thresholdDb": -30 })", quiet), 10000.0, 24000, 24000)
                               / toneLevel (quiet, 10000.0, 24000, 24000))) < 0.2);
}

TEST (modulation_effects_are_validated)
{
    auto errors = [] (const std::string& json)
    {
        Diagnostics d;
        validateEffectSpec (spec (json), d);
        return d;
    };
    CHECK (errors (R"({ "type": "tremolo", "shape": "saw" })").errors.size() == 1);
    CHECK (errors (R"({ "type": "phaser", "stages": 5 })").errors.size() == 1);
    CHECK (errors (R"({ "type": "chorus", "depthMs": 30 })").errors.size() == 1);
    const auto both = errors (R"({ "type": "chorus", "rateHz": 1, "rateBeats": 2 })");
    CHECK (both.ok() && both.warnings.size() == 1);
    CHECK (errors (R"({ "type": "flanger", "automation": { "depthMs": [ { "beat": 0, "value": 1 } ], "rateHz": [ { "beat": 0, "value": 1 } ] } })").errors.size() == 1);
    CHECK (errors (R"({ "type": "gate", "thresholdDb": -50, "rangeDb": -30, "attackMs": 1, "holdMs": 10, "releaseMs": 50 })").ok());
}
