#include "TestFramework.h"
#include "TestUtil.h"

#include "core/Random.h"
#include "media/Timecode.h"
#include "media/Video.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace mr;
using namespace mr::media;

namespace
{

std::string videoPath (const std::string& name)
{
    return mrtest::outputDir().getChildFile (name).getFullPathName().toStdString();
}

std::string rateName (FrameRate r)
{
    char text[32];
    std::snprintf (text, sizeof (text), "%d_%d", r.num, r.den);
    return text;
}

// 드롭 프레임 타임코드를 프레임마다 하나씩 세어 만든다 (분마다 앞 번호를 건너뛰고 10분째는 건너뛰지 않는다)
std::vector<std::string> countedDropFrame (int fps, int64_t frames)
{
    const int dropped = fps / 15;
    std::vector<std::string> labels;
    labels.reserve ((size_t) frames);
    int hh = 0, mm = 0, ss = 0, ff = 0;
    char text[32];
    for (int64_t n = 0; n < frames; ++n)
    {
        std::snprintf (text, sizeof (text), "%02d:%02d:%02d;%02d", hh, mm, ss, ff);
        labels.push_back (text);
        if (++ff == fps)
        {
            ff = 0;
            if (++ss == 60)
            {
                ss = 0;
                if (++mm == 60)
                {
                    mm = 0;
                    ++hh;
                }
                if (mm % 10 != 0)
                    ff = dropped;
            }
        }
    }
    return labels;
}

// 음악 닮은 시험 신호: 0.25 초마다 바뀌는 화음 (배음 셋) 과 작은 두드림
std::vector<std::vector<float>> musicSignal (double sampleRate, double seconds)
{
    const auto n = (size_t) std::llround (sampleRate * seconds);
    std::vector<std::vector<float>> ch (2, std::vector<float> (n, 0.0f));
    Random rng (17);
    const double roots[] = { 220.0, 261.63, 196.0, 293.66, 246.94, 174.61 };
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / sampleRate;
        const int step = (int) (t / 0.25);
        const double local = t - step * 0.25;
        const double f = roots[step % 6];
        const double env = std::exp (-local * 6.0);
        double v = 0.0;
        for (int h = 1; h <= 3; ++h)
            v += std::sin (2.0 * M_PI * f * h * t + h) / h;
        v *= 0.25 * env;
        v += 0.05 * rng.symmetric() * std::exp (-local * 60.0);
        ch[0][i] = (float) v;
        ch[1][i] = (float) (0.8 * v + 0.05 * std::sin (2.0 * M_PI * 3.0 * f * t));
    }
    return ch;
}

// a 를 lag 만큼 늦춘 것이 b 와 가장 닮은 자리와 그 상관 (lag 은 -maxLag..maxLag)
std::pair<int, double> bestLag (const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t length, int maxLag)
{
    int best = 0;
    double bestCorr = -2.0;
    for (int lag = -maxLag; lag <= maxLag; ++lag)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (size_t i = from; i < from + length; ++i)
        {
            const long j = (long) i - lag;
            if (j < 0 || j >= (long) a.size() || i >= b.size())
                continue;
            ab += (double) a[(size_t) j] * b[i];
            aa += (double) a[(size_t) j] * a[(size_t) j];
            bb += (double) b[i] * b[i];
        }
        const double c = aa > 0.0 && bb > 0.0 ? ab / std::sqrt (aa * bb) : 0.0;
        if (c > bestCorr)
        {
            bestCorr = c;
            best = lag;
        }
    }
    return { best, bestCorr };
}

double rmsDb (const std::vector<float>& x, size_t from, size_t to)
{
    double sum = 0.0;
    to = std::min (to, x.size());
    for (size_t i = from; i < to; ++i)
        sum += (double) x[i] * x[i];
    const double rms = to > from ? std::sqrt (sum / (double) (to - from)) : 0.0;
    return rms > 0.0 ? 20.0 * std::log10 (rms) : -300.0;
}

} // namespace

