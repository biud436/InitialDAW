#include "Project.h"

#include "composition/Arrange.h"
#include "rendering/OfflineRenderer.h"

#include <algorithm>

namespace daw
{

// 편집 하나: 문서의 부분들(트랙, 최상위 속성)의 앞뒤 JSON
class Project::ReplaceAction final : public juce::UndoableAction
{
public:
    ReplaceAction (Project& project, std::vector<Change> before, std::vector<Change> after)
        : project_ (project), before_ (std::move (before)), after_ (std::move (after))
    {
    }

    bool perform() override { return project_.apply (after_, +1); }
    bool undo() override { return project_.apply (before_, -1); }
    int getSizeInUnits() override
    {
        int size = 16;
        auto count = [&size] (const juce::var& track)
        {
            if (const auto* notes = track.getProperty ("notes", juce::var()).getArray())
                size += notes->size() * 2;
        };
        for (const auto& c : after_)
        {
            count (c.value);
            if (const auto* tracks = c.value.getArray(); tracks != nullptr && c.property == juce::Identifier ("tracks"))
                for (const auto& t : *tracks)
                    count (t);
        }
        return size;
    }

private:
    Project& project_;
    std::vector<Change> before_, after_;
};

Project::Project() = default;
Project::~Project() { undo_.clearUndoHistory(); }

bool Project::load (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = juce::String::fromUTF8 ("파일이 없습니다: ") + file.getFullPathName();
        return false;
    }
    std::string docError;
    auto doc = mr::CompositionDocument::load (file, docError);
    if (! doc)
    {
        error = juce::String::fromUTF8 ("프로젝트를 읽을 수 없습니다.\n") + juce::String::fromUTF8 (docError.c_str());
        return false;
    }
    auto result = doc->parse();
    if (! result.composition)
    {
        error = juce::String::fromUTF8 ("프로젝트를 읽을 수 없습니다.") + describeErrors (result);
        return false;
    }

    undo_.clearUndoHistory();
    depth_ = savedDepth_ = 0;
    parsedAfter_.reset();
    document_ = std::move (doc);
    file_ = file;
    modified_ = file.getLastModificationTime();
    adopt (std::move (*result.composition), result.diagnostics.warnings);
    return true;
}

void Project::adopt (mr::Composition composition, std::vector<mr::Diagnostic> warnings)
{
    composition_ = std::move (composition);
    tempo_ = std::make_unique<mr::TempoMap> (composition_->tempo);
    meters_ = composition_->meters();
    warnings_ = std::move (warnings);

    summaries_.clear();
    for (size_t i = 0; i < composition_->tracks.size(); ++i)
    {
        const auto& t = composition_->tracks[i];
        TrackSummary s;
        s.colour = t.colour.empty() ? trackColourFor (i) : juce::Colour::fromString ("ff" + juce::String (t.colour.substr (1)));
        s.notes = t.notes.size();
        if (! t.notes.empty())
        {
            s.lowPitch = 127;
            s.highPitch = 0;
            for (const auto& n : t.notes)
            {
                s.lowPitch = std::min (s.lowPitch, n.pitch);
                s.highPitch = std::max (s.highPitch, n.pitch);
            }
        }
        for (const auto& [id, lane] : t.automation)
            if (! lane.empty())
                s.lanes.push_back (id);
        summaries_.push_back (s);
    }

    lengthSeconds_ = mr::estimateDurationSeconds (*composition_);
    lengthBeats_ = tempo_->beatAt (lengthSeconds_);
}

juce::String Project::describeErrors (const mr::ParseResult& result)
{
    juce::String text;
    for (const auto& e : result.diagnostics.errors)
        text << "\n" << juce::String::fromUTF8 (e.path.c_str()) << ": " << juce::String::fromUTF8 (e.message.c_str());
    return text;
}

std::vector<mr::EditNote> Project::editNotes (size_t track) const
{
    return mr::editNotes (document_->track (track), composition_->tracks.at (track));
}

