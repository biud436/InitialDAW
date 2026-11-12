// 메인 화면: 트랜스포트, 트랙 보기, 아래 칸(피아노 롤 또는 믹서), 상태 줄. 메뉴와 단축키, 재생기 만들기, 파일 감시, 편집 명령과 저장을 맡는다.
#pragma once

#include "ArrangementPanel.h"
#include "AudioEngine.h"
#include "Bouncer.h"
#include "ExportDialog.h"
#include "InstrumentWindow.h"
#include "MidiInputs.h"
#include "NewSongDialog.h"
#include "MixerPanel.h"
#include "PianoRollPanel.h"
#include "Project.h"
#include "ScorePanel.h"
#include "TransportBar.h"
#include "TypingKeyboard.h"
#include "VideoView.h"
#if JUCE_MAC
 #include "NativeKeys.h"
#endif
#include "composition/Arrange.h"
#include "instruments/Registry.h"
#include "midi/MidiRecorder.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <optional>

namespace daw
{

class MainComponent final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            public juce::ApplicationCommandTarget,
                            public juce::MenuBarModel,
                            private Project::Listener,
                            private juce::Timer
{
public:
    // withAudio=false 는 화면만 (스냅숏): 오디오 장치를 열지 않고 재생기를 만들지 않는다.
    // persist=false 면 앱 설정(최근 파일, 장치, 믹서 보기)을 쓰지 않는다 (자체 시험).
    explicit MainComponent (bool withAudio, bool persist = true);
    ~MainComponent() override;

    bool openProject (const juce::File& file);
    // 저장하지 않은 편집이 있으면 먼저 묻고 연다
    void requestOpen (const juce::File& file);
    void showOpenDialog();
    void reloadProject();

    bool hasUnsavedEdits() const noexcept { return project_.hasUnsavedEdits(); }
    // 저장하지 않은 편집이 있으면 저장할지 묻는다. 저장했거나 버리기로 했으면 proceed 를 부른다 (취소하면 부르지 않는다).
    void askToSave (std::function<void()> proceed);
    bool saveProject();
    void saveProjectAs (std::function<void (bool saved)> done = {});

    // 스냅숏용: 트랙을 고르고, 재생 위치를 두고, 곡 전체가 보이게 맞춘다
    void selectTrack (int index) { timeline_.setSelectedTrack (index); }
    int selectedTrack() const noexcept { return timeline_.selectedTrack(); }
    double playheadBeat() const noexcept { return timeline_.playheadBeat(); }
    void showLane (const std::string& key) { pianoRoll_.showLane (key); }   // 곡선의 열쇠 (cc1, mixer/gainDb, ...)
    void setPlayheadSeconds (double seconds);
    void zoomToFit();
    void showBars (double firstBar, double lastBar);   // 두 마디(1부터, 끝 마디 포함)가 화면에 꽉 차게
    const Project& project() const noexcept { return project_; }
    Project& project() noexcept { return project_; }
    PianoRollPanel& pianoRoll() noexcept { return pianoRoll_; }
    TypingKeyboard& typingKeyboard() noexcept { return typing_; }
    MixerPanel& mixer() noexcept { return mixer_; }
    void showMixer (bool show);
    bool mixerShown() const noexcept { return mixerShown_; }
    void showScore (bool show);   // 아래 칸에 고른 트랙의 악보 (믹서를 닫는다)
    bool scoreShown() const noexcept { return scoreShown_; }
    ScorePanel& scorePanel() noexcept { return score_; }
    // 파일 > MusicXML 내보내기: 고른 트랙(없으면 오디오 트랙을 뺀 모든 트랙)의 악보. 실패하면 error
    bool exportMusicXml (const juce::File& file, bool selectedOnly, juce::String& error);
    // 스냅숏용: 오디오 장치 없이 재생기를 만들어 seconds 부터 잠깐 돌리고 그 미터 값을 믹서에 둔다
    void primeMeters (double seconds);

    // 악기 창: 트랙의 악기가 플러그인이면 그 화면을 띄운다 (이미 열렸으면 앞으로). 아니면 상태 줄에 알리고 false.
    bool openInstrumentWindow (size_t track);
    void showInstrumentMenu (size_t track);
    // 트랙의 악기를 바꾼다 (편집 하나). definition 이 있으면 곡 안의 정의로 함께 더한다. 플러그인이면 준비된 뒤 창을 연다.
    bool changeInstrument (size_t track, const std::string& name, const juce::var& definition = {});
    void scanPlugins();   // 앱을 따로 실행해 VST3 와 AU 를 찾는다 (플러그인 캐시)

    // 효과 칸의 플러그인 창 (재생기의 그 플러그인). 재생이 준비되지 않았으면 false
    bool openEffectWindow (ChannelRef channel, size_t effect);
    static std::string effectOwner (ChannelRef channel);   // 재생기의 효과 칸 이름: track:<번호>, bus:<번호>, master

    // ---- 트랙 목록 (편집 하나씩, 녹음 중에는 하지 않는다) ----
    // 악기의 트랙을 at 자리(없으면 맨 뒤)에 더하고 고른다. 이름은 악기 이름에서 (겹치면 번호)
    bool addTrack (const std::string& instrument, const juce::var& definition = {}, std::optional<size_t> at = std::nullopt);
    bool duplicateTrack (size_t track);
    // 트랙을 오디오로 굳히기: 뒤 스레드에서 렌더링하고 끝나면 finishBounce. 이미 굳히는 중이면 false
    bool bounceTrack (size_t track);
    // 입력 모니터링: 오디오 트랙이 입력 소리를 그 트랙의 효과, 페이더, 센드로 낸다 (한 번에 한 트랙). 입력이 꺼져 있으면 켠다
    bool setInputMonitoring (size_t track, bool on);
    // 곡선 쓰기: 켜면 재생 중에 믹서의 페이더와 밸런스를 움직인 것이 곡선으로 적힌다
    void setAutomationWrite (bool on);
    // MIDI 배우기 모드 (믹서를 보이고 덮개를 씌운다)
    void setMidiLearn (bool on);
    MidiLearn& midiLearn() noexcept { return *learn_; }
    bool automationWrite() const noexcept { return automationWrite_; }
    // 트랙의 출력 (비면 마스터): 서브그룹 버스로 보낸다. 트랙 여럿을 편집 하나로
    bool setTrackOutput (const std::vector<size_t>& tracks, const std::string& bus);
    // 폴더 (비면 폴더에서 뺀다). 트랙 여럿을 편집 하나로
    bool setTrackFolder (const std::vector<size_t>& tracks, const std::string& folder);
    // 폴더의 트랙을 같은 이름의 버스로 보낸다 (버스가 없으면 만든다)
    bool routeFolderToBus (const std::string& folder);
    bool renameFolder (const std::string& folder, const std::string& name);
    std::vector<size_t> folderTracks (const std::string& folder) const;
    int inputMonitorTrack() const;   // 없으면 -1
    bool bouncing() const { return bounceJob_ != nullptr; }
    // 굳힌 결과를 곡에 넣는다 (원래 트랙 뒤의 오디오 트랙, 원래 트랙은 뮤트). 편집 하나
    bool finishBounce (const std::string& trackName, const BounceOutcome& outcome);
    bool deleteTrack (size_t track);   // 마지막 하나는 지우지 않는다
    bool renameTrack (size_t track, const juce::String& name);   // 사이드체인 키도 따라간다
    bool moveTrack (size_t from, size_t to);
    bool setTrackColour (size_t track, const juce::String& hex);   // "#rrggbb", 비면 기본 색
    void showTrackMenu (size_t track);   // 트랙 머리의 오른쪽 클릭 메뉴
    void showNewTrackMenu();             // 악기를 골라 고른 트랙 아래에 새 트랙
    static juce::String trackNameForInstrument (const std::string& instrument);

    // ---- 마커 (편집 하나씩) ----
    bool addMarker (double beat, const juce::String& name, bool rename);   // 이름이 비면 "마커 N". rename 이면 이름 칸을 연다
    bool moveMarker (size_t marker, double beat);
    bool renameMarker (size_t marker, const juce::String& name);
    bool deleteMarker (size_t marker);
    bool goToMarker (int direction);   // 재생 위치의 앞(-1)이나 뒤(+1) 마커로
    void showMarkerMenu (int marker, double beat);
    // ---- 구간 (마커에서 다음 마커까지, 편집 하나씩) ----
    bool moveSection (size_t section, size_t before);   // before 번째 구간 앞으로 (구간 수면 맨 뒤로)
    bool duplicateSection (size_t section);              // 바로 뒤에
    bool deleteSection (size_t section);
    void showSectionMenu (size_t section);

    // ---- 트랙 보기의 구간 (고른 트랙들의 박 범위) ----
    bool copyRange (bool cut);
    bool pasteRange();      // 고른 구간의 처음, 없으면 재생 위치에 (그 자리의 내용은 지우고)
    bool duplicateRange();  // 구간 바로 뒤에, 붙인 구간을 고른다
    bool clearRange();
    bool insertTime (double at, double length);   // 곡 전체에 빈 시간을 넣는다
    bool deleteTime (double from, double to);     // 곡 전체에서 그 시간을 지운다
    static juce::String describeBeats (double beat, const mr::MeterMap& meters);   // "5마디", "5.3"
    // 재생 위치의 마디부터 박자를 바꾼다 (곡의 처음이면 처음 박자). 그 마디의 박자 바꾸기를 지우려면 setMeterAt 에 nullopt
    bool setMeterAt (double beat, std::optional<std::pair<int, int>> meter);
    // 편집 메뉴의 실행 취소 기록: 처음 상태, 지난 편집(오래된 것부터), 지금 자리(표시), 다시 실행할 편집. 고르면 그 자리로
    juce::PopupMenu historyMenu();
    // 스텝 입력: 멈춘 동안 자판과 MIDI 건반으로 친 음을 재생 위치에 격자 길이로 넣고 한 칸 나아간다. 함께 누른 음은 모두 뗄 때 한 칸에.
    // → 는 쉼표(한 칸 나아가기), ← 는 한 칸 물러서기, 백스페이스는 마지막 칸을 지우고 물러선다 (고른 음표가 없을 때)
    void setStepInput (bool on);
    bool stepInput() const noexcept { return stepInput_; }
    double stepCursor() const;
    void stepNote (int pitch, int velocity, bool on);
    bool stepRest();
    bool stepBack();
    bool stepDelete();
    bool jumpHistory (int steps);
    static const std::vector<std::pair<juce::String, juce::String>>& trackColourChoices();   // 이름, "#rrggbb"
    std::vector<std::string> articulationsOf (size_t track) const;   // 트랙 악기의 아티큘레이션 이름
    std::map<int, juce::String> keyNamesOf (size_t track) const;      // 트랙 악기의 건반 이름 (드럼과 타악기)
    // 트랙의 악기가 내장 신스면 그 패널 (창에 넣지 않은 것, 스냅숏과 시험도 쓴다). 아니면 nullptr.
    std::unique_ptr<SynthPanel> makeSynthPanel (size_t track);
    // 내보내기 대화상자 (지금 문서, 앱 설정의 마지막 값). 창이나 스냅숏이 담는다.
    std::unique_ptr<ExportDialog> makeExportDialog();
    void showExportDialog();
    // 새 곡 대화상자 (템플릿 목록). 만들면 그 곡을 연다.
    std::unique_ptr<NewSongDialog> makeNewSongDialog();
    void showNewSongDialog();
    // ---- 오디오 트랙과 클립 (편집 하나씩) ----
    // 오디오 파일을 곡 폴더의 audio/ 에 복사하고 (이미 곡 폴더 안이면 그대로) 클립으로 넣는다. track 이 오디오 트랙이면 그 트랙에,
    // 아니면 파일마다 새 오디오 트랙을 고른 트랙 아래에. beat 는 클립의 시작
    bool importAudio (const juce::StringArray& files, std::optional<size_t> track, double beat, juce::String& error);
    void showImportAudioDialog();
    bool addAudioTrack (std::optional<size_t> at = std::nullopt);   // 빈 오디오 트랙
    bool editClip (ArrangementPanel::ClipRef ref, const mr::AudioClip& edited, bool copy = false);
    bool deleteClip (ArrangementPanel::ClipRef ref);
    bool duplicateClip (ArrangementPanel::ClipRef ref);   // 바로 뒤에
    bool splitClip (ArrangementPanel::ClipRef ref, double beat);
    // 테이크 고르기 (컴핑): 이 클립이 그 자리에서 들리고 겹친 다른 클립은 그 자리에서 뮤트된다. 트랙 보기의 고른 구간이 이 클립과 겹치면 그 구간만
    // (경계에서 클립을 나눈다). 편집 하나
    bool useTake (ArrangementPanel::ClipRef ref);
    // 퀵 샘플러: 오디오 파일 (그 한 구간)을 건반에 펼친 악기로. 곡 폴더의 samples/ 에 SFZ 를 쓰고 곡 안의 악기 정의와 그 악기의 트랙을 더한다 (편집 하나)
    bool makeSamplerTrack (const juce::File& audio, double startSeconds, std::optional<double> lengthSeconds, bool oneShot, juce::String& error);
    void showSamplerDialog();
    // 멜로디 받아 적기: 클립(파일의 그 구간)의 단선율을 음표로 받아 적어 클립 아래 새 트랙에 둔다 (편집 하나)
    bool transcribeClip (ArrangementPanel::ClipRef ref, juce::String& error);
    // 어택으로 조각내기: 클립의 어택마다 조각을 건반(C2 부터)에 둔 SFZ 와, 그 조각을 클립의 자리에서 치는 새 트랙 (편집 하나)
    bool sliceClip (ArrangementPanel::ClipRef ref, juce::String& error);
    // 템포 재기: 클립이 재생하는 구간의 템포를 재서 sourceBpm 으로 (곡 템포를 따라 늘고 준다)
    bool detectClipTempo (ArrangementPanel::ClipRef ref, juce::String& error);
    // 곡의 템포를 이 클립의 연주에 맞춘다: 박마다 템포 점, 클립은 늘이지 않고 첫 박을 마디 머리에 (편집 하나)
    bool followClipTempo (ArrangementPanel::ClipRef ref, juce::String& error);
    // 오디오 퀀타이즈: 클립의 어택을 격자(박)로 끌어오는 워프 마커 (세기 0..1). 편집 하나
    bool quantizeClipAudio (ArrangementPanel::ClipRef ref, double grid, double strength, juce::String& error, std::optional<mr::Groove> groove = {});
    // 클립의 어택(16분음표 격자에서 어긋난 만큼과 크기)으로 한 마디 그루브를 내 그루브 폴더에 저장한다
    bool saveGrooveFromClip (ArrangementPanel::ClipRef ref, const juce::String& name, juce::String& error);
    bool setClipMuted (ArrangementPanel::ClipRef ref, bool muted);
    bool setClipGain (ArrangementPanel::ClipRef ref, double gainDb);
    bool normalizeClip (ArrangementPanel::ClipRef ref);   // 봉우리가 -1 dBFS 가 되는 게인
    // 거꾸로 재생을 켜고 끈다 (같은 자리에서 같은 소리 조각을 거꾸로)
    bool reverseClip (ArrangementPanel::ClipRef ref);
    bool setClipFadeShape (ArrangementPanel::ClipRef ref, bool fadeIn, mr::FadeShape shape);
    // 시작 순서로 바로 뒤의 들리는 클립과 겹치거나 맞붙었으면 그 사이의 크로스페이드 (맞붙었으면 앞뒤 10 ms 씩 늘인다)
    bool crossfadeClip (ArrangementPanel::ClipRef ref);
    std::optional<ArrangementPanel::ClipRef> nextClip (ArrangementPanel::ClipRef ref) const;
    bool clearClipGainCurve (ArrangementPanel::ClipRef ref);
    // 트랙 보기의 클립 게인 곡선 편집 (켜면 클립 몸통을 눌러 점을 더하고 끈다)
    void setClipGainEditing (bool on);
    // 클립을 나누고 자르는 자리를 가까운 영점 교차로 (앱 설정)
    void setZeroCrossingCuts (bool on);
    bool zeroCrossingCuts() const noexcept { return arrangement_.zeroCrossingCuts; }
    void showClipMenu (ArrangementPanel::ClipRef ref);
    std::unique_ptr<juce::AlertWindow> grooveDialog_;

    // ---- 트랙 템플릿 ----
    // 트랙의 소리와 처리(악기, 효과, 믹서, 센드, 출력, 색)를 앱 데이터 폴더의 InitialDAW/TrackTemplates/<이름>.json 에 저장하고, 새 트랙을 템플릿으로 더한다
    juce::File trackTemplateFolder;   // 비면 기본 템플릿만
    bool saveTrackTemplate (size_t track, const juce::String& name, juce::String& error);
    // 기본 템플릿 다음에 내 템플릿 (화면 이름, 템플릿)
    std::vector<std::pair<juce::String, juce::var>> trackTemplates() const;
    bool addTrackFromTemplate (const juce::var& templ, std::optional<size_t> at = std::nullopt);
    std::unique_ptr<juce::AlertWindow> templateDialog_;
    std::optional<mr::AudioClip> clipAt (ArrangementPanel::ClipRef ref) const;

    // ---- 자동 저장 ----
    // 저장하지 않은 편집이 있으면 2분마다 앱 데이터 폴더(곡 파일 곁이 아니다)에 곡 사본을 쓴다. 저장하거나 버리면 지운다.
    // 곡을 열 때 그 곡보다 새로운 사본이 있으면 되살릴지 묻는다 (되살리기는 편집 하나, 저장은 사람이 한다)
    static constexpr double autosaveSeconds = 120.0;
    void setAutosaveDirectory (const juce::File& dir) { autosaveDir_ = dir; }   // 비우면 하지 않는다 (persist=false 의 기본)
    juce::File autosaveFile() const;    // 지금 곡의 사본 자리 (곡이 없거나 자동 저장을 끄면 빈 File)
    bool autosaveNow();                 // 저장하지 않은 편집이 있으면 쓴다
    bool hasNewerAutosave() const;      // 열린 곡 파일보다 새롭고 내용이 다른 사본이 있다
    bool restoreAutosave();             // 사본의 내용을 편집 하나로 넣는다
    void discardAutosave();

    // ---- 영상 (곡의 video, 소리에는 쓰지 않는다) ----
    // 영상을 붙인다 (곡 폴더 안이면 상대 경로, 밖이면 절대 경로). 있던 영상은 바꾸고 자리는 곡의 0초로. 편집 하나
    bool attachVideo (const juce::File& file, juce::String& error);
    bool setVideoOffset (double offsetSeconds);   // 영상 0초가 놓일 곡의 초
    bool detachVideo();
    void showAttachVideoDialog();
    void showVideoMenu();
    // 장면 바뀜 후보를 찾는다 (뒤 스레드, 끝나면 영상 레인에 보인다). wait 이면 이 스레드에서 끝까지 (시험)
    bool findSceneChanges (bool wait = false);
    // 장면 바뀜 자리(영상의 초)마다 마커 "장면 N" (곡 앞은 빼고, 같은 박의 마커가 있으면 두지 않는다). 편집 하나
    bool addSceneMarkers (const std::vector<double>& videoSeconds);
    // 영상의 소리를 곡 폴더의 audio/ 에 WAV 로 꺼내 영상 자리에 놓인 오디오 트랙으로 (편집 하나)
    bool importVideoSound (juce::String& error);
    void showVideoWindow (bool show);
    bool videoWindowShown() const noexcept { return videoWindow_ != nullptr && videoWindow_->isVisible(); }
    VideoWindow* videoWindow() noexcept { return videoWindow_.get(); }
    const std::optional<mr::media::VideoInfo>& videoInfo() const noexcept { return videoInfo_; }

    // ---- MIDI 가져오기 ----
    // MIDI 파일을 새 곡 JSON 으로 써서 연다 (있는 파일은 덮어쓰지 않는다)
    bool importMidiAsSong (const juce::File& midi, const juce::File& song, juce::String& error);
    // MIDI 파일의 트랙을 지금 곡의 맨 뒤에 더한다 (편집 하나). 템포와 박자는 지금 곡의 것을 쓴다
    bool importMidiTracks (const juce::File& midi, juce::String& error);
    void showImportMidiDialog();   // MIDI 파일을 고르고 저장할 곳을 정해 새 곡으로

    // 끌어 놓기: .mid 는 열린 곡에 트랙으로 (곡이 없으면 새 곡으로), .json 은 연다
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // 템플릿에서 곡을 만들어 연다. 실패하면 false 와 문구.
    bool createSong (const mr::SongTemplate& songTemplate, const juce::String& title, const juce::File& file, juce::String& error);

    // ---- 녹음, 반복, 메트로놈 ----
    // MIDI 입력(또는 시험)이 받은 메시지: 고른 트랙으로 들려주고, 녹음 중이면 녹음기에 넣는다. positionSample 은 받은 순간의 재생 샘플.
    void liveMidi (const juce::MidiMessage& message, int64_t positionSample);
    // 멈춰 있으면 카운트인 뒤 재생 위치부터, 재생 중이면 바로 고른 트랙에 녹음한다. 재생기가 없거나 트랙이 없으면 false
    bool startRecording();
    // 녹음을 끝내고 음표와 곡선을 편집 하나로 트랙에 넣는다 (keep=false 면 버린다). 넣었으면 true
    bool stopRecording (bool keep = true);
    bool isRecording() const noexcept { return recording_; }
    void setCountInBars (int bars);
    int countInBars() const noexcept { return countInBars_; }
    bool setLooping (bool on);   // 곡에 반복 구간이 없으면 켜지 않고 false
    bool looping() const noexcept { return looping_; }
    void setMetronome (bool on);
    bool metronome() const noexcept { return metronome_; }
    void setPunch (bool on) { punch_ = on; saveSettings(); }
    void setRecordQuantize (bool on) { recordQuantize_ = on; saveSettings(); }
    // 반복 구간을 바꾸는 편집 (비면 지운다). 상태 줄에 알린다.
    bool editLoop (const std::optional<std::pair<double, double>>& range);
    MidiInputs* midiInputs() const noexcept { return midi_.get(); }
    ArrangementPanel& arrangement() noexcept { return arrangement_; }
    // 오디오 트랙의 녹음: 입력을 곡 폴더의 audio/<트랙> take N.wav 에 쓰고, 끝나면 클립 하나로 넣는다 (편집 하나)
    bool startAudioRecording (size_t track);
    bool isRecordingAudio() const noexcept { return audioRecording_; }
    // 테이크를 클립으로: 첫 샘플의 곡 위치에서 지연(재생기 출력 + 장치 왕복)을 빼 제자리에 놓는다
    static bool applyAudioTake (Project& project, size_t track, const mr::AudioTake& take, int latencySamples, juce::String& error);
    static juce::File nextTakeFile (const Project& project, size_t track);
    // 녹음한 것을 트랙에 넣는다 (편집 하나). 곡선은 있던 곡선에 점을 더한다. selected 는 넣은 음표.
    static bool applyTake (Project& project, size_t track, const mr::RecordedTake& take, mr::NoteSelection& selected, juce::String& error);

    // 자체 시험용 (창 없이 앱의 재생 경로를 돌린다)
    std::shared_ptr<mr::RealtimePlayer> player() const { return audio_ != nullptr ? audio_->player() : nullptr; }
    void setMonitorGainDb (double db) { transport_.setMonitorGainDb (db); }
    void play() { if (auto p = player()) p->play(); }
    void stop() { if (auto p = player()) p->stop(); }
    // 그 박 구간만 모든 트랙으로 재생하고 끝에서 멈춘 뒤 재생 위치를 구간의 처음으로 돌린다. 재생기가 없으면 false
    bool playBeats (double fromBeat, double toBeat);
    juce::String deviceDescription() const;
    bool loudnessMeasured() const noexcept { return loudnessMeasured_; }
    double monitorGainDb() const noexcept { return monitorGainDb_; }

    void paint (juce::Graphics& g) override;
    void resized() override;

    // ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info) override;
    bool perform (const InvocationInfo& info) override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int index, const juce::String& name) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    juce::ApplicationCommandManager& commands() noexcept { return commands_; }

private:
    enum CommandIDs
    {
        openFile = 0x2001,
        newFile = 0x2100,
        reloadFile,
        toggleAutoReload,
        playStop,
        playSelection,
        toggleTyping,
        typingOctaveDown,
        typingOctaveUp,
        toStart,
        zoomIn,
        zoomOut,
        zoomFit,
        audioSettings,
        selectPrevTrack,
        selectNextTrack,
        saveFile,
        saveFileAs,
        exportFile,
        undoEdit,
        redoEdit,
        cutNotes,
        copyNotes,
        pasteNotes,
        duplicateNotes,
        deleteNotes,
        selectAllNotes,
        deselectNotes,
        quantizeNotes,
        octaveUp,
        octaveDown,
        nudgeLeft,
        nudgeRight,
        editTempo,
        toggleMixer,
        toggleScore,
        exportMusicXmlCommand,
        instrumentWindow,
        scanPluginsCommand,
        recordToggle,
        loopToggle,
        metronomeToggle,
        loopFromSelection,
        clearLoop,
        playSeam,
        newTrack,
        duplicateTrackCommand,
        deleteTrackCommand,
        renameTrackCommand,
        bounceTrackCommand,
        stepInputCommand,
        inputMonitorCommand,
        automationWriteCommand,
        samplerCommand,
        midiLearnCommand,
        moveTrackUp,
        moveTrackDown,
        addMarkerCommand,
        previousMarker,
        nextMarker,
        insertTimeCommand,
        deleteTimeCommand,
        importMidiCommand,
        importAudioCommand,
        splitClipCommand,
        zeroCrossingCommand,
        clipGainCommand,
        attachVideoCommand,
        videoWindowCommand
    };
    static constexpr int recentBase = 0x3000;