TEST (timecode_counts_like_smpte)
{
    const FrameRate f30 { 30, 1 }, f25 { 25, 1 }, f24 { 24, 1 };
    CHECK_EQ (timecode (0, f30, false), std::string ("00:00:00:00"));
    CHECK_EQ (timecode (29, f30, false), std::string ("00:00:00:29"));
    CHECK_EQ (timecode (30, f30, false), std::string ("00:00:01:00"));
    CHECK_EQ (timecode (108000, f30, false), std::string ("01:00:00:00"));
    CHECK_EQ (timecode (90000, f25, false), std::string ("01:00:00:00"));
    CHECK_EQ (timecode (24 * 61 + 5, f24, false), std::string ("00:01:01:05"));
    CHECK_EQ (timecode (-30, f30, false), std::string ("-00:00:01:00"));

    const FrameRate ntsc { 30000, 1001 }, ntsc60 { 60000, 1001 };
    CHECK (ntsc.dropFrame());
    CHECK (ntsc60.dropFrame());
    CHECK (! f30.dropFrame());
    CHECK_EQ (ntsc.nominal(), 30);
    CHECK_EQ (timecode (1799, ntsc, true), std::string ("00:00:59;29"));
    CHECK_EQ (timecode (1800, ntsc, true), std::string ("00:01:00;02"));
    CHECK_EQ (timecode (17982, ntsc, true), std::string ("00:10:00;00"));
    CHECK_EQ (timecode (107892, ntsc, true), std::string ("01:00:00;00"));
    CHECK_EQ (timecode (1800, ntsc, false), std::string ("00:01:00:00"));

    // 두 시간 (29.97) 과 20분 (59.94) 의 모든 프레임이 하나씩 센 것과 같다
    for (const auto& [rate, frames] : { std::pair { ntsc, (int64_t) 215784 }, std::pair { ntsc60, (int64_t) 71928 } })
    {
        const auto counted = countedDropFrame (rate.nominal(), frames);
        int64_t mismatches = 0, first = -1;
        for (int64_t n = 0; n < frames; ++n)
            if (timecode (n, rate, true) != counted[(size_t) n])
            {
                if (first < 0)
                    first = n;
                ++mismatches;
            }
        CHECK_EQ (mismatches, (int64_t) 0);
        if (first >= 0)
            std::fprintf (stderr, "  first mismatch at %lld: %s vs %s\n", (long long) first, timecode (first, rate, true).c_str(),
                          counted[(size_t) first].c_str());
    }

    CHECK (frameRateFromFps (29.97) .num == 30000 && frameRateFromFps (29.97).den == 1001);
    CHECK (frameRateFromFps (29.970029f).num == 30000);
    CHECK (frameRateFromFps (23.976).num == 24000 && frameRateFromFps (23.976).den == 1001);
    CHECK (frameRateFromFps (59.94).num == 60000);
    CHECK (frameRateFromFps (25.0).num == 25 && frameRateFromFps (25.0).den == 1);
    CHECK (frameRateFromFps (12.5).num == 12500 && frameRateFromFps (12.5).den == 1000);

    // 프레임 경계: 경계에서 다음 프레임, 그 바로 앞은 앞 프레임
    for (int64_t k = 0; k < 3000; k += 7)
    {
        const double t = secondsAtFrame (k, ntsc);
        CHECK_EQ (frameAtSeconds (t, ntsc), k);
        CHECK_EQ (frameAtSeconds (t + 0.0005, ntsc), k);
        if (k > 0)
            CHECK_EQ (frameAtSeconds (t - 0.0005, ntsc), k - 1);
    }
    CHECK_EQ (timecodeAt (60.0, ntsc), std::string ("00:00:59;28"));   // 60 초는 1798.2 프레임
}

TEST (scene_changes_pick_jumps_above_the_neighbourhood)
{
    Random rng (5);
    std::vector<double> d (300);
    for (auto& v : d)
        v = 0.01 + 0.02 * rng.uniform();
    d[0] = 0.0;
    d[40] = 0.6;                     // 자른 자리
    d[120] = 0.35;
    d[121] = 0.3;                    // 번쩍임: 붙은 두 자리 가운데 하나만
    for (int i = 180; i < 230; ++i)  // 빠른 움직임: 둘레가 크면 그만한 차이는 후보가 아니다
        d[(size_t) i] = 0.15 + 0.05 * rng.uniform();
    d[200] = 0.3;
    d[260] = 0.25;
    const auto picked = pickSceneChanges (d, 30.0, {});
    REQUIRE (picked.size() == 3);
    CHECK_EQ (picked[0], (size_t) 40);
    CHECK_EQ (picked[1], (size_t) 120);
    CHECK_EQ (picked[2], (size_t) 260);
    CHECK (pickSceneChanges ({}, 30.0, {}).empty());
}

