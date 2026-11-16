#include "TestFramework.h"
#include "TestUtil.h"

#include "composition/Document.h"
#include "core/Random.h"
#include "dsp/Restore.h"
#include "instruments/AudioClips.h"
#include "io/AudioFile.h"
#include "rendering/OfflineRenderer.h"

#include <cmath>
#include <cstdio>

using namespace mr;

namespace
{

constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

int at (double seconds) { return (int) std::lround (seconds * kRate); }

double rms (const float* x, int from, int to)
{
    double s = 0.0;
    for (int i = from; i < to; ++i)
        s += (double) x[i] * x[i];
    return std::sqrt (s / std::max (1, to - from));
}

double db (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1.0e-12)); }

// 스테레오 버퍼의 두 채널 RMS (구간, 초)
double rms2 (const juce::AudioBuffer<float>& b, double from, double to)
{
    const double l = rms (b.getReadPointer (0), at (from), at (to)), r = rms (b.getReadPointer (1), at (from), at (to));
    return std::sqrt (0.5 * (l * l + r * r));
}

// 두 버퍼 차이의 RMS (구간, 초)
double differenceRms (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, double from, double to)
{
    double s = 0.0;
    int n = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = at (from); i < at (to); ++i, ++n)
        {
            const double d = (double) a.getSample (ch, i) - b.getSample (ch, i);
            s += d * d;
        }
    return std::sqrt (s / std::max (1, n));
}

// 한 주파수의 크기 (사인파 진폭으로, 구간의 해닝 창 DFT)
double toneAmplitude (const float* x, double from, double to, double hz)
{
    const int a = at (from), n = at (to) - a;
    double re = 0.0, im = 0.0, wsum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / n);
        re += w * x[a + i] * std::cos (2.0 * kPi * hz * i / kRate);
        im -= w * x[a + i] * std::sin (2.0 * kPi * hz * i / kRate);
        wsum += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / wsum;
}

double envelope (double t, double start, double end, double ramp)
{
    if (t < start || t >= end)
        return 0.0;
    const double in = std::min (1.0, (t - start) / ramp), out = std::min (1.0, (end - t) / ramp);
    return std::sin (0.5 * kPi * in) * std::sin (0.5 * kPi * out);
}

// 깨끗한 신호: 1~3 초 사인파 화음 (A3, C#4, E4), 4~7 초 말소리를 닮은 소리 (130~170 Hz 를 오가는 기본음과 배음, 포먼트 셋, 음절 다섯).
// 그 밖 (0~1, 3~4, 7~10 초) 은 소리가 없다
juce::AudioBuffer<float> cleanSignal()
{
    const int n = at (10.0);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kRate;
        double v = 0.0;
        if (const double e = envelope (t, 1.0, 3.0, 0.02); e > 0.0)
            for (const double f : { 220.0, 277.18, 329.63 })
                v += 0.1 * e * std::sin (2.0 * kPi * f * t);
        double syllable = 0.0;
        for (int s = 0; s < 5; ++s)
            syllable = std::max (syllable, envelope (t, 4.0 + s * 0.6, 4.0 + s * 0.6 + 0.45, 0.03));
        const double f0 = 150.0 + 20.0 * std::sin (2.0 * kPi * 0.7 * t);
        phase += 2.0 * kPi * f0 / kRate;
        if (syllable > 0.0)
            for (int h = 1; h * f0 < 4000.0; ++h)
            {
                const double f = h * f0;
                const double formants = std::exp (-std::pow ((f - 700.0) / 180.0, 2.0)) + 0.6 * std::exp (-std::pow ((f - 1200.0) / 220.0, 2.0))
                                        + 0.3 * std::exp (-std::pow ((f - 2600.0) / 300.0, 2.0)) + 0.05;
                v += 0.06 * syllable * formants * std::sin (h * phase);
            }
        b.setSample (0, i, (float) v);
        b.setSample (1, i, (float) v);
    }
    return b;
}

// 채널마다 다른 흰 잡음 (RMS 가 level)
void addNoise (juce::AudioBuffer<float>& b, double level, uint64_t seed)
{
    Random random (seed);
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.addSample (ch, i, (float) (level * random.gaussian()));
}

} // namespace

