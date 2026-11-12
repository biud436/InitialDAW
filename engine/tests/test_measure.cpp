#include "TestFramework.h"
#include "TestUtil.h"

#include "analysis/MixMeasure.h"
#include "rendering/OfflineRenderer.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

using namespace mr;

namespace
{

// 두 채널의 사인파를 블록마다 넣는다 (오른쪽은 sign 배)
StemMeter sineMeter (double hz, double amplitude, float sign, double seconds = 1.0, double sr = 48000.0)
{
    StemMeter m (sr);
    const int total = (int) (seconds * sr), block = 512;
    juce::AudioBuffer<float> b (2, block);
    for (int start = 0; start < total; start += block)
    {
        const int n = std::min (block, total - start);
        for (int i = 0; i < n; ++i)
        {
            const float v = (float) (amplitude * std::sin (2.0 * 3.141592653589793 * hz * (start + i) / sr));
            b.setSample (0, i, v);
            b.setSample (1, i, sign * v);
        }
        m.add (b, 0, n);
    }
    m.finish();
    return m;
}

} // namespace

TEST (stem_meter_reads_power_bands_and_side)
{
    auto m = sineMeter (700.0, 0.5, 1.0f);
    CHECK_EQ (m.samples(), (int64_t) 48000);
    CHECK_NEAR (powerDb (m.power (0, 48000)), 10.0 * std::log10 (0.125), 0.05);
    const auto bands = m.bandPower (0, 48000);
    double sum = 0.0;
    for (double v : bands)
        sum += v;
    CHECK_NEAR (powerDb (sum), 10.0 * std::log10 (0.125), 0.1);
    CHECK (bands[6] / sum > 0.99);                           // 500~1000 Hz
    CHECK (m.sidePower (0, 48000) < 1.0e-10);                // 두 채널이 같으면 사이드가 없다
    CHECK_NEAR (powerDb (m.power (12000, 24000)), 10.0 * std::log10 (0.125), 0.05);

    auto wide = sineMeter (700.0, 0.5, -1.0f);               // 반대 위상이면 모두 사이드다
    CHECK_NEAR (powerDb (wide.sidePower (0, 48000)), 10.0 * std::log10 (0.125), 0.05);
}

TEST (stem_meter_puts_a_low_tone_in_the_low_band)
{
    auto m = sineMeter (80.0, 0.25, 1.0f, 2.0);
    const auto bands = m.bandPower (0, 96000);
    double sum = 0.0;
    for (double v : bands)
        sum += v;
    CHECK (bands[2] / sum > 0.97);                           // 60~120 Hz
    CHECK_NEAR (measureBandEdges (48000.0).back(), 24000.0, 1.0e-9);
}

TEST (render_measures_bars_bands_and_note_heads_without_stems)
{
    auto r = mrtest::parse (R"({ "tempo": 120, "render": { "tailSeconds": 0.5 },
      "tracks": [ { "name": "Tone", "instrument": "synth.sine",
                    "notes": [ { "pitch": "A4", "startBeat": 0, "durationBeat": 3, "velocity": 100 },
                               { "pitch": "A4", "startBeat": 4, "durationBeat": 3.5, "velocity": 100 } ] } ] })");
    REQUIRE (r.composition.has_value());
    RenderOptions o;
    o.sampleRate = 48000.0;
    o.useDefaultRegistry = false;
    o.measure = true;
    const auto report = renderComposition (*r.composition, o);
    const auto& m = report.measure;
    REQUIRE (m.applied);
    REQUIRE (m.barStartBeats.size() == 4);                   // 0, 4, 8 박과 꼬리의 끝 (3.75 + 0.5 = 4.25 초 = 8.5 박)
    CHECK_NEAR (m.barStartBeats[1], 4.0, 1.0e-9);
    CHECK_NEAR (m.barStartBeats.back(), 8.5, 1.0e-6);
    CHECK_NEAR (m.barStartSeconds[2], 4.0, 1.0e-6);
    REQUIRE (m.stems.size() == 2);
    const auto& tone = m.stems[0];
    CHECK_EQ (tone.name, std::string ("Tone"));
    CHECK_EQ (m.stems[1].kind, std::string ("master"));
    REQUIRE (tone.barDb.size() == 3);
    CHECK (tone.barDb[0] > -30.0);
    CHECK (tone.barDb[2] < tone.barDb[0] - 20.0);            // 음이 끝난 뒤의 꼬리는 거의 무음
    int loudest = 0;
    for (int b = 1; b < measureBandCount; ++b)
        if (tone.barBandDb[0][(size_t) b] > tone.barBandDb[0][(size_t) loudest])
            loudest = b;
    CHECK_EQ (loudest, 5);                                   // A4 (440 Hz) 는 400~500 Hz
    REQUIRE (tone.notes.size() == 2);
    CHECK_NEAR (tone.notes[1].beat, 4.0, 0.01);
    CHECK_EQ (tone.notes[1].pitch, 69);
    CHECK_NEAR (tone.notes[0].seconds, 1.5, 0.01);
    CHECK_NEAR (tone.notes[1].seconds, 1.75, 0.01);
    CHECK (tone.notes[0].attackDb > -30.0);
    CHECK_NEAR (tone.notes[0].lateDb, tone.notes[0].attackDb, 1.5);   // 이어지는 사인파는 머리 뒤에 사라지지 않는다
    CHECK (report.tracks[0].stem.empty());                   // 스템 파일을 쓰지 않는다
}
