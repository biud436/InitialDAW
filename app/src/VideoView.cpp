#include "VideoView.h"

#include "Theme.h"

#include <cmath>
#include <limits>

namespace daw
{

namespace
{

juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }

constexpr size_t thumbnailCacheSize = 800;
constexpr size_t pendingLimit = 96;

} // namespace

juce::Image toImage (const mr::media::VideoImage& image)
{
    if (image.empty())
        return {};
    juce::Image out (juce::Image::ARGB, image.width, image.height, false);
    juce::Image::BitmapData data (out, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < image.height; ++y)
    {
        const uint8_t* src = &image.rgba[(size_t) y * (size_t) image.width * 4];
        for (int x = 0; x < image.width; ++x, src += 4)
            data.setPixelColour (x, y, juce::Colour (src[0], src[1], src[2], (juce::uint8) 255));
    }
    return out;
}

// ---------------------------------------------------------------------------
// VideoThumbnails
// ---------------------------------------------------------------------------

VideoThumbnails::VideoThumbnails (const juce::File& file, int height, bool synchronous, std::function<void()> changed)
    : juce::Thread ("InitialDAW video thumbnails"), file_ (file), height_ (std::max (8, height)), synchronous_ (synchronous),
      changed_ (std::move (changed))
{
    if (! synchronous_)
        startThread (juce::Thread::Priority::low);
}

VideoThumbnails::~VideoThumbnails()
{
    cancelPendingUpdate();
    signalThreadShouldExit();
    notify();
    stopThread (-1);
}

juce::Image VideoThumbnails::read (const Key& key)
{
    if (frames_ == nullptr)
        frames_ = std::make_unique<mr::media::VideoFrames> (file_.getFullPathName().toStdString());
    if (! frames_->ok())
        return {};
    const double step = (double) key.second * 1.0e-6;
    const double seconds = std::clamp ((double) key.first * step, 0.0, std::max (0.0, frames_->info().durationSeconds - 1.0e-3));
    const auto image = frames_->frameAt (seconds, 0, height_, step * 0.5);
    return image ? toImage (*image) : juce::Image();
}

void VideoThumbnails::store (const Key& key, const juce::Image& image)
{
    std::lock_guard<std::mutex> hold (lock_);
    if (! image.isValid())
    {
        failed_.insert (key);
        return;
    }
    if (cache_.emplace (key, image).second)
        order_.push_back (key);
    while (order_.size() > thumbnailCacheSize)
    {
        cache_.erase (order_.front());
        order_.pop_front();
    }
}

juce::Image VideoThumbnails::get (int64_t index, double step)
{
    const Key key { index, (int64_t) std::llround (step * 1.0e6) };
    {
        std::lock_guard<std::mutex> hold (lock_);
        if (const auto it = cache_.find (key); it != cache_.end())
            return it->second;
        if (failed_.count (key) > 0)
            return {};
        if (! synchronous_)
        {
            if (std::find (pending_.begin(), pending_.end(), key) == pending_.end())
            {
                pending_.push_back (key);
                if (pending_.size() > pendingLimit)
                    pending_.erase (pending_.begin());
            }
        }
    }
    if (! synchronous_)
    {
        notify();
        return {};
    }
    const auto image = read (key);
    store (key, image);
    return image;
}

void VideoThumbnails::run()
{
    while (! threadShouldExit())
    {
        std::optional<Key> key;
        {
            std::lock_guard<std::mutex> hold (lock_);
            if (! pending_.empty())
            {
                key = pending_.back();
                pending_.pop_back();
            }
        }
        if (! key)
        {
            wait (200);
            continue;
        }
        store (*key, read (*key));
        triggerAsyncUpdate();
    }
}

void VideoThumbnails::handleAsyncUpdate()
{
    if (changed_)
        changed_();
}

// ---------------------------------------------------------------------------
// VideoView 의 뒤 스레드
// ---------------------------------------------------------------------------

class VideoView::Feed final : private juce::Thread
{
public:
    explicit Feed (VideoView& owner) : juce::Thread ("InitialDAW video"), owner_ (owner) { startThread (juce::Thread::Priority::normal); }

    ~Feed() override
    {
        signalThreadShouldExit();
        notify();
        stopThread (-1);
    }

    void setFile (const juce::File& file)
    {
        {
            std::lock_guard<std::mutex> hold (lock_);
            file_ = file;
            ++generation_;
        }
        notify();
    }