bool Project::editTrack (size_t track, const juce::var& after, const juce::String& name, juce::String& error)
{
    if (! loaded() || track >= trackCount())
        return false;
    return edit ({ Change { (int) track, {}, after } }, name, error);
}

bool Project::editTempo (const juce::var& tempo, const juce::var& timeSignature, const juce::String& name, juce::String& error)
{
    if (! loaded())
        return false;
    return edit ({ Change { -1, "tempo", tempo }, Change { -1, "timeSignature", timeSignature } }, name, error);
}

std::optional<std::pair<double, double>> Project::loopRange() const
{
    if (! loaded() || ! composition_->loop)
        return std::nullopt;
    return std::make_pair (composition_->loop->startBeat, composition_->loop->endBeat);
}

bool Project::editLoop (const std::optional<std::pair<double, double>>& range, juce::String& error)
{
    if (! loaded())
        return false;
    auto metadata = document_->property ("metadata");
    const bool hadMetadata = metadata.isObject();
    if (! hadMetadata)
        metadata = juce::var (new juce::DynamicObject());
    auto* obj = metadata.getDynamicObject();
    if (range && range->second > range->first && range->first >= 0.0)
    {
        auto* loop = new juce::DynamicObject();
        loop->setProperty ("startBeat", range->first);
        loop->setProperty ("endBeat", range->second);
        obj->setProperty ("loop", juce::var (loop));
    }
    else
    {
        if (! obj->hasProperty ("loop"))
            return false;   // 바뀌는 것이 없다
        obj->removeProperty ("loop");
    }
    juce::var value = metadata;
    if (! hadMetadata && obj->getProperties().isEmpty())
        value = juce::var();
    return edit ({ Change { -1, "metadata", value } }, juce::String::fromUTF8 ("반복 구간"), error);
}

bool Project::editRoot (const juce::var& after, const juce::String& name, juce::String& error)
{
    if (! loaded())
        return false;
    std::vector<Change> changes;
    for (const auto& key : mr::changedProperties (document_->root(), after))
        changes.push_back ({ -1, key, after.getProperty (key, juce::var()).clone() });
    if (changes.empty())
        return false;
    return edit (changes, name, error);
}

Project::Change Project::currentPart (const Change& like) const
{
    if (like.track >= 0)
        return { like.track, {}, document_->track ((size_t) like.track) };
    return { -1, like.property, document_->property (like.property) };
}

void Project::setPart (const Change& change)
{
    if (change.track >= 0)
        document_->setTrack ((size_t) change.track, change.value.clone());
    else
        document_->setProperty (change.property, change.value.clone());
}

bool Project::edit (const std::vector<Change>& changes, const juce::String& name, juce::String& error)
{
    if (! loaded() || changes.empty())
        return false;
    std::vector<Change> before;
    for (const auto& c : changes)
    {
        if (c.track >= (int) trackCount())
            return false;
        before.push_back (currentPart (c));
    }
    for (const auto& c : changes)
        setPart (c);
    auto result = document_->parse();
    juce::String problem;
    if (! result.composition)
        problem = juce::String::fromUTF8 ("편집한 내용이 프로젝트 형식에 맞지 않습니다.") + describeErrors (result);
    else if (checkTrack)
        for (const auto& c : changes)
            if (c.track >= 0 && problem.isEmpty())
                problem = checkTrack ((size_t) c.track, result.composition->tracks[(size_t) c.track]);
    if (problem.isNotEmpty())
    {
        for (auto it = before.rbegin(); it != before.rend(); ++it)
            setPart (*it);
        error = problem;
        return false;
    }
    std::vector<Change> after;
    for (const auto& c : changes)
        after.push_back ({ c.track, c.property, c.value.clone() });
    parsedAfter_ = std::move (result);
    undo_.beginNewTransaction (name);
    const juce::ScopedValueSetter<bool> fresh (performingNew_, true);
    undo_.perform (new ReplaceAction (*this, std::move (before), std::move (after)));
    return true;
}

