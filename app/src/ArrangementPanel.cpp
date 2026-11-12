#include "ArrangementPanel.h"

#include "Theme.h"
#include "TimelineDrawing.h"
#include "instruments/AudioClips.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace daw
{

ArrangementPanel::ArrangementPanel (Project& project, TimelineState& timeline) : project_ (project), timeline_ (timeline)
{
    timeline_.addListener (this);
    vertical_.addListener (this);
    horizontal_.addListener (this);
    vertical_.setAutoHide (false);
    horizontal_.setAutoHide (false);
    addAndMakeVisible (vertical_);
    addAndMakeVisible (horizontal_);
}

ArrangementPanel::~ArrangementPanel()
{
    timeline_.removeListener (this);
}

void ArrangementPanel::projectChanged()
{
    rows_.assign (project_.trackCount(), {});
    if (project_.loaded())
        for (size_t i = 0; i < rows_.size(); ++i)
        {
            rows_[i].mute = project_.composition().tracks[i].mixer.mute;
            rows_[i].solo = project_.composition().tracks[i].mixer.solo;
        }
    scrollY_ = 0;
    range_.reset();
    selectedClip_.reset();
    selectedMarker_ = -1;
    collapsed_.clear();
    finishRename (false);
    refreshLayout();
}

void ArrangementPanel::refreshLayout()
{
    const size_t count = rows_.size();
    rowTop_.assign (count, 0);
    rowHeight_.assign (count, laneHeight);
    folderRows_.clear();
    int y = 0;
    const auto* tracks = project_.loaded() && project_.trackCount() == count ? &project_.composition().tracks : nullptr;
    for (size_t i = 0; i < count; ++i)
    {
        const std::string folder = tracks != nullptr ? (*tracks)[i].folder : std::string();
        if (! folder.empty() && (i == 0 || (*tracks)[i - 1].folder != folder))
        {
            FolderRow f;
            f.name = folder;
            f.first = f.last = i;
            f.top = y;
            folderRows_.push_back (f);
            y += folderHeight;
        }
        if (! folder.empty())
            folderRows_.back().last = i;
        rowTop_[i] = y;
        rowHeight_[i] = ! folder.empty() && collapsed_.count (folder) > 0 ? 0 : laneHeight;
        y += rowHeight_[i];
    }
    rowsBottom_ = y;
    sections_ = project_.loaded() ? mr::sectionsOf (project_.documentRoot(), project_.file().getParentDirectory()) : std::vector<mr::Section> {};
    updateScrollBars();
    repaint();
}

void ArrangementPanel::setFolderCollapsed (const std::string& folder, bool collapsed)
{
    if (collapsed)
        collapsed_.insert (folder);
    else
        collapsed_.erase (folder);
    refreshLayout();
}

juce::Rectangle<int> ArrangementPanel::folderHeaderBounds (const std::string& folder) const
{
    for (const auto& f : folderRows_)
        if (f.name == folder)
            return { 0, lanesTop() + f.top - scrollY_, getWidth() - scrollBarSize, folderHeight };
    return {};
}

bool ArrangementPanel::trackHidden (size_t index) const { return index < rowHeight_.size() && rowHeight_[index] == 0; }

void ArrangementPanel::tracksChanged()
{
    finishRename (false);
    if (range_ && range_->lastTrack >= project_.trackCount())
        range_.reset();
    const auto count = project_.trackCount();
    rows_.assign (count, {});
    if (project_.loaded())
        for (size_t i = 0; i < count; ++i)
        {
            rows_[i].mute = project_.composition().tracks[i].mixer.mute;
            rows_[i].solo = project_.composition().tracks[i].mixer.solo;
        }
    refreshLayout();
}

void ArrangementPanel::refreshFlags()
{
    if (! project_.loaded() || rows_.size() != project_.trackCount())
        return;
    for (size_t i = 0; i < rows_.size(); ++i)
        setTrackFlags (i, project_.composition().tracks[i].mixer.mute, project_.composition().tracks[i].mixer.solo);
}

void ArrangementPanel::setTrackFlags (size_t index, bool mute, bool solo)
{
    if (index >= rows_.size())
        return;
    rows_[index].mute = mute;
    rows_[index].solo = solo;
    repaint (rowBounds (index).withWidth (headerWidth));
}

void ArrangementPanel::setInputMonitorTrack (int index)
{
    if (index == inputMonitorTrack_)
        return;
    inputMonitorTrack_ = index;
    repaint();
}

void ArrangementPanel::setTrackDescription (size_t index, const juce::String& description)
{
    if (index >= rows_.size())
        return;
    rows_[index].description = description;
    repaint (rowBounds (index).withWidth (headerWidth));
}

void ArrangementPanel::setLevels (const std::vector<float>& peaks)
{
    for (size_t i = 0; i < rows_.size() && i < peaks.size(); ++i)
    {
        // 올라갈 때는 바로, 내려갈 때는 천천히
        const float v = peaks[i];
        auto& level = rows_[i].level;
        const float next = v > level ? v : level * 0.82f;
        if (std::abs (next - level) > 1.0e-4f)
        {
            level = next;
            repaint (rowBounds (i).withWidth (headerWidth).removeFromBottom (8));
        }
    }
}

juce::Rectangle<int> ArrangementPanel::lanesArea() const
{
    return { headerWidth, lanesTop(), getWidth() - headerWidth - scrollBarSize, getHeight() - lanesTop() - scrollBarSize };
}

juce::Rectangle<int> ArrangementPanel::rowBounds (size_t index) const
{
    if (index >= rowTop_.size())
        return { 0, lanesTop() + (int) index * laneHeight - scrollY_, getWidth() - scrollBarSize, laneHeight };
    return { 0, lanesTop() + rowTop_[index] - scrollY_, getWidth() - scrollBarSize, rowHeight_[index] };
}

juce::Rectangle<int> ArrangementPanel::muteBox (size_t index) const
{
    auto r = rowBounds (index);
    return { headerWidth - 64, r.getY() + 10, 24, 20 };
}

juce::Rectangle<int> ArrangementPanel::soloBox (size_t index) const
{
    auto r = rowBounds (index);
    return { headerWidth - 36, r.getY() + 10, 24, 20 };
}

int ArrangementPanel::contentHeight() const { return rowsBottom_ + addRowHeight; }

juce::Rectangle<int> ArrangementPanel::addTrackBox() const
{
    return { 12, lanesTop() + rowsBottom_ - scrollY_ + 10, headerWidth - 24, 28 };
}

juce::Rectangle<int> ArrangementPanel::nameBox (size_t index) const
{
    auto r = rowBounds (index);
    return { r.getX() + 10, r.getY() + 6, headerWidth - 10 - 72, 24 };
}

void ArrangementPanel::beginRename (size_t index)
{
    if (! project_.loaded() || index >= rows_.size())
        return;
    finishRename (false);
    renaming_ = index;
    renameEditor_ = std::make_unique<juce::TextEditor>();
    auto& ed = *renameEditor_;
    ed.setFont (uiFont (14.0f, true));
    ed.setColour (juce::TextEditor::backgroundColourId, colours::background);
    ed.setColour (juce::TextEditor::textColourId, colours::text);
    ed.setColour (juce::TextEditor::outlineColourId, colours::accent);
    ed.setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
    ed.setIndents (4, 3);
    renamingMarker_ = false;
    ed.setText (juce::String::fromUTF8 (project_.composition().tracks[index].name.c_str()), false);
    ed.selectAll();
    ed.onReturnKey = [this] { finishRename (true); };
    ed.onEscapeKey = [this] { finishRename (false); };
    ed.onFocusLost = [this] { finishRename (true); };
    addAndMakeVisible (ed);
    ed.setBounds (nameBox (index));
    ed.grabKeyboardFocus();
}

void ArrangementPanel::finishRename (bool commit)
{
    if (renameEditor_ == nullptr)
        return;
    auto editor = std::move (renameEditor_);   // 콜백이 다시 들어와도 한 번만
    const auto text = editor->getText().trim();
    const auto index = renaming_;
    const bool marker = renamingMarker_;
    renamingMarker_ = false;
    editor->onFocusLost = nullptr;
    removeChildComponent (editor.get());
    editor.reset();
    repaint();
    if (! commit || text.isEmpty())
        return;
    if (marker)
    {
        if (onRenameMarker)
            onRenameMarker (index, text);
    }
    else if (onRenameTrack)
        onRenameTrack (index, text);
}

juce::Rectangle<int> ArrangementPanel::markerStrip() const
{
    return { headerWidth, timeRulerHeight, std::max (0, getWidth() - headerWidth - scrollBarSize), markerHeight };
}

juce::Rectangle<int> ArrangementPanel::sectionStrip() const
{
    return { headerWidth, timeRulerHeight + markerHeight, std::max (0, getWidth() - headerWidth - scrollBarSize), sectionHeight };
}

float ArrangementPanel::sectionX (size_t section, float fraction) const
{
    if (section >= sections_.size())
        return 0.0f;
    const TimelineGeometry geo (timeline_, headerWidth, lanesArea().getRight());
    const auto& sec = sections_[section];
    return geo.x (sec.start + (sec.end - sec.start) * fraction);
}

std::optional<size_t> ArrangementPanel::sectionAt (juce::Point<int> p) const
{
    if (! sectionStrip().contains (p))
        return std::nullopt;
    const TimelineGeometry geo (timeline_, headerWidth, lanesArea().getRight());
    const double beat = geo.beat ((float) p.x);
    for (size_t i = 0; i < sections_.size(); ++i)
        if (beat >= sections_[i].start && beat < sections_[i].end)
            return i;
    return std::nullopt;
}

void ArrangementPanel::paintSections (juce::Graphics& g, const TimelineGeometry& geo)
{
    const auto strip = sectionStrip();
    g.setColour (colours::panel.darker (0.2f));
    g.fillRect (strip);
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (strip);
    const bool dragging = pressedSection_ && sectionTarget_;
    const float shift = dragging ? sectionDragX_ - sectionPressX_ : 0.0f;
    for (size_t i = 0; i < sections_.size(); ++i)
    {
        const auto& sec = sections_[i];
        auto box = juce::Rectangle<float> (geo.x (sec.start), (float) strip.getY() + 2.0f, geo.x (sec.end) - geo.x (sec.start), (float) strip.getHeight() - 4.0f)
                       .reduced (1.0f, 0.0f);
        const bool moving = dragging && i == *pressedSection_;
        if (moving)
            box = box.translated (shift, 0.0f);
        g.setColour (colours::marker.withAlpha (moving ? 0.75f : (i % 2 == 0 ? 0.32f : 0.22f)));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (colours::marker.withAlpha (moving ? 1.0f : 0.6f));
        g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, 1.0f);
        g.setColour (moving ? juce::Colours::black.withAlpha (0.85f) : colours::text.withAlpha (0.85f));
        g.setFont (uiFont (11.0f, true));
        g.drawText (juce::String::fromUTF8 (sec.name.c_str()), box.reduced (6.0f, 0.0f), juce::Justification::centredLeft, true);
    }
    if (dragging && ! sections_.empty())
    {
        // 놓일 자리
        const size_t t = *sectionTarget_;
        const double beat = t < sections_.size() ? sections_[t].start : sections_.back().end;
        const float x = geo.x (beat);
        g.setColour (colours::accent);
        g.fillRect (x - 1.5f, (float) strip.getY(), 3.0f, (float) strip.getHeight());
    }
    g.setColour (colours::outline);
    g.fillRect (strip.getX(), strip.getBottom() - 1, strip.getWidth(), 1);
}