    void setTarget (double seconds, bool playing)
    {
        {
            std::lock_guard<std::mutex> hold (lock_);
            target_ = seconds;
            playing_ = playing;
        }
        notify();
    }

    void setSize (int width, int height)
    {
        std::lock_guard<std::mutex> hold (lock_);
        maxWidth_ = std::max (64, width);
        maxHeight_ = std::max (36, height);
    }

    // 보일 프레임 (그림, 시각). 새것이 없으면 false
    bool take (juce::Image& image, std::optional<double>& seconds)
    {
        std::lock_guard<std::mutex> hold (lock_);
        if (! fresh_)
            return false;
        fresh_ = false;
        image = published_;
        seconds = publishedSeconds_;
        return true;
    }

private:
    void publish (const mr::media::VideoImage* frame)
    {
        auto image = frame != nullptr ? toImage (*frame) : juce::Image();
        {
            std::lock_guard<std::mutex> hold (lock_);
            published_ = image;
            publishedSeconds_ = frame != nullptr ? std::optional<double> (frame->seconds) : std::nullopt;
            fresh_ = true;
        }
        owner_.triggerAsyncUpdate();   // 주인은 이 스레드를 멈춘 뒤에 사라진다
    }

    void run() override
    {
        std::unique_ptr<mr::media::VideoFrames> frames;
        std::unique_ptr<mr::media::VideoDecoder> decoder;
        std::deque<mr::media::VideoImage> queue;
        bool decoderEnded = false;
        int generation = -1;
        juce::File file;
        double lastStill = std::numeric_limits<double>::quiet_NaN();
        std::optional<double> shown;
        bool shownNothing = false;
        while (! threadShouldExit())
        {
            double target = 0.0;
            bool playing = false;
            int width = 0, height = 0;
            {
                std::lock_guard<std::mutex> hold (lock_);
                target = target_;
                playing = playing_;
                width = maxWidth_;
                height = maxHeight_;
                if (generation != generation_)
                {
                    generation = generation_;
                    file = file_;
                    frames.reset();
                    decoder.reset();
                    queue.clear();
                    lastStill = std::numeric_limits<double>::quiet_NaN();
                    shown.reset();
                    shownNothing = false;
                }
            }
            if (file == juce::File())
            {
                wait (100);
                continue;
            }
            if (frames == nullptr)
                frames = std::make_unique<mr::media::VideoFrames> (file.getFullPathName().toStdString());
            const double duration = frames->ok() ? frames->info().durationSeconds : 0.0;
            if (target < 0.0 || target >= duration)
            {
                // 곡이 영상 밖이다
                if (! shownNothing)
                {
                    publish (nullptr);
                    shownNothing = true;
                    shown.reset();
                }
                decoder.reset();
                queue.clear();
                lastStill = target;
                wait (playing ? 10 : 50);
                continue;
            }
            shownNothing = false;
            if (! playing)
            {
                decoder.reset();
                queue.clear();
                if (target != lastStill)
                {
                    lastStill = target;
                    if (const auto image = frames->frameAt (target, width, height))
                    {
                        publish (&*image);
                        shown = image->seconds;
                    }
                }
                wait (50);
                continue;
            }
            lastStill = std::numeric_limits<double>::quiet_NaN();
            // 재생: 뒤로 갔거나 멀리 앞으로 갔으면 그 자리에서 다시 푼다
            const bool behind = shown && target < *shown - 1.0e-3;
            const bool farAhead = ! queue.empty() ? target > queue.back().seconds + 1.0 : (shown && target > *shown + 1.0);
            if (decoder == nullptr || behind || farAhead)
            {
                decoder = std::make_unique<mr::media::VideoDecoder> (file.getFullPathName().toStdString(), target, width, height);
                queue.clear();
                decoderEnded = false;
                shown.reset();
            }
            while (! decoderEnded && queue.size() < 8 && (queue.empty() || queue.back().seconds < target + 0.25))
            {
                mr::media::VideoImage image;
                if (! decoder->next (image))
                {
                    decoderEnded = true;
                    break;
                }
                queue.push_back (std::move (image));
            }
            while (queue.size() > 1 && queue[1].seconds <= target + 1.0e-6)
                queue.pop_front();
            if (! queue.empty() && queue.front().seconds <= target + 1.0e-6 && (! shown || queue.front().seconds != *shown))
            {
                publish (&queue.front());
                shown = queue.front().seconds;
            }
            wait (5);
        }
    }

