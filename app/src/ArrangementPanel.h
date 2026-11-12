// 트랙 보기: 왼쪽은 트랙 머리(이름, 악기, 뮤트, 솔로, 레벨), 오른쪽은 눈금자와 트랙마다 음표와 다이내믹스 곡선.
#pragma once

#include "Project.h"
#include "TimelineDrawing.h"
#include "VideoView.h"
#include "composition/Arrange.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace daw
{

class ArrangementPanel final : public juce::Component,
                               public juce::FileDragAndDropTarget,
                               private TimelineState::Listener,
                               private juce::ScrollBar::Listener
{
public:
    static constexpr int headerWidth = 230;
    static constexpr int timeRulerHeight = 30;                      // 마디 번호와 반복 구간
    static constexpr int markerHeight = 20;                         // 그 아래의 마커 띠
    static constexpr int sectionHeight = 20;                        // 그 아래의 구간 띠 (마커에서 다음 마커까지)
    static constexpr int rulerHeight = timeRulerHeight + markerHeight + sectionHeight;   // 레인이 시작하는 높이
    static constexpr int laneHeight = 60;
    static constexpr int addRowHeight = 48;   // 트랙 아래의 더하기 단추 줄
    static constexpr int scrollBarSize = 10;

    ArrangementPanel (Project& project, TimelineState& timeline);
    ~ArrangementPanel() override;

    void projectChanged();
    void tracksChanged();  // 트랙 목록이 바뀌었다 (보기 자리는 그대로)
    void refreshFlags();   // 뮤트와 솔로를 프로젝트에서 다시 읽는다
    void setTrackFlags (size_t index, bool mute, bool solo);
    void setTrackDescription (size_t index, const juce::String& description);
    void setInputMonitorTrack (int index);   // 입력 모니터링 중인 트랙 (-1 은 없음)
    // 폴더: 같은 폴더 이름이 이어진 트랙 위의 머리 줄. 접으면 그 트랙들을 감춘다 (화면 상태, 곡에 쓰지 않는다)
    static constexpr int folderHeight = 26;
    void refreshLayout();                                       // 트랙의 폴더가 바뀌었다
    void setFolderCollapsed (const std::string& folder, bool collapsed);
    bool folderCollapsed (const std::string& folder) const { return collapsed_.count (folder) > 0; }
    juce::Rectangle<int> folderHeaderBounds (const std::string& folder) const;   // 첫 머리 줄 (없으면 빈 사각형)
    bool trackHidden (size_t index) const;
    std::function<void (const std::string& folder)> onFolderMenu;   // 폴더 머리를 오른쪽 클릭했다
    void setLevels (const std::vector<float>& peaks);   // 트랙 순서, 0..1 선형

    std::function<void (double beat)> onSeek;
    std::function<void (size_t index, bool mute)> onMute;
    std::function<void (size_t index, bool solo)> onSolo;
    std::function<void (size_t index)> onOpenInstrument;   // 트랙 머리(이름 밖)를 두 번 클릭했다
    std::function<void (size_t index)> onTrackMenu;        // 트랙 머리를 오른쪽 클릭했다
    std::function<void (size_t index, const juce::String& name)> onRenameTrack;   // 이름 칸에서 고친 이름
    std::function<void (size_t from, size_t to)> onMoveTrack;                     // 트랙 머리를 끌어 놓았다
    std::function<void()> onAddTrack;                                             // 트랙 아래의 더하기 단추

    // 마커 띠: 두 번 누르면 그 마디에 마커를 더하고, 마커를 끌어 옮기고, 마커를 두 번 누르면 이름을 고친다
    std::function<void (double beat)> onAddMarker;
    std::function<void (size_t marker, double beat)> onMoveMarker;
    std::function<void (size_t marker, const juce::String& name)> onRenameMarker;
    std::function<void (int marker, double beat)> onMarkerMenu;   // 오른쪽 클릭 (-1 은 빈 곳)
    void beginMarkerRename (size_t marker);
    juce::Rectangle<int> markerStrip() const;
    std::optional<size_t> markerAt (juce::Point<int> p) const;

    // 구간 띠: 마커에서 다음 마커까지의 덩어리. 끌어 놓으면 그 자리 앞으로 옮기고 (onSectionMoved, before 는 구간 번호, 구간 수면 맨 뒤),
    // 누르면 그 처음으로, 두 번 누르면 모든 트랙의 그 구간을 고르고, 오른쪽 클릭은 onSectionMenu
    juce::Rectangle<int> sectionStrip() const;
    const std::vector<mr::Section>& sections() const noexcept { return sections_; }
    float sectionX (size_t section, float fraction) const;   // 구간 덩어리 안의 가로 비율 자리 (시험이 마우스를 흉내 낼 때)
    std::function<void (size_t section, size_t before)> onSectionMoved;
    std::function<void (size_t section)> onSectionMenu;

    // 레인을 끌어 고른 구간: 트랙 범위와 박 범위 (격자에 맞춘다). 바뀌면 onRangeChanged
    struct Range
    {
        size_t firstTrack = 0, lastTrack = 0;
        double start = 0.0, end = 0.0;
        bool operator== (const Range& o) const
        {
            return firstTrack == o.firstTrack && lastTrack == o.lastTrack && start == o.start && end == o.end;
        }
        std::vector<size_t> tracks() const;
        double length() const noexcept { return end - start; }
    };
    const std::optional<Range>& range() const noexcept { return range_; }
    void setRange (std::optional<Range> range);
    std::function<void()> onRangeChanged;
    double rangeSnap() const;   // 구간을 맞추는 격자 (박): 넓게 보면 그 자리의 마디 길이, 아니면 1 박
    bool snapsToBars() const;   // 넓게 보면 마디(박자 지도)에 맞춘다
    double snapFloor (double beat) const;   // 격자의 앞 줄
    double snapCeil (double beat) const;    // 격자의 뒤 줄
    double snapRound (double beat) const;   // 가까운 줄
    juce::Point<float> lanePoint (size_t track, double beat) const;   // 그 트랙 레인의 가운데 높이 (시험이 마우스를 흉내 낼 때)

    // ---- 오디오 클립 ----
    // 오디오 트랙(악기 audio)의 레인에는 클립이 파형과 함께 보인다. 몸통을 끌면 옮기고(⌥ 는 사본), 양 끝을 끌면 자르고,
    // 위 모서리의 손잡이를 끌면 페이드. 격자(구간 고르기와 같다)에 맞추고 ⌘ 는 맞추지 않는다. 놓을 때 편집 하나.
    // 아래 모서리의 삼각형은 워프 마커: 끌면 그 자리의 소리를 옮기고(이웃 마커 사이가 늘고 준다), ⌥ 누르기는 지우고, 클립을 두 번 누르면 더한다
    // 게인 곡선 편집을 켜면 몸통을 눌러 곡선의 점을 더하고 끌며(위가 +12 dB, 아래가 -36 dB), ⌥ 누르기는 점을 지운다 (클립은 옮기지 않는다)
    struct ClipRef
    {
        size_t track = 0, clip = 0;
        bool operator== (const ClipRef& o) const { return track == o.track && clip == o.clip; }
        bool operator!= (const ClipRef& o) const { return ! (*this == o); }
    };
    bool isAudioTrack (size_t track) const;
    const std::optional<ClipRef>& selectedClip() const noexcept { return selectedClip_; }
    void setSelectedClip (std::optional<ClipRef> clip);
    juce::Rectangle<float> clipBounds (size_t track, size_t clip) const;   // 화면의 클립 (시험이 마우스를 흉내 낼 때)
    juce::Point<float> warpMarkerPoint (size_t track, size_t clip, size_t marker) const;   // 워프 마커의 손잡이 (시험이 마우스를 흉내 낼 때)
    void setGainCurveEditing (bool on);
    bool gainCurveEditing() const noexcept { return gainEditing_; }
    // 클립 안에서 파일의 초와 데시벨이 놓인 화면의 점 (시험이 마우스를 흉내 낼 때)
    juce::Point<float> gainCurvePoint (size_t track, size_t clip, double sourceSeconds, double db) const;
    bool zeroCrossingCuts = false;   // 양 끝을 끌어 자른 자리를 가까운 영점 교차로
    std::function<void (ClipRef ref, const mr::AudioClip& edited, bool copy)> onClipEdited;
    std::function<void (ClipRef ref)> onClipMenu;        // 클립을 오른쪽 클릭했다
    std::function<void()> onClipSelected;                // 클립을 골랐다 (구간과 음표 선택을 푼다)
    std::function<void (const juce::StringArray& files, std::optional<size_t> track, double beat)> onDropAudio;   // 오디오 파일을 놓았다

    // 오디오 녹음 중인 자리 (빨간 띠와 입력 봉우리)
    struct RecordingRegion
    {
        size_t track = 0;
        double start = 0.0, end = 0.0;
        float peak = 0.0f;
    };
    void setRecordingRegion (std::optional<RecordingRegion> region);

    // ---- 영상 레인 (곡에 영상이 있을 때 마커 띠 아래): 타임코드 줄과 섬네일, 장면 바뀜 후보 ----
    // 몸통을 끌면 영상의 자리를 옮기고 (격자에 맞추고, ⌘ 는 프레임에 맞춘다) 놓을 때 onVideoMoved. 후보를 두 번 누르면 onSceneMarker
    static constexpr int videoHeight = 58;
    static constexpr int videoTimecodeHeight = 16;
    struct VideoLane
    {
        juce::File file;
        double offsetSeconds = 0.0;     // 영상 0초가 놓인 곡의 초
        double durationSeconds = 0.0;
        mr::media::FrameRate rate;
        int width = 0, height = 0;
    };
    void setVideo (std::optional<VideoLane> video, bool synchronousThumbnails = false);   // 비면 레인을 감춘다
    const std::optional<VideoLane>& video() const noexcept { return video_; }
    void setSceneChanges (std::vector<double> videoSeconds);
    const std::vector<double>& sceneChanges() const noexcept { return scenes_; }
    juce::Rectangle<int> videoLaneBounds() const;   // 영상이 없으면 빈 사각형
    int lanesTop() const noexcept { return rulerHeight + (video_ ? videoHeight : 0); }   // 트랙 레인이 시작하는 높이
    float videoX (double videoSeconds) const;       // 영상의 초가 놓인 x (시험이 마우스를 흉내 낼 때)
    std::function<void (double offsetSeconds)> onVideoMoved;
    std::function<void()> onVideoMenu;
    std::function<void (double videoSeconds)> onSceneMarker;

    // 트랙 이름을 고치는 칸을 연다 (리턴은 고치고, 이스케이프는 그만둔다)
    void beginRename (size_t index);
    void finishRename (bool commit);   // 이름 칸을 닫는다 (commit 이면 고친 이름을 알린다)
    juce::TextEditor* renameEditor() const noexcept { return renameEditor_.get(); }
    juce::Rectangle<int> addTrackBox() const;
    juce::Rectangle<int> nameBox (size_t index) const;   // 머리의 이름 글자 자리
    std::function<void (std::optional<std::pair<double, double>>)> onLoopEdited;   // 눈금자에서 반복 구간을 끌어 놓았다 (비면 지운다)

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify (const juce::MouseEvent& e, float scaleFactor) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;   // 오디오 파일만 (MIDI 와 곡은 창이 받는다)
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    struct TrackRow
    {
        bool mute = false;
        bool solo = false;
        float level = 0.0f;
        juce::String description;
    };

    void timelineChanged() override;
    void scrollBarMoved (juce::ScrollBar* bar, double newRangeStart) override;
    void updateScrollBars();
    juce::Rectangle<int> lanesArea() const;
    juce::Rectangle<int> rowBounds (size_t index) const;    // 머리와 레인을 합친 행 (스크롤 반영)
    juce::Rectangle<int> muteBox (size_t index) const;
    juce::Rectangle<int> soloBox (size_t index) const;
    int contentHeight() const;
    void paintHeader (juce::Graphics& g, size_t index, juce::Rectangle<int> r);
    void paintLane (juce::Graphics& g, size_t index, juce::Rectangle<int> r);

    Project& project_;
    TimelineState& timeline_;
    int inputMonitorTrack_ = -1;
    std::vector<TrackRow> rows_;
    struct FolderRow
    {
        std::string name;
        size_t first = 0, last = 0;   // 이어진 트랙
        int top = 0;                  // 내용 안의 y (스크롤 전)
    };
    std::vector<FolderRow> folderRows_;
    std::vector<int> rowTop_, rowHeight_;   // 트랙마다 내용 안의 y 와 높이 (접힌 트랙은 0)
    int rowsBottom_ = 0;                     // 마지막 줄의 아래
    std::set<std::string> collapsed_;
    void paintFolder (juce::Graphics& g, const FolderRow& folder, juce::Rectangle<int> r);
    int scrollY_ = 0;
    int dropIndexAt (int y) const;   // 끌어 놓을 자리 (0..트랙 수)
    std::vector<MarkerView> markerViews() const;   // 끄는 마커는 끄는 자리에
    int trackAtY (int y) const;                    // 그 높이의 트랙 (위아래 밖은 끝 트랙)
    void paintClips (juce::Graphics& g, size_t index, juce::Rectangle<int> lane, juce::Colour colour, bool audible);
    std::pair<double, double> clipBeats (const mr::AudioClip& clip) const;   // 시작 박과 끝 박
    double fileSeconds (const mr::AudioClip& clip) const;                    // 파일 길이 (모르면 0)
    enum class ClipDrag { none, move, trimStart, trimEnd, fadeIn, fadeOut, warp, gain };
    ClipDrag clipHit (size_t track, size_t clip, juce::Point<float> p) const;
    std::optional<size_t> warpMarkerAt (size_t track, size_t clip, juce::Point<float> p) const;
    std::optional<size_t> gainPointAt (size_t track, size_t clip, juce::Point<float> p) const;
    static juce::Rectangle<float> gainArea (juce::Rectangle<float> box);   // 게인 곡선이 그려지는 자리
    static float gainY (juce::Rectangle<float> box, double db);
    static double gainDbAt (juce::Rectangle<float> box, float y);
    mr::AudioClip draggedClip (const juce::MouseEvent& e) const;

    bool draggingRuler_ = false;
    bool draggingLoop_ = false;
    std::optional<size_t> pressedHeader_;   // 머리를 누른 트랙 (끌면 옮긴다)
    bool movingTrack_ = false;
    int dropIndex_ = -1;
    std::unique_ptr<juce::TextEditor> renameEditor_;
    size_t renaming_ = 0;
    bool renamingMarker_ = false;
    std::optional<size_t> pressedMarker_;   // 누른 마커 (끌면 옮긴다)
    std::optional<double> markerDragBeat_;
    int selectedMarker_ = -1;
    std::optional<std::pair<size_t, double>> rangeAnchor_;   // 레인을 누른 트랙과 박
    bool selectingRange_ = false;
    std::optional<Range> range_;
    std::optional<ClipRef> selectedClip_;
    std::optional<RecordingRegion> recording_;
    std::optional<ClipRef> pressedClip_;
    ClipDrag clipDrag_ = ClipDrag::none;
    size_t warpMarker_ = 0;                      // 끄는 워프 마커
    size_t gainPoint_ = 0;                       // 끄는 게인 곡선의 점
    mr::AudioClip gainBase_;                     // 점을 끄기 시작한 클립 (누를 때 더한 점을 담는다)
    bool gainEditing_ = false;
    std::optional<mr::AudioClip> clipPreview_;   // 끄는 동안의 클립
    bool clipCopy_ = false;
    mutable std::map<juce::String, double> fileSeconds_;
    LoopRulerDrag loopDrag_;
    std::vector<mr::Section> sections_;
    std::optional<size_t> pressedSection_;
    std::optional<size_t> sectionTarget_;   // 끄는 동안 놓일 자리 (경계 번호)
    float sectionPressX_ = 0.0f, sectionDragX_ = 0.0f;
    std::optional<size_t> sectionAt (juce::Point<int> p) const;
    void paintSections (juce::Graphics& g, const TimelineGeometry& geo);
    std::optional<VideoLane> video_;
    std::unique_ptr<VideoThumbnails> thumbnails_;
    std::vector<double> scenes_;
    std::optional<double> videoDragOffset_;     // 끄는 동안의 영상 자리 (곡의 초)
    float videoPressX_ = 0.0f;
    bool videoDragFree_ = false;
    void paintVideoLane (juce::Graphics& g, const TimelineGeometry& geo);
    double videoOffsetNow() const { return videoDragOffset_.value_or (video_ ? video_->offsetSeconds : 0.0); }
    std::optional<size_t> sceneAt (juce::Point<int> p) const;
    juce::ScrollBar vertical_ { true };
    juce::ScrollBar horizontal_ { false };
};

} // namespace daw
