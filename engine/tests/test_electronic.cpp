#include "TestFramework.h"
#include "TestUtil.h"

#include "analysis/Analysis.h"
#include "effects/Effects.h"
#include "io/AudioFile.h"
#include "realtime/RealtimePlayer.h"
#include "rendering/OfflineRenderer.h"

#include <cmath>

using namespace mr;

namespace
{

constexpr double kRate = 48000.0;

Composition composition (const std::string& json)
{
    auto r = mrtest::parse (json);
    if (! r.composition)
        for (const auto& e : r.diagnostics.errors)
            std::fprintf (stderr, "    parse error %s: %s\n", e.path.c_str(), e.message.c_str());
    return *r.composition;
}

struct Rendered
{
    juce::AudioBuffer<float> master;
    juce::File stems;
};

Rendered render (const Composition& c, const std::string& name)
{
    RenderOptions o;
    o.useDefaultRegistry = false;
    o.bitDepth = 32;
    o.output = mrtest::outputDir().getChildFile (name + ".wav");
    o.stemsDir = mrtest::outputDir().getChildFile (name + "_stems");
    renderComposition (c, o);
    return { readAudioFile (o.output).buffer, *o.stemsDir };
}

juce::AudioBuffer<float> stem (const Rendered& r, const std::string& file) { return readAudioFile (r.stems.getChildFile (file)).buffer; }

double rmsDb (const juce::AudioBuffer<float>& b, double fromSec, double toSec, int channel = 0)
{
    const int from = (int) (fromSec * kRate), count = (int) ((toSec - fromSec) * kRate);
    return juce::Decibels::gainToDecibels ((double) b.getRMSLevel (channel, from, count), -160.0);
}

double frequency (const juce::AudioBuffer<float>& b, double fromSec, double toSec)
{
    const int from = (int) (fromSec * kRate), to = (int) (toSec * kRate);
    int crossings = 0, first = -1, last = -1;
    for (int i = from + 1; i < to; ++i)
        if (b.getSample (0, i - 1) < 0.0f && b.getSample (0, i) >= 0.0f)
        {
            if (first < 0) first = i;
            last = i;
            ++crossings;
        }
    return crossings > 1 ? (crossings - 1) * kRate / (last - first) : 0.0;
}

// 자기상관으로 찾은 기본 주파수 (배음이 많은 소리에도 맞는다)
double pitch (const juce::AudioBuffer<float>& b, double fromSec)
{
    const int from = (int) (fromSec * kRate), n = 8192;
    auto x = [&] (int i) { return (double) b.getSample (0, from + i) + b.getSample (1, from + i); };
    int bestLag = 0;
    double best = -1.0;
    for (int lag = (int) (kRate / 1000.0); lag <= (int) (kRate / 40.0); ++lag)
    {
        double num = 0.0, e1 = 0.0, e2 = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double a = x (i), c = x (i + lag);
            num += a * c; e1 += a * a; e2 += c * c;
        }
        const double r = num / std::sqrt (e1 * e2 + 1.0e-12);
        if (r > best + 1.0e-3)
        {
            best = r;
            bestLag = lag;
        }
    }
    return bestLag > 0 ? kRate / bestLag : 0.0;
}

// 밝기의 대용: 표본 사이 차이의 RMS 를 신호의 RMS 로 나눈다 (고역이 많을수록 크다)
double brightness (const juce::AudioBuffer<float>& b, double fromSec, double toSec)
{
    const int from = (int) (fromSec * kRate), to = (int) (toSec * kRate);
    double d = 0.0, x = 0.0;
    for (int i = from + 1; i < to; ++i)
    {
        const double v = b.getSample (0, i), dv = v - b.getSample (0, i - 1);
        d += dv * dv;
        x += v * v;
    }
    return x > 0.0 ? std::sqrt (d / x) : 0.0;
}

EffectSpec spec (const std::string& json)
{
    EffectSpec s;
    s.params = juce::JSON::parse (juce::String (json));
    s.type = s.params.getProperty ("type", "").toString().toStdString();
    s.path = "$.test";
    return s;
}

