#include "TestFramework.h"
#include "TestUtil.h"

#include "instruments/QuickSampler.h"
#include "io/AudioFile.h"
#include "rendering/OfflineRenderer.h"

#include <cmath>

using namespace mr;

namespace
{

juce::File tone (const char* name, double frequency, double seconds, double rate = 44100.0)
{
    const auto file = mrtest::outputDir().getChildFile ("sampler").getChildFile (name);
    file.getParentDirectory().createDirectory();
    const int n = (int) (seconds * rate);
    juce::AudioBuffer<float> b (1, n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / rate;
        // 배음이 있는 소리 (톱니파에 가깝게, 첫 배음이 가장 크다)
        double v = 0.0;
        for (int h = 1; h <= 6; ++h)
            v += std::sin (2.0 * juce::MathConstants<double>::pi * frequency * h * t) / h;
        b.setSample (0, i, (float) (0.3 * v * std::exp (-t * 0.5)));
    }
    writeWav (file, b, n, rate, 24);
    return file;
}

double frequencyOf (const juce::AudioBuffer<float>& b, double fromSec, double toSec, double rate)
{
    // 기본 주파수: 아래쪽 영교차 (배음이 있어도 한 주기에 한 번)
    int crossings = 0, first = -1, last = -1;
    for (int i = (int) (fromSec * rate) + 1; i < (int) (toSec * rate); ++i)
        if (b.getSample (0, i - 1) < 0.0f && b.getSample (0, i) >= 0.0f)
        {
            if (first < 0) first = i;
            last = i;
            ++crossings;
        }
    return crossings > 1 ? (crossings - 1) * rate / (last - first) : 0.0;
}

} // namespace

TEST (quick_sampler_measures_pitch)
{
    const auto a3 = tone ("a3.wav", 220.0, 1.5);
    const auto pitch = estimatePitch (a3);
    REQUIRE (pitch.has_value());
    std::printf ("    220 Hz tone: %.2f Hz, midi %.3f, clarity %.2f\n", pitch->frequency, pitch->midi, pitch->clarity);
    CHECK_NEAR (pitch->midi, 57.0, 0.05);
    // 조금 높은 소리 (A4 + 30 센트)
    const auto sharp = tone ("sharp.wav", 440.0 * std::pow (2.0, 30.0 / 1200.0), 1.0);
    const auto p2 = estimatePitch (sharp);
    REQUIRE (p2.has_value());
    CHECK_NEAR (p2->midi, 69.3, 0.05);
    // 잡음은 높이가 없다
    const auto noise = mrtest::outputDir().getChildFile ("sampler/noise.wav");
    {
        juce::AudioBuffer<float> b (1, 44100);
        juce::Random r (3);
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, r.nextFloat() * 2.0f - 1.0f);
        writeWav (noise, b, b.getNumSamples(), 44100.0, 24);
    }
    CHECK (! estimatePitch (noise).has_value());
}

TEST (quick_sampler_sfz_plays_in_tune)
{
#if MUSIC_RENDER_HAS_SFIZZ
    const auto sharp = tone ("lead.wav", 440.0 * std::pow (2.0, 30.0 / 1200.0), 2.0);
    const auto sfz = mrtest::outputDir().getChildFile ("sampler/lead.sfz");
    const auto result = writeSamplerSfz (sfz, sharp, {});
    CHECK_EQ (result.rootKey, 69);
    CHECK_NEAR (result.tuneCents, -30.0, 1.0);
    // 곡 안의 정의로 쓰고 E5 를 치면 659.3 Hz (재서 고친 높이)
    const auto song = mrtest::outputDir().getChildFile ("sampler/song.json");
    song.replaceWithText (R"({ "tempo": 120, "instruments": [ { "lead": { "type": "sfz", "file": "lead.sfz" } } ],
        "tracks": [ { "name": "Lead", "instrument": "lead", "notes": [ { "pitch": 76, "startBeat": 0, "durationBeat": 2, "velocity": 110 } ] } ] })");
    auto parsed = loadComposition (song);
    REQUIRE (parsed.composition.has_value());
    RenderOptions o;
    o.useDefaultRegistry = false;
    o.bitDepth = 32;
    o.output = mrtest::outputDir().getChildFile ("sampler/song.wav");
    renderComposition (*parsed.composition, o);
    const auto out = readAudioFile (o.output).buffer;
    const double f = frequencyOf (out, 0.2, 0.9, 48000.0);
    std::printf ("    E5 from the sample: %.2f Hz (expected 659.26)\n", f);
    CHECK_NEAR (f, 659.26, 2.0);
    // 원샷: 모든 건반이 같은 높이
    const auto drum = mrtest::outputDir().getChildFile ("sampler/hit.sfz");
    const auto hit = writeSamplerSfz (drum, sharp, { std::nullopt, true });
    CHECK (drum.loadFileAsString().contains ("pitch_keytrack=0") && drum.loadFileAsString().contains ("loop_mode=one_shot"));
    juce::ignoreUnused (hit);
#endif
}
