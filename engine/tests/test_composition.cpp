#include "TestFramework.h"
#include "TestUtil.h"

#include "composition/Harmony.h"
#include "composition/Templates.h"

#include <algorithm>
#include <set>

using namespace mr;

TEST (parses_the_minimal_spec_example)
{
    auto r = mrtest::parse (R"({
      "tempo": 92, "timeSignature": [4, 4],
      "tracks": [ { "name": "Piano", "instrument": "piano",
                    "notes": [ { "pitch": 60, "startBeat": 0.0, "durationBeat": 1.0, "velocity": 72 } ] } ] })");
    REQUIRE (r.composition.has_value());
    const auto& c = *r.composition;
    CHECK_EQ (c.tempo.size(), (size_t) 1);
    CHECK_NEAR (c.tempo[0].bpm, 92.0, 1e-12);
    CHECK_EQ (c.tracks.size(), (size_t) 1);
    CHECK_EQ (c.tracks[0].notes[0].pitch, 60);
    CHECK_EQ (c.tracks[0].notes[0].velocity, 72);
    CHECK (r.diagnostics.warnings.empty());
}

TEST (reports_errors_with_json_paths)
{
    auto r = mrtest::parse (R"({
      "tempo": 92,
      "tracks": [
        { "name": "A", "instrument": "piano",
          "notes": [ { "pitch": 128, "startBeat": 0, "durationBeat": 1, "velocity": 72 },
                     { "pitch": 60, "startBeat": -1, "durationBeat": 0, "velocty": 70 } ] },
        { "name": "A", "instrument": "piano", "notes": [] } ] })");
    CHECK (! r.composition.has_value());
    const auto& d = r.diagnostics;
    CHECK (mrtest::hasError (d, "$.tracks[0].notes[0].pitch", "out of range"));
    CHECK (mrtest::hasError (d, "$.tracks[0].notes[1].startBeat", "out of range"));
    CHECK (mrtest::hasError (d, "$.tracks[0].notes[1].durationBeat", "greater than 0"));
    CHECK (mrtest::hasError (d, "$.tracks[0].notes[1].velocity", "missing"));
    CHECK (mrtest::hasWarning (d, "$.tracks[0].notes[1].velocty", "velocity"));
    CHECK (mrtest::hasError (d, "$.tracks[1].name", "duplicate"));
}

TEST (reports_invalid_json_with_line_numbers)
{
    auto r = mrtest::parse ("{ \"tempo\": 92,\n  \"tracks\": [ }");
    CHECK (! r.composition.has_value());
    REQUIRE (r.diagnostics.errors.size() == 1);
    CHECK (r.diagnostics.errors[0].message.find ("invalid JSON") != std::string::npos);
    CHECK (r.diagnostics.errors[0].message.find ("line 2, column") != std::string::npos);
    CHECK_EQ (describeJsonError ("3:7: error: Syntax error"), std::string ("invalid JSON at line 3, column 7: Syntax error"));
}

TEST (tempo_must_start_at_beat_zero_and_ascend)
{
    auto r = mrtest::parse (R"({ "tempo": [ { "beat": 1, "bpm": 90 }, { "beat": 0.5, "bpm": 100 } ],
      "tracks": [ { "name": "A", "instrument": "piano", "notes": [] } ] })");
    CHECK (mrtest::hasError (r.diagnostics, "$.tempo[0].beat", "beat 0"));
    CHECK (mrtest::hasError (r.diagnostics, "$.tempo[1].beat", "ascending"));
}

