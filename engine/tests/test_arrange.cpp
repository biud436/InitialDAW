#include "TestFramework.h"
#include "TestUtil.h"

#include "composition/Arrange.h"
#include "composition/Document.h"
#include "rendering/OfflineRenderer.h"
#include "timing/TempoMap.h"

using namespace mr;

namespace
{

juce::var rootOf (const std::string& json)
{
    juce::var v;
    juce::JSON::parse (juce::String::fromUTF8 (json.c_str()), v);
    return v;
}

Composition parsedOf (const juce::var& root)
{
    auto r = parseCompositionValue (root, mrtest::sourceDir().getChildFile ("tests/inline.json"));
    for (const auto& e : r.diagnostics.errors)
        std::fprintf (stderr, "    parse error %s: %s\n", e.path.c_str(), e.message.c_str());
    return r.composition ? *r.composition : Composition();
}

bool parses (const juce::var& root)
{
    return parseCompositionValue (root, mrtest::sourceDir().getChildFile ("tests/inline.json")).composition.has_value();
}

std::vector<std::string> namesOf (const juce::var& root)
{
    std::vector<std::string> out;
    for (const auto& t : *root["tracks"].getArray())
        out.push_back (t["name"].toString().toStdString());
    return out;
}

const char* songJson = R"({ "title": "구간", "tempo": [ { "beat": 0, "bpm": 100 }, { "beat": 8, "bpm": 100, "ramp": true }, { "beat": 16, "bpm": 140 } ],
  "metadata": { "loop": { "startBeat": 4, "endBeat": 20 }, "author": "x" },
  "markers": [ { "beat": 0, "name": "도입" }, { "beat": 8, "name": "주제", "comment": "남는다" }, { "beat": 16, "name": "끝" } ],
  "render": { "endBeat": 24 },
  "buses": [ { "name": "Verb", "effects": [ { "type": "reverb", "automation": { "mix": [ { "beat": 0, "value": 0.2 }, { "beat": 16, "value": 0.6 } ] } } ] } ],
  "master": { "effects": [ { "type": "compressor", "sidechain": "Kick" } ] },
  "tracks": [
    { "name": "Kick", "instrument": "synth.drums",
      "notes": [ { "pitch": 36, "startBeat": 0, "durationBeat": 0.5, "velocity": 100 }, { "pitch": 36, "startBeat": 4, "durationBeat": 0.5, "velocity": 100 },
                 { "pitch": 36, "startBeat": 8, "durationBeat": 0.5, "velocity": 100 }, { "pitch": 36, "startBeat": 12, "durationBeat": 0.5, "velocity": 100 } ] },
    { "name": "Lead", "instrument": "synth.piano", "comment": "선율",
      "cc1": [ { "beat": 0, "value": 20 }, { "beat": 8, "value": 100 }, { "beat": 16, "value": 40 } ],
      "automation": { "cc64": [ { "beat": 0, "value": 127, "curve": "step" }, { "beat": 6, "value": 0, "curve": "step" }, { "beat": 6.5, "value": 127, "curve": "step" } ] },
      "effects": [ { "type": "compressor", "sidechain": "Kick" }, { "type": "filter", "automation": { "cutoffHz": [ { "beat": 2, "value": 400 }, { "beat": 10, "value": 8000 } ] } } ],
      "sends": [ { "bus": "Verb", "levelDb": -12, "automation": { "levelDb": [ { "beat": 0, "value": -30 }, { "beat": 12, "value": -6 } ] } } ],
      "notes": [ { "pitch": "C4", "startBeat": 1, "durationBeat": 2, "velocity": 80, "lyric": "가" },
                 { "pitch": "E4", "startBeat": 5, "durationBeat": 4, "velocity": 90, "comment": "걸친 음" },
                 { "pitch": "G4", "startBeat": 9, "durationBeat": 1, "velocity": 70 },
                 { "pitch": "C5", "startBeat": 14, "durationBeat": 6, "velocity": 75 } ] },
    { "name": "Bass", "instrument": "synth.bass", "notes": [ { "pitch": 36, "startBeat": 0, "durationBeat": 16, "velocity": 90 } ] } ] })";

const TrackSpec& track (const Composition& c, const std::string& name)
{
    for (const auto& t : c.tracks)
        if (t.name == name)
            return t;
    static TrackSpec none;
    return none;
}

double effectLaneValue (const juce::var& owner, int effect, const char* param, double beat)
{
    Diagnostics d;
    const auto lane = readAutomationLane (owner["effects"][effect]["automation"][param], "$", -1.0e9, 1.0e9, d);
    return lane.valueAt (beat);
}

double sendLaneValue (const juce::var& trackJson, double beat)
{
    Diagnostics d;
    return readAutomationLane (trackJson["sends"][0]["automation"]["levelDb"], "$", -200, 20, d).valueAt (beat);
}

} // namespace

TEST (arrange_unique_track_names)
{
    const auto root = rootOf (songJson);
    CHECK_EQ (uniqueTrackName (root, "Pad"), std::string ("Pad"));
    CHECK_EQ (uniqueTrackName (root, "Lead"), std::string ("Lead 2"));
    auto two = withTrackDuplicated (root, 1);
    CHECK_EQ (namesOf (two)[2], std::string ("Lead 2"));
    CHECK_EQ (uniqueTrackName (two, "Lead 2"), std::string ("Lead 3"));
    CHECK_EQ (uniqueTrackName (two, ""), std::string ("Track"));
    CHECK (parses (two));
}

