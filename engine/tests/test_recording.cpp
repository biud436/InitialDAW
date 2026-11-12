#include "TestFramework.h"

#include "midi/MidiRecorder.h"

using namespace mr;

TEST (recorder_pairs_note_on_and_off_into_notes)
{
    MidiRecorder r;
    CHECK (! r.recording());
    r.start ({}, 4.0);
    CHECK (r.recording());
    r.noteOn (4.0, 60, 90);
    r.noteOn (4.5, 64, 70);
    r.noteOff (5.0, 60);
    r.noteOff (5.25, 64);
    const auto notes = r.notesSoFar (6.0);
    REQUIRE (notes.size() == 2);
    const auto take = r.finish (8.0);
    REQUIRE (take.notes.size() == 2);
    CHECK_EQ (take.notes[0].pitch, 60);
    CHECK_NEAR (take.notes[0].startBeat, 4.0, 1.0e-9);
    CHECK_NEAR (take.notes[0].durationBeat, 1.0, 1.0e-9);
    CHECK_EQ (take.notes[0].velocity, 90);
    CHECK (! take.notes[0].humanize);
    CHECK_EQ (take.notes[1].pitch, 64);
    CHECK_NEAR (take.notes[1].durationBeat, 0.75, 1.0e-9);
    CHECK (take.lanes.empty());
    CHECK (! r.recording());
    CHECK (r.empty());
}

TEST (recorder_closes_held_notes_at_the_end_and_on_retrigger)
{
    MidiRecorder r;
    r.start ({}, 0.0);
    r.noteOn (1.0, 60, 100);
    r.noteOn (2.0, 60, 110);   // 같은 음을 다시 눌렀다: 앞 음은 2.0 에서 끝난다
    const auto live = r.notesSoFar (2.5);
    REQUIRE (live.size() == 2);
    CHECK_NEAR (live[0].durationBeat, 1.0, 1.0e-9);
    CHECK_NEAR (live[1].durationBeat, 0.5, 1.0e-9);
    CHECK_EQ (r.noteCount(), (size_t) 2);
    const auto take = r.finish (3.0);
    REQUIRE (take.notes.size() == 2);
    CHECK_NEAR (take.notes[1].startBeat, 2.0, 1.0e-9);
    CHECK_NEAR (take.notes[1].durationBeat, 1.0, 1.0e-9);
    CHECK_EQ (take.notes[1].velocity, 110);
}

TEST (recorder_quantizes_starts_and_keeps_lengths)
{
    MidiRecorder r;
    RecordOptions o;
    o.quantizeGrid = 0.5;
    r.start (o, 0.0);
    r.noteOn (1.2, 62, 80);
    r.noteOff (1.9, 62);
    r.noteOn (2.76, 64, 80);
    r.noteOff (3.0, 64);
    const auto take = r.finish (4.0);
    REQUIRE (take.notes.size() == 2);
    CHECK_NEAR (take.notes[0].startBeat, 1.0, 1.0e-9);
    CHECK_NEAR (take.notes[0].durationBeat, 0.7, 1.0e-9);
    CHECK_NEAR (take.notes[1].startBeat, 3.0, 1.0e-9);
    CHECK_NEAR (take.notes[1].durationBeat, 0.24, 1.0e-9);
}

TEST (recorder_punch_keeps_only_notes_started_inside_the_range)
{
    MidiRecorder r;
    RecordOptions o;
    o.punch = std::make_pair (4.0, 8.0);
    r.start (o, 0.0);
    r.noteOn (3.5, 60, 80);
    r.noteOff (4.5, 60);
    r.noteOn (4.0, 62, 80);
    r.noteOff (5.0, 62);
    r.noteOn (7.9, 64, 80);
    r.noteOff (9.0, 64);
    r.noteOn (8.0, 65, 80);
    r.noteOff (9.0, 65);
    const auto take = r.finish (10.0);
    REQUIRE (take.notes.size() == 2);
    CHECK_EQ (take.notes[0].pitch, 62);
    CHECK_EQ (take.notes[1].pitch, 64);
}