// ---- 영상 레인 ----

void ArrangementPanel::setVideo (std::optional<VideoLane> video, bool synchronousThumbnails)
{
    const bool had = video_.has_value();
    const bool sameFile = video && video_ && video->file == video_->file;
    video_ = std::move (video);
    videoDragOffset_.reset();
    if (! video_)
    {
        thumbnails_.reset();
        scenes_.clear();
    }
    else if (! sameFile || thumbnails_ == nullptr)
    {
        scenes_.clear();
        thumbnails_ = std::make_unique<VideoThumbnails> (video_->file, (videoHeight - videoTimecodeHeight - 4) * 2, synchronousThumbnails,
                                                         [safe = juce::Component::SafePointer<ArrangementPanel> (this)]
                                                         {
                                                             if (safe != nullptr)
                                                                 safe->repaint (safe->videoLaneBounds());
                                                         });
    }
    if (had != video_.has_value())
    {
        resized();
        refreshLayout();
    }
    repaint();
}

void ArrangementPanel::setSceneChanges (std::vector<double> videoSeconds)
{
    scenes_ = std::move (videoSeconds);
    repaint (videoLaneBounds());
}

juce::Rectangle<int> ArrangementPanel::videoLaneBounds() const
{
    if (! video_)
        return {};
    return { headerWidth, rulerHeight, std::max (0, getWidth() - headerWidth - scrollBarSize), videoHeight };
}

float ArrangementPanel::videoX (double videoSeconds) const
{
    const TimelineGeometry geo (timeline_, headerWidth, lanesArea().getRight());
    return geo.x (project_.loaded() ? project_.tempo().beatAt (videoOffsetNow() + videoSeconds) : 0.0);
}

std::optional<size_t> ArrangementPanel::sceneAt (juce::Point<int> p) const
{
    if (! video_ || ! videoLaneBounds().contains (p))
        return std::nullopt;
    std::optional<size_t> best;
    float bestDistance = 6.0f;
    for (size_t i = 0; i < scenes_.size(); ++i)
        if (const float d = std::abs (videoX (scenes_[i]) - (float) p.x); d <= bestDistance)
        {
            best = i;
            bestDistance = d;
        }
    return best;
}

void ArrangementPanel::paintVideoLane (juce::Graphics& g, const TimelineGeometry& geo)
{
    const auto lane = videoLaneBounds();
    if (lane.isEmpty() || ! video_)
        return;
    const auto& v = *video_;
    const auto& tempo = project_.tempo();
    const double offset = videoOffsetNow();
    auto xAt = [&] (double videoSeconds) { return geo.x (tempo.beatAt (offset + videoSeconds)); };
    g.setColour (colours::panel.darker (0.25f));
    g.fillRect (lane);
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (lane);
    const float x0 = xAt (0.0), x1 = xAt (v.durationSeconds);
    const auto strip = lane.withHeight (videoTimecodeHeight).toFloat();
    const auto thumbs = juce::Rectangle<float> ((float) lane.getX(), strip.getBottom() + 2.0f, (float) lane.getWidth(),
                                                (float) (videoHeight - videoTimecodeHeight - 4));
    const auto span = juce::Rectangle<float> (x0, (float) lane.getY(), x1 - x0, (float) lane.getHeight());
    g.setColour (colours::panelRaised.darker (0.1f));
    g.fillRect (span.withTrimmedTop (strip.getHeight()));

    // 화면의 1초가 몇 픽셀인지 (보기 처음 자리의 템포로)
    const double viewSeconds = tempo.secondsAt (geo.startBeat);
    const double pxPerSecond = std::max (1.0e-3, (double) (geo.x (tempo.beatAt (viewSeconds + 1.0)) - geo.x (geo.startBeat)));
    const double firstVisible = std::max (0.0, tempo.secondsAt (geo.startBeat) - offset);
    const double lastVisible = std::min (v.durationSeconds, tempo.secondsAt (geo.endBeat()) - offset);

    // 섬네일: 칸 길이는 프레임 길이의 2의 거듭제곱 배 (확대를 바꿔도 같은 칸을 다시 쓴다). 칸이 섬네일보다 좁으면 다음 칸에서 자른다
    if (thumbnails_ != nullptr && v.rate.valid() && v.width > 0 && v.height > 0 && lastVisible > firstVisible)
    {
        const float thumbWidth = thumbs.getHeight() * (float) v.width / (float) v.height;
        double step = v.rate.frameSeconds();
        while (step * 2.0 * pxPerSecond <= thumbWidth && step < 3600.0)
            step *= 2.0;
        const auto first = (int64_t) std::floor (firstVisible / step), last = (int64_t) std::floor (lastVisible / step);
        for (int64_t k = first; k <= last && k - first < 400; ++k)
        {
            const float tx = xAt ((double) k * step);
            const float next = std::min (x1, xAt ((double) (k + 1) * step));
            const auto box = juce::Rectangle<float> (tx, thumbs.getY(), std::max (1.0f, std::min (thumbWidth, next - tx)), thumbs.getHeight());
            const auto image = thumbnails_->get (k, step);
            if (image.isValid())
            {
                juce::Graphics::ScopedSaveState clip (g);
                g.reduceClipRegion (box.toNearestInt());
                g.drawImage (image, juce::Rectangle<float> (tx, thumbs.getY(), thumbWidth, thumbs.getHeight()), juce::RectanglePlacement::stretchToFit);
            }
            else
            {
                g.setColour (colours::panelRaised);
                g.fillRect (box.reduced (0.5f));
            }
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRect (box.getX(), box.getY(), 1.0f, box.getHeight());
        }
    }

    // 타임코드 줄: 프레임 번호의 간격 (타임코드의 1초는 명목 프레임 수), 글자 사이가 96 픽셀 넘게
    if (v.rate.valid() && lastVisible > firstVisible)
    {
        const int64_t perSecond = std::max (1, v.rate.nominal());
        static const int64_t multiples[] { 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600 };
        int64_t stepFrames = std::max<int64_t> (1, perSecond / 2);
        if ((double) stepFrames * v.rate.frameSeconds() * pxPerSecond < 96.0)
            for (const int64_t m : multiples)
            {
                stepFrames = m * perSecond;
                if ((double) stepFrames * v.rate.frameSeconds() * pxPerSecond >= 96.0)
                    break;
            }
        g.setFont (monoFont (10.0f));
        const int64_t firstFrame = mr::media::frameAtSeconds (firstVisible, v.rate) / stepFrames * stepFrames;
        for (int64_t frame = firstFrame; mr::media::secondsAtFrame (frame, v.rate) <= lastVisible + 1.0e-9; frame += stepFrames)
        {
            const double t = mr::media::secondsAtFrame (frame, v.rate);
            const float tx = xAt (t);
            g.setColour (colours::textFaint);
            g.fillRect (tx, strip.getBottom() - 5.0f, 1.0f, 5.0f);
            g.setColour (colours::textDim);
            g.drawText (juce::String (mr::media::timecodeAt (t, v.rate)), juce::Rectangle<float> (tx + 3.0f, strip.getY(), 90.0f, strip.getHeight()),
                        juce::Justification::centredLeft, false);
        }
    }

    // 장면 바뀜 후보
    for (const double scene : scenes_)
    {
        const float sx = xAt (scene);
        if (sx < (float) lane.getX() - 6.0f || sx > (float) lane.getRight() + 6.0f)
            continue;
        g.setColour (colours::marker.withAlpha (0.85f));
        g.fillRect (sx - 0.5f, thumbs.getY(), 2.0f, thumbs.getHeight());
        juce::Path tri;
        tri.addTriangle (sx - 5.0f, thumbs.getBottom() + 2.0f, sx + 5.0f, thumbs.getBottom() + 2.0f, sx, thumbs.getBottom() - 6.0f);
        g.fillPath (tri);
    }

    // 영상의 양 끝
    g.setColour (videoDragOffset_ ? colours::accent : colours::textFaint);
    g.drawRect (span.withTrimmedTop (strip.getHeight()).expanded (0.0f, -1.0f), 1.0f);
    drawPlayhead (g, lane, geo, timeline_.playheadBeat());
    g.setColour (colours::outline);
    g.fillRect (lane.getX(), lane.getBottom() - 1, lane.getWidth(), 1);
}

std::vector<MarkerView> ArrangementPanel::markerViews() const
{
    std::vector<MarkerView> out;
    if (! project_.loaded())
        return out;
    const auto& markers = project_.markers();
    for (size_t i = 0; i < markers.size(); ++i)
    {
        MarkerView v { markers[i].beat, juce::String::fromUTF8 (markers[i].name.c_str()) };
        if (pressedMarker_ && *pressedMarker_ == i && markerDragBeat_)
            v.beat = *markerDragBeat_;
        out.push_back (v);
    }
    return out;
}

std::optional<size_t> ArrangementPanel::markerAt (juce::Point<int> p) const
{
    if (! markerStrip().contains (p))
        return std::nullopt;
    const TimelineGeometry geo (timeline_, headerWidth, lanesArea().getRight());
    const auto views = markerViews();
    for (size_t i = views.size(); i-- > 0;)
        if (markerLabel (markerStrip(), geo, views, i).expanded (3.0f, 0.0f).contains (p.toFloat()))
            return i;
    return std::nullopt;
}