// 킥(전자 키트)이 박마다, 패드가 길게. 패드 트랙과 버스에 킥을 키로 하는 압축기
const char* pumpJson = R"({ "tempo": 120, "render": { "tailSeconds": 0.5 },
  "buses": [ { "name": "verb", "effects": [ { "type": "compressor", "sidechain": "Kick", "thresholdDb": -36, "ratio": 12, "attackMs": 1, "releaseMs": 120 } ] } ],
  "tracks": [
    { "name": "Kick", "instrument": "synth.edm-drums", "notes": [
        { "pitch": 36, "startBeat": 0, "durationBeat": 0.5, "velocity": 110 }, { "pitch": 36, "startBeat": 1, "durationBeat": 0.5, "velocity": 110 },
        { "pitch": 36, "startBeat": 2, "durationBeat": 0.5, "velocity": 110 }, { "pitch": 36, "startBeat": 3, "durationBeat": 0.5, "velocity": 110 } ] },
    { "name": "Pad", "instrument": "synth.saw-pad", "sends": [ { "bus": "verb", "levelDb": 0, "preFader": true } ],
      "effects": [ { "type": "compressor", "sidechain": "Kick", "thresholdDb": -36, "ratio": 12, "attackMs": 1, "releaseMs": 120 } ],
      "notes": [ { "pitch": "A3", "startBeat": 0, "durationBeat": 4, "velocity": 90 }, { "pitch": "E4", "startBeat": 0, "durationBeat": 4, "velocity": 90 } ] } ] })";

} // namespace

TEST (sidechain_compressor_ducks_on_the_key_only)
{
    auto fx = createEffect (spec (R"({ "type": "compressor", "sidechain": "Kick", "thresholdDb": -30, "ratio": 10, "attackMs": 1, "releaseMs": 100 })"),
                            {}, kRate, 256);
    CHECK_EQ (fx->sidechainSource(), std::string ("Kick"));
    fx->prepare (kRate, 256);
    const int total = (int) kRate;
    juce::AudioBuffer<float> io (2, total), key (2, total);
    key.clear();
    for (int i = 0; i < total; ++i)
    {
        const float x = 0.25f * (float) std::sin (2.0 * 3.141592653589793 * 220.0 * i / kRate);
        io.setSample (0, i, x);
        io.setSample (1, i, x);
        if (i >= (int) (0.5 * kRate) && i < (int) (0.55 * kRate))
            key.setSample (0, i, 0.8f), key.setSample (1, i, 0.8f);
    }
    for (int start = 0; start < total; start += 256)
    {
        const int n = std::min (256, total - start);
        juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, start, n);
        fx->setSidechainInput (key.getReadPointer (0, start), key.getReadPointer (1, start));
        fx->process (view, n);
    }
    const double before = rmsDb (io, 0.2, 0.3), during = rmsDb (io, 0.51, 0.55), after = rmsDb (io, 0.9, 1.0);
    CHECK (during < before - 10.0);
    CHECK (std::abs (after - before) < 1.0);

    // 키를 받지 못한 블록은 무음 키로 본다: 입력이 아무리 커도 줄이지 않는다
    auto quiet = createEffect (spec (R"({ "type": "compressor", "sidechain": "Kick", "thresholdDb": -30, "ratio": 10 })"), {}, kRate, 256);
    quiet->prepare (kRate, 256);
    juce::AudioBuffer<float> loud (2, 4800);
    for (int i = 0; i < 4800; ++i)
        loud.setSample (0, i, 0.9f), loud.setSample (1, i, 0.9f);
    quiet->process (loud, 4800);
    CHECK (std::abs (loud.getSample (0, 4700) - 0.9f) < 1.0e-5f);
}

TEST (delay_echoes_at_the_set_time_and_ping_pongs)
{
    auto fx = createEffect (spec (R"({ "type": "delay", "timeMs": 100, "feedback": 0.5, "mix": 1, "pingPong": 1, "lowCutHz": 10, "highCutHz": 20000 })"),
                            {}, kRate, 512);
    fx->prepare (kRate, 512);
    juce::AudioBuffer<float> b (2, (int) (0.5 * kRate));
    b.clear();
    b.setSample (0, 0, 1.0f);
    b.setSample (1, 0, 1.0f);
    fx->process (b, b.getNumSamples());
    const int d = (int) (0.1 * kRate);
    auto peakNear = [&] (int ch, int at) { return b.getMagnitude (ch, at - 2, 5); };
    CHECK (peakNear (0, d) > 0.5f);          // 첫 메아리는 왼쪽
    CHECK (peakNear (1, d) < 0.01f);
    CHECK (peakNear (1, 2 * d) > 0.2f);      // 둘째는 오른쪽 (되먹임 0.5)
    CHECK (peakNear (0, 2 * d) < 0.01f);
    CHECK (peakNear (1, 2 * d) < peakNear (0, d));
}

