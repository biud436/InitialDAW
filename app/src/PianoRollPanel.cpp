#include "PianoRollPanel.h"

#include "Grooves.h"

#include "Theme.h"
#include "TimelineDrawing.h"
#include "composition/Arrange.h"

#include <algorithm>
#include <set>

namespace daw
{

namespace
{
const char* const noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

juce::String noteName (int pitch) { return juce::String (noteNames[pitch % 12]) + juce::String (pitch / 12 - 1); }
bool isBlack (int pitch) { const int k = pitch % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }

// 트랙 사이에서 함께 쓰는 클립보드 (붙이면 고른 트랙에 들어간다)
std::vector<mr::Note>& clipboard()
{
    static std::vector<mr::Note> notes;
    return notes;
}

struct GridChoice
{
    const char* label;
    double beats;
};
const GridChoice gridChoices[] = { { "4분음표", 1.0 }, { "8분음표", 0.5 }, { "16분음표", 0.25 }, { "32분음표", 0.125 },
                                   { "8분음표 셋잇단", 1.0 / 3.0 }, { "16분음표 셋잇단", 1.0 / 6.0 } };

// 곡선 추가 메뉴에 올리는 컨트롤러와 처음 값
constexpr int laneItemVelocity = 1;
constexpr int laneItemAdd = 2;
constexpr int laneItemBase = 1000;
} // namespace

PianoRollPanel::PianoRollPanel (Project& project, TimelineState& timeline) : project_ (project), timeline_ (timeline)
{
    timeline_.addListener (this);
    project_.addListener (this);
    vertical_.addListener (this);
    horizontal_.addListener (this);
    vertical_.setAutoHide (false);
    horizontal_.setAutoHide (false);
    addAndMakeVisible (vertical_);
    addAndMakeVisible (horizontal_);
    setWantsKeyboardFocus (true);

    for (int i = 0; i < (int) std::size (gridChoices); ++i)
        gridBox_.addItem (juce::String::fromUTF8 (gridChoices[i].label), i + 1);
    gridBox_.setSelectedId (3, juce::dontSendNotification);
    gridBox_.setTooltip (juce::String::fromUTF8 ("음표를 놓고 옮기는 격자입니다. ⌘ 를 누르고 끌면 격자를 무시합니다."));
    gridBox_.setWantsKeyboardFocus (false);
    gridBox_.onChange = [this]
    {
        const int i = gridBox_.getSelectedId() - 1;
        if (i >= 0 && i < (int) std::size (gridChoices))
            grid_ = lastLength_ = gridChoices[i].beats;
    };
    addChildComponent (gridBox_);
    drumButton_.setButtonText (juce::String::fromUTF8 ("드럼 보기"));
    drumButton_.setClickingTogglesState (true);
    drumButton_.setTooltip (juce::String::fromUTF8 ("이름 있는 건반만 줄로 보이고 빈 칸을 누르면 그 칸에 칩니다. 끄면 피아노 건반 전체가 보입니다."));
    drumButton_.setWantsKeyboardFocus (false);
    drumButton_.onClick = [this] { setDrumView (drumButton_.getToggleState()); };
    addChildComponent (drumButton_);
    companionButton_.setButtonText (juce::String::fromUTF8 ("다른 트랙..."));
    companionButton_.setTooltip (juce::String::fromUTF8 ("다른 트랙의 음을 흐리게 함께 보거나, 함께 고르고 옮기고 다듬습니다."));
    companionButton_.setWantsKeyboardFocus (false);
    companionButton_.onClick = [this] { companionMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&companionButton_)); };
    addChildComponent (companionButton_);

    laneBox_.setWantsKeyboardFocus (false);
    laneBox_.onChange = [this]
    {
        const int id = laneBox_.getSelectedId();
        if (id == laneItemAdd)
        {
            refreshLaneBox();   // 고르기 전의 칸으로 되돌려 둔다
            showAddCurveMenu();
            return;
        }
        laneKey_ = id >= laneItemBase && (size_t) (id - laneItemBase) < laneItems_.size() ? laneItems_[(size_t) (id - laneItemBase)] : std::string();
        repaint();
    };
    addChildComponent (laneBox_);
}

PianoRollPanel::~PianoRollPanel()
{
    project_.removeListener (this);
    timeline_.removeListener (this);
}

int PianoRollPanel::track() const
{
    const int index = timeline_.selectedTrack();
    return project_.loaded() && index >= 0 && (size_t) index < project_.trackCount() ? index : -1;
}

void PianoRollPanel::projectChanged()
{
    stopAudition();
    shownTrack_ = -1;
    selection_.clear();
    others_.clear();
    drag_ = Drag::none;
    centreOnTrack();
    refreshLaneBox();
    resized();
    repaint();
}

juce::Rectangle<int> PianoRollPanel::gridArea() const
{
    return { leftWidth, rulerHeight, getWidth() - leftWidth - scrollBarSize, getHeight() - rulerHeight - laneHeight - scrollBarSize };
}

juce::Rectangle<int> PianoRollPanel::keyboardArea() const
{
    const auto grid = gridArea();
    return { leftWidth - keyboardWidth, grid.getY(), keyboardWidth, grid.getHeight() };
}

int PianoRollPanel::keyVelocity (float x) const
{
    const auto keys = keyboardArea().toFloat();
    const float fraction = juce::jlimit (0.0f, 1.0f, (x - keys.getX()) / keys.getWidth());
    return juce::jlimit (30, 120, (int) std::round (30.0f + 90.0f * fraction));
}

void PianoRollPanel::audition (int pitch, int velocity, const std::string& articulation, int channel, int onTrack)
{
    const int index = onTrack >= 0 ? onTrack : track();
    if (pitch == auditionPitch_ && index == auditionTrack_)
        return;
    stopAudition();
    if (index < 0 || pitch < 0 || pitch > 127 || ! onAudition)
        return;
    onAudition ((size_t) index, pitch, juce::jlimit (1, 127, velocity), articulation, channel);
    auditionPitch_ = pitch;
    auditionTrack_ = index;
    ++auditionToken_;
    repaint (keyboardArea());
}

void PianoRollPanel::auditionBriefly (int pitch, int velocity, const std::string& articulation, int channel)
{
    audition (pitch, velocity, articulation, channel);
    const int token = auditionToken_;
    juce::Timer::callAfterDelay (350, [safe = juce::Component::SafePointer<PianoRollPanel> (this), token]
    {
        if (safe != nullptr && safe->auditionToken_ == token && safe->drag_ == Drag::none)
            safe->stopAudition();
    });
}

void PianoRollPanel::stopAudition()
{
    if (auditionPitch_ < 0)
        return;
    if (onAudition)
        onAudition ((size_t) auditionTrack_, auditionPitch_, 0, {}, 1);
    auditionPitch_ = -1;
    auditionTrack_ = -1;
    repaint (keyboardArea());
}

juce::Rectangle<int> PianoRollPanel::laneArea() const
{
    return { leftWidth, getHeight() - laneHeight - scrollBarSize, getWidth() - leftWidth - scrollBarSize, laneHeight };
}

juce::Rectangle<float> PianoRollPanel::laneValueArea() const { return laneArea().toFloat().reduced (0.0f, 10.0f); }

float PianoRollPanel::pitchY (int pitch) const
{
    return (float) gridArea().getY() + (float) (topPitch_ - virtualOf (pitch)) * keyHeight_;
}

int PianoRollPanel::pitchAt (float y) const
{
    return pitchOfVirtual (juce::jlimit (lowestVirtual(), 127, (int) std::ceil (topPitch_ - (y - (float) gridArea().getY()) / keyHeight_)));
}

int PianoRollPanel::virtualOf (int pitch) const
{
    if (rows_.empty())
        return pitch;
    const auto it = std::find (rows_.begin(), rows_.end(), pitch);
    return it == rows_.end() ? -1000 : 127 - (int) (it - rows_.begin());
}

int PianoRollPanel::pitchOfVirtual (int v) const
{
    if (rows_.empty())
        return juce::jlimit (0, 127, v);
    return rows_[(size_t) juce::jlimit (0, (int) rows_.size() - 1, 127 - v)];
}

int PianoRollPanel::movedPitch (int pitch, int delta) const
{
    if (rows_.empty())
        return juce::jlimit (0, 127, pitch + delta);
    return pitchOfVirtual (juce::jlimit (lowestVirtual(), 127, virtualOf (pitch) + delta));
}

void PianoRollPanel::rebuildRows()
{
    const int index = track();
    const auto previous = rows_;
    rows_.clear();
    bool on = false;
    if (index >= 0 && ! keyNames_.empty() && ! audioTrack())
    {
        const auto& t = project_.composition().tracks[(size_t) index];
        const auto choice = drumChoice_.find (t.name);
        on = choice == drumChoice_.end() || choice->second;
        if (on)
        {
            std::set<int> pitches;
            for (const auto& [pitch, name] : keyNames_)
                pitches.insert (pitch);
            for (const auto& n : t.notes)
                pitches.insert (n.pitch);
            rows_.assign (pitches.rbegin(), pitches.rend());
        }
    }
    drumButton_.setToggleState (on, juce::dontSendNotification);
    drumButton_.setVisible (index >= 0 && ! keyNames_.empty() && ! audioTrack());
    if (rows_ != previous)
    {
        if (! rows_.empty() && previous.empty())
        {
            keyHeight_ = std::max (keyHeight_, 18.0f);
            scrollToUsedRows();
        }
        updateScrollBars();
    }
}

void PianoRollPanel::scrollToUsedRows()
{
    // 음이 있는 줄들이 보이게: 다 들어가면 가운데에
    topPitch_ = 127.0;
    const int index = track();
    if (index < 0 || rows_.empty())
        return;
    int hi = -1000, lo = 1000;
    std::map<int, int> count;
    for (const auto& n : project_.composition().tracks[(size_t) index].notes)
    {
        const int v = virtualOf (n.pitch);
        hi = std::max (hi, v);
        lo = std::min (lo, v);
        ++count[v];
    }
    if (hi < lo)
        return;
    const double visible = std::max (1.0, gridArea().getHeight() / (double) keyHeight_);
    const double used = hi - lo + 1;
    if (used <= visible)
    {
        topPitch_ = std::min (127.0, hi + std::floor ((visible - used) * 0.5));
        return;
    }
    // 다 들어가지 않으면 가장 많이 친 줄을 가운데에
    const auto busiest = std::max_element (count.begin(), count.end(), [] (const auto& a, const auto& b) { return a.second < b.second; })->first;
    topPitch_ = std::min (127.0, busiest + std::floor (visible * 0.5));
}

void PianoRollPanel::setDrumView (bool on)
{
    const int index = track();
    if (index < 0)
        return;
    drumChoice_[project_.composition().tracks[(size_t) index].name] = on;
    const bool was = drumView();
    rebuildRows();
    if (was && ! drumView())
        centreOnTrack();
    repaint();
}

double PianoRollPanel::snapRound (double beat) const { return std::max (0.0, std::round (beat / grid_) * grid_); }
double PianoRollPanel::snapFloor (double beat) const { return std::max (0.0, std::floor (beat / grid_ + 1.0e-9) * grid_); }

bool PianoRollPanel::isSelected (size_t index) const
{
    return std::binary_search (selection_.begin(), selection_.end(), index);
}

void PianoRollPanel::refreshKeyNames()
{
    const int index = track();
    keyNames_ = index >= 0 && keyNamesFor ? keyNamesFor ((size_t) index) : std::map<int, juce::String> {};
    // 같은 이름의 건반이 여럿이면 음 이름을 붙여 가른다 ("탐 2 (B2)")
    std::map<juce::String, int> uses;
    for (const auto& [pitch, name] : keyNames_)
        ++uses[name];
    for (auto& [pitch, name] : keyNames_)
        if (uses[name] > 1)
            name << " (" << noteName (pitch) << ")";
    rebuildRows();
}

void PianoRollPanel::centreOnTrack()
{
    const int index = track();
    shownTrack_ = timeline_.selectedTrack();
    laneKey_.clear();
    refreshKeyNames();
    if (index < 0)
        return;
    if (audioTrack())
    {
        audioNote_.reset();
        centreOnAudioNotes();
        return;
    }
    if (drumView())
    {
        scrollToUsedRows();
        updateScrollBars();
        return;
    }
    const auto& s = project_.summary ((size_t) index);
    const double centre = (s.lowPitch + s.highPitch) * 0.5;
    const double visibleKeys = std::max (1.0, gridArea().getHeight() / (double) keyHeight_);
    topPitch_ = juce::jlimit (visibleKeys - 1.0, 127.0, centre + visibleKeys * 0.5);
    updateScrollBars();
}

void PianoRollPanel::refreshLaneBox()
{
    laneBox_.clear (juce::dontSendNotification);
    gridBox_.setVisible (track() >= 0 && gridArea().getHeight() > 150);   // 오디오 트랙은 음의 시작과 끝을 격자에 맞춘다
    const int index = track();
    if (index < 0)
        return;
    laneItems_.clear();
    if (! audioTrack())
        laneBox_.addItem (juce::String::fromUTF8 ("벨로시티"), laneItemVelocity);
    for (const auto& t : automatedTargets (project_.composition().tracks[(size_t) index]))
    {
        laneBox_.addItem (t.label, laneItemBase + (int) laneItems_.size());
        laneItems_.push_back (t.key);
    }
    if (const auto* vca = vcaOf (project_.composition(), project_.composition().tracks[(size_t) index]))
    {
        laneBox_.addItem (vcaTarget (*vca).label, laneItemBase + (int) laneItems_.size());   // 속한 VCA 의 게인
        laneItems_.push_back ("vca");
    }
    laneBox_.addItem (tempoTarget (project_.composition().tempo).label, laneItemBase + (int) laneItems_.size());   // 곡마다 늘 있다
    laneItems_.push_back ("tempo");
    laneBox_.addSeparator();
    laneBox_.addItem (juce::String::fromUTF8 ("곡선 추가..."), laneItemAdd);
    auto found = std::find (laneItems_.begin(), laneItems_.end(), laneKey_);
    if (! laneKey_.empty() && found == laneItems_.end())
        laneKey_.clear();
    if (laneKey_.empty() && audioTrack() && laneItems_.size() > 1)
        laneKey_ = laneItems_.front();   // 오디오 트랙에는 벨로시티가 없다 (곡선이 있으면 처음 것)
    found = std::find (laneItems_.begin(), laneItems_.end(), laneKey_);
    if (found != laneItems_.end())
        laneBox_.setSelectedId (laneItemBase + (int) (found - laneItems_.begin()), juce::dontSendNotification);
    else if (! audioTrack())
        laneBox_.setSelectedId (laneItemVelocity, juce::dontSendNotification);
    else
        laneBox_.setText (juce::String::fromUTF8 ("곡선 없음"), juce::dontSendNotification);
}