void ArrangementPanel::beginMarkerRename (size_t marker)
{
    if (! project_.loaded() || marker >= project_.markers().size())
        return;
    finishRename (false);
    const TimelineGeometry geo (timeline_, headerWidth, lanesArea().getRight());
    const auto views = markerViews();
    auto box = markerLabel (markerStrip(), geo, views, marker).toNearestInt();
    box = box.withWidth (std::max (box.getWidth(), 160)).withHeight (markerHeight + 4).withY (timeRulerHeight - 2);
    renaming_ = marker;
    renamingMarker_ = true;
    selectedMarker_ = (int) marker;
    renameEditor_ = std::make_unique<juce::TextEditor>();
    auto& ed = *renameEditor_;
    ed.setFont (uiFont (12.0f, true));
    ed.setColour (juce::TextEditor::backgroundColourId, colours::background);
    ed.setColour (juce::TextEditor::textColourId, colours::text);
    ed.setColour (juce::TextEditor::outlineColourId, colours::marker);
    ed.setColour (juce::TextEditor::focusedOutlineColourId, colours::marker);
    ed.setIndents (4, 2);
    ed.setText (juce::String::fromUTF8 (project_.markers()[marker].name.c_str()), false);
    ed.selectAll();
    ed.onReturnKey = [this] { finishRename (true); };
    ed.onEscapeKey = [this] { finishRename (false); };
    ed.onFocusLost = [this] { finishRename (true); };
    addAndMakeVisible (ed);
    ed.setBounds (box);
    ed.grabKeyboardFocus();
}

double ArrangementPanel::rangeSnap() const
{
    return snapsToBars() ? project_.meters().barBeatsAt (timeline_.viewStartBeat()) : 1.0;
}

bool ArrangementPanel::snapsToBars() const { return timeline_.pixelsPerBeat() < 20.0; }

double ArrangementPanel::snapFloor (double beat) const
{
    beat = std::max (0.0, beat);
    return snapsToBars() ? project_.meters().floorToBar (beat + 1.0e-9) : std::floor (beat + 1.0e-9);
}

double ArrangementPanel::snapCeil (double beat) const
{
    const double f = snapFloor (beat);
    if (beat - f < 1.0e-9)
        return f;
    return snapsToBars() ? project_.meters().barStart (project_.meters().barAt (f) + 1) : f + 1.0;
}

double ArrangementPanel::snapRound (double beat) const
{
    beat = std::max (0.0, beat);
    return snapsToBars() ? project_.meters().roundToBar (beat) : std::round (beat);
}

std::vector<size_t> ArrangementPanel::Range::tracks() const
{
    std::vector<size_t> out;
    for (size_t t = firstTrack; t <= lastTrack; ++t)
        out.push_back (t);
    return out;
}

void ArrangementPanel::setRange (std::optional<Range> range)
{
    if (range == range_)
        return;
    range_ = range;
    if (range_)
        selectedClip_.reset();
    repaint();
    if (onRangeChanged)
        onRangeChanged();
}

int ArrangementPanel::trackAtY (int y) const
{
    if (rows_.empty())
        return 0;
    const int rel = y - lanesTop() + scrollY_;
    int found = 0;
    for (size_t i = 0; i < rowTop_.size(); ++i)
        if (rowHeight_[i] > 0 && rel >= rowTop_[i])
            found = (int) i;   // 그 높이 위의 마지막 보이는 트랙 (폴더 머리 줄은 아래 트랙)
    for (const auto& f : folderRows_)
        if (rel >= f.top && rel < f.top + folderHeight)
            return (int) f.first;
    return juce::jlimit (0, (int) rows_.size() - 1, found);
}

juce::Point<float> ArrangementPanel::lanePoint (size_t track, double beat) const
{
    const TimelineGeometry geo (timeline_, headerWidth, lanesArea().getRight());
    return { geo.x (beat), (float) rowBounds (track).getCentreY() };
}

bool ArrangementPanel::isAudioTrack (size_t track) const
{
    return project_.loaded() && track < project_.trackCount() && project_.composition().tracks[track].instrument == "audio";
}

void ArrangementPanel::setRecordingRegion (std::optional<RecordingRegion> region)
{
    if (! region && ! recording_)
        return;
    recording_ = region;
    repaint();
}

void ArrangementPanel::setSelectedClip (std::optional<ClipRef> clip)
{
    if (clip == selectedClip_)
        return;
    selectedClip_ = clip;
    repaint();
}

double ArrangementPanel::fileSeconds (const mr::AudioClip& clip) const
{
    const auto file = mr::clipFile (clip, project_.file().getParentDirectory());
    const auto key = file.getFullPathName();
    if (auto it = fileSeconds_.find (key); it != fileSeconds_.end())
        return it->second;
    const auto info = mr::audioFileInfo (file);
    return fileSeconds_[key] = info ? info->seconds() : 0.0;
}

std::pair<double, double> ArrangementPanel::clipBeats (const mr::AudioClip& clip) const
{
    double length = clip.lengthSeconds ? *clip.lengthSeconds : std::max (0.0, fileSeconds (clip) - clip.offsetSeconds);
    if (length <= 0.0)
        length = 1.0;   // 읽지 못한 파일도 보이게
    // 늘인 클립은 늘인 길이 (파일의 초에서 곡의 초로)
    return { clip.startBeat, project_.beatAtSeconds (mr::clipSongSecondsAt (clip, project_.tempo(), clip.offsetSeconds + length)) };
}

juce::Rectangle<float> ArrangementPanel::clipBounds (size_t track, size_t clip) const
{
    if (! isAudioTrack (track) || clip >= project_.composition().tracks[track].clips.size())
        return {};
    const TimelineGeometry geo (timeline_, headerWidth, lanesArea().getRight());
    auto spec = project_.composition().tracks[track].clips[clip];
    if (clipPreview_ && pressedClip_ && *pressedClip_ == ClipRef { track, clip } && ! clipCopy_)
        spec = *clipPreview_;
    const auto [b0, b1] = clipBeats (spec);
    const auto row = rowBounds (track);
    const float x = geo.x (b0), w = std::max (2.0f, geo.x (b1) - geo.x (b0));
    // 뮤트한 테이크는 레인 아래의 띠에, 들리는 클립은 그 위에 (겹쳐 녹음한 테이크가 보이고 눌린다)
    const auto& clips = project_.composition().tracks[track].clips;
    const bool takes = std::any_of (clips.begin(), clips.end(), [] (const mr::AudioClip& c) { return c.muted; });
    if (spec.muted)
        return { x, (float) row.getBottom() - 16.0f, w, 12.0f };
    return { x, (float) row.getY() + 3.0f, w, (float) row.getHeight() - (takes ? 21.0f : 7.0f) };
}

