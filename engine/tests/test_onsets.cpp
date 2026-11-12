#include "TestFramework.h"
#include "TestUtil.h"

#include "analysis/Onsets.h"
#include "core/Random.h"
#include "instruments/QuickSampler.h"
#include "io/AudioFile.h"
#include "rendering/OfflineRenderer.h"

#include <cmath>

using namespace mr;

namespace
{

constexpr double rate = 48000.0;

// 드럼 루프: 박마다 킥(1, 3박)이나 스네어(2, 4박), 8분음표마다 하이햇. 소리의 시작 시각을 hits 에 남긴다
std::vector<float> drumLoop (double bpm, double beats, std::vector<double>& hits, double swing = 0.0)
{
    const double beat = 60.0 / bpm;
    std::vector<float> x ((size_t) (beats * beat * rate), 0.0f);
    Random noise (7);
    auto add = [&] (double at, int kind)
    {
        const int start = (int) std::lround (at * rate);
        const int length = (int) ((kind == 0 ? 0.35 : kind == 1 ? 0.18 : 0.04) * rate);
        double phase = 0.0, hp = 0.0, previous = 0.0;
        for (int i = 0; i < length && start + i < (int) x.size(); ++i)
        {
            const double t = i / rate;
            double v = 0.0;
            if (kind == 0)
            {
                phase += 2.0 * juce::MathConstants<double>::pi * (50.0 + 100.0 * std::exp (-t * 30.0)) / rate;
                v = 0.8 * std::sin (phase) * std::exp (-t * 9.0);
            }
            else
            {
                const double w = noise.gaussian();
                hp = 0.9 * (hp + w - previous);   // 고역 통과
                previous = w;
                v = (kind == 1 ? 0.35 * std::exp (-t * 22.0) : 0.12 * std::exp (-t * 90.0)) * hp;
            }
            const double fade = std::min (1.0, (length - i) / (0.01 * rate));   // 끝을 10 ms 로 줄인다 (잘린 자리가 어택이 되지 않게)
            x[(size_t) (start + i)] += (float) (v * fade);
        }
    };
    for (int e = 0; e < (int) std::lround (beats * 2.0); ++e)
    {
        double at = e * beat / 2.0;
        if (e % 2 == 1)
            at += swing * beat / 2.0;
        hits.push_back (at);
        add (at, 2);
        if (e % 2 == 0)
            add (at, (e / 2) % 2 == 0 ? 0 : 1);
    }
    return x;
}

} // namespace

TEST (onsets_mark_each_hit_of_a_drum_loop)
{
    std::vector<double> hits;
    const auto x = drumLoop (128.0, 8.0, hits);
    const auto onsets = detectOnsets (x.data(), (int) x.size(), rate);
    std::printf ("    %zu hits, %zu onsets\n", hits.size(), onsets.size());
    REQUIRE (onsets.size() == hits.size());
    double worst = 0.0;
    for (size_t i = 0; i < hits.size(); ++i)
        worst = std::max (worst, std::abs (onsets[i] - hits[i]));
    std::printf ("    worst onset error %.2f ms\n", worst * 1000.0);
    CHECK (worst < 0.004);
    // 어택 앞에서 자른다 (소리의 머리를 잃지 않는다)
    for (size_t i = 0; i < hits.size(); ++i)
        CHECK (onsets[i] <= hits[i] + 0.0005);
}

TEST (tempo_of_a_loop)
{
    std::vector<double> hits;
    const auto loop = drumLoop (128.0, 8.0, hits);
    const auto t = estimateTempo (loop.data(), (int) loop.size(), rate);
    REQUIRE (t.has_value());
    std::printf ("    128 BPM loop: %.3f BPM, confidence %.2f, first beat %.3f s\n", t->bpm, t->confidence, t->firstBeatSeconds);
    CHECK_NEAR (t->bpm, 128.0, 1.0e-6);   // 두 마디 길이에 맞춘다
    CHECK (t->firstBeatSeconds < 0.02);
    // 마디에 맞지 않는 길이 (스윙 있는 92 BPM, 10 박 반): 길이에 맞추지 않고 잰다
    hits.clear();
    const auto swung = drumLoop (92.0, 10.5, hits, 0.2);
    const auto s = estimateTempo (swung.data(), (int) swung.size(), rate);
    REQUIRE (s.has_value());
    std::printf ("    92 BPM swung: %.3f BPM, confidence %.2f\n", s->bpm, s->confidence);
    CHECK_NEAR (s->bpm, 92.0, 0.5);
    // 무음과 어택 하나는 템포가 없다
    std::vector<float> silence (96000, 0.0f);
    CHECK (! estimateTempo (silence.data(), (int) silence.size(), rate).has_value());
}

