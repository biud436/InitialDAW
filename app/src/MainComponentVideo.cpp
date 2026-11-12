// MainComponent 의 영상: 붙이기, 자리 옮기기, 떼기, 장면 바뀜 찾기와 마커, 영상의 소리 가져오기, 영상 창.
#include "MainComponent.h"

#include "Theme.h"
#include "composition/Arrange.h"
#include "composition/Document.h"
#include "instruments/AudioClips.h"
#include "io/AudioFile.h"
#include "media/Timecode.h"
#include "media/Video.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace daw
{

namespace
{

juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }

using ScenesDone = std::function<void (std::optional<std::vector<mr::media::SceneChange>>, juce::String)>;

class SceneJob final : public juce::Thread
{
public:
    SceneJob (juce::File file, ScenesDone done) : juce::Thread ("InitialDAW scenes"), file_ (std::move (file)), done_ (std::move (done)) {}
    ~SceneJob() override
    {
        signalThreadShouldExit();
        stopThread (-1);
    }

    float progress() const noexcept { return progress_.load(); }

private:
    void run() override
    {
        std::string error;
        auto scenes = mr::media::detectSceneChanges (file_.getFullPathName().toStdString(), {}, error, [this] (double v)
        {
            progress_.store ((float) v);
            return ! threadShouldExit();
        });
        if (threadShouldExit())
            return;
        juce::MessageManager::callAsync ([done = done_, scenes = std::move (scenes), error]
        {
            done (scenes, juce::String::fromUTF8 (error.c_str()));
        });
    }

    const juce::File file_;
    ScenesDone done_;
    std::atomic<float> progress_ { 0.0f };
};

juce::String secondsText (double seconds)
{
    return juce::String (seconds, 3) + u8 ("초");
}

} // namespace

void MainComponent::refreshVideo()
{
    std::optional<ArrangementPanel::VideoLane> lane;
    juce::File file;
    if (project_.loaded() && project_.composition().video)
    {
        file = mr::videoFileOf (project_.composition());
        const auto modified = file.getLastModificationTime();
        if (file != videoInfoFile_ || modified != videoInfoTime_)
        {
            videoInfoFile_ = file;
            videoInfoTime_ = modified;
            std::string error;
            videoInfo_ = mr::media::probeVideo (file.getFullPathName().toStdString(), error);
            if (! videoInfo_)
                setStatus (u8 ("영상을 열 수 없습니다: ") + juce::String::fromUTF8 (error.c_str()), true);
        }
        if (videoInfo_)
            lane = ArrangementPanel::VideoLane { file, project_.composition().video->offsetSeconds, videoInfo_->durationSeconds, videoInfo_->rate,
                                                 videoInfo_->width, videoInfo_->height };
    }
    else
    {
        videoInfo_.reset();
        videoInfoFile_ = juce::File();
        if (sceneJob_ != nullptr)
            sceneJob_.reset();
    }
    arrangement_.setVideo (lane, ! withAudio_);
    if (videoWindow_ != nullptr)
    {
        if (lane)
            videoWindow_->view().setVideo (lane->file, lane->rate, lane->durationSeconds);
        else
            videoWindow_->view().setVideo (juce::File(), {}, 0.0);
        updateVideoWindow();
    }
    commands_.commandStatusChanged();
}

void MainComponent::updateVideoWindow()
{
    if (videoWindow_ == nullptr || ! videoWindow_->isVisible() || ! project_.loaded() || ! project_.composition().video)
        return;
    double seconds = project_.secondsAtBeat (timeline_.playheadBeat());
    bool playing = false;
    if (auto p = player())
    {
        playing = p->isPlaying();
        // 들리는 소리의 곡 위치: 재생기의 출력 지연과 장치의 출력 지연을 뺀다
        int64_t heard = p->position();
        if (playing)
            heard -= p->outputLatencySamples() + (audio_ != nullptr ? audio_->outputLatency() : 0);
        seconds = (double) heard / p->sampleRate();
    }
    videoWindow_->view().setTime (seconds - project_.composition().video->offsetSeconds, playing);
}

