// 피아노 롤의 오디오 트랙: 녹음의 음 덩어리를 높이 줄에 그리고, 음마다 높이, 흔들림 펴기, 비브라토, 시작과 끝(워프 마커)을 고친다.
#include "PianoRollPanel.h"

#include "Theme.h"
#include "TimelineDrawing.h"
#include "composition/Document.h"
#include "instruments/AudioClips.h"

#include <algorithm>
#include <cmath>

namespace daw
{

namespace
{
const char* const pitchNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

juce::String pitchName (int pitch) { return juce::String (pitchNames[((pitch % 12) + 12) % 12]) + juce::String (pitch / 12 - 1); }

// 들리는 가운데 높이: 음 이름과 센트 ("E4 +12")
juce::String heardName (double midi)
{
    const int p = (int) std::lround (midi);
    const int cents = (int) std::lround (100.0 * (midi - p));
    return pitchName (p) + (cents == 0 ? juce::String() : juce::String (cents > 0 ? " +" : " ") + juce::String (cents));
}

juce::String percent (double v) { return juce::String ((int) std::lround (v * 100.0)) + "%"; }

bool identity (const mr::PitchEdit& e) { return std::abs (e.semitones) < 1.0e-6 && e.flatten < 1.0e-6 && std::abs (e.vibrato - 1.0) < 1.0e-6; }

bool overlaps (const mr::PitchEdit& e, const mr::NoteSegment& s) { return e.seconds < s.endSeconds && e.endSeconds() > s.startSeconds; }

constexpr float edgeGrab = 5.0f;       // 막대 양 끝을 잡는 너비 (픽셀)
constexpr float handleRadius = 5.0f;   // 손잡이의 반지름
constexpr float handleRise = 10.0f;    // 손잡이가 막대 위로 떨어진 거리
constexpr float handleTravel = 80.0f;  // 손잡이를 이만큼 끌면 값이 1 바뀐다
} // namespace

juce::File PianoRollPanel::songFolder() const { return project_.file().getParentDirectory(); }

std::shared_ptr<const std::vector<mr::NoteSegment>> PianoRollPanel::notesOfClip (const mr::AudioClip& clip) const
{
    // 뒤집은 클립은 거꾸로 돌린 사본의 음 (사본은 뒤 스레드에서 쓴다)
    const auto original = mr::clipFile (clip, songFolder());
    const auto file = clip.reverse ? mr::reversedCopyOf (original) : original;
    if (auto cached = mr::cachedFileNoteSegments (file))
        return cached;
    const auto key = file.getFullPathName();
    if (measuring_.insert (key).second)
    {
        juce::Component::SafePointer<PianoRollPanel> safe (const_cast<PianoRollPanel*> (this));
        measurePool_->addJob ([clip, folder = songFolder(), key, safe]
        {
            mr::fileNoteSegments (mr::clipSourceFile (clip, folder));
            juce::MessageManager::callAsync ([safe, key]
            {
                if (auto* self = safe.getComponent())
                {
                    self->measuring_.erase (key);
                    if (! self->audioCentred_)
                        self->centreOnAudioNotes();
                    self->repaint();
                }
            });
        });
    }
    return nullptr;
}

void PianoRollPanel::measureAudioNotes()
{
    const int index = track();
    if (index < 0 || ! audioTrack())
        return;
    for (const auto& clip : project_.composition().tracks[(size_t) index].clips)
        mr::fileNoteSegments (mr::clipSourceFile (clip, songFolder()));
    centreOnAudioNotes();
    repaint();
}

std::vector<PianoRollPanel::AudioNote> PianoRollPanel::audioNotes() const
{
    std::vector<AudioNote> out;
    const int index = track();
    if (index < 0 || ! audioTrack())
        return out;
    const auto& clips = project_.composition().tracks[(size_t) index].clips;
    const auto folder = songFolder();
    const auto& tempo = project_.tempo();
    for (size_t c = 0; c < clips.size(); ++c)
    {
        const auto& clip = clips[c];
        if (clip.muted)
            continue;   // 고르지 않은 테이크
        const auto all = notesOfClip (clip);
        if (all == nullptr)
            continue;
        const double from = clip.offsetSeconds, to = from + mr::clipLengthSeconds (clip, folder);
        const auto [first, last] = mr::segmentRange (*all, from, to);
        const double clipEnd = tempo.beatAt (mr::clipSongSecondsAt (clip, tempo, to));
        for (size_t k = first; k < last; ++k)
        {
            const auto& s = (*all)[k];
            AudioNote n;
            n.ref = { c, k - first };
            n.owner = all;
            n.segment = &s;
            for (const auto& e : clip.pitchEdits)
                if (overlaps (e, s))
                {
                    n.edit = e;
                    break;
                }
            n.startBeat = std::max (clip.startBeat, tempo.beatAt (mr::clipSongSecondsAt (clip, tempo, std::max (from, s.startSeconds))));
            n.endBeat = std::min (clipEnd, tempo.beatAt (mr::clipSongSecondsAt (clip, tempo, std::min (to, s.endSeconds))));
            n.heard = s.midi + clip.pitchSemitones + (n.edit ? n.edit->semitones : 0.0);
            out.push_back (std::move (n));
        }
    }
    return out;
}

std::optional<PianoRollPanel::AudioNote> PianoRollPanel::findAudioNote (AudioNoteRef ref) const
{
    for (auto& n : audioNotes())
        if (n.ref == ref)
            return n;
    return std::nullopt;
}

size_t PianoRollPanel::audioNoteCount (size_t clip) const
{
    size_t count = 0;
    for (const auto& n : audioNotes())
        count += n.ref.clip == clip ? 1 : 0;
    return count;
}

const mr::NoteSegment* PianoRollPanel::audioNoteSegment (AudioNoteRef ref) const
{
    const auto n = findAudioNote (ref);
    return n ? n->segment : nullptr;   // 파일의 음 목록은 캐시가 들고 있다
}

std::optional<mr::PitchEdit> PianoRollPanel::audioNoteEdit (AudioNoteRef ref) const
{
    const auto n = findAudioNote (ref);
    return n ? n->edit : std::nullopt;
}

void PianoRollPanel::selectAudioNote (std::optional<AudioNoteRef> ref)
{
    audioNote_ = ref;
    repaint();
}

mr::PitchEdit PianoRollPanel::editOrDefault (const AudioNote& n) const
{
    if (n.edit)
        return *n.edit;
    mr::PitchEdit e;
    e.seconds = n.segment->startSeconds;
    e.lengthSeconds = n.segment->endSeconds - n.segment->startSeconds;
    return e;
}

float PianoRollPanel::centreY (double midi) const
{
    return (float) gridArea().getY() + (float) (topPitch_ - midi + 0.5) * keyHeight_;
}

juce::Rectangle<float> PianoRollPanel::audioNoteRect (const AudioNote& source, const TimelineGeometry& geo) const
{
    auto n = source;
    // 끄는 동안의 미리보기
    if (audioNote_ && *audioNote_ == n.ref && dragMoved_)
    {
        if (drag_ == Drag::audioPitch)
            n.heard += audioPreview_.semitones - (n.edit ? n.edit->semitones : 0.0);
        else if (drag_ == Drag::audioStart)
            n.startBeat = edgePreview_;
        else if (drag_ == Drag::audioEnd)
            n.endBeat = edgePreview_;
    }
    const float x0 = geo.x (n.startBeat), x1 = geo.x (n.endBeat);
    const float h = std::max (4.0f, keyHeight_ - 2.0f);
    return { x0, centreY (n.heard) - 0.5f * h, std::max (3.0f, x1 - x0), h };
}

std::optional<PianoRollPanel::AudioNoteRef> PianoRollPanel::audioNoteAt (juce::Point<float> p, int& edge) const
{
    edge = 0;
    const TimelineGeometry geo (timeline_, leftWidth, gridArea().getRight());
    const auto notes = audioNotes();
    for (auto it = notes.rbegin(); it != notes.rend(); ++it)
    {
        const auto r = audioNoteRect (*it, geo).expanded (1.0f, 2.0f);
        if (! r.contains (p))
            continue;
        const bool wide = r.getWidth() > 3.0f * edgeGrab;
        edge = wide && p.x - r.getX() <= edgeGrab ? -1 : wide && r.getRight() - p.x <= edgeGrab ? 1 : 0;
        return it->ref;
    }
    return std::nullopt;
}

juce::Point<float> PianoRollPanel::audioNotePoint (AudioNoteRef ref, float xFraction) const
{
    const auto n = findAudioNote (ref);
    if (! n)
        return {};
    const TimelineGeometry geo (timeline_, leftWidth, gridArea().getRight());
    const auto r = audioNoteRect (*n, geo);
    return { juce::jlimit (r.getX() + 1.0f, r.getRight() - 1.0f, r.getX() + xFraction * r.getWidth()), r.getCentreY() };
}

juce::Point<float> PianoRollPanel::audioNoteHandle (AudioNoteRef ref, bool vibrato) const
{
    const auto n = findAudioNote (ref);
    if (! n)
        return {};
    const TimelineGeometry geo (timeline_, leftWidth, gridArea().getRight());
    const auto r = audioNoteRect (*n, geo);
    return { r.getCentreX() + (vibrato ? 1.0f : -1.0f) * (handleRadius * 2.0f), r.getY() - handleRise };
}

int PianoRollPanel::audioHandleAt (juce::Point<float> p) const
{
    if (! audioNote_ || ! findAudioNote (*audioNote_))
        return -1;
    for (int k = 0; k < 2; ++k)
        if (audioNoteHandle (*audioNote_, k == 1).getDistanceFrom (p) <= handleRadius + 2.0f)
            return k;
    return -1;
}

bool PianoRollPanel::commitClip (size_t clip, const mr::AudioClip& edited, const juce::String& name)
{
    const int index = track();
    if (index < 0)
        return false;
    juce::String error;
    committing_ = true;
    const bool ok = project_.editTrack ((size_t) index, mr::withClip (project_.trackJson ((size_t) index), clip, edited), name, error);
    committing_ = false;
    if (! ok)
        status (error.replaceCharacter ('\n', ' '), true);
    repaint();
    return ok;
}

bool PianoRollPanel::setAudioNoteEdit (AudioNoteRef ref, std::optional<mr::PitchEdit> edit, const juce::String& name)
{
    const auto n = findAudioNote (ref);
    if (! n)
        return false;
    if (! mr::clipStretchAvailable())
    {
        status (juce::String::fromUTF8 ("이 빌드에는 늘이기와 음높이 바꾸기가 없어 음 높이를 고치지 못합니다."), true);
        return false;
    }
    auto clip = project_.composition().tracks[(size_t) track()].clips[ref.clip];
    if (clip.warpSlices)
    {
        status (juce::String::fromUTF8 ("자르기 방식의 클립은 파일의 높이로 틉니다. 워프 방식을 늘이기로 바꾸면 음 높이를 고칩니다."), true);
        return false;
    }
    const auto& s = *n->segment;
    std::vector<mr::PitchEdit> kept;
    for (const auto& e : clip.pitchEdits)
        if (! overlaps (e, s))
            kept.push_back (e);
    if (edit && ! identity (*edit))
    {
        auto e = *edit;
        e.seconds = s.startSeconds;
        e.lengthSeconds = s.endSeconds - s.startSeconds;
        e.semitones = juce::jlimit (-24.0, 24.0, e.semitones);
        e.flatten = juce::jlimit (0.0, 1.0, e.flatten);
        e.vibrato = juce::jlimit (0.0, 2.0, e.vibrato);
        kept.push_back (e);
        std::sort (kept.begin(), kept.end(), [] (const mr::PitchEdit& a, const mr::PitchEdit& b) { return a.seconds < b.seconds; });
    }
    if (kept == clip.pitchEdits)
        return false;
    clip.pitchEdits = kept;
    if (! commitClip (ref.clip, clip, name))
        return false;
    audioNote_ = ref;
    if (const auto after = findAudioNote (ref))
        status (juce::String::fromUTF8 ("음 ") + juce::String ((int) ref.note + 1) + ": " + heardName (after->heard));
    return true;
}

bool PianoRollPanel::shiftAudioNote (AudioNoteRef ref, int semitones)
{
    const auto n = findAudioNote (ref);
    if (! n || semitones == 0)
        return false;
    auto e = editOrDefault (*n);
    e.semitones += semitones;
    return setAudioNoteEdit (ref, e, juce::String::fromUTF8 ("음 높이"));
}

bool PianoRollPanel::snapAudioNote (AudioNoteRef ref)
{
    const auto n = findAudioNote (ref);
    if (! n)
        return false;
    auto e = editOrDefault (*n);
    const double base = n->heard - e.semitones;
    e.semitones = std::round (n->heard) - base;
    return setAudioNoteEdit (ref, e, juce::String::fromUTF8 ("음 높이를 반음에"));
}

bool PianoRollPanel::moveAudioNoteEdge (AudioNoteRef ref, bool end, double songBeat)
{
    const auto n = findAudioNote (ref);
    if (! n)
        return false;
    auto clip = project_.composition().tracks[(size_t) track()].clips[ref.clip];
    const auto& tempo = project_.tempo();
    const double length = mr::clipLengthSeconds (clip, songFolder());
    const double from = clip.offsetSeconds, to = from + length;
    const double a = juce::jlimit (from, to, n->segment->startSeconds), b = juce::jlimit (from, to, n->segment->endSeconds);
    auto markerNear = [] (const mr::AudioClip& c, double seconds)
    {
        for (size_t i = 0; i < c.warp.size(); ++i)
            if (std::abs (c.warp[i].seconds - seconds) < 1.0e-3)
                return (int) i;
        return -1;
    };
    // 음의 양 끝과 이웃 음에 마커를 두어 끈 경계의 둘레만 늘고 준다. 이웃 음과 50 ms 넘게 떨어졌으면 그 가까운 끝(틈이 늘고 준다),
    // 이어 부른 음이면 그 먼 끝(이웃 음이 늘고 준다). 처음 넣는 마커는 클립의 양 끝도 함께 넣는다
    std::vector<double> anchors { a > from + 1.0e-3 ? a : b, a, b };
    for (const auto& other : audioNotes())
        if (other.ref.clip == ref.clip && (other.ref.note + 1 == ref.note || other.ref.note == ref.note + 1))
        {
            const bool before = other.ref.note < ref.note;
            const bool apart = before ? n->segment->startSeconds - other.segment->endSeconds > 0.05 : other.segment->startSeconds - n->segment->endSeconds > 0.05;
            const double seconds = before == apart ? other.segment->endSeconds : other.segment->startSeconds;
            if (seconds > from + 1.0e-3 && seconds < to - 1.0e-3 && std::abs (seconds - a) > 1.0e-3 && std::abs (seconds - b) > 1.0e-3)
                anchors.push_back (seconds);
        }
    for (const double seconds : anchors)
        if (markerNear (clip, seconds) < 0)
            clip = mr::withWarpMarkerAdded (clip, tempo, length, seconds);
    const int index = markerNear (clip, end ? b : a);
    if (index < 0)
        return false;
    const double local = songBeat - clip.startBeat + mr::warpBeatAt (clip, clip.offsetSeconds);
    clip = mr::withWarpMarkerMoved (clip, (size_t) index, local);
    return commitClip (ref.clip, clip, juce::String::fromUTF8 (end ? "음 끝 옮기기" : "음 시작 옮기기"));
}

void PianoRollPanel::centreOnAudioNotes()
{
    const auto notes = audioNotes();
    double low = 60.0, high = 60.0;
    if (! notes.empty())
    {
        low = high = notes.front().heard;
        for (const auto& n : notes)
        {
            low = std::min (low, n.heard);
            high = std::max (high, n.heard);
        }
    }
    audioCentred_ = ! notes.empty();
    const double visibleKeys = std::max (1.0, gridArea().getHeight() / (double) keyHeight_);
    topPitch_ = juce::jlimit (visibleKeys - 1.0, 127.0, 0.5 * (low + high) + visibleKeys * 0.5);
    updateScrollBars();
}

void PianoRollPanel::paintAudioNotes (juce::Graphics& g, const TimelineGeometry& geo)
{
    const int index = track();
    const auto grid = gridArea();
    const auto colour = project_.summary ((size_t) index).colour;
    const auto& clips = project_.composition().tracks[(size_t) index].clips;
    const auto& tempo = project_.tempo();
    const auto folder = songFolder();
    // 클립이 트는 구간
    for (const auto& clip : clips)
    {
        if (clip.muted)
            continue;
        const float x0 = geo.x (clip.startBeat), x1 = geo.x (project_.beatAtSeconds (mr::clipEndSeconds (clip, tempo, folder)));
        g.setColour (colour.withAlpha (0.05f));
        g.fillRect (juce::Rectangle<float> (x0, (float) grid.getY(), x1 - x0, (float) grid.getHeight()));
        g.setColour (colour.withAlpha (0.35f));
        g.fillRect (x0, (float) grid.getY(), 1.0f, (float) grid.getHeight());
        g.fillRect (x1 - 1.0f, (float) grid.getY(), 1.0f, (float) grid.getHeight());
    }
    const auto notes = audioNotes();
    if (notes.empty())
    {
        juce::String text;
        if (clips.empty())
            text = juce::String::fromUTF8 ("오디오 트랙에 클립이 없습니다. 오디오 파일을 트랙 보기에 끌어 놓으면 클립으로 들어가고, 녹음의 음이 여기에 나옵니다.");
        else if (! measuring_.empty())
            text = juce::String::fromUTF8 ("녹음의 음을 재는 중입니다.");
        else
            text = juce::String::fromUTF8 ("클립에서 음을 찾지 못했습니다. 노래나 독주 악기처럼 한 번에 한 음이 울리는 녹음의 음을 보입니다.");
        g.setColour (colours::textDim);
        g.setFont (uiFont (14.0f));
        g.drawFittedText (text, grid.reduced (40, 20), juce::Justification::centred, 3);
        return;
    }
    const juce::Font label = uiFont (std::min (10.5f, keyHeight_ - 1.0f));
    for (const auto& n : notes)
    {
        const auto r = audioNoteRect (n, geo);
        if (r.getRight() < (float) grid.getX() || r.getX() > (float) grid.getRight())
            continue;
        const bool selected = audioNote_ && *audioNote_ == n.ref;
        g.setColour (colour.withAlpha (selected ? 0.5f : (n.edit ? 0.36f : 0.24f)));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (selected ? colours::selection : colour.brighter (n.edit ? 0.5f : 0.15f));
        g.drawRoundedRectangle (r, 3.0f, selected ? 1.6f : 1.0f);

        // 높이 곡선 (편집을 입힌 소리, 끄는 동안은 미리보기)
        const auto& s = *n.segment;
        const auto& clip = clips[n.ref.clip];
        std::optional<mr::PitchEdit> shown = n.edit;
        if (selected && dragMoved_ && (drag_ == Drag::audioPitch || drag_ == Drag::audioFlatten || drag_ == Drag::audioVibrato))
            shown = audioPreview_;
        const auto curve = shown ? mr::editedPitchCurve (s, *shown) : s.curve;
        const double from = clip.offsetSeconds, to = from + mr::clipLengthSeconds (clip, folder);
        juce::Path path;
        bool open = false;
        const bool stretchedEdge = selected && dragMoved_ && (drag_ == Drag::audioStart || drag_ == Drag::audioEnd);
        for (size_t i = 0; i < curve.size(); ++i)
        {
            const double t = s.curveStart + (double) i * s.hopSeconds;
            if (curve[i] <= 0.0f || t < from || t > to)
            {
                open = false;
                continue;
            }
            float x = geo.x (tempo.beatAt (mr::clipSongSecondsAt (clip, tempo, t)));
            if (stretchedEdge)   // 끄는 경계로 막대 안의 곡선을 늘인다
            {
                const float b0 = geo.x (n.startBeat), b1 = geo.x (n.endBeat);
                if (b1 - b0 > 1.0f)
                    x = r.getX() + (x - b0) / (b1 - b0) * r.getWidth();
            }
            const float y = centreY (curve[i] + clip.pitchSemitones);
            if (open)
                path.lineTo (x, y);
            else
                path.startNewSubPath (x, y);
            open = true;
        }
        g.setColour (colours::text.withAlpha (selected ? 0.95f : 0.7f));
        g.strokePath (path, juce::PathStrokeType (selected ? 1.4f : 1.1f));

        if (r.getWidth() > 40.0f && keyHeight_ >= 9.0f)
        {
            double heard = n.heard;
            if (selected && dragMoved_ && drag_ == Drag::audioPitch)
                heard += audioPreview_.semitones - (n.edit ? n.edit->semitones : 0.0);
            g.setFont (label);
            g.setColour (colours::text.withAlpha (0.9f));
            g.drawText (heardName (heard), r.withTrimmedLeft (4.0f).withY (r.getY() - label.getHeight() - 1.0f).withHeight (label.getHeight() + 1.0f).toNearestInt(),
                        juce::Justification::centredLeft, false);
        }
        if (selected)
        {
            // 손잡이: 펴기 (왼쪽), 비브라토 (오른쪽). 끄는 동안 값
            const auto e = (dragMoved_ && (drag_ == Drag::audioFlatten || drag_ == Drag::audioVibrato)) ? audioPreview_ : editOrDefault (n);
            for (int k = 0; k < 2; ++k)
            {
                const auto c = audioNoteHandle (n.ref, k == 1);
                const double value = k == 0 ? e.flatten : e.vibrato / 2.0;
                g.setColour (colours::panelRaised);
                g.fillEllipse (c.x - handleRadius, c.y - handleRadius, 2.0f * handleRadius, 2.0f * handleRadius);
                g.setColour (k == 0 ? colours::accent : colours::marker);
                juce::Path arc;
                arc.addPieSegment (c.x - handleRadius, c.y - handleRadius, 2.0f * handleRadius, 2.0f * handleRadius, 0.0f,
                                   (float) (juce::MathConstants<double>::twoPi * juce::jlimit (0.0, 1.0, value)), 0.0f);
                g.fillPath (arc);
                g.drawEllipse (c.x - handleRadius, c.y - handleRadius, 2.0f * handleRadius, 2.0f * handleRadius, 1.0f);
            }
            if (dragMoved_ && (drag_ == Drag::audioFlatten || drag_ == Drag::audioVibrato))
            {
                const bool vib = drag_ == Drag::audioVibrato;
                const auto c = audioNoteHandle (n.ref, vib);
                g.setColour (colours::text);
                g.setFont (uiFont (11.0f));
                g.drawText (juce::String::fromUTF8 (vib ? "비브라토 " : "흔들림 펴기 ") + percent (vib ? e.vibrato : e.flatten),
                            juce::Rectangle<float> (c.x + 8.0f, c.y - 8.0f, 120.0f, 16.0f).toNearestInt(), juce::Justification::centredLeft, false);
            }
        }
    }
    // 끄는 경계
    if (audioNote_ && dragMoved_ && (drag_ == Drag::audioStart || drag_ == Drag::audioEnd))
    {
        g.setColour (colours::playhead.withAlpha (0.8f));
        g.fillRect (geo.x (edgePreview_) - 0.5f, (float) grid.getY(), 1.0f, (float) grid.getHeight());
    }
}

void PianoRollPanel::paintAudioInfo (juce::Graphics& g, juce::Rectangle<int>& r)
{
    auto line = [&] (const juce::String& label, const juce::String& value)
    {
        auto row = r.removeFromTop (18);
        g.setColour (colours::textFaint);
        g.drawText (label, row.removeFromLeft (52), juce::Justification::centredLeft);
        g.setColour (colours::textDim);
        g.drawText (value, row, juce::Justification::centredLeft, true);
    };
    const auto& track = project_.composition().tracks[(size_t) this->track()];
    line (juce::String::fromUTF8 ("클립"), juce::String ((int) track.clips.size()) + juce::String::fromUTF8 ("개"));
    const auto notes = audioNotes();
    size_t edited = 0;
    for (const auto& n : notes)
        edited += n.edit ? 1 : 0;
    line (juce::String::fromUTF8 ("음"), notes.empty() && ! measuring_.empty() ? juce::String::fromUTF8 ("재는 중")
                                                                                : juce::String ((int) notes.size()) + juce::String::fromUTF8 ("개 (고친 음 ")
                                                                                      + juce::String ((int) edited) + ")");
    if (! audioNote_)
        return;
    const auto n = findAudioNote (*audioNote_);
    if (! n)
        return;
    r.removeFromTop (6);
    const auto e = editOrDefault (*n);
    line (juce::String::fromUTF8 ("고른 음"), juce::String ((int) audioNote_->note + 1) + juce::String::fromUTF8 ("번째, ") + heardName (n->heard));
    line (juce::String::fromUTF8 ("원래"), heardName (n->segment->midi + track.clips[n->ref.clip].pitchSemitones));
    line (juce::String::fromUTF8 ("옮김"), juce::String (e.semitones, 2) + juce::String::fromUTF8 (" 반음"));
    line (juce::String::fromUTF8 ("펴기"), percent (e.flatten) + juce::String::fromUTF8 (" (흔들림 ") + juce::String ((int) std::lround (n->segment->driftCents)) + juce::String::fromUTF8 (" 센트)"));
    line (juce::String::fromUTF8 ("비브라토"), percent (e.vibrato) + " (" + juce::String ((int) std::lround (n->segment->vibratoCents)) + juce::String::fromUTF8 (" 센트)"));
}

void PianoRollPanel::showAudioNoteMenu (AudioNoteRef ref)
{
    const auto n = findAudioNote (ref);
    if (! n)
        return;
    const auto e = editOrDefault (*n);
    juce::PopupMenu m;
    m.addItem (juce::String::fromUTF8 ("높이 되돌리기"), n->edit.has_value(), false,
               [this, ref] { setAudioNoteEdit (ref, std::nullopt, juce::String::fromUTF8 ("음 높이 되돌리기")); });
    m.addItem (juce::String::fromUTF8 ("가운데 높이를 반음에 맞추기"), true, false, [this, ref] { snapAudioNote (ref); });
    m.addItem (juce::String::fromUTF8 ("반음 위로 (↑)"), true, false, [this, ref] { shiftAudioNote (ref, 1); });
    m.addItem (juce::String::fromUTF8 ("반음 아래로 (↓)"), true, false, [this, ref] { shiftAudioNote (ref, -1); });
    juce::PopupMenu flatten, vibrato;
    for (const int pct : { 0, 25, 50, 75, 100 })
        flatten.addItem (juce::String (pct) + "%", true, std::abs (e.flatten - pct / 100.0) < 0.005, [this, ref, pct]
        {
            if (const auto now = findAudioNote (ref))
            {
                auto x = editOrDefault (*now);
                x.flatten = pct / 100.0;
                setAudioNoteEdit (ref, x, juce::String::fromUTF8 ("흔들림 펴기"));
            }
        });
    for (const int pct : { 0, 50, 100, 150, 200 })
        vibrato.addItem (juce::String (pct) + "%", true, std::abs (e.vibrato - pct / 100.0) < 0.005, [this, ref, pct]
        {
            if (const auto now = findAudioNote (ref))
            {
                auto x = editOrDefault (*now);
                x.vibrato = pct / 100.0;
                setAudioNoteEdit (ref, x, juce::String::fromUTF8 ("비브라토"));
            }
        });
    m.addSubMenu (juce::String::fromUTF8 ("흔들림 펴기 (") + percent (e.flatten) + ")", flatten);
    m.addSubMenu (juce::String::fromUTF8 ("비브라토 (") + percent (e.vibrato) + ")", vibrato);
    m.addSeparator();
    const size_t clipIndex = ref.clip;
    const bool any = ! project_.composition().tracks[(size_t) track()].clips[clipIndex].pitchEdits.empty();
    m.addItem (juce::String::fromUTF8 ("이 클립의 높이 편집 모두 지우기"), any, false, [this, clipIndex]
    {
        auto clip = project_.composition().tracks[(size_t) track()].clips[clipIndex];
        clip.pitchEdits.clear();
        commitClip (clipIndex, clip, juce::String::fromUTF8 ("높이 편집 모두 지우기"));
    });
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void PianoRollPanel::audioMouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.position;
    dragMoved_ = false;
    if (! e.mods.isPopupMenu())
        if (const int handle = audioHandleAt (pos); handle >= 0)
        {
            audioPreview_ = editOrDefault (*findAudioNote (*audioNote_));
            drag_ = handle == 0 ? Drag::audioFlatten : Drag::audioVibrato;
            return;
        }
    int edge = 0;
    const auto hit = audioNoteAt (pos, edge);
    if (! hit)
    {
        if (audioNote_)
        {
            audioNote_.reset();
            repaint();
        }
        return;
    }
    audioNote_ = hit;
    repaint();
    if (e.mods.isPopupMenu())
    {
        showAudioNoteMenu (*hit);
        return;
    }
    const auto n = findAudioNote (*hit);
    audioPreview_ = editOrDefault (*n);
    edgePreview_ = edge < 0 ? n->startBeat : n->endBeat;
    drag_ = edge < 0 ? Drag::audioStart : edge > 0 ? Drag::audioEnd : Drag::audioPitch;
}

void PianoRollPanel::audioMouseDrag (const juce::MouseEvent& e)
{
    const auto n = audioNote_ ? findAudioNote (*audioNote_) : std::nullopt;
    if (! n)
        return;
    dragMoved_ = dragMoved_ || e.getDistanceFromDragStart() > 3;
    if (! dragMoved_)
        return;
    const auto& clip = project_.composition().tracks[(size_t) track()].clips[n->ref.clip];
    const auto start = editOrDefault (*n);
    const float dy = e.position.y - e.mouseDownPosition.y;
    switch (drag_)
    {
        case Drag::audioPitch:
        {
            const double base = n->segment->midi + clip.pitchSemitones;
            const double moved = start.semitones - dy / keyHeight_;
            audioPreview_.semitones = juce::jlimit (-24.0, 24.0, e.mods.isAltDown() ? std::round (moved * 100.0) / 100.0 : std::round (base + moved) - base);
            break;
        }
        case Drag::audioFlatten:
            audioPreview_.flatten = juce::jlimit (0.0, 1.0, std::round ((start.flatten - dy / handleTravel) * 100.0) / 100.0);
            break;
        case Drag::audioVibrato:
            audioPreview_.vibrato = juce::jlimit (0.0, 2.0, std::round ((start.vibrato - dy / handleTravel) * 100.0) / 100.0);
            break;
        case Drag::audioStart:
        case Drag::audioEnd:
        {
            const TimelineGeometry geo (timeline_, leftWidth, gridArea().getRight());
            const double raw = geo.beat (e.position.x);
            double beat = e.mods.isCommandDown() ? raw : snapRound (raw);
            const double gap = 1.0 / 64.0;
            beat = drag_ == Drag::audioStart ? std::min (beat, n->endBeat - gap) : std::max (beat, n->startBeat + gap);
            edgePreview_ = std::max (0.0, beat);
            break;
        }
        default:
            return;
    }
    repaint();
}

void PianoRollPanel::audioMouseUp (Drag drag)
{
    if (! dragMoved_ || ! audioNote_)
    {
        repaint();
        return;
    }
    const auto ref = *audioNote_;
    switch (drag)
    {
        case Drag::audioPitch: setAudioNoteEdit (ref, audioPreview_, juce::String::fromUTF8 ("음 높이")); break;
        case Drag::audioFlatten: setAudioNoteEdit (ref, audioPreview_, juce::String::fromUTF8 ("흔들림 펴기")); break;
        case Drag::audioVibrato: setAudioNoteEdit (ref, audioPreview_, juce::String::fromUTF8 ("비브라토")); break;
        case Drag::audioStart:
        case Drag::audioEnd: moveAudioNoteEdge (ref, drag == Drag::audioEnd, edgePreview_); break;
        default: break;
    }
    dragMoved_ = false;
    repaint();
}

} // namespace daw
