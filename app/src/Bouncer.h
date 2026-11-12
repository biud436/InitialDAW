// 트랙을 오디오로 굳히기: 엔진의 BounceRender 를 뒤 스레드에서 돌리고, 끝나면 곡에 오디오 트랙을 넣는 편집을 만든다.
#pragma once

#include "composition/Composition.h"
#include "rendering/Bounce.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>

namespace daw
{

struct BounceOutcome
{
    bool ok = false;
    bool cancelled = false;
    juce::String error;
    mr::BounceResult result;
};

// 굳힌 파일을 쓸 자리: 곡 폴더의 audio/<트랙> bounce N.wav
juce::File nextBounceFile (const juce::File& songFile, const std::string& trackName);

// 곡 JSON 에 굳힌 오디오 트랙을 넣는다: 원래 트랙 바로 뒤에 "<이름> 오디오" (색, 믹서, 센드를 옮기고 효과와 음표는 뺀다),
// 원래 트랙은 뮤트. 원래 트랙을 이름으로 찾는다 (없으면 void)
juce::var withBounce (const juce::var& root, const std::string& trackName, const mr::BounceResult& result, const juce::File& songFolder);

// 플러그인 악기가 있으면 악기 만들기를 start() 안(메시지 스레드)에서 한다. 끝나면 onDone 을 메시지 스레드에서 부른다
class BounceJob final : private juce::Thread
{
public:
    BounceJob (mr::Composition composition, size_t track, juce::File output, std::function<void (const BounceOutcome&)> onDone);
    ~BounceJob() override;

    void start();
    void cancel() noexcept { cancel_.store (true); }
    float progress() const noexcept { return progress_.load(); }
    const juce::File& output() const noexcept { return output_; }
    bool running() const { return isThreadRunning(); }

private:
    void run() override;

    mr::Composition composition_;
    size_t track_;
    juce::File output_;
    std::function<void (const BounceOutcome&)> onDone_;
    std::unique_ptr<mr::BounceRender> render_;
    juce::String buildError_;
    bool outputExisted_ = false;
    std::atomic<bool> cancel_ { false };
    std::atomic<float> progress_ { 0.0f };
    std::shared_ptr<bool> alive_ = std::make_shared<bool> (true);
};

} // namespace daw
