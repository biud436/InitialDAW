#include "TestFramework.h"
#include "TestUtil.h"

#include "io/AudioFile.h"
#include "realtime/AudioTakeRecorder.h"
#include "realtime/RealtimePlayer.h"

#include <cmath>

using namespace mr;

namespace
{

// 블록 하나 (두 채널, 왼쪽은 사인파, 오른쪽은 그 반)
struct Block
{
    std::vector<float> left, right;
    const float* channels[2];

    Block (int n, int64_t start)
        : left ((size_t) n), right ((size_t) n)
    {
        for (int i = 0; i < n; ++i)
        {
            left[(size_t) i] = 0.5f * (float) std::sin (0.01 * (double) (start + i));
            right[(size_t) i] = 0.5f * left[(size_t) i];
        }
        channels[0] = left.data();
        channels[1] = right.data();
    }
};

} // namespace

TEST (audio_take_records_the_playing_blocks_with_their_song_position)
{
    const auto file = mrtest::outputDir().getChildFile ("take_basic.wav");
    file.deleteFile();
    AudioTakeRecorder recorder;
    std::string error;
    REQUIRE (recorder.start (file, 48000.0, 2, error));

    // 카운트인(재생 전)은 받지 않는다
    Block early (512, 0);
    recorder.push (early.channels, 2, 512, 488, false);
    CHECK_EQ (recorder.startPosition(), (int64_t) -1);
    for (int64_t pos = 1000; pos < 1000 + 3 * 512; pos += 512)
    {
        Block b (512, pos);
        recorder.push (b.channels, 2, 512, pos, true);
    }
    CHECK_EQ (recorder.startPosition(), (int64_t) 1000);
    CHECK_EQ (recorder.samplesWritten(), (int64_t) 1536);
    // 위치가 끊기면 (반복, 탐색) 테이크는 끝난다
    Block jump (512, 5000);
    recorder.push (jump.channels, 2, 512, 5000, true);
    CHECK (recorder.ended());
    CHECK_EQ (recorder.samplesWritten(), (int64_t) 1536);

    const auto take = recorder.stop();
    REQUIRE (take.has_value());
    CHECK_EQ (take->startPosition, (int64_t) 1000);
    CHECK_EQ (take->samples, (int64_t) 1536);
    const auto data = readAudioFile (file);
    REQUIRE (data.buffer.getNumSamples() == 1536 && data.buffer.getNumChannels() == 2);
    CHECK_EQ (data.bitDepth, 24);
    double diff = 0.0;
    for (int i = 0; i < 1536; ++i)
    {
        const float expected = 0.5f * (float) std::sin (0.01 * (double) (1000 + i));
        diff = std::max (diff, (double) std::abs (data.buffer.getSample (0, i) - expected));
        diff = std::max (diff, (double) std::abs (data.buffer.getSample (1, i) - 0.5f * expected));
    }
    CHECK (diff < 1.0e-6);
}

TEST (audio_take_loop_passes_continue_in_one_file)
{
    const auto file = mrtest::outputDir().getChildFile ("take_loop.wav");
    file.deleteFile();
    AudioTakeRecorder recorder;
    std::string error;
    REQUIRE (recorder.start (file, 48000.0, 2, error, true));
    // 1000 에서 세 블록, 반복 시작 1000 으로 돌아가 두 블록, 다시 돌아가 한 블록
    for (const auto& [pos, count] : std::vector<std::pair<int64_t, int>> { { 1000, 3 }, { 1000, 2 }, { 1000, 1 } })
        for (int k = 0; k < count; ++k)
        {
            Block b (512, pos + k * 512);
            recorder.push (b.channels, 2, 512, pos + k * 512, true);
        }
    CHECK (! recorder.ended());
    CHECK_EQ (recorder.samplesWritten(), (int64_t) 6 * 512);
    const auto take = recorder.stop();
    REQUIRE (take.has_value());
    REQUIRE (take->passes.size() == 3u);
    CHECK (take->passes[0].fileStart == 0 && take->passes[0].startPosition == 1000 && take->passes[0].samples == 1536);
    CHECK (take->passes[1].fileStart == 1536 && take->passes[1].startPosition == 1000 && take->passes[1].samples == 1024);
    CHECK (take->passes[2].fileStart == 2560 && take->passes[2].samples == 512);
    // 반복 녹음이 아니면 반복 하나
    const auto single = mrtest::outputDir().getChildFile ("take_single.wav");
    single.deleteFile();
    AudioTakeRecorder plain;
    REQUIRE (plain.start (single, 48000.0, 1, error));
    Block b (512, 0);
    plain.push (b.channels, 1, 512, 0, true);
    const auto one = plain.stop();
    REQUIRE (one.has_value());
    CHECK (one->passes.size() == 1u && one->passes[0].samples == 512);
}