void ArrangementPanel::paintClips (juce::Graphics& g, size_t index, juce::Rectangle<int> lane, juce::Colour colour, bool audible)
{
    const auto& track = project_.composition().tracks[index];
    const TimelineGeometry geo (timeline_, headerWidth, lane.getRight());
    const auto folder = project_.file().getParentDirectory();
    auto draw = [&] (const mr::AudioClip& clip, juce::Rectangle<float> box, bool selected, float alpha)
    {
        if (box.getRight() < (float) lane.getX() || box.getX() > (float) lane.getRight())
            return;
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (lane);
        g.setColour (colour.withAlpha (0.22f * alpha));
        g.fillRoundedRectangle (box, 4.0f);
        // 파형 (봉우리 표, 게인을 곱한다)
        const auto file = mr::clipFile (clip, folder);
        const float gain = (float) juce::Decibels::decibelsToGain (clip.gainDb, -96.0);
        const float mid = box.getCentreY() + 5.0f, half = (box.getHeight() - 14.0f) * 0.5f;
        if (const auto peaks = mr::clipPeaks (file, clip.reverse); peaks != nullptr && ! peaks->maximum.empty())
        {
            g.setColour (colour.brighter (0.25f).withAlpha ((audible ? 0.85f : 0.35f) * alpha));
            const float x0 = std::max (box.getX(), (float) lane.getX()), x1 = std::min (box.getRight(), (float) lane.getRight());
            for (float x = x0; x < x1; x += 1.0f)
            {
                // 파일의 초 (늘인 클립은 늘이기를 따라)
                const double s0 = mr::clipSourceAt (clip, project_.tempo(), project_.secondsAtBeat (geo.beat (x)));
                const double s1 = mr::clipSourceAt (clip, project_.tempo(), project_.secondsAtBeat (geo.beat (x + 1.0f)));
                const auto b0 = (size_t) std::max (0.0, s0 * peaks->sampleRate / peaks->bucketFrames);
                const auto b1 = std::max (b0 + 1, (size_t) std::max (0.0, s1 * peaks->sampleRate / peaks->bucketFrames));
                float lo = 0.0f, hi = 0.0f;
                for (size_t b = b0; b < b1 && b < peaks->maximum.size(); ++b)
                {
                    lo = std::min (lo, peaks->minimum[b]);
                    hi = std::max (hi, peaks->maximum[b]);
                }
                // 데시벨 눈금 (-54 dBFS 가 0): 여린 녹음도 모양이 보인다
                auto scale = [] (float a)
                {
                    const float mag = std::abs (a);
                    const float h = mag > 0.0f ? juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (mag) + 54.0f) / 54.0f) : 0.0f;
                    return a < 0.0f ? -h : h;
                };
                const float curve = clip.gainCurve.empty() ? 1.0f : (float) juce::Decibels::decibelsToGain (clip.gainCurve.valueAt (s0), -96.0);
                const float top = mid - scale (hi * gain * curve) * half, bottom = mid - scale (lo * gain * curve) * half;
                g.fillRect (x, top, 1.0f, std::max (1.0f, bottom - top));
            }
        }
        // 페이드: 모서리에서 손잡이까지의 선과 어두운 삼각형
        const double startSec = project_.secondsAtBeat (clip.startBeat);
        const auto [cb0, cb1] = clipBeats (clip);
        const double endSec = project_.secondsAtBeat (cb1);
        const float fin = geo.x (project_.beatAtSeconds (startSec + clip.fadeInSeconds)) - box.getX();
        const float fout = box.getRight() - geo.x (project_.beatAtSeconds (endSec - clip.fadeOutSeconds));
        // 페이드의 모양대로 (아래 모서리가 무음, 위 모서리가 0 dB): 모양 위는 어둡게
        juce::Path shade, edge;
        auto fadePath = [&] (bool in, float width, mr::FadeShape shape)
        {
            constexpr int steps = 24;
            const float x0 = in ? box.getX() : box.getRight();
            const float dir = in ? 1.0f : -1.0f;
            juce::Path line;
            for (int i = 0; i <= steps; ++i)
            {
                const float t = (float) i / steps;
                const float x = x0 + dir * width * t;
                const float y = box.getBottom() - mr::fadeGain (shape, t) * box.getHeight();
                if (i == 0)
                    line.startNewSubPath (x, y);
                else
                    line.lineTo (x, y);
            }
            edge.addPath (line);
            juce::Path area (line);
            area.lineTo (x0 + dir * width, box.getY());
            area.lineTo (x0, box.getY());
            area.closeSubPath();
            shade.addPath (area);
        };
        if (fin > 0.5f)
            fadePath (true, fin, clip.fadeInShape);
        if (fout > 0.5f)
            fadePath (false, fout, clip.fadeOutShape);
        g.setColour (colours::background.withAlpha (0.45f * alpha));
        g.fillPath (shade);
        g.setColour (colour.withAlpha (0.9f * alpha));
        g.strokePath (edge, juce::PathStrokeType (1.0f));
        // 게인 곡선: 편집 중이면 늘, 아니면 곡선이 있을 때만
        if (! clip.muted && (gainEditing_ || ! clip.gainCurve.empty()))
        {
            const auto area = gainArea (box);
            juce::Path line;
            const float x0 = std::max (box.getX(), (float) lane.getX()), x1 = std::min (box.getRight(), (float) lane.getRight());
            for (float x = x0; x <= x1; x += 2.0f)
            {
                const double source = mr::clipSourceAt (clip, project_.tempo(), project_.secondsAtBeat (geo.beat (x)));
                const float y = gainY (box, clip.gainCurve.empty() ? 0.0 : clip.gainCurve.valueAt (source));
                if (x == x0)
                    line.startNewSubPath (x, y);
                else
                    line.lineTo (x, y);
            }
            g.setColour (colours::clipGain.withAlpha ((gainEditing_ ? 0.95f : 0.6f) * alpha));
            g.strokePath (line, juce::PathStrokeType (gainEditing_ ? 1.5f : 1.0f));
            if (gainEditing_)
            {
                for (const auto& p : clip.gainCurve.points)
                {
                    const float x = geo.x (project_.beatAtSeconds (mr::clipSongSecondsAt (clip, project_.tempo(), p.beat)));
                    if (x < box.getX() - 0.5f || x > box.getRight() + 0.5f)
                        continue;
                    g.fillEllipse (x - 3.5f, gainY (box, p.value) - 3.5f, 7.0f, 7.0f);
                }
                g.setColour (colours::textDim.withAlpha (0.5f * alpha));
                g.drawHorizontalLine ((int) gainY (box, 0.0), area.getX(), area.getRight());
            }
        }
        // 워프 마커: 아래 모서리의 삼각형과 옅은 세로줄
        for (size_t k = 0; k < clip.warp.size(); ++k)
        {
            const float x = geo.x (mr::warpMarkerSongBeat (clip, k));
            if (x < box.getX() - 0.5f || x > box.getRight() + 0.5f)
                continue;
            g.setColour (colours::text.withAlpha (0.25f * alpha));
            g.fillRect (x - 0.5f, box.getY() + 8.0f, 1.0f, box.getHeight() - 16.0f);
            juce::Path tri;
            tri.addTriangle (x - 4.0f, box.getBottom(), x + 4.0f, box.getBottom(), x, box.getBottom() - 7.0f);
            g.setColour (colours::text.withAlpha (0.85f * alpha));
            g.fillPath (tri);
        }
        // 손잡이, 테두리, 이름
        g.setColour (colours::text.withAlpha (0.8f * alpha));
        g.fillRect (box.getX() + fin - 3.0f, box.getY(), 6.0f, 6.0f);
        g.fillRect (box.getRight() - fout - 3.0f, box.getY(), 6.0f, 6.0f);
        g.setColour (selected ? colours::text : colour.withAlpha (0.9f * alpha));
        g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, selected ? 2.0f : 1.0f);
        if (clip.muted)
            return;   // 테이크 띠에는 이름을 쓰지 않는다
        auto label = juce::File (juce::String::fromUTF8 (clip.file.c_str())).getFileNameWithoutExtension();
        if (clip.reverse)
            label << juce::String::fromUTF8 ("  거꾸로");
        if (std::abs (clip.gainDb) > 0.05)
            label << "  " << (clip.gainDb > 0 ? "+" : "") << juce::String (clip.gainDb, 1) << " dB";
        g.setColour (colours::text.withAlpha (0.9f * alpha));
        g.setFont (uiFont (11.0f, true));
        g.drawText (label, box.reduced (8.0f, 2.0f).withHeight (13.0f), juce::Justification::centredLeft, true);
    };
    for (size_t c = 0; c < track.clips.size(); ++c)
    {
        const bool dragged = clipPreview_ && pressedClip_ && *pressedClip_ == ClipRef { index, c };
        if (dragged && ! clipCopy_)
            continue;   // 끄는 자리에 그린다
        draw (track.clips[c], clipBounds (index, c), selectedClip_ && *selectedClip_ == ClipRef { index, c }, track.clips[c].muted ? 0.45f : 1.0f);
    }
    if (recording_ && recording_->track == index && recording_->end > recording_->start)
    {
        const auto row = rowBounds (index);
        const juce::Rectangle<float> box (geo.x (recording_->start), (float) row.getY() + 3.0f,
                                          std::max (2.0f, geo.x (recording_->end) - geo.x (recording_->start)), (float) row.getHeight() - 7.0f);
        g.setColour (colours::danger.withAlpha (0.25f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (colours::danger);
        g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.5f);
        const float level = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (recording_->peak, -60.0f) + 60.0f) / 60.0f);
        g.fillRect (box.getRight() - 4.0f, box.getBottom() - 2.0f - level * (box.getHeight() - 4.0f), 3.0f, level * (box.getHeight() - 4.0f));
        g.setColour (colours::text);
        g.setFont (uiFont (11.0f, true));
        g.drawText (juce::String::fromUTF8 ("녹음 중"), box.reduced (8.0f, 2.0f).withHeight (13.0f), juce::Justification::centredLeft, true);
    }
    if (clipPreview_ && pressedClip_ && pressedClip_->track == index)
    {
        const auto [b0, b1] = clipBeats (*clipPreview_);
        const auto row = rowBounds (index);
        draw (*clipPreview_, { geo.x (b0), (float) row.getY() + 3.0f, std::max (2.0f, geo.x (b1) - geo.x (b0)), (float) row.getHeight() - 7.0f },
              true, 0.85f);
    }
}

std::optional<size_t> ArrangementPanel::warpMarkerAt (size_t track, size_t clip, juce::Point<float> p) const
{
    const auto box = clipBounds (track, clip);
    if (box.isEmpty() || p.y < box.getBottom() - 10.0f || p.y > box.getBottom() + 1.0f)
        return std::nullopt;
    const auto& spec = project_.composition().tracks[track].clips[clip];
    const TimelineGeometry geo (timeline_, headerWidth, lanesArea().getRight());
    std::optional<size_t> best;
    float distance = 5.0f;
    for (size_t k = 0; k < spec.warp.size(); ++k)
        if (const float d = std::abs (geo.x (mr::warpMarkerSongBeat (spec, k)) - p.x); d <= distance)
        {
            distance = d;
            best = k;
        }
    return best;
}

juce::Point<float> ArrangementPanel::warpMarkerPoint (size_t track, size_t clip, size_t marker) const
{
    const auto box = clipBounds (track, clip);
    const auto& spec = project_.composition().tracks[track].clips[clip];
    const TimelineGeometry geo (timeline_, headerWidth, lanesArea().getRight());
    return { geo.x (mr::warpMarkerSongBeat (spec, marker)), box.getBottom() - 3.0f };
}

void ArrangementPanel::setGainCurveEditing (bool on)
{
    if (on == gainEditing_)
        return;
    gainEditing_ = on;
    repaint();
}

juce::Rectangle<float> ArrangementPanel::gainArea (juce::Rectangle<float> box)
{
    // 위 모서리의 페이드 손잡이와 아래 모서리의 워프 마커를 비킨다
    return box.withTrimmedTop (std::min (10.0f, box.getHeight() * 0.25f)).withTrimmedBottom (std::min (8.0f, box.getHeight() * 0.2f));
}

float ArrangementPanel::gainY (juce::Rectangle<float> box, double db)
{
    const auto area = gainArea (box);
    const double f = (12.0 - std::clamp (db, -36.0, 12.0)) / 48.0;   // 위가 +12 dB, 아래가 -36 dB
    return area.getY() + (float) f * area.getHeight();
}

double ArrangementPanel::gainDbAt (juce::Rectangle<float> box, float y)
{
    const auto area = gainArea (box);
    const double f = std::clamp ((double) (y - area.getY()) / std::max (1.0f, area.getHeight()), 0.0, 1.0);
    return std::round ((12.0 - 48.0 * f) * 10.0) / 10.0;
}

juce::Point<float> ArrangementPanel::gainCurvePoint (size_t track, size_t clip, double sourceSeconds, double db) const
{
    const auto box = clipBounds (track, clip);
    const auto& spec = project_.composition().tracks[track].clips[clip];
    const TimelineGeometry geo (timeline_, headerWidth, lanesArea().getRight());
    return { geo.x (project_.beatAtSeconds (mr::clipSongSecondsAt (spec, project_.tempo(), sourceSeconds))), gainY (box, db) };
}

std::optional<size_t> ArrangementPanel::gainPointAt (size_t track, size_t clip, juce::Point<float> p) const
{
    const auto& spec = project_.composition().tracks[track].clips[clip];
    std::optional<size_t> best;
    float distance = 6.0f;
    for (size_t k = 0; k < spec.gainCurve.points.size(); ++k)
        if (const float d = gainCurvePoint (track, clip, spec.gainCurve.points[k].beat, spec.gainCurve.points[k].value).getDistanceFrom (p); d <= distance)
        {
            distance = d;
            best = k;
        }
    return best;
}

ArrangementPanel::ClipDrag ArrangementPanel::clipHit (size_t track, size_t clip, juce::Point<float> p) const
{
    const auto box = clipBounds (track, clip);
    if (! box.expanded (4.0f, 0.0f).contains (p))
        return ClipDrag::none;
    const auto& spec = project_.composition().tracks[track].clips[clip];
    const TimelineGeometry geo (timeline_, headerWidth, lanesArea().getRight());
    const double startSec = project_.secondsAtBeat (spec.startBeat);
    const double endSec = project_.secondsAtBeat (clipBeats (spec).second);
    const float fin = geo.x (project_.beatAtSeconds (startSec + spec.fadeInSeconds));
    const float fout = geo.x (project_.beatAtSeconds (endSec - spec.fadeOutSeconds));
    if (p.y < box.getY() + 9.0f)
    {
        if (std::abs (p.x - fin) <= 6.0f)
            return ClipDrag::fadeIn;
        if (std::abs (p.x - fout) <= 6.0f)
            return ClipDrag::fadeOut;
    }
    if (std::abs (p.x - box.getX()) <= 5.0f)
        return ClipDrag::trimStart;
    if (std::abs (p.x - box.getRight()) <= 5.0f)
        return ClipDrag::trimEnd;
    return ClipDrag::move;
}