TEST (sidechain_pumps_tracks_and_buses_in_a_render)
{
    const auto r = render (composition (pumpJson), "pump");
    const auto pad = stem (r, "Pad.wav");
    const auto verb = stem (r, "bus-verb.wav");
    // 120 BPM: 킥은 0, 0.5, 1.0, 1.5초. 킥 바로 뒤는 눌리고 박의 끝에서는 돌아온다
    for (const auto* b : { &pad, &verb })
    {
        const double ducked = rmsDb (*b, 1.005, 1.04), open = rmsDb (*b, 1.38, 1.48);
        CHECK (ducked < open - 8.0);
    }
    // 키를 무음으로 하면(뮤트) 압축기가 없는 것과 같다
    auto muted = composition (pumpJson);
    muted.tracks[0].mixer.mute = true;
    auto plain = composition (pumpJson);
    plain.tracks[1].effects.clear();
    const auto still = stem (render (muted, "pump_muted"), "Pad.wav");
    const auto reference = stem (render (plain, "pump_plain"), "Pad.wav");
    double maxDiff = 0.0;
    for (int i = 0; i < reference.getNumSamples(); ++i)
        maxDiff = std::max (maxDiff, (double) std::abs (still.getSample (0, i) - reference.getSample (0, i)));
    CHECK (maxDiff < 1.0e-6);
}

TEST (sidechain_references_are_validated)
{
    auto errorsFor = [] (const std::string& json)
    {
        std::string all;
        for (const auto& e : mrtest::parse (json).diagnostics.errors)
            all += e.path + " " + e.message + "\n";
        return all;
    };
    const auto unknown = errorsFor (R"({ "tempo": 120, "tracks": [ { "name": "Pad", "instrument": "synth.pad",
        "effects": [ { "type": "compressor", "sidechain": "Kik" } ], "notes": [] }, { "name": "Kick", "instrument": "synth.edm-drums", "notes": [] } ] })");
    CHECK (unknown.find ("$.tracks[0].effects[0].sidechain unknown track 'Kik'") != std::string::npos);
    const auto self = errorsFor (R"({ "tempo": 120, "tracks": [ { "name": "Pad", "instrument": "synth.pad",
        "effects": [ { "type": "compressor", "sidechain": "Pad" } ], "notes": [] } ] })");
    CHECK (self.find ("cannot key its own compressor") != std::string::npos);
    const auto loop = errorsFor (R"({ "tempo": 120, "tracks": [
        { "name": "A", "instrument": "synth.pad", "effects": [ { "type": "compressor", "sidechain": "B" } ], "notes": [] },
        { "name": "B", "instrument": "synth.pad", "effects": [ { "type": "compressor", "sidechain": "A" } ], "notes": [] } ] })");
    CHECK (loop.find ("form a loop") != std::string::npos);
    const auto busUnknown = errorsFor (R"({ "tempo": 120, "buses": [ { "name": "verb", "effects": [ { "type": "compressor", "sidechain": "Nope" } ] } ],
        "tracks": [ { "name": "Pad", "instrument": "synth.pad", "notes": [] } ] })");
    CHECK (busUnknown.find ("$.buses[0].effects[0].sidechain unknown track 'Nope'") != std::string::npos);
}