TEST (note_names_map_to_midi_numbers)
{
    CHECK_EQ (parseNoteName ("C4").value_or (-1), 60);
    CHECK_EQ (parseNoteName ("A4").value_or (-1), 69);
    CHECK_EQ (parseNoteName ("F#3").value_or (-1), 54);
    CHECK_EQ (parseNoteName ("Bb2").value_or (-1), 46);
    CHECK_EQ (parseNoteName ("e4").value_or (-1), 64);
    CHECK_EQ (parseNoteName ("C-1").value_or (-1), 0);
    CHECK_EQ (parseNoteName ("G9").value_or (-1), 127);
    CHECK (! parseNoteName ("A9").has_value());
    CHECK (! parseNoteName ("H4").has_value());
    CHECK (! parseNoteName ("C").has_value());

    auto r = mrtest::parse (R"({ "tempo": 100, "tracks": [ { "name": "A", "instrument": "piano",
      "notes": [ { "pitch": "E4", "startBeat": 0, "durationBeat": 1, "velocity": 64 },
                 { "pitch": "X9", "startBeat": 1, "durationBeat": 1, "velocity": 64 } ] } ] })");
    CHECK (mrtest::hasError (r.diagnostics, "$.tracks[0].notes[1].pitch", "invalid note name"));
}