TEST (arrange_track_list_edits_keep_sidechains_valid)
{
    const auto root = rootOf (songJson);
    REQUIRE (parses (root));

    // 이름을 바꾸면 사이드체인 키도 따라간다 (트랙과 마스터)
    auto renamed = withTrackRenamed (root, 0, "Kick In");
    REQUIRE (parses (renamed));
    CHECK_EQ (renamed["tracks"][1]["effects"][0]["sidechain"].toString(), juce::String ("Kick In"));
    CHECK_EQ (renamed["master"]["effects"][0]["sidechain"].toString(), juce::String ("Kick In"));
    CHECK_EQ (root["tracks"][0]["name"].toString(), juce::String ("Kick"));   // 원본은 그대로

    // 지우면 그 키를 쓰던 압축기는 보통 압축기가 된다
    int cleared = 0;
    auto removed = withTrackRemoved (root, 0, &cleared);
    CHECK_EQ (cleared, 2);
    REQUIRE (parses (removed));
    CHECK (! removed["tracks"][0]["effects"][0].getDynamicObject()->hasProperty ("sidechain"));
    CHECK (namesOf (removed) == (std::vector<std::string> { "Lead", "Bass" }));

    // 옮기기, 넣기 (겹친 이름은 바꾼다), 색
    auto moved = withTrackMoved (root, 2, 0);
    CHECK (namesOf (moved) == (std::vector<std::string> { "Bass", "Kick", "Lead" }));
    auto inserted = withTrackInserted (root, 1, newTrackJson ("Bass", "synth.pad"));
    CHECK (namesOf (inserted) == (std::vector<std::string> { "Kick", "Bass 2", "Lead", "Bass" }));
    REQUIRE (parses (inserted));
    CHECK (parsedOf (inserted).tracks[1].notes.empty());
    auto coloured = withTrackColour (root, 1, "#ff8800");
    CHECK_EQ (parsedOf (coloured).tracks[1].colour, std::string ("#ff8800"));
    CHECK (! withTrackColour (coloured, 1, "")["tracks"][1].getDynamicObject()->hasProperty ("color"));
    // 사본은 원래 객체의 다른 필드(주석, 가사)를 지킨다
    auto dup = withTrackDuplicated (root, 1);
    CHECK_EQ (dup["tracks"][2]["comment"].toString(), juce::String::fromUTF8 ("선율"));
    CHECK_EQ (dup["tracks"][2]["notes"][0]["lyric"].toString(), juce::String::fromUTF8 ("가"));
}

TEST (arrange_track_colour_is_checked)
{
    auto r = mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "A", "instrument": "synth.piano", "color": "orange", "notes": [] } ] })");
    CHECK (mrtest::hasError (r.diagnostics, "$.tracks[0].color", "#rrggbb"));
    r = mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "A", "instrument": "synth.piano", "color": "#12abEF", "notes": [] } ] })");
    CHECK (r.composition.has_value());
    CHECK (r.diagnostics.warnings.empty());
}

TEST (arrange_markers)
{
    auto r = mrtest::parse (R"({ "tempo": 120, "markers": [ { "beat": 4, "name": "B" }, { "beat": 0, "name": "A" } ],
                                 "tracks": [ { "name": "A", "instrument": "synth.piano", "notes": [] } ] })");
    CHECK (mrtest::hasError (r.diagnostics, "$.markers[1].beat", "ascending"));

    auto root = rootOf (songJson);
    const auto c = parsedOf (root);
    REQUIRE (c.markers.size() == 3u);
    CHECK_EQ (c.markers[1].name, std::string ("주제"));

    size_t index = 99;
    auto added = withMarkerAdded (root, 12, "다리", &index);
    CHECK_EQ (index, (size_t) 2);
    CHECK_EQ (added.getArray()->size(), 4);
    CHECK_EQ (added[2]["name"].toString(), juce::String::fromUTF8 ("다리"));
    root.getDynamicObject()->setProperty ("markers", added);
    auto moved = withMarkerMoved (root, 1, 20, &index);   // 주제를 맨 뒤로
    CHECK_EQ (index, (size_t) 3);
    CHECK_EQ (moved[3]["comment"].toString(), juce::String::fromUTF8 ("남는다"));
    CHECK_EQ (withMarkerRenamed (root, 0, "시작")[0]["name"].toString(), juce::String::fromUTF8 ("시작"));
    CHECK_EQ (withMarkerRemoved (root, 0).getArray()->size(), 3);
    CHECK (withMarkerRemoved (rootOf (R"({ "markers": [ { "beat": 0, "name": "x" } ] })"), 0).isVoid());   // 마지막 마커를 지우면 키를 지운다
}