TEST (realtime_playback_matches_the_offline_render_with_sidechain)
{
    // 키 트랙(Kick)이 파일에서 뒤에 있어도 먼저 처리된다
    auto c = composition (pumpJson);
    std::swap (c.tracks[0], c.tracks[1]);
    RenderOptions o;
    o.useDefaultRegistry = false;
    o.bitDepth = 32;
    o.output = mrtest::outputDir().getChildFile ("pump_reference.wav");
    renderComposition (c, o);
    const auto reference = readAudioFile (o.output).buffer;

    PlayerOptions po;
    po.useDefaultRegistry = false;
    po.sampleRate = kRate;
    RealtimePlayer player (c, po);
    player.play();
    juce::AudioBuffer<float> live (2, reference.getNumSamples());
    for (int done = 0; done < live.getNumSamples(); done += 300)
    {
        const int n = std::min (300, live.getNumSamples() - done);
        float* ptrs[2] = { live.getWritePointer (0, done), live.getWritePointer (1, done) };
        player.process (ptrs, 2, n);
    }
    double maxDiff = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < reference.getNumSamples(); ++i)
            maxDiff = std::max (maxDiff, (double) std::abs (live.getSample (ch, i) - reference.getSample (ch, i)));
    CHECK (rmsDb (reference, 0.2, 1.8) > -40.0);
    CHECK (maxDiff < 1.0e-5);
}

TEST (electronic_presets_sound_as_designed)
{
    const auto r = render (composition (R"({ "tempo": 120, "render": { "tailSeconds": 0.3 },
      "tracks": [
        { "name": "Saw", "instrument": "synth.supersaw", "notes": [ { "pitch": "A3", "startBeat": 0, "durationBeat": 2, "velocity": 100 } ] },
        { "name": "Bright", "instrument": "synth.saw-bass", "cc74": [ { "beat": 0, "value": 127 } ],
          "notes": [ { "pitch": "A2", "startBeat": 0, "durationBeat": 2, "velocity": 100 } ] },
        { "name": "Dark", "instrument": "synth.saw-bass", "cc74": [ { "beat": 0, "value": 30 } ],
          "notes": [ { "pitch": "A2", "startBeat": 0, "durationBeat": 2, "velocity": 100 } ] },
        { "name": "Kick", "instrument": "synth.edm-drums", "notes": [ { "pitch": 36, "startBeat": 0, "durationBeat": 1, "velocity": 110 } ] },
        { "name": "Clap", "instrument": "synth.edm-drums", "notes": [ { "pitch": 39, "startBeat": 0, "durationBeat": 1, "velocity": 110 } ] },
        { "name": "Hat", "instrument": "synth.edm-drums", "notes": [ { "pitch": 42, "startBeat": 0, "durationBeat": 1, "velocity": 110 } ] } ] })"), "electronic");

    // 슈퍼소: 음높이가 맞고 좌우가 다르다 (유니즌을 벌렸다)
    const auto saw = stem (r, "Saw.wav");
    CHECK (std::abs (pitch (saw, 0.3) - 220.0) < 6.0);
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (int i = (int) (0.3 * kRate); i < (int) (0.9 * kRate); ++i)
    {
        const double a = saw.getSample (0, i), b = saw.getSample (1, i);
        lr += a * b; ll += a * a; rr += b * b;
    }
    CHECK (lr / std::sqrt (ll * rr) < 0.95);

    // CC74 를 내리면 필터가 닫혀 어두워진다
    CHECK (brightness (stem (r, "Dark.wav"), 0.3, 0.9) < 0.6 * brightness (stem (r, "Bright.wav"), 0.3, 0.9));

    // 킥: 꼬리가 50 Hz 근처의 낮은 사인파, 클랩과 하이햇: 소리가 나고 하이햇은 빨리 사라진다
    const auto kick = stem (r, "Kick.wav");
    CHECK (std::abs (frequency (kick, 0.12, 0.3) - 48.0) < 6.0);
    CHECK (rmsDb (stem (r, "Clap.wav"), 0.0, 0.1) > -40.0);
    const auto hat = stem (r, "Hat.wav");
    CHECK (rmsDb (hat, 0.0, 0.03) > -45.0);
    CHECK (rmsDb (hat, 0.2, 0.3) < rmsDb (hat, 0.0, 0.03) - 30.0);
}