void PianoRollPanel::showLane (const std::string& key)
{
    laneKey_ = key;
    refreshLaneBox();   // 곡선이 없으면 벨로시티로
    repaint();
}

bool PianoRollPanel::addLane (const std::string& key)
{
    const int index = track();
    if (index < 0)
        return false;
    const auto& spec = project_.composition().tracks[(size_t) index];
    const auto target = automationTarget (spec, key);
    if (! target || ! mr::automationLaneOf (spec, key).empty())
        return false;
    mr::AutomationLane lane;
    lane.points.push_back ({ 0.0, target->defaultValue, mr::CurveShape::linear });
    if (! commitLane (key, lane, juce::String::fromUTF8 ("곡선 추가: ") + target->label))
        return false;
    laneKey_ = key;
    refreshLaneBox();
    repaint();
    return true;
}

void PianoRollPanel::pruneSelection()
{
    const int index = track();
    const size_t count = index >= 0 ? project_.composition().tracks[(size_t) index].notes.size() : 0;
    selection_.erase (std::remove_if (selection_.begin(), selection_.end(), [count] (size_t i) { return i >= count; }), selection_.end());
    for (auto it = others_.begin(); it != others_.end();)
    {
        if (! editable (it->first))
        {
            it = others_.erase (it);
            continue;
        }
        const size_t n = project_.composition().tracks[it->first].notes.size();
        auto& sel = it->second;
        sel.erase (std::remove_if (sel.begin(), sel.end(), [n] (size_t i) { return i >= n; }), sel.end());
        ++it;
    }
}

void PianoRollPanel::timelineChanged()
{
    if (timeline_.selectedTrack() != shownTrack_)
    {
        stopAudition();
        audioNote_.reset();
        selection_.clear();
        others_.clear();
        drag_ = Drag::none;
        centreOnTrack();
        refreshLaneBox();
        resized();
    }
    updateScrollBars();
    repaint();
}

void PianoRollPanel::projectEdited (int editedTrack)
{
    if (editedTrack < 0 || editedTrack == track())
    {
        refreshKeyNames();   // 악기를 바꿨을 수 있다
        if (! committing_ && project_.lastEditChangedNotes())
            selection_.clear();   // 실행 취소와 다시 실행 뒤에는 번호가 다른 음을 가리킬 수 있다
        refreshLaneBox();
    }
    if (! committing_ && project_.lastEditChangedNotes() && (editedTrack < 0 || others_.count ((size_t) editedTrack) > 0))
        others_.clear();
    pruneSelection();
    if (audioNote_ && ! findAudioNote (*audioNote_))
        audioNote_.reset();
    repaint();
}

void PianoRollPanel::updateScrollBars()
{
    const auto grid = gridArea();
    const double visibleKeys = grid.getHeight() / (double) keyHeight_;
    topPitch_ = juce::jlimit (std::min (127.0, lowestVirtual() + visibleKeys - 1.0), 127.0, topPitch_);
    vertical_.setRangeLimits (0.0, 128.0 - lowestVirtual());
    vertical_.setCurrentRange (127.0 - topPitch_, visibleKeys, juce::dontSendNotification);

    const double visibleBeats = grid.getWidth() / timeline_.pixelsPerBeat();
    const double total = std::max (timeline_.lengthBeats() + 8.0, timeline_.viewStartBeat() + visibleBeats);
    horizontal_.setRangeLimits (0.0, total);
    horizontal_.setCurrentRange (timeline_.viewStartBeat(), visibleBeats, juce::dontSendNotification);
}

void PianoRollPanel::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (bar == &vertical_)
    {
        topPitch_ = 127.0 - start;
        repaint();
    }
    else
        timeline_.setView (start, timeline_.pixelsPerBeat());
}

void PianoRollPanel::resized()
{
    vertical_.setBounds (getWidth() - scrollBarSize, rulerHeight, scrollBarSize, gridArea().getHeight());
    horizontal_.setBounds (leftWidth, getHeight() - scrollBarSize, getWidth() - leftWidth - scrollBarSize, scrollBarSize);
    const bool show = track() >= 0;
    const int infoWidth = leftWidth - keyboardWidth;
    gridBox_.setBounds (14, gridArea().getBottom() - 34, infoWidth - 26, 24);
    drumButton_.setBounds (14, gridArea().getBottom() - 92, infoWidth - 26, 24);
    companionButton_.setBounds (12, laneArea().getY() + 42, infoWidth - 24, 24);
    companionButton_.setVisible (show && ! audioTrack());
    gridBox_.setVisible (show && gridArea().getHeight() > 150);
    laneBox_.setBounds (12, laneArea().getY() + 10, infoWidth - 24, 24);
    laneBox_.setVisible (show);
    updateScrollBars();
}

// ---- 그리기 ----

juce::Rectangle<float> PianoRollPanel::noteRect (const mr::Note& n, const TimelineGeometry& geo) const
{
    const float x0 = geo.x (n.startBeat);
    if (drumView())   // 친 자리의 마름모가 드는 정사각형
    {
        const float side = keyHeight_ - 2.0f;
        return { x0 - side * 0.5f, pitchY (n.pitch) + 1.0f, side, side };
    }
    const float w = std::max (2.0f, (float) (n.durationBeat * geo.pixelsPerBeat) - 1.0f);
    return { x0, pitchY (n.pitch) + 1.0f, w, keyHeight_ - 2.0f };
}

void PianoRollPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    g.setColour (colours::outline);
    g.fillRect (0, 0, getWidth(), 1);

    const auto grid = gridArea();
    if (track() < 0)
    {
        g.setColour (colours::textDim);
        g.setFont (uiFont (14.0f));
        g.drawText (juce::String::fromUTF8 ("트랙을 고르면 음표가 여기에 나옵니다."), getLocalBounds(), juce::Justification::centred);
        return;
    }
    const TimelineGeometry geo (timeline_, leftWidth, grid.getRight());

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (grid);
        const int top = (int) std::ceil (topPitch_);
        for (int v = top; v >= lowestVirtual(); --v)
        {
            const int p = pitchOfVirtual (v);
            const float y = pitchY (p);
            if (y > (float) grid.getBottom())
                break;
            if (drumView())   // 줄마다 번갈아, 이름 없는 건반(쓰인 음)은 어둡게
                g.setColour (keyNames_.count (p) == 0 ? colours::laneA.darker (0.35f) : (v % 2 == 0 ? colours::laneB.brighter (0.06f) : colours::laneB));
            else if (keyNames_.empty())
                g.setColour (isBlack (p) ? colours::laneA.darker (0.25f) : colours::laneB);
            else   // 드럼과 타악기: 이름이 있는 건반만 밝게
                g.setColour (keyNames_.count (p) > 0 ? colours::laneB.brighter (0.06f) : colours::laneA.darker (0.35f));
            g.fillRect (juce::Rectangle<float> ((float) grid.getX(), y, (float) grid.getWidth(), keyHeight_));
            if (drumView())
            {
                g.setColour (colours::outline.withAlpha (0.5f));
                g.fillRect (juce::Rectangle<float> ((float) grid.getX(), y + keyHeight_ - 1.0f, (float) grid.getWidth(), 1.0f));
            }
            else if (p % 12 == 0)
            {
                g.setColour (colours::barLine);
                g.fillRect (juce::Rectangle<float> ((float) grid.getX(), y + keyHeight_ - 1.0f, (float) grid.getWidth(), 1.0f));
            }
        }
        // 조의 음계에 든 줄을 밝게 (조 바꾸기 자리에서 바뀐다), 으뜸음 줄은 더 밝게
        if (! drumView() && (project_.composition().key || ! project_.composition().keyChanges.empty()))
        {
            std::vector<std::pair<double, mr::KeySignature>> spans;
            if (const auto first = mr::keyAt (project_.composition(), geo.startBeat))
                spans.push_back ({ geo.startBeat, *first });
            for (const auto& change : project_.composition().keyChanges)
                if (change.beat > geo.startBeat && change.beat < geo.endBeat())
                    spans.push_back ({ change.beat, change.key });
            for (size_t k = 0; k < spans.size(); ++k)
            {
                const float x0 = std::max ((float) grid.getX(), geo.x (spans[k].first));
                const float x1 = k + 1 < spans.size() ? geo.x (spans[k + 1].first) : (float) grid.getRight();
                const int tonic = mr::tonicPitchClass (spans[k].second);
                for (int v = top; v >= lowestVirtual(); --v)
                {
                    const int p = pitchOfVirtual (v);
                    const float y = pitchY (p);
                    if (y > (float) grid.getBottom())
                        break;
                    if (! mr::inKeyScale (spans[k].second, p))
                        continue;
                    g.setColour (colours::accent.withAlpha (p % 12 == tonic ? 0.11f : 0.045f));
                    g.fillRect (juce::Rectangle<float> (x0, y, x1 - x0, keyHeight_ - 1.0f));
                }
                if (k > 0)
                {
                    g.setColour (colours::accent.withAlpha (0.5f));
                    g.fillRect (x0, (float) grid.getY(), 1.0f, (float) grid.getHeight());
                }
            }
        }
        // 마커 줄
        for (const auto& m : project_.markers())
        {
            const float x = geo.x (m.beat);
            if (x >= (float) grid.getX() && x <= (float) grid.getRight())
            {
                g.setColour (colours::marker.withAlpha (0.22f));
                g.fillRect (x, (float) grid.getY(), 1.0f, (float) grid.getHeight());
            }
        }
        // 격자 칸이 박보다 잘고 충분히 넓을 때만 격자 줄을 더 그린다
        const double gridPx = grid_ * geo.pixelsPerBeat;
        if (grid_ < 1.0 && gridPx >= 10.0)
        {
            g.setColour (colours::beatLine.withAlpha (0.45f));
            for (double b = std::floor (geo.startBeat / grid_) * grid_; b <= geo.endBeat(); b += grid_)
                g.fillRect (geo.x (b), (float) grid.getY(), 1.0f, (float) grid.getHeight());
        }
        drawGrid (g, grid, geo, project_.meters());
        if (audioTrack())
            paintAudioNotes (g, geo);   // 녹음의 음 덩어리
        else
            paintNotes (g, geo);
        if (drag_ == Drag::band && ! band_.isEmpty())
        {
            g.setColour (colours::selection.withAlpha (0.18f));
            g.fillRect (band_);
            g.setColour (colours::selection.withAlpha (0.8f));
            g.drawRect (band_, 1.0f);
        }
        drawPlayhead (g, grid, geo, timeline_.playheadBeat());
    }

    drawRuler (g, juce::Rectangle<int> (leftWidth, 0, grid.getWidth(), rulerHeight), geo, project_.meters(),
               timeline_.lengthBeats(), timeline_.playheadBeat(), timeline_.loopRange(), timeline_.loopOn());
    drawRulerMarkers (g, juce::Rectangle<int> (leftWidth, 0, grid.getWidth(), rulerHeight), geo, project_.markers());

    auto left = juce::Rectangle<int> (0, 0, leftWidth, getHeight());
    g.setColour (colours::panel);
    g.fillRect (left);
    paintInfo (g, juce::Rectangle<int> (0, 0, leftWidth - keyboardWidth, grid.getBottom()));
    paintKeyboard (g, juce::Rectangle<int> (leftWidth - keyboardWidth, grid.getY(), keyboardWidth, grid.getHeight()));
    g.setColour (colours::outline);
    g.fillRect (leftWidth - 1, 0, 1, getHeight());

    paintLane (g);
}

// 가사: 음표 안에 음절을 쓰고, 좁으면 글자를 줄이고, 그래도 넘치면 음표 바로 아래나 위의 빈자리에 쓴다.
// 다른 음표나 앞에 쓴 가사와 겹치면 쓰지 않는다. 이어 부르는 "-" 는 음표 안의 가로선이다.
void PianoRollPanel::paintLyrics (juce::Graphics& g, const std::vector<ShownNote>& shown, const juce::Font& pitchFont)
{
    const auto grid = gridArea().toFloat();
    const float largest = std::min (12.0f, keyHeight_ - 1.0f);
    constexpr float smallest = 8.0f;
    std::vector<juce::Rectangle<float>> outside;   // 음표 밖에 쓴 가사
    for (const auto& s : shown)
    {
        if (s.lyric.empty())
            continue;
        const auto& r = s.rect;
        if (mr::isLyricExtension (s.lyric))
        {
            const float x0 = r.getX() + std::min (4.0f, r.getWidth() * 0.3f);
            float x1 = std::max (x0 + 1.0f, std::min (r.getRight(), grid.getRight()) - std::min (3.0f, r.getWidth() * 0.2f));
            const float labelWidth = juce::GlyphArrangement::getStringWidth (pitchFont, s.label);
            if (keyHeight_ >= 11.0f && x1 - x0 >= labelWidth + 24.0f)   // 넓으면 음 이름을 오른쪽 끝에
            {
                g.setColour (juce::Colours::black.withAlpha (0.55f * s.alpha));
                g.setFont (pitchFont);
                g.drawText (s.label, juce::Rectangle<float> (x1 - labelWidth - 2.0f, r.getY(), labelWidth + 2.0f, r.getHeight()),
                            juce::Justification::centredRight, false);
                x1 -= labelWidth + 6.0f;
            }
            g.setColour (juce::Colours::black.withAlpha (0.6f * s.alpha));
            g.fillRect (juce::Rectangle<float> (x0, r.getCentreY() - 0.75f, x1 - x0, 1.5f));
            continue;
        }
        if (largest < smallest)
            continue;
        const auto text = juce::String::fromUTF8 (s.lyric.c_str());
        const auto inside = r.withTrimmedLeft (4.0f).withTrimmedRight (2.0f);
        bool placed = false;
        for (float size = largest; size >= smallest && ! placed; size -= 1.0f)
        {
            const auto font = uiFont (size, true);
            const float w = juce::GlyphArrangement::getStringWidth (font, text);
            if (w > inside.getWidth())
                continue;
            g.setColour (juce::Colours::black.withAlpha (0.85f * s.alpha));
            g.setFont (font);
            g.drawText (text, inside, juce::Justification::centredLeft, false);
            const auto rest = inside.withTrimmedLeft (w + 6.0f);
            if (keyHeight_ >= 11.0f && juce::GlyphArrangement::getStringWidth (pitchFont, s.label) <= rest.getWidth())
            {
                g.setColour (juce::Colours::black.withAlpha (0.55f * s.alpha));
                g.setFont (pitchFont);
                g.drawText (s.label, rest, juce::Justification::centredLeft, false);
            }
            placed = true;
        }
        for (float size = largest; size >= smallest && ! placed; size -= 1.0f)
        {
            const auto font = uiFont (size, true);
            const float w = juce::GlyphArrangement::getStringWidth (font, text) + 1.0f;
            for (const float y : { r.getBottom() + 1.0f, r.getY() - size - 2.0f })
            {
                const juce::Rectangle<float> box (r.getX(), y, w, size + 1.0f);
                const auto hits = [&box] (const juce::Rectangle<float>& other) { return other.intersects (box); };
                if (! grid.contains (box) || std::any_of (outside.begin(), outside.end(), hits)
                    || std::any_of (shown.begin(), shown.end(), [&hits] (const ShownNote& o) { return hits (o.rect); }))
                    continue;
                g.setColour (colours::text.withAlpha (0.9f * s.alpha));
                g.setFont (font);
                g.drawText (text, box, juce::Justification::centredLeft, false);
                outside.push_back (box);
                placed = true;
                break;
            }
        }
    }
}