TEST (arrange_lane_span_keeps_values_outside)
{
    AutomationLane lane;
    lane.points = { { 0, 0, CurveShape::linear }, { 8, 80, CurveShape::step }, { 12, 20, CurveShape::smooth }, { 20, 100, CurveShape::linear } };

    // 자르기: 구간 처음과 끝의 값
    const auto slice = sliceLane (lane, 4, 10);
    REQUIRE (! slice.empty());
    for (double b = 0; b <= 6.0; b += 0.25)
        CHECK_NEAR (slice.valueAt (b), lane.valueAt (4 + b - (b == 6.0 ? 1.0e-9 : 0.0)), 1.0e-6);

    // 내용 지우기: 구간 밖은 그대로, step 구간은 붙든다
    const auto cleared = replaceLaneSpan (lane, 2, 10, {}, 8);
    for (double b : { 0.0, 1.0, 1.99, 10.0, 11.0, 13.0, 19.0, 25.0 })
        CHECK_NEAR (cleared.valueAt (b), lane.valueAt (b), 1.0e-6);

    // 넣기: 앞은 그대로, 뒤는 length 만큼 밀린 값, 빈자리는 넣은 자리의 값
    AutomationLane hold;
    hold.points = { { 0, lane.valueAt (6), CurveShape::step } };
    const auto inserted = replaceLaneSpan (lane, 6, 6, hold, 4);
    for (double b = 0; b < 6.0; b += 0.5)
        CHECK_NEAR (inserted.valueAt (b), lane.valueAt (b), 1.0e-6);
    for (double b = 6.0; b < 10.0; b += 0.5)
        CHECK_NEAR (inserted.valueAt (b), lane.valueAt (6), 1.0e-6);
    for (double b = 6.0; b < 26.0; b += 0.5)
        CHECK_NEAR (inserted.valueAt (b + 4), lane.valueAt (b), 1.0e-6);

    // 지우기: [4, 9) 를 지우면 뒤가 5 박 당겨진다
    const auto deleted = replaceLaneSpan (lane, 4, 9, {}, 0);
    for (double b = 0; b < 4.0; b += 0.5)
        CHECK_NEAR (deleted.valueAt (b), lane.valueAt (b), 1.0e-6);
    for (double b = 9.0; b < 26.0; b += 0.5)
        CHECK_NEAR (deleted.valueAt (b - 5), lane.valueAt (b), 1.0e-6);
}

TEST (arrange_copy_and_paste_range)
{
    const auto root = rootOf (songJson);
    const auto before = parsedOf (root);

    // Lead 와 Bass 의 [4, 8) 을 복사해 [16, 20) 에 붙인다
    const auto clip = copyRange (root, { 1, 2 }, 4, 8);
    REQUIRE (clip.tracks.size() == 2u);
    CHECK_EQ (clip.tracks[0].notes.size(), 1);   // E4 (5박), 끝은 8박에서 자른다
    CHECK_NEAR ((double) clip.tracks[0].notes[0]["durationBeat"], 3.0, 1.0e-9);
    CHECK_EQ (clip.tracks[1].notes.size(), 0);   // 베이스는 0박에서 시작했다
    CHECK (clip.tracks[0].lanes.count ("cc1") == 1);
    CHECK (clip.tracks[0].lanes.count ("effects/1:filter/cutoffHz") == 1);
    CHECK (clip.tracks[0].lanes.count ("sends/Verb") == 1);

    const auto pasted = withClipPasted (root, clip, 1, 16);
    REQUIRE (parses (pasted));
    const auto after = parsedOf (pasted);
    const auto& lead = track (after, "Lead");
    // 16박의 C5 (14박에서 시작해 들어오는 음)는 16박에서 잘리고 붙인 E4 가 17박에
    bool found = false;
    for (const auto& n : lead.notes)
    {
        if (n.pitch == 72)
            CHECK_NEAR (n.durationBeat, 2.0, 1.0e-9);
        if (n.pitch == 64 && std::abs (n.startBeat - 17.0) < 1.0e-9)
            found = true;
    }
    CHECK (found);
    CHECK_EQ (lead.notes.size(), track (before, "Lead").notes.size() + 1);
    // 붙인 자리의 곡선은 원래 자리와 같고, 구간 밖은 그대로다
    const auto& cc1Before = track (before, "Lead").automation.at (1);
    const auto& cc1After = lead.automation.at (1);
    for (double b = 0; b < 4.0; b += 0.25)
        CHECK_NEAR (cc1After.valueAt (16 + b), cc1Before.valueAt (4 + b), 1.0e-6);
    for (double b = 0; b < 16.0; b += 0.5)
        CHECK_NEAR (cc1After.valueAt (b), cc1Before.valueAt (b), 1.0e-6);
    for (double b = 0; b < 4.0; b += 0.5)
        CHECK_NEAR (effectLaneValue (pasted["tracks"][1], 1, "cutoffHz", 16 + b), effectLaneValue (root["tracks"][1], 1, "cutoffHz", 4 + b), 1.0e-3);
    for (double b = 0; b < 4.0; b += 0.5)
        CHECK_NEAR (sendLaneValue (pasted["tracks"][1], 16 + b), sendLaneValue (root["tracks"][1], 4 + b), 1.0e-6);
    // 다른 트랙(Kick)은 그대로
    CHECK_EQ (writeJson (pasted["tracks"][0]), writeJson (root["tracks"][0]));
    // 가사와 주석이 사본에 남는다
    CHECK_EQ (pasted["tracks"][1]["notes"][4]["comment"].toString(), juce::String::fromUTF8 ("걸친 음"));
    CHECK_NEAR ((double) pasted["tracks"][1]["notes"][4]["startBeat"], 17.0, 1.0e-9);
}

