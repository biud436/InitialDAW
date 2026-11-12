// 열린 프로젝트(Composition JSON)와 화면이 함께 쓰는 보기 상태. 프로젝트는 원래 JSON 트리를 들고 편집하고,
// 편집마다 실행 취소 기록을 남긴다.
#pragma once

#include "composition/Composition.h"
#include "composition/Document.h"
#include "timing/TempoMap.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace daw
{

// 믹서의 채널: 트랙, 버스, 마스터
struct ChannelRef
{
    enum class Kind { track, bus, master };
    Kind kind = Kind::track;
    size_t index = 0;

    static ChannelRef track (size_t i) { return { Kind::track, i }; }
    static ChannelRef bus (size_t i) { return { Kind::bus, i }; }
    static ChannelRef master() { return { Kind::master, 0 }; }
    bool operator== (const ChannelRef& o) const { return kind == o.kind && (kind == Kind::master || index == o.index); }
    bool operator!= (const ChannelRef& o) const { return ! (*this == o); }
};

struct TrackSummary
{
    int lowPitch = 60;
    int highPitch = 72;
    size_t notes = 0;
    juce::Colour colour;
    std::vector<int> lanes;   // 곡선이 있는 컨트롤러 (번호 순)
};

class Project
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        // 편집, 실행 취소, 다시 실행 뒤에 부른다. track 은 바뀐 트랙, -1 은 템포나 박자 (모든 트랙의 시각이 바뀐다).
        virtual void projectEdited (int track) = 0;
    };

    Project();
    ~Project();

    // 실패하면 false 와 사람이 읽을 오류 문장 (지금 프로젝트는 그대로 둔다). 성공하면 실행 취소 기록을 비운다.
    bool load (const juce::File& file, juce::String& error);
    void clear();

    bool loaded() const noexcept { return composition_.has_value(); }
    const mr::Composition& composition() const { return *composition_; }
    const mr::TempoMap& tempo() const { return *tempo_; }
    juce::File file() const { return file_; }
    juce::Time modificationTime() const { return modified_; }
    void setModificationTime (juce::Time t) { modified_ = t; }   // 바깥의 변경을 알고도 편집을 지키기로 했을 때
    juce::String title() const;
    double barBeats() const { return loaded() ? composition_->barBeats() : 4.0; }   // 처음 박자의 마디 길이
    const mr::MeterMap& meters() const noexcept { return meters_; }                  // 박자 지도 (곡 중간의 박자 바꾸기)
    double lengthSeconds() const noexcept { return lengthSeconds_; }
    double lengthBeats() const noexcept { return lengthBeats_; }
    size_t trackCount() const { return loaded() ? composition_->tracks.size() : 0; }
    const TrackSummary& summary (size_t index) const { return summaries_.at (index); }
    const std::vector<mr::Diagnostic>& warnings() const noexcept { return warnings_; }

    double beatAtSeconds (double seconds) const { return loaded() ? tempo_->beatAt (seconds) : seconds * 2.0; }
    double secondsAtBeat (double beat) const { return loaded() ? tempo_->secondsAt (beat) : beat * 0.5; }
    // 곡의 반복 구간 (metadata.loop, 박)
    std::optional<std::pair<double, double>> loopRange() const;

    static juce::Colour trackColourFor (size_t index);

    // ---- 편집 ----
    // 문서의 한 부분: track >= 0 이면 그 트랙 JSON, 아니면 최상위 속성 (tempo, timeSignature, buses, master). value 가 void 면 속성을 지운다.
    struct Change
    {
        int track = -1;
        juce::Identifier property;
        juce::var value;
    };

    juce::var trackJson (size_t track) const { return document_->track (track); }
    juce::var property (const juce::Identifier& name) const { return document_ ? document_->property (name) : juce::var(); }
    std::vector<mr::EditNote> editNotes (size_t track) const;
    // 트랙 JSON 을 바꾸는 편집. 다시 파싱하고 checkTrack 으로 검사해 통과하면 실행 취소 기록에 넣고 적용한다.
    // 실패하면 false 와 오류 문장 (아무것도 바뀌지 않는다).
    bool editTrack (size_t track, const juce::var& after, const juce::String& name, juce::String& error);
    bool editTempo (const juce::var& tempo, const juce::var& timeSignature, const juce::String& name, juce::String& error);
    // 반복 구간을 바꾸는 편집 (metadata.loop). 비면 지운다. metadata 의 다른 필드는 그대로 둔다.
    bool editLoop (const std::optional<std::pair<double, double>>& range, juce::String& error);
    // 여러 부분을 한 번에 (실행 취소 한 단계). 다시 파싱하고 바뀐 트랙을 checkTrack 으로 검사한다.
    bool edit (const std::vector<Change>& changes, const juce::String& name, juce::String& error);
    // 곡 JSON 트리 전체를 바꾼 것처럼 편집한다 (바뀐 최상위 속성만 넣는다, mr::Arrange 의 함수가 만든 트리). 바뀐 것이 없으면 false
    bool editRoot (const juce::var& after, const juce::String& name, juce::String& error);
    // 편집 중인 JSON 트리 (읽기만, 깊은 사본이 아니다)
    const juce::var& documentRoot() const { return document_->root(); }
    // 마커 (박 오름차순)
    const std::vector<mr::Marker>& markers() const { return composition_->markers; }

    // ---- 믹서 ----
    size_t busCount() const { return loaded() ? composition_->buses.size() : 0; }
    const mr::MixerSettings& mixerOf (ChannelRef channel) const;
    const std::vector<mr::EffectSpec>& effectsOf (ChannelRef channel) const;
    juce::String channelName (ChannelRef channel) const;
    juce::var channelJson (ChannelRef channel) const;          // 깊은 사본 (마스터가 없으면 void)
    Change channelChange (ChannelRef channel, const juce::var& json) const;
    bool editChannel (ChannelRef channel, const juce::var& json, const juce::String& name, juce::String& error);
    // 마지막 편집(실행 취소, 다시 실행 포함)이 어느 트랙의 음을 바꿨는가 (피아노 롤이 선택을 지킬지 정한다)
    bool lastEditChangedNotes() const noexcept { return lastEditChangedNotes_; }
    juce::UndoManager& undoManager() noexcept { return undo_; }
    // 실행 취소 기록의 한 자리로: steps 가 음수면 그만큼 되돌리고 양수면 다시 실행한다. 곡은 끝에 한 번 다시 읽고 알린다 (track -1)
    bool jumpHistory (int steps);
    bool hasUnsavedEdits() const noexcept { return loaded() && depth_ != savedDepth_; }
    bool save (const juce::File& file, juce::String& error);
    std::string toJson() const { return document_ ? document_->toJson() : std::string(); }
    // 트랙이 악기와 맞는지 (빈 문자열이면 통과). 앱이 재생기의 악기로 채운다.
    std::function<juce::String (size_t track, const mr::TrackSpec& spec)> checkTrack;

    void addListener (Listener* l) { listeners_.add (l); }
    void removeListener (Listener* l) { listeners_.remove (l); }