TEST (restore_reduces_broadband_noise_and_keeps_the_signal)
{
    const auto clean = cleanSignal();
    const double chord = rms2 (clean, 1.2, 2.8);
    const double noiseLevel = chord * std::pow (10.0, -30.0 / 20.0);   // 화음보다 30 dB 아래
    auto noisy = clean;
    addNoise (noisy, noiseLevel, 7);

    for (const bool chosenProfile : { false, true })
    {
        auto out = noisy;
        dsp::RestoreSettings s;
        s.noiseDb = 24.0;
        if (chosenProfile)
        {
            s.profileStart = 7.5;   // 잡음만 들리는 곳
            s.profileLength = 1.0;
        }
        const auto report = dsp::restoreAudio (out, kRate, s);
        CHECK (report.denoised);
        CHECK_NEAR (report.noiseFloorDb, db (noiseLevel), 1.5);
        // 잡음만 있는 자리: 20 dB 넘게 준다 (끝에서 0.2 초 떨어진 안쪽)
        for (const auto& [from, to] : std::vector<std::pair<double, double>> { { 0.2, 0.8 }, { 3.3, 3.8 }, { 7.3, 9.8 } })
        {
            const double reduced = db (rms2 (out, from, to) / rms2 (noisy, from, to));
            std::printf ("    profile %s, noise %.1f-%.1f s: %.1f dB\n", chosenProfile ? "chosen" : "auto", from, to, reduced);
            CHECK (reduced < -20.0);
        }
        // 신호가 있는 자리: 크기가 깨끗한 신호와 1 dB 안, 차이는 원래 잡음보다 작다
        for (const auto& [from, to] : std::vector<std::pair<double, double>> { { 1.2, 2.8 }, { 4.1, 6.9 } })
        {
            const double level = db (rms2 (out, from, to) / rms2 (clean, from, to));
            const double error = db (differenceRms (out, clean, from, to) / rms2 (clean, from, to));
            const double before = db (differenceRms (noisy, clean, from, to) / rms2 (clean, from, to));
            std::printf ("    profile %s, signal %.1f-%.1f s: level %+.2f dB, error %.1f dB (noisy %.1f dB)\n", chosenProfile ? "chosen" : "auto", from, to,
                         level, error, before);
            CHECK (std::abs (level) < 1.0);
            CHECK (error < before);
        }
    }
    // 잡음을 줄이지 않으면 (다른 복원도 없으면) 그대로다
    auto same = noisy;
    dsp::restoreAudio (same, kRate, {});
    CHECK (differenceRms (same, noisy, 0.0, 10.0) == 0.0);
}

TEST (restore_removes_mains_hum_and_its_harmonics)
{
    for (const auto& [actual, nominal] : std::vector<std::pair<double, double>> { { 60.0, -1.0 }, { 49.9, 50.0 } })
    {
        auto clean = cleanSignal();
        addNoise (clean, 1.0e-4, 3);   // -80 dB 의 잡음 바닥
        auto hummed = clean;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < hummed.getNumSamples(); ++i)
                for (int k = 1; k <= 10; ++k)
                    hummed.addSample (ch, i, (float) (0.02 / k * std::sin (2.0 * kPi * k * actual * i / kRate + 0.3 * k)));
        auto out = hummed;
        dsp::RestoreSettings s;
        s.humHz = nominal;
        const auto report = dsp::restoreAudio (out, kRate, s);
        std::printf ("    hum %.1f Hz (asked %.0f): measured %.3f Hz, %d notches\n", actual, nominal, report.humHz, (int) report.humNotches.size());
        CHECK_NEAR (report.humHz, actual, 0.05);
        CHECK (report.humNotches.size() == 10);
        double worst = -200.0;
        for (int k = 1; k <= 10; ++k)
        {
            const double before = toneAmplitude (hummed.getReadPointer (0), 2.0, 8.0, k * actual);
            const double after = toneAmplitude (out.getReadPointer (0), 2.0, 8.0, k * actual);
            worst = std::max (worst, db (after / before));
        }
        std::printf ("    hum %.1f Hz: weakest reduction %.1f dB\n", actual, worst);
        CHECK (worst < -40.0);
        // 화음은 그대로 (0.3 dB 안)
        for (const double f : { 220.0, 277.18, 329.63 })
            CHECK (std::abs (db (toneAmplitude (out.getReadPointer (0), 1.2, 2.8, f) / toneAmplitude (clean.getReadPointer (0), 1.2, 2.8, f))) < 0.3);
    }
    // 험이 없으면 찾지 않는다
    auto plain = cleanSignal();
    addNoise (plain, 1.0e-4, 5);
    dsp::RestoreSettings s;
    s.humHz = -1.0;
    const auto none = dsp::restoreAudio (plain, kRate, s);
    CHECK (none.humHz == 0.0);
    CHECK (none.humNotches.empty());
}

