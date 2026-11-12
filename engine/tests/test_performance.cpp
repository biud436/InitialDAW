#include "TestFramework.h"
#include "TestUtil.h"

#include "performance/Performance.h"

#include <set>

using namespace mr;

namespace
{

std::vector<Note> scale (int count)
{
    std::vector<Note> notes;
    for (int i = 0; i < count; ++i)
        notes.push_back ({ 60 + (i % 12), i * 0.5, 0.5, 80, 1, {}, true });
    return notes;
}

ResolvedPerformance humanized (uint64_t seed)
{
    ResolvedPerformance p;
    p.timingHumanizeMs = 12.0;
    p.velocityHumanize = 8.0;
    p.noteLengthHumanize = 0.04;
    p.humanizeSeed = seed;
    return p;
}

} // namespace

TEST (without_humanize_notes_land_on_the_grid)
{
    TempoMap tempo ({ { 0.0, 120.0, false } });
    PerformanceContext ctx { tempo, 4.0, "piano", 0 };
    const auto out = perform ({ { 69, 1.0, 1.0, 100, 1, {}, true } }, ResolvedPerformance {}, ctx);
    REQUIRE (out.size() == 1);
    CHECK_NEAR (out[0].startSec, 0.5, 1e-12);
    CHECK_NEAR (out[0].endSec, 1.0, 1e-12);
    CHECK_EQ (out[0].velocity, 100);
    CHECK_EQ ((int64_t) std::llround (out[0].startSec * 48000.0), (int64_t) 24000);
}

TEST (humanize_is_reproducible_with_a_seed)
{
    TempoMap tempo ({ { 0.0, 100.0, false } });
    PerformanceContext ctx { tempo, 4.0, "piano", 0 };
    const auto notes = scale (64);
    const auto a = perform (notes, humanized (12345), ctx);
    const auto b = perform (notes, humanized (12345), ctx);
    const auto c = perform (notes, humanized (54321), ctx);
    REQUIRE (a.size() == b.size());
    bool identical = true, differs = false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        identical = identical && a[i].startSec == b[i].startSec && a[i].velocity == b[i].velocity && a[i].endSec == b[i].endSec;
        differs = differs || a[i].startSec != c[i].startSec || a[i].velocity != c[i].velocity;
    }
    CHECK (identical);
    CHECK (differs);
}

TEST (humanize_stays_within_bounds_and_varies)
{
    TempoMap tempo ({ { 0.0, 100.0, false } });
    PerformanceContext ctx { tempo, 4.0, "piano", 0 };
    const auto notes = scale (400);
    const auto out = perform (notes, humanized (7), ctx);
    std::set<int> velocities;
    double maxOffset = 0.0;
    for (const auto& p : out)
    {
        const auto& n = notes[p.sourceIndex];
        const double nominal = tempo.secondsAt (n.startBeat);
        maxOffset = std::max (maxOffset, std::abs (p.startSec - nominal));
        CHECK (p.velocity >= 72 && p.velocity <= 88);
        velocities.insert (p.velocity);
        const double nominalLength = tempo.secondsAt (n.startBeat + n.durationBeat) - nominal;
        CHECK ((p.endSec - p.startSec) <= nominalLength * 1.04 + 1e-9);
    }
    CHECK (maxOffset <= 0.012 + 1e-12);
    CHECK (maxOffset > 0.004);
    CHECK (velocities.size() > 8);
}

TEST (editing_one_note_keeps_the_others_humanization)
{
    TempoMap tempo ({ { 0.0, 100.0, false } });
    PerformanceContext ctx { tempo, 4.0, "piano", 0 };
    auto notes = scale (16);
    const auto before = perform (notes, humanized (99), ctx);
    notes[5].pitch = 40;   // 한 음만 고친다
    const auto after = perform (notes, humanized (99), ctx);
    for (size_t i = 0; i < before.size(); ++i)
        if (before[i].sourceIndex != 5)
        {
            const auto& match = *std::find_if (after.begin(), after.end(), [&] (const PerformedNote& p) { return p.sourceIndex == before[i].sourceIndex; });
            CHECK_EQ (match.startSec, before[i].startSec);
            CHECK_EQ (match.velocity, before[i].velocity);
        }
}

