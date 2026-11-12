#include "composition/NoteEdits.h"

#include "core/Random.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace mr
{

namespace
{

// 고른 음 전체가 범위 안에 머무는 이동량
void limitMove (const std::vector<EditNote>& notes, const NoteSelection& selection, double& deltaBeats, int& deltaPitch)
{
    for (const size_t i : selection)
    {
        const auto& n = notes[i].note;
        deltaBeats = std::max (deltaBeats, -n.startBeat);
        deltaPitch = std::clamp (deltaPitch, -n.pitch, 127 - n.pitch);
    }
}

NoteEditResult unchanged (const std::vector<EditNote>& notes, const NoteSelection& selection) { return { notes, selection }; }

// 고른 음 (비면 모두)
NoteSelection chosen (const std::vector<EditNote>& notes, const NoteSelection& selection)
{
    if (! selection.empty())
        return selection;
    NoteSelection all;
    for (size_t i = 0; i < notes.size(); ++i)
        all.push_back (i);
    return all;
}

double tidyBeat (double b) { return std::round (b * 1.0e6) / 1.0e6; }

} // namespace

NoteEditResult moveNotes (const std::vector<EditNote>& notes, const NoteSelection& selection, double deltaBeats, int deltaPitch)
{
    limitMove (notes, selection, deltaBeats, deltaPitch);
    auto out = unchanged (notes, selection);
    for (const size_t i : selection)
    {
        auto& n = out.notes[i].note;
        n.startBeat = std::max (0.0, n.startBeat + deltaBeats);
        n.pitch += deltaPitch;
    }
    return out;
}

NoteEditResult copyNotes (const std::vector<EditNote>& notes, const NoteSelection& selection, double deltaBeats, int deltaPitch)
{
    limitMove (notes, selection, deltaBeats, deltaPitch);
    std::vector<Note> copies;
    for (const size_t i : selection)
    {
        auto n = notes[i].note;
        n.startBeat = std::max (0.0, n.startBeat + deltaBeats);
        n.pitch += deltaPitch;
        copies.push_back (n);
    }
    return addNotes (notes, copies);
}

NoteEditResult resizeNotes (const std::vector<EditNote>& notes, const NoteSelection& selection, double deltaBeats, double minLength)
{
    auto out = unchanged (notes, selection);
    for (const size_t i : selection)
    {
        auto& n = out.notes[i].note;
        n.durationBeat = std::max (minLength, n.durationBeat + deltaBeats);
    }
    return out;
}

NoteEditResult setVelocity (const std::vector<EditNote>& notes, const NoteSelection& selection, int velocity)
{
    auto out = unchanged (notes, selection);
    for (const size_t i : selection)
        out.notes[i].note.velocity = std::clamp (velocity, 1, 127);
    return out;
}

NoteEditResult setArticulation (const std::vector<EditNote>& notes, const NoteSelection& selection, const std::string& articulation)
{
    NoteEditResult r { notes, selection };
    for (const size_t i : selection)
        if (i < r.notes.size())
            r.notes[i].note.articulation = articulation;
    return r;
}

NoteEditResult offsetVelocity (const std::vector<EditNote>& notes, const NoteSelection& selection, int delta)
{
    auto out = unchanged (notes, selection);
    for (const size_t i : selection)
        out.notes[i].note.velocity = std::clamp (notes[i].note.velocity + delta, 1, 127);
    return out;
}

NoteEditResult quantizeNotes (const std::vector<EditNote>& notes, const NoteSelection& selection, double grid)
{
    auto out = unchanged (notes, selection);
    if (grid <= 0.0)
        return out;
    auto snap = [grid] (Note& n) { n.startBeat = std::max (0.0, std::round (n.startBeat / grid) * grid); };
    if (selection.empty())
        for (auto& e : out.notes)
            snap (e.note);
    else
        for (const size_t i : selection)
            snap (out.notes[i].note);
    return out;
}

NoteEditResult deleteNotes (const std::vector<EditNote>& notes, const NoteSelection& selection)
{
    NoteEditResult out;
    std::vector<bool> drop (notes.size(), false);
    for (const size_t i : selection)
        if (i < notes.size())
            drop[i] = true;
    for (size_t i = 0; i < notes.size(); ++i)
        if (! drop[i])
            out.notes.push_back (notes[i]);
    return out;
}

NoteEditResult addNotes (const std::vector<EditNote>& notes, const std::vector<Note>& added)
{
    std::vector<EditNote> merged = notes;
    std::vector<bool> fresh (notes.size(), false);
    for (const auto& n : added)
    {
        // 시작이 이 음보다 늦은 첫 음의 앞 (같은 시작이면 그 뒤)
        size_t at = merged.size();
        for (size_t i = 0; i < merged.size(); ++i)
            if (merged[i].note.startBeat > n.startBeat)
            {
                at = i;
                break;
            }
        merged.insert (merged.begin() + (std::ptrdiff_t) at, EditNote { n, juce::var() });
        fresh.insert (fresh.begin() + (std::ptrdiff_t) at, true);
    }
    NoteEditResult out;
    out.notes = std::move (merged);
    for (size_t i = 0; i < fresh.size(); ++i)
        if (fresh[i])
            out.selection.push_back (i);
    return out;
}

NoteEditResult pasteNotes (const std::vector<EditNote>& notes, const std::vector<Note>& clip, double atBeat)
{
    if (clip.empty())
        return { notes, {} };
    double first = clip.front().startBeat;
    for (const auto& n : clip)
        first = std::min (first, n.startBeat);
    std::vector<Note> moved = clip;
    for (auto& n : moved)
        n.startBeat = std::max (0.0, n.startBeat - first + atBeat);
    return addNotes (notes, moved);
}

NoteSelection notesInRange (const std::vector<EditNote>& notes, double beat0, double beat1, int pitchLow, int pitchHigh)
{
    NoteSelection out;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i].note;
        if (n.pitch >= pitchLow && n.pitch <= pitchHigh && n.startBeat < beat1 && n.startBeat + n.durationBeat > beat0)
            out.push_back (i);
    }
    return out;
}