TEST (arrange_clear_range)
{
    const auto root = rootOf (songJson);
    const auto cleared = withRangeCleared (root, { 1 }, 4, 12);
    REQUIRE (parses (cleared));
    const auto lead = track (parsedOf (cleared), "Lead");
    REQUIRE (lead.notes.size() == 2u);   // 1박의 C4 와 14박의 C5
    CHECK_EQ (lead.notes[0].pitch, 60);
    CHECK_EQ (lead.notes[1].pitch, 72);
    const auto& cc64 = lead.automation.at (64);
    CHECK_NEAR (cc64.valueAt (3.9), 127.0, 1.0e-9);
    CHECK_NEAR (cc64.valueAt (12.5), 127.0, 1.0e-9);
}

TEST (arrange_insert_and_delete_time)
{
    const auto root = rootOf (songJson);
    const auto before = parsedOf (root);
    const auto inserted = withTimeInserted (root, 6, 4);
    REQUIRE (parses (inserted));
    const auto after = parsedOf (inserted);

    // 음: 6박 뒤는 4박 밀리고, 5박의 E4 (9박까지)는 6박에서 잘린다
    const auto& lead = track (after, "Lead");
    CHECK_NEAR (lead.notes[1].durationBeat, 1.0, 1.0e-9);
    CHECK_NEAR (lead.notes[2].startBeat, 13.0, 1.0e-9);
    CHECK_NEAR (track (after, "Kick").notes[2].startBeat, 12.0, 1.0e-9);
    CHECK_NEAR (track (after, "Bass").notes[0].durationBeat, 6.0, 1.0e-9);
    // 곡선: 앞은 같고 뒤는 밀린 값 (트랙, 효과, 센드, 버스)
    const auto& a = track (before, "Lead").automation.at (1);
    const auto& b = lead.automation.at (1);
    for (double x = 0; x < 6.0; x += 0.5)
        CHECK_NEAR (b.valueAt (x), a.valueAt (x), 1.0e-6);
    for (double x = 6.0; x < 20.0; x += 0.5)
        CHECK_NEAR (b.valueAt (x + 4), a.valueAt (x), 1.0e-6);
    for (double x = 6.0; x < 20.0; x += 0.5)
        CHECK_NEAR (effectLaneValue (inserted["buses"][0], 0, "mix", x + 4), effectLaneValue (root["buses"][0], 0, "mix", x), 1.0e-6);
    for (double x = 6.0; x < 20.0; x += 0.5)
        CHECK_NEAR (sendLaneValue (inserted["tracks"][1], x + 4), sendLaneValue (root["tracks"][1], x), 1.0e-6);
    // 템포: 램프 구간(8~16박)이 12~20박으로
    const TempoMap t0 (before.tempo), t1 (after.tempo);
    for (double x = 6.0; x < 24.0; x += 0.5)
        CHECK_NEAR (t1.bpmAt (x + 4), t0.bpmAt (x), 1.0e-6);
    for (double x = 0; x < 6.0; x += 0.5)
        CHECK_NEAR (t1.bpmAt (x), t0.bpmAt (x), 1.0e-6);
    // 마커, 반복 구간, 끝 박
    REQUIRE (after.markers.size() == 3u);
    CHECK_NEAR (after.markers[1].beat, 12.0, 1.0e-9);
    CHECK_NEAR (after.loop->startBeat, 4.0, 1.0e-9);
    CHECK_NEAR (after.loop->endBeat, 24.0, 1.0e-9);
    CHECK_NEAR (*after.render.endBeat, 28.0, 1.0e-9);
    CHECK_EQ (inserted["metadata"]["author"].toString(), juce::String ("x"));

    // 넣은 자리를 지우면 음과 곡선, 템포, 마커가 되돌아온다 (잘린 음은 잘린 채)
    const auto back = withTimeDeleted (inserted, 6, 10);
    REQUIRE (parses (back));
    const auto restored = parsedOf (back);
    for (size_t i = 0; i < before.tracks.size(); ++i)
        for (size_t n = 0; n < before.tracks[i].notes.size(); ++n)
            CHECK_NEAR (restored.tracks[i].notes[n].startBeat, before.tracks[i].notes[n].startBeat, 1.0e-9);
    for (double x = 0; x < 24.0; x += 0.25)
        CHECK_NEAR (track (restored, "Lead").automation.at (1).valueAt (x), a.valueAt (x), 1.0e-6);
    const TempoMap t2 (restored.tempo);
    for (double x = 0; x < 24.0; x += 0.5)
        CHECK_NEAR (t2.bpmAt (x), t0.bpmAt (x), 1.0e-6);
    CHECK_NEAR (restored.markers[1].beat, 8.0, 1.0e-9);
    CHECK_NEAR (restored.loop->endBeat, 20.0, 1.0e-9);
    CHECK_NEAR (*restored.render.endBeat, 24.0, 1.0e-9);

    // 지우기: [8, 12) 의 마커와 음이 사라지고 뒤가 당겨진다
    const auto deleted = withTimeDeleted (root, 8, 12);
    REQUIRE (parses (deleted));
    const auto d = parsedOf (deleted);
    CHECK_EQ (d.markers.size(), (size_t) 2);
    CHECK_NEAR (d.markers[1].beat, 12.0, 1.0e-9);
    CHECK_EQ (track (d, "Kick").notes.size(), (size_t) 3);
    CHECK_NEAR (d.loop->endBeat, 16.0, 1.0e-9);
    const TempoMap t3 (d.tempo);
    CHECK_NEAR (t3.bpmAt (12), 140.0, 1.0e-6);
    CHECK_NEAR (t3.bpmAt (4), 100.0, 1.0e-6);

    // 처음에 넣기: 템포의 첫 점은 0 박에 남는다
    const auto atStart = withTimeInserted (root, 0, 4);
    REQUIRE (parses (atStart));
    CHECK_NEAR (parsedOf (atStart).tempo.front().beat, 0.0, 1.0e-12);
}