void PianoRollPanel::paintNotes (juce::Graphics& g, const TimelineGeometry& geo)
{
    const size_t index = (size_t) track();
    const auto grid = gridArea();
    const double viewEnd = geo.endBeat();
    const bool moving = (drag_ == Drag::move && dragMoved_);
    const bool resizing = drag_ == Drag::resize;
    const auto pitchFont = uiFont (std::min (11.0f, keyHeight_ - 1.0f));
    std::vector<ShownNote> shown;

    // labels 는 보이는 트랙만 (음 이름과 가사)
    auto drawNote = [&] (mr::Note n, bool selected, float alpha, juce::Colour colour, bool labels)
    {
        if (n.startBeat > viewEnd || n.startBeat + n.durationBeat < geo.startBeat)
            return;
        const auto rect = noteRect (n, geo);
        if (rect.getBottom() < (float) grid.getY() || rect.getY() > (float) grid.getBottom())
            return;
        const bool hasLyric = labels && ! n.lyric.empty();
        const float v = (float) n.velocity / 127.0f;
        auto fill = colour.withMultipliedBrightness (0.55f + 0.45f * v);
        if (selected)
            fill = fill.brighter (0.35f);
        if (drumView())
        {
            // 길이는 옅은 선, 친 자리는 마름모 (세기만큼 크고 밝게)
            const float end = geo.x (n.startBeat + n.durationBeat);
            g.setColour (colour.withAlpha (0.35f * alpha));
            g.fillRect (juce::Rectangle<float> (rect.getCentreX(), rect.getCentreY() - 1.0f, std::max (0.0f, end - rect.getCentreX()), 2.0f));
            const auto d = rect.reduced (rect.getWidth() * 0.18f * (1.0f - v));
            juce::Path diamond;
            diamond.addQuadrilateral (d.getCentreX(), d.getY(), d.getRight(), d.getCentreY(), d.getCentreX(), d.getBottom(), d.getX(), d.getCentreY());
            g.setColour (fill.withMultipliedAlpha (alpha));
            g.fillPath (diamond);
            g.setColour (selected ? juce::Colours::white.withAlpha (0.95f * alpha) : colour.brighter (0.4f).withAlpha (0.8f * alpha));
            g.strokePath (diamond, juce::PathStrokeType (selected ? 1.6f : 1.0f));
            return;
        }
        if (labels)
            shown.push_back ({ rect, std::move (n.lyric), {}, alpha });
        g.setColour (fill.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (rect, 2.5f);
        g.setColour (selected ? juce::Colours::white.withAlpha (0.95f) : colour.brighter (0.4f).withAlpha (0.8f * alpha));
        g.drawRoundedRectangle (rect.reduced (0.5f), 2.5f, selected ? 1.6f : 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f * alpha));
        g.fillRect (rect.withWidth (std::min (3.0f, rect.getWidth())).withTrimmedTop ((1.0f - v) * rect.getHeight()));
        const bool wide = labels && rect.getWidth() > 30.0f && keyHeight_ >= 11.0f;
        if (! wide && ! hasLyric)
            return;
        const auto named = keyNames_.find (n.pitch);
        const auto pitchLabel = named != keyNames_.end() ? named->second : noteName (n.pitch);
        const auto label = n.articulation.empty() ? pitchLabel : pitchLabel + " " + juce::String::fromUTF8 (n.articulation.c_str());
        if (hasLyric)
            shown.back().label = label;   // 가사 뒤에 남는 자리에 쓴다 (paintLyrics)
        else
        {
            g.setColour (juce::Colours::black.withAlpha (0.75f * alpha));
            g.setFont (pitchFont);
            g.drawText (label, rect.withTrimmedLeft (5.0f), juce::Justification::centredLeft, false);
        }
    };

    // 한 트랙의 음: 끄는 동안 고른 음은 옮긴 자리에 (복사면 원래 음을 흐리게 남긴다)
    auto paintTrack = [&] (size_t t, float alpha, const mr::NoteSelection* selection, bool labels)
    {
        const auto& notes = project_.composition().tracks[t].notes;
        const auto colour = project_.summary (t).colour;
        const bool main = t == index;
        auto selectedIn = [selection] (size_t i) { return selection != nullptr && std::binary_search (selection->begin(), selection->end(), i); };
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const bool selected = selectedIn (i);
            if (selected && (moving || resizing))
            {
                if (moving && dragCopy_)
                    drawNote (notes[i], false, 0.5f * alpha, colour, labels);
                continue;
            }
            drawNote (notes[i], selected, alpha, colour, labels);
        }
        if ((moving || resizing) && selection != nullptr)
            for (const size_t i : *selection)
            {
                if (i >= notes.size())
                    continue;
                auto n = notes[i];
                if (moving)
                {
                    n.startBeat = std::max (0.0, n.startBeat + dragBeats_);
                    n.pitch = main ? movedPitch (n.pitch, dragPitch_) : juce::jlimit (0, 127, n.pitch + (drumView() ? 0 : dragPitch_));
                }
                else
                    n.durationBeat = std::max (grid_ * 0.5, n.durationBeat + dragBeats_);
                drawNote (n, true, std::max (alpha, 0.85f), colour, labels);
            }
    };
    for (const size_t t : companionTracks (Companion::shown))
        paintTrack (t, 0.3f, nullptr, false);
    for (const size_t t : companionTracks (Companion::edited))
    {
        const auto found = others_.find (t);
        paintTrack (t, 0.7f, found != others_.end() ? &found->second : nullptr, false);
    }
    paintTrack (index, 1.0f, &selection_, true);
    paintLyrics (g, shown, pitchFont);
    // 녹음 중인 음표: 붉은 테두리
    for (const auto& n : recordingNotes_)
    {
        if (n.startBeat > viewEnd || n.startBeat + n.durationBeat < geo.startBeat)
            continue;
        const auto rect = noteRect (n, geo);
        if (rect.getBottom() < (float) grid.getY() || rect.getY() > (float) grid.getBottom())
            continue;
        g.setColour (colours::danger.withAlpha (0.45f));
        g.fillRoundedRectangle (rect, 2.5f);
        g.setColour (colours::danger);
        g.drawRoundedRectangle (rect.reduced (0.5f), 2.5f, 1.2f);
    }
}

void PianoRollPanel::paintInfo (juce::Graphics& g, juce::Rectangle<int> area)
{
    const size_t index = (size_t) track();
    const auto& track = project_.composition().tracks[index];
    const auto& summary = project_.summary (index);
    auto r = area.reduced (14, 10);
    g.setColour (summary.colour);
    g.fillRoundedRectangle (r.removeFromTop (4).toFloat(), 2.0f);
    r.removeFromTop (8);
    g.setColour (colours::text);
    g.setFont (uiFont (15.0f, true));
    g.drawText (juce::String::fromUTF8 (track.name.c_str()), r.removeFromTop (22), juce::Justification::centredLeft, true);
    g.setFont (uiFont (12.0f));
    auto line = [&] (const juce::String& label, const juce::String& value)
    {
        auto row = r.removeFromTop (18);
        g.setColour (colours::textFaint);
        g.drawText (label, row.removeFromLeft (52), juce::Justification::centredLeft);
        g.setColour (colours::textDim);
        g.drawText (value, row, juce::Justification::centredLeft, true);
    };
    line (juce::String::fromUTF8 ("악기"), juce::String::fromUTF8 (track.instrument.c_str()));
    if (audioTrack())
    {
        paintAudioInfo (g, r);
        return;
    }
    line (juce::String::fromUTF8 ("음표"), juce::String ((int) summary.notes) + juce::String::fromUTF8 ("개"));
    if (summary.notes > 0)
        line (juce::String::fromUTF8 ("음역"), noteName (summary.lowPitch) + " ~ " + noteName (summary.highPitch));
    if (! track.articulation.empty())
        line (juce::String::fromUTF8 ("주법"), juce::String::fromUTF8 (track.articulation.c_str()));
    if (track.transpose != 0)
        line (juce::String::fromUTF8 ("조옮김"), juce::String (track.transpose));
    if (hasSelection())
        line (juce::String::fromUTF8 ("고른 음표"), juce::String ((int) selectionSize()) + juce::String::fromUTF8 ("개")
                                                     + (selectionSize() > selection_.size() ? juce::String::fromUTF8 (" (트랙 여럿)") : juce::String()));
    if (const auto n = companionTracks (Companion::shown).size() + companionTracks (Companion::edited).size(); n > 0)
        line (juce::String::fromUTF8 ("함께"), juce::String ((int) n) + juce::String::fromUTF8 ("트랙 (고치기 ")
                                                + juce::String ((int) companionTracks (Companion::edited).size()) + ")");

    if (gridBox_.isVisible())
    {
        g.setColour (colours::textFaint);
        g.drawText (juce::String::fromUTF8 ("격자"), gridBox_.getBounds().translated (0, -20).withHeight (18), juce::Justification::centredLeft);
    }
}