std::vector<Note> selectedNotes (const std::vector<EditNote>& notes, const NoteSelection& selection)
{
    std::vector<Note> out;
    for (const size_t i : selection)
        if (i < notes.size())
            out.push_back (notes[i].note);
    return out;
}

} // namespace mr

namespace mr
{

NoteEditResult quantizeNotes (const std::vector<EditNote>& notes, const NoteSelection& selection, const QuantizeOptions& o)
{
    auto out = unchanged (notes, selection);
    if (o.grid <= 0.0)
        return out;
    const double strength = std::clamp (o.strength, 0.0, 1.0);
    auto target = [&o] (double beat)
    {
        const double index = std::round (beat / o.grid);
        double t = index * o.grid;
        if (((long long) index) % 2 != 0)
            t += o.grid / 3.0 * std::clamp (o.swing, 0.0, 1.0);   // 둘째 칸을 늦춘다
        return t;
    };
    for (const size_t i : chosen (notes, selection))
    {
        auto& n = out.notes[i].note;
        const double end = n.startBeat + n.durationBeat;
        n.startBeat = tidyBeat (std::max (0.0, n.startBeat + (target (n.startBeat) - n.startBeat) * strength));
        if (o.ends)
        {
            double e = end + (std::round (end / o.grid) * o.grid - end) * strength;
            if (e < n.startBeat + o.grid * 0.5)
                e = n.startBeat + o.grid;
            n.durationBeat = tidyBeat (e - n.startBeat);
        }
        else
            n.durationBeat = tidyBeat (std::max (1.0e-3, end - n.startBeat));
    }
    return out;
}

NoteEditResult scaleVelocities (const std::vector<EditNote>& notes, const NoteSelection& selection, double factor)
{
    auto out = unchanged (notes, selection);
    const auto pick = chosen (notes, selection);
    if (pick.empty())
        return out;
    double mean = 0.0;
    for (const size_t i : pick)
        mean += notes[i].note.velocity;
    mean /= (double) pick.size();
    for (const size_t i : pick)
    {
        auto& n = out.notes[i].note;
        n.velocity = std::clamp ((int) std::lround (mean + (n.velocity - mean) * factor), 1, 127);
    }
    return out;
}

NoteEditResult rampVelocities (const std::vector<EditNote>& notes, const NoteSelection& selection, int from, int to)
{
    auto out = unchanged (notes, selection);
    const auto pick = chosen (notes, selection);
    if (pick.empty())
        return out;
    double first = 1.0e300, last = -1.0e300;
    for (const size_t i : pick)
    {
        first = std::min (first, notes[i].note.startBeat);
        last = std::max (last, notes[i].note.startBeat);
    }
    for (const size_t i : pick)
    {
        const double t = last > first ? (notes[i].note.startBeat - first) / (last - first) : 0.0;
        out.notes[i].note.velocity = std::clamp ((int) std::lround (from + (to - from) * t), 1, 127);
    }
    return out;
}

NoteEditResult humanizeNotes (const std::vector<EditNote>& notes, const NoteSelection& selection, int velocity, double beats, uint64_t seed)
{
    auto out = unchanged (notes, selection);
    for (const size_t i : chosen (notes, selection))
    {
        auto& n = out.notes[i].note;
        Random r (combineSeed (seed, (uint64_t) i * 131 + (uint64_t) n.pitch));
        const double dv = (r.uniform() * 2.0 - 1.0) * velocity;
        const double dt = (r.uniform() * 2.0 - 1.0) * beats;
        n.velocity = std::clamp ((int) std::lround (n.velocity + dv), 1, 127);
        const double end = n.startBeat + n.durationBeat;
        n.startBeat = tidyBeat (std::max (0.0, n.startBeat + dt));
        n.durationBeat = tidyBeat (std::max (1.0e-3, end - n.startBeat));
    }
    return out;
}

NoteEditResult legatoNotes (const std::vector<EditNote>& notes, const NoteSelection& selection)
{
    auto out = unchanged (notes, selection);
    const auto pick = chosen (notes, selection);
    std::vector<double> starts;
    for (const size_t i : pick)
        starts.push_back (notes[i].note.startBeat);
    std::sort (starts.begin(), starts.end());
    starts.erase (std::unique (starts.begin(), starts.end(), [] (double a, double b) { return std::abs (a - b) < 1.0e-6; }), starts.end());
    for (const size_t i : pick)
    {
        auto& n = out.notes[i].note;
        const auto next = std::upper_bound (starts.begin(), starts.end(), n.startBeat + 1.0e-6);
        if (next != starts.end())
            n.durationBeat = tidyBeat (*next - n.startBeat);
    }
    return out;
}

NoteEditResult setLengths (const std::vector<EditNote>& notes, const NoteSelection& selection, double length)
{
    auto out = unchanged (notes, selection);
    if (length <= 0.0)
        return out;
    for (const size_t i : chosen (notes, selection))
        out.notes[i].note.durationBeat = tidyBeat (length);
    return out;
}

NoteEditResult snapToScale (const std::vector<EditNote>& notes, const NoteSelection& selection, int root, const std::vector<int>& intervals)
{
    auto out = unchanged (notes, selection);
    if (intervals.empty())
        return out;
    auto inScale = [&] (int pitch)
    {
        const int pc = ((pitch - root) % 12 + 12) % 12;
        return std::find (intervals.begin(), intervals.end(), pc) != intervals.end();
    };
    for (const size_t i : chosen (notes, selection))
    {
        auto& n = out.notes[i].note;
        if (inScale (n.pitch))
            continue;
        for (int d = 1; d <= 6; ++d)
        {
            if (n.pitch - d >= 0 && inScale (n.pitch - d))
            {
                n.pitch -= d;
                break;
            }
            if (n.pitch + d <= 127 && inScale (n.pitch + d))
            {
                n.pitch += d;
                break;
            }
        }
    }
    return out;
}

NoteEditResult retrogradeNotes (const std::vector<EditNote>& notes, const NoteSelection& selection)
{
    auto out = unchanged (notes, selection);
    const auto pick = chosen (notes, selection);
    if (pick.empty())
        return out;
    double first = 1.0e300, last = -1.0e300;
    for (const size_t i : pick)
    {
        first = std::min (first, notes[i].note.startBeat);
        last = std::max (last, notes[i].note.startBeat + notes[i].note.durationBeat);
    }
    for (const size_t i : pick)
    {
        auto& n = out.notes[i].note;
        n.startBeat = tidyBeat (std::max (0.0, first + last - (n.startBeat + n.durationBeat)));
    }
    return out;
}

NoteEditResult invertNotes (const std::vector<EditNote>& notes, const NoteSelection& selection)
{
    auto out = unchanged (notes, selection);
    const auto pick = chosen (notes, selection);
    if (pick.empty())
        return out;
    size_t axis = pick.front();
    for (const size_t i : pick)
    {
        const auto& a = notes[i].note;
        const auto& b = notes[axis].note;
        if (a.startBeat < b.startBeat - 1.0e-9 || (std::abs (a.startBeat - b.startBeat) < 1.0e-9 && a.pitch < b.pitch))
            axis = i;
    }
    const int pivot = notes[axis].note.pitch;
    for (const size_t i : pick)
    {
        auto& n = out.notes[i].note;
        n.pitch = std::clamp (2 * pivot - n.pitch, 0, 127);
    }
    return out;
}

} // namespace mr

