#include "performance/Performance.h"

#include "core/Random.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace mr
{

ResolvedPerformance resolvePerformance (const PerformanceSettings& c, const PerformanceSettings& t, std::optional<uint64_t> seedOverride)
{
    ResolvedPerformance r;
    auto pick = [] (const std::optional<double>& track, const std::optional<double>& comp, double fallback)
    { return track ? *track : (comp ? *comp : fallback); };
    r.timingHumanizeMs = pick (t.timingHumanizeMs, c.timingHumanizeMs, 0.0);
    r.velocityHumanize = pick (t.velocityHumanize, c.velocityHumanize, 0.0);
    r.noteLengthHumanize = pick (t.noteLengthHumanize, c.noteLengthHumanize, 0.0);
    r.legatoOverlapMs = pick (t.legatoOverlapMs, c.legatoOverlapMs, 0.0);
    r.swing = pick (t.swing, c.swing, 0.0);
    r.swingGrid = pick (t.swingGrid, c.swingGrid, 0.5);
    r.accentGrid = pick (t.accentGrid, c.accentGrid, 1.0);
    r.accentPattern = t.accentPattern ? *t.accentPattern : (c.accentPattern ? *c.accentPattern : std::vector<int> {});
    const uint64_t compositionSeed = seedOverride ? *seedOverride : c.humanizeSeed.value_or (0);
    r.humanizeSeed = t.humanizeSeed ? *t.humanizeSeed : compositionSeed;
    return r;
}

double swingBeat (double beat, double swing, double grid)
{
    if (swing <= 0.0 || grid <= 0.0)
        return beat;
    const double pair = grid * 2.0;
    const double pos = std::fmod (beat, pair);
    const double tolerance = 1.0e-4;
    if (std::abs (pos - grid) < tolerance)
        return beat + swing * grid / 3.0;   // swing 1 = 셋잇단 느낌 (2:1)
    return beat;
}

namespace
{

double clampSymmetric (double x, double limit)
{
    return std::max (-limit, std::min (limit, x));
}

} // namespace

int instrumentKey (int pitch, int transpose, const std::map<int, int>* keymap)
{
    const int key = std::clamp (pitch + transpose, 0, 127);
    if (keymap != nullptr)
        if (const auto it = keymap->find (key); it != keymap->end())
            return it->second;
    return key;
}

std::vector<PerformedNote> perform (const std::vector<Note>& notes, const ResolvedPerformance& s, const PerformanceContext& ctx)
{
    std::vector<size_t> order (notes.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort (order.begin(), order.end(), [&] (size_t a, size_t b)
    {
        if (notes[a].startBeat != notes[b].startBeat)
            return notes[a].startBeat < notes[b].startBeat;
        return notes[a].pitch < notes[b].pitch;
    });

    // 이어지는 음표가 있는 끝 박 (레가토 판정). 1/1000 박 단위로 맞춘다.
    auto key = [] (double beat) { return (int64_t) std::llround (beat * 1000.0); };
    std::map<int64_t, int> startsAt;
    for (const auto& n : notes)
        startsAt[key (n.startBeat)]++;

    const uint64_t trackSeed = combineSeed (s.humanizeSeed, hashString (ctx.trackName));
    std::map<std::pair<int, int64_t>, int> occurrences;

    std::vector<PerformedNote> out;
    out.reserve (notes.size());
    for (size_t idx : order)
    {
        const Note& n = notes[idx];
        PerformedNote p;
        p.sourceIndex = idx;
        p.pitch = instrumentKey (n.pitch, ctx.transpose, ctx.keymap);
        p.channel = n.channel;
        p.articulation = n.articulation.empty() ? ctx.trackArticulation : n.articulation;
        const ArticulationDef* art = nullptr;
        if (ctx.profile != nullptr)
        {
            if (p.articulation.empty())
                p.articulation = ctx.profile->defaultArticulation;
            if (auto it = ctx.profile->articulations.find (p.articulation); it != ctx.profile->articulations.end())
                art = &it->second;
        }

        const double lengthBeats = n.durationBeat * (art != nullptr ? art->lengthFactor : 1.0);
        const double startBeat = swingBeat (n.startBeat, s.swing, s.swingGrid);
        const double endBeat = swingBeat (n.startBeat + lengthBeats, s.swing, s.swingGrid);

        double velocity = n.velocity + (art != nullptr ? art->velocityOffset : 0);
        if (! s.accentPattern.empty())
        {
            const double pos = ctx.meters != nullptr ? ctx.meters->beatInBar (n.startBeat) : std::fmod (n.startBeat, ctx.barBeats);
            const auto slot = (size_t) std::floor (pos / s.accentGrid + 1.0e-6) % s.accentPattern.size();
            velocity += s.accentPattern[slot];
        }

        double timingMs = 0.0, lengthFactor = 1.0;
        if (n.humanize)
        {
            const auto noteKey = std::make_pair (n.pitch, key (n.startBeat));
            const int occurrence = occurrences[noteKey]++;
            uint64_t seed = combineSeed (trackSeed, (uint64_t) n.pitch);
            seed = combineSeed (seed, (uint64_t) key (n.startBeat));
            seed = combineSeed (seed, (uint64_t) occurrence);
            Random rng (seed);
            const double g1 = rng.gaussian(), g2 = rng.gaussian(), g3 = rng.gaussian();
            if (s.timingHumanizeMs > 0.0)
                timingMs = clampSymmetric (g1 * s.timingHumanizeMs * 0.5, s.timingHumanizeMs);
            if (s.velocityHumanize > 0.0)
                velocity += std::round (clampSymmetric (g2 * s.velocityHumanize * 0.5, s.velocityHumanize));
            if (s.noteLengthHumanize > 0.0)
                lengthFactor = 1.0 + clampSymmetric (g3 * s.noteLengthHumanize * 0.5, s.noteLengthHumanize);
        }

        const double nominalStart = ctx.tempo.secondsAt (startBeat);
        const double nominalEnd = ctx.tempo.secondsAt (endBeat);
        p.startSec = std::max (0.0, nominalStart + timingMs / 1000.0);
        p.endSec = p.startSec + (nominalEnd - nominalStart) * lengthFactor;

        const bool legatoArticulation = p.articulation == "legato" || (art != nullptr && art->legato);
        p.legato = legatoArticulation || startsAt.count (key (n.startBeat + n.durationBeat)) > 0;
        // 레가토 아티큘레이션은 겹침을 따로 정하지 않았어도 40ms 겹친다
        const double overlapMs = s.legatoOverlapMs > 0.0 ? s.legatoOverlapMs : (legatoArticulation ? 40.0 : 0.0);
        if (p.legato && overlapMs > 0.0)
            p.endSec += overlapMs / 1000.0;

        p.endSec = std::max (p.endSec, p.startSec + 0.005);
        p.velocity = (int) std::clamp (velocity, 1.0, 127.0);
        out.push_back (p);
    }

    std::stable_sort (out.begin(), out.end(), [] (const PerformedNote& a, const PerformedNote& b)
    { return a.startSec < b.startSec; });

    // 같은 음높이가 겹치면 앞 음을 뒤 음 직전에 끊는다. MIDI 에서 같은 음의 note-off 는 어느 쪽을
    // 끄는지 악기마다 달라 겹침을 남기면 뒤 음이 잘린다.
    std::map<std::pair<int, int>, size_t> lastByPitch;
    for (size_t i = 0; i < out.size(); ++i)
    {
        const auto k = std::make_pair (out[i].channel, out[i].pitch);
        if (auto it = lastByPitch.find (k); it != lastByPitch.end())
        {
            auto& prev = out[it->second];
            if (prev.endSec > out[i].startSec - 0.001)
                prev.endSec = std::max (prev.startSec + 0.005, out[i].startSec - 0.001);
        }
        lastByPitch[k] = i;
    }
    return out;
}

} // namespace mr