bool Project::apply (const std::vector<Change>& changes, int step)
{
    if (! document_)
        return false;
    for (const auto& c : changes)
        setPart (c);
    if (batching_)
    {
        depth_ += step;
        return true;
    }
    auto result = parsedAfter_ ? std::move (*parsedAfter_) : document_->parse();
    parsedAfter_.reset();
    if (! result.composition)
        return false;

    if (performingNew_ && depth_ < savedDepth_)
        savedDepth_ = -1;   // 저장한 상태는 버린 다시 실행 기록 안에 있었다
    depth_ += step;

    // 음이 바뀐 트랙이 있는가 (트랙 수가 같을 때만 비교한다)
    lastEditChangedNotes_ = result.composition->tracks.size() != composition_->tracks.size();
    for (size_t i = 0; ! lastEditChangedNotes_ && i < composition_->tracks.size(); ++i)
    {
        const auto& a = composition_->tracks[i].notes;
        const auto& b = result.composition->tracks[i].notes;
        lastEditChangedNotes_ = ! std::equal (a.begin(), a.end(), b.begin(), b.end(), [] (const mr::Note& x, const mr::Note& y)
        {
            return x.pitch == y.pitch && x.startBeat == y.startBeat && x.durationBeat == y.durationBeat && x.velocity == y.velocity;
        });
    }
    adopt (std::move (*result.composition), result.diagnostics.warnings);
    int changed = -1;
    if (changes.size() == 1 && changes[0].track >= 0)
        changed = changes[0].track;
    listeners_.call ([changed] (Listener& l) { l.projectEdited (changed); });
    return true;
}

bool Project::jumpHistory (int steps)
{
    if (! loaded() || steps == 0)
        return false;
    int moved = 0;
    {
        const juce::ScopedValueSetter<bool> batch (batching_, true);
        for (; steps < 0 && undo_.canUndo() && undo_.undo(); ++steps)
            ++moved;
        for (; steps > 0 && undo_.canRedo() && undo_.redo(); --steps)
            ++moved;
    }
    if (moved == 0)
        return false;
    auto result = document_->parse();
    if (! result.composition)
        return false;
    lastEditChangedNotes_ = true;
    adopt (std::move (*result.composition), result.diagnostics.warnings);
    listeners_.call ([] (Listener& l) { l.projectEdited (-1); });
    return true;
}

const mr::MixerSettings& Project::mixerOf (ChannelRef c) const
{
    switch (c.kind)
    {
        case ChannelRef::Kind::track: return composition_->tracks.at (c.index).mixer;
        case ChannelRef::Kind::bus: return composition_->buses.at (c.index).mixer;
        case ChannelRef::Kind::master: break;
    }
    return composition_->master.mixer;
}

const std::vector<mr::EffectSpec>& Project::effectsOf (ChannelRef c) const
{
    switch (c.kind)
    {
        case ChannelRef::Kind::track: return composition_->tracks.at (c.index).effects;
        case ChannelRef::Kind::bus: return composition_->buses.at (c.index).effects;
        case ChannelRef::Kind::master: break;
    }
    return composition_->master.effects;
}

juce::String Project::channelName (ChannelRef c) const
{
    switch (c.kind)
    {
        case ChannelRef::Kind::track: return juce::String::fromUTF8 (composition_->tracks.at (c.index).name.c_str());
        case ChannelRef::Kind::bus: return juce::String::fromUTF8 (composition_->buses.at (c.index).name.c_str());
        case ChannelRef::Kind::master: break;
    }
    return juce::String::fromUTF8 ("마스터");
}

juce::var Project::channelJson (ChannelRef c) const
{
    switch (c.kind)
    {
        case ChannelRef::Kind::track: return document_->track (c.index);
        case ChannelRef::Kind::bus: return document_->property ("buses")[(int) c.index];
        case ChannelRef::Kind::master: break;
    }
    return document_->property ("master");
}

