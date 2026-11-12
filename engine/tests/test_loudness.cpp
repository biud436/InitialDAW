#include "TestFramework.h"
#include "TestUtil.h"

#include "analysis/Analysis.h"
#include "analysis/LoudnessMeter.h"
#include "realtime/RealtimePlayer.h"

#include <cmath>

using namespace mr;

namespace
{

constexpr double kRate = 48000.0;

juce::AudioBuffer<float> sine (double seconds, double frequency, float amplitude)
{
    const int n = (int) (seconds * kRate);
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i)
    {
        const float v = amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi * frequency * i / kRate);
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    return b;
}

void feed (LoudnessMeter& meter, const juce::AudioBuffer<float>& b, int block = 512)
{
    for (int i = 0; i < b.getNumSamples(); i += block)
    {
        const int n = std::min (block, b.getNumSamples() - i);
        meter.process (b.getReadPointer (0, i), b.getReadPointer (1, i), n);
    }
}

} // namespace

TEST (loudness_meter_reads_a_reference_sine)
{
    LoudnessMeter meter;
    meter.prepare (kRate);
    CHECK_EQ (meter.integrated(), LoudnessMeter::silent);
    feed (meter, sine (5.0, 1000.0, 0.1f));   // -20 dBFS 두 채널: -20 LUFS
    std::printf ("    1 kHz -20 dBFS: M %.2f, S %.2f, I %.2f LUFS, true peak %.2f dB\n", meter.momentary(), meter.shortTerm(), meter.integrated(),
                 meter.truePeakDb());
    CHECK_NEAR (meter.momentary(), -20.0, 0.15);
    CHECK_NEAR (meter.shortTerm(), -20.0, 0.15);
    CHECK_NEAR (meter.integrated(), -20.0, 0.15);
    CHECK_NEAR (meter.truePeakDb(), -20.0, 0.1);
    CHECK_NEAR (meter.measuredSeconds(), 5.0, 0.02);
    // 다시 재기
    meter.requestReset();
    feed (meter, sine (0.2, 1000.0, 0.0f));
    CHECK_EQ (meter.integrated(), LoudnessMeter::silent);
    CHECK_EQ (meter.shortTerm(), LoudnessMeter::silent);
}

TEST (loudness_meter_matches_the_offline_measure)
{
    // 크기가 바뀌는 소리 (조용한 앞부분은 게이트에 걸린다): 오프라인 통합 라우드니스, 트루 피크와 같다
    auto b = sine (12.0, 220.0, 0.3f);
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / kRate;
        const float g = t < 3.0 ? 0.001f : t < 6.0 ? 0.2f : (float) (0.5 + 0.5 * std::sin (t * 3.0));
        const float x = (float) std::sin (2.0 * juce::MathConstants<double>::pi * 3150.0 * t) * 0.4f;   // 높은 음 (K 가중이 키운다)
        b.setSample (0, i, b.getSample (0, i) * g + x * g);
        b.setSample (1, i, b.getSample (1, i) * g * 0.7f);
    }
    LoudnessMeter meter;
    meter.prepare (kRate);
    feed (meter, b, 300);
    const auto offline = integratedLoudness (b, b.getNumSamples(), kRate);
    REQUIRE (offline.has_value());
    const double tp = truePeak (b, b.getNumSamples());
    std::printf ("    integrated realtime %.3f, offline %.3f LUFS; true peak %.3f, offline %.3f dB\n", meter.integrated(), *offline, meter.truePeakDb(),
                 20.0 * std::log10 (tp));
    CHECK_NEAR (meter.integrated(), *offline, 0.1);
    CHECK_NEAR (meter.truePeakDb(), 20.0 * std::log10 (tp), 0.05);
}

TEST (player_measures_its_master)
{
    auto r = mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "Pad", "instrument": "synth.pad", "notes": [
        { "pitch": 60, "startBeat": 0, "durationBeat": 8, "velocity": 100 }, { "pitch": 67, "startBeat": 0, "durationBeat": 8, "velocity": 90 } ] } ] })");
    REQUIRE (r.composition.has_value());
    PlayerOptions o;
    o.useDefaultRegistry = false;
    o.sampleRate = kRate;
    o.blockSize = 512;
    RealtimePlayer player (*r.composition, o);
    player.play();
    const int total = (int) (4.0 * kRate);
    juce::AudioBuffer<float> out (2, total);
    for (int done = 0; done < total;)
    {
        const int n = std::min (441, total - done);
        float* ptrs[2] = { out.getWritePointer (0, done), out.getWritePointer (1, done) };
        player.process (ptrs, 2, n);
        done += n;
    }
    const auto offline = integratedLoudness (out, total, kRate);
    REQUIRE (offline.has_value());
    CHECK_NEAR (player.loudness().integrated(), *offline, 0.1);
    std::vector<float> tail (4096);
    CHECK_EQ (player.copyMasterSamples (tail.data(), 4096), 4096);
    double diff = 0.0;
    for (int i = 0; i < 4096; ++i)
        diff = std::max (diff, (double) std::abs (tail[(size_t) i] - 0.5f * (out.getSample (0, total - 4096 + i) + out.getSample (1, total - 4096 + i))));
    CHECK (diff < 1.0e-6);   // 스펙트럼에 주는 소리는 마지막 출력과 같다
}
