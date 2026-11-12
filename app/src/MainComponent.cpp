#include "MainComponent.h"

#include "Grooves.h"

#include "Theme.h"
#include "composition/Arrange.h"
#include "composition/Document.h"
#include "instruments/AudioClips.h"
#include "instruments/QuickSampler.h"
#include "analysis/PitchTrack.h"
#include "analysis/Onsets.h"
#include "notation/Notation.h"
#include "instruments/Registry.h"
#include "midi/MidiImport.h"
#include "notation/MusicXml.h"
#include "plugins/PluginHost.h"
#include "rendering/EventBuilder.h"
#include "rendering/OfflineRenderer.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <set>

namespace daw
{

// 재생기를 뒤에서 만든다 (샘플 악기를 읽는 데 몇 초가 걸린다). 끝나면 메시지 스레드로 넘긴다.
class MainComponent::Loader final : public juce::Thread
{
public:
    Loader (MainComponent& owner, mr::Composition composition, mr::PlayerOptions options, int generation,
            double resumeSeconds, bool resumePlaying)
        : juce::Thread ("InitialDAW player loader"), owner_ (&owner), composition_ (std::move (composition)),
          options_ (std::move (options)), generation_ (generation), resumeSeconds_ (resumeSeconds), resumePlaying_ (resumePlaying)
    {
    }

    ~Loader() override { stopThread (-1); }

    void run() override
    {
        std::shared_ptr<mr::RealtimePlayer> player;
        juce::String error;
        try
        {
            player = std::make_shared<mr::RealtimePlayer> (composition_, options_, [this] (size_t done, size_t total, const std::string& name)
            {
                done_.store ((int) done);
                total_.store ((int) total);
                const juce::ScopedLock lock (nameLock_);
                current_ = juce::String::fromUTF8 (name.c_str());
            });
        }
        catch (const mr::Failure& f)
        {
            error = juce::String::fromUTF8 (f.what());
            for (const auto& d : f.details())
                error << "\n" << juce::String::fromUTF8 (d.path.c_str()) << ": " << juce::String::fromUTF8 (d.message.c_str());
        }
        catch (const std::exception& e)
        {
            error = juce::String::fromUTF8 (e.what());
        }
        auto safe = owner_;
        const int generation = generation_;
        const double resume = resumeSeconds_;
        const bool playing = resumePlaying_;
        juce::MessageManager::callAsync ([safe, player, error, generation, resume, playing]
        {
            if (auto* o = safe.getComponent())
                o->playerReady (player, error, generation, resume, playing);
        });
    }

    juce::String progressText() const
    {
        const juce::ScopedLock lock (nameLock_);
        return juce::String::fromUTF8 ("악기를 준비하고 있습니다 ") + juce::String (done_.load()) + "/" + juce::String (total_.load())
               + (current_.isNotEmpty() ? " (" + current_ + ")" : juce::String());
    }

private:
    juce::Component::SafePointer<MainComponent> owner_;
    mr::Composition composition_;
    mr::PlayerOptions options_;
    int generation_;
    double resumeSeconds_;
    bool resumePlaying_;
    std::atomic<int> done_ { 0 }, total_ { 0 };
    juce::CriticalSection nameLock_;
    juce::String current_;
};

// 곡 전체를 파일 없이 렌더링해 목표 음량까지의 게인을 잰다 (내보내기의 정규화와 같은 값). 낮은 우선순위로 뒤에서.
class MainComponent::LoudnessJob final : public juce::Thread
{
public:
    LoudnessJob (MainComponent& owner, mr::Composition composition, int generation)
        : juce::Thread ("InitialDAW loudness"), owner_ (&owner), composition_ (std::move (composition)), generation_ (generation)
    {
    }

    ~LoudnessJob() override
    {
        cancel_.store (true);
        stopThread (-1);
    }

    void run() override
    {
        double gain = 0.0;
        juce::String error;
        try
        {
            mr::RenderOptions o;
            o.cancel = &cancel_;
            const auto report = mr::renderComposition (composition_, o);
            gain = report.normalisation.applied ? report.normalisation.gainDb : 0.0;
        }
        catch (const std::exception& e)
        {
            error = juce::String::fromUTF8 (e.what());
        }
        if (cancel_.load())
            return;
        auto safe = owner_;
        const int generation = generation_;
        juce::MessageManager::callAsync ([safe, gain, error, generation]
        {
            if (auto* o = safe.getComponent())
                o->loudnessReady (gain, error, generation);
        });
    }

private:
    juce::Component::SafePointer<MainComponent> owner_;
    mr::Composition composition_;
    int generation_;
    std::atomic<bool> cancel_ { false };
};

MainComponent::MainComponent (bool withAudio, bool persist)
    : withAudio_ (withAudio), persist_ (persist && withAudio), arrangement_ (project_, timeline_), pianoRoll_ (project_, timeline_), mixer_ (project_, timeline_)
{
    juce::PropertiesFile::Options options;
    options.applicationName = "InitialDAW";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    properties_ = std::make_unique<juce::ApplicationProperties>();
    properties_->setStorageParameters (options);
    recent_.setMaxNumberOfItems (12);
    recent_.restoreFromString (settings().getValue ("recentFiles"));
    autoReload_ = settings().getBoolValue ("autoReload", true);
    typing_.setEnabled (persist_ ? settings().getBoolValue ("typingKeyboard", true) : true);
    typing_.setBase (persist_ ? settings().getIntValue ("typingBase", 60) : 60);
    if (persist_)
    {
        autosaveDir_ = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("InitialDAW").getChildFile ("Autosave");
        mixer_.presetFolder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("InitialDAW").getChildFile ("Presets");
        pianoRoll_.grooveFolder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("InitialDAW").getChildFile ("Grooves");
        trackTemplateFolder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("InitialDAW").getChildFile ("TrackTemplates");
    }
    if (persist_)
    {
        countInBars_ = juce::jlimit (0, 2, settings().getIntValue ("countInBars", 1));
        metronome_ = settings().getBoolValue ("metronome", false);
        metronomeGainDb_ = juce::jlimit (-24.0, 6.0, settings().getDoubleValue ("metronomeGainDb", -6.0));
        punch_ = settings().getBoolValue ("punch", false);
        recordQuantize_ = settings().getBoolValue ("recordQuantize", false);
        arrangement_.zeroCrossingCuts = settings().getBoolValue ("clipZeroCrossings", false);
    }

    addAndMakeVisible (transport_);
    addAndMakeVisible (arrangement_);
    addAndMakeVisible (resizer_);
    addAndMakeVisible (pianoRoll_);
    addChildComponent (mixer_);
    addChildComponent (score_);
    score_.percussion = [this] (const mr::TrackSpec& t)
    {
        const auto* def = registry_ ? registry_->find (t.instrument) : nullptr;
        return def != nullptr && ! def->keyNames.empty();
    };
    score_.onSeek = [this] (double beat) { seekToBeat (beat); };
    mixer_.analyzer().onReset = [this]
    {
        if (auto p = player())
            p->loudness().requestReset();
        setStatus (juce::String::fromUTF8 ("라우드니스를 처음부터 다시 잽니다."));
    };
    layout_.setItemLayout (0, 120, -1.0, -0.58);
    layout_.setItemLayout (1, 5, 5, 5);
    layout_.setItemLayout (2, 160, -1.0, -0.42);

    transport_.onPlayStop = [this] { togglePlay(); };
    transport_.onToStart = [this] { goToStart(); };
    transport_.onRecord = [this] { if (recording_) stopRecording(); else startRecording(); };
    transport_.onLoop = [this] { setLooping (! looping_); };
    transport_.onMetronome = [this] { setMetronome (! metronome_); };
    transport_.setMetronome (metronome_);
    arrangement_.onLoopEdited = [this] (std::optional<std::pair<double, double>> range) { editLoop (range); };
    pianoRoll_.onLoopEdited = [this] (std::optional<std::pair<double, double>> range) { editLoop (range); };
    transport_.onMonitorGainChanged = [this] (double db)
    {
        if (! settingMonitor_)
            monitorTouched_ = true;
        monitorGainDb_ = db;
        if (audio_ != nullptr && audio_->player() != nullptr)
            audio_->player()->setMonitorGainDb (db);
    };
    arrangement_.onSeek = [this] (double beat) { seekToBeat (beat); };
    pianoRoll_.onSeek = [this] (double beat) { seekToBeat (beat); };
    pianoRoll_.onStatus = [this] (const juce::String& text, bool error) { setStatus (text, error); };
    mixer_.onStatus = [this] (const juce::String& text, bool error) { setStatus (text, error); };
    mixer_.onPreview = [this] (const mr::Composition& c) { previewOnPlayer (c); };
    mixer_.onOpenInstrument = [this] (size_t t) { openInstrumentWindow (t); };
    mixer_.onInstrumentMenu = [this] (size_t t) { showInstrumentMenu (t); };
    mixer_.onOpenPluginEffect = [this] (ChannelRef channel, size_t effect) { openEffectWindow (channel, effect); };
    arrangement_.onOpenInstrument = [this] (size_t t) { openInstrumentWindow (t); };
    arrangement_.onTrackMenu = [this] (size_t t) { showTrackMenu (t); };
    arrangement_.onFolderMenu = [this] (const std::string& folder) { showFolderMenu (folder); };
    learn_ = std::make_unique<MidiLearn> (mixer_, project_);
    mixer_.learn = learn_.get();
    learn_->onStatus = [this] (const juce::String& text, bool error) { setStatus (text, error); };
    learn_->onChanged = [this]
    {
        mixer_.learnChanged();
        if (persist_)
            settings().setValue ("midiLearn", learn_->toSettings());
    };
    if (persist_)
        learn_->fromSettings (settings().getValue ("midiLearn"));
    mixer_.writeBeat = [this]() -> std::optional<double>
    {
        // 곡선 쓰기를 켜고 재생 중일 때만, 재생기의 지금 위치
        if (! automationWrite_ || ! project_.loaded())
            return std::nullopt;
        if (auto p = player(); p != nullptr && p->isPlaying())
            return project_.beatAtSeconds (p->positionSeconds());
        return std::nullopt;
    };
    arrangement_.onRenameTrack = [this] (size_t t, const juce::String& name) { renameTrack (t, name); };
    arrangement_.onMoveTrack = [this] (size_t from, size_t to) { moveTrack (from, to); };
    arrangement_.onAddTrack = [this] { showNewTrackMenu(); };
    arrangement_.onAddMarker = [this] (double beat) { addMarker (beat, {}, true); };
    arrangement_.onMoveMarker = [this] (size_t m, double beat) { moveMarker (m, beat); };
    arrangement_.onRenameMarker = [this] (size_t m, const juce::String& name) { renameMarker (m, name); };
    arrangement_.onMarkerMenu = [this] (int m, double beat) { showMarkerMenu (m, beat); };
    arrangement_.onSectionMoved = [this] (size_t section, size_t before) { moveSection (section, before); };
    arrangement_.onSectionMenu = [this] (size_t section) { showSectionMenu (section); };
    arrangement_.onVideoMoved = [this] (double offset) { setVideoOffset (offset); };
    arrangement_.onVideoMenu = [this] { showVideoMenu(); };
    arrangement_.onSceneMarker = [this] (double seconds) { addSceneMarkers ({ seconds }); };
    arrangement_.onRangeChanged = [this]
    {
        if (const auto r = arrangement_.range())
        {
            pianoRoll_.deselect();   // 고른 것은 하나만: 구간이나 음표
            const auto& bar = project_.meters();
            setStatus (juce::String::fromUTF8 ("구간: ") + describeBeats (r->start, bar) + " ~ " + describeBeats (r->end, bar) + ", "
                       + juce::String::fromUTF8 ("트랙 ") + juce::String ((int) (r->lastTrack - r->firstTrack + 1)) + juce::String::fromUTF8 ("개"));
        }
        commands_.commandStatusChanged();
    };
    pianoRoll_.onPressed = [this] { arrangement_.setRange (std::nullopt); arrangement_.setSelectedClip (std::nullopt); };
    arrangement_.onClipEdited = [this] (ArrangementPanel::ClipRef ref, const mr::AudioClip& clip, bool copy) { editClip (ref, clip, copy); };
    arrangement_.onClipMenu = [this] (ArrangementPanel::ClipRef ref) { showClipMenu (ref); };
    arrangement_.onClipSelected = [this] { pianoRoll_.deselect(); commands_.commandStatusChanged(); };
    arrangement_.onDropAudio = [this] (const juce::StringArray& files, std::optional<size_t> track, double beat)
    {
        juce::String error;
        if (! importAudio (files, track, beat, error))
            setStatus (error.replaceCharacter ('\n', ' '), true);
    };
    pianoRoll_.articulationsFor = [this] (size_t t) { return articulationsOf (t); };
    pianoRoll_.keyNamesFor = [this] (size_t t) { return keyNamesOf (t); };
    pianoRoll_.onAudition = [this] (size_t t, int pitch, int velocity, const std::string& articulation, int channel)
    {
        const auto p = player();
        if (p == nullptr || t >= p->trackCount())
            return;
        if (velocity > 0)
            p->auditionNoteOn (t, pitch, velocity, articulation, channel);
        else
            p->auditionNoteOff (t, pitch);
    };
    transport_.onTempoClicked = [this] { showTempoDialog(); };
    project_.addListener (this);
    project_.checkTrack = [this] (size_t index, const mr::TrackSpec& spec) { return checkTrack (index, spec); };
    arrangement_.onMute = [this] (size_t i, bool) { toggleTrackFlag (i, false); };
    arrangement_.onSolo = [this] (size_t i, bool) { toggleTrackFlag (i, true); };
    transport_.setTitle ("InitialDAW");
    transport_.setTransportEnabled (false);
    setStatus (juce::String::fromUTF8 ("프로젝트를 여십시오."));

    commands_.registerAllCommandsForTarget (this);
    // 키보드 포커스가 비거나 다른 창에 있어도 메뉴와 단축키의 명령은 이 화면이 받는다 (메뉴가 비활성화되지 않게)
    commands_.setFirstCommandTarget (this);
    setApplicationCommandManagerToWatch (&commands_);   // 명령 상태가 바뀌면 메뉴를 다시 만든다
    addKeyListener (commands_.getKeyMappings());
    addKeyListener (&typing_);   // 나중에 더한 것을 먼저 묻는다: 연주에 쓰는 자판은 단축키보다 먼저
    setWantsKeyboardFocus (true);
    typing_.onNote = [this] (int pitch, bool on)
    {
        const auto p = player();
        if (on)
        {
            const int t = timeline_.selectedTrack();
            if (t < 0)
                setStatus (juce::String::fromUTF8 ("트랙을 고르면 자판으로 그 트랙을 연주합니다."));
            else if (p != nullptr && (size_t) t < p->trackCount())
            {
                typedTracks_[pitch] = (size_t) t;
                p->auditionNoteOn ((size_t) t, pitch, 100);
            }
        }
        else if (const auto it = typedTracks_.find (pitch); it != typedTracks_.end())
        {
            if (p != nullptr && it->second < p->trackCount())
                p->auditionNoteOff (it->second, pitch);
            typedTracks_.erase (it);
        }
        if (recording_ && ! audioRecording_ && p != nullptr && audio_ != nullptr)
        {
            const double beat = project_.beatAtSeconds ((double) audio_->estimatedPosition() / audio_->sampleRate());
            if (on && p->isPlaying())
                recorder_.noteOn (beat, pitch, 100);
            else if (! on)
                recorder_.noteOff (beat, pitch);
        }
        stepNote (pitch, 100, on);
        showHeldNotes();
    };
    transport_.onTypingClicked = [this] { setTyping (! typing_.isEnabled()); };
#if JUCE_MAC
    if (withAudio_)   // 창이 있는 앱만 (스냅숏과 시험은 자판을 받지 않는다)
    {
        typing_.setNativeKeys (true);
        nativeKeys_ = std::make_unique<NativeKeys> (*this, [this] (int code, bool down, bool repeat) { return typing_.nativeKey (code, down, repeat); });
    }
#endif
    showTypingState();

    if (withAudio_)
    {
        audio_ = std::make_unique<AudioEngine>();
        const auto saved = settings().getXmlValue ("audioDevice");
        const auto error = audio_->open (saved.get());
        if (error.isNotEmpty())
            setStatus (juce::String::fromUTF8 ("오디오 장치를 열 수 없습니다: ") + error, true);
        audio_->onDeviceChanged = [this] { rebuildPlayer(); };
        midi_ = std::make_unique<MidiInputs>();
        midi_->positionSource = [this] { return audio_->estimatedPosition(); };
        midi_->onEvent = [this] (const MidiInputs::Event& e) { liveMidi (e.message, e.position); };
        if (persist_)
            midi_->setDisabledIdentifiers (juce::StringArray::fromLines (settings().getValue ("midiInputsDisabled")));
        else
            midi_->refreshDevices();
        startTimerHz (30);
        showMixer (settings().getBoolValue ("mixerShown", false));
    }
    setSize (1500, 940);
}

MainComponent::~MainComponent()
{
    stopTimer();
    sceneJob_.reset();
    videoWindow_.reset();
    setApplicationCommandManagerToWatch (nullptr);   // commands_ 가 먼저 사라진다
    commands_.setFirstCommandTarget (nullptr);
#if JUCE_MAC
    nativeKeys_.reset();
#endif
    removeKeyListener (&typing_);
    typing_.onNote = nullptr;
    tempoDialog_.reset();
    project_.removeListener (this);
    loudness_.reset();
    loader_.reset();
    retired_.clear();
    saveSettings();
    closeInstrumentWindows (false);   // 편집기는 플러그인(재생기)보다 먼저 사라진다
    midi_.reset();
    if (audio_ != nullptr)
        audio_->setPlayer (nullptr);
    audio_.reset();
}

juce::PropertiesFile& MainComponent::settings() { return *properties_->getUserSettings(); }

void MainComponent::saveSettings()
{
    if (! persist_)
        return;   // 스냅숏과 시험은 창의 설정(최근 파일, 장치, 믹서 보기)을 바꾸지 않는다
    settings().setValue ("recentFiles", recent_.toString());
    settings().setValue ("autoReload", autoReload_);
    settings().setValue ("mixerShown", mixerShown_);
    settings().setValue ("typingKeyboard", typing_.isEnabled());
    settings().setValue ("typingBase", typing_.base());
    settings().setValue ("countInBars", countInBars_);
    settings().setValue ("metronome", metronome_);
    settings().setValue ("metronomeGainDb", metronomeGainDb_);
    settings().setValue ("clipZeroCrossings", arrangement_.zeroCrossingCuts);
    settings().setValue ("punch", punch_);
    settings().setValue ("recordQuantize", recordQuantize_);
    if (midi_ != nullptr)
        settings().setValue ("midiInputsDisabled", midi_->disabledIdentifiers().joinIntoString ("\n"));
    if (project_.loaded())
        settings().setValue ("lastProject", project_.file().getFullPathName());
    if (audio_ != nullptr)
        if (auto state = audio_->devices().createStateXml())
            settings().setValue ("audioDevice", state.get());
    settings().saveIfNeeded();
}

bool MainComponent::openProject (const juce::File& file)
{
    juce::String error;
    const bool sameFile = project_.loaded() && project_.file() == file;
    closeInstrumentWindows (false);   // 트랙 번호가 바뀔 수 있다 (바뀐 소리는 묻기 전에 이미 곡에 썼다)
    if (recording_)
        stopRecording (false);   // 다시 읽는 곡에는 넣지 않는다
    if (! project_.load (file, error))
    {
        setStatus (error.upToFirstOccurrenceOf ("\n", false, false), true);
        if (withAudio_)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    juce::String::fromUTF8 ("프로젝트를 열 수 없습니다"), error);
        return false;
    }
    {
        mr::RenderOptions ro;
        mr::Diagnostics d;
        registry_.emplace (mr::buildRegistry (project_.composition(), ro, d));
    }
    recent_.addFile (file);
    saveSettings();

    applyInputMonitor();   // 트랙을 지우거나 오디오 트랙이 아니게 되면 끈다
    arrangement_.refreshLayout();   // 폴더
    timeline_.setLengthBeats (project_.lengthBeats());
    timeline_.setLoopRange (project_.loopRange());
    if (! project_.loopRange())
        setLooping (false);
    if (! sameFile)
    {
        timeline_.setSelectedTrack (0);
        localSeconds_ = 0.0;
        timeline_.setPlayheadBeat (0.0);
        // 날 믹스의 음량은 곡마다 크게 다르다 (현악 -30 LUFS, 신스 -15 LUFS). 재기 전에는 0 dB 로 둔다.
        monitorTouched_ = false;
        setMonitorGainQuietly (0.0);
    }
    else if ((size_t) timeline_.selectedTrack() >= project_.trackCount())
        timeline_.setSelectedTrack (0);

    trackNames_.clear();
    for (const auto& t : project_.composition().tracks)
        trackNames_.push_back (t.name);
    arrangement_.projectChanged();
    if (! sameFile)
        arrangement_.setVideo (std::nullopt);   // 다른 곡의 장면 후보를 지운다
    refreshVideo();
    pianoRoll_.projectChanged();
    mixer_.projectChanged();
    updateTitle();
    if (! sameFile)
        zoomToFit();
    updateTransport();
    const int seconds = (int) std::round (project_.lengthSeconds());
    setStatus (juce::String::fromUTF8 ("트랙 ") + juce::String ((int) project_.trackCount()) + juce::String::fromUTF8 ("개, 길이 ")
               + juce::String (seconds / 60) + ":" + juce::String (seconds % 60).paddedLeft ('0', 2));
    rebuildPlayer();
    startLoudnessAnalysis();
    lastAutosaved_.clear();
    lastAutosaveMs_ = juce::Time::getMillisecondCounterHiRes();
    if (! sameFile)
        offerAutosave();
    return true;
}

juce::File MainComponent::autosaveFile() const
{
    if (autosaveDir_ == juce::File() || ! project_.loaded())
        return {};
    const auto path = project_.file().getFullPathName();
    return autosaveDir_.getChildFile (project_.file().getFileNameWithoutExtension() + "-"
                                      + juce::String::toHexString (path.hashCode64()).paddedLeft ('0', 16) + ".json");
}

bool MainComponent::autosaveNow()
{
    const auto target = autosaveFile();
    if (target == juce::File() || ! project_.hasUnsavedEdits())
        return false;
    lastAutosaveMs_ = juce::Time::getMillisecondCounterHiRes();
    const auto text = project_.toJson();
    if (text == lastAutosaved_)
        return false;
    // 곡을 감싼 객체: 원래 파일과 시각 (곡은 앱의 저장 형식 그대로)
    std::string wrapped = "{\n \"source\": ";
    wrapped += mr::writeJson (juce::var (project_.file().getFullPathName()));
    wrapped += ",\n \"savedAt\": " + mr::writeJson (juce::var (juce::Time::getCurrentTime().toISO8601 (true)));
    wrapped += ",\n \"song\": " + text + "}\n";
    if (! target.getParentDirectory().createDirectory() || ! target.replaceWithText (juce::String::fromUTF8 (wrapped.c_str())))
        return false;
    lastAutosaved_ = text;
    return true;
}

bool MainComponent::hasNewerAutosave() const
{
    const auto file = autosaveFile();
    if (! file.existsAsFile())
        return false;
    const auto wrapper = juce::JSON::parse (file);
    if (wrapper.getProperty ("source", "").toString() != project_.file().getFullPathName())
        return false;
    const auto savedAt = juce::Time::fromISO8601 (wrapper.getProperty ("savedAt", "").toString());
    if (savedAt <= project_.file().getLastModificationTime())
        return false;
    return mr::writeJson (wrapper.getProperty ("song", juce::var())) + "\n" != project_.toJson();
}

bool MainComponent::restoreAutosave()
{
    if (! hasNewerAutosave())
        return false;
    const auto wrapper = juce::JSON::parse (autosaveFile());
    juce::String error;
    if (! project_.editRoot (wrapper.getProperty ("song", juce::var()), juce::String::fromUTF8 ("자동 저장 되살리기"), error))
    {
        setStatus (juce::String::fromUTF8 ("자동 저장을 되살리지 못했습니다: ") + error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    setStatus (juce::String::fromUTF8 ("자동 저장한 편집을 되살렸습니다. 저장(⌘S)하면 파일에 씁니다. 실행 취소로 파일의 내용으로 돌아갑니다."));
    return true;
}

void MainComponent::discardAutosave()
{
    lastAutosaved_.clear();
    if (const auto file = autosaveFile(); file.existsAsFile())
        file.deleteFile();
}

void MainComponent::offerAutosave()
{
    if (! hasNewerAutosave() || ! withAudio_)
        return;
    const auto wrapper = juce::JSON::parse (autosaveFile());
    const auto savedAt = juce::Time::fromISO8601 (wrapper.getProperty ("savedAt", "").toString());
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, juce::String::fromUTF8 ("자동 저장한 편집이 있습니다"),
                                        project_.title() + juce::String::fromUTF8 (" 의 저장하지 않은 편집을 ")
                                            + savedAt.formatted ("%Y-%m-%d %H:%M") + juce::String::fromUTF8 ("에 자동 저장했습니다. 되살리겠습니까?"),
                                        juce::String::fromUTF8 ("되살리기"), juce::String::fromUTF8 ("버리기"), this,
                                        juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                                        {
                                            if (safe == nullptr)
                                                return;
                                            if (result == 1)
                                                safe->restoreAutosave();
                                            else
                                                safe->discardAutosave();
                                        }));
}

void MainComponent::setMonitorGainQuietly (double db)
{
    const juce::ScopedValueSetter<bool> quiet (settingMonitor_, true);
    transport_.setMonitorGainDb (db);
}

void MainComponent::startLoudnessAnalysis()
{
    loudness_.reset();   // 앞의 측정은 멈춘다
    if (! withAudio_ || ! project_.loaded() || ! project_.composition().master.targetLufs)
        return;
    // 플러그인 악기는 메시지 스레드에서만 만들 수 있어 재지 않는다
    mr::RenderOptions ro;
    mr::Diagnostics d;
    const auto registry = mr::buildRegistry (project_.composition(), ro, d);
    for (const auto& t : project_.composition().tracks)
        if (const auto* def = registry.find (t.instrument); def != nullptr && (def->type == "vst3" || def->type == "au"))
            return;
    loudnessMeasured_ = false;
    loudness_ = std::make_unique<LoudnessJob> (*this, project_.composition(), ++loudnessGeneration_);
    loudness_->startThread (juce::Thread::Priority::low);
}

void MainComponent::loudnessReady (double gainDb, const juce::String& error, int generation)
{
    if (generation != loudnessGeneration_ || error.isNotEmpty())
        return;
    loudnessMeasured_ = true;
    if (! monitorTouched_)
    {
        setMonitorGainQuietly (juce::jlimit (-24.0, 24.0, std::round (gainDb * 2.0) / 2.0));
        setStatus (juce::String::fromUTF8 ("곡의 목표 음량에 맞춰 모니터 게인을 정했습니다."));
    }
}

void MainComponent::reloadProject()
{
    if (project_.loaded())
        openProject (project_.file());
}

void MainComponent::requestOpen (const juce::File& file)
{
    askToSave ([safe = juce::Component::SafePointer<MainComponent> (this), file]
    {
        if (safe != nullptr)
            safe->openProject (file);
    });
}

void MainComponent::askToSave (std::function<void()> proceed)
{
    flushInstrumentWindows();
    if (! project_.hasUnsavedEdits())
    {
        proceed();
        return;
    }
    juce::AlertWindow::showYesNoCancelBox (juce::MessageBoxIconType::QuestionIcon, juce::String::fromUTF8 ("저장하지 않은 편집이 있습니다"),
                                           project_.title() + juce::String::fromUTF8 (" 의 편집을 저장하시겠습니까?"),
                                           juce::String::fromUTF8 ("저장"), juce::String::fromUTF8 ("저장하지 않음"),
                                           juce::String::fromUTF8 ("취소"), this,
                                           juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MainComponent> (this),
                                                                                 proceed] (int result)
                                           {
                                               if (safe == nullptr || result == 0)
                                                   return;
                                               if (result == 2)
                                                   safe->discardAutosave();   // 버리기로 했다
                                               if (result == 2 || safe->saveProject())
                                                   proceed();
                                           }));
}

bool MainComponent::saveProject()
{
    if (! project_.loaded())
        return false;
    flushInstrumentWindows();
    juce::String error;
    if (! project_.save (project_.file(), error))
    {
        setStatus (juce::String::fromUTF8 ("저장하지 못했습니다: ") + error, true);
        return false;
    }
    updateTitle();
    discardAutosave();
    setStatus (juce::String::fromUTF8 ("저장했습니다: ") + project_.file().getFileName());
    return true;
}

void MainComponent::saveProjectAs (std::function<void (bool)> done)
{
    flushInstrumentWindows();
    if (! project_.loaded())
    {
        if (done)
            done (false);
        return;
    }
    chooser_ = std::make_unique<juce::FileChooser> (juce::String::fromUTF8 ("다른 이름으로 저장"), project_.file(), "*.json");
    chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                               | juce::FileBrowserComponent::warnAboutOverwriting,
                           [safe = juce::Component::SafePointer<MainComponent> (this), done] (const juce::FileChooser& fc)
                           {
                               if (safe == nullptr)
                                   return;
                               auto file = fc.getResult();
                               bool saved = false;
                               if (file != juce::File())
                               {
                                   if (! file.hasFileExtension ("json"))
                                       file = file.withFileExtension ("json");
                                   juce::String error;
                                   const auto oldAutosave = safe->autosaveFile();
                                   saved = safe->project_.save (file, error);
                                   if (saved)
                                   {
                                       if (oldAutosave.existsAsFile())
                                           oldAutosave.deleteFile();
                                       safe->lastAutosaved_.clear();
                                       safe->recent_.addFile (file);
                                       safe->saveSettings();
                                       safe->updateTitle();
                                       safe->setStatus (juce::String::fromUTF8 ("저장했습니다: ") + file.getFileName());
                                   }
                                   else
                                       safe->setStatus (juce::String::fromUTF8 ("저장하지 못했습니다: ") + error, true);
                               }
                               if (done)
                                   done (saved);
                           });
}

void MainComponent::projectEdited (int track)
{
    // 트랙 목록이 바뀌었는가 (수, 또는 같은 이름이 다른 자리로). 이름만 바뀐 것은 아니다
    {
        std::vector<std::string> names;
        for (const auto& t : project_.composition().tracks)
            names.push_back (t.name);
        bool structural = names.size() != trackNames_.size();
        for (size_t i = 0; ! structural && i < names.size(); ++i)
            structural = names[i] != trackNames_[i] && std::find (names.begin(), names.end(), trackNames_[i]) != names.end();
        trackNames_ = names;
        if (structural)
        {
            // 번호로 묶인 창과 누른 음은 옛 목록의 것이다
            instrumentWindows_.clear();
            pendingInstrumentWindows_.erase (std::remove_if (pendingInstrumentWindows_.begin(), pendingInstrumentWindows_.end(),
                                                             [&names] (size_t t) { return t >= names.size(); }),
                                             pendingInstrumentWindows_.end());
            typedTracks_.clear();
            midiTracks_.clear();
            if (timeline_.selectedTrack() >= (int) names.size())
                timeline_.setSelectedTrack ((int) names.size() - 1);
            arrangement_.tracksChanged();
        }
    }
    applyInputMonitor();            // 트랙을 지우거나 오디오 트랙이 아니게 되면 끈다
    arrangement_.refreshLayout();   // 폴더
    if (track < 0)
        refreshVideo();
    timeline_.setLengthBeats (project_.lengthBeats());
    timeline_.setLoopRange (project_.loopRange());
    if (! project_.loopRange() && looping_)
        setLooping (false);
    if (track < 0)
    {
        // 곡 안의 악기 정의가 바뀌었을 수 있다 (편집 검사에 쓰는 레지스트리)
        mr::RenderOptions ro;
        mr::Diagnostics d;
        registry_.emplace (mr::buildRegistry (project_.composition(), ro, d));
    }
    checkSynthWindows();
    if (withAudio_)
    {
        auto current = audio_->player();
        const bool loading = loader_ != nullptr && loader_->isThreadRunning();
        if (current == nullptr || loading)
            rebuildPlayer();
        else
        {
            // 바뀐 부분만 바꿔 끼운다. 템포, 박자, 트랙과 버스의 목록, 악기, 트랙 효과의 지연이 바뀌었으면 다시 만든다.
            try
            {
                const auto pluginChanges = tracksWithChangedPluginSettings (current->composition(), project_.composition());
                if (! current->apply (project_.composition()))
                    rebuildPlayer();
                else
                {
                    applyTransportToPlayer (*current);   // 반복 구간이 바뀌었을 수 있다
                    for (const size_t t : pluginChanges)   // 실행 취소로 넣은 상태를 창이 새 기준으로 삼는다
                        if (auto it = instrumentWindows_.find (t); it != instrumentWindows_.end())
                            if (auto* plugin = it->second->pluginPanel())
                                plugin->resync();
                }
            }
            catch (const std::exception& e)
            {
                setStatus (juce::String::fromUTF8 ("편집을 재생에 반영하지 못해 다시 준비합니다: ") + juce::String::fromUTF8 (e.what()), true);
                rebuildPlayer();
            }
        }
    }
    arrangement_.refreshFlags();
    arrangement_.repaint();
    pianoRoll_.repaint();
    updateTitle();
    updateTransport();
}