// 2 kHz 위 에너지가 전체에서 차지하는 몫 (dB). 바탕음이 큰 음색에서 밝기를 잴 때 쓴다.
double highShareDb (const juce::AudioBuffer<float>& b, double fromSec, double toSec, double cutoffHz = 2000.0)
{
    juce::IIRFilter hp1, hp2;
    hp1.setCoefficients (juce::IIRCoefficients::makeHighPass (kRate, cutoffHz));
    hp2.setCoefficients (juce::IIRCoefficients::makeHighPass (kRate, cutoffHz));
    const int from = (int) (fromSec * kRate), to = (int) (toSec * kRate);
    double hi = 0.0, all = 0.0;
    for (int i = 0; i < std::min (to, b.getNumSamples()); ++i)
    {
        const float x = b.getSample (0, i);
        const float y = hp2.processSingleSampleRaw (hp1.processSingleSampleRaw (x));
        if (i >= from)
        {
            hi += (double) y * y;
            all += (double) x * x;
        }
    }
    return 10.0 * std::log10 (std::max (1.0e-20, hi) / std::max (1.0e-20, all));
}

TEST (keyboard_presets_sound_as_designed)
{
    const auto r = render (composition (R"({ "tempo": 60, "render": { "tailSeconds": 0.5 },
      "tracks": [
        { "name": "EpSoft", "instrument": "synth.ep", "notes": [ { "pitch": "A4", "startBeat": 0, "durationBeat": 3, "velocity": 40 } ] },
        { "name": "EpHard", "instrument": "synth.ep", "notes": [ { "pitch": "A4", "startBeat": 0, "durationBeat": 3, "velocity": 120 } ] },
        { "name": "Keys", "instrument": "synth.poly-keys", "notes": [ { "pitch": "A3", "startBeat": 0, "durationBeat": 3, "velocity": 100 } ] } ] })"), "keyboard");

    // 일렉트릭 피아노: 음높이가 맞고, 세게 치면 밝고(공격 뒤의 몸통에서 벨로시티가 거르개를 정한다), 서서히 사라지되 지속음이 남는다
    const auto soft = stem (r, "EpSoft.wav"), hard = stem (r, "EpHard.wav");
    CHECK (std::abs (pitch (hard, 0.5) - 440.0) < 8.0);
    CHECK (highShareDb (hard, 0.15, 0.6) > highShareDb (soft, 0.15, 0.6) + 3.0);
    CHECK (highShareDb (hard, 0.0, 0.08) > highShareDb (soft, 0.0, 0.08));
    CHECK (rmsDb (hard, 1.8, 2.4) < rmsDb (hard, 0.05, 0.3) - 6.0);
    CHECK (rmsDb (hard, 1.8, 2.4) > rmsDb (hard, 0.05, 0.3) - 30.0);

    // 폴리 키: 음높이가 맞고 좌우가 다르며, 공격 뒤 지속음으로 내려앉는다
    const auto keys = stem (r, "Keys.wav");
    CHECK (std::abs (pitch (keys, 0.5) - 220.0) < 6.0);
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (int i = (int) (0.5 * kRate); i < (int) (1.5 * kRate); ++i)
    {
        const double a = keys.getSample (0, i), b = keys.getSample (1, i);
        lr += a * b; ll += a * a; rr += b * b;
    }
    CHECK (lr / std::sqrt (ll * rr) < 0.97);
    CHECK (rmsDb (keys, 2.0, 2.8) < rmsDb (keys, 0.0, 0.2) - 3.0);
}

