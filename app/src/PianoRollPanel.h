// 피아노 롤: 고른 트랙의 음표를 음높이 격자에 그리고 고친다. 아래 칸은 벨로시티와 컨트롤러 곡선. 가로축은 트랙 보기와 같다.
// 끌기는 미리 보여 주기만 하고, 마우스를 뗄 때 편집 하나로 프로젝트에 넣는다 (실행 취소 한 번에 되돌아간다).
#pragma once

#include "analysis/NoteSegments.h"
#include "composition/Groove.h"
#include "AutomationTargets.h"
#include "HarmonyMenu.h"
#include "Project.h"
#include "TimelineDrawing.h"
#include "composition/NoteEdits.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>

namespace daw
{

struct TimelineGeometry;

class PianoRollPanel final : public juce::Component,
                             private TimelineState::Listener,
                             private Project::Listener,
                             private juce::ScrollBar::Listener
{
public:
    static constexpr int leftWidth = 230;      // 트랙 보기의 머리 너비와 같다 (가로축이 맞는다)
    static constexpr int keyboardWidth = 64;
    static constexpr int rulerHeight = 24;
    static constexpr int laneHeight = 110;
    static constexpr int scrollBarSize = 10;

    PianoRollPanel (Project& project, TimelineState& timeline);
    ~PianoRollPanel() override;

    void projectChanged();
    std::function<void (double beat)> onSeek;
    std::function<void()> onPressed;   // 마우스를 눌렀다 (트랙 보기의 구간 고르기를 푼다)
    std::function<void (const juce::String& text, bool error)> onStatus;
    std::function<std::vector<std::string> (size_t track)> articulationsFor;   // 악기 프로필의 아티큘레이션
    std::function<std::map<int, juce::String> (size_t track)> keyNamesFor;     // 악기 정의의 건반 이름 (드럼과 타악기)
    const std::map<int, juce::String>& keyNames() const noexcept { return keyNames_; }
    // 누른 음을 바로 들려준다 (건반, 음표 누르기와 옮기기, 새 음): 트랙, 곡의 음높이, 벨로시티(0 은 끄기), 아티큘레이션, 채널
    std::function<void (size_t track, int pitch, int velocity, const std::string& articulation, int channel)> onAudition;
    std::function<void (std::optional<std::pair<double, double>>)> onLoopEdited;   // 눈금자에서 반복 구간을 끌어 놓았다 (비면 지운다)

    // ---- 여러 트랙 ----
    // 다른 트랙의 음을 그 트랙 색으로 흐리게 보인다 (보기). 함께 고치는 트랙은 고르기, 옮기기, 길이, 지우기, 조옮김, 음표 다듬기가 넘어간다
    // (실행 취소 한 번). 새 음, 벨로시티, 복사와 붙여 넣기는 보이는 트랙만. 흐린 음을 두 번 누르면 그 트랙을 보인다.
    // 트랙 이름으로 기억한다 (화면 상태, 곡에 쓰지 않는다)
    enum class Companion { none, shown, edited };
    void setCompanion (size_t track, Companion mode);
    Companion companion (size_t track) const;
    std::vector<size_t> companionTracks (Companion mode) const;   // 그 방식인 트랙 (보이는 트랙과 오디오 트랙은 빼고, 트랙 순서)
    const std::map<size_t, mr::NoteSelection>& otherSelection() const noexcept { return others_; }
    juce::PopupMenu companionMenu();
    // 그 박의 조(곡의 key, keyChanges)의 음계에 든 음인가 (조가 적히지 않았으면 false). 피아노 롤이 그 줄을 밝게 그린다
    bool inKey (double beat, int pitch) const;

    // ---- 오디오 트랙의 음 ----
    // 오디오 트랙을 보이면 클립마다 녹음의 음 덩어리(가운데 높이의 막대와 높이 곡선)를 높이 줄에 그린다. 음은 뒤 스레드에서 파일마다 한 번 잰다.
    // 위아래로 끌면 가운데 높이가 반음에 맞고 (⌥ 는 센트 단위), 고른 음 위의 손잡이로 펴기와 비브라토, 양 끝 끌기는 워프 마커를 옮긴다
    struct AudioNoteRef
    {
        size_t clip = 0, note = 0;   // 클립 번호, 클립이 트는 음 가운데 번호 (0 부터)
        bool operator== (const AudioNoteRef& o) const { return clip == o.clip && note == o.note; }
    };
    std::optional<AudioNoteRef> selectedAudioNote() const noexcept { return audioNote_; }
    void selectAudioNote (std::optional<AudioNoteRef> ref);
    void measureAudioNotes();                                 // 보이는 오디오 트랙의 음을 지금 잰다 (시험과 스냅숏)
    size_t audioNoteCount (size_t clip) const;                // 잰 음의 수 (아직 재지 않았으면 0)
    const mr::NoteSegment* audioNoteSegment (AudioNoteRef ref) const;
    std::optional<mr::PitchEdit> audioNoteEdit (AudioNoteRef ref) const;
    // 그 음의 편집을 바꾼다 (nullopt 는 되돌리기). 편집 하나
    bool setAudioNoteEdit (AudioNoteRef ref, std::optional<mr::PitchEdit> edit, const juce::String& name);
    bool shiftAudioNote (AudioNoteRef ref, int semitones);   // 반음씩 (↑↓)
    bool snapAudioNote (AudioNoteRef ref);                   // 가운데 높이를 가까운 반음에
    bool moveAudioNoteEdge (AudioNoteRef ref, bool end, double songBeat);   // 음의 시작이나 끝을 곡의 그 박으로 (워프 마커)
    juce::Point<float> audioNotePoint (AudioNoteRef ref, float xFraction) const;   // 막대 가운데 줄의 자리 (0 왼쪽 끝, 1 오른쪽 끝)
    juce::Point<float> audioNoteHandle (AudioNoteRef ref, bool vibrato) const;    // 고른 음 위의 손잡이 (펴기, 비브라토)

    // 편집 명령 (메뉴와 단축키가 부른다). 할 일이 없으면 false.
    bool hasSelection() const noexcept;
    std::optional<std::pair<double, double>> selectionRange() const;   // 고른 음표의 첫 시작부터 마지막 끝까지 (박)
    void setTypingKeys (std::map<int, juce::String> labels);   // 건반에 자판 글자를 쓴다 (비우면 지운다)
    void setTypedNotes (std::vector<int> pitches);             // 자판과 MIDI 입력으로 누르고 있는 음을 건반에 칠한다
    void setRecordingNotes (std::vector<mr::Note> notes);      // 녹음 중인 음표 (아직 곡에 없다)
    void setSelection (mr::NoteSelection selection);           // 고른 음표를 바꾼다 (녹음한 음)
    size_t selectionSize() const noexcept;   // 함께 고치는 트랙의 고른 음 포함
    bool canPaste() const;
    bool selectAll();
    void deselect();
    bool deleteSelection();
    bool copySelection();
    bool cutSelection();
    bool paste (double atBeat);
    bool duplicateSelection();
    bool transposeSelection (int semitones);
    bool nudgeSelection (int gridSteps);
    bool quantize();
    // 음표 다듬기 (고른 음표, 없으면 트랙의 모든 음표). 편집 하나
    bool quantizeWith (const mr::QuantizeOptions& options, const juce::String& name);
    bool scaleVelocities (double factor);              // 평균을 가운데로 넓히거나 좁힌다
    bool rampVelocities (bool up);                      // 크레셴도(가장 여린 값에서 가장 센 값으로)나 디미누엔도
    bool humanize (int velocity, double milliseconds);  // 세기 ±velocity, 시작 ±ms (곡의 템포로 박으로 바꾼다)
    bool legato();                                      // 다음 음의 시작까지
    bool setLengths (double beats);
    bool snapToScale (int root, const std::string& scaleId);
    bool retrograde();
    bool invert();
    bool arpeggiate (mr::ArpeggioOrder order);          // 화음을 지금 격자마다 한 음씩
    bool strum (bool upward, double milliseconds);      // 화음의 음을 ms 씩 늦춘다 (곡의 템포로 박으로)
    // 그루브: 고른 음표(비면 모두)의 시작을 그루브 자리로 timing 만큼, 세기를 그 칸의 강세 쪽으로 velocity 만큼
    bool applyGroove (const mr::Groove& groove, double timing, double velocity);
    // 고른 음표(비면 모두)에서 지금 격자로 한 마디 그루브를 뽑아 grooveFolder 에 저장한다
    bool saveGrooveFromNotes (const juce::String& name, juce::String& error);
    juce::File grooveFolder;                            // 내 그루브 (비면 기본 그루브만)
    juce::PopupMenu toolsMenu();                        // 위의 도구 (편집 메뉴와 음표 오른쪽 클릭)
    bool setArticulation (const std::string& articulation);   // 고른 음표 (빈 문자열은 트랙 기본값)
    double grid() const noexcept { return grid_; }
    // 드럼 보기: 이름 있는 건반과 쓰인 건반만 줄로 (위에서 아래로 높은 음부터), 음표는 친 자리의 마름모, 빈 칸을 누르면 그 칸에 친다
    bool drumView() const noexcept { return ! rows_.empty(); }
    bool canUseDrumView() const noexcept { return ! keyNames_.empty(); }
    void setDrumView (bool on);
    const std::vector<int>& drumRows() const noexcept { return rows_; }
    void setGrid (double beats);   // 격자 상자의 항목 가운데 같은 값을 고른다

    // 코드와 스케일 넣기 (빈 곳 오른쪽 클릭, 편집 메뉴). 넣은 음표를 고르고 들려준다. 할 일이 없거나 넣지 못하면 false.
    bool insertChord (int rootPitch, const std::string& chordId, double atBeat);
    bool insertScale (int rootPitch, const std::string& scaleId, double atBeat);
    // rootPitch 가 없으면 근음(C4~B4)부터 고르는 메뉴
    juce::PopupMenu insertMenu (bool chords, std::optional<int> rootPitch, double atBeat);
    std::shared_ptr<HarmonyOptions> harmonyOptions() const { return harmony_; }
    // 아래 칸: 비면 벨로시티, 아니면 곡선의 열쇠 (cc1, mixer/gainDb, mixer/pan, sends/<버스>, effects/<번호>:<종류>/<매개변수>)
    const std::string& laneShown() const noexcept { return laneKey_; }
    void showLane (const std::string& key);              // 그 곡선이 있으면 보인다 (비면 벨로시티)
    // 곡선을 더한다 (그 대상의 지금 값 한 점). 오디오 트랙의 볼륨처럼 처음 그리는 곡선. 더했으면 true
    bool addLane (const std::string& key);

    // 화면 위치 (편집 시험이 마우스를 흉내 낼 때 쓴다): 음높이 줄의 가운데, 아래 칸 값 영역의 비율(0 아래, 1 위)
    juce::Point<float> notePoint (double beat, int pitch) const;
    juce::Point<float> lanePoint (double beat, float fraction) const;
    juce::Point<float> keyPoint (int pitch, float fraction) const;   // 건반의 가로 비율(0 왼쪽, 1 오른쪽)

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify (const juce::MouseEvent& e, float scaleFactor) override;

private:
    enum class Drag { none, ruler, loop, keys, move, resize, band, velocity, point, audioPitch, audioStart, audioEnd, audioFlatten, audioVibrato };

    void timelineChanged() override;
    void projectEdited (int track) override;
    void scrollBarMoved (juce::ScrollBar* bar, double newRangeStart) override;

    int track() const;   // 보이는 트랙, 없으면 -1
    bool audioTrack() const;   // 보이는 트랙이 오디오 트랙 (음표를 고치지 않는다)
    void centreOnTrack();
    void updateScrollBars();
    void refreshLaneBox();
    void pruneSelection();
    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> keyboardArea() const;
    int keyVelocity (float x) const;   // 건반의 오른쪽을 누를수록 세게
    void audition (int pitch, int velocity, const std::string& articulation = {}, int channel = 1, int onTrack = -1);   // 들리는 음을 바꾼다
    void auditionBriefly (int pitch, int velocity, const std::string& articulation = {}, int channel = 1);
    void stopAudition();
    juce::Rectangle<int> laneArea() const;
    juce::Rectangle<float> laneValueArea() const;
    float pitchY (int pitch) const;
    int pitchAt (float y) const;
    // 세로 자리: 피아노 보기는 음높이, 드럼 보기는 127 - 줄 번호 (맨 위 줄이 127)
    int virtualOf (int pitch) const;
    int pitchOfVirtual (int v) const;
    int lowestVirtual() const noexcept { return rows_.empty() ? 0 : 128 - (int) rows_.size(); }
    int movedPitch (int pitch, int delta) const;   // 세로로 delta 칸 (드럼 보기는 줄 단위)
    void rebuildRows();
    void scrollToUsedRows();
    double snapRound (double beat) const;
    double snapFloor (double beat) const;
    bool isSelected (size_t index) const;
    juce::Rectangle<float> noteRect (const mr::Note& n, const TimelineGeometry& geo) const;
    int noteAt (juce::Point<float> p, bool& onRightEdge) const;
    int velocityBarAt (juce::Point<float> p) const;
    int pointAt (juce::Point<float> p) const;
    double laneValueAt (float y) const;
    float laneY (double value) const;
    std::optional<mr::AutomationLane> currentLane() const;   // 보이는 곡선 (없으면 nullopt)
    std::optional<LaneTarget> currentTarget() const;

    bool commitNotes (const mr::NoteEditResult& result, const juce::String& name);
    // 트랙 여럿의 편집 결과를 편집 하나로 (보이는 트랙 하나뿐이면 commitNotes). 고른 음은 결과의 것으로
    using TrackEdits = std::vector<std::pair<size_t, mr::NoteEditResult>>;
    bool commitTracks (TrackEdits results, const juce::String& name);
    // 고른 음이 있는 트랙마다 (보이는 트랙과 함께 고치는 트랙) edit 를 부른다. 고른 음이 없으면 false
    bool editSelection (const std::function<mr::NoteEditResult (const std::vector<mr::EditNote>&, const mr::NoteSelection&)>& edit,
                        const juce::String& name);
    bool editable (size_t track) const;   // 함께 고치는 트랙인가
    struct Hit
    {
        size_t track = 0, note = 0;
        bool edge = false;   // 오른쪽 끝 (길이)
    };
    // 그 자리의 음: 보이는 트랙, 함께 고치는 트랙, (shown 이면) 흐리게 보는 트랙 순서
    std::optional<Hit> hitNote (juce::Point<float> p, bool shown) const;
    // 다듬기 도구 하나를 고른 음표(없으면 모두)에: 바뀐 것이 없으면 알리고 false
    bool applyTool (const std::function<mr::NoteEditResult (const std::vector<mr::EditNote>&, const mr::NoteSelection&)>& tool,
                    const juce::String& name);
    bool commitLane (const std::string& key, const mr::AutomationLane& lane, const juce::String& name);
    void showCurveShapeMenu (int point);
    void showAddCurveMenu();
    void showArticulationMenu();
    void showInsertMenu (int pitch, double beat);
    void auditionChord (const std::vector<int>& pitches, int velocity);
    void status (const juce::String& text, bool error = false) { if (onStatus) onStatus (text, error); }
    std::unique_ptr<juce::AlertWindow> grooveDialog_;   // 그루브 이름

    struct ShownNote   // 그린 음표: 가사를 그 위에 쓴다
    {
        juce::Rectangle<float> rect;
        std::string lyric;
        juce::String label;   // 높이 이름 (아티큘레이션 포함)
        float alpha = 1.0f;
    };
    void paintNotes (juce::Graphics& g, const TimelineGeometry& geo);
    void paintLyrics (juce::Graphics& g, const std::vector<ShownNote>& shown, const juce::Font& pitchFont);
    void paintKeyboard (juce::Graphics& g, juce::Rectangle<int> area);
    void paintInfo (juce::Graphics& g, juce::Rectangle<int> area);
    void paintLane (juce::Graphics& g);
    void paintVelocities (juce::Graphics& g, juce::Rectangle<int> lane, const TimelineGeometry& geo);
    void paintCurve (juce::Graphics& g, juce::Rectangle<int> lane, const TimelineGeometry& geo);

    Project& project_;
    TimelineState& timeline_;
    int shownTrack_ = -1;
    float keyHeight_ = 12.0f;
    double topPitch_ = 84.0;     // 격자 맨 위 줄의 음높이 (실수: 부드럽게 스크롤)
    std::string laneKey_;                  // 아래 칸에 보이는 곡선 (비면 벨로시티)
    std::vector<std::string> laneItems_;   // 칸 상자의 곡선 항목 (번호 - laneItemBase)  // 아래 칸에 보이는 것: 벨로시티 또는 컨트롤러 번호

    mr::NoteSelection selection_;
    mr::NoteSelection bandBase_;   // 사각형 고르기를 시작할 때의 선택 (⇧ 로 더할 때)
    std::map<size_t, mr::NoteSelection> others_;      // 함께 고치는 트랙의 고른 음
    std::map<size_t, mr::NoteSelection> othersBase_;  // 사각형 고르기를 시작할 때
    std::map<std::string, Companion> companions_;     // 트랙 이름마다 보기 방식
    int anchorTrack_ = -1;                            // 끌기를 시작한 음의 트랙
    juce::TextButton companionButton_;
    Drag drag_ = Drag::none;
    int anchor_ = -1;              // 끌기를 시작한 음
    double dragBeats_ = 0.0;       // 미리보기: 옮기는 박 또는 늘이는 길이
    int dragPitch_ = 0;
    bool dragCopy_ = false;
    bool dragMoved_ = false;
    bool pendingToggle_ = false;   // ⇧ 클릭한 고른 음: 끌지 않으면 뗄 때 선택에서 뺀다
    juce::Rectangle<float> band_;
    int velocityValue_ = 0;        // 미리보기: 끄는 막대의 세기
    int pointIndex_ = -1;
    mr::AutomationLane laneEdit_;  // 미리보기: 끄는 점이 반영된 곡선
    bool committing_ = false;
    int auditionPitch_ = -1;       // 들려주고 있는 음 (곡의 음높이)
    int auditionTrack_ = -1;
    int auditionToken_ = 0;        // 짧게 들려준 음을 끌 때 그 사이에 새로 누른 음은 두기 위한 번호
    std::map<int, juce::String> keyNames_;   // 보이는 트랙 악기의 건반 이름
    std::vector<int> rows_;                  // 드럼 보기의 줄 (비면 피아노 보기)
    std::map<std::string, bool> drumChoice_; // 트랙 이름마다 사람이 고른 보기 (없으면 건반 이름이 있으면 드럼 보기)
    int clickAdded_ = -1;                    // 드럼 보기에서 클릭으로 막 넣은 음 (이어지는 두 번 클릭이 지우지 않게)
    juce::TextButton drumButton_;
    void refreshKeyNames();
    std::map<int, juce::String> typingLabels_;
    std::vector<int> typedNotes_;
    std::vector<mr::Note> recordingNotes_;
    LoopRulerDrag loopDrag_;

    uint64_t humanizeCount_ = 0;
    double grid_ = 0.25;
    double lastLength_ = 0.25;     // 새 음의 길이 (마지막으로 누른 음의 길이)
    int lastVelocity_ = 100;
    std::shared_ptr<HarmonyOptions> harmony_ = std::make_shared<HarmonyOptions>();

    juce::ComboBox gridBox_;
    juce::ComboBox laneBox_;
    juce::ScrollBar vertical_ { true };
    juce::ScrollBar horizontal_ { false };

    // 오디오 트랙의 음 (PianoRollAudio.cpp)
    struct AudioNote
    {
        AudioNoteRef ref;
        std::shared_ptr<const std::vector<mr::NoteSegment>> owner;
        const mr::NoteSegment* segment = nullptr;
        std::optional<mr::PitchEdit> edit;
        double startBeat = 0.0, endBeat = 0.0;   // 곡의 박 (클립이 트는 구간으로 자른다)
        double heard = 0.0;                      // 들리는 가운데 높이 (클립의 음높이와 편집 포함)
    };
    juce::File songFolder() const;
    std::shared_ptr<const std::vector<mr::NoteSegment>> notesOfClip (const mr::AudioClip& clip) const;   // 클립 소리의 파일의 음, 없으면 뒤 스레드에 맡긴다
    std::vector<AudioNote> audioNotes() const;
    std::optional<AudioNote> findAudioNote (AudioNoteRef ref) const;
    float centreY (double midi) const;   // 소수 높이의 줄 가운데
    juce::Rectangle<float> audioNoteRect (const AudioNote& n, const TimelineGeometry& geo) const;
    std::optional<AudioNoteRef> audioNoteAt (juce::Point<float> p, int& edge) const;   // edge: -1 시작, 1 끝, 0 몸통
    int audioHandleAt (juce::Point<float> p) const;   // 0 펴기, 1 비브라토, -1 없음
    mr::PitchEdit editOrDefault (const AudioNote& n) const;
    bool commitClip (size_t clip, const mr::AudioClip& edited, const juce::String& name);
    void paintAudioNotes (juce::Graphics& g, const TimelineGeometry& geo);
    void paintAudioInfo (juce::Graphics& g, juce::Rectangle<int>& r);
    void showAudioNoteMenu (AudioNoteRef ref);
    void audioMouseDown (const juce::MouseEvent& e);
    void audioMouseDrag (const juce::MouseEvent& e);
    void audioMouseUp (Drag drag);
    void centreOnAudioNotes();

    std::optional<AudioNoteRef> audioNote_;
    mr::PitchEdit audioPreview_;     // 끄는 동안의 편집
    double edgePreview_ = 0.0;       // 끄는 동안의 경계 (곡의 박)
    bool audioCentred_ = false;      // 잰 음의 높이로 화면을 맞췄다
    mutable std::set<juce::String> measuring_;   // 뒤 스레드에서 재는 파일
    std::unique_ptr<juce::ThreadPool> measurePool_ = std::make_unique<juce::ThreadPool> (1);   // 맨 먼저 지운다 (일이 끝나기를 기다린다)
};

} // namespace daw