TEST (arrange_moves_mixer_curves)
{
    auto root = rootOf (R"({ "tempo": 120, "master": { "mixer": { "gainDb": -1, "automation": { "gainDb": [ { "beat": 0, "value": 0 }, { "beat": 16, "value": -12 } ] } } },
      "tracks": [ { "name": "A", "instrument": "synth.piano", "mixer": { "pan": 0.2, "automation": { "pan": [ { "beat": 0, "value": -1 }, { "beat": 8, "value": 1 } ] } },
                    "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 80 } ] } ] })");
    const auto before = parsedOf (root);
    const auto inserted = withTimeInserted (root, 4, 4);
    REQUIRE (parses (inserted));
    const auto after = parsedOf (inserted);
    for (double b = 4.0; b < 16.0; b += 0.5)
    {
        CHECK_NEAR (after.tracks[0].mixer.panLane.valueAt (b + 4), before.tracks[0].mixer.panLane.valueAt (b), 1.0e-6);
        CHECK_NEAR (after.master.mixer.gainLane.valueAt (b + 4), before.master.mixer.gainLane.valueAt (b), 1.0e-6);
    }
    CHECK_NEAR (after.tracks[0].mixer.pan, 0.2, 1.0e-9);   // 고정값과 다른 필드는 그대로
    // 구간 복사는 트랙의 믹서 곡선도 옮긴다
    const auto clip = copyRange (root, { 0 }, 0, 4);
    CHECK (clip.tracks[0].lanes.count ("mixer/pan") == 1);
    const auto pasted = parsedOf (withClipPasted (root, clip, 0, 8));
    CHECK_NEAR (pasted.tracks[0].mixer.panLane.valueAt (10), before.tracks[0].mixer.panLane.valueAt (2), 1.0e-6);
}

TEST (arrange_changed_properties)
{
    const auto root = rootOf (songJson);
    const auto renamed = withTrackRenamed (root, 1, "Melody");
    const auto changed = changedProperties (root, renamed);
    REQUIRE (changed.size() == 1u);
    CHECK (changed[0] == juce::Identifier ("tracks"));
    const auto renamedKick = withTrackRenamed (root, 0, "K");
    CHECK_EQ (changedProperties (root, renamedKick).size(), (size_t) 2);   // tracks, master
}

TEST (track_templates_keep_the_sound_and_drop_the_music)
{
    auto root = juce::JSON::parse (R"({ "tempo": 120, "instruments": [ { "sampler.bell": { "type": "sfz", "file": "samples/bell.sfz" } } ],
        "buses": [ { "name": "Hall" } ],
        "tracks": [ { "name": "Kick", "instrument": "synth.edm-drums", "notes": [] },
                    { "name": "Bell", "instrument": "sampler.bell", "transpose": 12, "folder": "Keys", "color": "#ff8800",
                      "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 90 } ], "cc1": [ { "beat": 0, "value": 64 } ],
                      "mixer": { "gainDb": -3, "pan": 0.2, "automation": { "gainDb": [ { "beat": 0, "value": -6 } ] } },
                      "sends": [ { "bus": "Hall", "levelDb": -10, "automation": { "levelDb": [ { "beat": 0, "value": -20 } ] } } ],
                      "effects": [ { "type": "compressor", "sidechain": "Kick", "automation": { "thresholdDb": [ { "beat": 0, "value": -10 } ] } },
                                   { "type": "eq", "lowCutHz": 100 } ] } ] })");
    const auto t = trackTemplateOf (root, 1, "Bell pad", juce::File ("/songs/demo"));
    const auto track = t["track"];
    CHECK (t["name"].toString() == "Bell pad");
    CHECK (! track.getDynamicObject()->hasProperty ("notes") && ! track.getDynamicObject()->hasProperty ("cc1") && ! track.getDynamicObject()->hasProperty ("folder"));
    CHECK ((double) track["mixer"]["gainDb"] == -3.0 && ! track["mixer"].getDynamicObject()->hasProperty ("automation"));
    CHECK (! track["sends"][0].getDynamicObject()->hasProperty ("automation") && ! track["effects"][0].getDynamicObject()->hasProperty ("automation"));
    CHECK ((int) track["transpose"] == 12 && track["color"].toString() == "#ff8800");
    CHECK (t["instrument"]["file"].toString() == juce::File ("/songs/demo/samples/bell.sfz").getFullPathName());
    // 다른 곡: 버스와 키 트랙이 없으면 센드와 사이드체인을 빼고, 악기 정의를 더하고, 이름이 겹치지 않게
    auto other = juce::JSON::parse (R"({ "tempo": 100, "tracks": [ { "name": "Bell", "instrument": "grand-piano", "notes": [] } ] })");
    int dropped = 0;
    const auto added = withTrackFromTemplate (other, 1, t, &dropped);
    REQUIRE (! added.isVoid());
    const auto parsed = mrtest::parse (juce::JSON::toString (added).toStdString());
    for (const auto& e : parsed.diagnostics.errors)
        std::fprintf (stderr, "    %s: %s\n", e.path.c_str(), e.message.c_str());
    REQUIRE (parsed.composition.has_value());
    const auto& made = parsed.composition->tracks[1];
    CHECK (made.name == "Bell 2" && made.instrument == "sampler.bell" && made.notes.empty() && made.effects.size() == 2);
    CHECK (made.sends.empty() && dropped == 2);
    CHECK (! added["tracks"][1]["effects"][0].getDynamicObject()->hasProperty ("sidechain"));
    CHECK (added["instruments"][0]["sampler.bell"]["file"].toString().endsWith ("bell.sfz"));
    CHECK (withTrackFromTemplate (other, 0, juce::var()).isVoid());
    // 기본 템플릿은 모두 곡에 들어가 검사를 통과한다
    for (const auto& [id, templ] : factoryTrackTemplates())
    {
        const auto song = withTrackFromTemplate (other, 1, templ);
        const auto r = mrtest::parse (juce::JSON::toString (song).toStdString());
        if (! CHECK (r.composition.has_value() && r.diagnostics.warnings.empty()))
            std::fprintf (stderr, "    template %s\n", id.c_str());
    }
    CHECK (factoryTrackTemplates().size() >= 7);
}

namespace
{

// 세 구간 (A 4/4 두 마디, B 3/4 두 마디, C 4/4 두 마디): 템포, 박자, 조, 곡선, 버스 곡선, 반복 구간이 구간마다 다르다
const char* sectionSong = R"({ "tempo": [ { "beat": 0, "bpm": 120 }, { "beat": 14, "bpm": 90 } ], "timeSignature": [4, 4],
  "meterChanges": [ { "beat": 8, "timeSignature": [3, 4] }, { "beat": 14, "timeSignature": [4, 4] } ],
  "key": "C major", "keyChanges": [ { "beat": 8, "key": "A minor" } ],
  "metadata": { "loop": { "startBeat": 8, "endBeat": 14 } },
  "markers": [ { "beat": 0, "name": "A" }, { "beat": 8, "name": "B" }, { "beat": 14, "name": "C" } ],
  "render": { "endBeat": 22 },
  "buses": [ { "name": "Hall", "effects": [ { "type": "reverb", "automation": { "mix": [
      { "beat": 0, "value": 0.2, "curve": "step" }, { "beat": 8, "value": 0.5, "curve": "step" }, { "beat": 14, "value": 0.8, "curve": "step" } ] } } ] } ],
  "tracks": [
    { "name": "Lead", "instrument": "synth.piano",
      "cc1": [ { "beat": 0, "value": 20, "curve": "step" }, { "beat": 8, "value": 60, "curve": "linear" }, { "beat": 14, "value": 100, "curve": "step" } ],
      "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 2, "velocity": 80 }, { "pitch": 62, "startBeat": 4, "durationBeat": 2, "velocity": 80 },
                 { "pitch": 64, "startBeat": 8, "durationBeat": 2, "velocity": 80 }, { "pitch": 65, "startBeat": 11, "durationBeat": 2, "velocity": 80 },
                 { "pitch": 67, "startBeat": 14, "durationBeat": 2, "velocity": 80 }, { "pitch": 69, "startBeat": 18, "durationBeat": 2, "velocity": 80 } ] } ] })";