    class Loader;
    class LoudnessJob;

    struct InstrumentChoice
    {
        std::string name;
        juce::var definition;   // 곡 안에 더할 정의 (찾은 플러그인), 레지스트리 악기는 void
    };
    // 악기 고르기 하위 메뉴 넷 (내장 신스, 샘플 악기, 플러그인, 찾은 플러그인). 항목 번호는 base + choices 의 번호
    void addInstrumentMenus (juce::PopupMenu& m, std::vector<InstrumentChoice>& choices, int base, const std::string& current) const;
    // 트랙 목록을 바꾼 곡 트리를 편집 하나로 넣는다 (열린 악기 창은 닫는다). selectAfter >= 0 이면 그 트랙을 고른다
    bool editTracks (const juce::var& root, const juce::String& name, int selectAfter);
    bool editMarkers (const juce::var& markers, const juce::String& name);
    // 구간 편집의 곡 트리를 편집 하나로 넣고 select 구간을 고른다
    bool editRange (const juce::var& root, const juce::String& name, std::optional<ArrangementPanel::Range> select);
    bool pasteClipAt (const mr::ArrangeClip& clip, size_t firstTrack, double at, const juce::String& name);
    enum class Clipboard { notes, range };
    Clipboard clipboard_ = Clipboard::notes;   // 마지막으로 복사한 것
    std::optional<mr::ArrangeClip> rangeClip_;

