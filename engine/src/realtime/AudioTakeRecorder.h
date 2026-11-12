// 오디오 입력을 WAV 로 녹음한다. 오디오 콜백이 블록마다 입력과 그 블록의 곡 위치를 넘기면 (잠금, 할당, 파일 입출력 없이)
// 뒤 스레드가 파일에 쓴다. 재생이 시작된 첫 블록부터 받고, 재생이 멈추거나 위치가 끊기면(탐색, 반복) 그 테이크는 끝난다.
// 반복 녹음(loopPasses)이면 위치가 끊겨도 계속 쓰고 끊긴 자리마다 새 반복(pass)을 적는다: 파일 하나에 반복마다의 테이크.
#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace mr
{

namespace codec { class WavFileWriter; }

// 오디오 스레드 하나가 넣고 쓰기 스레드 하나가 꺼내는 채널 둘의 샘플 고리 (잠금과 할당 없음)
class SampleRing
{
public:
    void reset (size_t capacity);
    bool push (const float* const* channels, int numSamples) noexcept;   // 자리가 모자라면 넣지 않고 false
    int pop (float* left, float* right, int maxSamples) noexcept;
    bool empty() const noexcept { return read_.load (std::memory_order_acquire) == write_.load (std::memory_order_acquire); }

private:
    std::vector<float> data_[2];
    size_t capacity_ = 0;
    std::atomic<size_t> write_ { 0 }, read_ { 0 };
};

struct AudioTake
{
    juce::File file;
    int64_t startPosition = 0;   // 첫 샘플을 받은 블록의 곡 위치 (지연은 빼지 않았다)
    int64_t samples = 0;
    double sampleRate = 0.0;
    int channels = 0;
    struct Pass
    {
        int64_t fileStart = 0;       // 파일 안의 첫 샘플
        int64_t startPosition = 0;   // 그 샘플을 받은 곡 위치
        int64_t samples = 0;
    };
    std::vector<Pass> passes;        // 반복 녹음의 반복마다 (한 번이면 하나)
};

class AudioTakeRecorder
{
public:
    AudioTakeRecorder();
    ~AudioTakeRecorder();

    // 메시지 스레드: 24비트 WAV 를 연다 (있으면 실패). channels 는 1 이나 2
    bool start (const juce::File& file, double sampleRate, int channels, std::string& error, bool loopPasses = false);
    // 오디오 스레드: input 의 앞 채널들(모자라면 첫 채널을 되풀이)을 쓴다. playing 은 이 블록에서 재생 중인가.
    // start, stop 과 겹쳐 부르지 않는다 (오디오 엔진이 콜백에 붙이고 떼는 사이에만 부른다)
    void push (const float* const* input, int numInputs, int numSamples, int64_t songPosition, bool playing) noexcept;
    // 메시지 스레드: 파일을 닫는다. 받은 샘플이 없으면 파일을 지우고 nullopt
    std::optional<AudioTake> stop();

    bool recording() const noexcept { return writer_ != nullptr; }
    bool ended() const noexcept { return ended_.load(); }                 // 멈추거나 위치가 끊겨 더 받지 않는다
    int64_t samplesWritten() const noexcept { return written_.load(); }
    int64_t startPosition() const noexcept { return startPosition_.load(); }   // 아직 받지 않았으면 -1
    float peak() noexcept { return peak_.exchange (0.0f); }              // 지난번 뒤의 입력 봉우리 (미터)

private:
    void drain();

    std::unique_ptr<codec::WavFileWriter> writer_;
    SampleRing ring_;
    std::thread thread_;
    std::atomic<bool> running_ { false };
    juce::File file_;
    double sampleRate_ = 0.0;
    int channels_ = 0;
    std::atomic<int64_t> startPosition_ { -1 };
    std::atomic<int64_t> next_ { -1 };
    std::atomic<int64_t> written_ { 0 };
    std::atomic<bool> ended_ { false };
    std::atomic<float> peak_ { 0.0f };
    std::atomic<bool> overflow_ { false };
    bool loopPasses_ = false;
    static constexpr int maxPasses = 256;
    std::array<std::pair<int64_t, int64_t>, maxPasses> passStarts_ {};   // (파일 안의 샘플, 곡 위치), 오디오 스레드가 쓴다
    std::atomic<int> passCount_ { 0 };
};

} // namespace mr