mr::AudioClip ArrangementPanel::draggedClip (const juce::MouseEvent& e) const
{
    auto clip = clipDrag_ == ClipDrag::gain ? gainBase_ : project_.composition().tracks[pressedClip_->track].clips[pressedClip_->clip];
    const TimelineGeometry geo (timeline_, headerWidth, lanesArea().getRight());
    const bool free = e.mods.isCommandDown();
    auto snapped = [this, free] (double beat) { return free ? beat : snapRound (beat); };
    // 시작, 끝, 페이드는 곡의 초, 파일 안의 자리와 길이는 파일의 초 (늘인 클립은 둘이 다르다)
    const auto& tempo = project_.tempo();
    const double startSec = project_.secondsAtBeat (clip.startBeat);
    const double length = clip.lengthSeconds ? *clip.lengthSeconds : std::max (0.0, fileSeconds (clip) - clip.offsetSeconds);
    const double endSec = mr::clipSongSecondsAt (clip, tempo, clip.offsetSeconds + length);
    const double playLength = endSec - startSec;
    const double fileLength = fileSeconds (clip);
    const double beatAtMouse = std::max (0.0, geo.beat (e.position.x));
    const double deltaBeats = geo.beat (e.position.x) - geo.beat (e.mouseDownPosition.x);
    switch (clipDrag_)
    {
        case ClipDrag::move:
            clip.startBeat = std::max (0.0, snapped (clip.startBeat + deltaBeats));
            break;
        case ClipDrag::trimStart:
        {
            const double minStartSec = mr::clipSongSecondsAt (clip, tempo, 0.0);   // 파일의 처음까지
            double sec = std::clamp (project_.secondsAtBeat (snapped (beatAtMouse)), std::max (0.0, minStartSec), endSec - 0.01);
            double offset = std::max (0.0, mr::clipSourceAt (clip, tempo, sec));
            if (zeroCrossingCuts)
            {
                offset = mr::nearestZeroCrossing (clip, project_.file().getParentDirectory(), offset);
                sec = std::clamp (mr::clipSongSecondsAt (clip, tempo, offset), std::max (0.0, minStartSec), endSec - 0.01);
            }
            clip.lengthSeconds = std::max (0.01, clip.offsetSeconds + length - offset);
            clip.offsetSeconds = offset;
            clip.startBeat = project_.beatAtSeconds (sec);
            clip.fadeInSeconds = std::min (clip.fadeInSeconds, endSec - sec);
            break;
        }
        case ClipDrag::trimEnd:
        {
            double source = mr::clipSourceAt (clip, tempo, project_.secondsAtBeat (snapped (beatAtMouse)));
            if (zeroCrossingCuts)
                source = mr::nearestZeroCrossing (clip, project_.file().getParentDirectory(), source);
            if (fileLength > 0.0)
                source = std::min (source, fileLength);
            clip.lengthSeconds = std::max (0.01, source - clip.offsetSeconds);
            clip.fadeOutSeconds = std::min (clip.fadeOutSeconds, mr::clipSongSecondsAt (clip, tempo, clip.offsetSeconds + *clip.lengthSeconds) - startSec);
            break;
        }
        case ClipDrag::fadeIn:
            clip.fadeInSeconds = std::clamp (project_.secondsAtBeat (beatAtMouse) - startSec, 0.0, playLength);
            break;
        case ClipDrag::fadeOut:
            clip.fadeOutSeconds = std::clamp (endSec - project_.secondsAtBeat (beatAtMouse), 0.0, playLength);
            break;
        case ClipDrag::warp:
            if (warpMarker_ < clip.warp.size())
            {
                // 곡의 박에서 클립 안의 박으로
                const double songBeat = snapped (mr::warpMarkerSongBeat (clip, warpMarker_) + deltaBeats);
                clip = mr::withWarpMarkerMoved (clip, warpMarker_, songBeat - clip.startBeat + mr::warpBeatAt (clip, clip.offsetSeconds));
            }
            break;
        case ClipDrag::gain:
            if (gainPoint_ < clip.gainCurve.points.size())
            {
                // 파일의 초는 이웃 점 사이와 클립이 트는 구간 안으로, 데시벨은 높이에서
                auto& points = clip.gainCurve.points;
                const double clipEndSource = clip.offsetSeconds + length;
                const double lo = gainPoint_ > 0 ? points[gainPoint_ - 1].beat : clip.offsetSeconds;
                const double hi = gainPoint_ + 1 < points.size() ? points[gainPoint_ + 1].beat : clipEndSource;
                const double source = mr::clipSourceAt (clip, tempo, project_.secondsAtBeat (std::max (0.0, geo.beat (e.position.x))));
                points[gainPoint_].beat = std::round (std::clamp (source, lo, std::max (lo, hi)) * 1.0e6) / 1.0e6;
                points[gainPoint_].value = gainDbAt (clipBounds (pressedClip_->track, pressedClip_->clip), e.position.y);
            }
            break;
        case ClipDrag::none:
            break;
    }
    auto tidy = [] (double v) { return std::round (v * 1.0e6) / 1.0e6; };
    clip.startBeat = tidy (clip.startBeat);
    clip.offsetSeconds = tidy (clip.offsetSeconds);
    if (clip.lengthSeconds)
        clip.lengthSeconds = tidy (*clip.lengthSeconds);
    clip.fadeInSeconds = tidy (clip.fadeInSeconds);
    clip.fadeOutSeconds = tidy (clip.fadeOutSeconds);
    return clip;
}

bool ArrangementPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (! project_.loaded())
        return false;
    for (const auto& f : files)
        if (mr::isAudioFileName (f))
            return true;
    return false;
}

void ArrangementPanel::filesDropped (const juce::StringArray& files, int x, int y)
{
    juce::StringArray audio;
    for (const auto& f : files)
        if (mr::isAudioFileName (f))
            audio.add (f);
    if (audio.isEmpty() || ! onDropAudio)
        return;
    const TimelineGeometry geo (timeline_, headerWidth, lanesArea().getRight());
    const double beat = x >= headerWidth ? snapFloor (geo.beat ((float) x)) : timeline_.playheadBeat();
    std::optional<size_t> track;
    if (y >= lanesTop() && y < lanesTop() + rowsBottom_ - scrollY_)
        if (const auto t = (size_t) trackAtY (y); isAudioTrack (t))
            track = t;
    onDropAudio (audio, track, beat);
}

int ArrangementPanel::dropIndexAt (int y) const
{
    const int rel = y - lanesTop() + scrollY_;
    // 보이는 트랙의 가운데보다 위면 그 트랙 앞
    for (size_t i = 0; i < rowTop_.size(); ++i)
        if (rowHeight_[i] > 0 && rel < rowTop_[i] + rowHeight_[i] / 2)
            return (int) i;
    return (int) rows_.size();
}

void ArrangementPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    const auto lanes = lanesArea();
    const TimelineGeometry geo (timeline_, headerWidth, lanes.getRight());
    const auto& barBeats = project_.meters();   // 박자 지도

    if (! project_.loaded())
    {
        g.setColour (colours::textDim);
        g.setFont (uiFont (16.0f));
        g.drawText (juce::String::fromUTF8 ("프로젝트를 열면 트랙이 여기에 나옵니다. 파일 메뉴의 열기(⌘O)로 프로젝트 파일(.json)을 고릅니다."),
                    getLocalBounds().reduced (40), juce::Justification::centred, true);
        return;
    }

    // 레인
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (juce::Rectangle<int> (0, lanesTop(), getWidth() - scrollBarSize, lanes.getHeight()));
        for (size_t i = 0; i < rows_.size(); ++i)
        {
            const auto row = rowBounds (i);
            if (row.getHeight() == 0 || row.getBottom() < lanesTop() || row.getY() > lanes.getBottom())
                continue;
            paintLane (g, i, row.withLeft (headerWidth));
            paintHeader (g, i, row.withWidth (headerWidth));
        }
        for (const auto& f : folderRows_)
            paintFolder (g, f, juce::Rectangle<int> (0, lanesTop() + f.top - scrollY_, getWidth() - scrollBarSize, folderHeight));
        // 트랙 아래 빈 곳에도 격자, 머리 칸에는 트랙 더하기 단추
        const int bottom = lanesTop() + rowsBottom_ - scrollY_;
        if (bottom < lanes.getBottom())
        {
            auto empty = juce::Rectangle<int> (headerWidth, bottom, lanes.getWidth(), lanes.getBottom() - bottom);
            g.setColour (colours::background);
            g.fillRect (empty);
            drawGrid (g, empty, geo, barBeats);
            g.setColour (colours::panel);
            g.fillRect (0, bottom, headerWidth, lanes.getBottom() - bottom);
            const auto add = addTrackBox().toFloat();
            g.setColour (colours::panelRaised);
            g.fillRoundedRectangle (add, 5.0f);
            g.setColour (colours::outline);
            g.drawRoundedRectangle (add.reduced (0.5f), 5.0f, 1.0f);
            g.setColour (colours::textDim);
            g.setFont (uiFont (13.0f, true));
            g.drawText (juce::String::fromUTF8 ("+  트랙 더하기"), add, juce::Justification::centred);
        }
        // 마커 줄과 고른 구간
        {
            const auto views = markerViews();
            for (const auto& m : views)
            {
                const float x = geo.x (m.beat);
                if (x >= (float) lanes.getX() && x <= (float) lanes.getRight())
                {
                    g.setColour (colours::marker.withAlpha (0.22f));
                    g.fillRect (x, (float) lanesTop(), 1.0f, (float) lanes.getHeight());
                }
            }
        }
        if (range_ && range_->lastTrack < rows_.size())
        {
            const float x0 = std::max ((float) lanes.getX(), geo.x (range_->start));
            const float x1 = std::min ((float) lanes.getRight(), geo.x (range_->end));
            const int y0 = rowBounds (range_->firstTrack).getY(), y1 = rowBounds (range_->lastTrack).getBottom();
            if (x1 > x0)
            {
                const juce::Rectangle<float> box (x0, (float) y0, x1 - x0, (float) (y1 - y0));
                g.setColour (colours::selection.withAlpha (0.18f));
                g.fillRect (box);
                g.setColour (colours::selection.withAlpha (0.9f));
                g.drawRect (box, 1.5f);
            }
        }
        if (movingTrack_ && dropIndex_ >= 0)
        {
            const int y = lanesTop() + (dropIndex_ < (int) rowTop_.size() ? rowTop_[(size_t) dropIndex_] : rowsBottom_) - scrollY_;
            g.setColour (colours::accent);
            g.fillRect (0, y - 1, getWidth() - scrollBarSize, 3);
        }
        drawPlayhead (g, lanes, geo, timeline_.playheadBeat());
    }

    // 눈금자, 마커 띠, 왼쪽 위 모서리
    drawRuler (g, juce::Rectangle<int> (headerWidth, 0, lanes.getWidth(), timeRulerHeight), geo, barBeats,
               timeline_.lengthBeats(), timeline_.playheadBeat(), timeline_.loopRange(), timeline_.loopOn());
    drawMarkers (g, markerStrip(), geo, markerViews(), selectedMarker_);
    paintSections (g, geo);
    if (video_)
    {
        paintVideoLane (g, geo);
        const auto head = juce::Rectangle<int> (0, rulerHeight, headerWidth, videoHeight);
        g.setColour (colours::panel);
        g.fillRect (head);
        g.setColour (colours::text);
        g.setFont (uiFont (12.0f, true));
        g.drawText (juce::String::fromUTF8 ("영상"), head.withHeight (22).reduced (12, 0).withTrimmedTop (4), juce::Justification::centredLeft);
        g.setColour (colours::textDim);
        g.setFont (uiFont (11.0f));
        g.drawText (video_->file.getFileName(), juce::Rectangle<int> (12, rulerHeight + 22, headerWidth - 24, 16), juce::Justification::centredLeft, true);
        g.setColour (colours::textFaint);
        g.drawText (juce::String (video_->rate.fps(), video_->rate.den == 1 ? 0 : 2) + " fps, " + juce::String (video_->width) + "x"
                        + juce::String (video_->height),
                    juce::Rectangle<int> (12, rulerHeight + 38, headerWidth - 24, 16), juce::Justification::centredLeft, true);
        g.setColour (colours::outline);
        g.fillRect (0, lanesTop() - 1, headerWidth, 1);
    }
    auto corner = juce::Rectangle<int> (0, 0, headerWidth, rulerHeight);
    g.setColour (colours::panel);
    g.fillRect (corner);
    g.setColour (colours::textDim);
    g.setFont (uiFont (12.0f, true));
    g.drawText (juce::String::fromUTF8 ("트랙 ") + juce::String ((int) rows_.size()), corner.withHeight (timeRulerHeight).reduced (12, 0),
                juce::Justification::centredLeft);
    g.setColour (colours::textFaint);
    g.setFont (uiFont (11.0f));
    g.drawText (juce::String::fromUTF8 ("마커"), juce::Rectangle<int> (12, timeRulerHeight, headerWidth - 24, markerHeight),
                juce::Justification::centredLeft);
    g.drawText (juce::String::fromUTF8 ("구간"), juce::Rectangle<int> (12, timeRulerHeight + markerHeight, headerWidth - 24, sectionHeight),
                juce::Justification::centredLeft);
    g.setColour (colours::outline);
    g.fillRect (0, rulerHeight - 1, headerWidth, 1);
    g.fillRect (headerWidth - 1, 0, 1, getHeight() - scrollBarSize);
}