TEST (video_frames_match_the_time)
{
    if (! videoSupported())
        return;
    for (const FrameRate rate : { FrameRate { 24, 1 }, FrameRate { 25, 1 }, FrameRate { 30000, 1001 }, FrameRate { 30, 1 } })
    {
        TestPattern p;
        p.rate = rate;
        p.frames = (int) std::lround (3.0 * rate.fps());
        const auto path = videoPath ("pattern_" + rateName (rate) + ".mp4");
        std::string error;
        REQUIRE (writeTestPattern (path, p, error));
        const auto info = probeVideo (path, error);
        REQUIRE (info.has_value());
        CHECK_EQ (info->rate.num, rate.num);
        CHECK_EQ (info->rate.den, rate.den);
        CHECK_EQ (info->frames, (int64_t) p.frames);
        CHECK_EQ (info->width, p.width);
        CHECK_EQ (info->height, p.height);
        CHECK_EQ (info->codec, std::string ("avc1"));
        CHECK (! info->hasAudio);
        CHECK_NEAR (info->durationSeconds, secondsAtFrame (p.frames, rate), 1.0e-3);

        VideoFrames frames (path);
        REQUIRE (frames.ok());
        // 프레임마다 앞 끝 (+0.6 ms), 가운데, 뒤 끝 (-0.6 ms)
        int wrong = 0, checked = 0;
        for (int64_t k = 0; k < p.frames; ++k)
            for (const double where : { 0.0006, 0.5 * rate.frameSeconds(), rate.frameSeconds() - 0.0006 })
            {
                const auto image = frames.frameAt (secondsAtFrame (k, rate) + where);
                ++checked;
                if (! image || readPatternFrame (*image) != k)
                {
                    if (wrong++ < 3)
                        std::fprintf (stderr, "  %s: frame %lld at +%.4f read %lld\n", rateName (rate).c_str(), (long long) k, where,
                                      image ? (long long) readPatternFrame (*image) : -2LL);
                }
            }
        CHECK_EQ (wrong, 0);
        CHECK_EQ (checked, 3 * p.frames);
        // 무작위 시각 (경계 0.5 ms 안은 빼고)
        Random rng (11);
        int randomWrong = 0;
        for (int i = 0; i < 60; ++i)
        {
            const double t = rng.uniform() * secondsAtFrame (p.frames, rate);
            const int64_t k = frameAtSeconds (t, rate);
            if (t - secondsAtFrame (k, rate) < 0.0005 || secondsAtFrame (k + 1, rate) - t < 0.0005)
                continue;
            const auto image = frames.frameAt (t);
            if (! image || readPatternFrame (*image) != k)
                ++randomWrong;
        }
        CHECK_EQ (randomWrong, 0);
        // 줄인 그림
        const auto small = frames.frameAt (1.5, 160, 0);
        REQUIRE (small.has_value());
        CHECK_EQ (small->width, 160);
        CHECK_EQ (small->height, 90);
        CHECK_EQ (readPatternFrame (*small), frameAtSeconds (1.5, rate));
        CHECK (! frames.frameAt (-0.1).has_value());
        CHECK (! frames.frameAt (info->durationSeconds + 0.1).has_value());
    }
}

TEST (video_decoder_plays_frames_in_order)
{
    if (! videoSupported())
        return;
    const FrameRate rate { 30000, 1001 };
    TestPattern p;
    p.rate = rate;
    p.frames = 150;   // 키프레임 사이 60 프레임
    const auto path = videoPath ("pattern_decoder.mp4");
    std::string error;
    REQUIRE (writeTestPattern (path, p, error));
    for (const double start : { 0.0, 1.234, 2.0 * rate.frameSeconds(), 4.9 })
    {
        VideoDecoder decoder (path, start, 0, 0);
        REQUIRE (decoder.ok());
        VideoImage image;
        int64_t expected = frameAtSeconds (start, rate);
        int wrong = 0, count = 0;
        while (decoder.next (image))
        {
            if (readPatternFrame (image) != expected || std::abs (image.seconds - secondsAtFrame (expected, rate)) > 1.0e-4)
                ++wrong;
            ++expected;
            ++count;
        }
        CHECK_EQ (wrong, 0);
        CHECK_EQ (expected, (int64_t) p.frames);
        CHECK_EQ (count, (int) (p.frames - frameAtSeconds (start, rate)));
    }
    VideoDecoder small (path, 0.5, 96, 96);
    VideoImage image;
    REQUIRE (small.next (image));
    CHECK_EQ (image.width, 96);
    CHECK_EQ (image.height, 54);
    CHECK_EQ (readPatternFrame (image), frameAtSeconds (0.5, rate));
}