// 같은 값의 다른 모양 (0 과 0.0, 적지 않은 기본 곡선 linear) 을 맞춘 JSON 글자
juce::var canonical (const juce::var& v)
{
    if (v.isDouble() && (double) v == std::floor ((double) v) && std::abs ((double) v) < 1.0e9)
        return (int) (double) v;
    if (const auto* arr = v.getArray())
    {
        juce::Array<juce::var> out;
        for (const auto& e : *arr)
            out.add (canonical (e));
        return out;
    }
    if (const auto* o = v.getDynamicObject())
    {
        auto* out = new juce::DynamicObject();
        for (const auto& prop : o->getProperties())
            if (! (prop.name.toString() == "curve" && prop.value.toString() == "linear"))
                out->setProperty (prop.name, canonical (prop.value));
        return juce::var (out);
    }
    return v;
}

std::string starts (const juce::var& root)
{
    std::string s;
    for (const auto& n : *root["tracks"][0]["notes"].getArray())
        s += (s.empty() ? "" : " ") + n["pitch"].toString().toStdString() + "@" + juce::String ((double) n["startBeat"]).toStdString();
    return s;
}

} // namespace

TEST (arrange_sections_of_a_song)
{
    const auto root = rootOf (sectionSong);
    const auto sections = sectionsOf (root);
    REQUIRE (sections.size() == 3);
    CHECK (sections[0].name == "A" && sections[0].start == 0.0 && sections[0].end == 8.0);
    CHECK (sections[1].name == "B" && sections[1].start == 8.0 && sections[1].end == 14.0);
    CHECK (sections[2].name == "C" && sections[2].start == 14.0 && sections[2].end == 22.0);
    // render.endBeat 가 없으면 마지막 음의 끝을 마디로 올린다
    auto open = root.clone();
    open.getDynamicObject()->removeProperty ("render");
    CHECK_NEAR (songEndBeat (open), 22.0, 1.0e-9);
}