void ArrangementPanel::paintFolder (juce::Graphics& g, const FolderRow& folder, juce::Rectangle<int> r)
{
    const bool collapsed = collapsed_.count (folder.name) > 0;
    g.setColour (colours::panelRaised);
    g.fillRect (r);
    g.setColour (colours::outline);
    g.fillRect (r.getX(), r.getBottom() - 1, r.getWidth(), 1);
    // 접고 펴는 삼각형
    juce::Path triangle;
    const float cx = 16.0f, cy = (float) r.getCentreY();
    if (collapsed)
        triangle.addTriangle (cx - 3.0f, cy - 5.0f, cx - 3.0f, cy + 5.0f, cx + 4.0f, cy);
    else
        triangle.addTriangle (cx - 5.0f, cy - 3.0f, cx + 5.0f, cy - 3.0f, cx, cy + 4.0f);
    g.setColour (colours::textDim);
    g.fillPath (triangle);
    const auto& tracks = project_.composition().tracks;
    juce::String sub = juce::String ((int) (folder.last - folder.first + 1)) + juce::String::fromUTF8 ("트랙");
    if (folder.first < tracks.size() && ! tracks[folder.first].output.empty())
    {
        bool same = true;
        for (size_t i = folder.first; i <= folder.last && i < tracks.size(); ++i)
            same = same && tracks[i].output == tracks[folder.first].output;
        if (same)
            sub << juce::String::fromUTF8 (", 버스 ") << juce::String::fromUTF8 (tracks[folder.first].output.c_str()) << juce::String::fromUTF8 ("로");
    }
    g.setColour (colours::text);
    g.setFont (uiFont (13.0f, true));
    g.drawText (juce::String::fromUTF8 (folder.name.c_str()), juce::Rectangle<int> (28, r.getY(), headerWidth - 40, r.getHeight()), juce::Justification::centredLeft, true);
    g.setColour (colours::textDim);
    g.setFont (uiFont (11.0f));
    g.drawText (sub, juce::Rectangle<int> (headerWidth + 8, r.getY(), 300, r.getHeight()), juce::Justification::centredLeft, true);
}

void ArrangementPanel::paintHeader (juce::Graphics& g, size_t index, juce::Rectangle<int> r)
{
    const auto& track = project_.composition().tracks[index];
    const auto& summary = project_.summary (index);
    const bool selected = (int) index == timeline_.selectedTrack();
    const auto& row = rows_[index];
    const bool audible = ! row.mute;

    g.setColour (selected ? colours::panelRaised.brighter (0.04f) : colours::panel);
    g.fillRect (r);
    g.setColour (summary.colour.withAlpha (audible ? 1.0f : 0.35f));
    g.fillRect (r.getX(), r.getY() + 1, 4, r.getHeight() - 2);
    if (selected)
    {
        g.setColour (colours::selection.withAlpha (0.12f));
        g.fillRect (r);
    }

    auto text = r.reduced (14, 8).withTrimmedRight (72);
    g.setColour (audible ? colours::text : colours::textFaint);
    g.setFont (uiFont (14.0f, true));
    g.drawText (juce::String::fromUTF8 (track.name.c_str()), text.removeFromTop (20), juce::Justification::centredLeft, true);
    g.setColour (colours::textDim);
    g.setFont (uiFont (12.0f));
    auto desc = row.description.isNotEmpty() ? row.description : juce::String::fromUTF8 (track.instrument.c_str());
    if (isAudioTrack (index))
        desc = juce::String::fromUTF8 ("오디오, 클립 ") + juce::String ((int) track.clips.size()) + juce::String::fromUTF8 ("개");
    if ((int) index == inputMonitorTrack_)
    {
        g.setColour (colours::danger);
        desc = juce::String::fromUTF8 ("입력 모니터링, ") + desc;
    }
    g.drawText (desc, text.removeFromTop (18), juce::Justification::centredLeft, true);

    auto drawToggle = [&] (juce::Rectangle<int> box, const juce::String& label, bool on, juce::Colour onColour)
    {
        g.setColour (on ? onColour : colours::panelRaised);
        g.fillRoundedRectangle (box.toFloat(), 4.0f);
        g.setColour (on ? onColour.brighter (0.2f) : colours::outline);
        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 4.0f, 1.0f);
        g.setColour (on ? juce::Colours::black : colours::textDim);
        g.setFont (uiFont (12.0f, true));
        g.drawText (label, box, juce::Justification::centred);
    };
    drawToggle (muteBox (index), "M", row.mute, colours::mute);
    drawToggle (soloBox (index), "S", row.solo, colours::solo);

    // 레벨 (-60 .. 0 dBFS)
    auto meter = juce::Rectangle<int> (r.getX() + 14, r.getBottom() - 9, headerWidth - 28, 4);
    g.setColour (colours::background);
    g.fillRoundedRectangle (meter.toFloat(), 2.0f);
    const float db = juce::Decibels::gainToDecibels (row.level, -60.0f);
    const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    if (frac > 0.0f)
    {
        g.setColour (db > -1.0f ? colours::danger : summary.colour.interpolatedWith (colours::solo, 0.3f));
        g.fillRoundedRectangle (meter.toFloat().withWidth (meter.getWidth() * frac), 2.0f);
    }

    g.setColour (colours::outline);
    g.fillRect (r.getX(), r.getBottom() - 1, r.getWidth(), 1);
}

void ArrangementPanel::paintLane (juce::Graphics& g, size_t index, juce::Rectangle<int> r)
{
    const auto& track = project_.composition().tracks[index];
    const auto& summary = project_.summary (index);
    const TimelineGeometry geo (timeline_, headerWidth, r.getRight());
    const bool selected = (int) index == timeline_.selectedTrack();
    const bool audible = ! rows_[index].mute;

    g.setColour (index % 2 == 0 ? colours::laneA : colours::laneB);
    g.fillRect (r);
    if (selected)
    {
        g.setColour (colours::selection.withAlpha (0.06f));
        g.fillRect (r);
    }
    drawGrid (g, r, geo, project_.meters());

    const auto colour = summary.colour.withMultipliedAlpha (audible ? 1.0f : 0.35f);

    // 다이내믹스 곡선 (CC1, 없으면 CC11) 을 레인 바닥에 옅게
    for (int id : { 1, 11 })
    {
        const auto it = track.automation.find (id);
        if (it == track.automation.end() || it->second.empty())
            continue;
        auto area = r.toFloat().reduced (0.0f, 4.0f);
        g.setColour (colour.withAlpha (audible ? 0.10f : 0.04f));
        g.fillPath (automationPath (it->second, id, area, geo, true));
        g.setColour (colour.withAlpha (audible ? 0.28f : 0.1f));
        g.strokePath (automationPath (it->second, id, area, geo, false), juce::PathStrokeType (1.0f));
        break;
    }

    if (isAudioTrack (index))
    {
        paintClips (g, index, r, colour, audible);
        g.setColour (colours::outline.withAlpha (0.6f));
        g.fillRect (r.getX(), r.getBottom() - 1, r.getWidth(), 1);
        return;
    }

    // 음표
    const int span = std::max (12, summary.highPitch - summary.lowPitch);
    const int high = summary.highPitch + (span - (summary.highPitch - summary.lowPitch)) / 2;
    const float usable = (float) r.getHeight() - 14.0f;
    const float noteH = juce::jlimit (2.0f, 6.0f, usable / (float) (span + 1));
    const double viewEnd = geo.endBeat();
    for (const auto& n : track.notes)
    {
        if (n.startBeat > viewEnd || n.startBeat + n.durationBeat < geo.startBeat)
            continue;
        const float x0 = std::max (geo.x (n.startBeat), (float) r.getX());
        const float x1 = std::min (geo.x (n.startBeat + n.durationBeat), (float) r.getRight());
        const float y = (float) r.getY() + 7.0f + (float) (high - n.pitch) / (float) span * (usable - noteH);
        g.setColour (colour.withAlpha ((0.45f + 0.55f * (float) n.velocity / 127.0f) * (audible ? 1.0f : 0.4f)));
        g.fillRoundedRectangle (x0, y, std::max (1.5f, x1 - x0 - 0.5f), noteH, std::min (1.5f, noteH * 0.5f));
    }

    g.setColour (colours::outline.withAlpha (0.6f));
    g.fillRect (r.getX(), r.getBottom() - 1, r.getWidth(), 1);
}

