#include "analysis/Playability.h"

#include "timing/MeterMap.h"
#include "timing/TempoMap.h"

#include <algorithm>
#include <cmath>

namespace mr
{
namespace
{

constexpr double struckWindow = 0.03;    // 이 박 안에 시작한 음은 함께 친다
constexpr double rollStep = 0.1;         // 굴리는 화음의 이웃 음 사이
constexpr double rollLength = 0.3;       // 굴리는 화음 전체
constexpr double graceLength = 0.1;      // 앞꾸밈음의 길이와 본 음까지의 거리
constexpr double graceLead = 0.12;

struct Group
{
    double start = 0.0;      // 첫 음의 시작
    double last = 0.0;       // 마지막 음의 시작
    bool rolled = false;
    std::vector<const Note*> notes;
    int low = 127, high = 0;
};

std::vector<Group> groupsOf (const TrackSpec& track)
{
    std::vector<const Note*> sorted;
    for (const auto& n : track.notes)
        sorted.push_back (&n);
    std::stable_sort (sorted.begin(), sorted.end(), [] (const Note* a, const Note* b) { return a->startBeat < b->startBeat; });
    std::vector<Group> groups;
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        const auto* n = sorted[i];
        // 앞꾸밈음은 손의 자리를 정하지 않는다
        if (n->durationBeat <= graceLength && i + 1 < sorted.size() && sorted[i + 1]->startBeat - n->startBeat <= graceLead
            && sorted[i + 1]->startBeat - n->startBeat > struckWindow)
            continue;
        const int pitch = n->pitch + track.transpose;
        bool joined = false;
        if (! groups.empty())
        {
            auto& g = groups.back();
            const bool struck = n->startBeat - g.start < struckWindow;
            const bool roll = ! struck && n->startBeat - g.last <= rollStep && n->startBeat - g.start <= rollLength && (g.rolled || g.notes.size() == 1 || g.last - g.start < struckWindow)
                              && n->durationBeat > rollLength && g.notes.back()->startBeat + g.notes.back()->durationBeat > n->startBeat + rollStep;
            if (struck || roll)
            {
                g.rolled = g.rolled || roll;
                g.last = n->startBeat;
                g.notes.push_back (n);
                g.low = std::min (g.low, pitch);
                g.high = std::max (g.high, pitch);
                joined = true;
            }
        }
        if (! joined)
            groups.push_back ({ n->startBeat, n->startBeat, false, { n }, pitch, pitch });
    }
    return groups;
}

} // namespace