void PianoRollPanel::paintKeyboard (juce::Graphics& g, juce::Rectangle<int> area)
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area);
    const int top = (int) std::ceil (topPitch_);
    g.setFont (uiFont (std::min (10.0f, keyHeight_ - 2.0f)));
    for (int v = top; v >= lowestVirtual(); --v)
    {
        const int p = pitchOfVirtual (v);
        const float y = pitchY (p);
        if (y > (float) area.getBottom())
            break;
        const auto key = juce::Rectangle<float> ((float) area.getX(), y, (float) area.getWidth(), keyHeight_);
        const bool black = isBlack (p) && ! drumView();
        g.setColour (black ? colours::blackKey : colours::whiteKey);
        g.fillRect (black ? key.withTrimmedRight (key.getWidth() * 0.35f) : key);
        if (black)
        {
            g.setColour (colours::whiteKey);
            g.fillRect (key.withTrimmedLeft (key.getWidth() * 0.65f));
        }
        if ((p == auditionPitch_ && auditionTrack_ == track()) || std::find (typedNotes_.begin(), typedNotes_.end(), p) != typedNotes_.end())
        {
            g.setColour (colours::accent.withAlpha (0.75f));
            g.fillRect (key);
        }
        const auto name = keyNames_.find (p);
        if (name != keyNames_.end() && keyHeight_ >= 8.0f)
        {
            // 드럼과 타악기의 이름 (흰 바탕에 쓴다)
            g.setColour (colours::whiteKey);
            g.fillRect (key.reduced (0.0f, 0.5f));
            g.setColour (colours::background);
            g.drawFittedText (name->second, key.withTrimmedLeft (3.0f).withTrimmedRight (2.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        }
        else if (const auto label = typingLabels_.find (p); label != typingLabels_.end() && keyHeight_ >= 8.0f)
        {
            g.setColour (black ? colours::whiteKey.withAlpha (0.85f) : colours::background.withAlpha (0.55f));
            g.drawText (label->second, key.withTrimmedLeft (4.0f).withWidth (key.getWidth() * 0.6f), juce::Justification::centredLeft, false);
        }
        g.setColour (colours::outline.withAlpha (0.8f));
        g.fillRect (key.withHeight (1.0f).withY (y + keyHeight_ - 1.0f));
        if ((p % 12 == 0 || drumView()) && keyHeight_ >= 8.0f && name == keyNames_.end())
        {
            g.setColour (colours::background);
            g.drawText (noteName (p), key.withTrimmedRight (4.0f), juce::Justification::centredRight, false);
        }
    }
}

std::optional<mr::AutomationLane> PianoRollPanel::currentLane() const
{
    if (laneKey_.empty() || track() < 0)
        return std::nullopt;
    if (laneKey_ == "tempo")
        return tempoLane (project_.composition().tempo);
    if (laneKey_ == "vca")
    {
        const auto* vca = vcaOf (project_.composition(), project_.composition().tracks[(size_t) track()]);
        if (vca == nullptr)
            return std::nullopt;
        if (! vca->gainLane.empty())
            return vca->gainLane;
        mr::AutomationLane fixed;   // 곡선이 없으면 고정값의 점 하나에서 시작한다
        fixed.points.push_back ({ 0.0, vca->gainDb, mr::CurveShape::linear });
        return fixed;
    }
    auto lane = mr::automationLaneOf (project_.composition().tracks[(size_t) track()], laneKey_);
    if (lane.empty())
        return std::nullopt;
    return lane;
}

std::optional<LaneTarget> PianoRollPanel::currentTarget() const
{
    if (laneKey_.empty() || track() < 0)
        return std::nullopt;
    if (laneKey_ == "tempo")
        return tempoTarget (project_.composition().tempo);
    if (laneKey_ == "vca")
    {
        if (const auto* vca = vcaOf (project_.composition(), project_.composition().tracks[(size_t) track()]))
            return vcaTarget (*vca);
        return std::nullopt;
    }
    return automationTarget (project_.composition().tracks[(size_t) track()], laneKey_);
}

void PianoRollPanel::paintLane (juce::Graphics& g)
{
    const auto lane = laneArea();
    const TimelineGeometry geo (timeline_, leftWidth, lane.getRight());

    g.setColour (colours::laneA);
    g.fillRect (lane);
    drawGrid (g, lane, geo, project_.meters());
    g.setColour (colours::outline);
    g.fillRect (0, lane.getY(), getWidth(), 1);

    auto labelArea = juce::Rectangle<int> (0, lane.getY() + 1, leftWidth - 1, lane.getHeight() - 1);
    g.setColour (colours::panel);
    g.fillRect (labelArea);

    // 곡선의 값: 끄는 점, 아니면 재생 위치
    if (const auto target = currentTarget())
        if (const auto curve = currentLane())
        {
            const bool dragging = drag_ == Drag::point && pointIndex_ >= 0 && (size_t) pointIndex_ < laneEdit_.points.size();
            const double value = dragging ? laneEdit_.points[(size_t) pointIndex_].value : curve->valueAt (timeline_.playheadBeat());
            g.setColour (dragging ? colours::text : colours::textDim);
            g.setFont (uiFont (12.0f));
            g.drawText ((dragging ? juce::String::fromUTF8 ("점: ") : juce::String::fromUTF8 ("재생 위치: ")) + target->format (value),
                        labelArea.withTrimmedTop (42).withTrimmedLeft (14).withHeight (18), juce::Justification::centredLeft, true);
        }

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (lane);
    if (laneKey_.empty())
    {
        if (! audioTrack())
            paintVelocities (g, lane, geo);
    }
    else
        paintCurve (g, lane, geo);
    drawPlayhead (g, lane, geo, timeline_.playheadBeat());
}

void PianoRollPanel::paintVelocities (juce::Graphics& g, juce::Rectangle<int> lane, const TimelineGeometry& geo)
{
    const size_t index = (size_t) track();
    const auto& notes = project_.composition().tracks[index].notes;
    const auto& colour = project_.summary (index).colour;
    const auto area = laneValueArea();
    const bool dragging = drag_ == Drag::velocity && anchor_ >= 0;
    const bool together = dragging && isSelected ((size_t) anchor_) && selection_.size() > 1;
    const int delta = dragging ? velocityValue_ - notes[(size_t) anchor_].velocity : 0;

    g.setColour (colours::beatLine);
    g.fillRect (juce::Rectangle<float> ((float) lane.getX(), area.getY(), (float) lane.getWidth(), 1.0f));
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        const float x = geo.x (n.startBeat);
        if (x < (float) lane.getX() - 4.0f || x > (float) lane.getRight() + 4.0f)
            continue;
        int v = n.velocity;
        if (dragging && (int) i == anchor_)
            v = together ? juce::jlimit (1, 127, v + delta) : velocityValue_;
        else if (together && isSelected (i))
            v = juce::jlimit (1, 127, v + delta);
        const float top = area.getBottom() - (float) v / 127.0f * area.getHeight();
        const bool selected = isSelected (i);
        g.setColour (selected ? colour.brighter (0.4f) : colour.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (x - 1.0f, top, 3.0f, area.getBottom() - top));
        g.fillEllipse (x - 3.0f, top - 3.0f, 6.0f, 6.0f);
    }
}

void PianoRollPanel::paintCurve (juce::Graphics& g, juce::Rectangle<int> lane, const TimelineGeometry& geo)
{
    const auto stored = currentLane();
    const auto target = currentTarget();
    if (! stored || ! target)
        return;
    const auto& curve = drag_ == Drag::point ? laneEdit_ : *stored;
    const auto& colour = project_.summary ((size_t) track()).colour;
    const auto area = laneValueArea();
    const auto norm = [&target] (double v) { return target->toNorm (v); };
    g.setColour (colour.withAlpha (0.16f));
    g.fillPath (automationPath (curve, norm, area, geo, true));
    g.setColour (colour);
    g.strokePath (automationPath (curve, norm, area, geo, false), juce::PathStrokeType (1.6f));
    for (size_t i = 0; i < curve.points.size(); ++i)
    {
        const auto& p = curve.points[i];
        const float x = geo.x (p.beat);
        if (x < (float) lane.getX() - 4.0f || x > (float) lane.getRight() + 4.0f)
            continue;
        const float y = laneY (p.value);
        const bool active = drag_ == Drag::point && (int) i == pointIndex_;
        g.setColour (active ? colour : colours::background);
        g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
        g.setColour (colour.brighter (0.3f));
        g.drawEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f, 1.4f);
    }
}

double PianoRollPanel::laneValueAt (float y) const
{
    const auto area = laneValueArea();
    const double norm = juce::jlimit (0.0, 1.0, (double) (area.getBottom() - y) / area.getHeight());
    const auto target = currentTarget();
    return target ? target->round (target->fromNorm (norm)) : 0.0;
}

float PianoRollPanel::laneY (double value) const
{
    const auto area = laneValueArea();
    const auto target = currentTarget();
    const double norm = target ? target->toNorm (value) : 0.0;
    return area.getBottom() - (float) juce::jlimit (0.0, 1.0, norm) * area.getHeight();
}

// ---- 맞히기 ----

juce::Point<float> PianoRollPanel::notePoint (double beat, int pitch) const
{
    const TimelineGeometry geo (timeline_, leftWidth, gridArea().getRight());
    return { geo.x (beat), pitchY (pitch) + keyHeight_ * 0.5f };
}

juce::Point<float> PianoRollPanel::keyPoint (int pitch, float fraction) const
{
    const auto keys = keyboardArea().toFloat();
    return { keys.getX() + keys.getWidth() * juce::jlimit (0.05f, 0.95f, fraction), pitchY (pitch) + keyHeight_ * 0.5f };
}

juce::Point<float> PianoRollPanel::lanePoint (double beat, float fraction) const
{
    const TimelineGeometry geo (timeline_, leftWidth, laneArea().getRight());
    const auto area = laneValueArea();
    return { geo.x (beat), area.getBottom() - fraction * area.getHeight() };
}

int PianoRollPanel::noteAt (juce::Point<float> p, bool& onRightEdge) const
{
    onRightEdge = false;
    const int index = track();
    if (index < 0 || ! gridArea().toFloat().contains (p))
        return -1;
    const TimelineGeometry geo (timeline_, leftWidth, gridArea().getRight());
    const auto& notes = project_.composition().tracks[(size_t) index].notes;
    for (size_t k = notes.size(); k-- > 0;)   // 나중에 그린 음이 위에 있다
    {
        const auto rect = noteRect (notes[k], geo).withY (pitchY (notes[k].pitch)).withHeight (keyHeight_);
        if (rect.contains (p))
        {
            onRightEdge = ! drumView() && rect.getWidth() > 9.0f && p.x > rect.getRight() - std::min (6.0f, rect.getWidth() / 3.0f);
            return (int) k;
        }
    }
    return -1;
}

int PianoRollPanel::velocityBarAt (juce::Point<float> p) const
{
    const int index = track();
    if (index < 0)
        return -1;
    const TimelineGeometry geo (timeline_, leftWidth, laneArea().getRight());
    const auto& notes = project_.composition().tracks[(size_t) index].notes;
    int best = -1;
    float bestDistance = 5.0f;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const float d = std::abs (geo.x (notes[i].startBeat) - p.x);
        // 같은 자리의 막대가 여럿이면 고른 음이 먼저
        if (d < bestDistance || (d <= bestDistance && best >= 0 && isSelected (i) && ! isSelected ((size_t) best)))
        {
            best = (int) i;
            bestDistance = d;
        }
    }
    return best;
}

int PianoRollPanel::pointAt (juce::Point<float> p) const
{
    const auto lane = currentLane();
    if (! lane)
        return -1;
    const TimelineGeometry geo (timeline_, leftWidth, laneArea().getRight());
    for (size_t i = 0; i < lane->points.size(); ++i)
        if (juce::Point<float> (geo.x (lane->points[i].beat), laneY (lane->points[i].value)).getDistanceFrom (p) <= 6.0f)
            return (int) i;
    return -1;
}

// ---- 여러 트랙 ----

void PianoRollPanel::setCompanion (size_t t, Companion mode)
{
    if (! project_.loaded() || t >= project_.trackCount())
        return;
    const auto& name = project_.composition().tracks[t].name;
    if (mode == Companion::none)
        companions_.erase (name);
    else
        companions_[name] = mode;
    if (mode != Companion::edited)
        others_.erase (t);
    repaint();
}

PianoRollPanel::Companion PianoRollPanel::companion (size_t t) const
{
    if (! project_.loaded() || t >= project_.trackCount())
        return Companion::none;
    const auto found = companions_.find (project_.composition().tracks[t].name);
    return found != companions_.end() ? found->second : Companion::none;
}

std::vector<size_t> PianoRollPanel::companionTracks (Companion mode) const
{
    std::vector<size_t> out;
    if (! project_.loaded() || mode == Companion::none)
        return out;
    const int main = track();
    for (size_t t = 0; t < project_.trackCount(); ++t)
        if ((int) t != main && project_.composition().tracks[t].instrument != "audio" && companion (t) == mode)
            out.push_back (t);
    return out;
}

bool PianoRollPanel::editable (size_t t) const
{
    return (int) t != track() && companion (t) == Companion::edited && project_.composition().tracks[t].instrument != "audio";
}

juce::PopupMenu PianoRollPanel::companionMenu()
{
    juce::PopupMenu m;
    const int main = track();
    if (! project_.loaded() || main < 0)
        return m;
    m.addSectionHeader (juce::String::fromUTF8 ("함께 보기 (흐리게)"));
    for (size_t t = 0; t < project_.trackCount(); ++t)
    {
        if ((int) t == main || project_.composition().tracks[t].instrument == "audio")
            continue;
        const auto mode = companion (t);
        m.addItem (juce::String::fromUTF8 (project_.composition().tracks[t].name.c_str()), true, mode != Companion::none,
                   [this, t, mode] { setCompanion (t, mode == Companion::none ? Companion::shown : Companion::none); });
    }
    m.addSectionHeader (juce::String::fromUTF8 ("함께 고치기 (고르기, 옮기기, 길이, 지우기, 다듬기)"));
    for (size_t t = 0; t < project_.trackCount(); ++t)
    {
        if ((int) t == main || project_.composition().tracks[t].instrument == "audio")
            continue;
        const auto mode = companion (t);
        m.addItem (juce::String::fromUTF8 (project_.composition().tracks[t].name.c_str()), true, mode == Companion::edited,
                   [this, t, mode] { setCompanion (t, mode == Companion::edited ? Companion::shown : Companion::edited); });
    }
    m.addSeparator();
    m.addItem (juce::String::fromUTF8 ("모두 함께 보기"), true, false, [this, main]
    {
        for (size_t t = 0; t < project_.trackCount(); ++t)
            if ((int) t != main && companion (t) == Companion::none)
                setCompanion (t, Companion::shown);
    });
    m.addItem (juce::String::fromUTF8 ("모두 끄기"), ! companions_.empty(), false, [this]
    {
        companions_.clear();
        others_.clear();
        repaint();
    });
    return m;
}

bool PianoRollPanel::inKey (double beat, int pitch) const
{
    if (! project_.loaded())
        return false;
    const auto key = mr::keyAt (project_.composition(), beat);
    return key && mr::inKeyScale (*key, pitch);
}

std::optional<PianoRollPanel::Hit> PianoRollPanel::hitNote (juce::Point<float> p, bool shown) const
{
    bool edge = false;
    if (const int k = noteAt (p, edge); k >= 0)
        return Hit { (size_t) track(), (size_t) k, edge };
    if (track() < 0 || ! gridArea().toFloat().contains (p))
        return std::nullopt;
    const TimelineGeometry geo (timeline_, leftWidth, gridArea().getRight());
    for (const auto mode : { Companion::edited, Companion::shown })
    {
        if (mode == Companion::shown && ! shown)
            continue;
        const auto tracks = companionTracks (mode);
        for (size_t i = tracks.size(); i-- > 0;)
        {
            const auto& notes = project_.composition().tracks[tracks[i]].notes;
            for (size_t k = notes.size(); k-- > 0;)
            {
                const auto rect = noteRect (notes[k], geo).withY (pitchY (notes[k].pitch)).withHeight (keyHeight_);
                if (rect.contains (p))
                {
                    const bool right = mode == Companion::edited && ! drumView() && rect.getWidth() > 9.0f
                                       && p.x > rect.getRight() - std::min (6.0f, rect.getWidth() / 3.0f);
                    return Hit { tracks[i], k, right };
                }
            }
        }
    }
    return std::nullopt;
}

bool PianoRollPanel::hasSelection() const noexcept
{
    if (! selection_.empty())
        return true;
    for (const auto& [t, sel] : others_)
        if (! sel.empty())
            return true;
    return false;
}

size_t PianoRollPanel::selectionSize() const noexcept
{
    size_t n = selection_.size();
    for (const auto& [t, sel] : others_)
        n += sel.size();
    return n;
}

bool PianoRollPanel::commitTracks (TrackEdits results, const juce::String& name)
{
    const int index = track();
    if (results.empty())
        return false;
    if (results.size() == 1 && (int) results[0].first == index)
    {
        others_.clear();
        return commitNotes (results[0].second, name);
    }
    std::vector<Project::Change> changes;
    for (const auto& [t, r] : results)
        changes.push_back ({ (int) t, {}, mr::withNotes (project_.trackJson (t), r.notes) });
    juce::String error;
    {
        const juce::ScopedValueSetter<bool> committing (committing_, true);
        if (! project_.edit (changes, name, error))
        {
            status (error.replaceCharacter ('\n', ' '), true);
            return false;
        }
    }
    selection_.clear();
    others_.clear();
    for (auto& [t, r] : results)
        if ((int) t == index)
            selection_ = r.selection;
        else
            others_[t] = r.selection;
    pruneSelection();
    repaint();
    return true;
}