    void timerCallback() override;
    void projectEdited (int track) override;
    void refreshVideo();       // 곡의 영상이 바뀌었으면 레인과 창에 알린다
    void showSceneChanges (const std::vector<mr::media::SceneChange>& scenes);
    void updateVideoWindow();  // 영상 창을 재생 위치로
    std::unique_ptr<juce::Thread> sceneJob_;   // 장면 바뀜 찾기
    std::unique_ptr<VideoWindow> videoWindow_;
    std::optional<mr::media::VideoInfo> videoInfo_;
    juce::File videoInfoFile_;
    juce::Time videoInfoTime_;
    juce::String checkTrack (size_t index, const mr::TrackSpec& spec) const;
    void showTempoDialog();
    void rebuildPlayer();
    void previewOnPlayer (const mr::Composition& composition);   // 믹서를 끄는 동안 (문서는 그대로)
    void toggleTrackFlag (size_t index, bool solo);
    std::vector<size_t> closeInstrumentWindows (bool capture, bool pluginsOnly = false);   // 닫은 창의 트랙
    void checkSynthWindows();
    void flushInstrumentWindows();                               // 열린 창의 바뀐 소리를 곡에 쓴다
    void updateMeters();
    void playerReady (std::shared_ptr<mr::RealtimePlayer> player, const juce::String& error, int generation,
                      double resumeSeconds, bool resumePlaying);
    void startLoudnessAnalysis();
    void loudnessReady (double gainDb, const juce::String& error, int generation);
    void setMonitorGainQuietly (double db);   // 앱이 정하는 값 (사람이 고친 것으로 치지 않는다)
    void seekToBeat (double beat);
    void togglePlay();
    void applyTransportToPlayer (mr::RealtimePlayer& player);   // 반복 구간, 반복, 메트로놈
    double currentBeat() const;                                   // 재생기의 지금 위치 (박)
    void showHeldNotes();                                         // 자판과 MIDI 로 누른 음을 건반에
    juce::PopupMenu midiInputMenu();
    void goToStart();
    void updateTitle();
    void updateTransport();
    void setStatus (const juce::String& text, bool error = false);
    void showAudioSettings();
    void saveSettings();
    juce::PropertiesFile& settings();