TEST (true_peak_limiter_keeps_inter_sample_peaks_under_the_ceiling)
{
    // fs/4 의 사인을 45도 어긋나게: 샘플은 진폭의 0.707 배라 샘플 피크로는 넘지 않지만 실제 봉우리는 넘는다
    const int total = 24000;
    juce::AudioBuffer<float> plain (2, total), tp (2, total);
    for (int i = 0; i < total; ++i)
    {
        const float x = 1.1f * (float) std::sin (2.0 * 3.141592653589793 * 0.25 * i + 0.25 * 3.141592653589793);
        for (auto* buf : { &plain, &tp })
            buf->setSample (0, i, x), buf->setSample (1, i, x);
    }
    auto run = [&] (juce::AudioBuffer<float>& io, const char* json)
    {
        auto fx = createEffect (spec (json), {}, kRate, 512);
        fx->prepare (kRate, 512);
        for (int start = 0; start < total; start += 512)
        {
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, start, std::min (512, total - start));
            fx->process (view, view.getNumSamples());
        }
        return fx->latencySamples();
    };
    run (plain, R"({ "type": "limiter", "ceilingDb": -1 })");
    const int latency = run (tp, R"({ "type": "limiter", "ceilingDb": -1, "truePeak": 1 })");
    CHECK (latency > 0);
    juce::AudioBuffer<float> steadyPlain (2, 12000), steadyTp (2, 12000);
    for (int ch = 0; ch < 2; ++ch)
    {
        steadyPlain.copyFrom (ch, 0, plain, ch, 10000, 12000);
        steadyTp.copyFrom (ch, 0, tp, ch, 10000, 12000);
        // 잘라 낸 구간의 가장자리에서는 보간 탭이 모자라 값이 부푼다: 양 끝을 짧게 페이드한다
        for (auto* buf : { &steadyPlain, &steadyTp })
        {
            buf->applyGainRamp (ch, 0, 64, 0.0f, 1.0f);
            buf->applyGainRamp (ch, 12000 - 64, 64, 1.0f, 0.0f);
        }
    }
    const double ceiling = juce::Decibels::decibelsToGain (-1.0);
    CHECK (truePeak (steadyPlain, 12000) > ceiling * 1.2);     // 샘플 피크 리미터는 놓친다
    CHECK (truePeak (steadyTp, 12000) <= ceiling * 1.005);     // 실제 봉우리 리미터는 잡는다
    CHECK (truePeak (steadyTp, 12000) > ceiling * 0.95);       // 지나치게 깎지 않는다
}

TEST (distortion_adds_harmonics_and_trailer_drums_sound_as_designed)
{
    // 100 Hz 사인에 디스토션: 3배음(300 Hz)이 생긴다. mix 0 이면 그대로다
    auto harmonic = [] (const juce::AudioBuffer<float>& b, double hz)
    {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double w = 2.0 * 3.141592653589793 * hz * i / kRate;
            re += b.getSample (0, i) * std::cos (w);
            im += b.getSample (0, i) * std::sin (w);
        }
        return std::sqrt (re * re + im * im) / b.getNumSamples();
    };
    auto sine = [] (juce::AudioBuffer<float>& b)
    {
        for (int i = 0; i < b.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                b.setSample (ch, i, 0.5f * (float) std::sin (2.0 * 3.141592653589793 * 100.0 * i / kRate));
    };
    juce::AudioBuffer<float> driven (2, 48000), dry (2, 48000);
    sine (driven);
    sine (dry);
    auto fx = createEffect (spec (R"({ "type": "distortion", "driveDb": 18, "toneHz": 12000 })"), {}, kRate, 48000);
    fx->prepare (kRate, 48000);
    fx->process (driven, 48000);
    auto off = createEffect (spec (R"({ "type": "distortion", "driveDb": 18, "mix": 0 })"), {}, kRate, 48000);
    off->prepare (kRate, 48000);
    off->process (dry, 48000);
    CHECK (harmonic (driven, 300.0) > 0.2 * harmonic (driven, 100.0));
    CHECK (harmonic (dry, 300.0) < 0.001);
    CHECK (driven.getMagnitude (0, 0, 48000) <= 1.0f);

    const auto r = render (composition (R"({ "tempo": 60, "render": { "tailSeconds": 3.0 },
      "tracks": [
        { "name": "Boom", "instrument": "synth.trailer-drums", "notes": [ { "pitch": 35, "startBeat": 0, "durationBeat": 1, "velocity": 120 } ] },
        { "name": "Taiko", "instrument": "synth.trailer-drums", "notes": [ { "pitch": 36, "startBeat": 0, "durationBeat": 1, "velocity": 120 } ] },
        { "name": "Anvil", "instrument": "synth.trailer-drums", "notes": [ { "pitch": 56, "startBeat": 0, "durationBeat": 1, "velocity": 120 } ] } ] })"), "trailer");
    const auto boom = stem (r, "Boom.wav"), taiko = stem (r, "Taiko.wav"), anvil = stem (r, "Anvil.wav");
    // 붐의 꼬리: 20~80 Hz 에서 가장 센 성분 (포화된 저음은 영점 교차로 세면 틀린다)
    double bestHz = 0.0, best = 0.0;
    for (double hz = 20.0; hz <= 80.0; hz += 0.5)
    {
        juce::AudioBuffer<float> tail (2, 48000);
        tail.copyFrom (0, 0, boom, 0, 48000, 48000);
        if (const double m = harmonic (tail, hz); m > best)
        {
            best = m;
            bestHz = hz;
        }
    }
    CHECK (std::abs (bestHz - 32.5) < 3.0);                         // 땅이 울리는 저음
    CHECK (rmsDb (boom, 2.0, 2.5) > -40.0);                         // 길게 남는다
    CHECK (std::abs (frequency (taiko, 0.15, 0.5) - 62.0) < 6.0);   // 큰 북의 몸통
    CHECK (rmsDb (taiko, 1.2, 1.5) < rmsDb (taiko, 0.0, 0.1) - 20.0);
    CHECK (brightness (anvil, 0.01, 0.3) > 3.0 * brightness (boom, 0.01, 0.3));   // 모루는 높은 배음

    // 끊기(44): 울리던 큰 북이 바로 멈춘다
    const auto choked = stem (render (composition (R"({ "tempo": 60, "render": { "tailSeconds": 1.0 },
      "tracks": [ { "name": "Taiko", "instrument": "synth.trailer-drums", "notes": [
        { "pitch": 36, "startBeat": 0, "durationBeat": 1, "velocity": 120 }, { "pitch": 44, "startBeat": 0.2, "durationBeat": 0.1, "velocity": 100 } ] } ] })"),
                                          "choke"), "Taiko.wav");
    CHECK (rmsDb (choked, 0.0, 0.15) > -30.0);
    CHECK (rmsDb (choked, 0.3, 0.6) < -70.0);
}