bool PianoRollPanel::editSelection (const std::function<mr::NoteEditResult (const std::vector<mr::EditNote>&, const mr::NoteSelection&)>& edit,
                                    const juce::String& name)
{
    const int index = track();
    if (index < 0)
        return false;
    TrackEdits results;
    if (! selection_.empty())
        results.push_back ({ (size_t) index, edit (project_.editNotes ((size_t) index), selection_) });
    for (const auto& [t, sel] : others_)
        if (! sel.empty() && editable (t))
            results.push_back ({ t, edit (project_.editNotes (t), sel) });
    return commitTracks (std::move (results), name);
}

// ---- 편집 넣기 ----

bool PianoRollPanel::commitNotes (const mr::NoteEditResult& result, const juce::String& name)
{
    const int index = track();
    if (index < 0)
        return false;
    juce::String error;
    const juce::ScopedValueSetter<bool> committing (committing_, true);
    if (! project_.editTrack ((size_t) index, mr::withNotes (project_.trackJson ((size_t) index), result.notes), name, error))
    {
        status (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    selection_ = result.selection;
    pruneSelection();
    repaint();
    return true;
}

bool PianoRollPanel::commitLane (const std::string& key, const mr::AutomationLane& lane, const juce::String& name)
{
    const int index = track();
    if (index < 0)
        return false;
    juce::String error;
    const juce::ScopedValueSetter<bool> committing (committing_, true);
    if (key == "tempo")
    {
        // 곡 전체의 템포 (재생기를 다시 만든다)
        const auto& c = project_.composition();
        if (lane.empty() || ! project_.editTempo (mr::tempoJson (tempoPoints (lane)), mr::timeSignatureJson (c.beatsPerBar, c.beatUnit),
                                                  name == juce::String::fromUTF8 ("곡선 점 옮기기") ? juce::String::fromUTF8 ("템포 점 옮기기") : name, error))
        {
            if (error.isNotEmpty())
                status (error.replaceCharacter ('\n', ' '), true);
            return false;
        }
        repaint();
        return true;
    }
    if (key == "vca")
    {
        // 속한 VCA 의 곡선 (곡의 vcas): 점을 다 지우면 곡선을 지우고 고정값으로
        const auto& c = project_.composition();
        const auto* vca = vcaOf (c, c.tracks[(size_t) index]);
        auto list = project_.property ("vcas");
        if (vca == nullptr || list.getArray() == nullptr)
            return false;
        auto* o = (*list.getArray())[(int) (vca - c.vcas.data())].getDynamicObject();
        auto automation = o->getProperty ("automation");
        if (! automation.isObject())
            automation = juce::var (new juce::DynamicObject());
        if (lane.empty())
            automation.getDynamicObject()->removeProperty ("gainDb");
        else
            automation.getDynamicObject()->setProperty ("gainDb", mr::laneJson (lane));
        if (automation.getDynamicObject()->getProperties().isEmpty())
            o->removeProperty ("automation");
        else
            o->setProperty ("automation", automation);
        if (! project_.edit ({ Project::Change { -1, "vcas", list } }, name, error))
        {
            status (error.replaceCharacter ('\n', ' '), true);
            return false;
        }
        repaint();
        return true;
    }
    if (! project_.editTrack ((size_t) index, mr::withTrackAutomation (project_.trackJson ((size_t) index), key, lane), name, error))
    {
        status (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    repaint();
    return true;
}

void PianoRollPanel::showCurveShapeMenu (int point)
{
    const auto lane = currentLane();
    if (! lane || point < 0 || (size_t) point >= lane->points.size())
        return;
    const auto current = lane->points[(size_t) point].curve;
    juce::PopupMenu m;
    m.addSectionHeader (juce::String::fromUTF8 ("다음 점까지의 모양"));
    m.addItem (1, juce::String::fromUTF8 ("직선"), true, current == mr::CurveShape::linear);
    m.addItem (2, juce::String::fromUTF8 ("계단"), true, current == mr::CurveShape::step);
    if (laneKey_ != "tempo")   // 템포는 계단과 램프(직선)만
    {
        m.addItem (3, juce::String::fromUTF8 ("지수"), true, current == mr::CurveShape::exponential);
        m.addItem (4, juce::String::fromUTF8 ("부드럽게"), true, current == mr::CurveShape::smooth);
    }
    m.addSeparator();
    m.addItem (5, juce::String::fromUTF8 ("점 지우기"));
    const auto controller = laneKey_;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                     [safe = juce::Component::SafePointer<PianoRollPanel> (this), point, controller] (int choice)
                     {
                         if (safe == nullptr || choice == 0 || safe->laneKey_ != controller)
                             return;
                         const auto stored = safe->currentLane();
                         if (! stored || (size_t) point >= stored->points.size())
                             return;
                         auto lane = *stored;
                         if (choice == 5)
                         {
                             lane.points.erase (lane.points.begin() + point);
                             safe->commitLane (controller, lane, juce::String::fromUTF8 ("곡선 점 지우기"));
                             return;
                         }
                         const mr::CurveShape shapes[] = { mr::CurveShape::linear, mr::CurveShape::step, mr::CurveShape::exponential,
                                                           mr::CurveShape::smooth };
                         lane.points[(size_t) point].curve = shapes[choice - 1];
                         safe->commitLane (controller, lane, juce::String::fromUTF8 ("곡선 모양"));
                     });
}

void PianoRollPanel::showAddCurveMenu()
{
    const int index = track();
    if (index < 0)
        return;
    const auto& spec = project_.composition().tracks[(size_t) index];
    auto keys = std::make_shared<std::vector<std::string>>();
    juce::PopupMenu controllers, mixer, sends, effects;
    for (const auto& t : automationTargets (spec))
    {
        if (! mr::automationLaneOf (spec, t.key).empty())
            continue;
        const int id = 1 + (int) keys->size();
        keys->push_back (t.key);
        auto& menu = t.group == LaneTarget::Group::controller ? controllers : t.group == LaneTarget::Group::mixer ? mixer
                   : t.group == LaneTarget::Group::send ? sends : effects;
        menu.addItem (id, t.label);
    }
    juce::PopupMenu m;
    m.addSubMenu (juce::String::fromUTF8 ("믹서 (볼륨, 밸런스)"), mixer, mixer.getNumItems() > 0);
    m.addSubMenu (juce::String::fromUTF8 ("센드"), sends, sends.getNumItems() > 0);
    m.addSubMenu (juce::String::fromUTF8 ("효과 매개변수"), effects, effects.getNumItems() > 0);
    m.addSubMenu (juce::String::fromUTF8 ("컨트롤러"), controllers, controllers.getNumItems() > 0 && ! audioTrack());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&laneBox_),
                     [safe = juce::Component::SafePointer<PianoRollPanel> (this), keys] (int choice)
                     {
                         if (safe != nullptr && choice > 0 && (size_t) choice <= keys->size())
                             safe->addLane ((*keys)[(size_t) choice - 1]);
                     });
}

// ---- 명령 ----

bool PianoRollPanel::canPaste() const { return track() >= 0 && ! clipboard().empty(); }

void PianoRollPanel::setTypingKeys (std::map<int, juce::String> labels)
{
    if (labels == typingLabels_)
        return;
    typingLabels_ = std::move (labels);
    repaint (keyboardArea());
}

void PianoRollPanel::setRecordingNotes (std::vector<mr::Note> notes)
{
    if (notes.empty() && recordingNotes_.empty())
        return;
    recordingNotes_ = std::move (notes);
    repaint (gridArea());
}

void PianoRollPanel::setSelection (mr::NoteSelection selection)
{
    selection_ = std::move (selection);
    others_.clear();
    pruneSelection();
    repaint();
}

void PianoRollPanel::setTypedNotes (std::vector<int> pitches)
{
    if (pitches == typedNotes_)
        return;
    typedNotes_ = std::move (pitches);
    repaint (keyboardArea());
}

std::optional<std::pair<double, double>> PianoRollPanel::selectionRange() const
{
    const int index = track();
    if (index < 0 || ! hasSelection())
        return std::nullopt;
    double from = 1.0e300, to = -1.0e300;
    auto take = [&] (size_t t, const mr::NoteSelection& sel)
    {
        const auto& notes = project_.composition().tracks[t].notes;
        for (const size_t i : sel)
            if (i < notes.size())
            {
                from = std::min (from, notes[i].startBeat);
                to = std::max (to, notes[i].startBeat + notes[i].durationBeat);
            }
    };
    take ((size_t) index, selection_);
    for (const auto& [t, sel] : others_)
        if (t < project_.trackCount())
            take (t, sel);
    if (to <= from)
        return std::nullopt;
    return std::make_pair (from, to);
}

bool PianoRollPanel::selectAll()
{
    const int index = track();
    if (index < 0)
        return false;
    selection_.clear();
    for (size_t i = 0; i < project_.composition().tracks[(size_t) index].notes.size(); ++i)
        selection_.push_back (i);
    others_.clear();
    for (const size_t t : companionTracks (Companion::edited))
        for (size_t i = 0; i < project_.composition().tracks[t].notes.size(); ++i)
            others_[t].push_back (i);
    repaint();
    return hasSelection();
}

void PianoRollPanel::deselect()
{
    selection_.clear();
    others_.clear();
    repaint();
}

bool PianoRollPanel::deleteSelection()
{
    return editSelection ([] (const auto& notes, const auto& sel) { return mr::deleteNotes (notes, sel); }, juce::String::fromUTF8 ("음표 지우기"));
}

bool PianoRollPanel::copySelection()
{
    if (selection_.empty() || track() < 0)
        return false;
    clipboard() = mr::selectedNotes (project_.editNotes ((size_t) track()), selection_);
    status (juce::String::fromUTF8 ("음표 ") + juce::String ((int) clipboard().size()) + juce::String::fromUTF8 ("개를 복사했습니다."));
    return true;
}

bool PianoRollPanel::cutSelection()
{
    if (! copySelection())
        return false;
    return commitNotes (mr::deleteNotes (project_.editNotes ((size_t) track()), selection_), juce::String::fromUTF8 ("음표 잘라내기"));
}

bool PianoRollPanel::paste (double atBeat)
{
    if (! canPaste())
        return false;
    return commitNotes (mr::pasteNotes (project_.editNotes ((size_t) track()), clipboard(), snapFloor (atBeat)),
                        juce::String::fromUTF8 ("음표 붙이기"));
}

bool PianoRollPanel::duplicateSelection()
{
    if (selection_.empty() || track() < 0)
        return false;
    const auto notes = project_.editNotes ((size_t) track());
    double first = notes[selection_.front()].note.startBeat, last = first;
    for (const size_t i : selection_)
    {
        first = std::min (first, notes[i].note.startBeat);
        last = std::max (last, notes[i].note.startBeat + notes[i].note.durationBeat);
    }
    const double span = std::max (grid_, std::ceil ((last - first) / grid_ - 1.0e-9) * grid_);
    return commitNotes (mr::copyNotes (notes, selection_, span, 0), juce::String::fromUTF8 ("음표 복제"));
}

bool PianoRollPanel::transposeSelection (int semitones)
{
    if (audioTrack())
        return audioNote_ && semitones != 0 && shiftAudioNote (*audioNote_, semitones > 0 ? 1 : -1);
    return editSelection ([semitones] (const auto& notes, const auto& sel) { return mr::moveNotes (notes, sel, 0.0, semitones); },
                          juce::String::fromUTF8 ("조옮김"));
}

bool PianoRollPanel::nudgeSelection (int gridSteps)
{
    const double beats = gridSteps * grid_;
    return editSelection ([beats] (const auto& notes, const auto& sel) { return mr::moveNotes (notes, sel, beats, 0); },
                          juce::String::fromUTF8 ("음표 옮기기"));
}

bool PianoRollPanel::setArticulation (const std::string& articulation)
{
    if (selection_.empty() || track() < 0)
        return false;
    const auto label = articulation.empty() ? juce::String::fromUTF8 ("트랙 기본값") : juce::String::fromUTF8 (articulation.c_str());
    return commitNotes (mr::setArticulation (project_.editNotes ((size_t) track()), selection_, articulation),
                        juce::String::fromUTF8 ("아티큘레이션: ") + label);
}

void PianoRollPanel::showArticulationMenu()
{
    const int index = track();
    if (index < 0 || selection_.empty())
        return;
    auto names = articulationsFor ? articulationsFor ((size_t) index) : std::vector<std::string>();
    const auto& notes = project_.composition().tracks[(size_t) index].notes;
    // 고른 음표가 모두 같은 아티큘레이션이면 표시한다
    std::string common = notes[selection_.front()].articulation;
    for (const size_t i : selection_)
        if (notes[i].articulation != common)
            common = "\x01";
    juce::PopupMenu m;
    m.addSectionHeader (juce::String::fromUTF8 ("아티큘레이션 (음표 ") + juce::String ((int) selection_.size()) + juce::String::fromUTF8 ("개)"));
    m.addItem (1, juce::String::fromUTF8 ("트랙 기본값"), true, common.empty());
    for (size_t i = 0; i < names.size(); ++i)
        m.addItem ((int) i + 2, juce::String::fromUTF8 (names[i].c_str()), true, common == names[i]);
    if (names.empty())
        m.addItem (-1, juce::String::fromUTF8 ("이 악기에는 아티큘레이션이 없습니다"), false);
    m.addSeparator();
    m.addSubMenu (juce::String::fromUTF8 ("음표 다듬기"), toolsMenu());
    m.showMenuAsync (juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<PianoRollPanel> (this), names] (int result)
    {
        if (safe == nullptr || result <= 0)
            return;
        safe->setArticulation (result == 1 ? std::string() : names[(size_t) result - 2]);
    });
}