TEST (every_planned_feature_is_now_accepted)
{
    auto r = mrtest::parse (R"({ "tempo": 100,
      "tracks": [ { "name": "A", "instrument": "piano", "mixer": { "gainDb": -3 },
                    "effects": [ { "type": "reverb" } ],
                    "cc1": [ { "beat": 0, "value": 40 } ],
                    "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 64, "articulation": "staccato" } ] } ],
      "master": { "targetLufs": -14 } })");
    // 1단계에서는 아래가 모두 "not implemented" 오류였다. 5단계까지 구현되어 이제는 받아들인다.
    CHECK (r.composition.has_value());
    CHECK (! mrtest::hasError (r.diagnostics, "$.tracks[0].mixer"));
    CHECK (! mrtest::hasError (r.diagnostics, "$.tracks[0].effects"));
    CHECK (! mrtest::hasError (r.diagnostics, "$.tracks[0].cc1"));                        // 4단계에서 구현되었다
    CHECK (! mrtest::hasError (r.diagnostics, "$.tracks[0].notes[0].articulation"));      // 악기와의 대조는 validate 와 render 가 한다
    CHECK (! mrtest::hasError (r.diagnostics, "$.master.targetLufs"));
    for (const auto& e : r.diagnostics.errors)
        CHECK (e.message.find ("not implemented") == std::string::npos);
}

TEST (automation_lane_interpolates)
{
    AutomationLane lane;
    lane.points = { { 0.0, 32.0, CurveShape::linear }, { 2.0, 55.0, CurveShape::step }, { 4.0, 84.0, CurveShape::smooth },
                    { 8.0, 48.0, CurveShape::exponential }, { 10.0, 96.0, CurveShape::linear } };
    CHECK_NEAR (lane.valueAt (-1.0), 32.0, 1e-12);       // 앞은 첫 값
    CHECK_NEAR (lane.valueAt (1.0), 43.5, 1e-12);        // linear 중간
    CHECK_NEAR (lane.valueAt (3.0), 55.0, 1e-12);        // step 은 앞 값을 유지
    CHECK_NEAR (lane.valueAt (4.0), 84.0, 1e-12);
    CHECK_NEAR (lane.valueAt (6.0), 66.0, 1e-12);        // smooth 의 중간은 선형과 같다
    CHECK (lane.valueAt (5.0) > 66.0 + 5.0);              // 앞쪽은 천천히 떠난다 (84 에 가깝다)
    CHECK_NEAR (lane.valueAt (9.0), 48.0 * std::sqrt (2.0), 1e-9);   // exponential: 기하 평균
    CHECK_NEAR (lane.valueAt (99.0), 96.0, 1e-12);       // 뒤는 마지막 값
    CHECK_EQ (controllerName (11), std::string ("cc11"));
    CHECK_EQ (parseControllerName ("cc64").value_or (-1), 64);
    CHECK_EQ (parseControllerName ("pitchBend").value_or (-1), kPitchBend);
    CHECK (! parseControllerName ("cc128").has_value());
    CHECK (! parseControllerName ("cc01").has_value());
}

TEST (metadata_loop_is_read_and_checked)
{
    auto r = mrtest::parse (R"({ "tempo": 120, "metadata": { "loop": { "startBeat": 16, "endBeat": 288, "comment": "x" } },
      "tracks": [ { "name": "A", "instrument": "synth.sine", "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 70 } ] } ] })");
    REQUIRE (r.composition.has_value());
    REQUIRE (r.composition->loop.has_value());
    CHECK_NEAR (r.composition->loop->startBeat, 16.0, 1e-12);
    CHECK_NEAR (r.composition->loop->endBeat, 288.0, 1e-12);

    auto none = mrtest::parse (R"({ "tempo": 120, "metadata": { "author": "x" },
      "tracks": [ { "name": "A", "instrument": "synth.sine", "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 70 } ] } ] })");
    REQUIRE (none.composition.has_value());
    CHECK (! none.composition->loop.has_value());
    CHECK (none.diagnostics.warnings.empty());

    auto bad = mrtest::parse (R"({ "tempo": 120, "metadata": { "loop": { "startBeat": 8, "endBeat": 8 } },
      "tracks": [ { "name": "A", "instrument": "synth.sine", "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 70 } ] } ] })");
    REQUIRE (bad.composition.has_value());
    CHECK (! bad.composition->loop.has_value());
    CHECK (mrtest::hasWarning (bad.diagnostics, "$.metadata.loop"));
}

TEST (templates_make_valid_empty_songs)
{
    Diagnostics d;
    const auto list = listTemplates (defaultTemplateDirectory(), d);
    CHECK (list.size() >= 13);
    CHECK (d.warnings.empty());
    REQUIRE (! list.empty());
    CHECK_EQ (list.front().id, std::string ("empty"));   // order 순
    bool sawLoop = false;
    for (const auto& t : list)
    {
        const auto text = songFromTemplate (t, "새 곡 " + t.id);
        const auto parsed = parseComposition (juce::String::fromUTF8 (text.c_str()), mrtest::outputDir().getChildFile (t.id + ".json"));
        CHECK (parsed.composition.has_value());
        if (! parsed.composition)
            continue;
        const auto& c = *parsed.composition;
        CHECK_EQ (c.title, "새 곡 " + t.id);
        CHECK_EQ (c.tracks.size(), t.trackNames.size());
        CHECK (! t.name.empty() && ! t.description.empty());
        CHECK_EQ (c.loop.has_value(), t.loop);
        sawLoop = sawLoop || t.loop;
        size_t notes = 0;
        for (const auto& track : c.tracks)
            notes += track.notes.size();
        CHECK_EQ (notes, (size_t) 0);
        juce::var root;
        juce::JSON::parse (juce::String::fromUTF8 (text.c_str()), root);
        CHECK_EQ (root.getProperty ("metadata", {}).getProperty ("template", {}).toString(), juce::String (t.id));   // 설명은 남기지 않는다
    }
    CHECK (sawLoop);

    const auto out = mrtest::outputDir().getChildFile ("from_template.json");
    out.deleteFile();
    createSongFromTemplate (list.front(), "처음", out);
    CHECK (loadComposition (out).composition.has_value());
    bool threw = false;
    try { createSongFromTemplate (list.front(), "다시", out); }
    catch (const Failure&) { threw = true; }
    CHECK (threw);   // 있는 파일을 덮어쓰지 않는다
}

TEST (chord_and_scale_tables_are_well_formed)
{
    std::set<std::string> ids;
    for (const auto& c : chordTypes())
    {
        CHECK (ids.insert (c.id).second);
        CHECK (! c.intervals.empty() && c.intervals.front() == 0);
        CHECK (std::is_sorted (c.intervals.begin(), c.intervals.end()) && std::adjacent_find (c.intervals.begin(), c.intervals.end()) == c.intervals.end());
        CHECK (std::find (chordGroups().begin(), chordGroups().end(), c.group) != chordGroups().end());
    }
    CHECK (chordTypes().size() >= 60);
    ids.clear();
    for (const auto& s : scaleTypes())
    {
        CHECK (ids.insert (s.id).second);
        CHECK (s.intervals.front() == 0 && s.intervals.back() < 12);
        CHECK (std::is_sorted (s.intervals.begin(), s.intervals.end()) && std::adjacent_find (s.intervals.begin(), s.intervals.end()) == s.intervals.end());
        CHECK (std::find (scaleGroups().begin(), scaleGroups().end(), s.group) != scaleGroups().end());
        CHECK (! s.name.empty() && ! scaleGroupName (s.group).empty());
    }
    CHECK (scaleTypes().size() >= 50);
    // 교회 선법은 장음계를 돌린 것이다
    const auto& major = findScaleType ("major")->intervals;
    const char* modes[] { "major", "dorian", "phrygian", "lydian", "mixolydian", "minor", "locrian" };
    for (int m = 0; m < 7; ++m)
    {
        std::vector<int> rotated;
        for (int k = 0; k < 7; ++k)
            rotated.push_back ((major[(size_t) ((m + k) % 7)] - major[(size_t) m] + 12) % 12);
        CHECK (findScaleType (modes[m])->intervals == rotated);
    }
}

TEST (chords_and_scales_make_pitches)
{
    const auto* m7 = findChordType ("m7");
    REQUIRE (m7 != nullptr);
    CHECK_EQ (chordSymbol (64, *m7), std::string ("Em7"));
    CHECK_EQ (chordSymbol (70, *findChordType ("maj")), std::string ("Bb"));
    CHECK_EQ (chordSymbol (68, *findChordType ("It+6")), std::string ("Ab It+6"));
    CHECK (chordPitches (64, *m7) == std::vector<int> ({ 64, 67, 71, 74 }));
    CHECK (chordPitches (64, *m7, 1) == std::vector<int> ({ 67, 71, 74, 76 }));
    CHECK (chordPitches (64, *m7, 3) == std::vector<int> ({ 74, 76, 79, 83 }));
    CHECK (chordPitches (64, *m7, 9) == chordPitches (64, *m7, 3));   // 음 수보다 많이 돌리지 않는다
    CHECK (chordPitches (122, *findChordType ("maj")) == std::vector<int> ({ 122, 126 }));   // 127 위(129)는 뺀다

    const auto* major = findScaleType ("major");
    REQUIRE (major != nullptr);
    CHECK (scalePitches (60, *major, 1, ScaleDirection::up) == std::vector<int> ({ 60, 62, 64, 65, 67, 69, 71, 72 }));
    CHECK (scalePitches (60, *major, 1, ScaleDirection::down) == std::vector<int> ({ 72, 71, 69, 67, 65, 64, 62, 60 }));
    CHECK_EQ (scalePitches (60, *major, 2, ScaleDirection::up).size(), (size_t) 15);
    const auto upDown = scalePitches (57, *findScaleType ("minor-pentatonic"), 1, ScaleDirection::upDown);
    CHECK (upDown == std::vector<int> ({ 57, 60, 62, 64, 67, 69, 67, 64, 62, 60, 57 }));

    const auto notes = scaleNotes (60, *major, 1, ScaleDirection::up, 8.0, 0.5, 90);
    REQUIRE (notes.size() == 8);
    CHECK_NEAR (notes[3].startBeat, 9.5, 1e-12);
    CHECK_NEAR (notes[3].durationBeat, 0.5, 1e-12);
    CHECK_EQ (notes[3].velocity, 90);
    const auto chord = chordNotes (64, *m7, 0, 4.0, 4.0, 80);
    CHECK_EQ (chord.size(), (size_t) 4);
    CHECK (std::all_of (chord.begin(), chord.end(), [] (const Note& n) { return n.startBeat == 4.0 && n.durationBeat == 4.0; }));
}
