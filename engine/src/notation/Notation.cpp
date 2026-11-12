#include "notation/Notation.h"

#include "timing/TempoMap.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace mr
{

namespace
{

constexpr int Q = divisionsPerQuarter;

enum class Window { straight16, straight32, triplet };

struct QNote
{
    int pitch = 60;
    int start = 0, end = 0;   // divisions (곡 처음부터)
    int velocity = 0;
    std::string lyric;
};

struct MeasureFrame
{
    double startBeat = 0.0;
    int start = 0, length = 0;   // divisions
    int beatsPerBar = 4, beatUnit = 4;
};

struct Value
{
    int duration;
    const char* type;
    int dots;
    bool triplet;
};

// 긴 것부터
constexpr Value values[] = { { 144, "whole", 1, false }, { 96, "whole", 0, false }, { 72, "half", 1, false }, { 48, "half", 0, false },
                             { 36, "quarter", 1, false }, { 24, "quarter", 0, false }, { 18, "eighth", 1, false }, { 16, "quarter", 0, true },
                             { 12, "eighth", 0, false }, { 9, "16th", 1, false }, { 8, "eighth", 0, true }, { 6, "16th", 0, false },
                             { 4, "16th", 0, true }, { 3, "32nd", 0, false } };

int beatLength (int beatsPerBar, int beatUnit)
{
    if (beatUnit == 8 && beatsPerBar % 3 == 0)
        return 36;   // 3/8, 6/8, 9/8, 12/8 은 점4분음표가 박
    return std::max (3, 4 * Q / beatUnit);
}

double gridOf (Window w) { return w == Window::triplet ? 1.0 / 6.0 : w == Window::straight32 ? 0.125 : 0.25; }

double distanceToGrid (double offset, double grid) { return std::abs (offset - std::round (offset / grid) * grid); }

// GM 드럼 건반의 타악기 보표 자리 (5선의 글자와 옥타브)와 머리
struct DrumPlace
{
    int step, octave;
    const char* head;
};

DrumPlace drumPlace (int pitch)
{
    switch (pitch)
    {
        case 35: case 36: return { 3, 4, "" };          // 킥: F4
        case 37: return { 0, 5, "x" };                   // 사이드 스틱
        case 38: case 40: return { 0, 5, "" };           // 스네어: C5
        case 39: return { 0, 5, "x" };                   // 클랩
        case 41: return { 4, 4, "" };                    // 낮은 플로어 탐: G4
        case 43: return { 5, 4, "" };                    // 플로어 탐: A4
        case 45: case 47: return { 1, 5, "" };           // 낮은 탐: D5
        case 48: case 50: return { 2, 5, "" };           // 높은 탐: E5
        case 42: return { 4, 5, "x" };                   // 닫힌 하이햇: G5
        case 44: return { 2, 4, "x" };                   // 페달 하이햇: D4
        case 46: return { 4, 5, "circle-x" };            // 열린 하이햇
        case 49: case 57: return { 5, 5, "x" };          // 크래시: A5
        case 51: case 59: return { 3, 5, "x" };          // 라이드: F5
        case 53: return { 3, 5, "diamond" };             // 라이드 벨
        case 52: case 55: return { 6, 5, "x" };          // 차이나, 스플래시: B5
        default: return { 6, 4, "" };                    // 그 밖: B4
    }
}

} // namespace

bool isGrandStaffInstrument (const std::string& name)
{
    static const std::set<std::string> keys { "grand-piano", "piano", "harp", "celeste", "synth.piano", "synth.ep", "synth.poly-keys", "dexed" };
    return keys.count (name) > 0;
}

KeySignature scoreKey (const Composition& c, const std::vector<size_t>& tracks, bool* estimated)
{
    if (estimated != nullptr)
        *estimated = ! c.key.has_value();
    if (c.key)
        return *c.key;
    std::vector<double> weights (12, 0.0);
    double end = 0.0;
    for (const size_t t : tracks)
        if (t < c.tracks.size())
            for (const auto& n : c.tracks[t].notes)
            {
                weights[(size_t) (((n.pitch + c.tracks[t].transpose) % 12 + 12) % 12)] += n.durationBeat;
                end = std::max (end, n.startBeat + n.durationBeat);
            }
    auto key = estimateKey (weights);
    // 나란한조는 음이 같아 표로 가르기 어렵다: 곡 끝에 울리는 가장 낮은 음이 나란한조의 으뜸음이면 그쪽
    int bass = 1000;
    for (const size_t t : tracks)
        if (t < c.tracks.size())
            for (const auto& n : c.tracks[t].notes)
                if (n.startBeat + n.durationBeat >= end - 1.0)
                    bass = std::min (bass, n.pitch + c.tracks[t].transpose);
    if (bass < 1000)
    {
        const auto other = relativeKey (key);
        const int pc = ((bass % 12) + 12) % 12;
        if (pc == tonicPitchClass (other) && pc != tonicPitchClass (key))
            key = other;
    }
    return key;
}

Score buildScore (const Composition& c, const ScoreOptions& options)
{
    Score score;
    score.title = c.title;
    std::vector<size_t> chosen = options.tracks;
    if (chosen.empty())
        for (size_t i = 0; i < c.tracks.size(); ++i)
            if (c.tracks[i].instrument != "audio" && ! c.tracks[i].notes.empty())
                chosen.push_back (i);
    auto isPercussion = [&] (const TrackSpec& t) { return options.percussion ? options.percussion (t) : false; };
    // 조는 곡의 것이다: 고른 트랙이 아니라 타악기와 오디오를 뺀 모든 트랙의 음으로
    std::vector<size_t> pitched;
    for (size_t t = 0; t < c.tracks.size(); ++t)
        if (c.tracks[t].instrument != "audio" && ! isPercussion (c.tracks[t]))
            pitched.push_back (t);
    score.key = scoreKey (c, pitched, &score.keyEstimated);

    // 마디
    const auto meters = c.meters();
    double end = options.endBeat.value_or (0.0);
    if (! options.endBeat)
        for (const size_t t : chosen)
            if (t < c.tracks.size())
                for (const auto& n : c.tracks[t].notes)
                    end = std::max (end, n.startBeat + n.durationBeat);
    std::vector<MeasureFrame> frames;
    for (int bar = 0;; ++bar)
    {
        const double start = meters.barStart (bar);
        if (bar > 0 && start >= end - 1.0e-9)
            break;
        const auto& seg = meters.segmentAt (start);
        MeasureFrame f;
        f.startBeat = start;
        f.start = (int) std::llround (start * Q);
        f.length = (int) std::llround (seg.barBeats() * Q);
        f.beatsPerBar = seg.beatsPerBar;
        f.beatUnit = seg.beatUnit;
        frames.push_back (f);
        if (frames.size() > 20000)
            break;
    }
    auto measureOfBeat = [&frames] (double beat)
    {
        size_t lo = 0;
        for (size_t hi = frames.size(); hi - lo > 1;)
        {
            const size_t mid = (lo + hi) / 2;
            if (frames[mid].startBeat <= beat + 1.0e-9)
                lo = mid;
            else
                hi = mid;
        }
        return lo;
    };
    const int scoreEnd = frames.back().start + frames.back().length;

    // 마디마다의 조
    auto keyAt = [&] (double beat)
    {
        KeySignature k = score.key;
        for (const auto& change : c.keyChanges)
            if (change.beat <= beat + 1.0e-9)
                k = change.key;
        return k;
    };

    const TempoMap tempoMap (c.tempo);
    for (const size_t t : chosen)
    {
        if (t >= c.tracks.size())
            continue;
        const auto& track = c.tracks[t];
        NotatedPart part;
        part.name = track.name;
        part.instrument = track.instrument;
        part.track = t;
        part.percussion = isPercussion (track);
        // 큰보표: 건반 악기이고 음이 가운데 C 의 위아래에 고루 있을 때 (선율만 있는 피아노 트랙은 보표 하나)
        bool grand = ! part.percussion && (options.grandStaff ? options.grandStaff (track) : isGrandStaffInstrument (track.instrument));
        if (grand && ! options.grandStaff)
        {
            size_t low = 0;
            for (const auto& n : track.notes)
                low += n.pitch + track.transpose < 60 ? 1 : 0;
            const size_t total = track.notes.size();
            grand = total > 0 && low * 10 >= total && (total - low) * 10 >= total;
        }
        // 보표와 자리표
        std::vector<std::vector<const Note*>> staffNotes (grand ? 2 : 1);
        for (const auto& n : track.notes)
            staffNotes[grand && n.pitch + track.transpose < 60 ? 1 : 0].push_back (&n);
        if (part.percussion)
            part.clefs = { Clef::percussion };
        else if (grand)
            part.clefs = { Clef::treble, Clef::bass };
        else
        {
            double sum = 0.0, weight = 0.0;
            for (const auto& n : track.notes)
            {
                sum += (n.pitch + track.transpose) * n.durationBeat;
                weight += n.durationBeat;
            }
            part.clefs = { weight > 0.0 && sum / weight < 57.0 ? Clef::bass : Clef::treble };
        }

        part.measures.resize (frames.size());
        for (size_t m = 0; m < frames.size(); ++m)
        {
            auto& nm = part.measures[m];
            nm.number = (int) m + 1;
            nm.startBeat = frames[m].startBeat;
            nm.length = frames[m].length;
            nm.beatsPerBar = frames[m].beatsPerBar;
            nm.beatUnit = frames[m].beatUnit;
            nm.timeChanged = m == 0 || frames[m].beatsPerBar != frames[m - 1].beatsPerBar || frames[m].beatUnit != frames[m - 1].beatUnit;
            nm.key = keyAt (frames[m].startBeat);
            nm.keyChanged = m == 0 || nm.key != part.measures[m - 1].key;
            nm.staves.resize (staffNotes.size());
            const double mEnd = frames[m].startBeat + frames[m].length / (double) Q;
            for (const auto& marker : c.markers)
                if (marker.beat >= frames[m].startBeat - 1.0e-9 && marker.beat < mEnd - 1.0e-9)
                    nm.markers.push_back (marker.name);
        }
        double lastShown = 0.0;
        for (size_t i = 0; i < c.tempo.size(); ++i)
        {
            const size_t m = measureOfBeat (c.tempo[i].beat);
            if (m >= part.measures.size()
                || ! (i == 0 || std::abs (c.tempo[i].bpm - tempoMap.bpmAt (c.tempo[i].beat - 1.0e-6)) > 1.0e-6 || c.tempo[i - 1].ramp))
                continue;
            TempoMark mark { (int) std::llround ((c.tempo[i].beat - frames[m].startBeat) * Q), c.tempo[i].bpm, false };
            const bool measureHasMark = std::any_of (part.measures[m].tempo.begin(), part.measures[m].tempo.end(), [] (const TempoMark& t) { return t.shown; });
            mark.shown = i == 0 || (! measureHasMark && std::abs (mark.bpm - lastShown) >= 0.05 * lastShown);
            if (mark.shown)
                lastShown = mark.bpm;
            part.measures[m].tempo.push_back (mark);
        }

        for (size_t staff = 0; staff < staffNotes.size(); ++staff)
        {
            // 펼쳐 친 화음(스트럼): 앞 음과 0.05 박 안에 이어 시작한 음은 묶음 전체가 0.2 박 안이면 묶음의 첫 시작으로 읽는다
            std::vector<const Note*> byStart (staffNotes[staff].begin(), staffNotes[staff].end());
            std::stable_sort (byStart.begin(), byStart.end(), [] (const Note* x, const Note* y) { return x->startBeat < y->startBeat; });
            std::map<const Note*, double> begins, clusterEnds;
            {
                double head = -1.0, previous = -1.0;
                std::vector<const Note*> cluster;
                auto close = [&]
                {
                    // 묶음의 거의 같은 끝(0.06 박 안)은 가장 늦은 끝으로
                    std::vector<std::pair<double, const Note*>> ends;
                    for (const auto* n : cluster)
                        ends.push_back ({ n->startBeat + n->durationBeat, n });
                    std::sort (ends.begin(), ends.end(), [] (const auto& x, const auto& y) { return x.first < y.first; });
                    for (size_t k = 0; k < ends.size();)
                    {
                        size_t j = k;
                        while (j + 1 < ends.size() && ends[j + 1].first - ends[j].first < 0.06)
                            ++j;
                        for (size_t q = k; q < j; ++q)
                            clusterEnds[ends[q].second] = ends[j].first;
                        k = j + 1;
                    }
                    cluster.clear();
                };
                for (const auto* n : byStart)
                {
                    if (head < 0.0 || n->startBeat - previous > 0.05 || n->startBeat - head > 0.2)
                    {
                        close();
                        head = n->startBeat;
                    }
                    begins[n] = head;
                    previous = n->startBeat;
                    cluster.push_back (n);
                }
                close();
            }
            // 끊어 쓴 음의 끝: 다음 시작과의 작은 틈(16분음표 아래이고 두 시작 사이의 30% 아래)은 다음 시작까지 읽는다
            std::vector<double> starts;
            for (const auto* n : staffNotes[staff])
                starts.push_back (begins[n]);
            std::sort (starts.begin(), starts.end());
            std::map<const Note*, double> ends;
            for (const auto* n : staffNotes[staff])
            {
                double end = clusterEnds.count (n) > 0 ? clusterEnds[n] : n->startBeat + n->durationBeat;
                const auto next = std::upper_bound (starts.begin(), starts.end(), begins[n] + 1.0e-6);
                if (next != starts.end() && end < *next)
                {
                    const double gap = *next - end;
                    if (gap < 0.25 - 1.0e-9 && gap < 0.3 * (*next - begins[n]))
                        end = *next;
                }
                ends[n] = end;
            }
            // 4분음표 칸마다의 격자
            std::map<std::pair<size_t, int>, std::vector<double>> offsets;
            auto place = [&] (double beat, size_t& m, int& w, double& offset)
            {
                m = measureOfBeat (beat);
                const double rel = beat - frames[m].startBeat;
                w = (int) std::floor (rel + 1.0e-9);
                offset = rel - w;
            };
            // 격자는 음의 시작으로 고른다 (끊어 친 끝은 믿기 어렵다). 끝은 그 칸의 격자에 맞춘다
            for (const auto* n : staffNotes[staff])
            {
                size_t m;
                int w;
                double o;
                place (begins[n], m, w, o);
                offsets[{ m, w }].push_back (o);
            }
            std::map<std::pair<size_t, int>, Window> windows;
            for (const auto& [key, list] : offsets)
            {
                const double windowLength = std::min (1.0, frames[key.first].length / (double) Q - key.second);
                double straight = 0.0, triplet = 0.0;
                bool fine = false;
                for (const double o : list)
                {
                    straight += distanceToGrid (o, 0.25);
                    triplet += distanceToGrid (o, 1.0 / 6.0);
                    fine = fine || distanceToGrid (o, 0.25) > 0.0626;
                }
                // 셋잇단은 곧은 격자보다 뚜렷이 맞을 때만 (조금씩 어긋난 펼친 화음은 곧은 격자의 한 자리로)
                windows[key] = windowLength >= 1.0 - 1.0e-9 && triplet + 1.0e-9 < straight * 0.6 ? Window::triplet
                             : fine ? Window::straight32 : Window::straight16;
            }
            auto windowAt = [&windows] (size_t m, int w)
            {
                const auto it = windows.find ({ m, w });
                return it == windows.end() ? Window::straight16 : it->second;
            };
            auto quantize = [&] (double beat)
            {
                size_t m;
                int w;
                double o;
                place (beat, m, w, o);
                const double g = gridOf (windowAt (m, w));
                return (int) std::llround ((frames[m].startBeat + w + std::round (o / g) * g) * Q);
            };
            std::vector<QNote> notes;
            for (const auto* n : staffNotes[staff])
            {
                QNote q;
                q.pitch = n->pitch + track.transpose;
                q.start = std::min (quantize (begins[n]), scoreEnd - 3);
                q.end = std::min (quantize (ends[n]), scoreEnd);
                if (q.end <= q.start)
                {
                    size_t m;
                    int w;
                    double o;
                    place (begins[n], m, w, o);
                    const auto win = windowAt (m, w);
                    q.end = std::min (scoreEnd, q.start + (win == Window::triplet ? 4 : win == Window::straight32 ? 3 : 6));
                }
                q.velocity = n->velocity;
                q.lyric = n->lyric;
                notes.push_back (q);
            }

            // 경계: 음의 시작과 끝, 마디 줄
            std::set<int> cuts;
            for (const auto& f : frames)
                cuts.insert (f.start);
            cuts.insert (scoreEnd);
            for (const auto& q : notes)
            {
                cuts.insert (q.start);
                cuts.insert (q.end);
            }
            const std::vector<int> bounds (cuts.begin(), cuts.end());
            for (size_t k = 0; k + 1 < bounds.size(); ++k)
            {
                const int a = bounds[k], b = bounds[k + 1];
                if (a >= scoreEnd)
                    break;
                const size_t m = measureOfBeat (a / (double) Q);
                const auto& frame = frames[m];
                // 이 사이에 울리는 음 (높이마다 하나)
                std::map<int, const QNote*> sounding;
                for (const auto& q : notes)
                    if (q.start <= a && a < q.end)
                    {
                        auto& slot = sounding[q.pitch];
                        if (slot == nullptr || q.start == a)
                            slot = &q;
                    }
                // 음가로 쪼갠다
                const int beatLen = beatLength (frame.beatsPerBar, frame.beatUnit);
                auto isTriplet = [&] (int pos) { return windowAt (m, pos / Q) == Window::triplet && (pos / Q + 1) * Q <= frame.length; };
                auto allowed = [&] (int pos, const Value& v)
                {
                    if (pos + v.duration > frame.length)
                        return false;
                    if (v.triplet)
                    {
                        const int ws = (pos / Q) * Q;
                        return isTriplet (pos) && pos + v.duration <= ws + Q && (pos - ws) % (v.duration == 16 ? 8 : v.duration) == 0;
                    }
                    if (v.duration >= beatLen)
                    {
                        if (pos % beatLen != 0)
                            return false;
                    }
                    else if (pos / beatLen != (pos + v.duration - 1) / beatLen)
                        return false;   // 박을 넘지 않는다
                    for (int w = pos / Q; w <= (pos + v.duration - 1) / Q; ++w)
                        if (isTriplet (w * Q) && (pos > w * Q || pos + v.duration < std::min ((w + 1) * Q, frame.length)))
                            return false;   // 셋잇단 칸은 통째로만 덮는다
                    return true;
                };
                std::vector<NotatedEvent> pieces;
                for (int pos = a - frame.start, stop = b - frame.start; pos < stop;)
                {
                    const Value* chosenValue = nullptr;
                    for (const auto& v : values)
                        if (v.duration <= stop - pos && allowed (pos, v))
                        {
                            chosenValue = &v;
                            break;
                        }
                    NotatedEvent e;
                    e.start = pos;
                    if (chosenValue != nullptr)
                    {
                        e.duration = chosenValue->duration;
                        e.type = chosenValue->type;
                        e.dots = chosenValue->dots;
                        e.triplet = chosenValue->triplet;
                    }
                    else
                    {
                        e.duration = std::min (stop - pos, isTriplet (pos) ? 4 : 3);
                        e.type = isTriplet (pos) ? "16th" : "32nd";
                        e.triplet = isTriplet (pos);
                    }
                    pieces.push_back (e);
                    pos += e.duration;
                }
                const Spelling dummy;
                for (size_t p = 0; p < pieces.size(); ++p)
                {
                    auto& e = pieces[p];
                    e.rest = sounding.empty();
                    for (const auto& [pitch, q] : sounding)
                    {
                        NotatedPitch np;
                        np.pitch = pitch;
                        const bool attack = q->start == a;
                        np.tieStop = p > 0 || ! attack;
                        np.tieStart = p + 1 < pieces.size() || q->end > b;
                        if (part.percussion)
                        {
                            const auto d = drumPlace (pitch);
                            np.displayStep = d.step;
                            np.displayOctave = d.octave;
                            np.notehead = d.head;
                            np.spelling = dummy;
                        }
                        else
                            np.spelling = spellPitch (pitch, part.measures[m].key);
                        if (p == 0 && attack)
                        {
                            e.velocity = std::max (e.velocity, q->velocity);
                            if (e.lyric.empty() && ! q->lyric.empty())
                                e.lyric = q->lyric;
                        }
                        e.pitches.push_back (np);
                    }
                    part.measures[m].staves[staff].push_back (e);
                }
            }

            // 마디마다: 마디 전체 쉼표, 셋잇단 묶음, 임시표
            for (auto& measure : part.measures)
            {
                auto& events = measure.staves[staff];
                if (events.empty())
                {
                    NotatedEvent rest;
                    rest.rest = true;
                    rest.duration = measure.length;
                    events.push_back (rest);
                }
                if (events.size() == 1 && events[0].rest)
                {
                    events[0].measureRest = true;
                    events[0].type = "whole";
                    events[0].dots = 0;
                    events[0].triplet = false;
                    events[0].duration = measure.length;
                }
                for (size_t i = 0; i < events.size(); ++i)
                    if (events[i].triplet)
                    {
                        const int w = events[i].start / Q;
                        events[i].tupletStart = i == 0 || ! events[i - 1].triplet || events[i - 1].start / Q != w;
                        events[i].tupletStop = i + 1 == events.size() || ! events[i + 1].triplet || events[i + 1].start / Q != w;
                    }
                if (part.percussion)
                    continue;
                std::map<std::pair<int, int>, int> state;   // (글자, 옥타브) -> 지금의 변화
                for (auto& e : events)
                    for (auto& p : e.pitches)
                    {
                        if (p.tieStop)
                            continue;
                        const auto key = std::make_pair (p.spelling.step, p.spelling.octave);
                        const auto it = state.find (key);
                        const int current = it != state.end() ? it->second : keyAlter (measure.key, p.spelling.step);
                        if (p.spelling.alter != current)
                            p.accidental = p.spelling.alter;
                        state[key] = p.spelling.alter;
                    }
            }
        }
        score.parts.push_back (std::move (part));
    }
    return score;
}

} // namespace mr