// ---- 코드와 스케일 넣기 ----

bool PianoRollPanel::insertChord (int rootPitch, const std::string& chordId, double atBeat)
{
    const auto* chord = mr::findChordType (chordId);
    if (chord == nullptr || track() < 0)
        return false;
    using L = HarmonyOptions::ChordLength;
    const double length = harmony_->chordLength == L::grid ? grid_ : harmony_->chordLength == L::beat ? 1.0
                        : harmony_->chordLength == L::twoBeats ? 2.0 : project_.meters().barBeatsAt (atBeat);
    const auto notes = mr::chordNotes (rootPitch, *chord, harmony_->inversion, std::max (0.0, atBeat), length, lastVelocity_);
    if (notes.empty())
        return false;
    const auto symbol = juce::String::fromUTF8 (mr::chordSymbol (rootPitch, *chord).c_str());
    if (! commitNotes (mr::addNotes (project_.editNotes ((size_t) track()), notes), juce::String::fromUTF8 ("코드 넣기: ") + symbol))
        return false;
    std::vector<int> pitches;
    juce::StringArray names;
    for (const auto& n : notes)
    {
        pitches.push_back (n.pitch);
        names.add (juce::String (mr::pitchClassName (n.pitch)) + juce::String (n.pitch / 12 - 1));
    }
    auditionChord (pitches, lastVelocity_);
    status (juce::String::fromUTF8 ("넣은 코드: ") + symbol + " (" + names.joinIntoString (", ") + ")");
    return true;
}

bool PianoRollPanel::insertScale (int rootPitch, const std::string& scaleId, double atBeat)
{
    const auto* scale = mr::findScaleType (scaleId);
    if (scale == nullptr || track() < 0)
        return false;
    const auto notes = mr::scaleNotes (rootPitch, *scale, harmony_->octaves, harmony_->direction, std::max (0.0, atBeat), grid_, lastVelocity_);
    if (notes.empty())
        return false;
    const auto name = juce::String (mr::pitchClassName (rootPitch)) + " " + scaleLabel (scale->id);
    if (! commitNotes (mr::addNotes (project_.editNotes ((size_t) track()), notes), juce::String::fromUTF8 ("스케일 넣기: ") + name))
        return false;
    auditionBriefly (notes.front().pitch, lastVelocity_);
    status (juce::String::fromUTF8 ("넣은 스케일: ") + name + " (" + juce::String ((int) notes.size()) + juce::String::fromUTF8 ("음)"));
    return true;
}

juce::PopupMenu PianoRollPanel::insertMenu (bool chords, std::optional<int> rootPitch, double atBeat)
{
    juce::Component::SafePointer<PianoRollPanel> safe (this);
    auto make = [safe, chords, atBeat, options = harmony_] (int root)
    {
        if (chords)
            return chordMenu (root, options, [safe, root, atBeat] (const mr::ChordType& c)
            {
                if (safe != nullptr)
                    safe->insertChord (root, c.id, atBeat);
            });
        return scaleMenu (root, options, [safe, root, atBeat] (const mr::ScaleType& sc)
        {
            if (safe != nullptr)
                safe->insertScale (root, sc.id, atBeat);
        });
    };
    return rootPitch ? make (*rootPitch) : rootMenu (make);
}