private:
    class ReplaceAction;

    // 문서의 부분들을 바꾸고 다시 파싱한다 (실행과 되돌리기가 부른다). step 은 +1(실행) 또는 -1(되돌리기).
    bool apply (const std::vector<Change>& changes, int step);
    void setPart (const Change& change);
    Change currentPart (const Change& like) const;
    void adopt (mr::Composition composition, std::vector<mr::Diagnostic> warnings);
    static juce::String describeErrors (const mr::ParseResult& result);

    std::optional<mr::CompositionDocument> document_;
    std::optional<mr::Composition> composition_;
    std::unique_ptr<mr::TempoMap> tempo_;
    mr::MeterMap meters_;
    std::vector<TrackSummary> summaries_;
    std::vector<mr::Diagnostic> warnings_;
    juce::File file_;
    juce::Time modified_;
    double lengthSeconds_ = 0.0;
    double lengthBeats_ = 0.0;

    juce::UndoManager undo_ { 2000000, 200 };
    int depth_ = 0;            // 실행한 편집 수 (되돌리면 줄어든다)
    int savedDepth_ = 0;       // 저장한 때의 depth_ (-1 이면 그 상태로 돌아갈 수 없다)
    bool performingNew_ = false;
    bool batching_ = false;    // jumpHistory 안: 걸음마다 다시 읽지 않는다
    std::optional<mr::ParseResult> parsedAfter_;   // edit 가 검사하며 파싱한 결과 (첫 실행이 다시 쓴다)
    bool lastEditChangedNotes_ = false;
    juce::ListenerList<Listener> listeners_;
};

// 트랙 보기와 피아노 롤이 함께 쓰는 가로축(박)과 선택. 바뀌면 듣는 쪽에 알린다.
class TimelineState
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void timelineChanged() = 0;
    };

    double pixelsPerBeat() const noexcept { return pixelsPerBeat_; }
    double viewStartBeat() const noexcept { return viewStartBeat_; }
    int selectedTrack() const noexcept { return selectedTrack_; }
    double playheadBeat() const noexcept { return playheadBeat_; }
    double lengthBeats() const noexcept { return lengthBeats_; }
    const std::optional<std::pair<double, double>>& loopRange() const noexcept { return loopRange_; }   // 눈금자의 반복 구간 띠
    bool loopOn() const noexcept { return loopOn_; }

    void setLengthBeats (double beats);
    void setView (double startBeat, double pixelsPerBeat);
    void scrollBy (double beats) { setView (viewStartBeat_ + beats, pixelsPerBeat_); }
    // anchorBeat 가 화면의 같은 자리에 머물도록 확대한다
    void zoomAround (double factor, double anchorBeat, double anchorOffsetPixels);
    void setSelectedTrack (int index);
    void setPlayheadBeat (double beat);   // 알리지만 보기를 옮기지 않는다
    void setLoopRange (const std::optional<std::pair<double, double>>& range);
    void setLoopOn (bool on);

    void addListener (Listener* l) { listeners_.add (l); }
    void removeListener (Listener* l) { listeners_.remove (l); }

private:
    void notify() { listeners_.call ([] (Listener& l) { l.timelineChanged(); }); }

    double pixelsPerBeat_ = 28.0;
    double viewStartBeat_ = 0.0;
    int selectedTrack_ = 0;
    double playheadBeat_ = 0.0;
    double lengthBeats_ = 64.0;
    std::optional<std::pair<double, double>> loopRange_;
    bool loopOn_ = false;
    juce::ListenerList<Listener> listeners_;
};

// 컨트롤러 번호의 화면 이름
juce::String controllerLabel (int id);

} // namespace daw