TEST (arrange_section_moved_carries_tempo_meter_key_and_curves)
{
    const auto root = rootOf (sectionSong);
    std::string error;
    const auto moved = withSectionMoved (root, 1, 0, &error);   // B 를 A 앞으로
    REQUIRE (! moved.isVoid());
    CHECK (parses (moved));
    CHECK_EQ (starts (moved), std::string ("64@0 65@3 60@6 62@10 67@14 69@18"));
    const auto c = parsedOf (moved);
    REQUIRE (c.markers.size() == 3);
    CHECK (c.markers[0].name == "B" && c.markers[0].beat == 0.0 && c.markers[1].name == "A" && c.markers[1].beat == 6.0 && c.markers[2].beat == 14.0);
    // 박자: B 의 3/4 두 마디, A 의 4/4 두 마디, C 의 4/4
    CHECK (c.beatsPerBar == 3 && c.beatUnit == 4);
    const auto meters = c.meters();
    CHECK (meters.isBarLine (6.0) && meters.barBeatsAt (6.0) == 4.0 && meters.barAt (14.0) == 4 && meters.barBeatsAt (14.0) == 4.0);
    // 조: A minor 로 시작, A 는 C major, C 는 A minor (원래 C 에 쓰이던 조)
    CHECK (keyAt (c, 0.0) == parseKeyName ("A minor") && keyAt (c, 6.0) == parseKeyName ("C major") && keyAt (c, 14.0) == parseKeyName ("A minor"));
    // 템포: B 와 A 는 120, C 는 90
    const TempoMap tempo (c.tempo);
    CHECK (tempo.bpmAt (1.0) == 120.0 && tempo.bpmAt (7.0) == 120.0 && tempo.bpmAt (15.0) == 90.0);
    // 곡선: B 의 60 에서 100 으로 오르는 linear, A 의 20, C 의 100
    const auto& cc1 = c.tracks[0].automation.at (1);
    CHECK_NEAR (cc1.valueAt (0.0), 60.0, 1.0e-9);
    CHECK_NEAR (cc1.valueAt (3.0), 80.0, 1.0e-9);
    CHECK_NEAR (cc1.valueAt (6.5), 20.0, 1.0e-9);
    CHECK_NEAR (cc1.valueAt (15.0), 100.0, 1.0e-9);
    // 버스 곡선과 반복 구간도 B 를 따라간다
    CHECK_NEAR (effectLaneValue (moved["buses"][0], 0, "mix", 1.0), 0.5, 1.0e-9);
    CHECK_NEAR (effectLaneValue (moved["buses"][0], 0, "mix", 7.0), 0.2, 1.0e-9);
    CHECK_NEAR (effectLaneValue (moved["buses"][0], 0, "mix", 15.0), 0.8, 1.0e-9);
    CHECK (c.loop && c.loop->startBeat == 0.0 && c.loop->endBeat == 6.0);
    CHECK (c.render.endBeat && *c.render.endBeat == 22.0);

    // 되돌리면 곡 JSON 이 같다 (숫자의 0 과 0.0 같은 모양은 맞춘다)
    const auto back = withSectionMoved (moved, 1, 0, &error);
    REQUIRE (! back.isVoid());
    CHECK_EQ (writeJson (canonical (back)), writeJson (canonical (root)));
    // 맨 뒤로 옮기기와 그대로인 옮기기
    const auto last = withSectionMoved (root, 0, 3, &error);
    CHECK_EQ (starts (last), std::string ("64@0 65@3 67@6 69@10 60@14 62@18"));
    CHECK_EQ (writeJson (withSectionMoved (root, 1, 1, &error)), writeJson (root));
}