TEST (swing_delays_off_beat_eighths)
{
    CHECK_NEAR (swingBeat (0.5, 1.0, 0.5), 0.5 + 0.5 / 3.0, 1e-12);
    CHECK_NEAR (swingBeat (1.5, 0.6, 0.5), 1.5 + 0.1, 1e-12);
    CHECK_NEAR (swingBeat (1.0, 1.0, 0.5), 1.0, 1e-12);
    TempoMap tempo ({ { 0.0, 60.0, false } });
    PerformanceContext ctx { tempo, 4.0, "drums", 0 };
    ResolvedPerformance p;
    p.swing = 1.0;
    const auto out = perform ({ { 42, 0.0, 0.5, 90, 1, {}, true }, { 42, 0.5, 0.5, 90, 1, {}, true } }, p, ctx);
    CHECK_NEAR (out[1].startSec, 0.5 + 1.0 / 6.0, 1e-9);
    CHECK_NEAR (out[0].endSec, 0.5 + 1.0 / 6.0 - 0.001, 1e-9);   // 같은 음 겹침 방지로 1ms 앞에서 끊긴다
}

TEST (accent_pattern_follows_the_bar)
{
    TempoMap tempo ({ { 0.0, 120.0, false } });
    PerformanceContext ctx { tempo, 4.0, "piano", 0 };
    ResolvedPerformance p;
    p.accentPattern = { 10, 0, 5, 0 };
    std::vector<Note> notes;
    for (int i = 0; i < 8; ++i)
        notes.push_back ({ 60 + i, (double) i, 1.0, 80, 1, {}, true });
    const auto out = perform (notes, p, ctx);
    const int expected[] = { 90, 80, 85, 80, 90, 80, 85, 80 };
    for (int i = 0; i < 8; ++i)
        CHECK_EQ (out[(size_t) i].velocity, expected[i]);
}

TEST (legato_overlap_extends_connected_notes_only)
{
    TempoMap tempo ({ { 0.0, 60.0, false } });
    PerformanceContext ctx { tempo, 4.0, "strings", 0 };
    ResolvedPerformance p;
    p.legatoOverlapMs = 50.0;
    const auto out = perform ({ { 60, 0.0, 1.0, 80, 1, {}, true }, { 62, 1.0, 1.0, 80, 1, {}, true }, { 64, 3.0, 1.0, 80, 1, {}, true } }, p, ctx);
    CHECK (out[0].legato);
    CHECK_NEAR (out[0].endSec, 1.05, 1e-9);
    CHECK (! out[1].legato);
    CHECK_NEAR (out[1].endSec, 2.0, 1e-9);
}

TEST (transpose_and_track_seed_override)
{
    PerformanceSettings comp, track;
    comp.humanizeSeed = 5;
    track.timingHumanizeMs = 3.0;
    auto r = resolvePerformance (comp, track, std::nullopt);
    CHECK_EQ (r.humanizeSeed, (uint64_t) 5);
    CHECK_NEAR (r.timingHumanizeMs, 3.0, 1e-12);
    r = resolvePerformance (comp, track, 77ull);
    CHECK_EQ (r.humanizeSeed, (uint64_t) 77);
    track.humanizeSeed = 9;
    r = resolvePerformance (comp, track, 77ull);
    CHECK_EQ (r.humanizeSeed, (uint64_t) 9);

    TempoMap tempo ({ { 0.0, 60.0, false } });
    PerformanceContext ctx { tempo, 4.0, "x", -12 };
    const auto out = perform ({ { 60, 0.0, 1.0, 80, 1, {}, true } }, ResolvedPerformance {}, ctx);
    CHECK_EQ (out[0].pitch, 48);
}
