// 영상 보기: 트랙 보기의 영상 레인 섬네일(뒤 스레드가 읽어 둔다)과 재생 위치의 프레임을 보이는 영상 창.
#pragma once

#include "media/Timecode.h"
#include "media/Video.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

namespace daw
{

juce::Image toImage (const mr::media::VideoImage& image);

// 영상 레인의 섬네일. 칸(step 초)마다 그 시각 둘레의 프레임을 뒤 스레드가 읽는다 (칸의 절반 안의 프레임이면 된다).
// synchronous 면 get 이 그 자리에서 읽는다 (스냅숏과 시험).
class VideoThumbnails final : private juce::Thread, private juce::AsyncUpdater
{
public:
    VideoThumbnails (const juce::File& file, int height, bool synchronous, std::function<void()> changed);
    ~VideoThumbnails() override;

    const juce::File& file() const noexcept { return file_; }
    // 칸 번호 index (시각 index * step) 의 섬네일. 없으면 읽기를 부탁하고 빈 그림
    juce::Image get (int64_t index, double step);

private:
    using Key = std::pair<int64_t, int64_t>;   // 칸 번호, 칸 길이 (마이크로초)
    void run() override;
    void handleAsyncUpdate() override;
    juce::Image read (const Key& key);
    void store (const Key& key, const juce::Image& image);

    const juce::File file_;
    const int height_;
    const bool synchronous_;
    std::function<void()> changed_;
    std::unique_ptr<mr::media::VideoFrames> frames_;   // 읽는 스레드 하나만 쓴다
    std::mutex lock_;
    std::map<Key, juce::Image> cache_;
    std::deque<Key> order_;                            // 오래된 것부터 (캐시 크기를 지킨다)
    std::vector<Key> pending_;                         // 나중에 부탁한 것을 먼저 읽는다
    std::set<Key> failed_;
};

// 재생 위치의 프레임. 멈춘 동안은 그 시각의 프레임을 찾아 읽고, 재생 중에는 차례로 풀어 앞서 읽어 둔다 (뒤 스레드).
class VideoView final : public juce::Component, private juce::AsyncUpdater
{
public:
    VideoView();
    ~VideoView() override;

    // 빈 File 이면 영상이 없다
    void setVideo (const juce::File& file, mr::media::FrameRate rate, double durationSeconds);
    // 보일 시각 (영상의 초, 곡이 영상 밖이면 영상 밖의 시각)
    void setTime (double videoSeconds, bool playing);
    // 그 시각의 프레임을 지금 읽어 보인다 (스냅숏과 시험)
    void showFrameNow (double videoSeconds);
    // 보이는 프레임의 시각 (없으면 비어 있다)
    std::optional<double> shownSeconds() const;
    const juce::Image& shownImage() const noexcept { return image_; }
    // 뒤 스레드가 새로 읽은 프레임을 가져온다 (보통은 알림으로 가져온다, 메시지 루프 없이 기다리는 시험이 부른다)
    bool pollFrame();

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    class Feed;
    void handleAsyncUpdate() override;

    juce::File file_;
    mr::media::FrameRate rate_;
    double duration_ = 0.0;
    double time_ = 0.0;
    juce::Image image_;
    std::optional<double> imageSeconds_;
    std::unique_ptr<Feed> feed_;
};

class VideoWindow final : public juce::DocumentWindow
{
public:
    explicit VideoWindow (juce::Component* centreAround);
    VideoView& view() noexcept { return *view_; }
    std::function<void()> onClose;
    void closeButtonPressed() override;

private:
    VideoView* view_ = nullptr;
};

} // namespace daw