namespace
{
// 120 BPM 의 킥(소리 없는 키, 페이더 -120)과 패드. keyLatency 면 키 트랙에, otherLatency 면 다른 트랙(Room)에 리버브가 있다 (지연 1024 샘플).
std::string keyedSong (bool consumerFx, bool keyLatency, bool otherLatency, bool masterKey)
{
    std::string json = R"({ "tempo": 120, "render": { "tailSeconds": 0.3 },)";
    if (masterKey)
        json += R"( "master": { "effects": [ { "type": "compressor", "sidechain": "Kick", "thresholdDb": -36, "ratio": 12, "attackMs": 0.5, "releaseMs": 120 } ] },)";
    json += R"( "tracks": [
      { "name": "Kick", "instrument": "synth.edm-drums", "mixer": { "gainDb": -120 }, )";
    if (keyLatency)
        json += R"("effects": [ { "type": "reverb", "mix": 0.2 } ], )";
    json += R"("notes": [
        { "pitch": 36, "startBeat": 0, "durationBeat": 0.5, "velocity": 110 }, { "pitch": 36, "startBeat": 1, "durationBeat": 0.5, "velocity": 110 },
        { "pitch": 36, "startBeat": 2, "durationBeat": 0.5, "velocity": 110 }, { "pitch": 36, "startBeat": 3, "durationBeat": 0.5, "velocity": 110 } ] },)";
    if (otherLatency)
        json += R"( { "name": "Room", "instrument": "synth.sine", "mixer": { "gainDb": -120 }, "effects": [ { "type": "reverb", "mix": 0.3 } ],
          "notes": [ { "pitch": "A4", "startBeat": 0, "durationBeat": 1, "velocity": 80 } ] },)";
    json += R"( { "name": "Pad", "instrument": "synth.saw-pad", )";
    if (consumerFx)
        json += R"("effects": [ { "type": "compressor", "sidechain": "Kick", "thresholdDb": -36, "ratio": 12, "attackMs": 0.5, "releaseMs": 120 } ], )";
    json += R"("notes": [ { "pitch": "A3", "startBeat": 0, "durationBeat": 4, "velocity": 90 }, { "pitch": "E4", "startBeat": 0, "durationBeat": 4, "velocity": 90 } ] } ] })";
    return json;
}