    VideoView& owner_;
    std::mutex lock_;
    juce::File file_;
    int generation_ = 0;
    double target_ = 0.0;
    bool playing_ = false;
    int maxWidth_ = 960, maxHeight_ = 540;
    juce::Image published_;
    std::optional<double> publishedSeconds_;
    bool fresh_ = false;
};

// ---------------------------------------------------------------------------
// VideoView
// ---------------------------------------------------------------------------

VideoView::VideoView()
{
    setOpaque (true);
    setSize (480, 270);
}

VideoView::~VideoView()
{
    cancelPendingUpdate();
    feed_.reset();
}

void VideoView::setVideo (const juce::File& file, mr::media::FrameRate rate, double durationSeconds)
{
    if (file == file_ && rate.num == rate_.num && rate.den == rate_.den)
        return;
    file_ = file;
    rate_ = rate;
    duration_ = durationSeconds;
    image_ = {};
    imageSeconds_.reset();
    if (file_ != juce::File() && feed_ == nullptr)
    {
        feed_ = std::make_unique<Feed> (*this);
        resized();
    }
    if (feed_ != nullptr)
        feed_->setFile (file_);
    repaint();
}

void VideoView::setTime (double videoSeconds, bool playing)
{
    const bool changed = std::abs (videoSeconds - time_) > 1.0e-9;
    time_ = videoSeconds;
    if (feed_ != nullptr)
        feed_->setTarget (videoSeconds, playing);
    if (changed)
        repaint();
}

void VideoView::showFrameNow (double videoSeconds)
{
    time_ = videoSeconds;
    image_ = {};
    imageSeconds_.reset();
    if (file_ != juce::File())
    {
        mr::media::VideoFrames frames (file_.getFullPathName().toStdString());
        const int scale = 2;
        if (const auto frame = frames.frameAt (videoSeconds, getWidth() * scale, getHeight() * scale))
        {
            image_ = toImage (*frame);
            imageSeconds_ = frame->seconds;
        }
    }
    repaint();
}

std::optional<double> VideoView::shownSeconds() const
{
    return imageSeconds_;
}

bool VideoView::pollFrame()
{
    if (feed_ == nullptr || ! feed_->take (image_, imageSeconds_))
        return false;
    repaint();
    return true;
}

void VideoView::handleAsyncUpdate()
{
    pollFrame();
}

void VideoView::resized()
{
    if (feed_ != nullptr)
        feed_->setSize (getWidth() * 2, getHeight() * 2);
}

void VideoView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    const auto area = getLocalBounds().toFloat();
    if (image_.isValid())
    {
        const auto placed = juce::RectanglePlacement (juce::RectanglePlacement::centred)
                                .appliedTo (image_.getBounds().toFloat(), area.reduced (0.0f, 0.0f));
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        g.drawImage (image_, placed);
    }
    else
    {
        g.setColour (colours::textDim);
        g.setFont (uiFont (14.0f));
        const juce::String text = file_ == juce::File() ? u8 ("곡에 영상이 없습니다. 파일 메뉴의 영상 붙이기로 영상을 고릅니다.")
                                                        : (time_ < 0.0 || time_ >= duration_ ? u8 ("영상 밖") : juce::String());
        g.drawFittedText (text, getLocalBounds().reduced (16), juce::Justification::centred, 3);
    }
    if (file_ != juce::File() && rate_.valid())
    {
        // 재생 위치의 타임코드 (왼쪽 아래)
        const auto text = juce::String (mr::media::timecodeAt (time_, rate_));
        g.setFont (monoFont (13.0f));
        const auto box = juce::Rectangle<float> (8.0f, area.getBottom() - 26.0f, 112.0f, 20.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawText (text, box, juce::Justification::centred, false);
    }
}

// ---------------------------------------------------------------------------
// VideoWindow
// ---------------------------------------------------------------------------

VideoWindow::VideoWindow (juce::Component* centreAround)
    : juce::DocumentWindow (u8 ("영상"), juce::Colours::black, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
{
    auto view = std::make_unique<VideoView>();
    view_ = view.get();
    setContentOwned (view.release(), true);
    setUsingNativeTitleBar (true);
    setResizable (true, false);
    setResizeLimits (240, 135, 3840, 2160);
    if (centreAround != nullptr)
        centreAroundComponent (centreAround, getWidth(), getHeight());
    setAlwaysOnTop (false);
}

void VideoWindow::closeButtonPressed()
{
    if (onClose)
        onClose();
}

} // namespace daw
