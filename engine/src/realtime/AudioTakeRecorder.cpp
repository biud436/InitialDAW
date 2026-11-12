#include "realtime/AudioTakeRecorder.h"

#include "io/Codecs.h"

#include <algorithm>
#include <chrono>

namespace mr
{

void SampleRing::reset (size_t capacity)
{
    capacity_ = capacity + 1;
    for (auto& d : data_)
        d.assign (capacity_, 0.0f);
    write_.store (0);
    read_.store (0);
}

bool SampleRing::push (const float* const* channels, int numSamples) noexcept
{
    const size_t w = write_.load (std::memory_order_relaxed), r = read_.load (std::memory_order_acquire);
    const size_t used = (w + capacity_ - r) % capacity_;
    if ((size_t) numSamples > capacity_ - 1 - used)
        return false;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < numSamples; ++i)
            data_[c][(w + (size_t) i) % capacity_] = channels[c][i];
    write_.store ((w + (size_t) numSamples) % capacity_, std::memory_order_release);
    return true;
}

int SampleRing::pop (float* left, float* right, int maxSamples) noexcept
{
    const size_t r = read_.load (std::memory_order_relaxed), w = write_.load (std::memory_order_acquire);
    const int n = (int) std::min<size_t> ((size_t) maxSamples, (w + capacity_ - r) % capacity_);
    for (int i = 0; i < n; ++i)
    {
        left[i] = data_[0][(r + (size_t) i) % capacity_];
        right[i] = data_[1][(r + (size_t) i) % capacity_];
    }
    read_.store ((r + (size_t) n) % capacity_, std::memory_order_release);
    return n;
}

// 쓰기 스레드: 고리에서 꺼내 파일에 쓴다. 멈추라고 하면 남은 것을 다 쓰고 끝난다.
void AudioTakeRecorder::drain()
{
    std::vector<float> left (4096), right (4096);
    const float* channels[2] = { left.data(), right.data() };
    for (;;)
    {
        const int n = ring_.pop (left.data(), right.data(), 4096);
        if (n > 0)
        {
            writer_->write (channels, n);
            continue;
        }
        if (! running_.load (std::memory_order_acquire) && ring_.empty())
            return;
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
}

AudioTakeRecorder::AudioTakeRecorder() = default;

AudioTakeRecorder::~AudioTakeRecorder()
{
    stop();
}

bool AudioTakeRecorder::start (const juce::File& file, double sampleRate, int channels, std::string& error, bool loopPasses)
{
    stop();
    channels = std::clamp (channels, 1, 2);
    if (file.exists())
    {
        error = "file exists: " + file.getFullPathName().toStdString();
        return false;
    }
    if (! file.getParentDirectory().createDirectory())
    {
        error = "cannot create directory " + file.getParentDirectory().getFullPathName().toStdString();
        return false;
    }
    auto writer = std::make_unique<codec::WavFileWriter> (file.getFullPathName().toStdString(), sampleRate, channels, 24);
    if (! writer->ok())
    {
        error = "cannot write " + file.getFullPathName().toStdString();
        return false;
    }
    writer_ = std::move (writer);
    // 20초 분량을 오디오 스레드와 쓰기 스레드 사이에 둔다
    ring_.reset ((size_t) (sampleRate * 20.0));
    running_.store (true);
    thread_ = std::thread ([this] { drain(); });
    file_ = file;
    sampleRate_ = sampleRate;
    channels_ = channels;
    startPosition_.store (-1);
    next_.store (-1);
    written_.store (0);
    ended_.store (false);
    overflow_.store (false);
    loopPasses_ = loopPasses;
    passCount_.store (0);
    return true;
}

void AudioTakeRecorder::push (const float* const* input, int numInputs, int numSamples, int64_t songPosition, bool playing) noexcept
{
    if (writer_ == nullptr || ended_.load (std::memory_order_relaxed) || numInputs <= 0 || input == nullptr || input[0] == nullptr)
        return;
    float level = 0.0f;
    for (int ch = 0; ch < std::min (numInputs, 2); ++ch)
        if (input[ch] != nullptr)
            for (int i = 0; i < numSamples; ++i)
                level = std::max (level, input[ch][i]);
    if (level > peak_.load (std::memory_order_relaxed))
        peak_.store (level, std::memory_order_relaxed);

    const int64_t expected = next_.load (std::memory_order_relaxed);
    if (! playing)
    {
        if (expected >= 0)
            ended_.store (true, std::memory_order_relaxed);   // 멈췄다
        return;
    }
    if (expected >= 0 && songPosition != expected)
    {
        if (! loopPasses_)
        {
            ended_.store (true, std::memory_order_relaxed);   // 탐색이나 반복으로 위치가 끊겼다
            return;
        }
        // 반복 녹음: 새 반복을 적고 이어 쓴다
        const int n = passCount_.load (std::memory_order_relaxed);
        if (n < maxPasses)
        {
            passStarts_[(size_t) n] = { written_.load (std::memory_order_relaxed), songPosition };
            passCount_.store (n + 1, std::memory_order_release);
        }
    }
    if (expected < 0)
    {
        startPosition_.store (songPosition, std::memory_order_relaxed);
        passStarts_[0] = { 0, songPosition };
        passCount_.store (1, std::memory_order_release);
    }
    const float* channels[2] = { input[0], numInputs > 1 && input[1] != nullptr ? input[1] : input[0] };
    if (! ring_.push (channels, numSamples))
        overflow_.store (true, std::memory_order_relaxed);
    next_.store (songPosition + numSamples, std::memory_order_relaxed);
    written_.fetch_add (numSamples, std::memory_order_relaxed);
}

std::optional<AudioTake> AudioTakeRecorder::stop()
{
    if (writer_ == nullptr)
        return std::nullopt;
    running_.store (false, std::memory_order_release);   // 남은 것을 쓰고 닫는다
    if (thread_.joinable())
        thread_.join();
    writer_->close();
    writer_.reset();
    AudioTake take { file_, startPosition_.load(), written_.load(), sampleRate_, channels_, {} };
    if (take.samples <= 0 || take.startPosition < 0)
    {
        file_.deleteFile();
        return std::nullopt;
    }
    const int n = passCount_.load (std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
    {
        const auto [fileStart, position] = passStarts_[(size_t) i];
        const int64_t end = i + 1 < n ? passStarts_[(size_t) i + 1].first : take.samples;
        if (end > fileStart)
            take.passes.push_back ({ fileStart, position, end - fileStart });
    }
    return take;
}

} // namespace mr
