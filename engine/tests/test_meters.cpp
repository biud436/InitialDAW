#include "TestFramework.h"
#include "TestUtil.h"

#include "composition/Arrange.h"
#include "midi/MidiExport.h"
#include "midi/MidiImport.h"
#include "timing/MeterMap.h"

using namespace mr;

TEST (meter_map_counts_bars_across_changes)
{
    // 4/4 넷 마디(16박), 3/4 넷 마디(12박), 그 뒤 6/8 (마디가 3박)
    const MeterMap m (4, 4, { { 16.0, 3, 4 }, { 28.0, 6, 8 } });
    REQUIRE (m.segments().size() == 3u);
    CHECK_EQ (m.barAt (0.0), 0);
    CHECK_EQ (m.barAt (15.9), 3);
    CHECK_EQ (m.barAt (16.0), 4);
    CHECK_EQ (m.barAt (27.0), 7);
    CHECK_EQ (m.barAt (28.0), 8);
    CHECK_EQ (m.barAt (31.0), 9);
    CHECK_NEAR (m.barStart (5), 19.0, 1.0e-9);
    CHECK_NEAR (m.barStart (9), 31.0, 1.0e-9);
    CHECK_NEAR (m.beatInBar (30.5), 2.5, 1.0e-9);
    CHECK_NEAR (m.barBeatsAt (29.0), 3.0, 1.0e-9);
    CHECK_NEAR (m.roundToBar (17.4), 16.0, 1.0e-9);
    CHECK_NEAR (m.roundToBar (17.6), 19.0, 1.0e-9);
    CHECK (m.isBarLine (22.0) && ! m.isBarLine (23.0));
    const auto lines = m.barLines (12.0, 32.0);
    CHECK (lines == (std::vector<double> { 12.0, 16.0, 19.0, 22.0, 25.0, 28.0, 31.0 }));
    // 0 박 앞은 처음 박자로 거꾸로 센다 (카운트인)
    CHECK_EQ (m.barAt (-1.0), -1);
    CHECK_NEAR (m.barStart (-1), -4.0, 1.0e-9);
}

TEST (meter_changes_are_parsed_and_checked)
{
    auto r = mrtest::parse (R"({ "tempo": 120, "timeSignature": [4, 4], "meterChanges": [ { "beat": 16, "timeSignature": [3, 4] }, { "beat": 20, "timeSignature": [6, 8] } ],
                                 "tracks": [ { "name": "A", "instrument": "synth.piano", "notes": [] } ] })");
    CHECK (mrtest::hasError (r.diagnostics, "$.meterChanges[1].beat", "bar"));   // 3/4 의 마디 줄은 19, 22
    r = mrtest::parse (R"({ "tempo": 120, "meterChanges": [ { "beat": 8, "timeSignature": [5, 6] }, { "beat": 4, "timeSignature": [3, 4] } ],
                            "tracks": [ { "name": "A", "instrument": "synth.piano", "notes": [] } ] })");
    CHECK (mrtest::hasError (r.diagnostics, "$.meterChanges[0].timeSignature[1]"));
    CHECK (mrtest::hasError (r.diagnostics, "$.meterChanges[1].beat", "ascending"));
    r = mrtest::parse (R"({ "tempo": 120, "meterChanges": [ { "beat": 16, "timeSignature": [7, 8] } ],
                            "tracks": [ { "name": "A", "instrument": "synth.piano", "notes": [] } ] })");
    REQUIRE (r.composition.has_value());
    CHECK (r.diagnostics.warnings.empty());
    CHECK_NEAR (r.composition->meters().barStart (5), 19.5, 1.0e-9);
}

TEST (meter_changes_move_with_inserted_and_deleted_time)
{
    juce::var root;
    juce::JSON::parse (R"({ "tempo": 120, "timeSignature": [4, 4], "meterChanges": [ { "beat": 16, "timeSignature": [3, 4] } ],
                            "tracks": [ { "name": "A", "instrument": "synth.piano", "notes": [] } ] })", root);
    const auto inserted = withTimeInserted (root, 8, 8);
    CHECK_NEAR ((double) inserted["meterChanges"][0]["beat"], 24.0, 1.0e-9);
    // 3/4 를 품은 [12, 22) 를 지우면 3/4 가 12 박에서 시작한다 (뒤의 음악이 제 박자를 지킨다)
    const auto deleted = withTimeDeleted (root, 12, 22);
    CHECK_NEAR ((double) deleted["meterChanges"][0]["beat"], 12.0, 1.0e-9);
    // 처음을 지우면 곡의 처음 박자가 된다
    const auto front = withTimeDeleted (root, 0, 19);
    CHECK (front.getProperty ("meterChanges", juce::var()).isVoid());
    CHECK_EQ ((int) front["timeSignature"][0], 3);
}

TEST (meter_changes_survive_midi_export_and_import)
{
    auto r = mrtest::parse (R"({ "tempo": 100, "timeSignature": [4, 4], "meterChanges": [ { "beat": 8, "timeSignature": [5, 8] }, { "beat": 13, "timeSignature": [4, 4] } ],
      "tracks": [ { "name": "A", "instrument": "synth.piano", "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 80 } ] } ] })");
    REQUIRE (r.composition.has_value());
    const auto mid = mrtest::outputDir().getChildFile ("meters.mid");
    exportMidi (*r.composition, mid, false, std::nullopt);
    MidiImportReport report;
    const auto back = importMidi (mid, {}, report);
    REQUIRE (back["meterChanges"].isArray() && back["meterChanges"].getArray()->size() == 2);
    CHECK_NEAR ((double) back["meterChanges"][0]["beat"], 8.0, 1.0e-6);
    CHECK_EQ ((int) back["meterChanges"][0]["timeSignature"][0], 5);
    CHECK_EQ ((int) back["meterChanges"][0]["timeSignature"][1], 8);
    CHECK_NEAR ((double) back["meterChanges"][1]["beat"], 13.0, 1.0e-6);
    CHECK (report.warnings.empty());
}