TEST (video_rotation_matches_the_image_generator)
{
    if (! videoSupported())
        return;
    TestPattern p;
    p.frames = 30;
    p.quarterTurns = 1;
    const auto path = videoPath ("pattern_rotated.mov");
    std::string error;
    REQUIRE (writeTestPattern (path, p, error));
    const auto info = probeVideo (path, error);
    REQUIRE (info.has_value());
    CHECK_EQ (info->width, p.height);
    CHECK_EQ (info->height, p.width);
    VideoFrames frames (path);
    const auto still = frames.frameAt (0.5);
    VideoDecoder decoder (path, 0.5);
    VideoImage played;
    REQUIRE (still.has_value());
    REQUIRE (decoder.next (played));
    REQUIRE (still->width == played.width && still->height == played.height);
    CHECK_EQ (played.width, p.height);
    double diff = 0.0;
    for (size_t i = 0; i < played.rgba.size(); ++i)
        diff += std::abs ((int) played.rgba[i] - (int) still->rgba[i]);
    diff /= (double) played.rgba.size();
    CHECK (diff < 6.0);
    if (diff >= 6.0)
        std::fprintf (stderr, "  mean difference %.2f\n", diff);
}

TEST (video_scene_changes_found_within_a_frame)
{
    if (! videoSupported())
        return;
    for (const FrameRate rate : { FrameRate { 30, 1 }, FrameRate { 25, 1 } })
    {
        TestPattern p;
        p.rate = rate;
        p.frames = (int) std::lround (9.0 * rate.fps());
        p.sceneCuts = { 37, 90, 151, 200 };
        const auto path = videoPath ("pattern_scenes_" + rateName (rate) + ".mp4");
        std::string error;
        REQUIRE (writeTestPattern (path, p, error));
        double lastProgress = 0.0;
        const auto scenes = detectSceneChanges (path, {}, error, [&] (double v) { lastProgress = v; return true; });
        REQUIRE (scenes.has_value());
        CHECK_EQ (scenes->size(), p.sceneCuts.size());
        for (size_t i = 0; i < std::min (scenes->size(), p.sceneCuts.size()); ++i)
        {
            CHECK (std::llabs ((*scenes)[i].frame - p.sceneCuts[i]) <= 1);
            CHECK_NEAR ((*scenes)[i].seconds, secondsAtFrame (p.sceneCuts[i], rate), rate.frameSeconds() + 1.0e-6);
            CHECK ((*scenes)[i].score > 0.2);
        }
        CHECK (lastProgress > 0.9);
        const auto cancelled = detectSceneChanges (path, {}, error, [] (double) { return false; });
        CHECK (! cancelled.has_value());
    }
}

TEST (video_sound_comes_out_where_it_was_put)
{
    if (! videoSupported())
        return;
    TestPattern p;
    p.frames = 90;
    p.clickSeconds = { 0.5, 1.25, 2.0 };
    const auto path = videoPath ("pattern_clicks.mp4");
    std::string error;
    REQUIRE (writeTestPattern (path, p, error));
    const auto info = probeVideo (path, error);
    REQUIRE (info.has_value());
    CHECK (info->hasAudio);
    CHECK_EQ (info->audioChannels, 2);
    CHECK_NEAR (info->audioSampleRate, 48000.0, 0.5);
    const auto sound = readVideoAudio (path, 48000.0, error);
    REQUIRE (sound.has_value());
    CHECK_EQ (sound->channels.size(), (size_t) 2);
    CHECK (std::llabs (sound->frames - 3 * 48000) <= 2048);
    // 클릭 모양을 시각마다 맞춰 본다
    const int clickLength = 240;
    std::vector<float> click (clickLength);
    for (int i = 0; i < clickLength; ++i)
        click[(size_t) i] = (float) (0.5 * (0.5 - 0.5 * std::cos (2.0 * M_PI * i / clickLength)) * std::sin (2.0 * M_PI * 1000.0 * i / 48000.0));
    for (const double at : p.clickSeconds)
    {
        std::vector<float> placed (sound->channels[0].size(), 0.0f);
        const auto start = (size_t) std::llround (at * 48000.0);
        for (int i = 0; i < clickLength && start + (size_t) i < placed.size(); ++i)
            placed[start + (size_t) i] = click[(size_t) i];
        const auto [lag, corr] = bestLag (placed, sound->channels[0], start - 480, 1200, 480);
        CHECK (std::abs (lag) <= 48);
        CHECK (corr > 0.95);
        if (std::abs (lag) > 48 || corr <= 0.95)
            std::fprintf (stderr, "  click at %.2f: lag %d samples, correlation %.4f\n", at, lag, corr);
    }
    // 소리 없는 영상
    const auto silent = readVideoAudio (videoPath ("pattern_decoder.mp4"), 48000.0, error);
    CHECK (! silent.has_value());
}

