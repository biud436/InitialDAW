#include "TestFramework.h"
#include "TestUtil.h"

#include "timing/TempoMap.h"

using namespace mr;

TEST (constant_tempo_converts_beats_to_seconds_and_samples)
{
    TempoMap t ({ { 0.0, 120.0, false } });
    CHECK_NEAR (t.secondsAt (1.0), 0.5, 1e-12);
    CHECK_NEAR (t.secondsAt (8.0), 4.0, 1e-12);
    CHECK_NEAR (t.beatAt (0.5), 1.0, 1e-12);
    CHECK_EQ (t.sampleAt (1.0, 48000.0), (int64_t) 24000);
    CHECK_EQ (t.sampleAt (1.0, 44100.0), (int64_t) 22050);
    TempoMap t92 ({ { 0.0, 92.0, false } });
    CHECK_NEAR (t92.secondsAt (92.0), 60.0, 1e-9);
}

TEST (tempo_steps_accumulate)
{
    TempoMap t ({ { 0.0, 120.0, false }, { 4.0, 60.0, false } });
    CHECK_NEAR (t.secondsAt (4.0), 2.0, 1e-12);
    CHECK_NEAR (t.secondsAt (5.0), 3.0, 1e-12);
    CHECK_NEAR (t.beatAt (3.0), 5.0, 1e-12);
    CHECK_NEAR (t.bpmAt (4.5), 60.0, 1e-12);
}

TEST (tempo_ramp_matches_numeric_integration)
{
    TempoMap t ({ { 0.0, 60.0, true }, { 4.0, 120.0, false } });
    // 60 → 120 bpm 를 4박 동안: ∫ 60/bpm(b) db = 4 ln 2
    CHECK_NEAR (t.secondsAt (4.0), 4.0 * std::log (2.0), 1e-9);
    double numeric = 0.0;
    const int steps = 200000;
    for (int i = 0; i < steps; ++i)
    {
        const double b = (i + 0.5) * 2.0 / steps;
        numeric += 60.0 / (60.0 + 15.0 * b) * (2.0 / steps);
    }
    CHECK_NEAR (t.secondsAt (2.0), numeric, 1e-6);
    CHECK_NEAR (t.bpmAt (2.0), 90.0, 1e-12);
    CHECK_NEAR (t.secondsAt (6.0), 4.0 * std::log (2.0) + 1.0, 1e-9);   // 램프 뒤는 120 고정
    for (double b : { 0.25, 1.0, 3.3, 4.0, 7.5 })
        CHECK_NEAR (t.beatAt (t.secondsAt (b)), b, 1e-9);
}

TEST (ritardando_slows_down)
{
    TempoMap t ({ { 0.0, 100.0, true }, { 8.0, 70.0, false } });
    const double first = t.secondsAt (1.0) - t.secondsAt (0.0);
    const double last = t.secondsAt (8.0) - t.secondsAt (7.0);
    CHECK (last > first * 1.3);
}