juce::String MainComponent::checkTrack (size_t index, const mr::TrackSpec& spec) const
{
    if (! registry_)
        return {};
    std::string name = spec.instrument;
    if (audio_ != nullptr)   // 대체 악기로 연주하고 있으면 그 악기로 검사한다 (악기를 바꾼 편집은 새 이름으로)
        if (auto p = audio_->player(); p != nullptr && index < p->trackCount() && p->trackInfo (index).instrument == spec.instrument)
            name = p->trackInfo (index).resolved;
    const auto* def = registry_->find (name);
    if (def == nullptr)
        return {};
    mr::Diagnostics d;
    mr::checkTrackAgainstInstrument (spec, *def, d);
    if (d.ok())
        return {};
    return juce::String::fromUTF8 ("이 악기에서 쓸 수 없습니다: ") + juce::String::fromUTF8 (d.errors.front().message.c_str());
}

void MainComponent::previewOnPlayer (const mr::Composition& composition)
{
    if (audio_ == nullptr || (loader_ != nullptr && loader_->isThreadRunning()))
        return;
    if (auto player = audio_->player())
    {
        try
        {
            player->apply (composition);   // 다시 만들어야 하는 변경이면 손을 뗄 때 다시 만든다
        }
        catch (const std::exception&)
        {
        }
    }
}

void MainComponent::toggleTrackFlag (size_t index, bool solo)
{
    if (! project_.loaded() || index >= project_.trackCount())
        return;
    auto m = project_.mixerOf (ChannelRef::track (index));
    auto& flag = solo ? m.solo : m.mute;
    flag = ! flag;
    juce::String error;
    const auto name = juce::String::fromUTF8 (solo ? (flag ? "솔로" : "솔로 풀기") : (flag ? "뮤트" : "뮤트 풀기"))
                      + ": " + project_.channelName (ChannelRef::track (index));
    if (! project_.editChannel (ChannelRef::track (index), mr::withMixer (project_.channelJson (ChannelRef::track (index)), m), name, error))
        setStatus (error.replaceCharacter ('\n', ' '), true);
}

bool MainComponent::openInstrumentWindow (size_t track)
{
    if (! project_.loaded() || track >= project_.trackCount())
        return false;
    timeline_.setSelectedTrack ((int) track);
    if (auto it = instrumentWindows_.find (track); it != instrumentWindows_.end())
    {
        it->second->setVisible (true);
        it->second->toFront (true);
        return true;
    }
    auto player = audio_ != nullptr ? audio_->player() : nullptr;
    auto* plugin = player != nullptr && track < player->trackCount() ? player->pluginInstance (track) : nullptr;
    std::unique_ptr<InstrumentWindow> window;
    if (plugin != nullptr)
    {
        window = std::make_unique<InstrumentWindow> (project_, track, *plugin, this);
        window->pluginPanel()->onStatus = [this] (const juce::String& text, bool error) { setStatus (text, error); };
    }
    else if (auto synth = makeSynthPanel (track))
    {
        const auto title = project_.channelName (ChannelRef::track (track)) + ": " + juce::String::fromUTF8 (synth->preset().c_str())
                           + juce::String::fromUTF8 (" (내장 신스)");
        window = std::make_unique<InstrumentWindow> (std::move (synth), title, this);
    }
    else
    {
        const auto name = player != nullptr && track < player->trackCount() ? player->trackInfo (track).resolved
                                                                              : project_.composition().tracks[track].instrument;
        setStatus (project_.channelName (ChannelRef::track (track)) + juce::String::fromUTF8 (" 의 악기(") + juce::String::fromUTF8 (name.c_str())
                   + juce::String::fromUTF8 (")는 샘플 악기라서 패널이 없습니다. 오른쪽 클릭 메뉴로 악기를 바꿀 수 있습니다."));
        return false;
    }
    window->onClose = [safe = juce::Component::SafePointer<MainComponent> (this), track]
    {
        if (safe == nullptr)
            return;
        if (auto it = safe->instrumentWindows_.find (track); it != safe->instrumentWindows_.end())
        {
            if (auto* panel = it->second->pluginPanel())
                panel->captureNow();
            it->second->setVisible (false);
        }
        // 창 자신의 단추 처리 안이라 다음 메시지에서 지운다
        juce::MessageManager::callAsync ([safe, track]
        {
            if (safe != nullptr)
                safe->instrumentWindows_.erase (track);
        });
    };
    const bool isPlugin = window->pluginPanel() != nullptr;
    instrumentWindows_[track] = std::move (window);
    setStatus (juce::String::fromUTF8 (isPlugin ? "악기 창을 열었습니다. 바꾼 소리는 0.5초 뒤 곡에 들어가고 ⌘Z 로 되돌립니다."
                                                : "신스 패널을 열었습니다. 손잡이를 돌리는 동안 바로 들리고, 손을 떼면 곡에 들어갑니다."));
    return true;
}

std::unique_ptr<SynthPanel> MainComponent::makeSynthPanel (size_t track)
{
    if (! registry_ || ! project_.loaded() || track >= project_.trackCount())
        return nullptr;
    std::string name = project_.composition().tracks[track].instrument;
    if (audio_ != nullptr)   // 대체 악기로 연주하고 있으면 그 악기
        if (auto p = audio_->player(); p != nullptr && track < p->trackCount() && p->trackInfo (track).instrument == name)
            name = p->trackInfo (track).resolved;
    const auto* def = registry_->find (name);
    if (def == nullptr || def->type != "synth")
        return nullptr;
    auto panel = std::make_unique<SynthPanel> (project_, track, def->preset, def->synthParameters);
    panel->onPreview = [this] (const mr::Composition& c) { previewOnPlayer (c); };
    panel->onStatus = [this] (const juce::String& text, bool error) { setStatus (text, error); };
    return panel;
}

std::string MainComponent::effectOwner (ChannelRef channel)
{
    switch (channel.kind)
    {
        case ChannelRef::Kind::track: return "track:" + std::to_string (channel.index);
        case ChannelRef::Kind::bus: return "bus:" + std::to_string (channel.index);
        case ChannelRef::Kind::master: break;
    }
    return "master";
}

bool MainComponent::openEffectWindow (ChannelRef channel, size_t effect)
{
    auto p = player();
    const auto owner = effectOwner (channel);
    auto plugin = p != nullptr ? p->effectPlugin (owner, effect) : nullptr;
    if (plugin == nullptr)
    {
        setStatus (juce::String::fromUTF8 ("재생 준비가 끝난 뒤에 플러그인 효과의 창을 엽니다."), true);
        return false;
    }
    const auto key = std::make_pair (owner, effect);
    if (auto it = effectWindows_.find (key); it != effectWindows_.end() && it->second->pluginPanel() != nullptr
                                             && &it->second->pluginPanel()->plugin() == plugin.get())
    {
        it->second->toFront (true);
        return true;
    }
    effectWindows_.erase (key);
    auto window = std::make_unique<InstrumentWindow> (project_, channel, effect, plugin, this);
    window->pluginPanel()->onStatus = [this] (const juce::String& text, bool error) { setStatus (text, error); };
    window->onClose = [this, key]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), key]
        {
            if (safe == nullptr)
                return;
            if (auto it = safe->effectWindows_.find (key); it != safe->effectWindows_.end())
            {
                if (auto* panel = it->second->pluginPanel())
                    panel->captureNow();
                safe->effectWindows_.erase (it);
            }
        });
    };
    effectWindows_[key] = std::move (window);
    return true;
}

void MainComponent::checkEffectWindows()
{
    auto p = player();
    for (auto it = effectWindows_.begin(); it != effectWindows_.end();)
    {
        auto* panel = it->second->pluginPanel();
        const auto current = p != nullptr ? p->effectPlugin (it->first.first, it->first.second) : nullptr;
        if (panel == nullptr || current.get() != &panel->plugin())
            it = effectWindows_.erase (it);   // 사슬이 바뀌었다 (다른 플러그인이 소리를 낸다)
        else
            ++it;
    }
}

std::vector<size_t> MainComponent::closeInstrumentWindows (bool capture, bool pluginsOnly)
{
    for (auto& [key, window] : effectWindows_)
        if (auto* panel = window->pluginPanel(); capture && panel != nullptr)
            panel->captureNow();
    effectWindows_.clear();
    std::vector<size_t> tracks;
    for (auto it = instrumentWindows_.begin(); it != instrumentWindows_.end();)
    {
        auto* plugin = it->second->pluginPanel();
        if (pluginsOnly && plugin == nullptr)
        {
            ++it;   // 신스 패널은 재생기와 상관없다
            continue;
        }
        if (capture && plugin != nullptr && it->second->isVisible())
            plugin->captureNow();
        if (it->second->isVisible())
            tracks.push_back (it->first);
        it = instrumentWindows_.erase (it);
    }
    return tracks;
}

void MainComponent::flushInstrumentWindows()
{
    for (auto& [track, window] : instrumentWindows_)
        if (auto* plugin = window->pluginPanel())
            plugin->captureNow();
    for (auto& [key, window] : effectWindows_)
        if (auto* plugin = window->pluginPanel())
            plugin->captureNow();
}

void MainComponent::checkSynthWindows()
{
    // 실행 취소로 악기가 바뀌면 맞지 않는 신스 패널은 닫는다
    for (auto it = instrumentWindows_.begin(); it != instrumentWindows_.end();)
    {
        auto* synth = it->second->synthPanel();
        bool keep = synth == nullptr;
        if (synth != nullptr)
            if (auto fresh = makeSynthPanel (it->first))
                keep = fresh->preset() == synth->preset();
        if (keep)
            ++it;
        else
            it = instrumentWindows_.erase (it);
    }
}

void MainComponent::addInstrumentMenus (juce::PopupMenu& m, std::vector<InstrumentChoice>& choices, int base, const std::string& current) const
{
    // 레지스트리의 악기 (형식별), 그다음 찾은 플러그인 가운데 레지스트리에 없는 악기
    juce::PopupMenu synths, samples, plugins, found;
    std::set<juce::String> knownPlugins;
    for (const auto& [name, def] : registry_->all())
    {
        if (def.type == "audio")
            continue;   // 오디오 트랙은 따로 만든다
        const int id = base + (int) choices.size();
        choices.push_back ({ name, {} });
        auto text = juce::String::fromUTF8 (name.c_str());
        if (! def.description.empty())
            text << "  (" << juce::String::fromUTF8 (def.description.c_str()) << ")";
        const bool ticked = name == current;
        if (def.type == "synth")
            synths.addItem (id, text, true, ticked);
        else if (def.type == "sfz")
            samples.addItem (id, text, true, ticked);
        else
        {
            plugins.addItem (id, text, true, ticked);
            knownPlugins.insert (juce::String::fromUTF8 (def.plugin.c_str()));
        }
    }
    const auto cache = juce::JSON::parse (mr::defaultPluginCache());
    if (const auto* list = cache.getProperty ("plugins", juce::var()).getArray())
        for (const auto& e : *list)
        {
            if (! (bool) e.getProperty ("isInstrument", false) || knownPlugins.count (e.getProperty ("plugin", "").toString()) > 0)
                continue;
            const auto pluginName = e.getProperty ("name", "").toString();
            const auto format = e.getProperty ("format", "").toString();
            // 곡 안의 정의 이름: 플러그인 이름을 소문자와 붙임표로
            auto slug = pluginName.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789 -").trim().replaceCharacter (' ', '-');
            if (slug.isEmpty())
                slug = "plugin";
            if (format == "au")
                slug << "-au";
            auto* def = new juce::DynamicObject();
            def->setProperty ("type", format);
            def->setProperty ("plugin", e.getProperty ("plugin", ""));
            def->setProperty ("pluginName", pluginName);
            def->setProperty ("description", pluginName + " (" + e.getProperty ("manufacturer", "").toString() + ")");
            const int id = base + (int) choices.size();
            choices.push_back ({ slug.toStdString(), juce::var (def) });
            found.addItem (id, pluginName + " (" + format.toUpperCase() + ", " + e.getProperty ("manufacturer", "").toString() + ")");
        }
    m.addSubMenu (juce::String::fromUTF8 ("내장 신스"), synths);
    m.addSubMenu (juce::String::fromUTF8 ("샘플 악기"), samples);
    m.addSubMenu (juce::String::fromUTF8 ("플러그인"), plugins);
    m.addSubMenu (juce::String::fromUTF8 ("찾은 플러그인"), found, found.getNumItems() > 0);
}

void MainComponent::showInstrumentMenu (size_t track)
{
    if (! project_.loaded() || track >= project_.trackCount() || ! registry_)
        return;
    const auto& spec = project_.composition().tracks[track];
    auto player = audio_ != nullptr ? audio_->player() : nullptr;
    const bool isPlugin = player != nullptr && track < player->trackCount() && player->pluginInstance (track) != nullptr;

    auto choices = std::make_shared<std::vector<InstrumentChoice>>();
    juce::PopupMenu m;
    m.addSectionHeader (project_.channelName (ChannelRef::track (track)) + ": " + juce::String::fromUTF8 (spec.instrument.c_str()));
    m.addItem (1, juce::String::fromUTF8 ("악기 창 열기"), isPlugin);
    m.addSubMenu ("VCA", mixer_.vcaMenu (ChannelRef::track (track)));
    m.addSeparator();
    addInstrumentMenus (m, *choices, 1000, spec.instrument);
    m.addSeparator();
    m.addItem (2, juce::String::fromUTF8 ("플러그인 찾기 (VST3, AU)"), scanner_ == nullptr);
    m.showMenuAsync (juce::PopupMenu::Options(),
                     [safe = juce::Component::SafePointer<MainComponent> (this), track, choices] (int result)
                     {
                         if (safe == nullptr || result == 0)
                             return;
                         if (result == 1)
                             safe->openInstrumentWindow (track);
                         else if (result == 2)
                             safe->scanPlugins();
                         else if (result >= 1000 && (size_t) (result - 1000) < choices->size())
                         {
                             const auto& c = (*choices)[(size_t) (result - 1000)];
                             safe->changeInstrument (track, c.name, c.definition);
                         }
                     });
}

// ---- 트랙 목록 ----

const std::vector<std::pair<juce::String, juce::String>>& MainComponent::trackColourChoices()
{
    static const std::vector<std::pair<juce::String, juce::String>> list {
        { juce::String::fromUTF8 ("빨강"), "#e5534b" }, { juce::String::fromUTF8 ("주황"), "#e8913a" }, { juce::String::fromUTF8 ("노랑"), "#d9c34a" },
        { juce::String::fromUTF8 ("연두"), "#8fcf4f" }, { juce::String::fromUTF8 ("초록"), "#3fb27f" }, { juce::String::fromUTF8 ("청록"), "#3bb8c4" },
        { juce::String::fromUTF8 ("파랑"), "#4a90e2" }, { juce::String::fromUTF8 ("남색"), "#6a6ee8" }, { juce::String::fromUTF8 ("보라"), "#a368e0" },
        { juce::String::fromUTF8 ("분홍"), "#e06aa8" }, { juce::String::fromUTF8 ("갈색"), "#a87f5a" }, { juce::String::fromUTF8 ("회색"), "#8b949e" },
    };
    return list;
}

void MainComponent::showTrackMenu (size_t track)
{
    if (! project_.loaded() || track >= project_.trackCount() || ! registry_)
        return;
    const auto& spec = project_.composition().tracks[track];
    auto player = audio_ != nullptr ? audio_->player() : nullptr;
    const bool isPlugin = player != nullptr && track < player->trackCount() && player->pluginInstance (track) != nullptr;
    const bool canEdit = ! recording_;
    const size_t count = project_.trackCount();

    auto choices = std::make_shared<std::vector<InstrumentChoice>>();
    juce::PopupMenu m;
    m.addSectionHeader (project_.channelName (ChannelRef::track (track)) + ": " + juce::String::fromUTF8 (spec.instrument.c_str()));
    m.addItem (juce::String::fromUTF8 ("이름 바꾸기"), canEdit, false, [this, track] { arrangement_.beginRename (track); });
    m.addItem (juce::String::fromUTF8 ("복제"), canEdit, false, [this, track] { duplicateTrack (track); });
    m.addItem (juce::String::fromUTF8 ("지우기"), canEdit && count > 1, false, [this, track] { deleteTrack (track); });
    m.addItem (juce::String::fromUTF8 ("오디오로 굳히기"), canEdit && bounceJob_ == nullptr, false, [this, track] { bounceTrack (track); });
    m.addItem (juce::String::fromUTF8 ("트랙 템플릿으로 저장..."), trackTemplateFolder != juce::File(), false, [this, track]
    {
        templateDialog_ = std::make_unique<juce::AlertWindow> (juce::String::fromUTF8 ("트랙 템플릿 저장"),
                                                               juce::String::fromUTF8 ("이 트랙의 악기와 소리, 효과, 믹서, 센드를 템플릿으로 저장합니다 (음표와 클립, 곡선은 넣지 않습니다)."),
                                                               juce::MessageBoxIconType::NoIcon, this);
        templateDialog_->addTextEditor ("name", project_.channelName (ChannelRef::track (track)));
        templateDialog_->addButton (juce::String::fromUTF8 ("저장"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        templateDialog_->addButton (juce::String::fromUTF8 ("취소"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        templateDialog_->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MainComponent> (this), track] (int result)
        {
            if (safe == nullptr || safe->templateDialog_ == nullptr)
                return;
            const auto name = safe->templateDialog_->getTextEditorContents ("name");
            safe->templateDialog_.reset();
            juce::String error;
            if (result == 1 && ! safe->saveTrackTemplate (track, name, error) && error.isNotEmpty())
                safe->setStatus (error, true);
        }));
    });
    if (spec.instrument == "audio")
    {
        const bool monitoring = inputMonitorTrack() == (int) track;
        m.addItem (juce::String::fromUTF8 ("입력 모니터링"), true, monitoring, [this, track, monitoring] { setInputMonitoring (track, ! monitoring); });
    }
    m.addSeparator();
    if ((int) track != timeline_.selectedTrack() && spec.instrument != "audio")
    {
        // 고른 트랙의 피아노 롤에 이 트랙을 함께 (흐리게 보기, 함께 고치기)
        using Companion = PianoRollPanel::Companion;
        const auto mode = pianoRoll_.companion (track);
        juce::PopupMenu together;
        together.addItem (juce::String::fromUTF8 ("흐리게 함께 보기"), true, mode == Companion::shown,
                          [this, track, mode] { pianoRoll_.setCompanion (track, mode == Companion::shown ? Companion::none : Companion::shown); });
        together.addItem (juce::String::fromUTF8 ("함께 고치기"), true, mode == Companion::edited,
                          [this, track, mode] { pianoRoll_.setCompanion (track, mode == Companion::edited ? Companion::none : Companion::edited); });
        m.addSubMenu (juce::String::fromUTF8 ("피아노 롤에 함께"), together);
    }
    m.addItem (juce::String::fromUTF8 ("위로 옮기기"), canEdit && track > 0, false, [this, track] { moveTrack (track, track - 1); });
    m.addItem (juce::String::fromUTF8 ("아래로 옮기기"), canEdit && track + 1 < count, false, [this, track] { moveTrack (track, track + 1); });
    juce::PopupMenu colours;
    colours.addItem (juce::String::fromUTF8 ("기본 (순서에 따른 색)"), true, spec.colour.empty(), [this, track] { setTrackColour (track, {}); });
    colours.addSeparator();
    for (const auto& [label, hex] : trackColourChoices())
    {
        juce::PopupMenu::Item item (label);
        item.setColour (juce::Colour::fromString ("ff" + hex.substring (1)))
            .setTicked (juce::String (spec.colour) == hex)
            .setAction ([this, track, hex = hex] { setTrackColour (track, hex); });
        colours.addItem (std::move (item));
    }
    m.addSubMenu (juce::String::fromUTF8 ("색"), colours);
    // 출력: 마스터나 버스 (서브그룹). 고른 구간에 이 트랙이 들면 그 트랙들 모두
    std::vector<size_t> targets { track };
    if (const auto r = arrangement_.range(); r && r->firstTrack <= track && track <= r->lastTrack)
    {
        targets.clear();
        for (size_t t = r->firstTrack; t <= r->lastTrack; ++t)
            targets.push_back (t);
    }
    juce::PopupMenu output;
    output.addItem (juce::String::fromUTF8 ("마스터"), canEdit, spec.output.empty(), [this, targets] { setTrackOutput (targets, {}); });
    for (const auto& b : project_.composition().buses)
        output.addItem (juce::String::fromUTF8 (b.name.c_str()), canEdit, spec.output == b.name, [this, targets, name = b.name] { setTrackOutput (targets, name); });
    m.addSubMenu (juce::String::fromUTF8 ("출력 (") + (spec.output.empty() ? juce::String::fromUTF8 ("마스터") : juce::String::fromUTF8 (spec.output.c_str())) + ")", output);
    juce::PopupMenu folders;
    std::vector<std::string> names;
    for (const auto& t : project_.composition().tracks)
        if (! t.folder.empty() && std::find (names.begin(), names.end(), t.folder) == names.end())
            names.push_back (t.folder);
    {
        int n = 1;
        std::string fresh;
        do
            fresh = juce::String::fromUTF8 ("폴더 ").toStdString() + std::to_string (n++);
        while (std::find (names.begin(), names.end(), fresh) != names.end());
        folders.addItem (juce::String::fromUTF8 ("새 폴더로 묶기 (") + juce::String ((int) targets.size()) + juce::String::fromUTF8 ("트랙)"), canEdit, false,
                         [this, targets, fresh] { setTrackFolder (targets, fresh); });
    }
    for (const auto& name : names)
        folders.addItem (juce::String::fromUTF8 (name.c_str()) + juce::String::fromUTF8 (" 에 넣기"), canEdit, spec.folder == name, [this, targets, name] { setTrackFolder (targets, name); });
    if (! spec.folder.empty())
    {
        folders.addSeparator();
        folders.addItem (juce::String::fromUTF8 ("폴더에서 빼기"), canEdit, false, [this, targets] { setTrackFolder (targets, {}); });
    }
    m.addSubMenu (juce::String::fromUTF8 ("폴더"), folders);
    m.addSeparator();
    m.addItem (juce::String::fromUTF8 ("악기 창 열기"), isPlugin, false, [this, track] { openInstrumentWindow (track); });
    juce::PopupMenu instruments;
    addInstrumentMenus (instruments, *choices, 1000, spec.instrument);
    if (spec.instrument != "audio")
        m.addSubMenu (juce::String::fromUTF8 ("악기 바꾸기"), instruments);
    else
        m.addItem (juce::String::fromUTF8 ("오디오 파일 가져오기..."), canEdit, false, [this] { showImportAudioDialog(); });
    m.addSeparator();
    juce::PopupMenu fresh;
    auto freshChoices = std::make_shared<std::vector<InstrumentChoice>>();
    fresh.addItem (juce::String::fromUTF8 ("오디오 트랙"), [this, track] { addAudioTrack (track + 1); });
    fresh.addSeparator();
    addInstrumentMenus (fresh, *freshChoices, 5000, {});
    m.addSubMenu (juce::String::fromUTF8 ("아래에 새 트랙"), fresh, canEdit);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<MainComponent> (this), track, choices, freshChoices] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        if (result >= 1000 && (size_t) (result - 1000) < choices->size())
        {
            const auto& c = (*choices)[(size_t) (result - 1000)];
            safe->changeInstrument (track, c.name, c.definition);
        }
        else if (result >= 5000 && (size_t) (result - 5000) < freshChoices->size())
        {
            const auto& c = (*freshChoices)[(size_t) (result - 5000)];
            safe->addTrack (c.name, c.definition, track + 1);
        }
    });
}

void MainComponent::showNewTrackMenu()
{
    if (! project_.loaded() || ! registry_ || recording_)
        return;
    auto choices = std::make_shared<std::vector<InstrumentChoice>>();
    juce::PopupMenu m;
    m.addSectionHeader (juce::String::fromUTF8 ("새 트랙의 악기"));
    const int selected = timeline_.selectedTrack();
    const size_t at = selected >= 0 ? (size_t) selected + 1 : project_.trackCount();
    m.addItem (juce::String::fromUTF8 ("오디오 트랙 (오디오 파일의 클립)"), [this, at] { addAudioTrack (at); });
    juce::PopupMenu templates;
    for (const auto& [label, templ] : trackTemplates())
        templates.addItem (label, [this, templ = templ, at] { addTrackFromTemplate (templ, at); });
    m.addSubMenu (juce::String::fromUTF8 ("템플릿에서"), templates);
    m.addSeparator();
    addInstrumentMenus (m, *choices, 1000, {});
    m.showMenuAsync (juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<MainComponent> (this), choices, at] (int result)
    {
        if (safe == nullptr || result < 1000 || (size_t) (result - 1000) >= choices->size())
            return;
        const auto& c = (*choices)[(size_t) (result - 1000)];
        safe->addTrack (c.name, c.definition, at);
    });
}

juce::String MainComponent::trackNameForInstrument (const std::string& instrument)
{
    // "synth.saw-pad" -> "Saw Pad", "sso-violins-1" -> "Sso Violins 1"
    auto base = juce::String::fromUTF8 (instrument.c_str()).fromLastOccurrenceOf (".", false, false);
    juce::StringArray words;
    words.addTokens (base, "-_ ", {});
    words.removeEmptyStrings();
    for (auto& w : words)
        w = w.substring (0, 1).toUpperCase() + w.substring (1);
    return words.isEmpty() ? juce::String ("Track") : words.joinIntoString (" ");
}