// 킥마다 킥 뒤 1~5 ms 의 줄어든 양 (dB, 양수가 줄어듦) 가운데 가장 작은 값
double weakestDuck (const juce::AudioBuffer<float>& ducked, const juce::AudioBuffer<float>& plain)
{
    double weakest = 1.0e9;
    for (int beat = 1; beat < 4; ++beat)
    {
        const double t = beat * 0.5;
        weakest = std::min (weakest, rmsDb (plain, t + 0.001, t + 0.005) - rmsDb (ducked, t + 0.001, t + 0.005));
    }
    return weakest;
}

// 실시간 재생의 출력은 가장 긴 트랙 지연만큼 늦다: 그만큼 당겨 오프라인과 같은 시각에 놓는다
juce::AudioBuffer<float> realtimeAligned (const Composition& c, int samples)
{
    PlayerOptions po;
    po.useDefaultRegistry = false;
    po.sampleRate = kRate;
    RealtimePlayer player (c, po);
    int latency = 0;
    for (size_t i = 0; i < player.trackCount(); ++i)
        latency = std::max (latency, player.trackInfo (i).latencySamples);
    player.play();
    juce::AudioBuffer<float> raw (2, samples + latency);
    for (int done = 0; done < raw.getNumSamples(); done += 300)
    {
        const int n = std::min (300, raw.getNumSamples() - done);
        float* ptrs[2] = { raw.getWritePointer (0, done), raw.getWritePointer (1, done) };
        player.process (ptrs, 2, n);
    }
    juce::AudioBuffer<float> out (2, samples);
    for (int ch = 0; ch < 2; ++ch)
        out.copyFrom (ch, 0, raw, ch, latency, samples);
    return out;
}

double maxDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int count)
{
    double d = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < count; ++i)
            d = std::max (d, (double) std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
    return d;
}
} // namespace

TEST (sidechain_keys_line_up_with_track_latency)
{
    // 다른 트랙의 지연(리버브, 1024 샘플)이 있어도, 키 트랙에 지연이 있어도 킥 뒤 1~5 ms 에 6 dB 넘게 누른다 (오프라인, 실시간)
    for (const bool keyLatency : { false, true })
    {
        const auto keyed = composition (keyedSong (true, keyLatency, true, false));
        const auto plainSong = composition (keyedSong (false, keyLatency, true, false));
        const auto ducked = render (keyed, "sc_latency_keyed").master, plain = render (plainSong, "sc_latency_plain").master;
        const double offlineDuck = weakestDuck (ducked, plain);
        if (! CHECK (offlineDuck > 6.0))
            std::fprintf (stderr, "    offline duck %.2f dB (key latency %d)\n", offlineDuck, (int) keyLatency);
        const auto live = realtimeAligned (keyed, ducked.getNumSamples()), livePlain = realtimeAligned (plainSong, plain.getNumSamples());
        const double realtimeDuck = weakestDuck (live, livePlain);
        if (! CHECK (realtimeDuck > 6.0))
            std::fprintf (stderr, "    realtime duck %.2f dB (key latency %d)\n", realtimeDuck, (int) keyLatency);
        CHECK (maxDifference (ducked, live, ducked.getNumSamples() - 2048) < 1.0e-5);
        CHECK (rmsDb (plain, 1.2, 1.4) > -40.0);
    }
    // 키가 더 늦으면 압축기 앞에서 소리를 늦춘다: 패드 트랙의 지연이 키 트랙의 지연(1024)과 같아진다
    RenderOptions o;
    o.useDefaultRegistry = false;
    o.bitDepth = 32;
    o.output = mrtest::outputDir().getChildFile ("sc_latency_report.wav");
    const auto report = renderComposition (composition (keyedSong (true, true, false, false)), o);
    CHECK_EQ (report.tracks[0].latencySamples, 1024);
    CHECK_EQ (report.tracks[1].latencySamples, 1024);
}

TEST (master_compressor_sidechain_ducks_the_mix)
{
    const auto keyed = composition (keyedSong (false, false, true, true));
    const auto plainSong = composition (keyedSong (false, false, true, false));
    const auto ducked = render (keyed, "sc_master_keyed").master, plain = render (plainSong, "sc_master_plain").master;
    CHECK (weakestDuck (ducked, plain) > 6.0);
    const auto live = realtimeAligned (keyed, ducked.getNumSamples());
    CHECK (maxDifference (ducked, live, ducked.getNumSamples() - 2048) < 1.0e-5);
}