TEST (slices_play_back_the_loop)
{
#if MUSIC_RENDER_HAS_SFIZZ
    // 120 BPM 에서 조각을 원래 자리(초 x 2 박)에서 치면 원래 루프와 같다 (조각 끝 5 ms 의 줄임만 다르다)
    std::vector<double> hits;
    const auto x = drumLoop (120.0, 8.0, hits);
    juce::AudioBuffer<float> b (1, (int) x.size());
    for (int i = 0; i < (int) x.size(); ++i)
        b.setSample (0, i, x[(size_t) i]);
    const auto dir = mrtest::outputDir().getChildFile ("slices");
    dir.createDirectory();
    const auto loop = dir.getChildFile ("loop.wav");
    writeWav (loop, b, b.getNumSamples(), rate, 24);
    const auto onsets = detectOnsets (x.data(), (int) x.size(), rate);
    REQUIRE (onsets.size() == hits.size());
    writeSliceSfz (dir.getChildFile ("loop-slices.sfz"), loop, onsets, x.size() / rate, 36);
    juce::String notes;
    for (size_t i = 0; i < onsets.size(); ++i)
    {
        const double next = i + 1 < onsets.size() ? onsets[i + 1] : x.size() / rate;
        notes << (i > 0 ? ", " : "") << "{ \"pitch\": " << (36 + (int) i) << ", \"startBeat\": " << juce::String (onsets[i] * 2.0, 6)
              << ", \"durationBeat\": " << juce::String ((next - 0.005 - onsets[i]) * 2.0, 6) << ", \"velocity\": 127, \"humanize\": false }";
    }
    const auto song = dir.getChildFile ("song.json");
    song.replaceWithText ("{ \"tempo\": 120, \"instruments\": [ { \"slices\": { \"type\": \"sfz\", \"file\": \"loop-slices.sfz\" } } ], "
                          "\"tracks\": [ { \"name\": \"Slices\", \"instrument\": \"slices\", \"notes\": [ " + notes + " ] } ] }");
    auto parsed = loadComposition (song);
    REQUIRE (parsed.composition.has_value());
    RenderOptions o;
    o.useDefaultRegistry = false;
    o.bitDepth = 32;
    o.output = dir.getChildFile ("song.wav");
    renderComposition (*parsed.composition, o);
    const auto out = readAudioFile (o.output).buffer;
    // 크기는 그대로 (SFZ 가 sfizz 의 기본 감쇠를 되돌린다). 파형은 256 샘플 칸마다 -1~1 샘플의 어긋남을 허락해 견준다:
    // 음의 박을 소수로 적어 샘플 자리가 반올림에 따라 움직이고, sfizz 가 미리 읽은 데이터에서 나중에 읽은 데이터로 바꿔 탈 때 한 샘플 걷는다
    const auto file = readAudioFile (loop).buffer;
    const int n = std::min (out.getNumSamples(), file.getNumSamples()) - 1;
    double energyIn = 0.0, energyOut = 0.0, error = 0.0;
    for (int i = 0; i < n; ++i)
    {
        energyIn += (double) file.getSample (0, i) * file.getSample (0, i);
        energyOut += (double) out.getSample (0, i) * out.getSample (0, i);
    }
    for (int start = 1; start < n; start += 256)
    {
        double best = 1.0e30;
        for (int lag = -1; lag <= 1; ++lag)
        {
            double e = 0.0;
            for (int i = start; i < std::min (n, start + 256); ++i)
            {
                const double d = file.getSample (0, i) - out.getSample (0, i + lag);
                e += d * d;
            }
            best = std::min (best, e);
        }
        error += best;
    }
    const double gainDb = 10.0 * std::log10 (energyOut / energyIn), errorDb = 10.0 * std::log10 (error / energyIn);
    std::printf ("    slices vs loop: level %+.2f dB, error %.1f dB\n", gainDb, errorDb);
    CHECK (std::abs (gainDb) < 0.5);
    CHECK (errorDb < -30.0);
    const auto sfz = dir.getChildFile ("loop-slices.sfz").loadFileAsString();
    CHECK (sfz.contains ("sample=loop.wav") && sfz.contains ("<region> key=51"));
#endif
}

TEST (beat_tracking_follows_a_speeding_performance)
{
    // 90 에서 110 BPM 으로 빨라지는 40 박 (박마다 ±8 ms 흔들림), 마디 첫 박이 크고 박 사이에 여린 8분음표
    const double rate = 48000.0;
    Random rng (31);
    std::vector<double> truth;
    double t = 0.2;
    for (int i = 0; i < 40; ++i)
    {
        truth.push_back (t + 0.008 * rng.symmetric());
        const double bpm = 90.0 + 20.0 * i / 39.0;
        t += 60.0 / bpm;
    }
    std::vector<float> x ((size_t) ((t + 1.0) * rate), 0.0f);
    auto hit = [&] (double at, double amp, double decay)
    {
        const int a = (int) std::lround (at * rate);
        for (int k = 0; k < 6000 && a + k < (int) x.size(); ++k)
            x[(size_t) (a + k)] += (float) (amp * rng.gaussian() * std::exp (-k / (decay * rate)));
    };
    for (size_t i = 0; i < truth.size(); ++i)
    {
        hit (truth[i], i % 4 == 0 ? 0.5 : 0.3, 0.02);
        if (i + 1 < truth.size())
            hit (0.5 * (truth[i] + truth[i + 1]), 0.08, 0.01);
    }
    const auto beats = trackBeats (x.data(), (int) x.size(), rate);
    if (const auto est = estimateTempo (x.data(), (int) x.size(), rate))
        std::printf ("    overall tempo %.2f\n", est->bpm);
    int matched = 0;
    double worst = 0.0;
    for (const double b : truth)
    {
        double nearest = 1.0e9;
        for (const double d : beats)
            nearest = std::min (nearest, std::abs (d - b));
        if (nearest < 0.015)
            ++matched;
        worst = std::max (worst, nearest);
    }
    std::printf ("    beats: %d found, %d of %d within 15 ms (worst %.1f ms)\n", (int) beats.size(), matched, (int) truth.size(), worst * 1000.0);
    CHECK (matched == (int) truth.size());
    CHECK ((int) beats.size() <= (int) truth.size() + 1);
}