bool MainComponent::editTracks (const juce::var& root, const juce::String& name, int selectAfter)
{
    if (recording_)
    {
        setStatus (juce::String::fromUTF8 ("녹음하는 동안에는 트랙 목록을 바꾸지 않습니다."), true);
        return false;
    }
    closeInstrumentWindows (true);
    pendingInstrumentWindows_.clear();
    juce::String error;
    if (! project_.editRoot (root, name, error))
    {
        if (error.isNotEmpty())
            setStatus (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    if (selectAfter >= 0)
        timeline_.setSelectedTrack (std::min (selectAfter, (int) project_.trackCount() - 1));
    setStatus (name);
    return true;
}

// ---- 출력과 폴더 ----

bool MainComponent::setTrackOutput (const std::vector<size_t>& tracks, const std::string& bus)
{
    if (! project_.loaded() || tracks.empty())
        return false;
    std::vector<Project::Change> changes;
    for (const size_t t : tracks)
    {
        if (t >= project_.trackCount() || project_.composition().tracks[t].output == bus)
            continue;
        auto json = project_.trackJson (t);
        if (bus.empty())
            json.getDynamicObject()->removeProperty ("output");
        else
            json.getDynamicObject()->setProperty ("output", juce::String::fromUTF8 (bus.c_str()));
        changes.push_back ({ (int) t, {}, json });
    }
    if (changes.empty())
        return false;
    juce::String error;
    const auto name = juce::String::fromUTF8 ("출력: ") + (bus.empty() ? juce::String::fromUTF8 ("마스터") : juce::String::fromUTF8 (bus.c_str()));
    if (! project_.edit (changes, name, error))
    {
        setStatus (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    setStatus (name);
    return true;
}

bool MainComponent::setTrackFolder (const std::vector<size_t>& tracks, const std::string& folder)
{
    if (! project_.loaded() || tracks.empty())
        return false;
    std::vector<Project::Change> changes;
    for (const size_t t : tracks)
    {
        if (t >= project_.trackCount() || project_.composition().tracks[t].folder == folder)
            continue;
        auto json = project_.trackJson (t);
        if (folder.empty())
            json.getDynamicObject()->removeProperty ("folder");
        else
            json.getDynamicObject()->setProperty ("folder", juce::String::fromUTF8 (folder.c_str()));
        changes.push_back ({ (int) t, {}, json });
    }
    if (changes.empty())
        return false;
    juce::String error;
    const auto name = folder.empty() ? juce::String::fromUTF8 ("폴더에서 빼기") : juce::String::fromUTF8 ("폴더: ") + juce::String::fromUTF8 (folder.c_str());
    if (! project_.edit (changes, name, error))
    {
        setStatus (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    setStatus (name);
    return true;
}

std::vector<size_t> MainComponent::folderTracks (const std::string& folder) const
{
    std::vector<size_t> out;
    if (project_.loaded() && ! folder.empty())
        for (size_t i = 0; i < project_.trackCount(); ++i)
            if (project_.composition().tracks[i].folder == folder)
                out.push_back (i);
    return out;
}

bool MainComponent::routeFolderToBus (const std::string& folder)
{
    const auto tracks = folderTracks (folder);
    if (tracks.empty())
        return false;
    // 같은 이름의 버스가 없으면 효과 없는 버스를 더하고, 트랙의 출력과 함께 편집 하나로
    std::vector<Project::Change> changes;
    const auto& buses = project_.composition().buses;
    if (std::none_of (buses.begin(), buses.end(), [&folder] (const mr::BusSpec& b) { return b.name == folder; }))
    {
        auto list = project_.property ("buses");
        if (! list.isArray())
            list = juce::var (juce::Array<juce::var>());
        auto* bus = new juce::DynamicObject();
        bus->setProperty ("name", juce::String::fromUTF8 (folder.c_str()));
        list.getArray()->add (juce::var (bus));
        changes.push_back ({ -1, "buses", list });
    }
    for (const size_t t : tracks)
    {
        auto json = project_.trackJson (t);
        json.getDynamicObject()->setProperty ("output", juce::String::fromUTF8 (folder.c_str()));
        changes.push_back ({ (int) t, {}, json });
    }
    juce::String error;
    const auto name = juce::String::fromUTF8 ("폴더를 버스로: ") + juce::String::fromUTF8 (folder.c_str());
    if (! project_.edit (changes, name, error))
    {
        setStatus (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    setStatus (name + juce::String::fromUTF8 (" (믹서의 버스 스트립이 폴더의 페이더입니다)"));
    return true;
}

bool MainComponent::renameFolder (const std::string& folder, const std::string& name)
{
    if (name.empty() || name == folder)
        return false;
    return setTrackFolder (folderTracks (folder), name);
}

void MainComponent::showFolderMenu (const std::string& folder)
{
    const auto tracks = folderTracks (folder);
    if (tracks.empty())
        return;
    const auto& spec = project_.composition();
    bool allMuted = true, routed = true;
    for (const size_t t : tracks)
    {
        allMuted = allMuted && spec.tracks[t].mixer.mute;
        routed = routed && spec.tracks[t].output == folder;
    }
    juce::PopupMenu m;
    m.addSectionHeader (juce::String::fromUTF8 ("폴더 ") + juce::String::fromUTF8 (folder.c_str()) + " (" + juce::String ((int) tracks.size())
                        + juce::String::fromUTF8 ("트랙)"));
    const bool collapsed = arrangement_.folderCollapsed (folder);
    m.addItem (collapsed ? juce::String::fromUTF8 ("펴기") : juce::String::fromUTF8 ("접기"), [this, folder, collapsed] { arrangement_.setFolderCollapsed (folder, ! collapsed); });
    m.addItem (juce::String::fromUTF8 ("이름 바꾸기..."), [this, folder]
    {
        folderDialog_ = std::make_unique<juce::AlertWindow> (juce::String::fromUTF8 ("폴더 이름"), juce::String(), juce::MessageBoxIconType::NoIcon, this);
        folderDialog_->addTextEditor ("name", juce::String::fromUTF8 (folder.c_str()));
        folderDialog_->addButton (juce::String::fromUTF8 ("바꾸기"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        folderDialog_->addButton (juce::String::fromUTF8 ("취소"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        folderDialog_->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MainComponent> (this), folder] (int result)
        {
            if (safe == nullptr || safe->folderDialog_ == nullptr)
                return;
            const auto text = safe->folderDialog_->getTextEditorContents ("name").trim().toStdString();
            safe->folderDialog_.reset();
            if (result == 1)
                safe->renameFolder (folder, text);
        }));
    });
    m.addSeparator();
    m.addItem (juce::String::fromUTF8 ("모두 뮤트"), true, allMuted, [this, tracks, allMuted]
    {
        std::vector<Project::Change> changes;
        for (const size_t t : tracks)
        {
            auto json = project_.trackJson (t);
            auto mixer = json.getProperty ("mixer", juce::var());
            if (! mixer.isObject())
                mixer = juce::var (new juce::DynamicObject());
            mixer.getDynamicObject()->setProperty ("mute", ! allMuted);
            json.getDynamicObject()->setProperty ("mixer", mixer);
            changes.push_back ({ (int) t, {}, json });
        }
        juce::String error;
        project_.edit (changes, allMuted ? juce::String::fromUTF8 ("폴더 뮤트 풀기") : juce::String::fromUTF8 ("폴더 뮤트"), error);
    });
    m.addItem (routed ? juce::String::fromUTF8 ("버스로 묶음 (") + juce::String::fromUTF8 (folder.c_str()) + ")" : juce::String::fromUTF8 ("같은 이름의 버스로 묶기 (폴더의 페이더와 효과)"),
               ! routed, routed, [this, folder] { routeFolderToBus (folder); });
    if (routed)
        m.addItem (juce::String::fromUTF8 ("버스 묶음 풀기 (마스터로)"), [this, tracks] { setTrackOutput (tracks, {}); });
    m.addSeparator();
    m.addItem (juce::String::fromUTF8 ("폴더 풀기 (트랙은 그대로)"), [this, tracks] { setTrackFolder (tracks, {}); });
    m.showMenuAsync (juce::PopupMenu::Options());
}

void MainComponent::setMidiLearn (bool on)
{
    if (on)
        showMixer (true);
    mixer_.setLearnMode (on);
    setStatus (on ? juce::String::fromUTF8 ("MIDI 배우기: 믹서의 페이더, 밸런스, 뮤트, 솔로, VCA, 열린 효과의 매개변수를 누르고 컨트롤러를 움직이면 묶습니다. 오른쪽 클릭은 묶음 지우기")
                  : juce::String::fromUTF8 ("MIDI 배우기를 마쳤습니다 (") + juce::String ((int) learn_->bindings().size()) + juce::String::fromUTF8 ("개 묶음)."));
    commands_.commandStatusChanged();
}

void MainComponent::setAutomationWrite (bool on)
{
    automationWrite_ = on;
    mixer_.setWriteMode (on);
    setStatus (on ? juce::String::fromUTF8 ("곡선 쓰기를 켰습니다. 재생하며 믹서의 페이더와 밸런스를 움직이면 그 움직임이 곡선으로 적히고, 손을 떼면 원래 곡선으로 돌아갑니다.")
                  : juce::String::fromUTF8 ("곡선 쓰기를 껐습니다."));
    commands_.commandStatusChanged();
}

int MainComponent::inputMonitorTrack() const
{
    if (monitorTrackName_.empty() || ! project_.loaded())
        return -1;
    const auto& tracks = project_.composition().tracks;
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].name == monitorTrackName_ && tracks[i].instrument == "audio")
            return (int) i;
    return -1;
}

void MainComponent::applyInputMonitor()
{
    const int index = inputMonitorTrack();
    if (index < 0)
        monitorTrackName_.clear();
    if (auto p = player())
        p->setInputMonitor (index);
    arrangement_.setInputMonitorTrack (index);
}

bool MainComponent::setInputMonitoring (size_t track, bool on)
{
    if (! project_.loaded() || track >= project_.trackCount())
        return false;
    if (! on)
    {
        monitorTrackName_.clear();
        applyInputMonitor();
        setStatus (juce::String::fromUTF8 ("입력 모니터링을 껐습니다."));
        commands_.commandStatusChanged();
        return true;
    }
    const auto& spec = project_.composition().tracks[track];
    if (spec.instrument != "audio")
    {
        setStatus (juce::String::fromUTF8 ("입력 모니터링은 오디오 트랙에서 합니다."), true);
        return false;
    }
    if (audio_ != nullptr && audio_->enableInput() <= 0)
    {
        setStatus (juce::String::fromUTF8 ("오디오 입력이 없습니다. 설정 > 오디오 장치(⌘,)에서 입력 장치와 채널을 고릅니다."), true);
        return false;
    }
    monitorTrackName_ = spec.name;
    applyInputMonitor();
    setStatus (juce::String::fromUTF8 ("입력 모니터링: ") + juce::String::fromUTF8 (spec.name.c_str())
               + juce::String::fromUTF8 (" (입력이 이 트랙의 효과, 페이더, 센드를 지납니다. 재생기의 지연만큼 늦게 들립니다)"));
    commands_.commandStatusChanged();
    return true;
}

bool MainComponent::bounceTrack (size_t track)
{
    if (! project_.loaded() || track >= project_.trackCount() || bounceJob_ != nullptr || recording_)
        return false;
    const auto name = project_.composition().tracks[track].name;
    const auto output = nextBounceFile (project_.file(), name);
    bounceTrackName_ = name;
    bounceJob_ = std::make_unique<BounceJob> (project_.composition(), track, output, [this] (const BounceOutcome& outcome)
    {
        // 작업은 자기 콜백 안에서 지우지 않는다
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), trackName = bounceTrackName_, outcome]
        {
            if (safe == nullptr)
                return;
            safe->bounceJob_.reset();
            safe->finishBounce (trackName, outcome);
            safe->commands_.commandStatusChanged();
        });
    });
    bounceJob_->start();
    setStatus (juce::String::fromUTF8 ("오디오로 굳히는 중: ") + juce::String::fromUTF8 (name.c_str()));
    commands_.commandStatusChanged();
    return true;
}

bool MainComponent::finishBounce (const std::string& trackName, const BounceOutcome& outcome)
{
    const auto shown = juce::String::fromUTF8 (trackName.c_str());
    if (! outcome.ok)
    {
        setStatus (outcome.cancelled ? juce::String::fromUTF8 ("굳히기를 멈췄습니다.")
                                     : juce::String::fromUTF8 ("굳히지 못했습니다: ") + outcome.error.replaceCharacter ('\n', ' '),
                   ! outcome.cancelled);
        return false;
    }
    if (outcome.result.silent)
    {
        setStatus (shown + juce::String::fromUTF8 (": 소리가 없어 굳히지 않았습니다."), true);
        return false;
    }
    const auto root = withBounce (project_.documentRoot(), trackName, outcome.result, project_.file().getParentDirectory());
    if (root.isVoid())
    {
        setStatus (shown + juce::String::fromUTF8 (": 트랙이 없어져 굳힌 파일을 넣지 못했습니다 (") + outcome.result.file.getFileName() + ")", true);
        return false;
    }
    int index = 0;
    for (size_t i = 0; i < project_.trackCount(); ++i)
        if (project_.composition().tracks[i].name == trackName)
            index = (int) i + 1;
    if (! editTracks (root, juce::String::fromUTF8 ("오디오로 굳히기: ") + shown, index))
        return false;
    setStatus (juce::String::fromUTF8 ("오디오로 굳혔습니다: ") + shown + " (" + outcome.result.file.getFileName() + ", "
               + describeBeats (outcome.result.startBeat, project_.meters()) + juce::String::fromUTF8 ("부터 ")
               + juce::String (outcome.result.lengthSeconds, 1) + juce::String::fromUTF8 ("초). 원래 트랙은 뮤트했습니다."));
    return true;
}

bool MainComponent::addTrack (const std::string& instrument, const juce::var& definition, std::optional<size_t> at)
{
    if (! project_.loaded())
        return false;
    const size_t index = std::min (at.value_or (project_.trackCount()), project_.trackCount());
    auto root = mr::withTrackInserted (project_.documentRoot(), index,
                                       mr::newTrackJson (trackNameForInstrument (instrument).toStdString(), instrument));
    if (definition.getDynamicObject() != nullptr)
        root.getDynamicObject()->setProperty ("instruments", mr::withInlineInstrument (root.getProperty ("instruments", juce::var()), instrument, definition));
    const auto* def = registry_ ? registry_->find (instrument) : nullptr;
    const bool plugin = definition.getDynamicObject() != nullptr || (def != nullptr && (def->type == "vst3" || def->type == "au"));
    const auto newName = juce::String (root["tracks"][(int) index]["name"].toString());
    if (! editTracks (root, juce::String::fromUTF8 ("트랙 더하기: ") + newName, (int) index))
        return false;
    if (plugin && withAudio_)
        pendingInstrumentWindows_.push_back (index);
    return true;
}

bool MainComponent::saveTrackTemplate (size_t track, const juce::String& rawName, juce::String& error)
{
    if (! project_.loaded() || track >= project_.trackCount() || trackTemplateFolder == juce::File())
        return false;
    const auto name = rawName.trim();
    const auto file = juce::File::createLegalFileName (name);
    if (file.isEmpty())
    {
        error = juce::String::fromUTF8 ("이름이 비어 있습니다.");
        return false;
    }
    flushInstrumentWindows();   // 플러그인 악기의 지금 소리
    const auto templ = mr::trackTemplateOf (project_.documentRoot(), track, name.toStdString(), project_.file().getParentDirectory());
    if (! trackTemplateFolder.createDirectory() || ! trackTemplateFolder.getChildFile (file + ".json").replaceWithText (juce::JSON::toString (templ)))
    {
        error = juce::String::fromUTF8 ("템플릿을 쓰지 못했습니다: ") + trackTemplateFolder.getChildFile (file + ".json").getFullPathName();
        return false;
    }
    setStatus (juce::String::fromUTF8 ("트랙 템플릿을 저장했습니다: ") + name);
    return true;
}

std::vector<std::pair<juce::String, juce::var>> MainComponent::trackTemplates() const
{
    static const std::map<std::string, const char*> names {
        { "vocal", "보컬 (오디오, EQ, 디에서, 컴프레서)" }, { "drum-kit", "드럼 세트 (트랜지언트, 병렬 압축)" }, { "grand-piano", "그랜드 피아노 (EQ, 컴프레서)" },
        { "bass-guitar", "베이스 기타 (EQ, 컴프레서)" }, { "synth-lead", "신스 리드 (코러스, 점8분음표 딜레이)" }, { "synth-pad", "신스 패드 (넓게, 리버브)" },
        { "strings", "현악 (EQ, 리버브)" },
    };
    std::vector<std::pair<juce::String, juce::var>> list;
    for (const auto& [id, templ] : mr::factoryTrackTemplates())
    {
        const auto it = names.find (id);
        list.push_back ({ it != names.end() ? juce::String::fromUTF8 (it->second) : juce::String (id), templ });
    }
    if (trackTemplateFolder.isDirectory())
    {
        auto files = trackTemplateFolder.findChildFiles (juce::File::findFiles, false, "*.json");
        files.sort();
        for (const auto& f : files)
        {
            const auto templ = juce::JSON::parse (f.loadFileAsString());
            if (templ.getProperty ("track", juce::var()).getDynamicObject() != nullptr)
                list.push_back ({ templ.getProperty ("name", f.getFileNameWithoutExtension()).toString(), templ });
        }
    }
    return list;
}

bool MainComponent::addTrackFromTemplate (const juce::var& templ, std::optional<size_t> at)
{
    if (! project_.loaded())
        return false;
    const size_t index = std::min (at.value_or (project_.trackCount()), project_.trackCount());
    int dropped = 0;
    const auto root = mr::withTrackFromTemplate (project_.documentRoot(), index, templ, &dropped);
    if (root.isVoid())
    {
        setStatus (juce::String::fromUTF8 ("트랙 템플릿이 아닙니다."), true);
        return false;
    }
    const auto newName = juce::String (root["tracks"][(int) index]["name"].toString());
    if (! editTracks (root, juce::String::fromUTF8 ("템플릿으로 트랙 더하기: ") + newName, (int) index))
        return false;
    if (dropped > 0)
        setStatus (juce::String::fromUTF8 ("이 곡에 없는 버스나 키 트랙을 가리키던 설정 ") + juce::String (dropped) + juce::String::fromUTF8 ("개를 뺐습니다."));
    return true;
}

bool MainComponent::duplicateTrack (size_t track)
{
    if (! project_.loaded() || track >= project_.trackCount())
        return false;
    flushInstrumentWindows();   // 플러그인의 지금 소리를 사본에도
    return editTracks (mr::withTrackDuplicated (project_.documentRoot(), track),
                       juce::String::fromUTF8 ("트랙 복제: ") + project_.channelName (ChannelRef::track (track)), (int) track + 1);
}

bool MainComponent::deleteTrack (size_t track)
{
    if (! project_.loaded() || track >= project_.trackCount() || project_.trackCount() < 2)
        return false;
    int cleared = 0;
    auto root = mr::withTrackRemoved (project_.documentRoot(), track, &cleared);
    auto name = juce::String::fromUTF8 ("트랙 지우기: ") + project_.channelName (ChannelRef::track (track));
    if (! editTracks (root, name, std::max (0, (int) track - (track + 1 >= project_.trackCount() ? 1 : 0))))
        return false;
    timeline_.setSelectedTrack (std::min ((int) track, (int) project_.trackCount() - 1));
    if (cleared > 0)
        setStatus (name + juce::String::fromUTF8 (" (이 트랙을 키로 쓰던 사이드체인 ") + juce::String (cleared) + juce::String::fromUTF8 ("개를 풀었습니다)"));
    return true;
}

bool MainComponent::renameTrack (size_t track, const juce::String& name)
{
    if (! project_.loaded() || track >= project_.trackCount())
        return false;
    const auto text = name.trim();
    const auto old = project_.channelName (ChannelRef::track (track));
    if (text.isEmpty() || text == old)
        return false;
    for (size_t i = 0; i < project_.trackCount(); ++i)
        if (i != track && project_.channelName (ChannelRef::track (i)) == text)
        {
            setStatus (juce::String::fromUTF8 ("같은 이름의 트랙이 있습니다: ") + text, true);
            return false;
        }
    return editTracks (mr::withTrackRenamed (project_.documentRoot(), track, text.toStdString()),
                       juce::String::fromUTF8 ("트랙 이름: ") + old + juce::String::fromUTF8 (" → ") + text, -1);
}

bool MainComponent::moveTrack (size_t from, size_t to)
{
    if (! project_.loaded() || from >= project_.trackCount() || to >= project_.trackCount() || from == to)
        return false;
    return editTracks (mr::withTrackMoved (project_.documentRoot(), from, to),
                       juce::String::fromUTF8 ("트랙 옮기기: ") + project_.channelName (ChannelRef::track (from)), (int) to);
}

bool MainComponent::setTrackColour (size_t track, const juce::String& hex)
{
    if (! project_.loaded() || track >= project_.trackCount())
        return false;
    juce::String error;
    auto root = mr::withTrackColour (project_.documentRoot(), track, hex.toStdString());
    if (! project_.editTrack (track, root["tracks"][(int) track], juce::String::fromUTF8 ("트랙 색"), error))
        return false;
    arrangement_.repaint();
    mixer_.repaint();
    return true;
}

// ---- 오디오 트랙과 클립 ----

std::optional<mr::AudioClip> MainComponent::clipAt (ArrangementPanel::ClipRef ref) const
{
    if (! project_.loaded() || ref.track >= project_.trackCount() || ref.clip >= project_.composition().tracks[ref.track].clips.size())
        return std::nullopt;
    return project_.composition().tracks[ref.track].clips[ref.clip];
}

bool MainComponent::addAudioTrack (std::optional<size_t> at)
{
    if (! project_.loaded())
        return false;
    const size_t index = std::min (at.value_or (project_.trackCount()), project_.trackCount());
    auto track = mr::newTrackJson ("Audio", "audio");
    track.getDynamicObject()->removeProperty ("notes");
    track.getDynamicObject()->setProperty ("clips", juce::Array<juce::var>());
    auto root = mr::withTrackInserted (project_.documentRoot(), index, track);
    return editTracks (root, juce::String::fromUTF8 ("오디오 트랙 더하기"), (int) index);
}

bool MainComponent::importAudio (const juce::StringArray& files, std::optional<size_t> track, double beat, juce::String& error)
{
    if (! project_.loaded())
        return false;
    if (recording_)
    {
        error = juce::String::fromUTF8 ("녹음하는 동안에는 가져오지 않습니다.");
        return false;
    }
    const auto folder = project_.file().getParentDirectory();
    auto root = project_.documentRoot().clone();
    int added = 0;
    size_t insertAt = timeline_.selectedTrack() >= 0 ? (size_t) timeline_.selectedTrack() + 1 : project_.trackCount();
    std::optional<size_t> firstNew;
    for (const auto& path : files)
    {
        const juce::File source (path);
        const auto info = mr::audioFileInfo (source);
        if (! info)
        {
            error = juce::String::fromUTF8 ("읽을 수 없는 오디오 파일입니다: ") + source.getFileName();
            return false;
        }
        // 곡 폴더 밖의 파일은 audio/ 에 복사한다 (같은 이름의 다른 파일이 있으면 번호를 붙인다)
        juce::File target = source;
        if (! source.isAChildOf (folder))
        {
            const auto dir = folder.getChildFile ("audio");
            dir.createDirectory();
            target = dir.getChildFile (source.getFileName());
            for (int n = 2; target.existsAsFile() && target.getSize() != source.getSize(); ++n)
                target = dir.getChildFile (source.getFileNameWithoutExtension() + " " + juce::String (n) + source.getFileExtension());
            if (! target.existsAsFile() && ! source.copyFileTo (target))
            {
                error = juce::String::fromUTF8 ("곡 폴더에 복사하지 못했습니다: ") + target.getFullPathName();
                return false;
            }
        }
        mr::AudioClip clip;
        clip.file = target.getRelativePathFrom (folder).replaceCharacter ('\\', '/').toStdString();
        clip.startBeat = std::max (0.0, beat);
        clip.lengthSeconds = std::round (info->seconds() * 1.0e6) / 1.0e6;
        auto* tracks = root["tracks"].getArray();
        if (track && *track < (size_t) tracks->size())
            tracks->set ((int) *track, mr::withClipAdded ((*tracks)[(int) *track], clip));
        else
        {
            auto t = mr::newTrackJson (source.getFileNameWithoutExtension().toStdString(), "audio");
            t.getDynamicObject()->removeProperty ("notes");
            t = mr::withClipAdded (t, clip);
            root = mr::withTrackInserted (root, insertAt, t);
            if (! firstNew)
                firstNew = insertAt;
            ++insertAt;
        }
        ++added;
    }
    juce::String editError;
    if (firstNew)
        closeInstrumentWindows (true);
    if (! project_.editRoot (root, juce::String::fromUTF8 ("오디오 가져오기: ") + juce::File (files[0]).getFileName()
                                       + (added > 1 ? juce::String::fromUTF8 (" 외 ") + juce::String (added - 1) + juce::String::fromUTF8 ("개") : juce::String()),
                            editError))
    {
        error = editError;
        return false;
    }
    if (firstNew)
        timeline_.setSelectedTrack ((int) *firstNew);
    else if (track)
        timeline_.setSelectedTrack ((int) *track);
    setStatus (juce::String::fromUTF8 ("오디오 파일 ") + juce::String (added) + juce::String::fromUTF8 ("개를 넣었습니다 (") + describeBeats (beat, project_.meters())
               + juce::String::fromUTF8 ("). 클립을 끌어 옮기고, 양 끝을 끌어 자르고, 위 모서리를 끌어 페이드를 겁니다."));
    return true;
}

std::optional<MainComponent::SampleTarget> MainComponent::prepareSampleTarget (const juce::File& audio, const juce::String& suffix, juce::String& error)
{
    const auto folder = project_.file().getParentDirectory();
    const auto samples = folder.getChildFile ("samples");
    samples.createDirectory();
    SampleTarget t;
    // 곡 폴더 밖의 파일은 samples/ 에 복사한다 (같은 이름의 다른 파일이면 번호)
    t.sample = audio;
    if (! audio.isAChildOf (folder))
    {
        t.sample = samples.getChildFile (audio.getFileName());
        for (int n = 2; t.sample.existsAsFile() && t.sample.getSize() != audio.getSize(); ++n)
            t.sample = samples.getChildFile (audio.getFileNameWithoutExtension() + " " + juce::String (n) + audio.getFileExtension());
        if (! t.sample.existsAsFile() && ! audio.copyFileTo (t.sample))
        {
            error = juce::String::fromUTF8 ("곡 폴더에 복사하지 못했습니다: ") + t.sample.getFullPathName();
            return std::nullopt;
        }
    }
    auto stem = juce::File::createLegalFileName (audio.getFileNameWithoutExtension()).replaceCharacter (' ', '-').toLowerCase();
    if (stem.isEmpty())
        stem = "sample";
    stem += suffix;
    // 악기 이름과 SFZ 파일 (겹치지 않게)
    for (int n = 1;; ++n)
    {
        const auto number = n == 1 ? juce::String() : "-" + juce::String (n);
        t.name = ("sampler." + stem + number).toStdString();
        t.sfz = samples.getChildFile (stem + number + ".sfz");
        if ((! registry_ || registry_->find (t.name) == nullptr) && ! t.sfz.exists())
            break;
    }
    return t;
}

juce::var MainComponent::withInlineInstrument (const juce::var& root, const SampleTarget& target, const juce::String& description) const
{
    // 곡 안의 악기 정의 ("instruments" 배열에 객체 하나). 지금 문서를 직접 고치지 않는다 (실행 취소가 이전 상태를 쥐고 있다)
    auto result = root.clone();
    auto instruments = result.getProperty ("instruments", juce::var());
    juce::Array<juce::var> list;
    if (instruments.isArray())
        list = *instruments.getArray();
    else if (instruments.isString() || instruments.isObject())
        list.add (instruments);
    auto* def = new juce::DynamicObject();
    def->setProperty ("type", "sfz");
    def->setProperty ("file", target.sfz.getRelativePathFrom (project_.file().getParentDirectory()).replaceCharacter ('\\', '/'));
    def->setProperty ("description", description);
    auto* entry = new juce::DynamicObject();
    entry->setProperty (juce::Identifier (juce::String (target.name)), juce::var (def));
    list.add (juce::var (entry));
    result.getDynamicObject()->setProperty ("instruments", list);
    return result;
}

bool MainComponent::makeSamplerTrack (const juce::File& audio, double startSeconds, std::optional<double> lengthSeconds, bool oneShot, juce::String& error)
{
    if (! project_.loaded() || ! audio.existsAsFile())
        return false;
    const auto target = prepareSampleTarget (audio, {}, error);
    if (! target)
        return false;
    mr::SamplerOptions options;
    options.oneShot = oneShot;
    options.startSeconds = startSeconds;
    options.lengthSeconds = lengthSeconds;
    options.relativeWithin = project_.file().getParentDirectory();
    mr::SamplerResult result;
    try
    {
        result = mr::writeSamplerSfz (target->sfz, target->sample, options);
    }
    catch (const std::exception& e)
    {
        error = juce::String::fromUTF8 (e.what());
        return false;
    }
    const size_t at = timeline_.selectedTrack() >= 0 ? (size_t) timeline_.selectedTrack() + 1 : project_.trackCount();
    auto root = withInlineInstrument (project_.documentRoot(), *target,
                                      juce::String::fromUTF8 ("퀵 샘플러: ") + audio.getFileName()
                                          + (oneShot ? juce::String::fromUTF8 (" (원샷)") : juce::String::fromUTF8 (", 기준 건반 ") + juce::String (result.rootKey)));
    auto track = mr::newTrackJson (mr::uniqueTrackName (root, audio.getFileNameWithoutExtension().toStdString()), target->name);
    root = mr::withTrackInserted (root, at, track);
    const auto title = juce::String::fromUTF8 ("오디오로 악기 만들기: ") + audio.getFileName();
    if (! editTracks (root, title, (int) at))
        return false;
    setStatus (title + (result.pitch ? juce::String::fromUTF8 (" (잰 높이 ") + juce::String (result.pitch->frequency, 1) + " Hz, " + juce::String (result.tuneCents, 1)
                                           + juce::String::fromUTF8 (" 센트 고침)")
                                     : oneShot ? juce::String::fromUTF8 (" (원샷, 모든 건반이 같은 소리)") : juce::String::fromUTF8 (" (높이를 재지 못해 C4 를 기준으로)")));
    return true;
}

bool MainComponent::sliceClip (ArrangementPanel::ClipRef ref, juce::String& error)
{
    const auto clip = clipAt (ref);
    if (! clip)
        return false;
    const auto folder = project_.file().getParentDirectory();
    const auto source = mr::clipSourceFile (*clip, folder);
    const double length = mr::clipLengthSeconds (*clip, folder);
    std::vector<double> onsets;
    try
    {
        onsets = mr::detectOnsets (source, clip->offsetSeconds, length);
    }
    catch (const std::exception& e)
    {
        error = juce::String::fromUTF8 (e.what());
        return false;
    }
    if (onsets.empty())
    {
        error = juce::String::fromUTF8 ("이 클립에서 어택을 찾지 못했습니다.");
        return false;
    }
    constexpr int firstKey = 36, most = 64;
    const bool cut = (int) onsets.size() > most;
    if (cut)
        onsets.resize ((size_t) most);
    const double end = clip->offsetSeconds + length;
    const auto target = prepareSampleTarget (source, "-slices", error);
    if (! target)
        return false;
    try
    {
        mr::writeSliceSfz (target->sfz, target->sample, onsets, end, firstKey, folder);
    }
    catch (const std::exception& e)
    {
        error = juce::String::fromUTF8 (e.what());
        return false;
    }
    // 조각마다 음 하나: 클립이 그 어택을 내는 곡의 박에서, 다음 조각 5 ms 앞까지 (늘인 클립이면 곡 템포를 따른 자리)
    const auto& tempo = project_.tempo();
    auto beatOf = [&] (double s) { return tempo.beatAt (mr::clipSongSecondsAt (*clip, tempo, s)); };
    auto round5 = [] (double v) { return std::round (v * 100000.0) / 100000.0; };
    juce::Array<juce::var> notes;
    for (size_t i = 0; i < onsets.size(); ++i)
    {
        const double next = i + 1 < onsets.size() ? onsets[i + 1] : end;
        const double b0 = round5 (beatOf (onsets[i])), b1 = round5 (beatOf (std::max (onsets[i] + 0.01, next - 0.005)));
        auto* n = new juce::DynamicObject();
        n->setProperty ("pitch", firstKey + (int) i);
        n->setProperty ("startBeat", b0);
        n->setProperty ("durationBeat", round5 (std::max (0.001, b1 - b0)));
        n->setProperty ("velocity", 127);   // 원래 크기 그대로
        n->setProperty ("humanize", false);
        notes.add (juce::var (n));
    }
    auto root = withInlineInstrument (project_.documentRoot(), *target,
                                      juce::String::fromUTF8 ("어택으로 나눈 조각: ") + source.getFileName() + " (" + juce::String ((int) onsets.size())
                                          + juce::String::fromUTF8 ("개, 건반 ") + juce::String (mr::noteName (firstKey)) + juce::String::fromUTF8 ("부터)"));
    auto track = mr::newTrackJson (mr::uniqueTrackName (root, source.getFileNameWithoutExtension().toStdString() + " slices"), target->name);
    track.getDynamicObject()->setProperty ("notes", notes);
    const size_t at = std::min (ref.track + 1, project_.trackCount());
    root = mr::withTrackInserted (root, at, track);
    const auto title = juce::String::fromUTF8 ("어택으로 조각내기: ") + source.getFileName();
    if (! editTracks (root, title, (int) at))
        return false;
    setStatus (title + " (" + juce::String ((int) onsets.size()) + juce::String::fromUTF8 ("조각, 건반 ") + juce::String (mr::noteName (firstKey)) + " ~ "
               + juce::String (mr::noteName (firstKey + (int) onsets.size() - 1)) + ")"
               + (cut ? juce::String::fromUTF8 (". 64조각까지만 썼습니다") : juce::String()));
    return true;
}

bool MainComponent::detectClipTempo (ArrangementPanel::ClipRef ref, juce::String& error)
{
    auto clip = clipAt (ref);
    if (! clip)
        return false;
    if (! mr::clipStretchAvailable())
    {
        error = juce::String::fromUTF8 ("이 빌드에서는 클립 늘이기를 쓸 수 없습니다.");
        return false;
    }
    const auto folder = project_.file().getParentDirectory();
    std::optional<mr::TempoEstimate> tempo;
    try
    {
        tempo = mr::estimateTempo (mr::clipSourceFile (*clip, folder), clip->offsetSeconds, mr::clipLengthSeconds (*clip, folder));
    }
    catch (const std::exception& e)
    {
        error = juce::String::fromUTF8 (e.what());
        return false;
    }
    if (! tempo)
    {
        error = juce::String::fromUTF8 ("이 클립에서 템포를 재지 못했습니다 (어택이 넷보다 적습니다).");
        return false;
    }
    clip->sourceBpm = tempo->bpm;
    clip->stretch = 1.0;
    clip->warp.clear();   // 잰 템포로 고르게 늘인다
    if (! editClip (ref, *clip))
        return false;
    setStatus (juce::String::fromUTF8 ("클립의 템포: ") + juce::String (tempo->bpm, 2) + " BPM" + juce::String::fromUTF8 (" (확신 ")
               + juce::String (juce::roundToInt (tempo->confidence * 100.0)) + juce::String::fromUTF8 ("%, 첫 박이 클립 시작에서 ")
               + juce::String (tempo->firstBeatSeconds - clip->offsetSeconds, 3) + juce::String::fromUTF8 ("초). 이제 곡의 템포를 따라 늘고 줄어듭니다."));
    return true;
}

bool MainComponent::followClipTempo (ArrangementPanel::ClipRef ref, juce::String& error)
{
    auto clip = clipAt (ref);
    if (! clip)
        return false;
    const auto folder = project_.file().getParentDirectory();
    std::vector<double> beats;
    try
    {
        beats = mr::trackBeats (mr::clipSourceFile (*clip, folder), clip->offsetSeconds, mr::clipLengthSeconds (*clip, folder));
    }
    catch (const std::exception& e)
    {
        error = juce::String::fromUTF8 (e.what());
        return false;
    }
    const auto after = mr::withTempoFollowingClip (project_.documentRoot(), ref.track, ref.clip, beats, project_.meters());
    if (after.isVoid())
    {
        error = juce::String::fromUTF8 ("이 클립에서 박을 셋 넘게 찾지 못했습니다.");
        return false;
    }
    if (! project_.editRoot (after, juce::String::fromUTF8 ("곡 템포를 녹음에 맞추기"), error))
        return false;
    double lo = 1.0e9, hi = 0.0;
    for (size_t i = 1; i < beats.size(); ++i)
    {
        lo = std::min (lo, 60.0 / (beats[i] - beats[i - 1]));
        hi = std::max (hi, 60.0 / (beats[i] - beats[i - 1]));
    }
    setStatus (juce::String::fromUTF8 ("박 ") + juce::String ((int) beats.size()) + juce::String::fromUTF8 ("개에 템포를 맞췄습니다 (")
               + juce::String (lo, 1) + "~" + juce::String (hi, 1) + juce::String::fromUTF8 (" BPM). 클립은 늘이지 않고 첫 박이 마디 머리에 놓입니다."));
    return true;
}

bool MainComponent::saveGrooveFromClip (ArrangementPanel::ClipRef ref, const juce::String& name, juce::String& error)
{
    auto clip = clipAt (ref);
    if (! clip || pianoRoll_.grooveFolder == juce::File())
        return false;
    const auto folder = project_.file().getParentDirectory();
    const auto file = mr::clipSourceFile (*clip, folder);
    std::vector<double> onsets, levels;
    try
    {
        onsets = mr::detectOnsets (file, clip->offsetSeconds, mr::clipLengthSeconds (*clip, folder));
        levels = mr::onsetLevelsDb (file, onsets);
    }
    catch (const std::exception& e)
    {
        error = juce::String::fromUTF8 (e.what());
        return false;
    }
    if (onsets.size() < 4)
    {
        error = juce::String::fromUTF8 ("어택이 넷보다 적어 그루브를 만들지 못했습니다.");
        return false;
    }
    // 어택의 곡의 박 (클립이 늘었으면 늘인 자리), 클립이 시작하는 마디부터 한 마디
    std::vector<double> beats;
    for (const double s : onsets)
        beats.push_back (project_.beatAtSeconds (mr::clipSongSecondsAt (*clip, project_.tempo(), s)));
    const auto& meters = project_.meters();
    auto groove = mr::grooveFromHits (beats, levels, 0.25, meters.barBeatsAt (clip->startBeat));
    return saveGroove (pianoRoll_.grooveFolder, groove, name, error);
}

bool MainComponent::quantizeClipAudio (ArrangementPanel::ClipRef ref, double grid, double strength, juce::String& error, std::optional<mr::Groove> groove)
{
    auto clip = clipAt (ref);
    if (! clip)
        return false;
    if (! mr::clipStretchAvailable())
    {
        error = juce::String::fromUTF8 ("이 빌드에서는 클립 늘이기를 쓸 수 없습니다.");
        return false;
    }
    const auto folder = project_.file().getParentDirectory();
    const double length = mr::clipLengthSeconds (*clip, folder);
    std::vector<double> onsets;
    try
    {
        onsets = mr::detectOnsets (mr::clipSourceFile (*clip, folder), clip->offsetSeconds, length);
    }
    catch (const std::exception& e)
    {
        error = juce::String::fromUTF8 (e.what());
        return false;
    }
    mr::AudioQuantizeOptions options;
    options.grid = grid;
    options.strength = strength;
    options.groove = groove;
    auto warp = mr::quantizedWarp (*clip, project_.tempo(), length, onsets, options);
    if (warp.empty())
    {
        error = juce::String::fromUTF8 ("격자로 옮길 어택이 없습니다 (어택 ") + juce::String ((int) onsets.size()) + juce::String::fromUTF8 ("개).");
        return false;
    }
    clip->warp = std::move (warp);
    clip->stretch = 1.0;   // 마커가 지금의 늘이기를 담는다
    if (! editClip (ref, *clip))
        return false;
    setStatus (juce::String::fromUTF8 ("어택 ") + juce::String ((int) onsets.size()) + juce::String::fromUTF8 ("개 가운데 ")
               + juce::String ((int) clip->warp.size() - 2) + juce::String::fromUTF8 ("개를 격자에 맞췄습니다 (워프 마커, 세기 ")
               + juce::String (juce::roundToInt (strength * 100.0)) + "%).");
    return true;
}

bool MainComponent::transcribeClip (ArrangementPanel::ClipRef ref, juce::String& error)
{
    const auto clip = clipAt (ref);
    if (! clip)
        return false;
    const auto folder = project_.file().getParentDirectory();
    const auto source = mr::clipSourceFile (*clip, folder);
    std::vector<mr::TranscribedNote> notes;
    try
    {
        notes = mr::transcribeMelody (source, clip->offsetSeconds, mr::clipLengthSeconds (*clip, folder));
    }
    catch (const std::exception& e)
    {
        error = juce::String::fromUTF8 (e.what());
        return false;
    }
    if (notes.empty())
    {
        error = juce::String::fromUTF8 ("이 클립에서 높이가 분명한 소리를 찾지 못했습니다.");
        return false;
    }
    // 파일의 초에서 곡의 박으로 (늘인 클립도), 음높이를 바꾼 클립은 들리는 높이로
    const auto& tempo = project_.tempo();
    const auto list = mr::transcribedNotesJson (notes, [&] (double s) { return tempo.beatAt (mr::clipSongSecondsAt (*clip, tempo, s)); },
                                                (int) std::lround (clip->pitchSemitones));
    std::string instrument = "grand-piano";
    if (registry_ && registry_->find (instrument) == nullptr)
        instrument = "synth.piano";
    auto root = project_.documentRoot().clone();
    auto track = mr::newTrackJson (mr::uniqueTrackName (root, source.getFileNameWithoutExtension().toStdString() + " melody"), instrument);
    track.getDynamicObject()->setProperty ("notes", list);
    const size_t at = std::min (ref.track + 1, project_.trackCount());
    root = mr::withTrackInserted (root, at, track);
    int low = 127, high = 0;
    for (const auto& n : notes)
    {
        low = std::min (low, n.pitch + (int) std::lround (clip->pitchSemitones));
        high = std::max (high, n.pitch + (int) std::lround (clip->pitchSemitones));
    }
    const auto title = juce::String::fromUTF8 ("멜로디를 음표로: ") + source.getFileName();
    if (! editTracks (root, title, (int) at))
        return false;
    setStatus (title + " (" + juce::String (list.size()) + juce::String::fromUTF8 ("음, ") + juce::String (mr::noteName (low)) + " ~ "
               + juce::String (mr::noteName (high)) + juce::String::fromUTF8 ("). 박에 맞추려면 피아노 롤의 양자화를 씁니다."));
    return true;
}

void MainComponent::showSamplerDialog()
{
    if (! project_.loaded())
        return;
    chooser_ = std::make_unique<juce::FileChooser> (juce::String::fromUTF8 ("악기로 만들 오디오 파일"), project_.file().getParentDirectory(),
                                                    "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                           [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& fc)
                           {
                               if (safe == nullptr || fc.getResult() == juce::File())
                                   return;
                               juce::String error;
                               if (! safe->makeSamplerTrack (fc.getResult(), 0.0, std::nullopt, false, error) && error.isNotEmpty())
                                   safe->setStatus (error.replaceCharacter ('\n', ' '), true);
                           });
}

void MainComponent::showImportAudioDialog()
{
    if (! project_.loaded())
        return;
    chooser_ = std::make_unique<juce::FileChooser> (juce::String::fromUTF8 ("가져올 오디오 파일"), project_.file().getParentDirectory(),
                                                    "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                               | juce::FileBrowserComponent::canSelectMultipleItems,
                           [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& fc)
                           {
                               if (safe == nullptr || fc.getResults().isEmpty())
                                   return;
                               juce::StringArray files;
                               for (const auto& f : fc.getResults())
                                   files.add (f.getFullPathName());
                               const int t = safe->timeline_.selectedTrack();
                               std::optional<size_t> track;
                               if (t >= 0 && safe->arrangement_.isAudioTrack ((size_t) t))
                                   track = (size_t) t;
                               juce::String error;
                               if (! safe->importAudio (files, track, safe->project_.meters().floorToBar (safe->timeline_.playheadBeat() + 1.0e-9), error))
                                   safe->setStatus (error.replaceCharacter ('\n', ' '), true);
                           });
}

bool MainComponent::editClip (ArrangementPanel::ClipRef ref, const mr::AudioClip& edited, bool copy)
{
    if (! clipAt (ref))
        return false;
    const auto json = project_.trackJson (ref.track);
    const auto after = copy ? mr::withClipAdded (json, edited) : mr::withClip (json, ref.clip, edited);
    juce::String error;
    if (! project_.editTrack (ref.track, after, juce::String::fromUTF8 (copy ? "클립 복사" : "클립 고치기"), error))
    {
        setStatus (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    arrangement_.setSelectedClip (ArrangementPanel::ClipRef { ref.track, copy ? project_.composition().tracks[ref.track].clips.size() - 1 : ref.clip });
    return true;
}

bool MainComponent::deleteClip (ArrangementPanel::ClipRef ref)
{
    if (! clipAt (ref))
        return false;
    juce::String error;
    if (! project_.editTrack (ref.track, mr::withClipRemoved (project_.trackJson (ref.track), ref.clip), juce::String::fromUTF8 ("클립 지우기"), error))
        return false;
    arrangement_.setSelectedClip (std::nullopt);
    return true;
}

bool MainComponent::duplicateClip (ArrangementPanel::ClipRef ref)
{
    auto clip = clipAt (ref);
    if (! clip)
        return false;
    const auto folder = project_.file().getParentDirectory();
    clip->startBeat = std::round (project_.beatAtSeconds (mr::clipEndSeconds (*clip, project_.tempo(), folder)) * 1.0e6) / 1.0e6;
    return editClip (ref, *clip, true);
}

bool MainComponent::setClipMuted (ArrangementPanel::ClipRef ref, bool muted)
{
    auto clip = clipAt (ref);
    if (! clip || clip->muted == muted)
        return false;
    clip->muted = muted;
    return editClip (ref, *clip);
}

bool MainComponent::useTake (ArrangementPanel::ClipRef ref)
{
    const auto chosen = clipAt (ref);
    if (! chosen)
        return false;
    const auto folder = project_.file().getParentDirectory();
    const auto& tempo = project_.tempo();
    auto spanOf = [&] (const mr::AudioClip& c) { return std::make_pair (tempo.secondsAt (c.startBeat), mr::clipEndSeconds (c, tempo, folder)); };
    auto [s0, s1] = spanOf (*chosen);
    // 고른 구간이 이 클립과 겹치면 그 구간만
    if (const auto r = arrangement_.range(); r && r->firstTrack <= ref.track && ref.track <= r->lastTrack)
    {
        const double r0 = tempo.secondsAt (r->start), r1 = tempo.secondsAt (r->end);
        if (r1 > s0 && r0 < s1)
        {
            s0 = std::max (s0, r0);
            s1 = std::min (s1, r1);
        }
    }
    auto tidy = [] (double v) { return std::round (v * 1.0e6) / 1.0e6; };
    // 클립의 [a, b) 초 부분 (늘이기를 따라 파일의 자리를 센다)
    auto part = [&] (const mr::AudioClip& c, double a, double b)
    {
        auto p = c;
        const double from = mr::clipSourceAt (c, tempo, a), to = mr::clipSourceAt (c, tempo, b);
        p.startBeat = tidy (tempo.beatAt (a));
        p.offsetSeconds = tidy (from);
        p.lengthSeconds = tidy (to - from);
        if (a > tempo.secondsAt (c.startBeat) + 1.0e-6)
        {
            p.fadeInSeconds = 0.0;
            p.fadeInShape = mr::FadeShape::sine;
        }
        if (b < mr::clipEndSeconds (c, tempo, folder) - 1.0e-6)
        {
            p.fadeOutSeconds = 0.0;
            p.fadeOutShape = mr::FadeShape::sine;
        }
        return p;
    };
    const auto originals = project_.trackJson (ref.track).getProperty ("clips", juce::var());
    const auto& clips = project_.composition().tracks[ref.track].clips;
    juce::Array<juce::var> out;
    int changed = 0;
    for (size_t k = 0; k < clips.size(); ++k)
    {
        const auto& c = clips[k];
        const auto original = originals.isArray() && (int) k < originals.size() ? originals[(int) k] : juce::var();
        const auto [c0, c1] = spanOf (c);
        if (c1 <= s0 + 1.0e-6 || c0 >= s1 - 1.0e-6)
        {
            out.add (original.isVoid() ? mr::clipJson (c) : original);   // 겹치지 않는다
            continue;
        }
        const bool wanted = k == ref.clip;
        if (c0 >= s0 - 1.0e-6 && c1 <= s1 + 1.0e-6)
        {
            auto whole = c;
            whole.muted = ! wanted;
            changed += whole.muted != c.muted ? 1 : 0;
            out.add (mr::clipJson (whole, original));
            continue;
        }
        // 경계에서 나눈다: 앞, 안, 뒤
        if (c0 < s0)
            out.add (mr::clipJson (part (c, c0, s0), original));
        auto inside = part (c, std::max (c0, s0), std::min (c1, s1));
        inside.muted = ! wanted;
        out.add (mr::clipJson (inside, original));
        if (c1 > s1)
            out.add (mr::clipJson (part (c, s1, c1), original));
        ++changed;
    }
    if (changed == 0)
    {
        setStatus (juce::String::fromUTF8 ("이미 이 테이크가 들립니다."));
        return false;
    }
    auto json = project_.trackJson (ref.track);
    json.getDynamicObject()->setProperty ("clips", out);
    // 편집이 템포 지도를 새로 만들므로 문구는 먼저 만든다
    const auto done = juce::String::fromUTF8 ("테이크 고르기: ") + describeBeats (tempo.beatAt (s0), project_.meters()) + " ~ "
                      + describeBeats (tempo.beatAt (s1), project_.meters());
    juce::String error;
    if (! project_.editTrack (ref.track, json, juce::String::fromUTF8 ("테이크 고르기"), error))
    {
        setStatus (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    setStatus (done);
    return true;
}

bool MainComponent::splitClip (ArrangementPanel::ClipRef ref, double beat)
{
    const auto clip = clipAt (ref);
    if (! clip)
        return false;
    const auto folder = project_.file().getParentDirectory();
    const double startSec = project_.secondsAtBeat (clip->startBeat), endSec = mr::clipEndSeconds (*clip, project_.tempo(), folder);
    const double atSec = project_.secondsAtBeat (beat);
    if (atSec <= startSec + 0.001 || atSec >= endSec - 0.001)
    {
        setStatus (juce::String::fromUTF8 ("재생 위치가 클립 안에 있어야 나눕니다."), true);
        return false;
    }
    auto tidy = [] (double v) { return std::round (v * 1.0e6) / 1.0e6; };
    // 나누는 자리의 파일 시각 (늘인 클립은 늘이기를 따라). 설정이 켜져 있으면 가까운 영점 교차로
    double atSource = mr::clipSourceAt (*clip, project_.tempo(), atSec);
    if (arrangement_.zeroCrossingCuts)
    {
        const double snapped = mr::nearestZeroCrossing (*clip, folder, atSource);
        const double snappedSec = mr::clipSongSecondsAt (*clip, project_.tempo(), snapped);
        if (snappedSec > startSec + 0.001 && snappedSec < endSec - 0.001)
        {
            atSource = snapped;
            beat = project_.beatAtSeconds (snappedSec);
        }
    }
    const double endSource = clip->offsetSeconds + mr::clipLengthSeconds (*clip, folder);
    auto head = *clip, tail = *clip;
    head.lengthSeconds = tidy (atSource - clip->offsetSeconds);
    head.fadeOutSeconds = 0.0;
    head.fadeOutShape = mr::FadeShape::sine;
    tail.startBeat = tidy (beat);
    tail.offsetSeconds = tidy (atSource);
    tail.lengthSeconds = tidy (endSource - atSource);
    tail.fadeInSeconds = 0.0;
    tail.fadeInShape = mr::FadeShape::sine;
    auto json = mr::withClip (project_.trackJson (ref.track), ref.clip, head);
    json = mr::withClipAdded (json, tail);
    juce::String error;
    if (! project_.editTrack (ref.track, json, juce::String::fromUTF8 ("클립 나누기"), error))
        return false;
    arrangement_.setSelectedClip (ArrangementPanel::ClipRef { ref.track, project_.composition().tracks[ref.track].clips.size() - 1 });
    return true;
}

bool MainComponent::setClipGain (ArrangementPanel::ClipRef ref, double gainDb)
{
    auto clip = clipAt (ref);
    if (! clip)
        return false;
    clip->gainDb = std::round (std::clamp (gainDb, -96.0, 24.0) * 100.0) / 100.0;
    return editClip (ref, *clip);
}

bool MainComponent::normalizeClip (ArrangementPanel::ClipRef ref)
{
    const auto clip = clipAt (ref);
    if (! clip)
        return false;
    const auto peaks = mr::clipPeaks (mr::clipFile (*clip, project_.file().getParentDirectory()), clip->reverse);
    if (peaks == nullptr || peaks->maximum.empty())
        return false;
    // 클립이 트는 부분의 봉우리
    const double length = clip->lengthSeconds ? *clip->lengthSeconds : 1.0e9;
    const auto b0 = (size_t) (clip->offsetSeconds * peaks->sampleRate / peaks->bucketFrames);
    const auto b1 = (size_t) std::min ((double) peaks->maximum.size(), std::ceil ((clip->offsetSeconds + length) * peaks->sampleRate / peaks->bucketFrames));
    float peak = 0.0f;
    for (size_t b = b0; b < b1; ++b)
        peak = std::max ({ peak, peaks->maximum[b], -peaks->minimum[b] });
    if (peak <= 1.0e-6f)
        return false;
    return setClipGain (ref, -1.0 - juce::Decibels::gainToDecibels ((double) peak));
}

bool MainComponent::reverseClip (ArrangementPanel::ClipRef ref)
{
    const auto clip = clipAt (ref);
    if (! clip)
        return false;
    const auto info = mr::audioFileInfo (mr::clipFile (*clip, project_.file().getParentDirectory()));
    if (! info)
    {
        setStatus (juce::String::fromUTF8 ("파일을 읽지 못해 거꾸로 재생할 수 없습니다."), true);
        return false;
    }
    const auto reversed = mr::withClipReversed (*clip, info->seconds());
    if (! editClip (ref, reversed))
        return false;
    setStatus (reversed.reverse ? juce::String::fromUTF8 ("클립을 거꾸로 재생합니다.") : juce::String::fromUTF8 ("클립을 앞으로 재생합니다."));
    return true;
}

bool MainComponent::setClipFadeShape (ArrangementPanel::ClipRef ref, bool fadeIn, mr::FadeShape shape)
{
    auto clip = clipAt (ref);
    if (! clip)
        return false;
    auto& target = fadeIn ? clip->fadeInShape : clip->fadeOutShape;
    if (target == shape)
        return false;
    target = shape;
    return editClip (ref, *clip);
}

std::optional<ArrangementPanel::ClipRef> MainComponent::nextClip (ArrangementPanel::ClipRef ref) const
{
    if (! project_.loaded() || ref.track >= project_.trackCount())
        return std::nullopt;
    const auto& clips = project_.composition().tracks[ref.track].clips;
    if (ref.clip >= clips.size() || clips[ref.clip].muted)
        return std::nullopt;
    // 시작 순서로 바로 뒤의 들리는 클립 (같은 시작이면 번호 순서)
    std::optional<size_t> best;
    for (size_t k = 0; k < clips.size(); ++k)
    {
        if (k == ref.clip || clips[k].muted)
            continue;
        const bool after = clips[k].startBeat > clips[ref.clip].startBeat || (clips[k].startBeat == clips[ref.clip].startBeat && k > ref.clip);
        if (after && (! best || clips[k].startBeat < clips[*best].startBeat))
            best = k;
    }
    if (! best)
        return std::nullopt;
    return ArrangementPanel::ClipRef { ref.track, *best };
}

bool MainComponent::crossfadeClip (ArrangementPanel::ClipRef ref)
{
    const auto next = nextClip (ref);
    const auto a = clipAt (ref);
    const auto b = next ? clipAt (*next) : std::nullopt;
    if (! a || ! b)
    {
        setStatus (juce::String::fromUTF8 ("뒤에 이어지는 클립이 없습니다."), true);
        return false;
    }
    const auto pair = mr::crossfadedClips (*a, *b, project_.tempo(), project_.file().getParentDirectory(), 0.02);
    if (! pair)
    {
        setStatus (juce::String::fromUTF8 ("뒤 클립과 겹치거나 맞붙어 있어야 크로스페이드를 넣습니다."), true);
        return false;
    }
    auto json = mr::withClip (project_.trackJson (ref.track), ref.clip, pair->first);
    json = mr::withClip (json, next->clip, pair->second);
    juce::String error;
    if (! project_.editTrack (ref.track, json, juce::String::fromUTF8 ("크로스페이드"), error))
    {
        setStatus (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    const bool equalGain = pair->first.fadeOutShape == mr::FadeShape::sine;
    setStatus (juce::String::fromUTF8 ("크로스페이드: ") + juce::String (juce::roundToInt (pair->first.fadeOutSeconds * 1000.0)) + " ms, "
               + (equalGain ? juce::String::fromUTF8 ("같은 소리라 S자 (크기의 합이 1)") : juce::String::fromUTF8 ("다른 소리라 등전력 (힘의 합이 1)")));
    return true;
}

bool MainComponent::clearClipGainCurve (ArrangementPanel::ClipRef ref)
{
    auto clip = clipAt (ref);
    if (! clip || clip->gainCurve.empty())
        return false;
    clip->gainCurve.points.clear();
    return editClip (ref, *clip);
}

void MainComponent::setClipGainEditing (bool on)
{
    arrangement_.setGainCurveEditing (on);
    commands_.commandStatusChanged();
    setStatus (on ? juce::String::fromUTF8 ("클립 게인 곡선 편집을 켰습니다. 클립 몸통을 누르면 점을 더하고, 점을 끌어 옮기고, ⌥ 를 누른 채 누르면 지웁니다.")
                  : juce::String::fromUTF8 ("클립 게인 곡선 편집을 껐습니다."));
}

void MainComponent::setZeroCrossingCuts (bool on)
{
    arrangement_.zeroCrossingCuts = on;
    commands_.commandStatusChanged();
    saveSettings();
}

void MainComponent::showClipMenu (ArrangementPanel::ClipRef ref)
{
    const auto clip = clipAt (ref);
    if (! clip)
        return;
    juce::PopupMenu m;
    m.addSectionHeader (juce::File (juce::String::fromUTF8 (clip->file.c_str())).getFileName());
    juce::PopupMenu gain;
    for (const double db : { -12.0, -6.0, -3.0, 0.0, 3.0, 6.0 })
        gain.addItem ((db > 0 ? "+" : "") + juce::String (db, 0) + " dB", true, std::abs (clip->gainDb - db) < 0.005, [this, ref, db] { setClipGain (ref, db); });
    gain.addSeparator();
    gain.addItem (juce::String::fromUTF8 ("1 dB 올리기"), [this, ref, g = clip->gainDb] { setClipGain (ref, g + 1.0); });
    gain.addItem (juce::String::fromUTF8 ("1 dB 내리기"), [this, ref, g = clip->gainDb] { setClipGain (ref, g - 1.0); });
    m.addSubMenu (juce::String::fromUTF8 ("게인 (") + juce::String (clip->gainDb, 1) + " dB)", gain);
    m.addItem (juce::String::fromUTF8 ("정규화 (봉우리 -1 dBFS)"), [this, ref] { normalizeClip (ref); });
    m.addItem (juce::String::fromUTF8 ("뮤트"), true, clip->muted, [this, ref, muted = clip->muted] { setClipMuted (ref, ! muted); });
    {
        // 이 클립의 소리(파일의 그 구간, 뒤집은 클립은 거꾸로 돌린 사본)로 악기 만들기
        const auto source = mr::clipSourceFile (*clip, project_.file().getParentDirectory());
        const double start = clip->offsetSeconds;
        const auto length = mr::clipLengthSeconds (*clip, project_.file().getParentDirectory());
        juce::PopupMenu sampler;
        sampler.addItem (juce::String::fromUTF8 ("건반에 펼치기 (높이를 재서)"), [this, source, start, length]
        {
            juce::String error;
            if (! makeSamplerTrack (source, start, length, false, error) && error.isNotEmpty())
                setStatus (error, true);
        });
        sampler.addItem (juce::String::fromUTF8 ("원샷 (모든 건반이 같은 소리, 북과 효과음)"), [this, source, start, length]
        {
            juce::String error;
            if (! makeSamplerTrack (source, start, length, true, error) && error.isNotEmpty())
                setStatus (error, true);
        });
        m.addSubMenu (juce::String::fromUTF8 ("이 클립으로 악기 만들기"), sampler);
    }
    m.addItem (juce::String::fromUTF8 ("어택으로 조각내기 (새 악기와 트랙)"), [this, ref]
    {
        juce::String error;
        if (! sliceClip (ref, error) && error.isNotEmpty())
            setStatus (error, true);
    });
    m.addItem (juce::String::fromUTF8 ("곡 템포를 이 녹음에 맞추기 (박마다 템포)"), [this, ref]
    {
        juce::String error;
        if (! followClipTempo (ref, error) && error.isNotEmpty())
            setStatus (error, true);
    });
    m.addItem (juce::String::fromUTF8 ("템포 재기 (곡 템포를 따르게)"), mr::clipStretchAvailable(), false, [this, ref]
    {
        juce::String error;
        if (! detectClipTempo (ref, error) && error.isNotEmpty())
            setStatus (error, true);
    });
    {
        // 오디오 퀀타이즈: 격자마다 세기
        juce::PopupMenu quantize;
        const std::pair<double, const char*> grids[] = { { 1.0, "1/4" }, { 0.5, "1/8" }, { 0.25, "1/16" }, { 1.0 / 3.0, "1/8 셋잇단" }, { 1.0 / 6.0, "1/16 셋잇단" } };
        for (const auto& [grid, name] : grids)
        {
            juce::PopupMenu strengths;
            for (const double k : { 1.0, 0.75, 0.5, 0.25 })
                strengths.addItem (juce::String (juce::roundToInt (k * 100.0)) + "%", [this, ref, grid = grid, k]
                {
                    juce::String error;
                    if (! quantizeClipAudio (ref, grid, k, error) && error.isNotEmpty())
                        setStatus (error, true);
                });
            quantize.addSubMenu (juce::String::fromUTF8 (name), strengths, mr::clipStretchAvailable());
        }
        quantize.addSeparator();
        juce::PopupMenu byGroove;
        for (const auto& g : availableGrooves (pianoRoll_.grooveFolder))
        {
            juce::PopupMenu strengths;
            for (const double k : { 1.0, 0.5 })
                strengths.addItem (juce::String (juce::roundToInt (k * 100.0)) + "%", [this, ref, g, k]
                {
                    juce::String error;
                    if (! quantizeClipAudio (ref, g.grid, k, error, g) && error.isNotEmpty())
                        setStatus (error, true);
                });
            byGroove.addSubMenu (grooveName (g), strengths);
        }
        quantize.addSubMenu (juce::String::fromUTF8 ("그루브로"), byGroove);
        m.addSubMenu (juce::String::fromUTF8 ("어택을 격자에 맞추기 (워프 마커)"), quantize, mr::clipStretchAvailable());
        m.addItem (juce::String::fromUTF8 ("어택으로 그루브 저장..."), pianoRoll_.grooveFolder != juce::File(), false, [this, ref]
        {
            grooveDialog_ = std::make_unique<juce::AlertWindow> (juce::String::fromUTF8 ("그루브 저장"),
                                                                 juce::String::fromUTF8 ("이 클립의 어택이 16분음표 격자에서 어긋난 만큼과 크기를 한 마디 그루브로 저장합니다."),
                                                                 juce::MessageBoxIconType::NoIcon, this);
            grooveDialog_->addTextEditor ("name", juce::File (juce::String::fromUTF8 (clipAt (ref)->file.c_str())).getFileNameWithoutExtension());
            grooveDialog_->addButton (juce::String::fromUTF8 ("저장"), 1, juce::KeyPress (juce::KeyPress::returnKey));
            grooveDialog_->addButton (juce::String::fromUTF8 ("취소"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            grooveDialog_->enterModalState (true, juce::ModalCallbackFunction::create (
                [safe = juce::Component::SafePointer<MainComponent> (this), ref] (int result)
                {
                    if (safe == nullptr || safe->grooveDialog_ == nullptr)
                        return;
                    const auto name = safe->grooveDialog_->getTextEditorContents ("name");
                    safe->grooveDialog_.reset();
                    juce::String error;
                    if (result != 1)
                        return;
                    if (safe->saveGrooveFromClip (ref, name, error))
                        safe->setStatus (juce::String::fromUTF8 ("그루브를 저장했습니다: ") + name);
                    else if (error.isNotEmpty())
                        safe->setStatus (error, true);
                }));
        });
        juce::PopupMenu mode;
        mode.addItem (juce::String::fromUTF8 ("늘이기 (마커 사이를 늘이고 줄인다, 음높이 그대로)"), true, ! clip->warpSlices, [this, ref, c = *clip]
        {
            auto edited = c;
            edited.warpSlices = false;
            editClip (ref, edited);
        });
        mode.addItem (juce::String::fromUTF8 ("자르기 (마커마다 잘라 그 자리에서 튼다, 타악기의 어택 그대로)"), true, clip->warpSlices, [this, ref, c = *clip]
        {
            auto edited = c;
            edited.warpSlices = true;
            edited.pitchSemitones = 0.0;   // 조각은 파일의 높이로 튼다
            edited.pitchCorrection.reset();
            edited.pitchEdits.clear();
            editClip (ref, edited);
        });
        m.addSubMenu (juce::String::fromUTF8 ("워프 방식 (") + juce::String::fromUTF8 (clip->warpSlices ? "자르기" : "늘이기") + ")", mode);
        m.addItem (juce::String::fromUTF8 ("워프 마커 지우기 (") + juce::String ((int) clip->warp.size()) + juce::String::fromUTF8 ("개)"), ! clip->warp.empty(), false,
                   [this, ref, c = *clip]
                   {
                       auto plain = c;
                       plain.warp.clear();
                       editClip (ref, plain);
                   });
    }
    m.addItem (juce::String::fromUTF8 ("멜로디를 음표로 (새 트랙)"), [this, ref]
    {
        juce::String error;
        if (! transcribeClip (ref, error) && error.isNotEmpty())
            setStatus (error, true);
    });
    // 테이크: 이 클립과 겹치는 클립들 (겹쳐 녹음한 반복)
    {
        const auto folder = project_.file().getParentDirectory();
        const auto& tempo = project_.tempo();
        const auto& clips = project_.composition().tracks[ref.track].clips;
        const double c0 = tempo.secondsAt (clip->startBeat), c1 = mr::clipEndSeconds (*clip, tempo, folder);
        juce::PopupMenu takes;
        int number = 0;
        for (size_t k = 0; k < clips.size(); ++k)
        {
            const double k0 = tempo.secondsAt (clips[k].startBeat), k1 = mr::clipEndSeconds (clips[k], tempo, folder);
            if (k1 <= c0 + 1.0e-6 || k0 >= c1 - 1.0e-6)
                continue;
            ++number;
            const auto label = juce::String::fromUTF8 ("테이크 ") + juce::String (number) + ": "
                               + juce::File (juce::String::fromUTF8 (clips[k].file.c_str())).getFileNameWithoutExtension() + " "
                               + juce::String ((int) clips[k].offsetSeconds / 60) + ":" + juce::String ((int) clips[k].offsetSeconds % 60).paddedLeft ('0', 2)
                               + (k == ref.clip ? juce::String::fromUTF8 (" (이 클립)") : juce::String());
            takes.addItem (label, true, ! clips[k].muted, [this, track = ref.track, k] { useTake (ArrangementPanel::ClipRef { track, k }); });
        }
        if (number > 1)
            m.addSubMenu (juce::String::fromUTF8 ("테이크 (고르면 그 자리에서 들린다, 고른 구간이 있으면 그 구간만)"), takes);
    }
    // 늘이기와 음높이
    {
        const bool can = mr::clipStretchAvailable();
        auto withClip = [this, ref, c = *clip] (std::function<void (mr::AudioClip&)> change)
        {
            return [this, ref, c, change]
            {
                auto edited = c;
                change (edited);
                editClip (ref, edited);
            };
        };
        juce::PopupMenu pitch;
        for (const double st : { -12.0, -7.0, -5.0, -2.0, -1.0, 0.0, 1.0, 2.0, 5.0, 7.0, 12.0 })
            pitch.addItem ((st > 0 ? "+" : "") + juce::String (st, 0) + juce::String::fromUTF8 (" 반음"), can, std::abs (clip->pitchSemitones - st) < 1.0e-9,
                           withClip ([st] (mr::AudioClip& c) { c.pitchSemitones = st; }));
        m.addSubMenu (juce::String::fromUTF8 ("음높이 (") + (clip->pitchSemitones > 0 ? "+" : "") + juce::String (clip->pitchSemitones, 0)
                          + juce::String::fromUTF8 (" 반음)"), pitch, can && ! clip->warpSlices);
        juce::PopupMenu stretch;
        const double bpmHere = project_.tempo().bpmAt (clip->startBeat);
        stretch.addItem (juce::String::fromUTF8 ("곡의 템포 따르기 (파일의 템포를 지금 곡의 ") + juce::String (bpmHere, 1) + " BPM)", can && ! clip->sourceBpm, false,
                         withClip ([bpmHere] (mr::AudioClip& c) { c.sourceBpm = bpmHere; c.stretch = 1.0; }));
        if (clip->sourceBpm)
            stretch.addItem (juce::String::fromUTF8 ("템포 따르기 끄기 (파일의 템포 ") + juce::String (*clip->sourceBpm, 1) + " BPM)", true, true,
                             withClip ([] (mr::AudioClip& c) { c.sourceBpm.reset(); }));
        stretch.addSeparator();
        for (const double k : { 0.5, 0.75, 1.0, 1.5, 2.0 })
            stretch.addItem (juce::String::fromUTF8 ("길이 ") + juce::String (k, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + juce::String::fromUTF8 ("배"),
                             can && ! clip->sourceBpm && clip->warp.empty(), ! clip->sourceBpm && clip->warp.empty() && std::abs (clip->stretch - k) < 1.0e-9,
                             withClip ([k] (mr::AudioClip& c) { c.stretch = k; }));
        m.addSubMenu (juce::String::fromUTF8 ("늘이기") + (! clip->warp.empty() ? juce::String::fromUTF8 (" (워프 마커)")
                                                       : clip->sourceBpm ? juce::String::fromUTF8 (" (곡의 템포를 따른다)")
                                                       : clip->stretch != 1.0 ? " (" + juce::String (clip->stretch, 2) + juce::String::fromUTF8 ("배)") : juce::String()),
                      stretch, can);
        // 음높이 교정: 가까운 반음이나 곡의 조의 음으로, 따라가는 빠르기와 세기
        {
            const auto& pc = clip->pitchCorrection;
            const auto& song = project_.composition();
            std::vector<size_t> all;
            size_t noteCount = 0;
            for (size_t t = 0; t < song.tracks.size(); ++t)
            {
                all.push_back (t);
                noteCount += song.tracks[t].notes.size();
            }
            bool estimated = false;
            const auto key = mr::scoreKey (song, all, &estimated);
            const bool knowsKey = ! estimated || noteCount > 0;
            const std::string keyText = mr::keyName (key);
            auto setTo = [withClip] (std::string keyName, double speedMs)
            {
                return withClip ([keyName, speedMs] (mr::AudioClip& c)
                {
                    auto next = c.pitchCorrection.value_or (mr::PitchCorrection {});
                    next.speedMs = speedMs;
                    next.key = keyName;
                    next.scale.clear();
                    next.root.clear();
                    c.pitchCorrection = next;
                    c.warpSlices = false;
                });
            };
            const std::pair<const char*, double> speeds[] = { { "빠르게 (0 ms, 비브라토도 편다)", 0.0 }, { "보통 (30 ms)", 30.0 },
                                                              { "자연스럽게 (80 ms, 비브라토와 꾸밈이 남는다)", 80.0 } };
            juce::PopupMenu tune, chromatic, inKey, strength;
            tune.addItem (juce::String::fromUTF8 ("끄기"), pc.has_value(), ! pc.has_value(), withClip ([] (mr::AudioClip& c) { c.pitchCorrection.reset(); }));
            for (const auto& [name, ms] : speeds)
            {
                chromatic.addItem (juce::String::fromUTF8 (name), true, pc && pc->key.empty() && pc->scale.empty() && pc->speedMs == ms, setTo ({}, ms));
                inKey.addItem (juce::String::fromUTF8 (name), knowsKey, pc && pc->key == keyText && pc->speedMs == ms, setTo (keyText, ms));
            }
            tune.addSubMenu (juce::String::fromUTF8 ("가까운 반음으로"), chromatic);
            tune.addSubMenu (juce::String::fromUTF8 (knowsKey ? (keyText + (estimated ? " 음으로 (음표로 추정한 조)" : " 음으로 (곡의 조)")).c_str()
                                                              : "곡의 조 음으로 (조가 정해지지 않았습니다)"),
                             inKey, knowsKey);
            for (const double k : { 1.0, 0.75, 0.5, 0.25 })
                strength.addItem (juce::String (juce::roundToInt (k * 100.0)) + "%", pc.has_value(), pc && std::abs (pc->strength - k) < 1.0e-9,
                                  withClip ([k] (mr::AudioClip& c) { if (c.pitchCorrection) c.pitchCorrection->strength = k; }));
            tune.addSubMenu (juce::String::fromUTF8 ("세기"), strength, pc.has_value());
            tune.addItem (juce::String::fromUTF8 ("포먼트 지키기 (목소리의 음색)"), pc.has_value(), pc && pc->formant,
                          withClip ([] (mr::AudioClip& c) { if (c.pitchCorrection) c.pitchCorrection->formant = ! c.pitchCorrection->formant; }));
            juce::String label = juce::String::fromUTF8 ("음높이 교정");
            if (pc)
                label << " (" << (pc->key.empty() && pc->scale.empty() ? juce::String::fromUTF8 ("반음") : juce::String::fromUTF8 ((pc->key.empty() ? pc->root + " " + pc->scale : pc->key).c_str()))
                      << ", " << juce::String (juce::roundToInt (pc->speedMs)) << " ms, " << juce::String (juce::roundToInt (pc->strength * 100.0)) << "%)";
            m.addSubMenu (label, tune, can && ! clip->warpSlices);
        }
        if (! can)
            m.addItem (juce::String::fromUTF8 ("이 빌드에는 늘이기와 음높이 바꾸기가 없습니다 (./build.sh 로 Signalsmith Stretch 를 받습니다)"), false, false, nullptr);
    }
    m.addSeparator();
    m.addItem (juce::String::fromUTF8 ("재생 위치에서 나누기"), [this, ref] { splitClip (ref, timeline_.playheadBeat()); });
    m.addItem (juce::String::fromUTF8 ("바로 뒤에 복제"), [this, ref] { duplicateClip (ref); });
    m.addItem (juce::String::fromUTF8 ("거꾸로 재생"), true, clip->reverse, [this, ref] { reverseClip (ref); });
    m.addSeparator();
    {
        // 페이드의 모양 (들어오기, 나가기)
        const std::pair<mr::FadeShape, const char*> shapes[] = { { mr::FadeShape::sine, "S자 (기본, 맞선 두 페이드의 크기 합이 1)" },
                                                                 { mr::FadeShape::linear, "직선" },
                                                                 { mr::FadeShape::equalPower, "등전력 (맞선 두 페이드의 힘 합이 1)" },
                                                                 { mr::FadeShape::exponential, "지수 (데시벨로 곧게)" } };
        juce::PopupMenu inShape, outShape;
        for (const auto& [shape, name] : shapes)
        {
            inShape.addItem (juce::String::fromUTF8 (name), true, clip->fadeInShape == shape, [this, ref, shape = shape] { setClipFadeShape (ref, true, shape); });
            outShape.addItem (juce::String::fromUTF8 (name), true, clip->fadeOutShape == shape, [this, ref, shape = shape] { setClipFadeShape (ref, false, shape); });
        }
        m.addSubMenu (juce::String::fromUTF8 ("페이드인 모양"), inShape);
        m.addSubMenu (juce::String::fromUTF8 ("페이드아웃 모양"), outShape);
    }
    m.addItem (juce::String::fromUTF8 ("페이드 지우기"), clip->fadeInSeconds > 0.0 || clip->fadeOutSeconds > 0.0, false, [this, ref, c = *clip]
    {
        auto plain = c;
        plain.fadeInSeconds = plain.fadeOutSeconds = 0.0;
        plain.fadeInShape = plain.fadeOutShape = mr::FadeShape::sine;
        editClip (ref, plain);
    });
    {
        const auto next = nextClip (ref);
        const auto b = next ? clipAt (*next) : std::nullopt;
        const bool can = b && mr::crossfadedClips (*clip, *b, project_.tempo(), project_.file().getParentDirectory(), 0.02).has_value();
        m.addItem (juce::String::fromUTF8 ("뒤 클립과 크로스페이드"), can, false, [this, ref] { crossfadeClip (ref); });
    }
    m.addItem (juce::String::fromUTF8 ("게인 곡선 편집"), true, arrangement_.gainCurveEditing(), [this] { setClipGainEditing (! arrangement_.gainCurveEditing()); });
    m.addItem (juce::String::fromUTF8 ("게인 곡선 지우기"), ! clip->gainCurve.empty(), false, [this, ref] { clearClipGainCurve (ref); });
    m.addSeparator();
    m.addItem (juce::String::fromUTF8 ("Finder 에서 보기"), [file = mr::clipFile (*clip, project_.file().getParentDirectory())] { file.revealToUser(); });
    m.addItem (juce::String::fromUTF8 ("지우기"), [this, ref] { deleteClip (ref); });
    m.showMenuAsync (juce::PopupMenu::Options());
}

// ---- 마커 ----

juce::String MainComponent::describeBeats (double beat, const mr::MeterMap& meters)
{
    // 마디.박 (1부터)
    const int bar = meters.barAt (beat);
    const double inBar = beat - meters.barStart (bar);
    if (std::abs (inBar) < 1.0e-6)
        return juce::String (bar + 1) + juce::String::fromUTF8 ("마디");
    return juce::String (bar + 1) + "." + juce::String (inBar + 1.0, std::abs (inBar - std::round (inBar)) < 1.0e-6 ? 0 : 2);
}

bool MainComponent::setMeterAt (double beat, std::optional<std::pair<int, int>> meter)
{
    if (! project_.loaded())
        return false;
    const auto& meters = project_.meters();
    const double at = meters.floorToBar (std::max (0.0, beat) + 1.0e-9);
    juce::String error;
    const auto changes = project_.property ("meterChanges");
    juce::Array<juce::var> list;
    if (const auto* arr = changes.getArray())
        for (const auto& c : *arr)
            if (std::abs ((double) c.getProperty ("beat", 0.0) - at) > 1.0e-6)
                list.add (c);
    if (at <= 0.0)
    {
        if (! meter)
            return false;
        return project_.editTempo (project_.property ("tempo"), mr::timeSignatureJson (meter->first, meter->second),
                                   juce::String::fromUTF8 ("박자: ") + juce::String (meter->first) + "/" + juce::String (meter->second), error);
    }
    if (meter)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("beat", at);
        o->setProperty ("timeSignature", mr::timeSignatureJson (meter->first, meter->second));
        list.add (juce::var (o));
        std::stable_sort (list.begin(), list.end(), [] (const juce::var& a, const juce::var& b)
                          { return (double) a.getProperty ("beat", 0.0) < (double) b.getProperty ("beat", 0.0); });
    }
    const auto name = meter ? juce::String::fromUTF8 ("박자 바꾸기: ") + describeBeats (at, meters) + juce::String::fromUTF8 ("부터 ")
                                  + juce::String (meter->first) + "/" + juce::String (meter->second)
                            : juce::String::fromUTF8 ("박자 바꾸기 지우기: ") + describeBeats (at, meters);
    if (! project_.edit ({ Project::Change { -1, "meterChanges", list.isEmpty() ? juce::var() : juce::var (list) } }, name, error))
    {
        setStatus (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    setStatus (name);
    return true;
}

// ---- 스텝 입력 ----

void MainComponent::setStepInput (bool on)
{
    stepInput_ = on && project_.loaded();
    stepHeld_.clear();
    stepChord_.clear();
    steps_.clear();
    if (stepInput_)
    {
        const double grid = std::max (1.0e-3, pianoRoll_.grid());
        moveStepCursor (std::round (timeline_.playheadBeat() / grid) * grid);
        setStatus (juce::String::fromUTF8 ("스텝 입력을 켰습니다. 멈춘 동안 친 음이 ") + describeBeats (stepCursor_, project_.meters())
                   + juce::String::fromUTF8 ("부터 격자 길이로 들어갑니다. → 쉼표, ← 물러서기, 백스페이스 마지막 칸 지우기"));
    }
    else
        setStatus (juce::String::fromUTF8 ("스텝 입력을 껐습니다."));
    commands_.commandStatusChanged();
}

double MainComponent::stepCursor() const
{
    // 사람이 재생 위치를 옮겼으면 그 자리 (격자에 맞춰)
    if (std::abs (timeline_.playheadBeat() - stepShown_) > 1.0e-6)
    {
        const double grid = std::max (1.0e-3, pianoRoll_.grid());
        return std::max (0.0, std::round (timeline_.playheadBeat() / grid) * grid);
    }
    return stepCursor_;
}

void MainComponent::moveStepCursor (double beat)
{
    stepCursor_ = std::max (0.0, beat);
    seekToBeat (stepCursor_);
    stepShown_ = timeline_.playheadBeat();
}

void MainComponent::stepNote (int pitch, int velocity, bool on)
{
    if (! stepInput_ || recording_ || ! project_.loaded())
        return;
    const int t = timeline_.selectedTrack();
    if (t < 0 || project_.composition().tracks[(size_t) t].instrument == "audio")
    {
        if (on)
            setStatus (juce::String::fromUTF8 ("스텝 입력은 음표 트랙에 넣습니다. 트랙을 고르십시오."), true);
        return;
    }
    if (auto p = player(); p != nullptr && p->isPlaying())
        return;   // 재생 중에는 연주만 한다
    if (on)
    {
        if (stepHeld_.empty() && stepChord_.empty())
            stepCursor_ = stepCursor();
        stepHeld_.insert (pitch);
        if (std::none_of (stepChord_.begin(), stepChord_.end(), [pitch] (const auto& c) { return c.first == pitch; }))
            stepChord_.push_back ({ pitch, juce::jlimit (1, 127, velocity) });
        return;
    }
    stepHeld_.erase (pitch);
    if (! stepHeld_.empty() || stepChord_.empty())
        return;
    // 모두 뗐다: 이번 칸의 음을 넣는다
    const double length = std::max (1.0e-3, pianoRoll_.grid());
    std::vector<mr::Note> added;
    Step step { (size_t) t, stepCursor_, length, {} };
    for (const auto& [p, v] : stepChord_)
    {
        mr::Note n;
        n.pitch = p;
        n.startBeat = stepCursor_;
        n.durationBeat = length;
        n.velocity = v;
        added.push_back (n);
        step.pitches.push_back (p);
    }
    stepChord_.clear();
    const auto result = mr::addNotes (project_.editNotes ((size_t) t), added);
    juce::String error;
    const auto name = juce::String::fromUTF8 ("스텝 입력: ") + describeBeats (stepCursor_, project_.meters());
    if (! project_.editTrack ((size_t) t, mr::withNotes (project_.trackJson ((size_t) t), result.notes), name, error))
    {
        setStatus (error.replaceCharacter ('\n', ' '), true);
        return;
    }
    steps_.push_back (step);
    moveStepCursor (stepCursor_ + length);
    setStatus (name + juce::String::fromUTF8 (", 다음 칸 ") + describeBeats (stepCursor_, project_.meters()));
}

bool MainComponent::stepRest()
{
    if (! stepInput_ || ! project_.loaded())
        return false;
    const double length = std::max (1.0e-3, pianoRoll_.grid());
    const double at = stepCursor();
    steps_.push_back ({ (size_t) std::max (0, timeline_.selectedTrack()), at, length, {} });
    moveStepCursor (at + length);
    setStatus (juce::String::fromUTF8 ("스텝 입력: 쉼표, 다음 칸 ") + describeBeats (stepCursor_, project_.meters()));
    return true;
}

bool MainComponent::stepBack()
{
    if (! stepInput_ || ! project_.loaded())
        return false;
    const double length = std::max (1.0e-3, pianoRoll_.grid());
    moveStepCursor (std::max (0.0, stepCursor() - length));
    setStatus (juce::String::fromUTF8 ("스텝 입력: ") + describeBeats (stepCursor_, project_.meters()));
    return true;
}

bool MainComponent::stepDelete()
{
    if (! stepInput_ || steps_.empty() || ! project_.loaded())
        return false;
    const auto step = steps_.back();
    steps_.pop_back();
    if (! step.pitches.empty() && step.track < project_.trackCount())
    {
        // 그 칸에 넣은 음 (같은 자리, 높이, 길이)을 지운다
        const auto notes = project_.editNotes (step.track);
        mr::NoteSelection remove;
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto& n = notes[i].note;
            if (std::abs (n.startBeat - step.beat) < 1.0e-6 && std::abs (n.durationBeat - step.length) < 1.0e-6
                && std::find (step.pitches.begin(), step.pitches.end(), n.pitch) != step.pitches.end())
                remove.push_back (i);
        }
        juce::String error;
        if (! remove.empty())
            project_.editTrack (step.track, mr::withNotes (project_.trackJson (step.track), mr::deleteNotes (notes, remove).notes),
                                juce::String::fromUTF8 ("스텝 입력 지우기: ") + describeBeats (step.beat, project_.meters()), error);
    }
    moveStepCursor (step.beat);
    setStatus (juce::String::fromUTF8 ("스텝 입력: ") + describeBeats (stepCursor_, project_.meters()));
    return true;
}

juce::PopupMenu MainComponent::historyMenu()
{
    juce::PopupMenu m;
    if (! project_.loaded())
        return m;
    auto& undo = project_.undoManager();
    const auto past = undo.getUndoDescriptions();     // 가장 최근 것부터
    const auto future = undo.getRedoDescriptions();   // 바로 다음 것부터
    constexpr int shown = 60;
    juce::Component::SafePointer<MainComponent> safe (this);
    auto jump = [safe] (int steps) { return [safe, steps] { if (safe != nullptr) safe->jumpHistory (steps); }; };
    if (past.size() > shown)
        m.addItem (juce::String::fromUTF8 ("앞의 기록 ") + juce::String (past.size() - shown) + juce::String::fromUTF8 ("개는 줄였습니다"), false, false, nullptr);
    m.addItem (juce::String::fromUTF8 ("처음 상태 (불러오거나 기록을 비운 때)"), past.size() <= shown, past.isEmpty(), jump (-past.size()));
    for (int i = std::min (shown, past.size()) - 1; i >= 0; --i)
        m.addItem (past[i], true, i == 0, jump (-i));
    if (! future.isEmpty())
    {
        m.addSeparator();
        m.addSectionHeader (juce::String::fromUTF8 ("다시 실행할 수 있는 편집"));
        for (int i = 0; i < std::min (shown, future.size()); ++i)
            m.addItem (future[i], true, false, jump (i + 1));
    }
    return m;
}

bool MainComponent::jumpHistory (int steps)
{
    if (steps == 0 || recording_)
        return false;
    if (! project_.jumpHistory (steps))
        return false;
    auto& undo = project_.undoManager();
    setStatus (juce::String::fromUTF8 ("실행 취소 기록: ") + (undo.canUndo() ? undo.getUndoDescription() : juce::String::fromUTF8 ("처음 상태")));
    commands_.commandStatusChanged();
    return true;
}

bool MainComponent::editMarkers (const juce::var& markers, const juce::String& name)
{
    juce::String error;
    if (! project_.edit ({ Project::Change { -1, "markers", markers } }, name, error))
    {
        if (error.isNotEmpty())
            setStatus (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    setStatus (name);
    return true;
}

bool MainComponent::addMarker (double beat, const juce::String& name, bool rename)
{
    if (! project_.loaded())
        return false;
    auto text = name;
    if (text.isEmpty())
        text = juce::String::fromUTF8 ("마커 ") + juce::String ((int) project_.markers().size() + 1);
    size_t index = 0;
    const auto markers = mr::withMarkerAdded (project_.documentRoot(), beat, text.toStdString(), &index);
    if (! editMarkers (markers, juce::String::fromUTF8 ("마커 더하기: ") + text))
        return false;
    if (rename)
        arrangement_.beginMarkerRename (index);
    return true;
}

bool MainComponent::moveMarker (size_t marker, double beat)
{
    if (! project_.loaded() || marker >= project_.markers().size())
        return false;
    const auto name = juce::String::fromUTF8 (project_.markers()[marker].name.c_str());
    return editMarkers (mr::withMarkerMoved (project_.documentRoot(), marker, beat), juce::String::fromUTF8 ("마커 옮기기: ") + name);
}

bool MainComponent::renameMarker (size_t marker, const juce::String& name)
{
    if (! project_.loaded() || marker >= project_.markers().size() || name.trim().isEmpty())
        return false;
    if (juce::String::fromUTF8 (project_.markers()[marker].name.c_str()) == name.trim())
        return false;
    return editMarkers (mr::withMarkerRenamed (project_.documentRoot(), marker, name.trim().toStdString()),
                        juce::String::fromUTF8 ("마커 이름: ") + name.trim());
}

bool MainComponent::deleteMarker (size_t marker)
{
    if (! project_.loaded() || marker >= project_.markers().size())
        return false;
    const auto name = juce::String::fromUTF8 (project_.markers()[marker].name.c_str());
    return editMarkers (mr::withMarkerRemoved (project_.documentRoot(), marker), juce::String::fromUTF8 ("마커 지우기: ") + name);
}

namespace
{
juce::String sectionName (const mr::Section& s) { return juce::String::fromUTF8 (s.name.c_str()); }
}

bool MainComponent::moveSection (size_t section, size_t before)
{
    if (! project_.loaded() || recording_)
        return false;
    const auto folder = project_.file().getParentDirectory();
    const auto sections = mr::sectionsOf (project_.documentRoot(), folder);
    if (section >= sections.size())
        return false;
    std::string reason;
    const auto root = mr::withSectionMoved (project_.documentRoot(), section, before, &reason, folder);
    if (root.isVoid())
    {
        setStatus (juce::String::fromUTF8 ("구간을 옮기지 못했습니다: ") + juce::String::fromUTF8 (reason.c_str()), true);
        return false;
    }
    juce::String error;
    const auto name = juce::String::fromUTF8 ("구간 옮기기: ") + sectionName (sections[section]);
    if (! project_.editRoot (root, name, error))
    {
        if (error.isNotEmpty())
            setStatus (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    arrangement_.setRange (std::nullopt);
    setStatus (name);
    return true;
}

bool MainComponent::duplicateSection (size_t section)
{
    if (! project_.loaded() || recording_)
        return false;
    const auto folder = project_.file().getParentDirectory();
    const auto sections = mr::sectionsOf (project_.documentRoot(), folder);
    if (section >= sections.size())
        return false;
    std::string reason;
    const auto root = mr::withSectionDuplicated (project_.documentRoot(), section, &reason, folder);
    if (root.isVoid())
    {
        setStatus (juce::String::fromUTF8 ("구간을 복제하지 못했습니다: ") + juce::String::fromUTF8 (reason.c_str()), true);
        return false;
    }
    juce::String error;
    const auto name = juce::String::fromUTF8 ("구간 복제: ") + sectionName (sections[section]);
    if (! project_.editRoot (root, name, error))
    {
        if (error.isNotEmpty())
            setStatus (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    arrangement_.setRange (std::nullopt);
    setStatus (name);
    return true;
}

bool MainComponent::deleteSection (size_t section)
{
    if (! project_.loaded() || recording_)
        return false;
    const auto folder = project_.file().getParentDirectory();
    const auto sections = mr::sectionsOf (project_.documentRoot(), folder);
    if (section >= sections.size())
        return false;
    std::string reason;
    const auto root = mr::withSectionDeleted (project_.documentRoot(), section, &reason, folder);
    if (root.isVoid())
    {
        setStatus (juce::String::fromUTF8 ("구간을 지우지 못했습니다: ") + juce::String::fromUTF8 (reason.c_str()), true);
        return false;
    }
    juce::String error;
    const auto name = juce::String::fromUTF8 ("구간 지우기: ") + sectionName (sections[section]);
    if (! project_.editRoot (root, name, error))
    {
        if (error.isNotEmpty())
            setStatus (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    arrangement_.setRange (std::nullopt);
    setStatus (name);
    return true;
}

void MainComponent::showSectionMenu (size_t section)
{
    const auto& sections = arrangement_.sections();
    if (! project_.loaded() || section >= sections.size())
        return;
    const auto sec = sections[section];
    juce::PopupMenu m;
    m.addSectionHeader (sectionName (sec) + " (" + describeBeats (sec.start, project_.meters()) + " ~ " + describeBeats (sec.end, project_.meters()) + ")");
    m.addItem (juce::String::fromUTF8 ("여기서 재생"), [this, start = sec.start]
    {
        seekToBeat (start);
        if (auto p = player(); p != nullptr && ! p->isPlaying())
            togglePlay();
    });
    m.addItem (juce::String::fromUTF8 ("모든 트랙의 이 구간 고르기"), project_.trackCount() > 0, false, [this, sec]
    {
        arrangement_.setRange (ArrangementPanel::Range { 0, project_.trackCount() - 1, sec.start, sec.end });
    });
    m.addSeparator();
    m.addItem (juce::String::fromUTF8 ("바로 뒤에 복제"), ! recording_, false, [this, section] { duplicateSection (section); });
    m.addItem (juce::String::fromUTF8 ("앞으로 옮기기"), ! recording_ && section > 0, false, [this, section] { moveSection (section, section - 1); });
    m.addItem (juce::String::fromUTF8 ("뒤로 옮기기"), ! recording_ && section + 1 < sections.size(), false, [this, section] { moveSection (section, section + 2); });
    m.addItem (juce::String::fromUTF8 ("지우기 (뒤를 당긴다)"), ! recording_ && sections.size() > 1, false, [this, section] { deleteSection (section); });
    m.showMenuAsync (juce::PopupMenu::Options());
}

bool MainComponent::goToMarker (int direction)
{
    if (! project_.loaded() || project_.markers().empty())
        return false;
    const double now = currentBeat();
    std::optional<double> target;
    for (const auto& m : project_.markers())
    {
        if (direction > 0 && m.beat > now + 1.0e-6 && ! target)
            target = m.beat;
        if (direction < 0 && m.beat < now - 1.0e-6)
            target = m.beat;   // 앞의 마지막
    }
    if (! target)
        return false;
    seekToBeat (*target);
    return true;
}

void MainComponent::showMarkerMenu (int marker, double beat)
{
    if (! project_.loaded())
        return;
    juce::PopupMenu m;
    if (marker >= 0 && (size_t) marker < project_.markers().size())
    {
        const auto index = (size_t) marker;
        m.addSectionHeader (juce::String::fromUTF8 (project_.markers()[index].name.c_str()));
        m.addItem (juce::String::fromUTF8 ("이름 바꾸기"), [this, index] { arrangement_.beginMarkerRename (index); });
        m.addItem (juce::String::fromUTF8 ("여기로 옮기기 (재생 위치)"), [this, index] { moveMarker (index, timeline_.playheadBeat()); });
        m.addItem (juce::String::fromUTF8 ("여기서 재생"), [this, beat]
        {
            seekToBeat (beat);
            if (auto p = player(); p != nullptr && ! p->isPlaying())
                togglePlay();
        });
        m.addSeparator();
        m.addItem (juce::String::fromUTF8 ("지우기"), [this, index] { deleteMarker (index); });
    }
    else
    {
        const auto& bar = project_.meters();
        const double at = bar.roundToBar (std::max (0.0, beat));
        m.addItem (juce::String::fromUTF8 ("마커 더하기 (") + describeBeats (at, bar) + ")", [this, at] { addMarker (at, {}, true); });
    }
    m.showMenuAsync (juce::PopupMenu::Options());
}

// ---- 구간 ----

bool MainComponent::copyRange (bool cut)
{
    const auto r = arrangement_.range();
    if (! project_.loaded() || ! r)
        return false;
    rangeClip_ = mr::copyRange (project_.documentRoot(), r->tracks(), r->start, r->end, project_.file().getParentDirectory());
    clipboard_ = Clipboard::range;
    const auto count = juce::String ((int) rangeClip_->noteCount());
    if (! cut)
    {
        setStatus (juce::String::fromUTF8 ("구간을 복사했습니다 (음표 ") + count + juce::String::fromUTF8 ("개)"));
        return true;
    }
    return editRange (mr::withRangeCleared (project_.documentRoot(), r->tracks(), r->start, r->end, project_.file().getParentDirectory()),
                      juce::String::fromUTF8 ("구간 잘라내기 (음표 ") + count + juce::String::fromUTF8 ("개)"), r);
}

bool MainComponent::pasteRange()
{
    if (! project_.loaded() || ! rangeClip_ || rangeClip_->empty())
        return false;
    const auto r = arrangement_.range();
    const double snap = arrangement_.rangeSnap();
    const double at = r ? r->start : std::max (0.0, std::floor (timeline_.playheadBeat() / snap + 1.0e-9) * snap);
    const size_t first = r ? r->firstTrack : (size_t) std::max (0, timeline_.selectedTrack());
    return pasteClipAt (*rangeClip_, first, at, juce::String::fromUTF8 ("구간 붙이기"));
}

bool MainComponent::pasteClipAt (const mr::ArrangeClip& clip, size_t firstTrack, double at, const juce::String& name)
{
    const size_t count = project_.trackCount();
    if (firstTrack >= count)
        return false;
    const size_t last = std::min (count - 1, firstTrack + clip.tracks.size() - 1);
    ArrangementPanel::Range pasted { firstTrack, last, at, at + clip.lengthBeats };
    return editRange (mr::withClipPasted (project_.documentRoot(), clip, firstTrack, at, project_.file().getParentDirectory()), name, pasted);
}

bool MainComponent::duplicateRange()
{
    const auto r = arrangement_.range();
    if (! project_.loaded() || ! r)
        return false;
    const auto clip = mr::copyRange (project_.documentRoot(), r->tracks(), r->start, r->end, project_.file().getParentDirectory());
    return pasteClipAt (clip, r->firstTrack, r->end, juce::String::fromUTF8 ("구간 복제"));
}

bool MainComponent::clearRange()
{
    const auto r = arrangement_.range();
    if (! project_.loaded() || ! r)
        return false;
    return editRange (mr::withRangeCleared (project_.documentRoot(), r->tracks(), r->start, r->end, project_.file().getParentDirectory()), juce::String::fromUTF8 ("구간 내용 지우기"), r);
}

bool MainComponent::insertTime (double at, double length)
{
    if (! project_.loaded() || length <= 0.0 || recording_)
        return false;
    const auto& meters = project_.meters();
    const double bar = meters.barBeatsAt (at > 0.0 ? at - 1.0e-6 : 0.0);   // 넣는 마디는 넣는 자리 앞의 박자
    const auto name = juce::String::fromUTF8 ("시간 넣기: ") + describeBeats (at, meters) + juce::String::fromUTF8 ("에 ")
                      + juce::String (length / bar, std::abs (length / bar - std::round (length / bar)) < 1.0e-9 ? 0 : 2) + juce::String::fromUTF8 ("마디");
    ArrangementPanel::Range inserted { 0, project_.trackCount() - 1, at, at + length };
    return editRange (mr::withTimeInserted (project_.documentRoot(), at, length, project_.file().getParentDirectory()), name, inserted);
}

bool MainComponent::deleteTime (double from, double to)
{
    if (! project_.loaded() || to <= from || recording_)
        return false;
    const auto& bar = project_.meters();
    const auto name = juce::String::fromUTF8 ("시간 지우기: ") + describeBeats (from, bar) + " ~ " + describeBeats (to, bar);
    if (! editRange (mr::withTimeDeleted (project_.documentRoot(), from, to, project_.file().getParentDirectory()), name, std::nullopt))
        return false;
    return true;
}

bool MainComponent::editRange (const juce::var& root, const juce::String& name, std::optional<ArrangementPanel::Range> select)
{
    juce::String error;
    if (! project_.editRoot (root, name, error))
    {
        setStatus (error.isNotEmpty() ? error.replaceCharacter ('\n', ' ') : juce::String::fromUTF8 ("바뀐 것이 없습니다"), error.isNotEmpty());
        return false;
    }
    arrangement_.setRange (select);
    setStatus (name);
    return true;
}

bool MainComponent::changeInstrument (size_t track, const std::string& name, const juce::var& definition)
{
    if (! project_.loaded() || track >= project_.trackCount())
        return false;
    if (project_.composition().tracks[track].instrument == name && definition.isVoid())
        return false;
    std::vector<Project::Change> changes;
    changes.push_back ({ (int) track, {}, mr::withInstrument (project_.trackJson (track), name) });
    if (definition.getDynamicObject() != nullptr)
        changes.push_back ({ -1, "instruments", mr::withInlineInstrument (project_.property ("instruments"), name, definition) });
    // 플러그인을 고르면 새 재생기가 준비된 뒤 창을 연다
    const auto* def = registry_ ? registry_->find (name) : nullptr;
    const bool plugin = definition.getDynamicObject() != nullptr || (def != nullptr && (def->type == "vst3" || def->type == "au"));
    closeInstrumentWindows (true);
    juce::String error;
    if (! project_.edit (changes, juce::String::fromUTF8 ("악기 바꾸기: ") + project_.channelName (ChannelRef::track (track)), error))
    {
        setStatus (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    if (plugin && withAudio_)
        pendingInstrumentWindows_.push_back (track);
    setStatus (juce::String::fromUTF8 ("악기를 바꿨습니다: ") + juce::String::fromUTF8 (name.c_str()));
    return true;
}

std::vector<std::string> MainComponent::articulationsOf (size_t track) const
{
    std::vector<std::string> out;
    if (! registry_ || ! project_.loaded() || track >= project_.trackCount())
        return out;
    std::string name = project_.composition().tracks[track].instrument;
    if (audio_ != nullptr)
        if (auto p = audio_->player(); p != nullptr && track < p->trackCount() && p->trackInfo (track).instrument == name)
            name = p->trackInfo (track).resolved;
    if (const auto* def = registry_->find (name))
        for (const auto& [articulation, spec] : def->profile.articulations)
            out.push_back (articulation);
    return out;
}

std::map<int, juce::String> MainComponent::keyNamesOf (size_t track) const
{
    std::map<int, juce::String> out;
    if (! registry_ || ! project_.loaded() || track >= project_.trackCount())
        return out;
    const auto& name = project_.composition().tracks[track].instrument;
    const auto* def = registry_->find (name);
    if (def != nullptr && def->keyNames.empty() && audio_ != nullptr)   // 대체 악기로 연주하고 있으면 그 이름
        if (auto p = audio_->player(); p != nullptr && track < p->trackCount() && p->trackInfo (track).instrument == name)
            def = registry_->find (p->trackInfo (track).resolved);
    if (def != nullptr)
        for (const auto& [key, text] : def->keyNames)
            out[key] = juce::String::fromUTF8 (text.c_str());
    return out;
}

void MainComponent::scanPlugins()
{
    if (scanner_ != nullptr)
        return;
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    juce::StringArray args { exe.getFullPathName(), "--scan-plugins", mr::defaultPluginCache().getFullPathName() };
    // 레지스트리가 쓰는 플러그인 폴더 (build.sh --plugins 가 받는 곳)도 찾는다
    const auto bundled = mr::resolveRegistryPath ("${MUSIC_RENDER_PLUGINS:-../../external/plugins}", mr::defaultRegistryFile().getParentDirectory());
    if (bundled.isDirectory())
        args.add (bundled.getFullPathName());
    scanner_ = std::make_unique<juce::ChildProcess>();
    if (! scanner_->start (args, juce::ChildProcess::wantStdOut))
    {
        scanner_.reset();
        setStatus (juce::String::fromUTF8 ("플러그인 찾기를 시작하지 못했습니다."), true);
        return;
    }
    setStatus (juce::String::fromUTF8 ("플러그인을 찾고 있습니다 (VST3, AU). 따로 실행하므로 계속 작업할 수 있습니다."));
}

void MainComponent::showScore (bool show)
{
    if (show == scoreShown_)
        return;
    scoreShown_ = show;
    if (show && mixerShown_)
        showMixer (false);
    if (show)
        score_.refresh();
    resized();
    menuItemsChanged();
}

bool MainComponent::exportMusicXml (const juce::File& file, bool selectedOnly, juce::String& error)
{
    if (! project_.loaded())
        return false;
    try
    {
        mr::ScoreOptions o;
        if (selectedOnly && timeline_.selectedTrack() >= 0)
            o.tracks = { (size_t) timeline_.selectedTrack() };
        o.percussion = score_.percussion;
        const auto score = mr::buildScore (project_.composition(), o);
        if (score.parts.empty())
        {
            error = juce::String::fromUTF8 ("악보로 옮길 음표가 없습니다.");
            return false;
        }
        mr::writeMusicXml (score, file);
        setStatus (juce::String::fromUTF8 ("MusicXML 을 썼습니다: ") + file.getFileName() + " (" + juce::String ((int) score.parts.size())
                   + juce::String::fromUTF8 ("파트, ") + juce::String ((int) score.parts.front().measures.size()) + juce::String::fromUTF8 ("마디, ")
                   + juce::String (mr::keyName (score.key)) + (score.keyEstimated ? juce::String::fromUTF8 (" 추정") : juce::String()) + ")");
        return true;
    }
    catch (const std::exception& e)
    {
        error = juce::String::fromUTF8 (e.what());
        return false;
    }
}

void MainComponent::showMixer (bool show)
{
    if (show == mixerShown_)
        return;
    mixerShown_ = show;
    if (show && scoreShown_)
    {
        scoreShown_ = false;
        score_.setVisible (false);
    }
    // 믹서는 아래 칸이 넉넉해야 한다 (페이더)
    layout_.setItemLayout (2, show ? 300 : 160, -1.0, show ? -0.55 : -0.42);
    layout_.setItemLayout (0, 120, -1.0, show ? -0.45 : -0.58);
    resized();
    menuItemsChanged();
    if (withAudio_)
        saveSettings();
}

void MainComponent::primeMeters (double seconds)
{
    if (! project_.loaded())
        return;
    try
    {
        mr::PlayerOptions options;
        options.threads = 0;
        mr::RealtimePlayer player (project_.composition(), options);
        // 8초 앞에서 시작한다 (위치를 옮기면 그 앞에서 시작한 음은 다시 울리지 않는다). 마지막 0.5초의 미터를 읽는다.
        const double start = std::max (0.0, seconds - 8.0);
        player.seekSeconds (start);
        player.play();
        juce::AudioBuffer<float> out (2, 512);
        float* ptrs[2] = { out.getWritePointer (0), out.getWritePointer (1) };
        const int blocks = (int) std::ceil ((seconds - start + 0.1) * player.sampleRate() / 512.0);
        const int metered = blocks - (int) std::ceil (0.5 * player.sampleRate() / 512.0);
        double now = 1.0;
        for (int block = 0; block < blocks; ++block)
        {
            player.process (ptrs, 2, 512);
            if (block < metered)
            {
                for (size_t i = 0; i < player.trackCount(); ++i)
                    player.takeTrackPeaks (i);
                for (size_t i = 0; i < player.busCount(); ++i)
                    player.takeBusPeaks (i);
                player.takeMasterPeaks();
            }
            else if ((block - metered) % 3 == 2)
            {
                std::vector<mr::StereoPeak> tracks (player.trackCount()), buses (player.busCount());
                for (size_t i = 0; i < tracks.size(); ++i)
                    tracks[i] = player.takeTrackPeaks (i);
                for (size_t i = 0; i < buses.size(); ++i)
                    buses[i] = player.takeBusPeaks (i);
                mixer_.setLevels (tracks, buses, player.takeMasterPeaks(), now);
                mixer_.analyzer().update (&player, project_.composition().master.targetLufs);
                now += 1536.0 / 48000.0;
            }
        }
    }
    catch (const std::exception& e)
    {
        setStatus (juce::String::fromUTF8 (e.what()), true);
    }
}

void MainComponent::updateMeters()
{
    auto player = audio_ != nullptr ? audio_->player() : nullptr;
    if (player == nullptr)
        return;
    std::vector<mr::StereoPeak> tracks (player->trackCount()), buses (player->busCount());
    std::vector<float> peaks (tracks.size());
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        tracks[i] = player->takeTrackPeaks (i);
        peaks[i] = std::max (tracks[i].left, tracks[i].right);
    }
    for (size_t i = 0; i < buses.size(); ++i)
        buses[i] = player->takeBusPeaks (i);
    const auto master = player->takeMasterPeaks();
    arrangement_.setLevels (peaks);
    if (mixerShown_)
    {
        mixer_.setLevels (tracks, buses, master, juce::Time::getMillisecondCounterHiRes() / 1000.0);
        if (mixer_.analyzer().isVisible())
            mixer_.analyzer().update (player.get(), project_.composition().master.targetLufs);
    }
}

void MainComponent::showTempoDialog()
{
    if (! project_.loaded())
        return;
    const auto& c = project_.composition();
    const bool single = c.tempo.size() == 1 && ! c.tempo[0].ramp;
    tempoDialog_ = std::make_unique<juce::AlertWindow> (
        juce::String::fromUTF8 ("템포와 박자"),
        single ? juce::String::fromUTF8 ("곡 전체의 템포와 박자를 바꿉니다.")
               : juce::String::fromUTF8 ("템포가 바뀌는 곡이라 박자만 바꿀 수 있습니다."),
        juce::MessageBoxIconType::NoIcon, this);
    if (single)
        tempoDialog_->addTextEditor ("bpm", juce::String (c.tempo[0].bpm, 3).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."),
                                     juce::String::fromUTF8 ("템포 (BPM)"));
    tempoDialog_->addTextEditor ("signature", juce::String (c.beatsPerBar) + "/" + juce::String (c.beatUnit), juce::String::fromUTF8 ("박자"));
    tempoDialog_->addButton (juce::String::fromUTF8 ("바꾸기"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    tempoDialog_->addButton (juce::String::fromUTF8 ("취소"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    tempoDialog_->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<MainComponent> (this), single] (int result)
        {
            if (safe == nullptr || safe->tempoDialog_ == nullptr)
                return;
            const auto bpmText = single ? safe->tempoDialog_->getTextEditorContents ("bpm").trim() : juce::String();
            const auto signature = safe->tempoDialog_->getTextEditorContents ("signature").trim();
            safe->tempoDialog_.reset();
            if (result != 1 || ! safe->project_.loaded())
                return;
            const auto& comp = safe->project_.composition();
            auto tempo = mr::tempoJson (comp.tempo);
            if (single)
            {
                const double bpm = bpmText.getDoubleValue();
                if (bpm < 1.0 || bpm > 999.0)
                {
                    safe->setStatus (juce::String::fromUTF8 ("템포는 1 에서 999 BPM 사이여야 합니다."), true);
                    return;
                }
                tempo = mr::tempoJson ({ mr::TempoPoint { 0.0, bpm, false } });
            }
            const int beats = signature.upToFirstOccurrenceOf ("/", false, false).trim().getIntValue();
            const int unit = signature.fromFirstOccurrenceOf ("/", false, false).trim().getIntValue();
            juce::String error;
            if (! signature.contains ("/") || ! safe->project_.editTempo (tempo, mr::timeSignatureJson (beats, unit),
                                                                          juce::String::fromUTF8 ("템포와 박자"), error))
                safe->setStatus (juce::String::fromUTF8 ("박자는 4/4 처럼 씁니다. 아래 수는 1, 2, 4, 8, 16, 32 가운데 하나입니다."), true);
        }), false);
}

void MainComponent::showOpenDialog()
{
    if (project_.hasUnsavedEdits())
    {
        askToSave ([safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr)
                safe->showOpenDialog();
        });
        return;
    }
    auto start = project_.loaded() ? project_.file().getParentDirectory()
                                   : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    chooser_ = std::make_unique<juce::FileChooser> (juce::String::fromUTF8 ("프로젝트 열기"), start, "*.json");
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                           [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& fc)
                           {
                               if (safe != nullptr && fc.getResult() != juce::File())
                                   safe->openProject (fc.getResult());
                           });
}

void MainComponent::rebuildPlayer()
{
    if (! withAudio_ || ! project_.loaded())
        return;
    ++generation_;
    double resume = localSeconds_;
    bool playing = false;
    if (auto current = audio_->player())
    {
        resume = current->positionSeconds();
        playing = current->isPlaying();
    }

    mr::PlayerOptions options;
    options.sampleRate = audio_->sampleRate();
    options.blockSize = 512;
    options.prepareClipsInBackground = true;   // 오디오 클립의 늘이기와 높이 편집을 고칠 때 화면이 멈추지 않는다

    // 플러그인 악기는 메시지 스레드에서 만든다. 아니면 뒤에서 만든다.
    bool needsMessageThread = false;
    {
        mr::RenderOptions ro;
        mr::Diagnostics d;
        const auto registry = mr::buildRegistry (project_.composition(), ro, d);
        for (const auto& t : project_.composition().tracks)
            if (const auto* def = registry.find (t.instrument))
                needsMessageThread |= def->type == "vst3" || def->type == "au";
        // 플러그인 효과도 메시지 스레드에서 만든다
        auto hasPluginEffect = [] (const std::vector<mr::EffectSpec>& effects)
        {
            return std::any_of (effects.begin(), effects.end(), [] (const mr::EffectSpec& e) { return e.type == "plugin"; });
        };
        for (const auto& t : project_.composition().tracks)
            needsMessageThread |= hasPluginEffect (t.effects);
        for (const auto& b : project_.composition().buses)
            needsMessageThread |= hasPluginEffect (b.effects);
        needsMessageThread |= hasPluginEffect (project_.composition().master.effects);
    }

    if (loader_ != nullptr)
        retired_.push_back (std::move (loader_));
    transport_.setTransportEnabled (audio_->player() != nullptr);
    if (needsMessageThread)
    {
        setStatus (juce::String::fromUTF8 ("플러그인 악기를 준비하고 있습니다"));
        std::shared_ptr<mr::RealtimePlayer> player;
        juce::String error;
        try
        {
            player = std::make_shared<mr::RealtimePlayer> (project_.composition(), options);
        }
        catch (const std::exception& e)
        {
            error = juce::String::fromUTF8 (e.what());
        }
        playerReady (player, error, generation_, resume, playing);
        return;
    }
    loader_ = std::make_unique<Loader> (*this, project_.composition(), options, generation_, resume, playing);
    loader_->startThread();
    setStatus (loader_->progressText());
}

void MainComponent::playerReady (std::shared_ptr<mr::RealtimePlayer> player, const juce::String& error, int generation,
                                 double resumeSeconds, bool resumePlaying)
{
    if (generation != generation_)
        return;   // 그 사이에 다시 불러왔다
    if (player == nullptr)
    {
        setStatus (juce::String::fromUTF8 ("재생을 준비하지 못했습니다: ") + error.upToFirstOccurrenceOf ("\n", false, false), true);
        return;
    }
    // 옛 플러그인의 창을 닫는다 (바뀐 소리는 곡에 쓰고). 그 사이에 바뀐 곡은 새 재생기에도 넣는다.
    for (const size_t t : closeInstrumentWindows (true, true))
        pendingInstrumentWindows_.push_back (t);
    try
    {
        if (! player->apply (project_.composition()))
        {
            rebuildPlayer();   // 열 창은 다음 재생기에서 연다
            return;
        }
    }
    catch (const std::exception& e)
    {
        setStatus (juce::String::fromUTF8 ("재생을 준비하지 못했습니다: ") + juce::String::fromUTF8 (e.what()), true);
        return;
    }
    player->setMonitorGainDb (monitorGainDb_);
    applyTransportToPlayer (*player);
    mixer_.clearLevels();
    for (size_t i = 0; i < player->trackCount(); ++i)
    {
        const auto& info = player->trackInfo (i);
        auto desc = juce::String::fromUTF8 (info.resolved.c_str());
        if (info.resolved != info.instrument)
            desc = juce::String::fromUTF8 (info.instrument.c_str()) + juce::String::fromUTF8 (" (대체: ") + desc + ")";
        arrangement_.setTrackDescription (i, desc);
    }
    player->seekSeconds (resumeSeconds);
    if (resumePlaying)
        player->play();
    player->setInputMonitor (inputMonitorTrack());
    audio_->setPlayer (player);
    transport_.setTransportEnabled (true);
    auto reopen = std::move (pendingInstrumentWindows_);
    pendingInstrumentWindows_.clear();
    std::sort (reopen.begin(), reopen.end());
    reopen.erase (std::unique (reopen.begin(), reopen.end()), reopen.end());
    for (const size_t t : reopen)
        if (t < project_.trackCount() && player->pluginInstance (t) != nullptr)
            openInstrumentWindow (t);
    const auto warnings = player->warnings().size();
    setStatus (juce::String::fromUTF8 ("재생 준비가 끝났습니다. ") + juce::String (audio_->sampleRate() / 1000.0, 1) + " kHz"
               + (warnings > 0 ? juce::String::fromUTF8 (", 경고 ") + juce::String ((int) warnings) + juce::String::fromUTF8 ("건") : juce::String()));
}

void MainComponent::seekToBeat (double beat)
{
    rangeReturnBeat_.reset();
    beat = juce::jlimit (0.0, std::max (0.0, project_.lengthBeats()), beat);
    const double seconds = project_.secondsAtBeat (beat);
    localSeconds_ = seconds;
    if (audio_ != nullptr && audio_->player() != nullptr)
        audio_->player()->seekSeconds (seconds);
    timeline_.setPlayheadBeat (beat);
    updateTransport();
}

void MainComponent::setPlayheadSeconds (double seconds) { seekToBeat (project_.beatAtSeconds (seconds)); }

void MainComponent::setTyping (bool on)
{
    typing_.setEnabled (on);
    pianoRoll_.setTypedNotes (typing_.held());
    showTypingState();
    commands_.commandStatusChanged();
    setStatus (on ? juce::String::fromUTF8 ("자판 연주를 켰습니다. 고른 트랙을 Z 줄과 Q 줄(흰건반), 그 위 줄(검은건반)로 연주합니다. ⌥↑ ⌥↓ 옥타브")
                  : juce::String::fromUTF8 ("자판 연주를 껐습니다. Q 는 격자 맞추기, X 는 믹서입니다."));
}

void MainComponent::showTypingState()
{
    const auto z = juce::MidiMessage::getMidiNoteName (typing_.base(), true, true, 4);
    transport_.setTyping (typing_.isEnabled(), "Z=" + z);
    pianoRoll_.setTypingKeys (typing_.isEnabled() ? typing_.labels() : std::map<int, juce::String> {});
}

bool MainComponent::playBeats (double fromBeat, double toBeat)
{
    auto p = player();
    if (p == nullptr || toBeat <= fromBeat)
        return false;
    const double rate = p->sampleRate();
    p->playRange ((int64_t) std::llround (project_.secondsAtBeat (fromBeat) * rate), (int64_t) std::llround (project_.secondsAtBeat (toBeat) * rate));
    rangeReturnBeat_ = fromBeat;
    timeline_.setPlayheadBeat (fromBeat);
    const auto bar = [this] (double beat) { return juce::String (project_.meters().barAt (beat) + 1); };
    setStatus (juce::String::fromUTF8 ("구간 재생: ") + bar (fromBeat) + juce::String::fromUTF8 ("마디에서 ")
               + bar (std::max (fromBeat, toBeat - 1.0e-6)) + juce::String::fromUTF8 ("마디까지"));
    return true;
}

void MainComponent::togglePlay()
{
    if (audio_ == nullptr || audio_->player() == nullptr)
        return;
    auto player = audio_->player();
    if (player->isPlaying() || player->countingIn())
    {
        if (recording_)
            stopRecording();
        player->stop();
    }
    else
    {
        if (looping_)
            if (const auto loop = project_.loopRange(); loop && (timeline_.playheadBeat() < loop->first || timeline_.playheadBeat() >= loop->second))
                seekToBeat (loop->first);   // 반복이 켜져 있으면 구간 안에서 시작한다
        player->play();
    }
}

void MainComponent::applyTransportToPlayer (mr::RealtimePlayer& player)
{
    const double rate = player.sampleRate();
    if (const auto loop = project_.loopRange())
        player.setLoopRange ((int64_t) std::llround (project_.secondsAtBeat (loop->first) * rate),
                             (int64_t) std::llround (project_.secondsAtBeat (loop->second) * rate));
    else
        player.setLoopRange (0, 0);
    player.setLooping (looping_);
    player.setMetronome (metronome_);
    player.setMetronomeGainDb (metronomeGainDb_);
}

double MainComponent::currentBeat() const
{
    if (auto p = player())
        return project_.beatAtSeconds (p->positionSeconds());
    return timeline_.playheadBeat();
}

void MainComponent::showHeldNotes()
{
    auto held = typing_.held();
    for (const auto& [pitch, track] : midiTracks_)
        held.push_back (pitch);
    pianoRoll_.setTypedNotes (std::move (held));
}

void MainComponent::liveMidi (const juce::MidiMessage& m, int64_t positionSample)
{
    const auto p = player();
    const int selected = timeline_.selectedTrack();
    const bool haveTrack = p != nullptr && selected >= 0 && (size_t) selected < p->trackCount();
    const double rate = audio_ != nullptr ? audio_->sampleRate() : 48000.0;
    const double beat = project_.loaded() ? project_.beatAtSeconds ((double) positionSample / rate) : 0.0;
    const bool capture = recording_ && ! audioRecording_ && p != nullptr && p->isPlaying();
    if (m.isNoteOn())
    {
        const int pitch = m.getNoteNumber(), velocity = m.getVelocity();
        if (const auto it = midiTracks_.find (pitch); it != midiTracks_.end() && p != nullptr && it->second < p->trackCount())
            p->auditionNoteOff (it->second, pitch);
        if (haveTrack)
        {
            midiTracks_[pitch] = (size_t) selected;
            p->auditionNoteOn ((size_t) selected, pitch, velocity);
        }
        else if (selected < 0)
            setStatus (juce::String::fromUTF8 ("트랙을 고르면 MIDI 입력으로 그 트랙을 연주합니다."));
        if (capture)
            recorder_.noteOn (beat, pitch, velocity);
        stepNote (pitch, velocity, true);
        showHeldNotes();
    }
    else if (m.isNoteOff())
    {
        const int pitch = m.getNoteNumber();
        if (const auto it = midiTracks_.find (pitch); it != midiTracks_.end())
        {
            if (p != nullptr && it->second < p->trackCount())
                p->auditionNoteOff (it->second, pitch);
            midiTracks_.erase (it);
        }
        if (recording_ && ! audioRecording_)
            recorder_.noteOff (beat, pitch);
        stepNote (pitch, 0, false);
        showHeldNotes();
    }
    else if (m.isController())
    {
        if (learn_ != nullptr && learn_->handle (m))
            return;   // 믹서에 묶인 컨트롤러 (배우는 중이면 묶는다)
        if (haveTrack)
            p->auditionController ((size_t) selected, m.getControllerNumber(), m.getControllerValue());
        if (capture)
            recorder_.controller (beat, m.getControllerNumber(), m.getControllerValue());
    }
    else if (m.isPitchWheel())
    {
        if (haveTrack)
            p->auditionController ((size_t) selected, mr::kPitchBend, m.getPitchWheelValue());
        if (capture)
            recorder_.controller (beat, mr::kPitchBend, (m.getPitchWheelValue() - 8192) / 8192.0);
    }
    else if (m.isChannelPressure())
    {
        if (capture)
            recorder_.controller (beat, mr::kAftertouch, m.getChannelPressureValue());
    }
}

bool MainComponent::startRecording()
{
    auto p = player();
    if (p == nullptr)
    {
        setStatus (juce::String::fromUTF8 ("재생 준비가 끝난 뒤에 녹음할 수 있습니다."), true);
        return false;
    }
    const int t = timeline_.selectedTrack();
    if (t < 0 || (size_t) t >= project_.trackCount())
    {
        setStatus (juce::String::fromUTF8 ("녹음할 트랙을 고르십시오."), true);
        return false;
    }
    if (recording_)
        return true;
    if (arrangement_.isAudioTrack ((size_t) t))
        return startAudioRecording ((size_t) t);
    recordTrack_ = (size_t) t;
    mr::RecordOptions o;
    if (recordQuantize_)
        o.quantizeGrid = pianoRoll_.grid();
    const auto loop = project_.loopRange();
    if (looping_ && loop)
    {
        o.loopEndBeat = loop->second;
        if (punch_)
            o.punch = *loop;
    }
    const auto& name = juce::String::fromUTF8 (project_.composition().tracks[recordTrack_].name.c_str());
    if (p->isPlaying())
    {
        recorder_.start (o, currentBeat());
        setStatus (juce::String::fromUTF8 ("녹음 중: 「") + name + juce::String::fromUTF8 ("」 (⌥R 로 끝냅니다)"));
    }
    else
    {
        if (looping_ && loop && (timeline_.playheadBeat() < loop->first || timeline_.playheadBeat() >= loop->second))
            seekToBeat (loop->first);
        recorder_.start (o, timeline_.playheadBeat());
        p->playWithCountIn (countInBars_);
        setStatus (countInBars_ > 0 ? juce::String::fromUTF8 ("카운트인 ") + juce::String (countInBars_) + juce::String::fromUTF8 ("마디 뒤 「") + name
                                          + juce::String::fromUTF8 ("」 에 녹음합니다.")
                                    : juce::String::fromUTF8 ("녹음 중: 「") + name + juce::String::fromUTF8 ("」 (⌥R 로 끝냅니다)"));
    }
    recording_ = true;
    transport_.setRecording (true);
    commands_.commandStatusChanged();
    return true;
}

bool MainComponent::startAudioRecording (size_t track)
{
    auto p = player();
    if (p == nullptr || audio_ == nullptr)
    {
        setStatus (juce::String::fromUTF8 ("재생 준비가 끝난 뒤에 녹음할 수 있습니다."), true);
        return false;
    }
    const int inputs = audio_->enableInput();
    if (inputs <= 0)
    {
        setStatus (juce::String::fromUTF8 ("오디오 입력이 없습니다. 설정 > 오디오 장치(⌘,)에서 입력 장치와 채널을 고릅니다."), true);
        return false;
    }
    const auto name = juce::String::fromUTF8 (project_.composition().tracks[track].name.c_str());
    const auto file = nextTakeFile (project_, track);
    std::string error;
    // 반복 재생 중이면 반복마다의 테이크를 한 파일에 (끝나면 반복마다 클립, 마지막만 들린다)
    if (! audioRecorder_.start (file, p->sampleRate(), std::min (2, inputs), error, looping_))
    {
        setStatus (juce::String::fromUTF8 ("녹음 파일을 열지 못했습니다: ") + juce::String::fromUTF8 (error.c_str()), true);
        return false;
    }
    audio_->setRecorder (&audioRecorder_);
    recordTrack_ = track;
    audioRecording_ = true;
    recording_ = true;
    transport_.setRecording (true);
    commands_.commandStatusChanged();
    const auto loop = project_.loopRange();
    if (! p->isPlaying())
    {
        if (looping_ && loop && (timeline_.playheadBeat() < loop->first || timeline_.playheadBeat() >= loop->second))
            seekToBeat (loop->first);
        p->playWithCountIn (countInBars_);
    }
    setStatus (juce::String::fromUTF8 ("오디오 녹음: 「") + name + juce::String::fromUTF8 ("」, 입력 ") + juce::String (std::min (2, inputs))
               + juce::String::fromUTF8 ("채널 (⌥R 로 끝냅니다. 반복으로 되돌아가면 한 바퀴에서 끝납니다)"));
    return true;
}

juce::File MainComponent::nextTakeFile (const Project& project, size_t track)
{
    const auto dir = project.file().getParentDirectory().getChildFile ("audio");
    auto stem = juce::File::createLegalFileName (juce::String::fromUTF8 (project.composition().tracks[track].name.c_str()));
    if (stem.isEmpty())
        stem = "take";
    for (int n = 1;; ++n)
    {
        const auto f = dir.getChildFile (stem + " take " + juce::String (n) + ".wav");
        if (! f.exists())
            return f;
    }
}

bool MainComponent::applyAudioTake (Project& project, size_t track, const mr::AudioTake& take, int latencySamples, juce::String& error)
{
    if (! project.loaded() || track >= project.trackCount() || take.sampleRate <= 0.0)
        return false;
    // 반복마다 클립 하나 (같은 파일의 다른 자리). 반복 녹음이면 마지막 반복만 들리고 앞의 것은 테이크로 남는다 (클립 뮤트)
    auto passes = take.passes;
    if (passes.empty())
        passes.push_back ({ 0, take.startPosition, take.samples });
    auto json = project.trackJson (track);
    auto tidy = [] (double v) { return std::round (v * 1.0e6) / 1.0e6; };
    int added = 0;
    for (size_t i = 0; i < passes.size(); ++i)
    {
        const auto& pass = passes[i];
        // 반복의 첫 샘플은 (출력 지연 + 장치의 왕복 지연) 만큼 앞의 곡 위치에서 연주한 소리다
        double startSec = (double) (pass.startPosition - latencySamples) / take.sampleRate;
        double skip = 0.0;
        if (startSec < 0.0)
        {
            skip = -startSec;
            startSec = 0.0;
        }
        const double length = (double) pass.samples / take.sampleRate - skip;
        if (length <= 0.001)
            continue;
        mr::AudioClip clip;
        clip.file = take.file.getRelativePathFrom (project.file().getParentDirectory()).replaceCharacter ('\\', '/').toStdString();
        clip.startBeat = tidy (project.beatAtSeconds (startSec));
        clip.offsetSeconds = tidy ((double) pass.fileStart / take.sampleRate + skip);
        clip.lengthSeconds = tidy (length);
        clip.muted = i + 1 < passes.size();
        json = mr::withClipAdded (json, clip);
        ++added;
    }
    if (added == 0)
    {
        error = juce::String::fromUTF8 ("녹음이 너무 짧습니다.");
        return false;
    }
    return project.editTrack (track, json, added > 1 ? juce::String::fromUTF8 ("오디오 녹음 (테이크 ") + juce::String (added) + juce::String::fromUTF8 ("개)")
                                                     : juce::String::fromUTF8 ("오디오 녹음"), error);
}

bool MainComponent::stopAudioRecording (bool keep)
{
    auto p = player();
    const int latency = (p != nullptr ? p->outputLatencySamples() : 0) + (audio_ != nullptr ? audio_->roundTripLatency() : 0);
    if (audio_ != nullptr)
        audio_->setRecorder (nullptr);   // 콜백에서 뗀 뒤에 닫는다
    const auto take = audioRecorder_.stop();
    recording_ = false;
    audioRecording_ = false;
    transport_.setRecording (false);
    arrangement_.setRecordingRegion (std::nullopt);
    commands_.commandStatusChanged();
    if (! take)
    {
        setStatus (juce::String::fromUTF8 ("녹음한 소리가 없습니다."));
        return false;
    }
    if (! keep || recordTrack_ >= project_.trackCount())
    {
        take->file.deleteFile();
        return false;
    }
    juce::String error;
    if (! applyAudioTake (project_, recordTrack_, *take, latency, error))
    {
        setStatus (juce::String::fromUTF8 ("녹음을 넣지 못했습니다: ") + error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    arrangement_.setSelectedClip (ArrangementPanel::ClipRef { recordTrack_, project_.composition().tracks[recordTrack_].clips.size() - 1 });
    setStatus (juce::String::fromUTF8 ("오디오 녹음: ") + juce::String ((double) take->samples / take->sampleRate, 1) + juce::String::fromUTF8 ("초를 「")
               + juce::String::fromUTF8 (project_.composition().tracks[recordTrack_].name.c_str()) + juce::String::fromUTF8 ("」 에 넣었습니다 (")
               + take->file.getFileName() + juce::String::fromUTF8 (", 지연 ") + juce::String (1000.0 * latency / take->sampleRate, 1) + " ms)");
    return true;
}

bool MainComponent::stopRecording (bool keep)
{
    if (! recording_)
        return false;
    if (audioRecording_)
        return stopAudioRecording (keep);
    recording_ = false;
    transport_.setRecording (false);
    pianoRoll_.setRecordingNotes ({});
    commands_.commandStatusChanged();
    const auto take = recorder_.finish (currentBeat());
    if (! keep || recordTrack_ >= project_.trackCount())
        return false;
    if (take.notes.empty() && take.lanes.empty())
    {
        setStatus (juce::String::fromUTF8 ("녹음한 음이 없습니다."));
        return false;
    }
    mr::NoteSelection selected;
    juce::String error;
    if (! applyTake (project_, recordTrack_, take, selected, error))
    {
        setStatus (juce::String::fromUTF8 ("녹음한 음을 넣지 못했습니다: ") + error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    if (timeline_.selectedTrack() == (int) recordTrack_)
        pianoRoll_.setSelection (selected);
    setStatus (juce::String::fromUTF8 ("녹음: 음표 ") + juce::String ((int) take.notes.size()) + juce::String::fromUTF8 ("개를 「")
               + juce::String::fromUTF8 (project_.composition().tracks[recordTrack_].name.c_str()) + juce::String::fromUTF8 ("」 에 넣었습니다.")
               + (take.lanes.empty() ? juce::String() : juce::String::fromUTF8 (" 컨트롤러 곡선 ") + juce::String ((int) take.lanes.size()) + juce::String::fromUTF8 ("개.")));
    return true;
}

bool MainComponent::applyTake (Project& project, size_t track, const mr::RecordedTake& take, mr::NoteSelection& selected, juce::String& error)
{
    if (! project.loaded() || track >= project.trackCount())
        return false;
    auto json = project.trackJson (track);
    const auto result = mr::addNotes (project.editNotes (track), take.notes);
    json = mr::withNotes (json, result.notes);
    const auto& lanes = project.composition().tracks[track].automation;
    for (const auto& [id, lane] : take.lanes)
    {
        mr::AutomationLane merged;
        if (const auto it = lanes.find (id); it != lanes.end())
            merged = it->second;
        mr::mergeLanePoints (merged, lane.points);
        json = mr::withLane (json, id, merged);
    }
    if (! project.editTrack (track, json, juce::String::fromUTF8 ("녹음"), error))
        return false;
    selected = result.selection;
    return true;
}

void MainComponent::setCountInBars (int bars)
{
    countInBars_ = juce::jlimit (0, 2, bars);
    saveSettings();
}

bool MainComponent::setLooping (bool on)
{
    if (on && ! project_.loopRange())
    {
        setStatus (juce::String::fromUTF8 ("반복 구간이 없습니다. 눈금자를 ⌥ 로 끌거나 편집 메뉴에서 고른 음표 구간을 반복 구간으로 정하십시오."), true);
        looping_ = false;
    }
    else
        looping_ = on;
    transport_.setLooping (looping_);
    timeline_.setLoopOn (looping_);
    if (auto p = player())
        p->setLooping (looping_);
    commands_.commandStatusChanged();
    return looping_ == on;
}

void MainComponent::setMetronome (bool on)
{
    metronome_ = on;
    transport_.setMetronome (on);
    if (auto p = player())
        p->setMetronome (on);
    saveSettings();
    commands_.commandStatusChanged();
}

bool MainComponent::editLoop (const std::optional<std::pair<double, double>>& range)
{
    juce::String error;
    if (! project_.editLoop (range, error))
    {
        if (error.isNotEmpty())
            setStatus (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    if (range)
    {
        const auto bar = [this] (double beat) { return juce::String (project_.meters().barAt (beat) + 1); };
        setStatus (juce::String::fromUTF8 ("반복 구간: ") + bar (range->first) + juce::String::fromUTF8 ("마디에서 ")
                   + bar (std::max (range->first, range->second - 1.0e-6)) + juce::String::fromUTF8 ("마디까지"));
    }
    else
        setStatus (juce::String::fromUTF8 ("반복 구간을 지웠습니다."));
    return true;
}

juce::PopupMenu MainComponent::midiInputMenu()
{
    juce::PopupMenu m;
    if (midi_ == nullptr)
    {
        m.addItem (juce::String::fromUTF8 ("오디오가 없어 MIDI 입력을 쓰지 않습니다"), false, false, std::function<void()>());
        return m;
    }
    midi_->refreshDevices();
    const auto devices = midi_->devices();
    if (devices.isEmpty())
        m.addItem (juce::String::fromUTF8 ("MIDI 입력 장치가 없습니다"), false, false, std::function<void()>());
    for (const auto& d : devices)
    {
        const auto id = d.identifier;
        m.addItem (d.name, true, midi_->isEnabled (id), [this, id]
        {
            midi_->setEnabled (id, ! midi_->isEnabled (id));
            saveSettings();
        });
    }
    return m;
}

void MainComponent::goToStart() { seekToBeat (0.0); timeline_.setView (0.0, timeline_.pixelsPerBeat()); }

void MainComponent::zoomToFit()
{
    const int width = std::max (200, arrangement_.getWidth() - ArrangementPanel::headerWidth - ArrangementPanel::scrollBarSize);
    const double beats = std::max (8.0, project_.lengthBeats() + 2.0);
    timeline_.setView (0.0, width / beats);
}

void MainComponent::showBars (double firstBar, double lastBar)
{
    const int width = std::max (200, arrangement_.getWidth() - ArrangementPanel::headerWidth - ArrangementPanel::scrollBarSize);
    const auto& meters = project_.meters();
    const double start = std::max (0.0, meters.barStart ((int) std::floor (firstBar) - 1));
    const double end = std::max (start + 1.0, meters.barStart ((int) std::floor (lastBar)));
    timeline_.setView (start, width / (end - start));
}

void MainComponent::updateTitle()
{
    const bool edited = project_.hasUnsavedEdits();
    const auto title = project_.loaded() ? project_.title() + (edited ? juce::String::fromUTF8 (" (수정됨)") : juce::String())
                                         : juce::String ("InitialDAW");
    transport_.setTitle (title);
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
    {
        window->setName (project_.loaded() ? title + " - InitialDAW" : juce::String ("InitialDAW"));
        if (auto* peer = window->getPeer())
            peer->setDocumentEditedStatus (edited);
    }
}

void MainComponent::updateTransport()
{
    if (! project_.loaded())
        return;
    const double beat = timeline_.playheadBeat();
    const auto& meters = project_.meters();
    const int bar = meters.barAt (beat) + 1;
    const int beatInBar = (int) std::floor (meters.beatInBar (beat) + 1.0e-9) + 1;
    transport_.setPosition (bar, beatInBar, project_.secondsAtBeat (beat));
    const auto& segment = meters.segmentAt (beat);   // 재생 위치의 박자
    transport_.setTempo (project_.tempo().bpmAt (beat), segment.beatsPerBar, segment.beatUnit);
}

void MainComponent::setStatus (const juce::String& text, bool error)
{
    transport_.setStatus (text, error);
}

void MainComponent::timerCallback()
{
    ++ticks_;
    if (ticks_ % 15 == 0 && ! effectWindows_.empty())
        checkEffectWindows();
    if (ticks_ % 300 == 0 && autosaveDir_ != juce::File() && project_.loaded())   // 10초마다 본다
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (! project_.hasUnsavedEdits())
        {
            if (! lastAutosaved_.empty())
                discardAutosave();   // 저장한 상태로 되돌렸다
        }
        else if (now - lastAutosaveMs_ >= autosaveSeconds * 1000.0)
            autosaveNow();
    }
    auto player = audio_ != nullptr ? audio_->player() : nullptr;
    if (loader_ != nullptr && loader_->isThreadRunning())
        setStatus (loader_->progressText());
    if (bounceJob_ != nullptr && ticks_ % 10 == 0)
        setStatus (juce::String::fromUTF8 ("오디오로 굳히는 중: ") + juce::String::fromUTF8 (bounceTrackName_.c_str()) + " "
                   + juce::String (juce::roundToInt (bounceJob_->progress() * 100.0f)) + "%");
    retired_.erase (std::remove_if (retired_.begin(), retired_.end(), [] (const auto& l) { return ! l->isThreadRunning(); }),
                    retired_.end());
    updateVideoWindow();

    if (player != nullptr)
    {
        const double seconds = player->positionSeconds();
        localSeconds_ = seconds;
        const double beat = project_.beatAtSeconds (seconds);
        timeline_.setPlayheadBeat (beat);
        transport_.setPlaying (player->isPlaying());
        if (rangeReturnBeat_ && ! player->isPlaying())
        {
            const double back = *rangeReturnBeat_;   // 구간 재생이 끝났거나 멈췄다: 처음 자리로
            rangeReturnBeat_.reset();
            seekToBeat (back);
        }

        if (mixerShown_)
            mixer_.showAutomationAt (beat);   // 곡선이 정하는 페이더와 밸런스

        // 재생 중에는 재생 위치가 보이도록 화면을 넘긴다
        if (player->isPlaying())
        {
            const double visible = (arrangement_.getWidth() - ArrangementPanel::headerWidth - ArrangementPanel::scrollBarSize)
                                   / timeline_.pixelsPerBeat();
            if (beat > timeline_.viewStartBeat() + visible * 0.92 || beat < timeline_.viewStartBeat())
                timeline_.setView (std::max (0.0, beat - visible * 0.08), timeline_.pixelsPerBeat());
        }

        player->collectRetired();
        updateMeters();
        // 창이 뒤로 가거나 key-up 을 놓쳐도 자판으로 누른 음이 남지 않게 한다
        if (! juce::Process::isForegroundProcess())
            typing_.releaseAll();
        else
            typing_.update();

        if (recording_ && audioRecording_)
        {
            if ((! player->isPlaying() && ! player->countingIn()) || audioRecorder_.ended())
                stopRecording();   // 곡 끝, 멈춤, 탐색이나 반복으로 위치가 끊겼다
            else if (const auto start = audioRecorder_.startPosition(); start >= 0)
            {
                const double rate = player->sampleRate();
                arrangement_.setRecordingRegion (ArrangementPanel::RecordingRegion {
                    recordTrack_, project_.beatAtSeconds ((double) start / rate),
                    project_.beatAtSeconds ((double) (start + audioRecorder_.samplesWritten()) / rate), audioRecorder_.peak() });
            }
        }
        else if (recording_)
        {
            if (! player->isPlaying() && ! player->countingIn())
                stopRecording();   // 곡 끝에 닿았거나 밖에서 멈췄다
            else if (timeline_.selectedTrack() == (int) recordTrack_)
                pianoRoll_.setRecordingNotes (recorder_.notesSoFar (beat));
        }
    }
    else
        transport_.setPlaying (false);

    updateTransport();
    if (midi_ != nullptr && ticks_ % 60 == 0)
        midi_->refreshDevices();   // 꽂은 장치를 연다

    if (ticks_ % 15 == 0 && audio_ != nullptr)
        transport_.setCpu (audio_->cpuLoad());

    if (scanner_ != nullptr && ! scanner_->isRunning())
    {
        // 플러그인이 표준 출력에 쓴 줄이 앞에 있을 수 있다: 마지막 JSON 줄을 읽는다
        const auto output = "\n" + scanner_->readAllProcessOutput();
        const auto result = juce::JSON::parse (output.fromLastOccurrenceOf ("\n{", true, false).trim());
        scanner_.reset();
        if ((bool) result.getProperty ("ok", false))
            setStatus (juce::String::fromUTF8 ("플러그인 ") + result.getProperty ("plugins", 0).toString() + juce::String::fromUTF8 ("개를 찾았습니다 (악기 ")
                       + result.getProperty ("instruments", 0).toString() + juce::String::fromUTF8 ("개). 트랙의 악기 메뉴의 찾은 플러그인에 있습니다."));
        else
            setStatus (juce::String::fromUTF8 ("플러그인을 찾지 못했습니다. 찾는 중에 멈춘 플러그인이 있을 수 있습니다."), true);
    }

    // 파일이 바뀌면 다시 불러온다 (작곡 생성기가 JSON 을 다시 쓰는 흐름). 저장하지 않은 편집이 있으면 묻는다.
    if (autoReload_ && ticks_ % 30 == 0 && project_.loaded() && ! askingAboutChange_)
    {
        const auto modified = project_.file().getLastModificationTime();
        if (modified != project_.modificationTime() && project_.file().existsAsFile())
        {
            flushInstrumentWindows();   // 악기 창에서 바꾼 소리도 저장하지 않은 편집으로 센다
            if (! project_.hasUnsavedEdits())
            {
                setStatus (juce::String::fromUTF8 ("파일이 바뀌어 다시 불러옵니다"));
                reloadProject();
            }
            else
            {
                askingAboutChange_ = true;
                juce::AlertWindow::showOkCancelBox (
                    juce::MessageBoxIconType::QuestionIcon, juce::String::fromUTF8 ("파일이 바뀌었습니다"),
                    juce::String::fromUTF8 ("다른 프로그램이 이 파일을 다시 썼습니다. 다시 불러오면 저장하지 않은 편집이 사라집니다."),
                    juce::String::fromUTF8 ("다시 불러오기"), juce::String::fromUTF8 ("편집 유지"), this,
                    juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MainComponent> (this), modified] (int result)
                    {
                        if (safe == nullptr)
                            return;
                        safe->askingAboutChange_ = false;
                        if (result == 1)
                            safe->reloadProject();
                        else
                            safe->project_.setModificationTime (modified);
                    }));
            }
        }
    }
}

juce::String MainComponent::deviceDescription() const
{
    if (audio_ == nullptr)
        return {};
    return audio_->deviceName() + ", " + juce::String (audio_->sampleRate(), 0) + " Hz, " + juce::String (audio_->bufferSize());
}

void MainComponent::paint (juce::Graphics& g) { g.fillAll (colours::background); }

void MainComponent::resized()
{
    auto r = getLocalBounds();
    transport_.setBounds (r.removeFromTop (58));
    pianoRoll_.setVisible (! mixerShown_ && ! scoreShown_);
    mixer_.setVisible (mixerShown_);
    score_.setVisible (scoreShown_ && ! mixerShown_);
    juce::Component* bottom = mixerShown_ ? static_cast<juce::Component*> (&mixer_)
                            : scoreShown_ ? static_cast<juce::Component*> (&score_) : static_cast<juce::Component*> (&pianoRoll_);
    juce::Component* parts[] = { &arrangement_, &resizer_, bottom };
    layout_.layOutComponents (parts, 3, r.getX(), r.getY(), r.getWidth(), r.getHeight(), true, true);
}

void MainComponent::showAudioSettings()
{
    if (audio_ == nullptr)
        return;
    auto* selector = new juce::AudioDeviceSelectorComponent (audio_->devices(), 0, 2, 2, 2, false, false, true, false);
    selector->setSize (520, 360);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = juce::String::fromUTF8 ("오디오 장치");
    o.dialogBackgroundColour = colours::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

// ---- 내보내기 ----

std::unique_ptr<ExportDialog> MainComponent::makeExportDialog()
{
    if (! project_.loaded())
        return nullptr;
    ExportDialog::Song song;
    song.composition = project_.composition();
    song.projectFile = project_.file();
    if (const auto range = pianoRoll_.selectionRange())
    {
        const auto meters = song.composition.meters();
        const int lastBar = meters.isBarLine (range->second) ? meters.barAt (range->second) : meters.barAt (range->second) + 1;
        song.selectedBars = std::make_pair (meters.barAt (range->first + 1.0e-9) + 1, std::max (1, lastBar));
    }
    ExportSettings defaults;
    auto directory = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    if (persist_)
    {
        auto& s = settings();
        const juce::File last (s.getValue ("exportDirectory"));
        if (juce::File::isAbsolutePath (last.getFullPathName()) && last.isDirectory())
            directory = last;
        const auto type = mr::audioFileTypeFor (juce::File ("x." + s.getValue ("exportFormat", "wav")));
        defaults.type = type.value_or (mr::AudioFileType::wav);
        defaults.bitDepth = s.getIntValue ("exportBitDepth", 24);
        defaults.quality = s.getIntValue ("exportQuality", -1);
        defaults.sampleRate = s.getDoubleValue ("exportSampleRate", 48000.0);
        defaults.stems = s.getBoolValue ("exportStems", false);
        defaults.midi = s.getBoolValue ("exportMidi", false);
        defaults.video = s.getBoolValue ("exportVideo", false) && exportableVideo (project_.composition()) != juce::File();
        defaults.videoMixOriginal = s.getBoolValue ("exportVideoSound", false);
        if (defaults.video)
            defaults.quality = s.getIntValue ("exportVideoQuality", 256);
    }
    auto dialog = std::make_unique<ExportDialog> (std::move (song), defaults, directory);
    dialog->onStart = [safe = juce::Component::SafePointer<MainComponent> (this)] (const ExportSettings& used)
    {
        if (safe == nullptr || ! safe->persist_)
            return;
        auto& s = safe->settings();
        s.setValue ("exportDirectory", used.output.getParentDirectory().getFullPathName());
        s.setValue ("exportVideo", used.video);
        if (used.video)
        {
            s.setValue ("exportVideoQuality", used.quality);
            s.setValue ("exportVideoSound", used.videoMixOriginal);
        }
        else
        {
            s.setValue ("exportFormat", juce::String (mr::audioFileTypeName (used.type)));
            if (used.type == mr::AudioFileType::wav || used.type == mr::AudioFileType::flac)
                s.setValue ("exportBitDepth", used.bitDepth);
            else
                s.setValue ("exportQuality", used.quality);
        }
        s.setValue ("exportSampleRate", used.sampleRate);
        s.setValue ("exportStems", used.stems);
        s.setValue ("exportMidi", used.midi);
        s.saveIfNeeded();
    };
    dialog->onFinished = [safe = juce::Component::SafePointer<MainComponent> (this)] (const ExportResult& r)
    {
        if (safe != nullptr && r.ok)
            safe->setStatus (juce::String::fromUTF8 ("내보냈습니다: ") + r.output.getFileName());
    };
    return dialog;
}

void MainComponent::showExportDialog()
{
    auto dialog = makeExportDialog();
    if (dialog == nullptr)
        return;
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (dialog.release());
    o.dialogTitle = juce::String::fromUTF8 ("내보내기");
    o.dialogBackgroundColour = colours::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.componentToCentreAround = this;
    o.launchAsync();
}

// ---- 새 곡 ----

std::unique_ptr<NewSongDialog> MainComponent::makeNewSongDialog()
{
    mr::Diagnostics d;
    const auto folder = mr::defaultTemplateDirectory();
    auto templates = mr::listTemplates (folder, d);
    // 기본 폴더: 지난번에 새 곡을 만든 곳, 아니면 지금 곡 폴더의 위 (곡마다 폴더를 두는 songs/), 아니면 문서 폴더
    auto directory = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    if (project_.loaded())
        directory = project_.file().getParentDirectory().getParentDirectory();
    if (persist_)
        if (const juce::File last (settings().getValue ("newSongDirectory")); juce::File::isAbsolutePath (last.getFullPathName()) && last.isDirectory())
            directory = last;
    auto dialog = std::make_unique<NewSongDialog> (std::move (templates), directory, folder.getFullPathName());
    dialog->onCreate = [safe = juce::Component::SafePointer<MainComponent> (this)] (const mr::SongTemplate& t, const juce::String& title,
                                                                                   const juce::File& file, juce::String& error)
    {
        return safe != nullptr && safe->createSong (t, title, file, error);
    };
    return dialog;
}

bool MainComponent::createSong (const mr::SongTemplate& t, const juce::String& title, const juce::File& file, juce::String& error)
{
    try
    {
        mr::createSongFromTemplate (t, title.toStdString(), file);
    }
    catch (const std::exception& e)
    {
        error = juce::String::fromUTF8 (e.what());
        return false;
    }
    if (persist_)
    {
        settings().setValue ("newSongDirectory", file.getParentDirectory().getParentDirectory().getFullPathName());
        settings().saveIfNeeded();
    }
    if (! openProject (file))
    {
        error = juce::String::fromUTF8 ("만든 곡을 열지 못했습니다.");
        return false;
    }
    setStatus (juce::String::fromUTF8 ("새 곡을 만들었습니다 (") + juce::String::fromUTF8 (t.name.c_str())
               + juce::String::fromUTF8 (" 템플릿). 피아노 롤의 빈 곳을 두 번 눌러 음표를 넣습니다."));
    return true;
}

bool MainComponent::importMidiAsSong (const juce::File& midi, const juce::File& song, juce::String& error)
{
    if (song.exists())
    {
        error = juce::String::fromUTF8 ("이미 있는 파일입니다: ") + song.getFullPathName();
        return false;
    }
    try
    {
        mr::Composition empty;
        mr::RenderOptions ro;
        mr::Diagnostics d;
        const auto registry = mr::buildRegistry (empty, ro, d);
        mr::MidiImportOptions options;
        options.registry = &registry;
        mr::MidiImportReport report;
        const auto root = mr::importMidi (midi, options, report);
        const auto parsed = mr::parseCompositionValue (root, song);
        if (! parsed.composition)
        {
            error = juce::String::fromUTF8 ("가져온 곡이 형식에 맞지 않습니다.");
            return false;
        }
        if (! song.getParentDirectory().createDirectory() || ! song.replaceWithText (juce::String::fromUTF8 ((mr::writeJson (root) + "\n").c_str())))
        {
            error = juce::String::fromUTF8 ("쓸 수 없습니다: ") + song.getFullPathName();
            return false;
        }
        if (! openProject (song))
        {
            error = juce::String::fromUTF8 ("가져온 곡을 열지 못했습니다.");
            return false;
        }
        setStatus (juce::String::fromUTF8 ("MIDI 를 가져왔습니다: 트랙 ") + juce::String (report.tracks) + juce::String::fromUTF8 ("개, 음표 ")
                   + juce::String (report.notes) + juce::String::fromUTF8 ("개")
                   + (report.warnings.empty() ? juce::String() : juce::String::fromUTF8 (" (박자 바꾸기는 첫 박자만 썼습니다)")));
        return true;
    }
    catch (const std::exception& e)
    {
        error = juce::String::fromUTF8 (e.what());
        return false;
    }
}

bool MainComponent::importMidiTracks (const juce::File& midi, juce::String& error)
{
    if (! project_.loaded() || ! registry_)
        return false;
    if (recording_)
    {
        error = juce::String::fromUTF8 ("녹음하는 동안에는 트랙을 더하지 않습니다.");
        return false;
    }
    try
    {
        mr::MidiImportOptions options;
        options.registry = &*registry_;
        mr::MidiImportReport report;
        const auto imported = mr::importMidi (midi, options, report);
        auto root = project_.documentRoot();
        const size_t first = project_.trackCount();
        for (const auto& t : *imported["tracks"].getArray())
            root = mr::withTrackInserted (root, (size_t) root["tracks"].getArray()->size(), t);   // 맨 뒤에 (겹친 이름은 번호)
        closeInstrumentWindows (true);
        juce::String editError;
        if (! project_.editRoot (root, juce::String::fromUTF8 ("MIDI 가져오기: ") + midi.getFileName(), editError))
        {
            error = editError;
            return false;
        }
        timeline_.setSelectedTrack ((int) first);
        // 가져온 템포가 지금 곡과 다르면 알린다 (음은 박 자리로 들어간다)
        const auto tempo = imported["tempo"];
        const double bpm = tempo.isArray() ? (double) tempo[0]["bpm"] : (double) tempo;
        const bool sameTempo = std::abs (project_.composition().tempo.front().bpm - bpm) < 0.01;
        setStatus (juce::String::fromUTF8 ("MIDI 의 트랙 ") + juce::String (report.tracks) + juce::String::fromUTF8 ("개를 더했습니다 (음표 ")
                   + juce::String (report.notes) + juce::String::fromUTF8 ("개)")
                   + (sameTempo ? juce::String() : juce::String::fromUTF8 (". 음은 박 자리로 들어가고 템포는 이 곡의 것을 씁니다 (MIDI ") + juce::String (bpm, 1) + " BPM)"));
        return true;
    }
    catch (const std::exception& e)
    {
        error = juce::String::fromUTF8 (e.what());
        return false;
    }
}

void MainComponent::showImportMidiDialog()
{
    askToSave ([safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe == nullptr)
            return;
        auto start = safe->project_.loaded() ? safe->project_.file().getParentDirectory()
                                             : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        safe->chooser_ = std::make_unique<juce::FileChooser> (juce::String::fromUTF8 ("가져올 MIDI 파일"), start, "*.mid;*.midi");
        safe->chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                     [safe] (const juce::FileChooser& fc)
                                     {
                                         const auto midi = fc.getResult();
                                         if (safe == nullptr || midi == juce::File())
                                             return;
                                         auto target = midi.withFileExtension ("json");
                                         safe->chooser_ = std::make_unique<juce::FileChooser> (juce::String::fromUTF8 ("새 곡을 저장할 곳"), target, "*.json");
                                         safe->chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                                                          | juce::FileBrowserComponent::warnAboutOverwriting,
                                                                      [safe, midi] (const juce::FileChooser& save)
                                                                      {
                                                                          auto song = save.getResult();
                                                                          if (safe == nullptr || song == juce::File())
                                                                              return;
                                                                          song = song.withFileExtension ("json");
                                                                          if (song.existsAsFile())
                                                                              song.deleteFile();   // 덮어쓰기는 대화상자에서 물었다
                                                                          juce::String error;
                                                                          if (! safe->importMidiAsSong (midi, song, error))
                                                                              safe->setStatus (error.replaceCharacter ('\n', ' '), true);
                                                                      });
                                     });
    });
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi") || f.endsWithIgnoreCase (".json")
            || (project_.loaded() && mr::media::isVideoFileName (f.toStdString())))
            return true;
    return false;
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& path : files)
    {
        const juce::File file (path);
        if (file.hasFileExtension ("json"))
        {
            requestOpen (file);
            return;
        }
        if (mr::media::isVideoFileName (path.toStdString()))
        {
            juce::String error;
            if (project_.loaded() && ! attachVideo (file, error))
                setStatus (error.replaceCharacter ('\n', ' '), true);
            return;
        }
        if (! file.hasFileExtension ("mid;midi"))
            continue;
        juce::String error;
        if (project_.loaded())
        {
            if (! importMidiTracks (file, error))
                setStatus (error.replaceCharacter ('\n', ' '), true);
        }
        else
        {
            const auto song = file.withFileExtension ("json");
            if (! importMidiAsSong (file, song, error))
                setStatus (error.replaceCharacter ('\n', ' '), true);
        }
    }
}

void MainComponent::showNewSongDialog()
{
    askToSave ([safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe == nullptr)
            return;
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (safe->makeNewSongDialog().release());
        o.dialogTitle = juce::String::fromUTF8 ("새 곡");
        o.dialogBackgroundColour = colours::panel;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.componentToCentreAround = safe.getComponent();
        o.launchAsync();
    });
}

// ---- 명령 ----

void MainComponent::getAllCommands (juce::Array<juce::CommandID>& c)
{
    c.addArray ({ newFile, openFile, reloadFile, toggleAutoReload, playStop, playSelection, toggleTyping, typingOctaveDown, typingOctaveUp, toStart, zoomIn, zoomOut, zoomFit, audioSettings,
                  selectPrevTrack, selectNextTrack, saveFile, saveFileAs, exportFile, undoEdit, redoEdit, cutNotes, copyNotes, pasteNotes,
                  duplicateNotes, deleteNotes, selectAllNotes, deselectNotes, quantizeNotes, octaveUp, octaveDown, nudgeLeft,
                  nudgeRight, editTempo, toggleMixer, toggleScore, exportMusicXmlCommand, instrumentWindow, scanPluginsCommand, recordToggle, loopToggle, metronomeToggle,
                  loopFromSelection, clearLoop, playSeam, newTrack, duplicateTrackCommand, deleteTrackCommand, renameTrackCommand, bounceTrackCommand, stepInputCommand, inputMonitorCommand,
                  automationWriteCommand, samplerCommand, midiLearnCommand,
                  moveTrackUp, moveTrackDown, addMarkerCommand, previousMarker, nextMarker, insertTimeCommand, deleteTimeCommand, importMidiCommand,
                  importAudioCommand, splitClipCommand, zeroCrossingCommand, clipGainCommand, attachVideoCommand, videoWindowCommand });
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    const auto file = juce::String::fromUTF8 ("파일");
    const auto play = juce::String::fromUTF8 ("재생");
    const auto view = juce::String::fromUTF8 ("보기");
    const auto edit = juce::String::fromUTF8 ("편집");
    const auto trackMenu = juce::String::fromUTF8 ("트랙");
    const bool loaded = project_.loaded();
    const bool ranged = loaded && arrangement_.range().has_value();
    const bool clipped = loaded && arrangement_.selectedClip().has_value();
    const bool selected = pianoRoll_.hasSelection();
    auto& undo = project_.undoManager();
    switch (id)
    {
        case saveFile:
            info.setInfo (juce::String::fromUTF8 ("저장"), {}, file, 0);
            info.addDefaultKeypress ('s', juce::ModifierKeys::commandModifier);
            info.setActive (loaded);
            break;
        case saveFileAs:
            info.setInfo (juce::String::fromUTF8 ("다른 이름으로 저장..."), {}, file, 0);
            info.addDefaultKeypress ('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            info.setActive (loaded);
            break;
        case newFile:
            info.setInfo (juce::String::fromUTF8 ("새 곡..."), juce::String::fromUTF8 ("템플릿에서 새 곡을 만듭니다"), file, 0);
            info.addDefaultKeypress ('n', juce::ModifierKeys::commandModifier);
            break;
        case exportFile:
            info.setInfo (juce::String::fromUTF8 ("내보내기..."), juce::String::fromUTF8 ("곡을 WAV, FLAC, Ogg Vorbis, MP3 로 내보냅니다"), file, 0);
            info.addDefaultKeypress ('e', juce::ModifierKeys::commandModifier);
            info.setActive (loaded);
            break;
        case undoEdit:
            info.setInfo (juce::String::fromUTF8 ("실행 취소")
                              + (undo.canUndo() ? juce::String::fromUTF8 (": ") + undo.getUndoDescription() : juce::String()), {}, edit, 0);
            info.addDefaultKeypress ('z', juce::ModifierKeys::commandModifier);
            info.setActive (undo.canUndo());
            break;
        case redoEdit:
            info.setInfo (juce::String::fromUTF8 ("다시 실행")
                              + (undo.canRedo() ? juce::String::fromUTF8 (": ") + undo.getRedoDescription() : juce::String()), {}, edit, 0);
            info.addDefaultKeypress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            info.setActive (undo.canRedo());
            break;
        case cutNotes:
            info.setInfo (juce::String::fromUTF8 (ranged ? "구간 잘라내기" : "잘라내기"), {}, edit, 0);
            info.addDefaultKeypress ('x', juce::ModifierKeys::commandModifier);
            info.setActive (selected || ranged);
            break;
        case copyNotes:
            info.setInfo (juce::String::fromUTF8 (ranged ? "구간 복사" : "복사"), {}, edit, 0);
            info.addDefaultKeypress ('c', juce::ModifierKeys::commandModifier);
            info.setActive (selected || ranged);
            break;
        case pasteNotes:
            info.setInfo (juce::String::fromUTF8 (clipboard_ == Clipboard::range ? (ranged ? "구간 붙이기 (고른 구간에)" : "구간 붙이기 (재생 위치에)")
                                                                                 : "재생 위치에 붙이기"), {}, edit, 0);
            info.addDefaultKeypress ('v', juce::ModifierKeys::commandModifier);
            info.setActive (clipboard_ == Clipboard::range ? rangeClip_.has_value() : pianoRoll_.canPaste());
            break;
        case duplicateNotes:
            info.setInfo (juce::String::fromUTF8 (clipped ? "클립을 바로 뒤에 복제" : ranged ? "구간을 바로 뒤에 복제" : "바로 뒤에 복제"), {}, edit, 0);
            info.addDefaultKeypress ('d', juce::ModifierKeys::commandModifier);
            info.setActive (selected || ranged || clipped);
            break;
        case deleteNotes:
            info.setInfo (juce::String::fromUTF8 (clipped ? "클립 지우기" : ranged ? "구간 내용 지우기" : stepInput_ && ! selected ? "스텝 입력: 마지막 칸 지우기" : "지우기"), {}, edit, 0);
            info.addDefaultKeypress (juce::KeyPress::backspaceKey, juce::ModifierKeys::noModifiers);
            info.addDefaultKeypress (juce::KeyPress::deleteKey, juce::ModifierKeys::noModifiers);
            info.setActive (selected || ranged || clipped || (stepInput_ && ! steps_.empty()));
            break;
        case selectAllNotes:
            info.setInfo (juce::String::fromUTF8 ("모두 고르기"), {}, edit, 0);
            info.addDefaultKeypress ('a', juce::ModifierKeys::commandModifier);
            info.setActive (loaded);
            break;
        case deselectNotes:
            info.setInfo (juce::String::fromUTF8 ("고르기 풀기"), {}, edit, 0);
            info.addDefaultKeypress (juce::KeyPress::escapeKey, juce::ModifierKeys::noModifiers);
            info.setActive (selected || ranged);
            break;
        case quantizeNotes:
            info.setInfo (juce::String::fromUTF8 ("격자에 맞추기"), {}, edit, 0);
            info.addDefaultKeypress ('q', juce::ModifierKeys::noModifiers);
            info.setActive (loaded);
            break;
        case octaveUp:
            info.setInfo (juce::String::fromUTF8 ("한 옥타브 위로"), {}, edit, 0);
            info.addDefaultKeypress (juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier);
            info.setActive (selected);
            break;
        case octaveDown:
            info.setInfo (juce::String::fromUTF8 ("한 옥타브 아래로"), {}, edit, 0);
            info.addDefaultKeypress (juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier);
            info.setActive (selected);
            break;
        case nudgeLeft:
            info.setInfo (juce::String::fromUTF8 (stepInput_ && ! selected ? "스텝 입력: 한 칸 물러서기" : "격자 한 칸 앞으로"), {}, edit, 0);
            info.addDefaultKeypress (juce::KeyPress::leftKey, juce::ModifierKeys::noModifiers);
            info.setActive (selected || stepInput_);
            break;
        case nudgeRight:
            info.setInfo (juce::String::fromUTF8 (stepInput_ && ! selected ? "스텝 입력: 쉼표 (한 칸 나아가기)" : "격자 한 칸 뒤로"), {}, edit, 0);
            info.addDefaultKeypress (juce::KeyPress::rightKey, juce::ModifierKeys::noModifiers);
            info.setActive (selected || stepInput_);
            break;
        case stepInputCommand:
            info.setInfo (juce::String::fromUTF8 ("스텝 입력"),
                          juce::String::fromUTF8 ("멈춘 동안 자판과 MIDI 건반으로 친 음을 재생 위치에 격자 길이로 넣고 한 칸 나아갑니다"), edit, 0);
            info.addDefaultKeypress ('e', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            info.setTicked (stepInput_);
            info.setActive (loaded && ! recording_);
            break;
        case editTempo:
            info.setInfo (juce::String::fromUTF8 ("템포와 박자..."), {}, edit, 0);
            info.setActive (loaded);
            break;
        case openFile:
            info.setInfo (juce::String::fromUTF8 ("열기..."), juce::String::fromUTF8 ("프로젝트 파일을 엽니다"), file, 0);
            info.addDefaultKeypress ('o', juce::ModifierKeys::commandModifier);
            break;
        case reloadFile:
            info.setInfo (juce::String::fromUTF8 ("다시 불러오기"), juce::String::fromUTF8 ("파일을 다시 읽습니다"), file, 0);
            info.addDefaultKeypress ('r', juce::ModifierKeys::commandModifier);
            info.setActive (project_.loaded());
            break;
        case toggleAutoReload:
            info.setInfo (juce::String::fromUTF8 ("바뀌면 자동으로 다시 불러오기"), {}, file, 0);
            info.setTicked (autoReload_);
            break;
        case playStop:
            info.setInfo (juce::String::fromUTF8 ("재생과 정지"), {}, play, 0);
            info.addDefaultKeypress (juce::KeyPress::spaceKey, juce::ModifierKeys::noModifiers);
            info.setActive (audio_ != nullptr && audio_->player() != nullptr);
            break;
        case playSelection:
            info.setInfo (juce::String::fromUTF8 ("고른 음표 구간 재생"),
                          juce::String::fromUTF8 ("고른 음표의 첫 시작부터 마지막 끝까지 모든 트랙으로 재생하고 처음 자리로 돌아옵니다"), play, 0);
            info.addDefaultKeypress (juce::KeyPress::spaceKey, juce::ModifierKeys::shiftModifier);
            info.setActive (audio_ != nullptr && audio_->player() != nullptr && pianoRoll_.hasSelection());
            break;
        case toggleTyping:
            info.setInfo (juce::String::fromUTF8 ("자판으로 연주"),
                          juce::String::fromUTF8 ("Z 줄과 Q 줄이 흰건반, 그 위 줄이 검은건반입니다. 켜 있으면 Q 와 X 는 단축키 대신 음을 냅니다"), play, 0);
            info.addDefaultKeypress ('k', juce::ModifierKeys::commandModifier);
            info.setTicked (typing_.isEnabled());
            break;
        case typingOctaveDown:
            info.setInfo (juce::String::fromUTF8 ("자판 연주 한 옥타브 아래"), {}, play, 0);
            info.addDefaultKeypress (juce::KeyPress::downKey, juce::ModifierKeys::altModifier);
            info.setActive (typing_.isEnabled() && typing_.base() > TypingKeyboard::lowestBase);
            break;
        case typingOctaveUp:
            info.setInfo (juce::String::fromUTF8 ("자판 연주 한 옥타브 위"), {}, play, 0);
            info.addDefaultKeypress (juce::KeyPress::upKey, juce::ModifierKeys::altModifier);
            info.setActive (typing_.isEnabled() && typing_.base() < TypingKeyboard::highestBase);
            break;
        case toStart:
            info.setInfo (juce::String::fromUTF8 ("처음으로"), {}, play, 0);
            info.addDefaultKeypress (juce::KeyPress::returnKey, juce::ModifierKeys::noModifiers);
            info.addDefaultKeypress (juce::KeyPress::homeKey, juce::ModifierKeys::noModifiers);
            break;
        case zoomIn:
            info.setInfo (juce::String::fromUTF8 ("확대"), {}, view, 0);
            info.addDefaultKeypress ('=', juce::ModifierKeys::commandModifier);
            break;
        case zoomOut:
            info.setInfo (juce::String::fromUTF8 ("축소"), {}, view, 0);
            info.addDefaultKeypress ('-', juce::ModifierKeys::commandModifier);
            break;
        case zoomFit:
            info.setInfo (juce::String::fromUTF8 ("곡 전체 보기"), {}, view, 0);
            info.addDefaultKeypress ('0', juce::ModifierKeys::commandModifier);
            break;
        case toggleMixer:
            info.setInfo (juce::String::fromUTF8 ("믹서"), juce::String::fromUTF8 ("아래 칸을 피아노 롤과 믹서로 바꿉니다"), view, 0);
            info.addDefaultKeypress ('x', juce::ModifierKeys::noModifiers);
            info.setTicked (mixerShown_);
            break;
        case toggleScore:
            info.setInfo (juce::String::fromUTF8 ("악보"), juce::String::fromUTF8 ("아래 칸을 피아노 롤과 고른 트랙의 악보로 바꿉니다"), view, 0);
            info.addDefaultKeypress ('s', juce::ModifierKeys::altModifier);
            info.setTicked (scoreShown_);
            info.setActive (loaded);
            break;
        case exportMusicXmlCommand:
            info.setInfo (juce::String::fromUTF8 ("MusicXML 내보내기..."), juce::String::fromUTF8 ("악보 프로그램이 읽는 MusicXML 로 곡의 악보를 씁니다"), file, 0);
            info.setActive (loaded);
            break;
        case instrumentWindow:
            info.setInfo (juce::String::fromUTF8 ("악기 창"), juce::String::fromUTF8 ("고른 트랙의 플러그인 악기 화면을 엽니다"),
                          juce::String::fromUTF8 ("트랙"), 0);
            info.addDefaultKeypress ('i', juce::ModifierKeys::commandModifier);
            info.setActive (loaded);
            break;
        case scanPluginsCommand:
            info.setInfo (juce::String::fromUTF8 ("플러그인 찾기 (VST3, AU)"), {}, juce::String::fromUTF8 ("트랙"), 0);
            info.setActive (scanner_ == nullptr);
            break;
        case selectPrevTrack:   // 고른 음표가 있으면 반음 올린다
            info.setInfo (juce::String::fromUTF8 (selected ? "반음 위로" : "위 트랙"), {}, selected ? edit : view, 0);
            info.addDefaultKeypress (juce::KeyPress::upKey, juce::ModifierKeys::noModifiers);
            break;
        case selectNextTrack:
            info.setInfo (juce::String::fromUTF8 (selected ? "반음 아래로" : "아래 트랙"), {}, selected ? edit : view, 0);
            info.addDefaultKeypress (juce::KeyPress::downKey, juce::ModifierKeys::noModifiers);
            break;
        case audioSettings:
            info.setInfo (juce::String::fromUTF8 ("오디오 장치..."), {}, juce::String::fromUTF8 ("설정"), 0);
            info.addDefaultKeypress (',', juce::ModifierKeys::commandModifier);
            break;
        case recordToggle:
            info.setInfo (juce::String::fromUTF8 (recording_ ? "녹음 끝내기" : "녹음"),
                          juce::String::fromUTF8 ("고른 트랙에 MIDI 입력과 자판 연주를 녹음합니다. 멈춰 있으면 카운트인 뒤 시작합니다"), play, 0);
            info.addDefaultKeypress ('r', juce::ModifierKeys::altModifier);
            info.setActive (audio_ != nullptr && audio_->player() != nullptr && loaded);
            info.setTicked (recording_);
            break;
        case loopToggle:
            info.setInfo (juce::String::fromUTF8 ("반복 구간 재생"), juce::String::fromUTF8 ("곡의 반복 구간을 되풀이합니다"), play, 0);
            info.addDefaultKeypress ('l', juce::ModifierKeys::altModifier);
            info.setActive (loaded);
            info.setTicked (looping_);
            break;
        case metronomeToggle:
            info.setInfo (juce::String::fromUTF8 ("메트로놈"), juce::String::fromUTF8 ("재생 중 박마다 소리를 냅니다 (내보내기에는 없습니다)"), play, 0);
            info.addDefaultKeypress ('m', juce::ModifierKeys::altModifier);
            info.setTicked (metronome_);
            break;
        case loopFromSelection:
            info.setInfo (juce::String::fromUTF8 ("고른 음표 구간을 반복 구간으로"), {}, edit, 0);
            info.setActive (selected);
            break;
        case clearLoop:
            info.setInfo (juce::String::fromUTF8 ("반복 구간 지우기"), {}, edit, 0);
            info.setActive (loaded && project_.loopRange().has_value());
            break;
        case playSeam:
            info.setInfo (juce::String::fromUTF8 ("반복 이음매 듣기"), juce::String::fromUTF8 ("반복 끝의 두 마디 앞부터 되풀이해 재생합니다"), play, 0);
            info.setActive (audio_ != nullptr && audio_->player() != nullptr && project_.loopRange().has_value());
            break;
        case addMarkerCommand:
            info.setInfo (juce::String::fromUTF8 ("마커 더하기 (재생 위치의 마디)"), {}, edit, 0);
            info.addDefaultKeypress ('m', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            info.setActive (loaded);
            break;
        case previousMarker:
            info.setInfo (juce::String::fromUTF8 ("앞 마커로"), {}, play, 0);
            info.addDefaultKeypress (juce::KeyPress::leftKey, juce::ModifierKeys::altModifier);
            info.setActive (loaded && ! project_.markers().empty());
            break;
        case nextMarker:
            info.setInfo (juce::String::fromUTF8 ("다음 마커로"), {}, play, 0);
            info.addDefaultKeypress (juce::KeyPress::rightKey, juce::ModifierKeys::altModifier);
            info.setActive (loaded && ! project_.markers().empty());
            break;
        case insertTimeCommand:
            info.setInfo (juce::String::fromUTF8 ("고른 구간 길이만큼 시간 넣기"), juce::String::fromUTF8 ("구간 처음에 빈 시간을 넣고 뒤를 모두 밉니다"), edit, 0);
            info.setActive (ranged && ! recording_);
            break;
        case deleteTimeCommand:
            info.setInfo (juce::String::fromUTF8 ("고른 구간의 시간 지우기"), juce::String::fromUTF8 ("모든 트랙에서 그 시간을 지우고 뒤를 당깁니다"), edit, 0);
            info.setActive (ranged && ! recording_);
            break;
        case importAudioCommand:
            info.setInfo (juce::String::fromUTF8 ("오디오 가져오기..."), juce::String::fromUTF8 ("오디오 파일을 재생 위치의 마디에 클립으로 넣습니다 (트랙 보기에 끌어 놓아도 됩니다)"), file, 0);
            info.addDefaultKeypress ('a', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            info.setActive (loaded && ! recording_);
            break;
        case attachVideoCommand:
            info.setInfo (juce::String::fromUTF8 ("영상 붙이기..."),
                          juce::String::fromUTF8 ("영상을 곡에 붙여 트랙 보기 위의 영상 레인과 영상 창으로 보며 씁니다 (창에 끌어 놓아도 됩니다)"), file, 0);
            info.setActive (loaded && ! recording_ && mr::media::videoSupported());
            break;
        case videoWindowCommand:
            info.setInfo (juce::String::fromUTF8 ("영상 창"), juce::String::fromUTF8 ("재생 위치의 영상 프레임을 따로 띄운 창에 보입니다"), view, 0);
            info.addDefaultKeypress ('v', juce::ModifierKeys::altModifier);
            info.setTicked (videoWindowShown());
            info.setActive (loaded && videoInfo_.has_value());
            break;
        case splitClipCommand:
            info.setInfo (juce::String::fromUTF8 ("클립을 재생 위치에서 나누기"), {}, edit, 0);
            info.addDefaultKeypress ('t', juce::ModifierKeys::commandModifier);
            info.setActive (loaded && arrangement_.selectedClip().has_value());
            break;
        case zeroCrossingCommand:
            info.setInfo (juce::String::fromUTF8 ("클립을 영점에서 자르기"),
                          juce::String::fromUTF8 ("클립을 나누고 양 끝을 끌어 자르는 자리를 10 ms 안의 가까운 영점 교차로 옮깁니다"), edit, 0);
            info.setTicked (arrangement_.zeroCrossingCuts);
            break;
        case clipGainCommand:
            info.setInfo (juce::String::fromUTF8 ("클립 게인 곡선 편집"),
                          juce::String::fromUTF8 ("오디오 클립의 몸통을 눌러 게인 곡선의 점을 더하고 끕니다 (⌥ 누르기는 지운다)"), view, 0);
            info.addDefaultKeypress ('g', juce::ModifierKeys::altModifier);
            info.setTicked (arrangement_.gainCurveEditing());
            info.setActive (loaded);
            break;
        case importMidiCommand:
            info.setInfo (juce::String::fromUTF8 ("MIDI 가져오기..."), juce::String::fromUTF8 ("표준 MIDI 파일을 새 곡으로 가져옵니다. 열린 곡에 더하려면 트랙 보기에 끌어 놓습니다"), file, 0);
            info.addDefaultKeypress ('i', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            break;
        case newTrack:
            info.setInfo (juce::String::fromUTF8 ("새 트랙..."), juce::String::fromUTF8 ("악기를 골라 고른 트랙 아래에 트랙을 더합니다"), trackMenu, 0);
            info.addDefaultKeypress ('t', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            info.setActive (loaded && ! recording_);
            break;
        case duplicateTrackCommand:
            info.setInfo (juce::String::fromUTF8 ("트랙 복제"), {}, trackMenu, 0);
            info.addDefaultKeypress ('d', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            info.setActive (loaded && ! recording_ && timeline_.selectedTrack() >= 0);
            break;
        case deleteTrackCommand:
            info.setInfo (juce::String::fromUTF8 ("트랙 지우기"), {}, trackMenu, 0);
            info.addDefaultKeypress (juce::KeyPress::backspaceKey, juce::ModifierKeys::commandModifier);
            info.setActive (loaded && ! recording_ && project_.trackCount() > 1);
            break;
        case renameTrackCommand:
            info.setInfo (juce::String::fromUTF8 ("트랙 이름 바꾸기"), {}, trackMenu, 0);
            info.setActive (loaded && ! recording_ && timeline_.selectedTrack() >= 0);
            break;
        case midiLearnCommand:
            info.setInfo (juce::String::fromUTF8 ("MIDI 배우기"), juce::String::fromUTF8 ("하드웨어 컨트롤러를 믹서와 효과 매개변수에 묶습니다"), view, 0);
            info.addDefaultKeypress ('l', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            info.setTicked (mixer_.learnMode());
            info.setActive (loaded);
            break;
        case samplerCommand:
            info.setInfo (juce::String::fromUTF8 ("오디오로 악기 만들기..."),
                          juce::String::fromUTF8 ("오디오 파일 하나를 건반에 펼친 악기로 만들어 새 트랙에 둡니다 (높이를 재서 맞춘다)"), trackMenu, 0);
            info.setActive (loaded && ! recording_);
            break;
        case automationWriteCommand:
            info.setInfo (juce::String::fromUTF8 ("곡선 쓰기"),
                          juce::String::fromUTF8 ("재생하며 믹서의 페이더와 밸런스를 움직이면 곡선으로 적습니다"), view, 0);
            info.addDefaultKeypress ('a', juce::ModifierKeys::altModifier);
            info.setTicked (automationWrite_);
            info.setActive (loaded);
            break;
        case inputMonitorCommand:
        {
            const int t = timeline_.selectedTrack();
            info.setInfo (juce::String::fromUTF8 ("입력 모니터링"),
                          juce::String::fromUTF8 ("고른 오디오 트랙이 입력 소리를 그 트랙의 효과, 페이더, 센드로 냅니다"), trackMenu, 0);
            info.addDefaultKeypress ('i', juce::ModifierKeys::altModifier);
            info.setTicked (t >= 0 && inputMonitorTrack() == t);
            info.setActive (loaded && t >= 0 && (size_t) t < project_.trackCount() && project_.composition().tracks[(size_t) t].instrument == "audio");
            break;
        }
        case bounceTrackCommand:
            info.setInfo (juce::String::fromUTF8 ("트랙을 오디오로 굳히기"),
                          juce::String::fromUTF8 ("고른 트랙의 악기와 효과 소리를 오디오 파일로 렌더링해 바로 아래 오디오 트랙에 넣고 원래 트랙을 뮤트합니다"),
                          trackMenu, 0);
            info.addDefaultKeypress ('b', juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier);
            info.setActive (loaded && ! recording_ && timeline_.selectedTrack() >= 0 && bounceJob_ == nullptr);
            break;
        case moveTrackUp:
            info.setInfo (juce::String::fromUTF8 ("트랙을 위로"), {}, trackMenu, 0);
            info.addDefaultKeypress (juce::KeyPress::upKey, juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier);
            info.setActive (loaded && ! recording_ && timeline_.selectedTrack() > 0);
            break;
        case moveTrackDown:
            info.setInfo (juce::String::fromUTF8 ("트랙을 아래로"), {}, trackMenu, 0);
            info.addDefaultKeypress (juce::KeyPress::downKey, juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier);
            info.setActive (loaded && ! recording_ && timeline_.selectedTrack() >= 0
                            && (size_t) timeline_.selectedTrack() + 1 < project_.trackCount());
            break;
        default:
            break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    const double centreBeat = timeline_.viewStartBeat()
                              + (arrangement_.getWidth() - ArrangementPanel::headerWidth) * 0.5 / timeline_.pixelsPerBeat();
    const double centreOffset = (arrangement_.getWidth() - ArrangementPanel::headerWidth) * 0.5;
    switch (info.commandID)
    {
        case newFile: showNewSongDialog(); return true;
        case openFile: showOpenDialog(); return true;
        case reloadFile: if (project_.loaded()) requestOpen (project_.file()); return true;
        case saveFile: saveProject(); return true;
        case saveFileAs: saveProjectAs(); return true;
        case exportFile: showExportDialog(); return true;
        case undoEdit: project_.undoManager().undo(); return true;
        case redoEdit: project_.undoManager().redo(); return true;
        case cutNotes:
            if (arrangement_.range())
                copyRange (true);
            else if (pianoRoll_.cutSelection())
                clipboard_ = Clipboard::notes;
            return true;
        case copyNotes:
            if (arrangement_.range())
                copyRange (false);
            else if (pianoRoll_.copySelection())
                clipboard_ = Clipboard::notes;
            return true;
        case pasteNotes:
            if (clipboard_ == Clipboard::range)
                pasteRange();
            else
                pianoRoll_.paste (timeline_.playheadBeat());
            return true;
        case duplicateNotes:
            if (const auto clip = arrangement_.selectedClip())
                duplicateClip (*clip);
            else if (arrangement_.range())
                duplicateRange();
            else
                pianoRoll_.duplicateSelection();
            return true;
        case deleteNotes:
            if (const auto clip = arrangement_.selectedClip())
                deleteClip (*clip);
            else if (arrangement_.range())
                clearRange();
            else if (stepInput_ && ! pianoRoll_.hasSelection())
                stepDelete();
            else
                pianoRoll_.deleteSelection();
            return true;
        case selectAllNotes: arrangement_.setRange (std::nullopt); pianoRoll_.selectAll(); return true;
        case deselectNotes: arrangement_.setRange (std::nullopt); pianoRoll_.deselect(); return true;
        case addMarkerCommand: addMarker (project_.meters().floorToBar (timeline_.playheadBeat() + 1.0e-9), {}, true); return true;
        case previousMarker: goToMarker (-1); return true;
        case nextMarker: goToMarker (1); return true;
        case insertTimeCommand:
            if (const auto r = arrangement_.range())
                insertTime (r->start, r->length());
            return true;
        case deleteTimeCommand:
            if (const auto r = arrangement_.range())
                deleteTime (r->start, r->end);
            return true;
        case quantizeNotes: pianoRoll_.quantize(); return true;
        case octaveUp: pianoRoll_.transposeSelection (12); return true;
        case octaveDown: pianoRoll_.transposeSelection (-12); return true;
        case nudgeLeft: if (stepInput_ && ! pianoRoll_.hasSelection()) stepBack(); else pianoRoll_.nudgeSelection (-1); return true;
        case nudgeRight: if (stepInput_ && ! pianoRoll_.hasSelection()) stepRest(); else pianoRoll_.nudgeSelection (1); return true;
        case stepInputCommand: setStepInput (! stepInput_); return true;
        case editTempo: showTempoDialog(); return true;
        case toggleAutoReload: autoReload_ = ! autoReload_; saveSettings(); menuItemsChanged(); return true;
        case playStop: togglePlay(); return true;
        case playSelection:
            if (auto p = player(); p != nullptr && p->playingRange())
                p->stop();
            else if (const auto range = pianoRoll_.selectionRange())
                playBeats (range->first, range->second);
            return true;
        case toStart: goToStart(); return true;
        case zoomIn: timeline_.zoomAround (1.4, centreBeat, centreOffset); return true;
        case zoomOut: timeline_.zoomAround (1.0 / 1.4, centreBeat, centreOffset); return true;
        case zoomFit: zoomToFit(); return true;
        case toggleMixer: showMixer (! mixerShown_); return true;
        case toggleScore: showScore (! scoreShown_); return true;
        case exportMusicXmlCommand:
        {
            auto chooser = std::make_shared<juce::FileChooser> (juce::String::fromUTF8 ("MusicXML 내보내기"),
                                                                project_.file().withFileExtension (".musicxml"), "*.musicxml;*.xml");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [safe = juce::Component::SafePointer<MainComponent> (this), chooser] (const juce::FileChooser& fc)
                                  {
                                      if (safe == nullptr || fc.getResult() == juce::File())
                                          return;
                                      juce::String error;
                                      if (! safe->exportMusicXml (fc.getResult(), false, error) && error.isNotEmpty())
                                          safe->setStatus (error.replaceCharacter ('\n', ' '), true);
                                  });
            return true;
        }
        case toggleTyping: setTyping (! typing_.isEnabled()); return true;
        case typingOctaveDown:
        case typingOctaveUp:
            typing_.releaseAll();
            typing_.setBase (typing_.base() + (info.commandID == typingOctaveUp ? 12 : -12));
            showTypingState();
            setStatus (juce::String::fromUTF8 ("자판 연주: Z 는 ") + juce::MidiMessage::getMidiNoteName (typing_.base(), true, true, 4)
                       + juce::String::fromUTF8 (", Q 는 ") + juce::MidiMessage::getMidiNoteName (typing_.base() + 12, true, true, 4));
            return true;
        case instrumentWindow:
            if (timeline_.selectedTrack() >= 0)
                openInstrumentWindow ((size_t) timeline_.selectedTrack());
            return true;
        case scanPluginsCommand: scanPlugins(); return true;
        case selectPrevTrack:
            if (! pianoRoll_.transposeSelection (1))
                timeline_.setSelectedTrack (std::max (0, timeline_.selectedTrack() - 1));
            return true;
        case selectNextTrack:
            if (! pianoRoll_.transposeSelection (-1))
                timeline_.setSelectedTrack (std::min ((int) project_.trackCount() - 1, timeline_.selectedTrack() + 1));
            return true;
        case audioSettings: showAudioSettings(); return true;
        case recordToggle:
            if (recording_)
                stopRecording();
            else
                startRecording();
            return true;
        case loopToggle: setLooping (! looping_); return true;
        case metronomeToggle: setMetronome (! metronome_); return true;
        case loopFromSelection:
            if (const auto range = pianoRoll_.selectionRange())
                editLoop (*range);
            return true;
        case clearLoop: editLoop (std::nullopt); return true;
        case newTrack: showNewTrackMenu(); return true;
        case importMidiCommand: showImportMidiDialog(); return true;
        case importAudioCommand: showImportAudioDialog(); return true;
        case attachVideoCommand: showAttachVideoDialog(); return true;
        case videoWindowCommand: showVideoWindow (! videoWindowShown()); return true;
        case splitClipCommand:
            if (const auto clip = arrangement_.selectedClip())
                splitClip (*clip, timeline_.playheadBeat());
            return true;
        case zeroCrossingCommand: setZeroCrossingCuts (! arrangement_.zeroCrossingCuts); return true;
        case clipGainCommand: setClipGainEditing (! arrangement_.gainCurveEditing()); return true;
        case duplicateTrackCommand: if (timeline_.selectedTrack() >= 0) duplicateTrack ((size_t) timeline_.selectedTrack()); return true;
        case deleteTrackCommand: if (timeline_.selectedTrack() >= 0) deleteTrack ((size_t) timeline_.selectedTrack()); return true;
        case renameTrackCommand: if (timeline_.selectedTrack() >= 0) arrangement_.beginRename ((size_t) timeline_.selectedTrack()); return true;
        case bounceTrackCommand: if (timeline_.selectedTrack() >= 0) bounceTrack ((size_t) timeline_.selectedTrack()); return true;
        case automationWriteCommand: setAutomationWrite (! automationWrite_); return true;
        case samplerCommand: showSamplerDialog(); return true;
        case midiLearnCommand: setMidiLearn (! mixer_.learnMode()); return true;
        case inputMonitorCommand:
            if (const int t = timeline_.selectedTrack(); t >= 0)
                setInputMonitoring ((size_t) t, inputMonitorTrack() != t);
            return true;
        case moveTrackUp:
            if (const int t = timeline_.selectedTrack(); t > 0)
                moveTrack ((size_t) t, (size_t) t - 1);
            return true;
        case moveTrackDown:
            if (const int t = timeline_.selectedTrack(); t >= 0)
                moveTrack ((size_t) t, (size_t) t + 1);
            return true;
        case playSeam:
            if (const auto loop = project_.loopRange())
            {
                setLooping (true);
                seekToBeat (std::max (loop->first, project_.meters().barStart (project_.meters().barAt (loop->second - 1.0e-6) - 1)));
                if (auto p = player())
                    p->play();
            }
            return true;
        default: return false;
    }
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { juce::String::fromUTF8 ("파일"), juce::String::fromUTF8 ("편집"), juce::String::fromUTF8 ("트랙"),
             juce::String::fromUTF8 ("재생"), juce::String::fromUTF8 ("보기"), juce::String::fromUTF8 ("설정") };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
    switch (index)
    {
        case 0:
        {
            m.addCommandItem (&commands_, newFile);
            m.addCommandItem (&commands_, openFile);
            juce::PopupMenu recentMenu;
            recent_.createPopupMenuItems (recentMenu, recentBase, true, true);
            m.addSubMenu (juce::String::fromUTF8 ("최근 프로젝트"), recentMenu, recent_.getNumFiles() > 0);
            m.addSeparator();
            m.addCommandItem (&commands_, saveFile);
            m.addCommandItem (&commands_, saveFileAs);
            m.addSeparator();
            m.addCommandItem (&commands_, importMidiCommand);
            m.addCommandItem (&commands_, importAudioCommand);
            m.addCommandItem (&commands_, attachVideoCommand);
            m.addCommandItem (&commands_, exportFile);
            m.addCommandItem (&commands_, exportMusicXmlCommand);
            m.addSeparator();
            m.addCommandItem (&commands_, reloadFile);
            m.addCommandItem (&commands_, toggleAutoReload);
            break;
        }
        case 1:
            m.addCommandItem (&commands_, undoEdit);
            m.addCommandItem (&commands_, redoEdit);
            {
                const auto& undo = project_.undoManager();
                const int n = undo.getUndoDescriptions().size() + undo.getRedoDescriptions().size();
                m.addSubMenu (juce::String::fromUTF8 ("실행 취소 기록 (") + juce::String (n) + juce::String::fromUTF8 ("개)"), historyMenu(),
                              project_.loaded() && n > 0 && ! recording_);
            }
            m.addSeparator();
            m.addCommandItem (&commands_, cutNotes);
            m.addCommandItem (&commands_, copyNotes);
            m.addCommandItem (&commands_, pasteNotes);
            m.addCommandItem (&commands_, duplicateNotes);
            m.addCommandItem (&commands_, deleteNotes);
            m.addCommandItem (&commands_, splitClipCommand);
            m.addCommandItem (&commands_, zeroCrossingCommand);
            m.addSeparator();
            m.addCommandItem (&commands_, selectAllNotes);
            m.addCommandItem (&commands_, deselectNotes);
            m.addSeparator();
            if (pianoRoll_.hasSelection())
            {
                m.addCommandItem (&commands_, selectPrevTrack);
                m.addCommandItem (&commands_, selectNextTrack);
            }
            m.addCommandItem (&commands_, octaveUp);
            m.addCommandItem (&commands_, octaveDown);
            m.addCommandItem (&commands_, nudgeLeft);
            m.addCommandItem (&commands_, nudgeRight);
            m.addCommandItem (&commands_, quantizeNotes);
            m.addCommandItem (&commands_, stepInputCommand);
            m.addSubMenu (juce::String::fromUTF8 ("음표 다듬기"), pianoRoll_.toolsMenu(), project_.loaded() && timeline_.selectedTrack() >= 0);
            m.addSubMenu (juce::String::fromUTF8 ("피아노 롤의 다른 트랙"), pianoRoll_.companionMenu(), project_.loaded() && timeline_.selectedTrack() >= 0);
            m.addSeparator();
            {
                const bool canInsert = project_.loaded() && timeline_.selectedTrack() >= 0;
                const double at = pianoRoll_.grid() > 0.0 ? std::floor (timeline_.playheadBeat() / pianoRoll_.grid() + 1.0e-9) * pianoRoll_.grid() : 0.0;
                m.addSubMenu (juce::String::fromUTF8 ("코드 넣기 (재생 위치)"), pianoRoll_.insertMenu (true, std::nullopt, at), canInsert);
                m.addSubMenu (juce::String::fromUTF8 ("스케일 넣기 (재생 위치)"), pianoRoll_.insertMenu (false, std::nullopt, at), canInsert);
            }
            m.addSeparator();
            m.addCommandItem (&commands_, loopFromSelection);
            m.addCommandItem (&commands_, clearLoop);
            m.addSeparator();
            m.addCommandItem (&commands_, addMarkerCommand);
            m.addCommandItem (&commands_, insertTimeCommand);
            m.addCommandItem (&commands_, deleteTimeCommand);
            {
                juce::PopupMenu bars;
                const auto& meters = project_.meters();
                const double at = meters.floorToBar (timeline_.playheadBeat() + 1.0e-9);
                const double barBeats = meters.barBeatsAt (at > 0.0 ? at - 1.0e-6 : 0.0);   // 넣는 자리 앞의 박자
                for (const int n : { 1, 2, 4, 8, 16 })
                    bars.addItem (juce::String (n) + juce::String::fromUTF8 ("마디"), project_.loaded() && ! recording_, false,
                                  [this, n, at, barBeats] { insertTime (at, n * barBeats); });
                m.addSubMenu (juce::String::fromUTF8 ("재생 위치의 마디에 빈 마디 넣기"), bars, project_.loaded());
                // 박자 바꾸기: 재생 위치의 마디부터
                juce::PopupMenu meter;
                const auto& seg = meters.segmentAt (at);
                for (const auto& [num, den] : std::vector<std::pair<int, int>> { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 4 }, { 7, 4 },
                                                                                { 3, 8 }, { 5, 8 }, { 6, 8 }, { 7, 8 }, { 9, 8 }, { 12, 8 }, { 2, 2 } })
                    meter.addItem (juce::String (num) + "/" + juce::String (den), project_.loaded(), seg.beatsPerBar == num && seg.beatUnit == den,
                                   [this, at, num = num, den = den] { setMeterAt (at, std::make_pair (num, den)); });
                const bool changeHere = std::any_of (project_.composition().meterChanges.begin(), project_.composition().meterChanges.end(),
                                                     [at] (const mr::MeterChange& c) { return std::abs (c.beat - at) < 1.0e-6; });
                meter.addSeparator();
                meter.addItem (juce::String::fromUTF8 ("이 마디의 박자 바꾸기 지우기"), changeHere, false, [this, at] { setMeterAt (at, std::nullopt); });
                m.addSubMenu (juce::String::fromUTF8 ("재생 위치의 마디부터 박자 (") + describeBeats (at, meters) + ")", meter, project_.loaded());
            }
            m.addSeparator();
            m.addCommandItem (&commands_, editTempo);
            break;
        case 2:
        {
            m.addCommandItem (&commands_, newTrack);
            m.addCommandItem (&commands_, duplicateTrackCommand);
            m.addCommandItem (&commands_, renameTrackCommand);
            m.addCommandItem (&commands_, bounceTrackCommand);
            m.addCommandItem (&commands_, inputMonitorCommand);
            m.addCommandItem (&commands_, samplerCommand);
            m.addCommandItem (&commands_, deleteTrackCommand);
            m.addCommandItem (&commands_, moveTrackUp);
            m.addCommandItem (&commands_, moveTrackDown);
            m.addSeparator();
            m.addCommandItem (&commands_, instrumentWindow);
            const int selected = timeline_.selectedTrack();
            m.addItem (juce::String::fromUTF8 ("악기 바꾸기..."), project_.loaded() && selected >= 0,
                       false, [this, selected] { showInstrumentMenu ((size_t) selected); });
            m.addSeparator();
            m.addCommandItem (&commands_, scanPluginsCommand);
            break;
        }
        case 3:
        {
            m.addCommandItem (&commands_, playStop);
            m.addCommandItem (&commands_, playSelection);
            m.addCommandItem (&commands_, loopToggle);
            m.addCommandItem (&commands_, playSeam);
            m.addSeparator();
            m.addCommandItem (&commands_, recordToggle);
            juce::PopupMenu countIn;
            const char* countIns[] { "없음", "1마디", "2마디" };
            for (int i = 0; i < 3; ++i)
                countIn.addItem (juce::String::fromUTF8 (countIns[i]), true, countInBars_ == i, [this, i] { setCountInBars (i); });
            m.addSubMenu (juce::String::fromUTF8 ("카운트인: ") + juce::String::fromUTF8 (countIns[countInBars_]), countIn);
            m.addItem (juce::String::fromUTF8 ("반복 구간 안에서만 녹음 (펀치)"), true, punch_, [this] { setPunch (! punch_); });
            m.addItem (juce::String::fromUTF8 ("녹음할 때 격자에 맞추기"), true, recordQuantize_, [this] { setRecordQuantize (! recordQuantize_); });
            m.addSeparator();
            m.addCommandItem (&commands_, metronomeToggle);
            juce::PopupMenu gain;
            for (const double db : { -12.0, -6.0, 0.0, 6.0 })
                gain.addItem ((db > 0 ? "+" : "") + juce::String (db, 0) + " dB", true, metronomeGainDb_ == db, [this, db]
                {
                    metronomeGainDb_ = db;
                    if (auto p = player())
                        p->setMetronomeGainDb (db);
                    saveSettings();
                });
            m.addSubMenu (juce::String::fromUTF8 ("메트로놈 음량"), gain);
            m.addSeparator();
            m.addCommandItem (&commands_, toggleTyping);
            m.addCommandItem (&commands_, typingOctaveUp);
            m.addCommandItem (&commands_, typingOctaveDown);
            m.addSeparator();
            m.addCommandItem (&commands_, toStart);
            m.addCommandItem (&commands_, previousMarker);
            m.addCommandItem (&commands_, nextMarker);
            break;
        }
        case 4:
            m.addCommandItem (&commands_, toggleMixer);
            m.addCommandItem (&commands_, toggleScore);
            m.addCommandItem (&commands_, videoWindowCommand);
            m.addCommandItem (&commands_, automationWriteCommand);
            m.addCommandItem (&commands_, midiLearnCommand);
            m.addCommandItem (&commands_, clipGainCommand);
            m.addSeparator();
            m.addCommandItem (&commands_, zoomIn);
            m.addCommandItem (&commands_, zoomOut);
            m.addCommandItem (&commands_, zoomFit);
            if (! pianoRoll_.hasSelection())
            {
                m.addSeparator();
                m.addCommandItem (&commands_, selectPrevTrack);
                m.addCommandItem (&commands_, selectNextTrack);
            }
            break;
        case 5:
            m.addCommandItem (&commands_, audioSettings);
            m.addSubMenu (juce::String::fromUTF8 ("MIDI 입력"), midiInputMenu());
            break;
        default:
            break;
    }
    return m;
}

void MainComponent::menuItemSelected (int id, int)
{
    if (id >= recentBase && id < recentBase + 100)
        requestOpen (recent_.getFile (id - recentBase));
}

} // namespace daw