TEST (recorder_ends_a_note_released_after_the_loop_wrapped_at_the_loop_end)
{
    MidiRecorder r;
    RecordOptions o;
    o.loopEndBeat = 8.0;
    r.start (o, 4.0);
    r.noteOn (7.5, 60, 80);
    r.noteOff (4.25, 60);   // 반복이 4박으로 되돌아간 뒤에 뗐다
    r.noteOn (4.5, 62, 80);
    const auto take = r.finish (5.0);
    REQUIRE (take.notes.size() == 2);
    CHECK_NEAR (take.notes[0].startBeat, 4.5, 1.0e-9);   // 시작 순서
    CHECK_NEAR (take.notes[1].startBeat, 7.5, 1.0e-9);
    CHECK_NEAR (take.notes[1].durationBeat, 0.5, 1.0e-9);

    MidiRecorder noLoop;
    noLoop.start ({}, 0.0);
    noLoop.noteOn (7.5, 60, 80);
    noLoop.noteOff (4.25, 60);
    const auto t2 = noLoop.finish (5.0);
    REQUIRE (t2.notes.size() == 1);
    CHECK_NEAR (t2.notes[0].durationBeat, 0.05, 1.0e-9);   // 반복 끝을 모르면 가장 짧은 음
}

TEST (recorder_records_controllers_as_thinned_lane_points)
{
    MidiRecorder r;
    r.start ({}, 0.0);
    r.controller (0.0, 64, 127);
    r.controller (0.1, 64, 127);   // 같은 값은 두지 않는다
    r.controller (1.0, 64, 0);
    r.controller (1.0, 64, 20);    // 같은 박은 새 값으로
    r.controller (0.5, 1, 30);
    r.controller (0.75, 1, 40);
    r.controller (0.25, 1, 10);    // 되돌아간 뒤: 박 순서로 끼운다
    r.controller (2.0, kPitchBend, 0.5);
    const auto take = r.finish (3.0);
    REQUIRE (take.lanes.count (64) == 1);
    const auto& pedal = take.lanes.at (64).points;
    REQUIRE (pedal.size() == 2);
    CHECK (pedal[0].curve == CurveShape::step);
    CHECK_NEAR (pedal[1].beat, 1.0, 1.0e-9);
    CHECK_NEAR (pedal[1].value, 20.0, 1.0e-9);
    const auto& mod = take.lanes.at (1).points;
    REQUIRE (mod.size() == 3);
    CHECK_NEAR (mod[0].beat, 0.25, 1.0e-9);
    CHECK (mod[0].curve == CurveShape::linear);
    CHECK_NEAR (mod[2].value, 40.0, 1.0e-9);
    CHECK_NEAR (take.lanes.at (kPitchBend).points[0].value, 0.5, 1.0e-9);

    MidiRecorder off;
    RecordOptions o;
    o.controllers = false;
    off.start (o, 0.0);
    off.controller (0.0, 64, 127);
    CHECK (off.finish (1.0).lanes.empty());
}

TEST (recorder_cancel_discards_everything_and_merge_keeps_beat_order)
{
    MidiRecorder r;
    r.start ({}, 0.0);
    r.noteOn (0.0, 60, 80);
    r.cancel();
    CHECK (! r.recording() && r.empty());
    r.noteOn (1.0, 60, 80);   // 녹음하지 않을 때는 받지 않는다
    CHECK (r.empty());

    AutomationLane lane;
    lane.points = { { 0.0, 10.0, CurveShape::linear }, { 2.0, 50.0, CurveShape::smooth }, { 4.0, 90.0, CurveShape::linear } };
    mergeLanePoints (lane, { { 2.0, 60.0, CurveShape::step }, { 1.0, 20.0, CurveShape::linear }, { 5.0, 100.0, CurveShape::linear } });
    REQUIRE (lane.points.size() == 5);
    CHECK_NEAR (lane.points[1].beat, 1.0, 1.0e-9);
    CHECK_NEAR (lane.points[2].value, 60.0, 1.0e-9);
    CHECK (lane.points[2].curve == CurveShape::step);
    CHECK_NEAR (lane.points[4].beat, 5.0, 1.0e-9);
}