TEST (arrange_section_duplicated_and_deleted)
{
    const auto root = rootOf (sectionSong);
    std::string error;
    const auto twice = withSectionDuplicated (root, 1, &error);
    REQUIRE (parses (twice));
    CHECK_EQ (starts (twice), std::string ("60@0 62@4 64@8 65@11 64@14 65@17 67@20 69@24"));
    const auto c = parsedOf (twice);
    CHECK (c.markers.size() == 4 && c.markers[2].name == "B" && c.markers[2].beat == 14.0 && c.markers[3].beat == 20.0);
    CHECK (c.meters().barBeatsAt (14.0) == 3.0 && c.meters().barBeatsAt (20.0) == 4.0 && *c.render.endBeat == 28.0);
    CHECK (TempoMap (c.tempo).bpmAt (19.0) == 120.0 && TempoMap (c.tempo).bpmAt (21.0) == 90.0);
    const auto gone = withSectionDeleted (root, 1, &error);
    REQUIRE (parses (gone));
    CHECK_EQ (starts (gone), std::string ("60@0 62@4 67@8 69@12"));
    const auto g = parsedOf (gone);
    CHECK (g.meterChanges.empty() && ! g.loop.has_value() && *g.render.endBeat == 16.0);   // 반복 구간은 지운 구간에 있었다
    CHECK (g.keyChanges.size() == 1 && g.keyChanges[0].beat == 8.0 && keyAt (g, 8.0) == parseKeyName ("A minor"));   // C 에 쓰이던 조
    CHECK (TempoMap (g.tempo).bpmAt (9.0) == 90.0);
    // 구간이 없거나 경계가 마디 줄이 아니면
    CHECK (withSectionDeleted (rootOf (R"({ "tempo": 120, "tracks": [] })"), 0, &error).isVoid());
    auto offBar = root.clone();
    offBar["markers"][1].getDynamicObject()->setProperty ("beat", 9);
    CHECK (withSectionMoved (offBar, 1, 0, &error).isVoid() && error.find ("bar") != std::string::npos);
}

TEST (arrange_moved_section_renders_like_the_pieces_joined)
{
    // 자리와 상관없이 같은 소리를 내는 오디오 클립 하나가 곡 전체에 걸친다 (구간 경계에서 나뉜다). 구간 A (120 BPM), B (90 BPM), C (120 BPM)
    const double rate = 48000.0;
    const auto wav = mrtest::outputDir().getChildFile ("sections_tone.wav");
    {
        juce::AudioBuffer<float> b (1, (int) (14.0 * rate));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / rate;
            const double f = 220.0 * std::pow (2.0, std::floor (t * 2.0) / 12.0);   // 반 초마다 반음씩
            b.setSample (0, i, (float) (0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * f * t) * (0.5 + 0.5 * std::fmod (t, 0.5) * 2.0)));
        }
        writeWav (wav, b, b.getNumSamples(), rate, 32);
    }
    const auto root = rootOf (R"({ "tempo": [ { "beat": 0, "bpm": 120 }, { "beat": 8, "bpm": 90 }, { "beat": 16, "bpm": 120 } ],
        "markers": [ { "beat": 0, "name": "A" }, { "beat": 8, "name": "B" }, { "beat": 16, "name": "C" } ], "render": { "endBeat": 24, "tailSeconds": 1 },
        "tracks": [ { "name": "Tape", "instrument": "audio", "clips": [ { "file": ")" + wav.getFullPathName().toStdString()
                              + R"(", "startBeat": 0, "lengthSeconds": 13.333333333333334 } ] } ] })");
    std::string error;
    const auto moved = withSectionMoved (root, 1, 0, &error);   // B, A, C
    REQUIRE (! moved.isVoid());
    REQUIRE (moved["tracks"][0]["clips"].getArray()->size() == 3);
    auto render = [] (const juce::var& song)
    {
        std::vector<float> left;
        RenderOptions o;
        o.sampleRate = 48000.0;
        o.onAudio = [&left] (const juce::AudioBuffer<float>& audio, double) { left.assign (audio.getReadPointer (0), audio.getReadPointer (0) + audio.getNumSamples()); };
        renderComposition (parsedOf (song), o);
        return left;
    };
    const auto original = render (root), rearranged = render (moved);
    REQUIRE (original.size() > (size_t) (rate * 13.0) && rearranged.size() == original.size());
    // 원래 렌더의 B (4~9.333초), A (0~4초), C (9.333초~) 를 이어 붙인 것
    const int64_t a1 = 192000, b1 = 448000;
    std::vector<float> expected;
    expected.insert (expected.end(), original.begin() + a1, original.begin() + b1);
    expected.insert (expected.end(), original.begin(), original.begin() + a1);
    expected.insert (expected.end(), original.begin() + b1, original.end());
    // 나뉜 클립의 양 끝은 짧은 페이드가 걸린다: 새 경계의 앞 10 ms 와 뒤 2초는 빼고 견준다
    const std::vector<int64_t> joins { 0, b1 - a1, b1 };
    double worst = 0.0, peak = 0.0;
    int64_t compared = 0;
    for (size_t i = 0; i < expected.size(); ++i)
    {
        bool skip = false;
        for (const auto j : joins)
            skip = skip || ((int64_t) i >= j - 480 && (int64_t) i < j + 96000);
        if (skip)
            continue;
        worst = std::max (worst, (double) std::abs (expected[i] - rearranged[i]));
        peak = std::max (peak, (double) std::abs (expected[i]));
        ++compared;
    }
    CHECK (compared > (int64_t) (rate * 6.0));
    const double worstDb = worst > 0.0 ? 20.0 * std::log10 (worst) : -300.0;
    std::fprintf (stderr, "  largest difference %.1f dBFS (peak %.1f dBFS)\n", worstDb, 20.0 * std::log10 (peak));
    CHECK (peak > 0.1);
    CHECK (worstDb < -90.0);
}