    const bool withAudio_;
    const bool persist_;
    Project project_;
    TimelineState timeline_;
    std::unique_ptr<AudioEngine> audio_;
    std::unique_ptr<MidiInputs> midi_;
    std::unique_ptr<juce::ApplicationProperties> properties_;
    juce::RecentlyOpenedFilesList recent_;
    juce::ApplicationCommandManager commands_;

    TransportBar transport_;
    ArrangementPanel arrangement_;
    PianoRollPanel pianoRoll_;
    TypingKeyboard typing_;
    std::vector<std::string> trackNames_;   // 마지막으로 본 트랙 목록 (목록이 바뀐 편집을 알아본다)
    std::map<int, size_t> typedTracks_;   // 자판으로 누른 음 -> 누를 때 고른 트랙 (트랙을 바꿔도 그 트랙에서 끈다)
#if JUCE_MAC
    std::unique_ptr<NativeKeys> nativeKeys_;
#endif
    MixerPanel mixer_;
    ScorePanel score_ { project_, timeline_ };
    bool scoreShown_ = false;
    bool mixerShown_ = false;
    std::map<size_t, std::unique_ptr<InstrumentWindow>> instrumentWindows_;
    std::map<std::pair<std::string, size_t>, std::unique_ptr<InstrumentWindow>> effectWindows_;   // (효과 칸 이름, 번호)
    void checkEffectWindows();   // 사슬이 바뀌어 다른 플러그인이 된 창은 닫는다
    std::vector<size_t> pendingInstrumentWindows_;   // 재생기가 준비되면 열 악기 창
    std::unique_ptr<juce::ChildProcess> scanner_;
    juce::StretchableLayoutManager layout_;
    juce::StretchableLayoutResizerBar resizer_ { &layout_, 1, false };
    juce::TooltipWindow tooltips_ { this, 500 };
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<juce::AlertWindow> tempoDialog_;
    std::optional<mr::Registry> registry_;   // 편집을 검사할 악기 정의 (프로젝트를 열 때 만든다)
    bool askingAboutChange_ = false;