TEST (scored_video_keeps_the_picture_and_lines_up_the_music)
{
    if (! videoSupported())
        return;
    const FrameRate rate { 30000, 1001 };
    TestPattern p;
    p.rate = rate;
    p.frames = 120;
    p.clickSeconds = { 0.3, 2.2 };
    const auto source = videoPath ("pattern_source.mp4");
    std::string error;
    REQUIRE (writeTestPattern (source, p, error));
    const double sr = 48000.0;
    const auto music = musicSignal (sr, 5.0);
    const float* channels[2] = { music[0].data(), music[1].data() };

    for (const double offset : { 0.5, -1.0 })
    {
        ScoreAudio audio { channels, 2, (int64_t) music[0].size(), sr, offset };
        const auto out = videoPath (offset > 0 ? "scored_late.mp4" : "scored_early.mov");
        const auto report = writeScoredVideo (source, audio, out, {}, error);
        REQUIRE (report.has_value());
        CHECK_NEAR (report->durationSeconds, secondsAtFrame (p.frames, rate), 1.0e-3);
        CHECK_EQ (report->videoSamples, (int64_t) p.frames);
        CHECK (! report->mixedOriginal);

        const auto a = videoSampleDigests (source, error);
        const auto b = videoSampleDigests (out, error);
        REQUIRE (a.has_value() && b.has_value());
        CHECK_EQ (a->size(), (size_t) p.frames);
        CHECK (*a == *b);

        const auto info = probeVideo (out, error);
        REQUIRE (info.has_value());
        CHECK (info->hasAudio);
        CHECK_EQ (info->rate.num, rate.num);
        CHECK_EQ (info->frames, (int64_t) p.frames);
        VideoFrames frames (out);
        const auto image = frames.frameAt (2.0);
        REQUIRE (image.has_value());
        CHECK_EQ (readPatternFrame (*image), frameAtSeconds (2.0, rate));

        const auto sound = readVideoAudio (out, sr, error);
        REQUIRE (sound.has_value());
        // 영상 i 샘플에 곡 i + offset 이 와야 한다
        std::vector<float> expected (sound->channels[0].size(), 0.0f);
        const auto shift = (int64_t) std::llround (offset * sr);
        for (size_t i = 0; i < expected.size(); ++i)
            if ((int64_t) i + shift >= 0 && (int64_t) i + shift < (int64_t) music[0].size())
                expected[i] = music[0][(size_t) ((int64_t) i + shift)];
        const size_t from = offset > 0 ? 4800 : (size_t) (1.2 * sr);
        const auto [lag, corr] = bestLag (expected, sound->channels[0], from, (size_t) (1.5 * sr), 200);
        CHECK (std::abs (lag) <= 48);
        CHECK (corr > 0.99);
        std::fprintf (stderr, "  offset %.1f: lag %d samples, correlation %.5f\n", offset, lag, corr);
        if (offset < 0)
        {
            CHECK (rmsDb (sound->channels[0], 0, (size_t) (0.95 * sr)) < -60.0);   // 곡이 시작하기 전은 조용하다
            CHECK_NEAR (report->musicSeconds, secondsAtFrame (p.frames, rate) - 1.0, 0.01);
        }
    }

    // 원래 소리를 섞는다
    ScoreAudio audio { channels, 2, (int64_t) music[0].size(), sr, 0.0 };
    ScoreVideoOptions mix;
    mix.mixOriginal = true;
    const auto out = videoPath ("scored_mixed.m4v");
    const auto report = writeScoredVideo (source, audio, out, mix, error);
    REQUIRE (report.has_value());
    CHECK (report->mixedOriginal);
    const auto original = readVideoAudio (source, sr, error);
    const auto sound = readVideoAudio (out, sr, error);
    REQUIRE (original.has_value() && sound.has_value());
    std::vector<float> expected (sound->channels[0].size(), 0.0f);
    for (size_t i = 0; i < expected.size(); ++i)
        expected[i] = (i < music[0].size() ? music[0][i] : 0.0f) + (i < original->channels[0].size() ? original->channels[0][i] : 0.0f);
    const auto [lag, corr] = bestLag (expected, sound->channels[0], (size_t) (0.2 * sr), (size_t) (2.5 * sr), 200);
    CHECK (std::abs (lag) <= 48);
    CHECK (corr > 0.99);

    // 출력이 원본을 덮으면 안 된다
    CHECK (! writeScoredVideo (source, audio, source, {}, error).has_value());
    CHECK (! writeScoredVideo (source, audio, videoPath ("scored.avi"), {}, error).has_value());
}