namespace mr
{

namespace
{

// 같은 시작의 고른 음 묶음 (시작 차례)
std::vector<std::vector<size_t>> chordsOf (const std::vector<EditNote>& notes, const NoteSelection& pick)
{
    std::vector<size_t> order (pick.begin(), pick.end());
    std::stable_sort (order.begin(), order.end(), [&notes] (size_t a, size_t b) { return notes[a].note.startBeat < notes[b].note.startBeat; });
    std::vector<std::vector<size_t>> groups;
    for (const size_t i : order)
    {
        if (groups.empty() || std::abs (notes[groups.back().front()].note.startBeat - notes[i].note.startBeat) > 1.0e-6)
            groups.push_back ({});
        groups.back().push_back (i);
    }
    for (auto& g : groups)
        std::stable_sort (g.begin(), g.end(), [&notes] (size_t a, size_t b) { return notes[a].note.pitch < notes[b].note.pitch; });
    return groups;
}

} // namespace

NoteEditResult arpeggiateNotes (const std::vector<EditNote>& notes, const NoteSelection& selection, ArpeggioOrder order, double step)
{
    const auto pick = chosen (notes, selection);
    if (step <= 0.0 || pick.empty())
        return unchanged (notes, selection);
    std::vector<bool> drop (notes.size(), false);
    std::vector<EditNote> added;
    for (const auto& chord : chordsOf (notes, pick))
    {
        if (chord.size() < 2)
        {
            added.push_back (notes[chord.front()]);   // 홑음은 그대로 (자리를 옮겨 다시 넣는다)
            drop[chord.front()] = true;
            continue;
        }
        std::vector<size_t> cycle (chord.begin(), chord.end());
        if (order == ArpeggioOrder::down)
            std::reverse (cycle.begin(), cycle.end());
        else if (order == ArpeggioOrder::upDown)
            for (size_t k = chord.size() - 1; k-- > 1;)
                cycle.push_back (chord[k]);
        double length = 0.0;
        for (const size_t i : chord)
        {
            length = std::max (length, notes[i].note.durationBeat);
            drop[i] = true;
        }
        const double start = notes[chord.front()].note.startBeat;
        const int count = std::max (1, (int) std::floor (length / step + 1.0e-9));
        std::vector<bool> used (notes.size(), false);
        for (int k = 0; k < count; ++k)
        {
            const size_t from = cycle[(size_t) k % cycle.size()];
            EditNote e = notes[from];
            if (used[from])
            {
                e.source = juce::var();   // 되풀이한 음은 새 음 (가사는 처음 음에만)
                e.note.lyric.clear();
            }
            used[from] = true;
            e.note.startBeat = tidyBeat (start + k * step);
            e.note.durationBeat = tidyBeat (k == count - 1 ? std::max (step, length - k * step) : step);
            added.push_back (e);
        }
    }
    std::vector<EditNote> kept;
    for (size_t i = 0; i < notes.size(); ++i)
        if (! drop[i])
            kept.push_back (notes[i]);
    // 새 음을 시작 차례의 자리에 넣고 고른다
    std::vector<bool> fresh (kept.size(), false);
    for (const auto& e : added)
    {
        size_t at = kept.size();
        for (size_t i = 0; i < kept.size(); ++i)
            if (kept[i].note.startBeat > e.note.startBeat)
            {
                at = i;
                break;
            }
        kept.insert (kept.begin() + (std::ptrdiff_t) at, e);
        fresh.insert (fresh.begin() + (std::ptrdiff_t) at, true);
    }
    NoteEditResult out;
    out.notes = std::move (kept);
    for (size_t i = 0; i < fresh.size(); ++i)
        if (fresh[i])
            out.selection.push_back (i);
    return out;
}

NoteEditResult strumNotes (const std::vector<EditNote>& notes, const NoteSelection& selection, double spread, bool upward)
{
    auto out = unchanged (notes, selection);
    for (const auto& chord : chordsOf (notes, chosen (notes, selection)))
    {
        if (chord.size() < 2)
            continue;
        for (size_t k = 0; k < chord.size(); ++k)
        {
            auto& n = out.notes[upward ? chord[k] : chord[chord.size() - 1 - k]].note;
            const double end = n.startBeat + n.durationBeat;
            n.startBeat = tidyBeat (std::min (end - 1.0e-3, n.startBeat + spread * (double) k));
            n.durationBeat = tidyBeat (end - n.startBeat);
        }
    }
    return out;
}

} // namespace mr