void PianoRollPanel::showInsertMenu (int pitch, double beat)
{
    if (track() < 0)
        return;
    const auto& meters = project_.meters();
    const auto where = juce::String (meters.barAt (beat) + 1) + juce::String::fromUTF8 ("마디 ")
                       + juce::String (meters.beatInBar (beat) + 1.0, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + juce::String::fromUTF8 ("박");
    const auto note = juce::String (mr::pitchClassName (pitch)) + juce::String (pitch / 12 - 1);
    juce::PopupMenu m;
    m.addSectionHeader (where);
    m.addSubMenu (juce::String::fromUTF8 ("코드 넣기 (근음 ") + note + ")", insertMenu (true, pitch, beat));
    m.addSubMenu (juce::String::fromUTF8 ("스케일 넣기 (") + note + juce::String::fromUTF8 (" 부터)"), insertMenu (false, pitch, beat));
    m.showMenuAsync (juce::PopupMenu::Options());
}

void PianoRollPanel::auditionChord (const std::vector<int>& pitches, int velocity)
{
    const int index = track();
    if (index < 0 || ! onAudition)
        return;
    stopAudition();
    for (const int p : pitches)
        onAudition ((size_t) index, p, juce::jlimit (1, 127, velocity), {}, 1);
    juce::Timer::callAfterDelay (700, [safe = juce::Component::SafePointer<PianoRollPanel> (this), pitches, index]
    {
        if (safe != nullptr && safe->onAudition)
            for (const int p : pitches)
                safe->onAudition ((size_t) index, p, 0, {}, 1);
    });
}

bool PianoRollPanel::quantize()
{
    if (track() < 0 || project_.composition().tracks[(size_t) track()].notes.empty())
        return false;
    return commitNotes (mr::quantizeNotes (project_.editNotes ((size_t) track()), selection_, grid_),
                        juce::String::fromUTF8 ("격자에 맞추기"));
}

// ---- 음표 다듬기 ----

void PianoRollPanel::setGrid (double beats)
{
    for (int i = 0; i < (int) std::size (gridChoices); ++i)
        if (std::abs (gridChoices[i].beats - beats) < 1.0e-9)
            gridBox_.setSelectedId (i + 1, juce::sendNotificationSync);
}

namespace
{
juce::String percent (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; }
} // namespace

bool PianoRollPanel::applyTool (const std::function<mr::NoteEditResult (const std::vector<mr::EditNote>&, const mr::NoteSelection&)>& tool,
                                const juce::String& name)
{
    const int index = track();
    if (index < 0)
        return false;
    // 고른 음이 있으면 고른 음이 있는 트랙마다, 없으면 보이는 트랙의 모든 음
    TrackEdits results;
    auto run = [&] (size_t t, const mr::NoteSelection& sel)
    {
        const auto notes = project_.editNotes (t);
        if (notes.empty())
            return;
        auto result = tool (notes, sel);
        bool changed = notes.size() != result.notes.size();
        for (size_t i = 0; i < notes.size() && i < result.notes.size() && ! changed; ++i)
        {
            const auto& a = notes[i].note;
            const auto& b = result.notes[i].note;
            changed = a.pitch != b.pitch || a.velocity != b.velocity || std::abs (a.startBeat - b.startBeat) > 1.0e-9
                      || std::abs (a.durationBeat - b.durationBeat) > 1.0e-9;
        }
        if (changed)
            results.push_back ({ t, std::move (result) });
    };
    if (! hasSelection())
        run ((size_t) index, selection_);
    else
    {
        if (! selection_.empty())
            run ((size_t) index, selection_);
        for (const auto& [t, sel] : others_)
            if (! sel.empty() && editable (t))
                run (t, sel);
    }
    if (results.empty())
    {
        status (name + juce::String::fromUTF8 (": 바뀐 음표가 없습니다"), false);
        return false;
    }
    return commitTracks (std::move (results), name);
}

bool PianoRollPanel::quantizeWith (const mr::QuantizeOptions& options, const juce::String& name)
{
    return applyTool ([options] (const auto& notes, const auto& selection) { return mr::quantizeNotes (notes, selection, options); }, name);
}

bool PianoRollPanel::applyGroove (const mr::Groove& groove, double timing, double velocity)
{
    return applyTool ([groove, timing, velocity] (const auto& notes, const auto& selection) { return mr::applyGroove (notes, selection, groove, timing, velocity); },
                      juce::String::fromUTF8 ("그루브 입히기: ") + grooveName (groove));
}

bool PianoRollPanel::saveGrooveFromNotes (const juce::String& name, juce::String& error)
{
    if (track() < 0 || audioTrack() || grooveFolder == juce::File())
        return false;
    const auto& all = project_.composition().tracks[(size_t) track()].notes;
    std::vector<mr::Note> chosen;
    if (selection_.empty())
        chosen = all;
    else
        for (const size_t i : selection_)
            if (i < all.size())
                chosen.push_back (all[i]);
    if (chosen.empty())
    {
        error = juce::String::fromUTF8 ("음표가 없습니다.");
        return false;
    }
    double from = 1.0e9, to = 0.0;
    for (const auto& n : chosen)
    {
        from = std::min (from, n.startBeat);
        to = std::max (to, n.startBeat + 1.0e-6);
    }
    const auto& meters = project_.meters();
    const double barStart = meters.barStart (meters.barAt (from));
    auto groove = mr::grooveFromNotes (chosen, barStart, to, grid_, meters.barBeatsAt (barStart));
    return saveGroove (grooveFolder, groove, name, error);
}

bool PianoRollPanel::scaleVelocities (double factor)
{
    return applyTool ([factor] (const auto& notes, const auto& selection) { return mr::scaleVelocities (notes, selection, factor); },
                      factor > 1.0 ? juce::String::fromUTF8 ("벨로시티 넓히기") : juce::String::fromUTF8 ("벨로시티 좁히기"));
}

bool PianoRollPanel::rampVelocities (bool up)
{
    return applyTool ([up] (const auto& notes, const auto& selection)
    {
        int lo = 127, hi = 1;
        auto visit = [&] (size_t i) { lo = std::min (lo, notes[i].note.velocity); hi = std::max (hi, notes[i].note.velocity); };
        if (selection.empty())
            for (size_t i = 0; i < notes.size(); ++i)
                visit (i);
        else
            for (const size_t i : selection)
                visit (i);
        if (hi - lo < 8)   // 모두 비슷하면 가운데에서 ±16
        {
            const int mid = (lo + hi) / 2;
            lo = std::max (1, mid - 16);
            hi = std::min (127, mid + 16);
        }
        return up ? mr::rampVelocities (notes, selection, lo, hi) : mr::rampVelocities (notes, selection, hi, lo);
    }, up ? juce::String::fromUTF8 ("크레셴도") : juce::String::fromUTF8 ("디미누엔도"));
}

bool PianoRollPanel::humanize (int velocity, double milliseconds)
{
    if (track() < 0)
        return false;
    double first = 0.0;
    const auto& notes = project_.composition().tracks[(size_t) track()].notes;
    if (! selection_.empty())
        first = notes[selection_.front()].startBeat;
    else if (! notes.empty())
        first = notes.front().startBeat;
    const double beats = milliseconds / 1000.0 * project_.tempo().bpmAt (first) / 60.0;
    const auto seed = (uint64_t) juce::Time::getHighResolutionTicks() + ++humanizeCount_;   // 누를 때마다 다른 흔들림
    return applyTool ([velocity, beats, seed] (const auto& n, const auto& selection) { return mr::humanizeNotes (n, selection, velocity, beats, seed); },
                      juce::String::fromUTF8 ("휴머나이즈"));
}

bool PianoRollPanel::legato()
{
    return applyTool ([] (const auto& notes, const auto& selection) { return mr::legatoNotes (notes, selection); }, juce::String::fromUTF8 ("레가토"));
}

bool PianoRollPanel::setLengths (double beats)
{
    return applyTool ([beats] (const auto& notes, const auto& selection) { return mr::setLengths (notes, selection, beats); },
                      juce::String::fromUTF8 ("길이 맞추기"));
}

bool PianoRollPanel::snapToScale (int root, const std::string& scaleId)
{
    const auto* scale = mr::findScaleType (scaleId);
    if (scale == nullptr)
        return false;
    const auto intervals = scale->intervals;
    return applyTool ([root, intervals] (const auto& notes, const auto& selection) { return mr::snapToScale (notes, selection, root, intervals); },
                      juce::String::fromUTF8 ("음계에 맞추기: ") + juce::String (mr::pitchClassName (root)) + " " + scaleLabel (scaleId));
}

bool PianoRollPanel::retrograde()
{
    return applyTool ([] (const auto& notes, const auto& selection) { return mr::retrogradeNotes (notes, selection); }, juce::String::fromUTF8 ("역행"));
}

bool PianoRollPanel::invert()
{
    return applyTool ([] (const auto& notes, const auto& selection) { return mr::invertNotes (notes, selection); }, juce::String::fromUTF8 ("반행"));
}

bool PianoRollPanel::arpeggiate (mr::ArpeggioOrder order)
{
    const double step = grid_;
    const auto name = order == mr::ArpeggioOrder::up ? juce::String::fromUTF8 ("아르페지오 (위로)")
                    : order == mr::ArpeggioOrder::down ? juce::String::fromUTF8 ("아르페지오 (아래로)") : juce::String::fromUTF8 ("아르페지오 (위아래로)");
    return applyTool ([order, step] (const auto& notes, const auto& selection) { return mr::arpeggiateNotes (notes, selection, order, step); }, name);
}

bool PianoRollPanel::strum (bool upward, double milliseconds)
{
    if (track() < 0)
        return false;
    const auto& notes = project_.composition().tracks[(size_t) track()].notes;
    const double first = ! selection_.empty() ? notes[selection_.front()].startBeat : notes.empty() ? 0.0 : notes.front().startBeat;
    const double spread = milliseconds / 1000.0 * project_.tempo().bpmAt (first) / 60.0;
    return applyTool ([spread, upward] (const auto& n, const auto& selection) { return mr::strumNotes (n, selection, spread, upward); },
                      upward ? juce::String::fromUTF8 ("스트럼 (낮은 음부터)") : juce::String::fromUTF8 ("스트럼 (높은 음부터)"));
}

juce::PopupMenu PianoRollPanel::toolsMenu()
{
    juce::Component::SafePointer<PianoRollPanel> safe (this);
    const bool any = track() >= 0 && ! project_.composition().tracks[(size_t) track()].notes.empty() && ! audioTrack();
    const double grid = grid_;
    juce::PopupMenu m;
    m.addSectionHeader (selection_.empty() ? juce::String::fromUTF8 ("트랙의 모든 음표") : juce::String::fromUTF8 ("고른 음표 ") + juce::String ((int) selection_.size()) + juce::String::fromUTF8 ("개"));

    juce::PopupMenu q;
    for (const double strength : { 1.0, 0.75, 0.5, 0.25 })
        q.addItem (juce::String::fromUTF8 ("시작을 ") + percent (strength), any, false, [safe, grid, strength]
        {
            if (safe != nullptr)
                safe->quantizeWith ({ grid, strength, 0.0, false }, juce::String::fromUTF8 ("격자에 맞추기 ") + percent (strength));
        });
    q.addItem (juce::String::fromUTF8 ("시작과 끝"), any, false, [safe, grid]
    {
        if (safe != nullptr)
            safe->quantizeWith ({ grid, 1.0, 0.0, true }, juce::String::fromUTF8 ("시작과 끝을 격자에 맞추기"));
    });
    q.addSeparator();
    for (const double swing : { 0.25, 0.5, 0.75, 1.0 })
        q.addItem (juce::String::fromUTF8 ("스윙 ") + percent (swing) + (swing == 1.0 ? juce::String::fromUTF8 (" (셋잇단)") : juce::String()), any, false,
                   [safe, grid, swing]
                   {
                       if (safe != nullptr)
                           safe->quantizeWith ({ grid, 1.0, swing, false }, juce::String::fromUTF8 ("스윙으로 맞추기 ") + percent (swing));
                   });
    m.addSubMenu (juce::String::fromUTF8 ("격자에 맞추기 (지금 격자)"), q, any);

    // 그루브 입히기: 기본 그루브와 내 그루브
    juce::PopupMenu grooves;
    for (const auto& g : availableGrooves (grooveFolder))
    {
        juce::PopupMenu how;
        const std::tuple<const char*, double, double> amounts[] = { { "시간과 세기", 1.0, 1.0 }, { "반만", 0.5, 0.5 }, { "시간만", 1.0, 0.0 }, { "세기만", 0.0, 1.0 } };
        for (const auto& [label, timing, velocity] : amounts)
            how.addItem (juce::String::fromUTF8 (label), any, false, [safe, g, timing = timing, velocity = velocity]
            {
                if (safe != nullptr)
                    safe->applyGroove (g, timing, velocity);
            });
        grooves.addSubMenu (grooveName (g), how, any);
        if (g.id == "backbeat-accent")
            grooves.addSeparator();   // 기본 그루브와 내 그루브 사이
    }
    grooves.addSeparator();
    grooves.addItem (juce::String::fromUTF8 ("이 음표로 그루브 저장..."), any && grooveFolder != juce::File(), false, [safe]
    {
        if (safe == nullptr)
            return;
        safe->grooveDialog_ = std::make_unique<juce::AlertWindow> (juce::String::fromUTF8 ("그루브 저장"),
                                                                   juce::String::fromUTF8 ("고른 음표(없으면 트랙의 모든 음표)의 어긋남과 강세를 지금 격자로 한 마디 그루브로 저장합니다."),
                                                                   juce::MessageBoxIconType::NoIcon, safe.getComponent());
        safe->grooveDialog_->addTextEditor ("name", juce::String::fromUTF8 (safe->project_.composition().tracks[(size_t) safe->track()].name.c_str()));
        safe->grooveDialog_->addButton (juce::String::fromUTF8 ("저장"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        safe->grooveDialog_->addButton (juce::String::fromUTF8 ("취소"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        safe->grooveDialog_->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int result)
        {
            if (safe == nullptr || safe->grooveDialog_ == nullptr)
                return;
            const auto name = safe->grooveDialog_->getTextEditorContents ("name");
            safe->grooveDialog_.reset();
            juce::String error;
            if (result == 1)
            {
                if (safe->saveGrooveFromNotes (name, error))
                    safe->status (juce::String::fromUTF8 ("그루브를 저장했습니다: ") + name, false);
                else if (error.isNotEmpty())
                    safe->status (error, true);
            }
        }));
    });
    m.addSubMenu (juce::String::fromUTF8 ("그루브 입히기"), grooves, any);

    juce::PopupMenu v;
    v.addItem (juce::String::fromUTF8 ("넓히기 (평균에서 1.25배)"), any, false, [safe] { if (safe != nullptr) safe->scaleVelocities (1.25); });
    v.addItem (juce::String::fromUTF8 ("좁히기 (평균으로 0.8배)"), any, false, [safe] { if (safe != nullptr) safe->scaleVelocities (0.8); });
    v.addSeparator();
    v.addItem (juce::String::fromUTF8 ("크레셴도 (시간 차례로)"), any, false, [safe] { if (safe != nullptr) safe->rampVelocities (true); });
    v.addItem (juce::String::fromUTF8 ("디미누엔도 (시간 차례로)"), any, false, [safe] { if (safe != nullptr) safe->rampVelocities (false); });
    m.addSubMenu (juce::String::fromUTF8 ("벨로시티"), v, any);

    juce::PopupMenu len;
    len.addItem (juce::String::fromUTF8 ("레가토 (다음 음의 시작까지)"), any, false, [safe] { if (safe != nullptr) safe->legato(); });
    len.addItem (juce::String::fromUTF8 ("격자 한 칸으로"), any, false, [safe, grid] { if (safe != nullptr) safe->setLengths (grid); });
    len.addItem (juce::String::fromUTF8 ("격자 반 칸으로 (스타카토)"), any, false, [safe, grid] { if (safe != nullptr) safe->setLengths (grid * 0.5); });
    m.addSubMenu (juce::String::fromUTF8 ("길이"), len, any);

    juce::PopupMenu h;
    h.addItem (juce::String::fromUTF8 ("약하게 (세기 ±4, 시간 ±6 ms)"), any, false, [safe] { if (safe != nullptr) safe->humanize (4, 6.0); });
    h.addItem (juce::String::fromUTF8 ("보통 (세기 ±8, 시간 ±12 ms)"), any, false, [safe] { if (safe != nullptr) safe->humanize (8, 12.0); });
    h.addItem (juce::String::fromUTF8 ("세게 (세기 ±14, 시간 ±20 ms)"), any, false, [safe] { if (safe != nullptr) safe->humanize (14, 20.0); });
    m.addSubMenu (juce::String::fromUTF8 ("휴머나이즈"), h, any);

    juce::PopupMenu sc;
    for (int pc = 0; pc < 12; ++pc)
        sc.addSubMenu (juce::String (mr::pitchClassName (pc)), scaleChoiceMenu (60 + pc, [safe, pc] (const mr::ScaleType& s)
        {
            if (safe != nullptr)
                safe->snapToScale (pc, s.id);
        }));
    m.addSubMenu (juce::String::fromUTF8 ("음계에 맞추기 (밖의 음을 가까운 음으로)"), sc, any);

    juce::PopupMenu chords;
    chords.addItem (juce::String::fromUTF8 ("아르페지오 위로 (지금 격자마다)"), any, false, [safe] { if (safe != nullptr) safe->arpeggiate (mr::ArpeggioOrder::up); });
    chords.addItem (juce::String::fromUTF8 ("아르페지오 아래로"), any, false, [safe] { if (safe != nullptr) safe->arpeggiate (mr::ArpeggioOrder::down); });
    chords.addItem (juce::String::fromUTF8 ("아르페지오 위아래로"), any, false, [safe] { if (safe != nullptr) safe->arpeggiate (mr::ArpeggioOrder::upDown); });
    chords.addSeparator();
    for (const double ms : { 15.0, 30.0, 60.0 })
    {
        chords.addItem (juce::String::fromUTF8 ("스트럼 낮은 음부터 (음마다 ") + juce::String ((int) ms) + " ms)", any, false,
                        [safe, ms] { if (safe != nullptr) safe->strum (true, ms); });
        chords.addItem (juce::String::fromUTF8 ("스트럼 높은 음부터 (음마다 ") + juce::String ((int) ms) + " ms)", any, false,
                        [safe, ms] { if (safe != nullptr) safe->strum (false, ms); });
    }
    m.addSubMenu (juce::String::fromUTF8 ("화음 펼치기 (같은 시작의 음)"), chords, any);

    m.addSeparator();
    m.addItem (juce::String::fromUTF8 ("역행 (시간을 거꾸로)"), any, false, [safe] { if (safe != nullptr) safe->retrograde(); });
    m.addItem (juce::String::fromUTF8 ("반행 (첫 음을 축으로 위아래 뒤집기)"), any, false, [safe] { if (safe != nullptr) safe->invert(); });
    return m;
}

// ---- 마우스 ----

bool PianoRollPanel::audioTrack() const
{
    const int t = track();
    return t >= 0 && project_.loaded() && project_.composition().tracks[(size_t) t].instrument == "audio";
}

void PianoRollPanel::mouseDown (const juce::MouseEvent& e)
{
    if (onPressed)
        onPressed();
    drag_ = Drag::none;
    if (e.getNumberOfClicks() == 1)
        clickAdded_ = -1;
    const bool onRuler = e.y < rulerHeight && e.x >= leftWidth;
    if (! project_.loaded() || (audioTrack() && ! laneArea().contains (e.getPosition()) && ! gridArea().contains (e.getPosition()) && ! onRuler))
        return;
    grabKeyboardFocus();
    const auto pos = e.position;
    if (e.y < rulerHeight && e.x >= leftWidth)
    {
        const TimelineGeometry geo (timeline_, leftWidth, gridArea().getRight());
        if (loopDrag_.begin ((float) e.x, juce::Rectangle<int> (leftWidth, 0, gridArea().getWidth(), rulerHeight), geo, project_.meters(),
                             timeline_.loopRange(), e.mods.isAltDown()))
        {
            drag_ = Drag::loop;
            return;
        }
        drag_ = Drag::ruler;
        if (onSeek)
            onSeek (std::max (0.0, geo.beat ((float) e.x)));
        return;
    }
    const int index = track();
    if (index < 0)
        return;
    const auto& notes = project_.composition().tracks[(size_t) index].notes;
    if (audioTrack() && gridArea().contains (e.getPosition()))
    {
        audioMouseDown (e);
        return;
    }

    if (keyboardArea().contains (e.getPosition()) && ! e.mods.isPopupMenu())
    {
        drag_ = Drag::keys;
        audition (pitchAt (pos.y), keyVelocity (pos.x));
        return;
    }

    if (gridArea().contains (e.getPosition()))
    {
        const auto found = hitNote (pos, false);
        const bool mainHit = found && (int) found->track == index;
        if (found && e.mods.isPopupMenu())
        {
            if (mainHit)
            {
                if (! isSelected (found->note))
                {
                    selection_ = { found->note };
                    others_.clear();
                }
                repaint();
                showArticulationMenu();
            }
            return;
        }
        if (e.mods.isPopupMenu())
        {
            const TimelineGeometry geo (timeline_, leftWidth, gridArea().getRight());
            showInsertMenu (pitchAt (pos.y), e.mods.isCommandDown() ? std::max (0.0, geo.beat (pos.x)) : snapFloor (geo.beat (pos.x)));
            return;
        }
        if (found)
        {
            const size_t hit = found->note;
            auto& sel = mainHit ? selection_ : others_[found->track];
            const bool selected = std::binary_search (sel.begin(), sel.end(), hit);
            pendingToggle_ = false;
            if (e.mods.isShiftDown())
            {
                if (selected)
                    pendingToggle_ = true;
                else
                    sel.insert (std::upper_bound (sel.begin(), sel.end(), hit), hit);
            }
            else if (! selected)
            {
                // 다른 음을 누르면 그 음만 고른다 (다른 트랙의 고른 음도 푼다)
                const mr::NoteSelection only { hit };
                selection_.clear();
                others_.clear();
                if (mainHit)
                    selection_ = only;
                else
                    others_[found->track] = only;
            }
            anchor_ = (int) hit;
            anchorTrack_ = (int) found->track;
            drag_ = found->edge ? Drag::resize : Drag::move;
            dragCopy_ = e.mods.isAltDown();
            dragBeats_ = 0.0;
            dragPitch_ = 0;
            dragMoved_ = false;
            const auto& n = project_.composition().tracks[found->track].notes[hit];
            if (mainHit)
            {
                lastLength_ = n.durationBeat;
                lastVelocity_ = n.velocity;
            }
            audition (n.pitch, n.velocity, n.articulation, n.channel, (int) found->track);
            repaint();
            return;
        }
        if (! e.mods.isShiftDown())
        {
            selection_.clear();
            others_.clear();
        }
        bandBase_ = selection_;
        othersBase_ = others_;
        drag_ = Drag::band;
        band_ = {};
        repaint();
        return;
    }

    if (laneArea().contains (e.getPosition()))
    {
        if (laneKey_.empty())
        {
            const int hit = velocityBarAt (pos);
            if (hit < 0)
                return;
            if (! isSelected ((size_t) hit))
                selection_ = { (size_t) hit };
            anchor_ = hit;
            velocityValue_ = notes[(size_t) hit].velocity;
            drag_ = Drag::velocity;
            dragMoved_ = false;
            repaint();
            return;
        }
        const int point = pointAt (pos);
        if (point < 0)
            return;
        if (e.mods.isPopupMenu())
        {
            showCurveShapeMenu (point);
            return;
        }
        laneEdit_ = *currentLane();
        pointIndex_ = point;
        drag_ = Drag::point;
        dragMoved_ = false;
        repaint();
    }
}

void PianoRollPanel::mouseDrag (const juce::MouseEvent& e)
{
    const TimelineGeometry geo (timeline_, leftWidth, gridArea().getRight());
    const bool free = e.mods.isCommandDown();
    const double raw = (e.position.x - e.mouseDownPosition.x) / geo.pixelsPerBeat;
    const int index = track();
    switch (drag_)
    {
        case Drag::ruler:
            if (onSeek)
                onSeek (std::max (0.0, geo.beat ((float) e.x)));
            return;
        case Drag::loop:
            if (const auto range = loopDrag_.drag ((float) e.x, geo))
                timeline_.setLoopRange (range);   // 미리보기 (놓을 때 편집)
            return;
        case Drag::keys:
            audition (pitchAt (e.position.y), keyVelocity (e.position.x));
            return;
        case Drag::move:
        {
            const auto& n = project_.composition().tracks[(size_t) anchorTrack_].notes[(size_t) anchor_];
            const double start = free ? std::max (0.0, n.startBeat + raw) : snapRound (n.startBeat + raw);
            dragBeats_ = start - n.startBeat;
            dragPitch_ = virtualOf (pitchAt (e.position.y)) - virtualOf (pitchAt (e.mouseDownPosition.y));
            dragMoved_ = dragMoved_ || e.getDistanceFromDragStart() > 3;
            const int heard = anchorTrack_ == index ? movedPitch (n.pitch, dragPitch_) : juce::jlimit (0, 127, n.pitch + (drumView() ? 0 : dragPitch_));
            audition (heard, n.velocity, n.articulation, n.channel, anchorTrack_);
            repaint();
            return;
        }
        case Drag::resize:
        {
            const auto& n = project_.composition().tracks[(size_t) anchorTrack_].notes[(size_t) anchor_];
            const double end = n.startBeat + n.durationBeat;
            const double newEnd = free ? end + raw : std::max (n.startBeat + grid_, snapRound (end + raw));
            dragBeats_ = newEnd - end;
            dragMoved_ = true;
            repaint();
            return;
        }
        case Drag::band:
        {
            band_ = juce::Rectangle<float> (e.mouseDownPosition, e.position).getIntersection (gridArea().toFloat());
            const double b0 = geo.beat (band_.getX()), b1 = geo.beat (band_.getRight());
            const int lowPitch = pitchAt (band_.getBottom()), highPitch = pitchAt (band_.getY());
            const auto found = mr::notesInRange (project_.editNotes ((size_t) index), b0, b1, lowPitch, highPitch);
            selection_.clear();
            std::set_union (bandBase_.begin(), bandBase_.end(), found.begin(), found.end(), std::back_inserter (selection_));
            // 함께 고치는 트랙의 음도 (드럼 보기는 줄이 보이는 트랙의 건반이라 보이는 트랙만)
            others_ = othersBase_;
            if (! drumView())
                for (const size_t t : companionTracks (Companion::edited))
                {
                    const auto more = mr::notesInRange (project_.editNotes (t), b0, b1, lowPitch, highPitch);
                    auto& sel = others_[t];
                    mr::NoteSelection merged;
                    std::set_union (sel.begin(), sel.end(), more.begin(), more.end(), std::back_inserter (merged));
                    sel = std::move (merged);
                }
            repaint();
            return;
        }
        case Drag::velocity:
        {
            const auto area = laneValueArea();
            velocityValue_ = juce::jlimit (1, 127, (int) std::round ((area.getBottom() - e.position.y) / area.getHeight() * 127.0f));
            dragMoved_ = true;
            repaint();
            return;
        }
        case Drag::point:
        {
            auto& points = laneEdit_.points;
            const auto stored = currentLane();
            if (! stored || (size_t) pointIndex_ >= stored->points.size())
                return;
            const auto original = stored->points[(size_t) pointIndex_];
            double beat = free ? original.beat + raw : snapRound (original.beat + raw);
            const double lo = pointIndex_ > 0 ? points[(size_t) pointIndex_ - 1].beat : 0.0;
            const double hi = (size_t) pointIndex_ + 1 < points.size() ? points[(size_t) pointIndex_ + 1].beat : 1.0e9;
            beat = juce::jlimit (lo, hi, beat);
            points[(size_t) pointIndex_].beat = beat;
            points[(size_t) pointIndex_].value = laneValueAt (e.position.y);
            dragMoved_ = true;
            repaint();
            return;
        }
        case Drag::audioPitch:
        case Drag::audioStart:
        case Drag::audioEnd:
        case Drag::audioFlatten:
        case Drag::audioVibrato:
            audioMouseDrag (e);
            return;
        case Drag::none:
            return;
    }
}

void PianoRollPanel::mouseUp (const juce::MouseEvent& e)
{
    stopAudition();
    const auto drag = drag_;
    drag_ = Drag::none;
    const int index = track();
    if (index < 0)
        return;
    switch (drag)
    {
        case Drag::move:
            if (dragMoved_ && (dragBeats_ != 0.0 || dragPitch_ != 0))
            {
                // 드럼 보기는 줄 단위로 옮긴다: 시간만 옮긴 뒤 음마다 높이를 바꾼다 (함께 고치는 트랙은 시간만)
                const int semitones = drumView() ? 0 : dragPitch_;
                TrackEdits results;
                if (! selection_.empty())
                {
                    const auto notes = project_.editNotes ((size_t) index);
                    auto result = dragCopy_ ? mr::copyNotes (notes, selection_, dragBeats_, semitones) : mr::moveNotes (notes, selection_, dragBeats_, semitones);
                    if (drumView())
                        for (const size_t i : result.selection)
                            result.notes[i].note.pitch = movedPitch (result.notes[i].note.pitch, dragPitch_);
                    results.push_back ({ (size_t) index, std::move (result) });
                }
                for (const auto& [t, sel] : others_)
                    if (! sel.empty() && editable (t))
                    {
                        const auto notes = project_.editNotes (t);
                        results.push_back ({ t, dragCopy_ ? mr::copyNotes (notes, sel, dragBeats_, semitones) : mr::moveNotes (notes, sel, dragBeats_, semitones) });
                    }
                commitTracks (std::move (results), dragCopy_ ? juce::String::fromUTF8 ("음표 복사") : juce::String::fromUTF8 ("음표 옮기기"));
            }
            else if (pendingToggle_ && ! dragMoved_)
            {
                auto& sel = anchorTrack_ == index ? selection_ : others_[(size_t) anchorTrack_];
                sel.erase (std::remove (sel.begin(), sel.end(), (size_t) anchor_), sel.end());
            }
            break;
        case Drag::resize:
            if (std::abs (dragBeats_) > 1.0e-9)
            {
                TrackEdits results;
                if (! selection_.empty())
                    results.push_back ({ (size_t) index, mr::resizeNotes (project_.editNotes ((size_t) index), selection_, dragBeats_, grid_ * 0.5) });
                for (const auto& [t, sel] : others_)
                    if (! sel.empty() && editable (t))
                        results.push_back ({ t, mr::resizeNotes (project_.editNotes (t), sel, dragBeats_, grid_ * 0.5) });
                if (anchorTrack_ == index)
                    lastLength_ = std::max (grid_ * 0.5, project_.composition().tracks[(size_t) index].notes[(size_t) anchor_].durationBeat + dragBeats_);
                commitTracks (std::move (results), juce::String::fromUTF8 ("음표 길이"));
            }
            break;
        case Drag::velocity:
            if (dragMoved_)
            {
                const auto notes = project_.editNotes ((size_t) index);
                const int delta = velocityValue_ - notes[(size_t) anchor_].note.velocity;
                if (delta != 0)
                {
                    if (isSelected ((size_t) anchor_) && selection_.size() > 1)
                        commitNotes (mr::offsetVelocity (notes, selection_, delta), juce::String::fromUTF8 ("벨로시티"));
                    else
                        commitNotes (mr::setVelocity (notes, { (size_t) anchor_ }, velocityValue_), juce::String::fromUTF8 ("벨로시티"));
                    lastVelocity_ = velocityValue_;
                }
            }
            break;
        case Drag::point:
            if (dragMoved_)
                commitLane (laneKey_, laneEdit_, juce::String::fromUTF8 ("곡선 점 옮기기"));
            break;
        case Drag::loop:
        {
            const TimelineGeometry geo (timeline_, leftWidth, gridArea().getRight());
            const auto range = loopDrag_.drag ((float) e.x, geo);
            if (range || loopDrag_.mode != LoopRulerDrag::Mode::create)
                if (onLoopEdited)
                    onLoopEdited (range);
            timeline_.setLoopRange (project_.loopRange());
            break;
        }
        case Drag::band:
            // 드럼 보기: 빈 칸을 누르고 끌지 않으면 그 칸에 친다 (격자 한 칸, 마지막 세기)
            if (drumView() && e.getNumberOfClicks() == 1 && e.getDistanceFromDragStart() < 3 && ! e.mods.isShiftDown()
                && gridArea().contains (e.getMouseDownPosition()))
            {
                const TimelineGeometry geo (timeline_, leftWidth, gridArea().getRight());
                mr::Note n;
                n.pitch = pitchAt (e.mouseDownPosition.y);
                n.startBeat = e.mods.isCommandDown() ? std::max (0.0, geo.beat (e.mouseDownPosition.x)) : snapFloor (geo.beat (e.mouseDownPosition.x));
                n.durationBeat = grid_;
                n.velocity = lastVelocity_;
                const auto notes = project_.editNotes ((size_t) index);
                if (commitNotes (mr::addNotes (notes, { n }), juce::String::fromUTF8 ("치기 추가")))
                {
                    clickAdded_ = selection_.empty() ? -1 : (int) selection_.front();
                    auditionBriefly (n.pitch, n.velocity);
                }
            }
            break;
        case Drag::audioPitch:
        case Drag::audioStart:
        case Drag::audioEnd:
        case Drag::audioFlatten:
        case Drag::audioVibrato:
            audioMouseUp (drag);
            break;
        case Drag::ruler:
        case Drag::keys:
        case Drag::none:
            break;
    }
    band_ = {};
    repaint();
}

void PianoRollPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int index = track();
    if (index < 0 || e.mods.isPopupMenu() || (audioTrack() && ! laneArea().contains (e.getPosition())))
        return;
    const TimelineGeometry geo (timeline_, leftWidth, gridArea().getRight());
    if (gridArea().contains (e.getPosition()))
    {
        bool edge = false;
        const int hit = noteAt (e.position, edge);
        const auto notes = project_.editNotes ((size_t) index);
        if (drumView() && (hit < 0 || hit == clickAdded_))
        {
            clickAdded_ = -1;   // 두 번 클릭의 첫 클릭이 넣은 치기는 남긴다
            return;
        }
        if (hit >= 0)
        {
            commitNotes (mr::deleteNotes (notes, { (size_t) hit }), juce::String::fromUTF8 ("음표 지우기"));
            return;
        }
        if (const auto other = hitNote (e.position, true); other && (int) other->track != index)
        {
            // 다른 트랙의 음: 그 트랙을 보이고 그 음을 고른다. 보던 트랙은 그 트랙의 보기 방식을 이어받는다
            const auto mode = companion (other->track);
            setCompanion ((size_t) index, mode);
            timeline_.setSelectedTrack ((int) other->track);
            setCompanion (other->track, Companion::none);
            setSelection ({ other->note });
            status (juce::String::fromUTF8 ("트랙을 바꿨습니다: ") + juce::String::fromUTF8 (project_.composition().tracks[other->track].name.c_str()));
            return;
        }
        mr::Note n;
        n.pitch = pitchAt (e.position.y);
        n.startBeat = e.mods.isCommandDown() ? std::max (0.0, geo.beat (e.position.x)) : snapFloor (geo.beat (e.position.x));
        n.durationBeat = lastLength_;
        n.velocity = lastVelocity_;
        if (commitNotes (mr::addNotes (notes, { n }), juce::String::fromUTF8 ("음표 추가")))
            auditionBriefly (n.pitch, n.velocity);
        return;
    }
    if (laneArea().contains (e.getPosition()) && ! laneKey_.empty())
    {
        const auto stored = currentLane();
        if (! stored)
            return;
        auto lane = *stored;
        const int point = pointAt (e.position);
        if (point >= 0)
        {
            if (laneKey_ == "tempo" && point == 0)
            {
                status (juce::String::fromUTF8 ("곡의 처음 템포는 지우지 않습니다. 끌어서 빠르기를 바꿉니다."));
                return;
            }
            lane.points.erase (lane.points.begin() + point);
            commitLane (laneKey_, lane, juce::String::fromUTF8 ("곡선 점 지우기"));
            return;
        }
        mr::AutomationPoint p;
        p.beat = e.mods.isCommandDown() ? std::max (0.0, geo.beat (e.position.x)) : snapRound (geo.beat (e.position.x));
        p.value = laneValueAt (e.position.y);
        auto at = std::upper_bound (lane.points.begin(), lane.points.end(), p.beat,
                                    [] (double b, const mr::AutomationPoint& q) { return b < q.beat; });
        if (at != lane.points.begin())
            p.curve = std::prev (at)->curve;
        lane.points.insert (at, p);
        commitLane (laneKey_, lane, juce::String::fromUTF8 ("곡선 점 추가"));
    }
}

void PianoRollPanel::mouseMove (const juce::MouseEvent& e)
{
    if (audioTrack() && gridArea().contains (e.getPosition()))
    {
        int edge = 0;
        if (audioHandleAt (e.position) >= 0)
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        else if (audioNoteAt (e.position, edge))
            setMouseCursor (edge != 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor);
        else
            setMouseCursor (juce::MouseCursor::NormalCursor);
        return;
    }
    const auto hit = gridArea().contains (e.getPosition()) ? hitNote (e.position, false) : std::nullopt;
    if (hit)
        setMouseCursor (hit->edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
    else if (laneArea().contains (e.getPosition())
             && (laneKey_.empty() ? velocityBarAt (e.position) >= 0 : pointAt (e.position) >= 0))
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void PianoRollPanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const TimelineGeometry geo (timeline_, leftWidth, gridArea().getRight());
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        const float anchorX = (float) std::max (e.x, leftWidth);
        timeline_.zoomAround (std::pow (1.0025, wheel.deltaY * 400.0), geo.beat (anchorX), anchorX - leftWidth);
        return;
    }
    if (e.mods.isAltDown())
    {
        keyHeight_ = juce::jlimit (6.0f, 28.0f, keyHeight_ * (float) std::pow (1.0025, wheel.deltaY * 400.0));
        updateScrollBars();
        repaint();
        return;
    }
    const bool horizontal = e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY);
    if (horizontal)
    {
        const float delta = std::abs (wheel.deltaX) > 0.0f ? wheel.deltaX : wheel.deltaY;
        timeline_.scrollBy (-delta * 240.0 / timeline_.pixelsPerBeat());
    }
    else
    {
        topPitch_ += wheel.deltaY * 240.0 / keyHeight_;
        updateScrollBars();
        repaint();
    }
}

void PianoRollPanel::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    const TimelineGeometry geo (timeline_, leftWidth, gridArea().getRight());
    const float anchorX = (float) std::max (e.x, leftWidth);
    timeline_.zoomAround (scaleFactor, geo.beat (anchorX), anchorX - leftWidth);
}

} // namespace daw