    std::unique_ptr<Loader> loader_;
    std::vector<std::unique_ptr<Loader>> retired_;   // 다시 불러와서 결과를 버릴 로더 (끝나기를 기다려 지운다)
    std::unique_ptr<LoudnessJob> loudness_;
    bool monitorTouched_ = false;      // 사람이 모니터 게인을 고쳤다 (재어 맞추지 않는다)
    int loudnessGeneration_ = 0;
    bool loudnessMeasured_ = false;
    bool settingMonitor_ = false;
    int generation_ = 0;
    bool autoReload_ = true;
    void setTyping (bool on);
    void showTypingState();
    double localSeconds_ = 0.0;      // 재생기가 없을 때의 재생 위치
    std::optional<double> rangeReturnBeat_;   // 구간 재생이 끝나면 돌아갈 자리
    double monitorGainDb_ = 0.0;
    int ticks_ = 0;

    juce::File autosaveDir_;
    std::string lastAutosaved_;   // 마지막으로 쓴 사본 (같으면 다시 쓰지 않는다)
    double lastAutosaveMs_ = 0.0;
    void offerAutosave();         // 열린 곡보다 새로운 사본이 있으면 묻는다

    bool stopAudioRecording (bool keep);
    mr::AudioTakeRecorder audioRecorder_;
    bool audioRecording_ = false;
    mr::MidiRecorder recorder_;
    bool recording_ = false;
    size_t recordTrack_ = 0;
    std::map<int, size_t> midiTracks_;   // MIDI 입력으로 누른 음 -> 트랙
    int countInBars_ = 1;
    bool looping_ = false;
    bool metronome_ = false;
    double metronomeGainDb_ = -6.0;
    bool punch_ = false;
    bool recordQuantize_ = false;
    std::unique_ptr<BounceJob> bounceJob_;
    struct Step   // 스텝 입력으로 넣은 칸 (쉼표는 음이 없다)
    {
        size_t track = 0;
        double beat = 0.0, length = 0.0;
        std::vector<int> pitches;
    };
    bool stepInput_ = false;
    double stepCursor_ = 0.0;   // 다음 칸의 박
    double stepShown_ = -1.0;   // 스텝 입력이 마지막으로 옮긴 재생 위치 (다르면 사람이 옮겼다)
    std::set<int> stepHeld_;
    std::vector<std::pair<int, int>> stepChord_;   // 이번 칸의 (음높이, 세기)
    std::vector<Step> steps_;
    void moveStepCursor (double beat);
    std::string bounceTrackName_;
    std::string monitorTrackName_;
    bool automationWrite_ = false;
    std::unique_ptr<MidiLearn> learn_;
    // 퀵 샘플러와 조각내기의 소리 자리: 곡 폴더의 samples/ (밖의 파일은 복사), 겹치지 않는 악기 이름과 SFZ
    struct SampleTarget
    {
        juce::File sample, sfz;
        std::string name;
    };
    std::optional<SampleTarget> prepareSampleTarget (const juce::File& audio, const juce::String& suffix, juce::String& error);
    juce::var withInlineInstrument (const juce::var& root, const SampleTarget& target, const juce::String& description) const;   // 입력 모니터링 중인 트랙 (이름으로: 순서가 바뀌어도 따라간다)
    void applyInputMonitor();
    void showFolderMenu (const std::string& folder);
    std::unique_ptr<juce::AlertWindow> folderDialog_;
};

} // namespace daw