void ArrangementPanel::resized()
{
    vertical_.setBounds (getWidth() - scrollBarSize, lanesTop(), scrollBarSize, getHeight() - lanesTop() - scrollBarSize);
    horizontal_.setBounds (headerWidth, getHeight() - scrollBarSize, getWidth() - headerWidth - scrollBarSize, scrollBarSize);
    updateScrollBars();
}

void ArrangementPanel::updateScrollBars()
{
    const auto lanes = lanesArea();
    const int visible = lanes.getHeight();
    scrollY_ = juce::jlimit (0, std::max (0, contentHeight() - visible), scrollY_);
    vertical_.setRangeLimits (0.0, (double) std::max (contentHeight(), visible));
    vertical_.setCurrentRange ((double) scrollY_, (double) visible, juce::dontSendNotification);

    const double visibleBeats = lanes.getWidth() / timeline_.pixelsPerBeat();
    const double total = std::max (timeline_.lengthBeats() + 8.0, timeline_.viewStartBeat() + visibleBeats);
    horizontal_.setRangeLimits (0.0, total);
    horizontal_.setCurrentRange (timeline_.viewStartBeat(), visibleBeats, juce::dontSendNotification);
}

void ArrangementPanel::timelineChanged()
{
    updateScrollBars();
    repaint();
}

void ArrangementPanel::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (bar == &vertical_)
    {
        scrollY_ = (int) start;
        repaint();
    }
    else
        timeline_.setView (start, timeline_.pixelsPerBeat());
}

void ArrangementPanel::mouseDown (const juce::MouseEvent& e)
{
    draggingRuler_ = false;
    draggingLoop_ = false;
    pressedHeader_.reset();
    movingTrack_ = false;
    dropIndex_ = -1;
    pressedMarker_.reset();
    markerDragBeat_.reset();
    rangeAnchor_.reset();
    selectingRange_ = false;
    pressedClip_.reset();
    clipPreview_.reset();
    clipDrag_ = ClipDrag::none;
    videoDragOffset_.reset();
    pressedSection_.reset();
    sectionTarget_.reset();
    if (! project_.loaded())
        return;
    const auto lanes = lanesArea();
    const TimelineGeometry geo (timeline_, headerWidth, lanes.getRight());
    if (sectionStrip().contains (e.getPosition()))
    {
        if (const auto section = sectionAt (e.getPosition()))
        {
            if (e.mods.isPopupMenu())
            {
                if (onSectionMenu)
                    onSectionMenu (*section);
                return;
            }
            pressedSection_ = section;
            sectionPressX_ = (float) e.x;
        }
        return;
    }
    if (e.y >= timeRulerHeight && e.y < timeRulerHeight + markerHeight && e.x >= headerWidth)
    {
        const auto marker = markerAt (e.getPosition());
        const double beat = std::max (0.0, geo.beat ((float) e.x));
        if (e.mods.isPopupMenu())
        {
            if (marker)
                selectedMarker_ = (int) *marker;
            repaint();
            if (onMarkerMenu)
                onMarkerMenu (marker ? (int) *marker : -1, marker ? project_.markers()[*marker].beat : beat);
            return;
        }
        if (marker)
        {
            pressedMarker_ = marker;
            selectedMarker_ = (int) *marker;
            repaint();
        }
        return;
    }
    if (e.y < timeRulerHeight && e.x >= headerWidth)
    {
        if (loopDrag_.begin ((float) e.x, juce::Rectangle<int> (headerWidth, 0, lanes.getWidth(), timeRulerHeight), geo, project_.meters(),
                             timeline_.loopRange(), e.mods.isAltDown()))
        {
            draggingLoop_ = true;
            return;
        }
        draggingRuler_ = true;
        if (onSeek)
            onSeek (std::max (0.0, geo.beat ((float) e.x)));
        return;
    }
    if (video_ && e.y >= rulerHeight && e.y < lanesTop())
    {
        if (e.mods.isPopupMenu() || e.x < headerWidth)
        {
            if (onVideoMenu)
                onVideoMenu();
            return;
        }
        videoPressX_ = (float) e.x;
        videoDragOffset_ = video_->offsetSeconds;
        videoDragFree_ = e.mods.isCommandDown();
        repaint (videoLaneBounds());
        return;
    }
    for (const auto& f : folderRows_)
        if (juce::Rectangle<int> (0, lanesTop() + f.top - scrollY_, getWidth() - scrollBarSize, folderHeight).contains (e.getPosition()))
        {
            if (e.mods.isPopupMenu())
            {
                if (onFolderMenu)
                    onFolderMenu (f.name);
            }
            else
                setFolderCollapsed (f.name, collapsed_.count (f.name) == 0);
            return;
        }
    for (size_t i = 0; i < rows_.size(); ++i)
    {
        if (! rowBounds (i).contains (e.getPosition()))
            continue;
        if (muteBox (i).contains (e.getPosition()))
        {
            if (onMute)
                onMute (i, ! rows_[i].mute);
            return;
        }
        if (soloBox (i).contains (e.getPosition()))
        {
            if (onSolo)
                onSolo (i, ! rows_[i].solo);
            return;
        }
        timeline_.setSelectedTrack ((int) i);
        if (e.x >= headerWidth && isAudioTrack (i))
        {
            // 몸통이 그 점을 담은 클립을 먼저 (붙어 있는 클립의 끝과 처음은 손잡이 자리가 겹친다)
            const auto& clips = project_.composition().tracks[i].clips;
            std::vector<size_t> order;
            for (size_t c = clips.size(); c-- > 0;)
                if (clipBounds (i, c).contains (e.position))
                    order.push_back (c);
            for (size_t c = clips.size(); c-- > 0;)
                if (std::find (order.begin(), order.end(), c) == order.end())
                    order.push_back (c);
            for (const size_t c : order)
                if (const auto marker = ! e.mods.isPopupMenu() ? warpMarkerAt (i, c, e.position) : std::nullopt)
                {
                    setSelectedClip (ClipRef { i, c });
                    setRange (std::nullopt);
                    if (onClipSelected)
                        onClipSelected();
                    if (e.mods.isAltDown())
                    {
                        // ⌥ 누르기: 그 마커를 지운다
                        if (onClipEdited)
                            onClipEdited (ClipRef { i, c }, mr::withWarpMarkerRemoved (clips[c], *marker), false);
                        return;
                    }
                    pressedClip_ = ClipRef { i, c };
                    clipDrag_ = ClipDrag::warp;
                    warpMarker_ = *marker;
                    clipCopy_ = false;
                    return;
                }
            for (const size_t c : order)
                if (const auto hit = clipHit (i, c, e.position); hit != ClipDrag::none)
                {
                    setSelectedClip (ClipRef { i, c });
                    setRange (std::nullopt);
                    if (onClipSelected)
                        onClipSelected();
                    if (e.mods.isPopupMenu())
                    {
                        if (onClipMenu)
                            onClipMenu (ClipRef { i, c });
                        return;
                    }
                    if (gainEditing_ && hit == ClipDrag::move && ! clips[c].muted)
                    {
                        // 게인 곡선의 점: ⌥ 누르기는 지우고, 빈 자리를 누르면 더해 끈다
                        auto edited = clips[c];
                        auto& points = edited.gainCurve.points;
                        auto point = gainPointAt (i, c, e.position);
                        if (point && e.mods.isAltDown())
                        {
                            points.erase (points.begin() + (std::ptrdiff_t) *point);
                            if (onClipEdited)
                                onClipEdited (ClipRef { i, c }, edited, false);
                            return;
                        }
                        if (! point)
                        {
                            const double source = mr::clipSourceAt (edited, project_.tempo(), project_.secondsAtBeat (std::max (0.0, geo.beat (e.position.x))));
                            const auto at = std::upper_bound (points.begin(), points.end(), source,
                                                              [] (double s, const mr::AutomationPoint& q) { return s < q.beat; });
                            point = (size_t) (at - points.begin());
                            points.insert (at, mr::AutomationPoint { std::round (source * 1.0e6) / 1.0e6, gainDbAt (clipBounds (i, c), e.position.y),
                                                                     mr::CurveShape::linear });
                            clipPreview_ = edited;   // 놓으면 더한 점이 편집이 된다
                        }
                        pressedClip_ = ClipRef { i, c };
                        clipDrag_ = ClipDrag::gain;
                        gainPoint_ = *point;
                        gainBase_ = edited;
                        clipCopy_ = false;
                        repaint();
                        return;
                    }
                    pressedClip_ = ClipRef { i, c };
                    clipDrag_ = hit;
                    clipCopy_ = e.mods.isAltDown() && hit == ClipDrag::move;
                    return;
                }
        }
        if (e.x >= headerWidth)
            setSelectedClip (std::nullopt);
        if (e.x >= headerWidth && ! e.mods.isPopupMenu())
            rangeAnchor_ = std::make_pair (i, std::max (0.0, geo.beat ((float) e.x)));
        if (e.x < headerWidth)
        {
            if (e.mods.isPopupMenu())
            {
                if (onTrackMenu)
                    onTrackMenu (i);
            }
            else
                pressedHeader_ = i;
        }
        return;
    }
    if (e.x < headerWidth && addTrackBox().contains (e.getPosition()) && onAddTrack)
        onAddTrack();
}

void ArrangementPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! project_.loaded())
        return;
    if (sectionStrip().contains (e.getPosition()))
    {
        // 모든 트랙의 그 구간을 고른다
        if (const auto section = sectionAt (e.getPosition()); section && ! rows_.empty())
            setRange (Range { 0, rows_.size() - 1, sections_[*section].start, sections_[*section].end });
        return;
    }
    if (e.y >= timeRulerHeight && e.y < timeRulerHeight + markerHeight && e.x >= headerWidth)
    {
        if (const auto marker = markerAt (e.getPosition()))
            beginMarkerRename (*marker);
        else if (onAddMarker)
        {
            const TimelineGeometry geo (timeline_, headerWidth, lanesArea().getRight());
            onAddMarker (project_.meters().roundToBar (std::max (0.0, geo.beat ((float) e.x))));
        }
        return;
    }
    if (video_ && e.y >= rulerHeight && e.y < lanesTop())
    {
        if (const auto scene = sceneAt (e.getPosition()); scene && onSceneMarker)
            onSceneMarker (scenes_[*scene]);
        return;
    }
    if (e.y < lanesTop())
        return;
    if (e.x >= headerWidth)
    {
        // 오디오 클립을 두 번 누르면 그 자리에 워프 마커
        for (size_t i = 0; i < rows_.size(); ++i)
        {
            if (! isAudioTrack (i) || ! rowBounds (i).contains (e.getPosition()))
                continue;
            const auto& clips = project_.composition().tracks[i].clips;
            for (size_t c = clips.size(); c-- > 0;)
                if (clipBounds (i, c).contains (e.position) && ! clips[c].muted && mr::clipStretchAvailable())
                {
                    const TimelineGeometry geo (timeline_, headerWidth, lanesArea().getRight());
                    const auto& tempo = project_.tempo();
                    const double length = mr::clipLengthSeconds (clips[c], project_.file().getParentDirectory());
                    const double source = mr::clipSourceAt (clips[c], tempo, project_.secondsAtBeat (std::max (0.0, geo.beat (e.position.x))));
                    if (source > clips[c].offsetSeconds && source < clips[c].offsetSeconds + length && onClipEdited)
                        onClipEdited (ClipRef { i, c }, mr::withWarpMarkerAdded (clips[c], tempo, length, source), false);
                    return;
                }
        }
        return;
    }
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rowBounds (i).contains (e.getPosition()) && ! muteBox (i).contains (e.getPosition()) && ! soloBox (i).contains (e.getPosition()))
        {
            if (nameBox (i).contains (e.getPosition()))
                beginRename (i);
            else if (onOpenInstrument)
                onOpenInstrument (i);
            return;
        }
}

void ArrangementPanel::mouseDrag (const juce::MouseEvent& e)
{
    const TimelineGeometry geo (timeline_, headerWidth, lanesArea().getRight());
    if (draggingLoop_)
    {
        if (const auto range = loopDrag_.drag ((float) e.x, geo))
            timeline_.setLoopRange (range);   // 미리보기 (놓을 때 편집)
        return;
    }
    if (pressedSection_)
    {
        if (! sectionTarget_ && std::abs (e.getDistanceFromDragStartX()) < 4)
            return;
        // 가까운 경계 (구간 처음들과 마지막 구간의 끝)
        const double beat = geo.beat ((float) e.x);
        size_t best = 0;
        double bestDistance = 1.0e300;
        for (size_t i = 0; i <= sections_.size(); ++i)
        {
            const double b = i < sections_.size() ? sections_[i].start : sections_.back().end;
            if (std::abs (b - beat) < bestDistance)
            {
                bestDistance = std::abs (b - beat);
                best = i;
            }
        }
        sectionTarget_ = best;
        sectionDragX_ = (float) e.x;
        repaint (sectionStrip());
        return;
    }
    if (videoDragOffset_ && video_)
    {
        // 영상 처음이 놓일 박: 누른 자리에서 끈 만큼. 격자에 맞추고, ⌘ 는 프레임에 맞춘다
        const auto& tempo = project_.tempo();
        const float startX = geo.x (tempo.beatAt (video_->offsetSeconds)) + ((float) e.x - videoPressX_);
        double offset = 0.0;
        if (videoDragFree_ || e.mods.isCommandDown())
        {
            const double fps = video_->rate.valid() ? video_->rate.fps() : 30.0;
            offset = std::round (tempo.secondsAt (geo.beat (startX)) * fps) / fps;
        }
        else
        {
            const double b = geo.beat (startX);   // 곡 앞(음수)은 처음 마디 길이나 박의 격자
            offset = tempo.secondsAt (b >= 0.0 ? snapRound (b) : std::round (b / rangeSnap()) * rangeSnap());
        }
        if (std::abs (e.getDistanceFromDragStartX()) < 3)
            offset = video_->offsetSeconds;
        if (offset != *videoDragOffset_)
        {
            videoDragOffset_ = offset;
            repaint (videoLaneBounds());
        }
        return;
    }
    if (pressedClip_)
    {
        if (! clipPreview_ && e.getDistanceFromDragStart() < 3)
            return;
        clipPreview_ = draggedClip (e);
        repaint();
        return;
    }
    if (pressedMarker_)
    {
        if (! markerDragBeat_ && std::abs (e.getDistanceFromDragStartX()) < 4)
            return;
        markerDragBeat_ = LoopRulerDrag::snapBeat (geo.beat ((float) e.x), LoopRulerDrag::snapsToBars (geo), project_.meters());
        repaint();
        return;
    }
    if (rangeAnchor_)
    {
        if (! selectingRange_ && e.getDistanceFromDragStart() < 5)
            return;
        selectingRange_ = true;
        const double b0 = rangeAnchor_->second, b1 = std::max (0.0, geo.beat ((float) e.x));
        const double lo = snapFloor (std::min (b0, b1));
        double hi = snapCeil (std::max (b0, b1));
        if (hi <= lo)
            hi = snapCeil (lo + 1.0e-6);
        const size_t t0 = rangeAnchor_->first, t1 = (size_t) trackAtY (e.y);
        setRange (Range { std::min (t0, t1), std::max (t0, t1), lo, hi });
        return;
    }
    if (pressedHeader_)
    {
        if (! movingTrack_ && std::abs (e.getDistanceFromDragStartY()) < 6)
            return;
        movingTrack_ = true;
        dropIndex_ = dropIndexAt (e.y);
        repaint();
        return;
    }
    if (! draggingRuler_)
        return;
    if (onSeek)
        onSeek (std::max (0.0, geo.beat ((float) e.x)));
}

void ArrangementPanel::mouseUp (const juce::MouseEvent& e)
{
    if (pressedSection_)
    {
        const size_t section = *pressedSection_;
        const auto target = sectionTarget_;
        pressedSection_.reset();
        sectionTarget_.reset();
        repaint (sectionStrip());
        if (! target)
        {
            if (onSeek && section < sections_.size())
                onSeek (sections_[section].start);   // 누르기만 하면 그 처음으로
        }
        else if (*target != section && *target != section + 1 && onSectionMoved)
            onSectionMoved (section, *target);
        return;
    }
    if (videoDragOffset_)
    {
        const double offset = *videoDragOffset_;
        const bool moved = video_ && std::abs (offset - video_->offsetSeconds) > 1.0e-9;
        videoDragOffset_.reset();
        repaint (videoLaneBounds());
        if (moved && onVideoMoved)
            onVideoMoved (offset);
        else if (! moved && video_ && onSeek && std::abs (e.getDistanceFromDragStartX()) < 3)
        {
            const TimelineGeometry geo (timeline_, headerWidth, lanesArea().getRight());
            onSeek (std::max (0.0, geo.beat ((float) e.x)));   // 누르기만 하면 그 자리로
        }
        return;
    }
    if (pressedClip_)
    {
        const auto ref = *pressedClip_;
        const bool moved = clipPreview_.has_value();
        const auto edited = moved ? draggedClip (e) : mr::AudioClip();
        const bool copy = clipCopy_;
        pressedClip_.reset();
        clipPreview_.reset();
        clipDrag_ = ClipDrag::none;
        repaint();
        if (moved && onClipEdited)
            onClipEdited (ref, edited, copy);
        return;
    }
    if (pressedMarker_)
    {
        const auto marker = *pressedMarker_;
        const auto beat = markerDragBeat_;
        pressedMarker_.reset();
        markerDragBeat_.reset();
        repaint();
        if (beat && *beat != project_.markers()[marker].beat)
        {
            if (onMoveMarker)
                onMoveMarker (marker, *beat);
        }
        else if (! beat && onSeek)
            onSeek (project_.markers()[marker].beat);   // 누르기만 하면 그 자리로
        return;
    }
    if (rangeAnchor_)
    {
        const bool selected = selectingRange_;
        rangeAnchor_.reset();
        selectingRange_ = false;
        if (! selected)
            setRange (std::nullopt);   // 레인을 누르기만 하면 트랙만 고른다
        return;
    }
    if (pressedHeader_)
    {
        const auto from = *pressedHeader_;
        const bool moved = movingTrack_;
        pressedHeader_.reset();
        movingTrack_ = false;
        if (moved)
        {
            // 놓은 자리는 끌기 전 목록의 틈 번호다. 뺀 뒤의 번호로 바꾼다
            int to = dropIndexAt (e.y);
            if (to > (int) from)
                --to;
            dropIndex_ = -1;
            repaint();
            if (to != (int) from && onMoveTrack)
                onMoveTrack (from, (size_t) to);
        }
        return;
    }
    if (! draggingLoop_)
        return;
    draggingLoop_ = false;
    const TimelineGeometry geo (timeline_, headerWidth, lanesArea().getRight());
    const auto range = loopDrag_.drag ((float) e.x, geo);
    if (range || loopDrag_.mode != LoopRulerDrag::Mode::create)   // 새로 만들다 너비가 없으면 그대로 둔다
        if (onLoopEdited)
            onLoopEdited (range);
    timeline_.setLoopRange (project_.loopRange());
}

void ArrangementPanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        const TimelineGeometry geo (timeline_, headerWidth, lanesArea().getRight());
        const float anchorX = (float) std::max (e.x, headerWidth);
        timeline_.zoomAround (std::pow (1.0025, wheel.deltaY * 400.0), geo.beat (anchorX), anchorX - headerWidth);
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
        scrollY_ -= (int) std::round (wheel.deltaY * 240.0);
        updateScrollBars();
        repaint();
    }
}

void ArrangementPanel::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    const TimelineGeometry geo (timeline_, headerWidth, lanesArea().getRight());
    const float anchorX = (float) std::max (e.x, headerWidth);
    timeline_.zoomAround (scaleFactor, geo.beat (anchorX), anchorX - headerWidth);
}

} // namespace daw