PlayabilityReport checkPlayability (const Composition& c, const PlayabilityOptions& options)
{
    PlayabilityReport report;
    std::vector<size_t> tracks = options.tracks;
    if (tracks.empty())
    {
        for (size_t t = 0; t < c.tracks.size(); ++t)
            if (! c.tracks[t].scorePart.empty() && c.tracks[t].instrument != "audio" && ! c.tracks[t].notes.empty())
                tracks.push_back (t);
        if (tracks.empty())
            for (size_t t = 0; t < c.tracks.size(); ++t)
                if (c.tracks[t].instrument != "audio" && ! c.tracks[t].notes.empty())
                    tracks.push_back (t);
    }
    const TempoMap tempo (c.tempo);
    const auto meters = c.meters();
    auto issue = [&] (size_t track, double beat, const char* kind, std::vector<int> pitches, double value, double limit, int semitones = 0)
    {
        PlayabilityIssue i;
        i.track = track;
        const int bar = meters.barAt (beat);
        i.bar = bar + 1;
        i.beat = beat - meters.barStart (bar) + 1.0;
        i.startBeat = beat;
        i.kind = kind;
        std::sort (pitches.begin(), pitches.end());
        i.pitches = std::move (pitches);
        i.value = value;
        i.limit = limit;
        i.semitones = semitones;
        report.issues.push_back (std::move (i));
    };

    std::vector<std::vector<Group>> all;
    std::vector<double> centre;
    for (const size_t t : tracks)
    {
        if (t >= c.tracks.size())
            continue;
        const auto& track = c.tracks[t];
        auto groups = groupsOf (track);
        PlayabilityHand hand;
        hand.track = t;
        hand.notes = track.notes.size();
        const auto pedal = track.automation.find (64);
        auto pedalDown = [&] (double beat) { return pedal != track.automation.end() && ! pedal->second.empty() && pedal->second.valueAt (beat) >= options.pedalDown; };
        double sum = 0.0;
        for (size_t i = 0; i < groups.size(); ++i)
        {
            const auto& g = groups[i];
            std::vector<int> pitches;
            for (const auto* n : g.notes)
                pitches.push_back (n->pitch + track.transpose);
            sum += 0.5 * (g.low + g.high);
            const int span = g.high - g.low;
            hand.widestSpan = std::max (hand.widestSpan, span);
            const int limit = g.rolled ? options.rolledSpan : options.span;
            if (span > limit)
                issue (t, g.start, "span", pitches, span, limit);
            if ((int) g.notes.size() > options.maxNotes)
                issue (t, g.start, "notes", pitches, (double) g.notes.size(), options.maxNotes);
            if (i == 0)
                continue;
            const auto& p = groups[i - 1];
            const double have = tempo.secondsAt (g.start) - tempo.secondsAt (p.last);
            if (hand.fastestSeconds == 0.0 || have < hand.fastestSeconds)
                hand.fastestSeconds = have;
            bool same = false;
            for (const auto* a : p.notes)
                for (const auto* b : g.notes)
                    same = same || a->pitch == b->pitch;
            if (same && have < options.repeatSeconds - 1.0e-9)
                issue (t, g.start, "repeat", pitches, have, options.repeatSeconds);
            else if (have < options.minGapSeconds - 1.0e-9)
                issue (t, g.start, "fast", pitches, have, options.minGapSeconds);
            // 자리 옮김: 앞 묶음과 한 손 자리에 들지 않으면 가까운 쪽 끝이 움직인 거리만큼 시간이 든다
            if (std::max (g.high, p.high) - std::min (g.low, p.low) > options.span)
            {
                const int travel = std::min (std::abs (g.low - p.low), std::abs (g.high - p.high));
                const double need = options.shiftSeconds + options.shiftPerSemitone * travel;
                if (have < need - 1.0e-9)
                    issue (t, g.start, "shift", pitches, have, need, travel);
            }
        }
        // 페달 없이 붙든 음: 울리는 동안 새로 친 음과의 벌림
        for (size_t i = 0; i < groups.size(); ++i)
            for (const auto* held : groups[i].notes)
            {
                const double end = held->startBeat + held->durationBeat;
                for (size_t j = i + 1; j < groups.size() && groups[j].start < end - 0.05; ++j)
                {
                    const auto& g = groups[j];
                    const int pitch = held->pitch + track.transpose;
                    const int span = std::max (g.high, pitch) - std::min (g.low, pitch);
                    if (span > options.rolledSpan && ! pedalDown (g.start + 0.02))
                    {
                        std::vector<int> pitches { pitch };
                        for (const auto* n : g.notes)
                            pitches.push_back (n->pitch + track.transpose);
                        issue (t, g.start, "held", pitches, span, options.rolledSpan);
                        break;
                    }
                }
            }
        centre.push_back (groups.empty() ? 60.0 : sum / (double) groups.size());
        all.push_back (std::move (groups));
        report.hands.push_back (hand);
    }

    // 두 손: 같은 순간에 낮은 손의 가장 높은 음이 높은 손의 가장 낮은 음에 닿는다
    for (size_t a = 0; a < all.size(); ++a)
        for (size_t b = a + 1; b < all.size(); ++b)
        {
            const size_t lo = centre[a] <= centre[b] ? a : b, hi = lo == a ? b : a;
            size_t k = 0;
            for (const auto& g : all[hi])
            {
                while (k < all[lo].size() && all[lo][k].start < g.start - struckWindow)
                    ++k;
                if (k < all[lo].size() && std::abs (all[lo][k].start - g.start) < struckWindow && all[lo][k].high >= g.low)
                    issue (report.hands[hi].track, g.start, "overlap", { all[lo][k].high, g.low }, all[lo][k].high - g.low, 0.0);
            }
        }
    std::stable_sort (report.issues.begin(), report.issues.end(), [] (const PlayabilityIssue& x, const PlayabilityIssue& y) { return x.startBeat < y.startBeat; });
    return report;
}

} // namespace mr