bool MainComponent::attachVideo (const juce::File& file, juce::String& error)
{
    if (! project_.loaded())
        return false;
    if (! mr::media::videoSupported())
    {
        error = u8 ("이 플랫폼에서는 영상을 열 수 없습니다.");
        return false;
    }
    std::string reason;
    const auto info = mr::media::probeVideo (file.getFullPathName().toStdString(), reason);
    if (! info)
    {
        error = u8 ("영상을 열 수 없습니다: ") + juce::String::fromUTF8 (reason.c_str());
        return false;
    }
    const auto folder = project_.file().getParentDirectory();
    const auto path = file.isAChildOf (folder) ? file.getRelativePathFrom (folder).replaceCharacter ('\\', '/') : file.getFullPathName();
    auto* o = new juce::DynamicObject();
    o->setProperty ("file", path);
    o->setProperty ("offsetSeconds", 0);
    juce::String editError;
    if (! project_.edit ({ Project::Change { -1, "video", juce::var (o) } }, u8 ("영상 붙이기: ") + file.getFileName(), editError))
    {
        error = editError;
        return false;
    }
    setStatus (u8 ("영상을 붙였습니다: ") + file.getFileName() + " (" + juce::String (mr::media::timecodeAt (info->durationSeconds, info->rate)) + ", "
               + juce::String (info->rate.fps(), info->rate.den == 1 ? 0 : 2) + " fps). "
               + u8 ("영상 레인을 끌어 자리를 옮기고, 오른쪽 클릭으로 영상 창과 장면 바뀜 찾기를 엽니다."));
    return true;
}