TEST (restore_fills_clicks_from_the_sound_around_them)
{
    auto clean = cleanSignal();
    addNoise (clean, 1.0e-4, 11);
    // 클릭이 없으면 찾지 않는다
    std::vector<float> mono ((size_t) clean.getNumSamples());
    for (int i = 0; i < clean.getNumSamples(); ++i)
        mono[(size_t) i] = clean.getSample (0, i);
    CHECK (dsp::findClicks (mono.data(), (int64_t) mono.size(), kRate).empty());

    // 클릭 서른: 한 샘플, 세 샘플, 1 ms 에 사라지는 튐을 소리 위, 조용한 곳에 고루
    auto clicked = clean;
    Random random (19);
    std::vector<int> places;
    for (int c = 0; c < 30; ++c)
    {
        const int place = at (0.3) + (int) (random.uniform() * (at (9.6) - at (0.3)));
        places.push_back (place);
        const double size = (0.3 + 0.4 * random.uniform()) * (random.uniform() < 0.5 ? -1.0 : 1.0);
        const int kind = c % 3;
        const int length = kind == 0 ? 1 : kind == 1 ? 3 : at (0.001);
        for (int k = 0; k < length; ++k)
        {
            const double v = kind == 2 ? size * std::exp (-k / 10.0) * (k % 2 == 0 ? 1.0 : -1.0) : size;
            for (int ch = 0; ch < 2; ++ch)
                clicked.addSample (ch, place + k, (float) v);
        }
    }
    auto out = clicked;
    dsp::RestoreSettings s;
    s.declick = true;
    const auto report = dsp::restoreAudio (out, kRate, s);
    std::vector<float> mixed ((size_t) clicked.getNumSamples());
    for (int i = 0; i < clicked.getNumSamples(); ++i)
        mixed[(size_t) i] = clicked.getSample (0, i);
    const auto found = dsp::findClicks (mixed.data(), (int64_t) mixed.size(), kRate);
    int covered = 0;
    for (const int p : places)
        for (const auto& c : found)
            if (p >= c.start && p < c.start + c.length)
            {
                ++covered;
                break;
            }
    const double whole = db (differenceRms (out, clean, 0.0, 10.0) / rms2 (clean, 0.0, 10.0));
    const double before = db (differenceRms (clicked, clean, 0.0, 10.0) / rms2 (clean, 0.0, 10.0));
    std::printf ("    clicks: %d inserted, %d filled, %d covered, difference %.1f dB (with clicks %.1f dB)\n", (int) places.size(), report.clicks,
                 covered, whole, before);
    CHECK (covered == (int) places.size());
    CHECK (report.clicks == (int) found.size());
    CHECK (whole < -40.0);
}

TEST (restore_runs_inside_clip_playback)
{
    // 파일: 잡음이 섞인 화음. 클립의 restore 는 클립을 준비할 때 파일의 소리를 고친다
    auto noisy = cleanSignal();
    addNoise (noisy, 0.003, 23);
    const auto file = mrtest::outputDir().getChildFile ("restore_take.wav");
    writeWav (file, noisy, noisy.getNumSamples(), kRate, 32);
    const auto song = R"({ "tempo": 120, "tracks": [ { "name": "Take", "instrument": "audio", "clips": [ { "file": ")" + file.getFullPathName().toStdString()
                      + R"(", "startBeat": 0, "restore": { "noiseDb": 18, "profile": { "seconds": 7.5, "lengthSeconds": 1 }, "humHz": "auto", "declick": true } } ] } ] })";
    auto parsed = mrtest::parse (song);
    REQUIRE (parsed.composition.has_value());
    const auto& clip = parsed.composition->tracks[0].clips[0];
    REQUIRE (clip.restore.has_value());
    CHECK (clip.restore->noiseDb == 18.0);
    CHECK (clip.restore->humHz == -1.0);
    CHECK (clip.restore->declick);
    CHECK (clip.restore->profileSeconds == 7.5);
    // 저장 형식으로 다시 읽어도 같다
    const auto saved = clipJson (clip);
    CHECK (saved["restore"]["humHz"].toString() == "auto");
    CHECK ((double) saved["restore"]["profile"]["lengthSeconds"] == 1.0);

    // 렌더링한 소리는 같은 값으로 직접 복원한 파일의 소리와 같다
    RenderOptions o;
    o.useDefaultRegistry = false;
    o.bitDepth = 32;
    o.output = mrtest::outputDir().getChildFile ("restore_render.wav");
    renderComposition (*parsed.composition, o);
    const auto rendered = readAudioFile (o.output).buffer;
    auto direct = noisy;
    dsp::RestoreSettings s;
    s.noiseDb = 18.0;
    s.profileStart = 7.5;
    s.profileLength = 1.0;
    s.humHz = -1.0;
    s.declick = true;
    dsp::restoreAudio (direct, kRate, s);
    double worst = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = at (0.5); i < at (9.5); ++i)
            worst = std::max (worst, (double) std::abs (rendered.getSample (ch, i) - direct.getSample (ch, i)));
    std::printf ("    clip playback against direct restore: %.2e\n", worst);
    CHECK (worst < 1.0e-5);
    CHECK (db (rms2 (rendered, 7.6, 9.4) / rms2 (noisy, 7.6, 9.4)) < -15.0);

    // 뒤집으면 잡음 지문 구간도 거울 자리로 간다 (10 초 파일의 7.5~8.5 초는 1.5~2.5 초)
    const auto reversed = withClipReversed (clip, 10.0);
    REQUIRE (reversed.restore.has_value());
    CHECK_NEAR (*reversed.restore->profileSeconds, 1.5, 1.0e-6);

    // 범위 밖과 모르는 값은 오류다
    CHECK (mrtest::hasError (mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "T", "instrument": "audio", "clips": [ { "file": "a.wav", "startBeat": 0, "restore": { "noiseDb": 50 } } ] } ] })").diagnostics,
                             "$.tracks[0].clips[0].restore.noiseDb"));
    CHECK (mrtest::hasError (mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "T", "instrument": "audio", "clips": [ { "file": "a.wav", "startBeat": 0, "restore": { "humHz": "loud" } } ] } ] })").diagnostics,
                             "$.tracks[0].clips[0].restore.humHz"));
}