TEST (audio_take_mono_input_and_empty_takes)
{
    const auto file = mrtest::outputDir().getChildFile ("take_mono.wav");
    file.deleteFile();
    AudioTakeRecorder recorder;
    std::string error;
    REQUIRE (recorder.start (file, 44100.0, 1, error));
    Block b (256, 0);
    recorder.push (b.channels, 1, 256, 0, true);
    recorder.push (b.channels, 1, 256, 999, false);   // 멈췄다
    CHECK (recorder.ended());
    const auto take = recorder.stop();
    REQUIRE (take.has_value());
    CHECK_EQ (take->channels, 1);
    CHECK_EQ (readAudioFile (file).buffer.getNumChannels(), 1);
    CHECK (recorder.peak() > 0.4f);

    // 받은 것이 없으면 파일을 지운다
    const auto empty = mrtest::outputDir().getChildFile ("take_empty.wav");
    empty.deleteFile();
    REQUIRE (recorder.start (empty, 44100.0, 2, error));
    recorder.push (b.channels, 2, 256, 0, false);
    CHECK (! recorder.stop().has_value());
    CHECK (! empty.exists());
    // 있는 파일에는 쓰지 않는다
    CHECK (! recorder.start (file, 44100.0, 2, error));
}

TEST (realtime_player_reports_its_output_latency)
{
    auto r = mrtest::parse (R"({ "tempo": 120, "tracks": [
        { "name": "A", "instrument": "synth.piano", "effects": [ { "type": "limiter", "ceilingDb": -1 } ], "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 80 } ] },
        { "name": "B", "instrument": "synth.pad", "notes": [ { "pitch": 48, "startBeat": 0, "durationBeat": 1, "velocity": 80 } ] } ] })");
    REQUIRE (r.composition.has_value());
    PlayerOptions o;
    o.useDefaultRegistry = false;
    o.sampleRate = 48000.0;
    o.blockSize = 512;
    RealtimePlayer player (*r.composition, o);
    int most = 0;
    for (size_t i = 0; i < player.trackCount(); ++i)
        most = std::max (most, player.trackInfo (i).latencySamples);
    CHECK (most > 0);   // 리미터는 미리 본다
    CHECK_EQ (player.outputLatencySamples(), most);
}

TEST (input_monitoring_goes_through_the_track)
{
    // 오디오 트랙 (클립 없음, -6 dB, 밸런스 0.5)과 다른 트랙. 입력 모니터링을 켜면 입력이 그 트랙의 페이더와 밸런스를 지난다
    auto r = mrtest::parse (R"({ "tempo": 120, "tracks": [
        { "name": "Mic", "instrument": "audio", "mixer": { "gainDb": -6, "pan": 0.5 } },
        { "name": "Keys", "instrument": "synth.piano", "mixer": { "mute": true }, "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 1 } ] } ] })");
    REQUIRE (r.composition.has_value());
    PlayerOptions o;
    o.useDefaultRegistry = false;
    o.sampleRate = 48000.0;
    o.blockSize = 512;
    o.threads = 0;
    RealtimePlayer player (*r.composition, o);
    const int n = 480;
    std::vector<float> input ((size_t) n);
    for (int i = 0; i < n; ++i)
        input[(size_t) i] = 0.3f * (float) std::sin (i * 0.05);
    const float* in[1] = { input.data() };
    juce::AudioBuffer<float> out (2, n);
    auto run = [&]
    {
        out.clear();
        float* ptrs[2] = { out.getWritePointer (0), out.getWritePointer (1) };
        player.process (ptrs, 2, n, in, 1);
    };
    for (int k = 0; k < 4; ++k)
        run();
    CHECK (out.getMagnitude (0, 0, n) < 1.0e-6f && out.getMagnitude (1, 0, n) < 1.0e-6f);   // 꺼져 있으면 들리지 않는다
    player.setInputMonitor (0);
    for (int k = 0; k < 4; ++k)   // 페이더가 잇는 짧은 시간을 지난다
        run();
    const auto g = faderGains (r.composition->tracks[0].mixer);
    double worst = 0.0;
    for (int i = 0; i < n; ++i)
    {
        worst = std::max (worst, (double) std::abs (out.getSample (0, i) - input[(size_t) i] * g.left));
        worst = std::max (worst, (double) std::abs (out.getSample (1, i) - input[(size_t) i] * g.right));
    }
    CHECK (worst < 1.0e-5);
    CHECK (g.right > g.left);
    // 멈춘 채로도 들리고 (연주자는 재생 전에 소리를 맞춘다), 끄면 그친다
    CHECK (! player.isPlaying());
    player.setInputMonitor (-1);
    for (int k = 0; k < 4; ++k)
        run();
    CHECK (out.getMagnitude (0, 0, n) < 1.0e-6f && out.getMagnitude (1, 0, n) < 1.0e-6f);
}