bool MainComponent::setVideoOffset (double offsetSeconds)
{
    if (! project_.loaded() || ! project_.composition().video)
        return false;
    auto video = project_.property ("video").clone();
    auto* o = video.getDynamicObject();
    if (o == nullptr)
        return false;
    const double rounded = std::round (offsetSeconds * 1.0e6) / 1.0e6;
    if (std::abs (rounded - project_.composition().video->offsetSeconds) < 1.0e-9)
        return false;
    o->setProperty ("offsetSeconds", rounded);
    juce::String error;
    if (! project_.edit ({ Project::Change { -1, "video", video } }, u8 ("영상 자리: 곡의 ") + secondsText (rounded), error))
    {
        if (error.isNotEmpty())
            setStatus (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    setStatus (u8 ("영상 처음을 곡의 ") + secondsText (rounded) + u8 ("에 두었습니다 (") + describeBeats (project_.beatAtSeconds (std::max (0.0, rounded)), project_.meters())
               + ").");
    return true;
}

bool MainComponent::detachVideo()
{
    if (! project_.loaded() || ! project_.composition().video)
        return false;
    juce::String error;
    if (! project_.edit ({ Project::Change { -1, "video", juce::var() } }, u8 ("영상 떼기"), error))
    {
        if (error.isNotEmpty())
            setStatus (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    setStatus (u8 ("영상을 뗐습니다 (곡의 소리는 그대로입니다)."));
    return true;
}

void MainComponent::showAttachVideoDialog()
{
    if (! project_.loaded())
        return;
    chooser_ = std::make_unique<juce::FileChooser> (u8 ("곡에 붙일 영상"), project_.file().getParentDirectory(), "*.mp4;*.mov;*.m4v");
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                           [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& fc)
                           {
                               if (safe == nullptr || fc.getResult() == juce::File())
                                   return;
                               juce::String error;
                               if (! safe->attachVideo (fc.getResult(), error))
                                   safe->setStatus (error.replaceCharacter ('\n', ' '), true);
                           });
}

void MainComponent::showSceneChanges (const std::vector<mr::media::SceneChange>& scenes)
{
    std::vector<double> seconds;
    for (const auto& s : scenes)
        seconds.push_back (s.seconds);
    arrangement_.setSceneChanges (seconds);
    if (seconds.empty())
        setStatus (u8 ("장면 바뀜을 찾지 못했습니다."));
    else
        setStatus (u8 ("장면 바뀜 후보 ") + juce::String ((int) seconds.size())
                   + u8 ("곳을 찾았습니다. 영상 레인의 표시를 두 번 누르면 그 자리에 마커를 두고, 오른쪽 클릭 메뉴로 모두 마커로 바꿉니다."));
}

bool MainComponent::findSceneChanges (bool wait)
{
    if (! project_.loaded() || ! videoInfo_)
        return false;
    const auto file = videoInfoFile_;
    if (wait)
    {
        std::string error;
        const auto scenes = mr::media::detectSceneChanges (file.getFullPathName().toStdString(), {}, error);
        if (! scenes)
        {
            setStatus (u8 ("장면 바뀜을 찾지 못했습니다: ") + juce::String::fromUTF8 (error.c_str()), true);
            return false;
        }
        showSceneChanges (*scenes);
        return true;
    }
    if (sceneJob_ != nullptr && sceneJob_->isThreadRunning())
        return false;
    sceneJob_ = std::make_unique<SceneJob> (file, [safe = juce::Component::SafePointer<MainComponent> (this), file]
                                                  (std::optional<std::vector<mr::media::SceneChange>> scenes, juce::String error)
    {
        if (safe == nullptr)
            return;
        safe->sceneJob_.reset();
        if (safe->videoInfoFile_ != file)
            return;   // 그 사이 영상이 바뀌었다
        if (scenes)
            safe->showSceneChanges (*scenes);
        else
            safe->setStatus (u8 ("장면 바뀜을 찾지 못했습니다: ") + error, true);
    });
    sceneJob_->startThread();
    setStatus (u8 ("장면 바뀜을 찾는 중입니다."));
    return true;
}

bool MainComponent::addSceneMarkers (const std::vector<double>& videoSeconds)
{
    if (! project_.loaded() || ! project_.composition().video || videoSeconds.empty())
        return false;
    const double offset = project_.composition().video->offsetSeconds;
    const auto& all = arrangement_.sceneChanges();
    auto root = project_.documentRoot().clone();
    int added = 0, skipped = 0;
    auto sorted = videoSeconds;
    std::sort (sorted.begin(), sorted.end());
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        const double song = sorted[i] + offset;
        if (song < 0.0)
        {
            ++skipped;
            continue;
        }
        const double beat = std::round (project_.beatAtSeconds (song) * 1.0e6) / 1.0e6;
        bool taken = false;
        if (const auto* markers = root.getProperty ("markers", {}).getArray())
            for (const auto& m : *markers)
                taken = taken || std::abs ((double) m.getProperty ("beat", -1.0) - beat) < 1.0e-6;
        if (taken)
        {
            ++skipped;
            continue;
        }
        // 이름의 번호는 찾은 후보 가운데의 차례 (하나씩 넣어도 모두 넣어도 같다)
        int number = (int) i + 1;
        for (size_t k = 0; k < all.size(); ++k)
            if (std::abs (all[k] - sorted[i]) < 1.0e-9)
                number = (int) k + 1;
        const auto name = u8 ("장면 ") + juce::String (number);
        root.getDynamicObject()->setProperty ("markers", mr::withMarkerAdded (root, beat, name.toStdString()));
        ++added;
    }
    if (added == 0)
    {
        setStatus (u8 ("넣을 장면 마커가 없습니다 (곡 앞이거나 같은 자리에 마커가 있습니다)."));
        return false;
    }
    if (! editMarkers (root.getProperty ("markers", {}), u8 ("장면 마커 넣기 (") + juce::String (added) + u8 ("개)")))
        return false;
    if (skipped > 0)
        setStatus (u8 ("장면 마커 ") + juce::String (added) + u8 ("개를 넣었습니다 (") + juce::String (skipped) + u8 ("곳은 곡 앞이거나 이미 마커가 있어 뺐습니다)."));
    return true;
}

bool MainComponent::importVideoSound (juce::String& error)
{
    if (! project_.loaded() || ! project_.composition().video || ! videoInfo_)
    {
        error = u8 ("곡에 영상이 없습니다.");
        return false;
    }
    if (! videoInfo_->hasAudio)
    {
        error = u8 ("영상에 소리가 없습니다.");
        return false;
    }
    if (recording_)
    {
        error = u8 ("녹음하는 동안에는 가져오지 않습니다.");
        return false;
    }
    std::string reason;
    const auto sound = mr::media::readVideoAudio (videoInfoFile_.getFullPathName().toStdString(), 0.0, reason);
    if (! sound || sound->frames <= 0)
    {
        error = u8 ("영상의 소리를 읽지 못했습니다: ") + juce::String::fromUTF8 (reason.c_str());
        return false;
    }
    const double offset = project_.composition().video->offsetSeconds;
    const double fileSeconds = (double) sound->frames / sound->sampleRate;
    if (offset + fileSeconds <= 0.0)
    {
        error = u8 ("영상의 소리가 모두 곡 앞에 있습니다.");
        return false;
    }
    const auto folder = project_.file().getParentDirectory();
    const auto dir = folder.getChildFile ("audio");
    dir.createDirectory();
    const auto stem = juce::File::createLegalFileName (videoInfoFile_.getFileNameWithoutExtension()) + u8 (" 소리");
    auto target = dir.getChildFile (stem + ".wav");
    for (int n = 2; target.exists(); ++n)
        target = dir.getChildFile (stem + " " + juce::String (n) + ".wav");
    juce::AudioBuffer<float> buffer ((int) sound->channels.size(), (int) sound->frames);
    for (size_t ch = 0; ch < sound->channels.size(); ++ch)
        buffer.copyFrom ((int) ch, 0, sound->channels[ch].data(), (int) sound->frames);
    try
    {
        mr::writeAudioFile (target, buffer, 0, buffer.getNumSamples(), sound->sampleRate, { mr::AudioFileType::wav, 24, -1 });
    }
    catch (const std::exception& e)
    {
        error = u8 ("소리 파일을 쓰지 못했습니다: ") + juce::String::fromUTF8 (e.what());
        return false;
    }
    mr::AudioClip clip;
    clip.file = target.getRelativePathFrom (folder).replaceCharacter ('\\', '/').toStdString();
    if (offset >= 0.0)
        clip.startBeat = std::round (project_.beatAtSeconds (offset) * 1.0e6) / 1.0e6;
    else
        clip.offsetSeconds = std::round (-offset * 1.0e6) / 1.0e6;
    clip.lengthSeconds = std::round ((fileSeconds - clip.offsetSeconds) * 1.0e6) / 1.0e6;
    auto track = mr::newTrackJson (mr::uniqueTrackName (project_.documentRoot(), u8 ("영상 소리").toStdString()), "audio");
    track.getDynamicObject()->removeProperty ("notes");
    track = mr::withClipAdded (track, clip);
    const size_t at = project_.trackCount();
    const auto root = mr::withTrackInserted (project_.documentRoot().clone(), at, track);
    if (! editTracks (root, u8 ("영상의 소리 가져오기"), (int) at))
    {
        target.deleteFile();
        error = u8 ("트랙을 더하지 못했습니다.");
        return false;
    }
    setStatus (u8 ("영상의 소리를 오디오 트랙으로 넣었습니다: ") + target.getFileName());
    return true;
}

void MainComponent::showVideoWindow (bool show)
{
    if (! show)
    {
        videoWindow_.reset();
        commands_.commandStatusChanged();
        return;
    }
    if (! project_.loaded() || ! videoInfo_)
        return;
    if (videoWindow_ == nullptr)
    {
        videoWindow_ = std::make_unique<VideoWindow> (this);
        videoWindow_->onClose = [this] { showVideoWindow (false); };
        const int width = 560;
        const int height = videoInfo_->width > 0 ? juce::jlimit (160, 900, width * videoInfo_->height / videoInfo_->width) : 315;
        videoWindow_->setSize (width, height);
        videoWindow_->centreAroundComponent (this, width, height);
    }
    refreshVideo();
    videoWindow_->setVisible (true);
    videoWindow_->toFront (true);
    updateVideoWindow();
    commands_.commandStatusChanged();
}

void MainComponent::showVideoMenu()
{
    if (! project_.loaded() || ! project_.composition().video)
        return;
    juce::PopupMenu m;
    const bool ready = videoInfo_.has_value();
    const bool searching = sceneJob_ != nullptr && sceneJob_->isThreadRunning();
    const auto scenes = arrangement_.sceneChanges();
    m.addItem (u8 ("영상 창"), ready, videoWindowShown(), [this] { showVideoWindow (! videoWindowShown()); });
    m.addSeparator();
    m.addItem (searching ? u8 ("장면 바뀜을 찾는 중") : u8 ("장면 바뀜 찾기"), ready && ! searching, false, [this] { findSceneChanges(); });
    m.addItem (u8 ("장면 바뀜을 모두 마커로 (") + juce::String ((int) scenes.size()) + u8 ("곳)"), ! scenes.empty(), false,
               [this, scenes] { addSceneMarkers (scenes); });
    m.addItem (u8 ("장면 후보 지우기"), ! scenes.empty(), false, [this] { arrangement_.setSceneChanges ({}); });
    m.addSeparator();
    m.addItem (u8 ("영상 처음을 곡의 처음에"), true, false, [this] { setVideoOffset (0.0); });
    m.addItem (u8 ("영상 처음을 재생 위치에"), true, false, [this] { setVideoOffset (project_.secondsAtBeat (timeline_.playheadBeat())); });
    m.addItem (u8 ("영상의 소리를 오디오 트랙으로"), ready && videoInfo_->hasAudio && ! recording_, false, [this]
    {
        juce::String error;
        if (! importVideoSound (error))
            setStatus (error, true);
    });
    m.addSeparator();
    m.addItem (u8 ("다른 영상으로 바꾸기..."), true, false, [this] { showAttachVideoDialog(); });
    m.addItem (u8 ("Finder 에서 보기"), mr::videoFileOf (project_.composition()).exists(), false,
               [file = mr::videoFileOf (project_.composition())] { file.revealToUser(); });
    m.addItem (u8 ("영상 떼기"), true, false, [this] { detachVideo(); });
    m.showMenuAsync (juce::PopupMenu::Options());
}

} // namespace daw