Project::Change Project::channelChange (ChannelRef c, const juce::var& json) const
{
    switch (c.kind)
    {
        case ChannelRef::Kind::track: return { (int) c.index, {}, json };
        case ChannelRef::Kind::bus:
        {
            auto buses = document_->property ("buses");
            if (auto* arr = buses.getArray(); arr != nullptr && (int) c.index < arr->size())
                arr->set ((int) c.index, json);
            return { -1, "buses", buses };
        }
        case ChannelRef::Kind::master: break;
    }
    return { -1, "master", json };
}

bool Project::editChannel (ChannelRef c, const juce::var& json, const juce::String& name, juce::String& error)
{
    if (! loaded())
        return false;
    return edit ({ channelChange (c, json) }, name, error);
}

bool Project::save (const juce::File& file, juce::String& error)
{
    if (! document_)
        return false;
    std::string saveError;
    if (! document_->save (file, saveError))
    {
        error = juce::String::fromUTF8 (saveError.c_str());
        return false;
    }
    file_ = file;
    modified_ = file.getLastModificationTime();
    savedDepth_ = depth_;
    return true;
}

juce::Colour Project::trackColourFor (size_t index)
{
    const float hue = std::fmod (0.58f + (float) index * 0.381966f, 1.0f);
    return juce::Colour::fromHSV (hue, 0.52f, 0.92f, 1.0f);
}

void Project::clear()
{
    undo_.clearUndoHistory();
    depth_ = savedDepth_ = 0;
    document_.reset();
    composition_.reset();
    tempo_.reset();
    summaries_.clear();
    warnings_.clear();
    file_ = juce::File();
    lengthSeconds_ = lengthBeats_ = 0.0;
}

juce::String Project::title() const
{
    if (! loaded())
        return {};
    if (! composition_->title.empty())
        return juce::String::fromUTF8 (composition_->title.c_str());
    return file_.getFileNameWithoutExtension();
}

void TimelineState::setLengthBeats (double beats)
{
    lengthBeats_ = std::max (4.0, beats);
    notify();
}

void TimelineState::setView (double startBeat, double ppb)
{
    ppb = std::clamp (ppb, 1.5, 400.0);
    startBeat = std::clamp (startBeat, 0.0, std::max (0.0, lengthBeats_));
    if (startBeat == viewStartBeat_ && ppb == pixelsPerBeat_)
        return;
    viewStartBeat_ = startBeat;
    pixelsPerBeat_ = ppb;
    notify();
}

void TimelineState::zoomAround (double factor, double anchorBeat, double anchorOffsetPixels)
{
    const double ppb = std::clamp (pixelsPerBeat_ * factor, 1.5, 400.0);
    setView (anchorBeat - anchorOffsetPixels / ppb, ppb);
}

void TimelineState::setSelectedTrack (int index)
{
    if (index == selectedTrack_)
        return;
    selectedTrack_ = index;
    notify();
}

void TimelineState::setPlayheadBeat (double beat)
{
    if (std::abs (beat - playheadBeat_) < 1.0e-9)
        return;
    playheadBeat_ = beat;
    notify();
}

void TimelineState::setLoopRange (const std::optional<std::pair<double, double>>& range)
{
    if (range == loopRange_)
        return;
    loopRange_ = range;
    notify();
}

void TimelineState::setLoopOn (bool on)
{
    if (on == loopOn_)
        return;
    loopOn_ = on;
    notify();
}

juce::String controllerLabel (int id)
{
    switch (id)
    {
        case 1: return juce::String::fromUTF8 ("다이내믹스 (CC1)");
        case 7: return juce::String::fromUTF8 ("볼륨 (CC7)");
        case 10: return juce::String::fromUTF8 ("팬 (CC10)");
        case 11: return juce::String::fromUTF8 ("익스프레션 (CC11)");
        case 64: return juce::String::fromUTF8 ("서스테인 페달 (CC64)");
        case 74: return juce::String::fromUTF8 ("필터 (CC74)");
        case 102: return juce::String::fromUTF8 ("공격 시간 (CC102)");
        case mr::kPitchBend: return juce::String::fromUTF8 ("피치 벤드");
        case mr::kAftertouch: return juce::String::fromUTF8 ("애프터터치");
        default: return "CC" + juce::String (id);
    }
}

} // namespace daw