TEST (song_video_field_is_read)
{
    const std::string track = R"("tracks": [{ "name": "A", "instrument": "synth.sine", "notes": [] }])";
    auto r = mrtest::parse (R"({ "tempo": 120, "video": { "file": "clips/promo.mp4", "offsetSeconds": -1.5 }, )" + track + "}");
    REQUIRE (r.composition.has_value());
    REQUIRE (r.composition->video.has_value());
    CHECK_EQ (r.composition->video->file, std::string ("clips/promo.mp4"));
    CHECK_NEAR (r.composition->video->offsetSeconds, -1.5, 1.0e-12);
    CHECK (videoFileOf (*r.composition) == mrtest::sourceDir().getChildFile ("tests/clips/promo.mp4"));
    CHECK (r.diagnostics.warnings.empty());

    auto plain = mrtest::parse (R"({ "tempo": 120, )" + track + "}");
    REQUIRE (plain.composition.has_value());
    CHECK (! plain.composition->video.has_value());
    CHECK (videoFileOf (*plain.composition) == juce::File());

    auto noFile = mrtest::parse (R"({ "tempo": 120, "video": { "offsetSeconds": 1 }, )" + track + "}");
    CHECK (mrtest::hasError (noFile.diagnostics, "$.video.file"));
    auto typo = mrtest::parse (R"({ "tempo": 120, "video": { "file": "a.mp4", "ofset": 1 }, )" + track + "}");
    CHECK (mrtest::hasWarning (typo.diagnostics, "$.video.ofset", "offsetSeconds"));
}

TEST (scored_video_keeps_frame_times_for_every_container)
{
    if (! videoSupported())
        return;
    struct Case { FrameRate rate; const char* source; const char* output; };
    const Case cases[] { { { 25, 1 }, "src25.mov", "out25.mp4" }, { { 25, 1 }, "src25.mp4", "out25.mov" }, { { 24, 1 }, "src24.mp4", "out24.mp4" },
                         { { 30, 1 }, "src30.mov", "out30.m4v" }, { { 30000, 1001 }, "src2997.mov", "out2997.mp4" } };
    const auto music = musicSignal (48000.0, 2.0);
    const float* channels[2] = { music[0].data(), music[1].data() };
    for (const auto& c : cases)
    {
        TestPattern p;
        p.rate = c.rate;
        p.frames = 75;
        std::string error;
        REQUIRE (writeTestPattern (videoPath (c.source), p, error));
        const auto report = writeScoredVideo (videoPath (c.source), { channels, 2, (int64_t) music[0].size(), 48000.0, 0.0 }, videoPath (c.output), {}, error);
        REQUIRE (report.has_value());
        const auto a = videoSampleDigests (videoPath (c.source), error), b = videoSampleDigests (videoPath (c.output), error);
        REQUIRE (a.has_value() && b.has_value());
        CHECK (*a == *b);
        CHECK_NEAR ((*a)[0].seconds, 0.0, 1.0e-6);
        VideoFrames frames (videoPath (c.output));
        int wrong = 0;
        for (int64_t k = 0; k < p.frames; k += 7)
            if (const auto image = frames.frameAt (secondsAtFrame (k, c.rate) + 0.5 * c.rate.frameSeconds()); ! image || readPatternFrame (*image) != k)
                ++wrong;
        CHECK_EQ (wrong, 0);
    }
}
