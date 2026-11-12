#include "TestFramework.h"
#include "TestUtil.h"

#include "analysis/PitchTrack.h"
#include "core/Random.h"
#include "io/AudioFile.h"

#include <cmath>

using namespace mr;

namespace
{

constexpr double rate = 48000.0;

struct Tone
{
    double start, end, midi, gainDb;
    double vibratoCents = 0.0;
    bool dipAtStart = false;   // 앞 음과 같은 높이를 다시 친다 (어택 앞의 골)
};

// 배음 다섯의 소리를 이어 붙인다. 이어지는 음은 위상이 이어진다 (레가토)
std::vector<float> melody (const std::vector<Tone>& tones, double seconds)
{
    std::vector<float> x ((size_t) (seconds * rate), 0.0f);
    Random noise (4);
    double phase = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = i / rate;
        x[i] = (float) (noise.gaussian() * 0.0005);   // -66 dBFS 쯤의 잡음
        for (const auto& tone : tones)
        {
            if (t < tone.start || t >= tone.end)
                continue;
            const double cents = tone.vibratoCents * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * (t - tone.start));
            const double f = 440.0 * std::pow (2.0, (tone.midi - 69.0 + cents / 100.0) / 12.0);
            phase += 2.0 * juce::MathConstants<double>::pi * f / rate;
            double env = std::min ({ 1.0, (t - tone.start) / 0.005, (tone.end - t) / 0.01 });
            if (tone.dipAtStart)
                env = std::min (1.0, 0.1 + 0.9 * (t - tone.start) / 0.015);
            double v = 0.0;
            for (int h = 1; h <= 5; ++h)
                v += std::sin (phase * h) / h;
            x[i] += (float) (0.25 * std::pow (10.0, tone.gainDb / 20.0) * env * v);
        }
    }
    return x;
}

} // namespace

TEST (pitch_track_follows_a_tone)
{
    const auto x = melody ({ { 0.0, 1.0, 57.0, 0.0 } }, 1.0);
    const auto frames = trackPitch (x.data(), (int) x.size(), rate, 4800, 43200);
    REQUIRE (! frames.empty());
    double worst = 0.0;
    for (const auto& f : frames)
        worst = std::max (worst, std::abs (f.midi - 57.0));
    CHECK (worst < 0.02);
    CHECK_NEAR (frames[10].seconds, 0.15, 1.0e-9);
}

TEST (transcribe_finds_the_notes_of_a_melody)
{
    // 쉼 뒤의 C4, 이어지는 E4 (레가토), 쉼 뒤의 G4, 같은 G4 를 다시 치기, 비브라토(±30 센트)의 A4, 여린 D3
    const std::vector<Tone> tones { { 0.10, 0.60, 60, 0 }, { 0.60, 1.10, 64, 0 }, { 1.25, 1.70, 67, 0 }, { 1.70, 2.10, 67, 0, 0.0, true },
                                    { 2.30, 3.00, 69, 0, 30.0 }, { 3.20, 3.60, 50, -12 } };
    const auto x = melody (tones, 4.0);
    const auto notes = transcribeMelody (x.data(), (int) x.size(), rate);
    std::printf ("    %zu notes:", notes.size());
    for (const auto& n : notes)
        std::printf (" %d (%.3f-%.3f, v%d, %+.1fc)", n.pitch, n.startSeconds, n.endSeconds, n.velocity, n.cents);
    std::printf ("\n");
    REQUIRE (notes.size() == tones.size());
    double worstStart = 0.0, worstEnd = 0.0;
    for (size_t i = 0; i < tones.size(); ++i)
    {
        CHECK_EQ (notes[i].pitch, (int) tones[i].midi);
        worstStart = std::max (worstStart, std::abs (notes[i].startSeconds - tones[i].start));
        worstEnd = std::max (worstEnd, std::abs (notes[i].endSeconds - tones[i].end));
    }
    std::printf ("    worst start %.1f ms, worst end %.1f ms\n", worstStart * 1000.0, worstEnd * 1000.0);
    CHECK (worstStart < 0.025);
    CHECK (worstEnd < 0.04);
    // 12 dB 여린 음은 세기가 낮다
    CHECK (notes[5].velocity < notes[0].velocity - 12);
    CHECK (std::abs (notes[4].cents) < 10.0);   // 비브라토의 가운데
}

TEST (transcribe_ignores_noise_and_silence)
{
    std::vector<float> x (48000, 0.0f);
    CHECK (transcribeMelody (x.data(), (int) x.size(), rate).empty());
    Random r (2);
    for (auto& v : x)
        v = (float) (r.gaussian() * 0.2);
    CHECK (transcribeMelody (x.data(), (int) x.size(), rate).empty());
}

TEST (transcribe_reads_a_part_of_a_file)
{
    const auto x = melody ({ { 0.2, 0.7, 62, 0 }, { 1.0, 1.5, 65, 0 } }, 2.0);
    juce::AudioBuffer<float> b (1, (int) x.size());
    for (int i = 0; i < (int) x.size(); ++i)
        b.setSample (0, i, x[(size_t) i]);
    const auto file = mrtest::outputDir().getChildFile ("transcribe/two.wav");
    file.getParentDirectory().createDirectory();
    writeWav (file, b, b.getNumSamples(), rate, 24);
    const auto all = transcribeMelody (file);
    CHECK (all.size() == 2 && all[0].pitch == 62 && all[1].pitch == 65);
    // 0.9초부터: 둘째 음만, 시각은 파일의 초
    const auto part = transcribeMelody (file, 0.9, 0.8);
    REQUIRE (part.size() == 1);
    CHECK_EQ (part[0].pitch, 65);
    CHECK_NEAR (part[0].startSeconds, 1.0, 0.025);
}
