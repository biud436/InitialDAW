#include "TestFramework.h"
#include "TestUtil.h"

#include "composition/Document.h"
#include "composition/NoteEdits.h"

using namespace mr;

namespace
{

CompositionDocument document (const std::string& json)
{
    std::string error;
    auto doc = CompositionDocument::fromText (juce::String::fromUTF8 (json.c_str()), mrtest::sourceDir().getChildFile ("tests/inline.json"), error);
    if (! doc)
        std::fprintf (stderr, "    document error: %s\n", error.c_str());
    return std::move (*doc);
}

Composition parsed (const CompositionDocument& doc)
{
    auto r = doc.parse();
    if (! r.composition)
        for (const auto& e : r.diagnostics.errors)
            std::fprintf (stderr, "    parse error %s: %s\n", e.path.c_str(), e.message.c_str());
    return *r.composition;
}

const char* songJson = R"({ "title": "시험 곡", "comment": "이 주석은 \"남는다\"", "metadata": { "author": "tester", "tags": ["a", "b"] },
  "tempo": 96, "timeSignature": [3, 4],
  "tracks": [
    { "name": "Lead", "instrument": "synth.piano", "comment": "멜로디",
      "cc11": [ { "beat": 0, "value": 60 }, { "beat": 4, "value": 100, "curve": "smooth" } ],
      "notes": [ { "pitch": "Bb4", "startBeat": 0, "durationBeat": 1, "velocity": 90, "comment": "첫 음" },
                 { "pitch": 67, "startBeat": 1.5, "durationBeat": 0.5, "velocity": 80, "humanize": false },
                 { "pitch": "C5", "startBeat": 2, "durationBeat": 1, "velocity": 70, "channel": 2 } ] },
    { "name": "Bass", "instrument": "synth.bass", "automation": { "cc1": [ { "beat": 0, "value": 30 } ] },
      "notes": [ { "pitch": 36, "startBeat": 0, "durationBeat": 3.75, "velocity": 100 } ] } ] })";

std::vector<EditNote> notesOf (const CompositionDocument& doc, size_t track)
{
    const auto c = parsed (doc);
    return editNotes (doc.track (track), c.tracks[track]);
}

NoteSelection all (const std::vector<EditNote>& notes)
{
    NoteSelection s;
    for (size_t i = 0; i < notes.size(); ++i)
        s.push_back (i);
    return s;
}

} // namespace

TEST (document_round_trip_keeps_unknown_fields_and_key_order)
{
    auto doc = document (songJson);
    const auto text = doc.toJson();
    CHECK (text.find ("\"metadata\": {\"author\": \"tester\", \"tags\": [\"a\", \"b\"]}") != std::string::npos);
    CHECK (text.find ("\"comment\": \"이 주석은 \\\"남는다\\\"\"") != std::string::npos);
    CHECK (text.find ("\"title\": \"시험 곡\"") < text.find ("\"tempo\": 96"));
    CHECK (text.find ("\"timeSignature\": [3, 4]") != std::string::npos);
    // 값이 스칼라뿐인 객체는 한 줄 (음표 하나가 한 줄)
    CHECK (text.find ("{\"pitch\": \"Bb4\", \"startBeat\": 0, \"durationBeat\": 1, \"velocity\": 90, \"comment\": \"첫 음\"}") != std::string::npos);
    CHECK (text.find ("\"startBeat\": 1.5") != std::string::npos);

    // 다시 읽으면 같은 곡, 다시 쓰면 같은 글
    std::string error;
    auto again = CompositionDocument::fromText (juce::String::fromUTF8 (text.c_str()), doc.file(), error);
    REQUIRE (again.has_value());
    CHECK_EQ (again->toJson(), text);
    const auto a = parsed (doc), b = parsed (*again);
    CHECK_EQ (b.tracks.size(), a.tracks.size());
    CHECK_EQ (b.tracks[0].notes.size(), (size_t) 3);
    CHECK_EQ (b.tracks[0].notes[2].channel, 2);
    CHECK_EQ (b.tracks[0].automation.at (11).points.size(), (size_t) 2);
    CHECK_EQ (b.beatsPerBar, 3);
}

TEST (document_writes_numbers_in_the_shortest_exact_form)
{
    juce::var v;
    juce::JSON::parse (juce::String::fromUTF8 (R"({ "a": 16.0, "b": 0.1, "c": 351.56, "d": 36, "e": -0.000123, "f": 1e21, "g": [true, false, null], "h": "탭\t줄\n" })"), v);
    const auto text = writeJson (v);
    CHECK_EQ (text, std::string ("{\"a\": 16.0, \"b\": 0.1, \"c\": 351.56, \"d\": 36, \"e\": -0.000123, \"f\": 1e+21, \"g\": [true, false, null], \"h\": \"탭\\t줄\\n\"}"));
    juce::var back;
    juce::JSON::parse (juce::String::fromUTF8 (text.c_str()), back);
    CHECK_EQ ((double) back["c"], 351.56);
    CHECK_EQ ((double) back["e"], -0.000123);
}

TEST (document_writes_edited_notes_into_their_original_objects)
{
    auto doc = document (songJson);
    const auto notes = notesOf (doc, 0);
    REQUIRE (notes.size() == 3);
    auto added = addNotes (notes, { Note { 62, 0.5, 0.25, 64 } });
    REQUIRE (added.selection.size() == 1);
    CHECK_EQ (added.selection[0], (size_t) 1);   // 시작 순서의 자리 (0 다음, 1.5 앞)
    auto& edited = added.notes;
    edited[0].note.pitch += 1;          // Bb4 -> B4 (이름으로 남는다)
    edited[0].note.startBeat = 4.0;     // 원래 정수였으므로 정수로
    edited[2].note.pitch -= 2;          // 67 -> 65 (번호로 남는다)
    edited[3].note.velocity = 30;
    doc.setTrack (0, withNotes (doc.track (0), edited));

    const auto text = doc.toJson();
    CHECK (text.find ("{\"pitch\": \"B4\", \"startBeat\": 4, \"durationBeat\": 1, \"velocity\": 90, \"comment\": \"첫 음\"}") != std::string::npos);
    CHECK (text.find ("{\"pitch\": 62, \"startBeat\": 0.5, \"durationBeat\": 0.25, \"velocity\": 64}") != std::string::npos);
    CHECK (text.find ("{\"pitch\": 65, \"startBeat\": 1.5, \"durationBeat\": 0.5, \"velocity\": 80, \"humanize\": false}") != std::string::npos);
    CHECK (text.find ("\"velocity\": 30, \"channel\": 2") != std::string::npos);

    CHECK_EQ (noteName (68, true), std::string ("Ab4"));
    CHECK_EQ (noteName (70), std::string ("A#4"));
    CHECK_EQ (noteName (0), std::string ("C-1"));

    const auto c = parsed (doc);
    CHECK_EQ (c.tracks[0].notes.size(), (size_t) 4);
    CHECK_EQ (c.tracks[0].notes[0].pitch, 71);
    CHECK_EQ (c.tracks[0].notes[1].pitch, 62);
    CHECK (! c.tracks[0].notes[2].humanize);
}

TEST (document_keeps_the_accidental_style_of_note_names)
{
    auto doc = document (R"({ "tempo": 100, "tracks": [ { "name": "A", "instrument": "synth.piano", "notes": [
      { "pitch": "Bb4", "startBeat": 0, "durationBeat": 1, "velocity": 90 }, { "pitch": "C#4", "startBeat": 1, "durationBeat": 1, "velocity": 90 },
      { "pitch": "E4", "startBeat": 2, "durationBeat": 1, "velocity": 90 } ] } ] })");
    auto notes = notesOf (doc, 0);
    notes[0].note.pitch -= 2;   // Bb4 -> Ab4
    notes[1].note.pitch += 2;   // C#4 -> D#4
    notes[2].note.pitch += 1;   // E4 -> F4
    doc.setTrack (0, withNotes (doc.track (0), notes));
    const auto text = doc.toJson();
    CHECK (text.find ("\"Ab4\"") != std::string::npos);
    CHECK (text.find ("\"D#4\"") != std::string::npos);
    CHECK (text.find ("\"F4\"") != std::string::npos);
}

TEST (document_writes_controller_curves_where_they_were)
{
    auto doc = document (songJson);
    AutomationLane lane;
    lane.points = { { 0.0, 10.0, CurveShape::linear }, { 2.0, 90.0, CurveShape::step } };
    doc.setTrack (0, withLane (doc.track (0), 11, lane));   // 트랙 바로 아래의 cc11
    doc.setTrack (0, withLane (doc.track (0), 1, lane));    // 새 곡선은 automation 안에
    doc.setTrack (1, withLane (doc.track (1), 1, AutomationLane {}));   // 빈 곡선은 지우고, 빈 automation 도 지운다
    const auto text = doc.toJson();
    CHECK (text.find ("\"cc11\": [\n") != std::string::npos);
    CHECK (text.find ("{\"beat\": 2.0, \"value\": 90.0, \"curve\": \"step\"}") != std::string::npos);
    CHECK (text.find ("\"automation\": {\n") != std::string::npos);

    const auto c = parsed (doc);
    CHECK (c.tracks[0].automation.at (11).points[1].curve == CurveShape::step);
    CHECK_EQ (c.tracks[0].automation.at (1).points.size(), (size_t) 2);
    CHECK_EQ (c.tracks[1].automation.count (1), (size_t) 0);
    CHECK (! doc.track (1).getDynamicObject()->hasProperty ("automation"));
}

TEST (document_tempo_and_time_signature)
{
    auto doc = document (songJson);
    doc.setProperty ("tempo", tempoJson ({ TempoPoint { 0.0, 120.0, false } }));
    doc.setProperty ("timeSignature", timeSignatureJson (6, 8));
    auto c = parsed (doc);
    CHECK_EQ (c.tempo.size(), (size_t) 1);
    CHECK_EQ (c.tempo[0].bpm, 120.0);
    CHECK_EQ (c.barBeats(), 3.0);
    doc.setProperty ("tempo", tempoJson ({ TempoPoint { 0.0, 100.0, true }, TempoPoint { 8.0, 140.0, false } }));
    c = parsed (doc);
    CHECK_EQ (c.tempo.size(), (size_t) 2);
    CHECK (c.tempo[0].ramp);
}

TEST (document_round_trips_the_example_songs)
{
    for (const auto* path : { "examples/minimal.json", "examples/piano_demo.json", "examples/orchestral_demo.json" })
    {
        const auto file = mrtest::sourceDir().getChildFile (path);
        std::string error;
        auto doc = CompositionDocument::load (file, error);
        REQUIRE (doc.has_value());
        const auto original = loadComposition (file);
        REQUIRE (original.composition.has_value());
        const auto text = doc->toJson();
        auto again = CompositionDocument::fromText (juce::String::fromUTF8 (text.c_str()), file, error);
        REQUIRE (again.has_value());
        const auto c = parsed (*again);
        CHECK_EQ (c.tracks.size(), original.composition->tracks.size());
        for (size_t t = 0; t < c.tracks.size(); ++t)
        {
            const auto& a = original.composition->tracks[t].notes;
            const auto& b = c.tracks[t].notes;
            REQUIRE (a.size() == b.size());
            for (size_t i = 0; i < a.size(); ++i)
                if (! CHECK (a[i].pitch == b[i].pitch && a[i].startBeat == b[i].startBeat && a[i].durationBeat == b[i].durationBeat
                             && a[i].velocity == b[i].velocity))
                    break;
        }
        CHECK_EQ (again->toJson(), text);
    }
}

TEST (note_edits_move_copy_and_resize_stay_in_range)
{
    auto doc = document (songJson);
    const auto notes = notesOf (doc, 0);   // Bb4(70)@0, 67@1.5, C5(72)@2

    // 시작 0 인 음이 있으므로 왼쪽으로는 0 까지만, 위로는 127 까지만
    auto moved = moveNotes (notes, { 0, 2 }, -1.0, 100);
    CHECK_EQ (moved.notes[0].note.startBeat, 0.0);
    CHECK_EQ (moved.notes[2].note.startBeat, 2.0);
    CHECK_EQ (moved.notes[2].note.pitch, 127);
    CHECK_EQ (moved.notes[0].note.pitch, 125);
    CHECK_EQ (moved.notes[1].note.pitch, 67);   // 고르지 않은 음
    CHECK (moved.selection == (NoteSelection { 0, 2 }));

    auto copied = copyNotes (notes, { 1 }, 4.0, 12);
    CHECK_EQ (copied.notes.size(), (size_t) 4);
    CHECK (copied.selection == (NoteSelection { 3 }));
    CHECK_EQ (copied.notes[3].note.pitch, 79);
    CHECK_EQ (copied.notes[3].note.startBeat, 5.5);
    CHECK (copied.notes[3].source.isVoid());
    CHECK (! copied.notes[3].note.humanize);   // 음의 설정은 사본에도

    auto resized = resizeNotes (notes, all (notes), -0.9, 0.125);
    CHECK_EQ (resized.notes[0].note.durationBeat, 0.125);
    CHECK_NEAR (resized.notes[2].note.durationBeat, 0.125, 1e-12);

    auto loud = offsetVelocity (notes, all (notes), 50);
    CHECK_EQ (loud.notes[0].note.velocity, 127);
    CHECK_EQ (setVelocity (notes, { 1 }, 0).notes[1].note.velocity, 1);
}

TEST (note_edits_quantize_delete_paste_and_select)
{
    std::vector<EditNote> notes;
    for (const double start : { 0.1, 0.9, 2.3, 3.74 })
        notes.push_back ({ Note { 60, start, 0.5, 90 }, juce::var() });

    auto q = quantizeNotes (notes, {}, 0.5);   // 선택이 없으면 모두
    CHECK_EQ (q.notes[0].note.startBeat, 0.0);
    CHECK_EQ (q.notes[1].note.startBeat, 1.0);
    CHECK_EQ (q.notes[2].note.startBeat, 2.5);
    CHECK_EQ (q.notes[3].note.startBeat, 3.5);
    CHECK_EQ (q.notes[3].note.durationBeat, 0.5);
    CHECK_EQ (quantizeNotes (notes, { 2 }, 1.0).notes[1].note.startBeat, 0.9);

    auto d = deleteNotes (notes, { 0, 2 });
    CHECK_EQ (d.notes.size(), (size_t) 2);
    CHECK_EQ (d.notes[1].note.startBeat, 3.74);
    CHECK (d.selection.empty());

    auto p = pasteNotes (notes, { Note { 64, 10.0, 1.0, 80 }, Note { 67, 10.5, 1.0, 80 } }, 1.0);
    CHECK (p.selection == (NoteSelection { 2, 3 }));
    CHECK_EQ (p.notes[2].note.startBeat, 1.0);
    CHECK_EQ (p.notes[3].note.startBeat, 1.5);
    CHECK_EQ (p.notes[4].note.startBeat, 2.3);

    CHECK (notesInRange (notes, 0.7, 2.4, 59, 61) == (NoteSelection { 1, 2 }));
    CHECK (notesInRange (notes, 0.5, 0.6, 59, 61) == (NoteSelection { 0 }));   // 걸치기만 해도
    CHECK (notesInRange (notes, 0.0, 10.0, 61, 70).empty());
    CHECK_EQ (selectedNotes (notes, { 3 }).front().startBeat, 3.74);
}

TEST (document_writes_mixer_sends_and_effects_into_their_objects)
{
    auto doc = document (R"({ "tempo": 120,
      "buses": [ { "name": "hall", "comment": "큰 방", "effects": [ { "type": "reverb", "mix": 1, "comment": "IR 없음" } ] } ],
      "tracks": [
        { "name": "A", "instrument": "synth.piano", "mixer": { "gainDb": -6, "note": "keep" },
          "sends": [ { "bus": "hall", "levelDb": -12, "comment": "작게" } ], "notes": [] },
        { "name": "B", "instrument": "synth.pad", "notes": [] } ] })");

    // 원래 있던 필드는 바꾸고, 없던 필드는 기본값과 다를 때만 더한다
    MixerSettings m;
    m.gainDb = -3.5;
    m.pan = 0.25;
    doc.setTrack (0, withMixer (doc.track (0), m));
    CHECK_EQ (writeJson (doc.track (0)["mixer"]), std::string (R"({"gainDb": -3.5, "note": "keep", "pan": 0.25})"));
    CHECK (! doc.track (0)["mixer"].hasProperty ("mute"));
    doc.setTrack (1, withMixer (doc.track (1), MixerSettings {}));
    CHECK (! doc.track (1).hasProperty ("mixer"));   // 모두 기본값이면 mixer 를 더하지 않는다
    MixerSettings muted;
    muted.mute = true;
    doc.setTrack (1, withMixer (doc.track (1), muted));
    CHECK_EQ (writeJson (doc.track (1)["mixer"]), std::string (R"({"mute": true})"));

    // 센드: 같은 자리의 객체에 쓰고(주석이 남는다), 더하고, 지운다
    SendSpec s0 { "hall", -9.0, true };
    SendSpec s1 { "hall", -20.0, false };
    doc.setTrack (0, withSends (doc.track (0), { s0, s1 }));
    CHECK_EQ (writeJson (doc.track (0)["sends"]),
              std::string ("[\n {\"bus\": \"hall\", \"levelDb\": -9, \"comment\": \"작게\", \"preFader\": true},\n {\"bus\": \"hall\", \"levelDb\": -20.0}\n]"));
    doc.setTrack (0, withSends (doc.track (0), {}));
    CHECK (! doc.track (0).hasProperty ("sends"));

    // 효과: 매개변수 바꾸기, 지우기, 목록 바꾸기 (주석이 남는다)
    auto buses = doc.property ("buses");
    auto effects = effectsJson (buses[0]);
    effects[0] = withEffectParameter (effects[0], "decaySeconds", 3.2);
    effects[0] = withEffectParameter (effects[0], "mix", juce::var());
    effects[0] = withEffectParameter (effects[0], "bypass", true);
    effects.append (juce::JSON::parse (R"({ "type": "eq", "highCutHz": 6000 })"));
    buses.getArray()->set (0, withEffects (buses[0], *effects.getArray()));
    doc.setProperty ("buses", buses);
    CHECK_EQ (writeJson (doc.property ("buses")[0]["effects"]),
              std::string ("[\n {\"type\": \"reverb\", \"comment\": \"IR 없음\", \"decaySeconds\": 3.2, \"bypass\": true},\n {\"type\": \"eq\", \"highCutHz\": 6000}\n]"));
    CHECK (! withEffects (doc.track (1), {}).hasProperty ("effects"));

    // 마스터가 없던 곡에 마스터 페이더
    MixerSettings master;
    master.gainDb = -2.0;
    doc.setProperty ("master", withMixer (doc.property ("master"), master));
    const auto c = parsed (doc);
    CHECK_NEAR (c.master.mixer.gainDb, -2.0, 1.0e-12);
    CHECK (c.buses[0].effects[0].bypass);
    CHECK_NEAR (c.tracks[0].mixer.pan, 0.25, 1.0e-12);
    CHECK (c.tracks[1].mixer.mute);
    CHECK (c.tracks[0].sends.empty());
}

TEST (document_writes_plugin_state_instruments_and_articulations)
{
    auto doc = document (R"({ "tempo": 120, "instruments": "extra.json",
      "tracks": [ { "name": "EP", "instrument": "dexed", "plugin": { "program": 3, "parameters": { "Cutoff": 0.4 }, "comment": "warm" },
                    "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 90, "articulation": "staccato" },
                               { "pitch": 62, "startBeat": 1, "durationBeat": 1, "velocity": 90 } ] } ] })");
    // 상태를 쓰면 program 과 parameters 를 지우고 주석은 남긴다
    doc.setTrack (0, withPluginState (doc.track (0), "AAEC"));
    CHECK_EQ (writeJson (doc.track (0)["plugin"]), std::string (R"({"comment": "warm", "state": "AAEC"})"));
    // 악기를 바꾸면 앞 악기의 플러그인 설정을 지운다
    CHECK (! withInstrument (doc.track (0), "synth.ep").hasProperty ("plugin"));
    CHECK (withInstrument (doc.track (0), "synth.ep")["instrument"] == juce::var ("synth.ep"));
    // 곡 안의 정의: 파일 경로는 남기고 객체를 더한다, 두 번째는 같은 객체에
    auto instruments = withInlineInstrument (doc.property ("instruments"), "surge", juce::JSON::parse (R"({ "type": "vst3", "pluginName": "Surge XT" })"));
    instruments = withInlineInstrument (instruments, "dls", juce::JSON::parse (R"({ "type": "au", "plugin": "AudioUnit:Synths/aumu,dls ,appl" })"));
    CHECK_EQ (writeJson (instruments), std::string ("[\n \"extra.json\",\n {\n  \"surge\": {\"type\": \"vst3\", \"pluginName\": \"Surge XT\"},\n  \"dls\": {\"type\": \"au\", \"plugin\": \"AudioUnit:Synths/aumu,dls ,appl\"}\n }\n]"));
    // 아티큘레이션: 바꾸고, 지우고, 원래 객체의 다른 필드는 남는다
    auto notes = notesOf (doc, 0);
    auto changed = setArticulation (notes, { 0, 1 }, "legato");
    doc.setTrack (0, withNotes (doc.track (0), changed.notes));
    CHECK (doc.track (0)["notes"][0]["articulation"] == juce::var ("legato"));
    CHECK (doc.track (0)["notes"][1]["articulation"] == juce::var ("legato"));
    auto cleared = setArticulation (notesOf (doc, 0), { 0 }, "");
    doc.setTrack (0, withNotes (doc.track (0), cleared.notes));
    CHECK (! doc.track (0)["notes"][0].hasProperty ("articulation"));
    CHECK_EQ (writeJson (doc.track (0)["notes"][1]), std::string (R"({"pitch": 62, "startBeat": 1, "durationBeat": 1, "velocity": 90, "articulation": "legato"})"));
}

TEST (lyrics_are_parsed_kept_through_edits_and_saved)
{
    auto doc = document (R"({ "tempo": 72, "tracks": [ { "name": "Lead", "instrument": "synth.piano", "notes": [
      { "pitch": "Eb4", "startBeat": 0, "durationBeat": 1, "velocity": 80, "lyric": "늦", "comment": "첫 음" },
      { "pitch": 68, "startBeat": 1, "durationBeat": 1, "velocity": 80, "lyric": "은" },
      { "pitch": 70, "startBeat": 2, "durationBeat": 0.5, "velocity": 80, "lyric": "-" },
      { "pitch": 72, "startBeat": 3, "durationBeat": 1, "velocity": 80 } ] } ] })");
    const auto c = parsed (doc);
    REQUIRE (c.tracks[0].notes.size() == 4);
    CHECK_EQ (c.tracks[0].notes[0].lyric, std::string ("늦"));
    CHECK_EQ (c.tracks[0].notes[1].lyric, std::string ("은"));
    CHECK (isLyricExtension (c.tracks[0].notes[2].lyric));
    CHECK (c.tracks[0].notes[3].lyric.empty());
    for (const auto& w : doc.parse().diagnostics.warnings)   // 아는 필드
        CHECK (w.path.find ("lyric") == std::string::npos);

    // 옮기기, 길이, 세기: 원래 객체에 쓰고 가사와 주석은 남는다
    auto notes = notesOf (doc, 0);
    auto edited = moveNotes (notes, { 0, 1 }, 4.0, 2).notes;
    edited = resizeNotes (edited, { 2 }, 0.5, 0.125).notes;
    edited = setVelocity (edited, { 0 }, 64).notes;
    doc.setTrack (0, withNotes (doc.track (0), edited));
    const auto text = doc.toJson();
    CHECK (text.find (R"({"pitch": "F4", "startBeat": 4, "durationBeat": 1, "velocity": 64, "lyric": "늦", "comment": "첫 음"})") != std::string::npos);
    CHECK (text.find (R"({"pitch": 70, "startBeat": 5, "durationBeat": 1, "velocity": 80, "lyric": "은"})") != std::string::npos);
    CHECK (text.find (R"({"pitch": 70, "startBeat": 2, "durationBeat": 1.0, "velocity": 80, "lyric": "-"})") != std::string::npos);
    CHECK (text.find (R"({"pitch": 72, "startBeat": 3, "durationBeat": 1, "velocity": 80})") != std::string::npos);

    // 저장 왕복: 다시 읽으면 같은 가사, 다시 쓰면 같은 글
    std::string error;
    auto again = CompositionDocument::fromText (juce::String::fromUTF8 (text.c_str()), doc.file(), error);
    REQUIRE (again.has_value());
    CHECK_EQ (again->toJson(), text);
    const auto back = parsed (*again);
    CHECK_EQ (back.tracks[0].notes[0].lyric, std::string ("늦"));
    CHECK_EQ (back.tracks[0].notes[0].startBeat, 4.0);

    // 사본과 붙여 넣은 음은 새 객체지만 가사를 가져간다. 새로 그린 음에는 없다
    auto copied = copyNotes (notesOf (*again, 0), { 0 }, 8.0, 0);
    CHECK (copied.notes.back().source.isVoid());
    CHECK_EQ (copied.notes.back().note.lyric, std::string ("늦"));
    auto pasted = pasteNotes (copied.notes, selectedNotes (copied.notes, { 1 }), 16.0);
    auto drawn = addNotes (pasted.notes, { Note { 60, 20.0, 1.0, 90 } });
    again->setTrack (0, withNotes (again->track (0), drawn.notes));
    const auto out = again->toJson();
    CHECK (out.find (R"({"pitch": 65, "startBeat": 12.0, "durationBeat": 1.0, "velocity": 64, "lyric": "늦"})") != std::string::npos);
    CHECK (out.find (R"({"pitch": 70, "startBeat": 16.0, "durationBeat": 1.0, "velocity": 80, "lyric": "은"})") != std::string::npos);
    CHECK (out.find (R"({"pitch": 60, "startBeat": 20.0, "durationBeat": 1.0, "velocity": 90})") != std::string::npos);

    // 가사를 바꾸고 지우기 (원래 객체의 다른 필드는 남는다)
    auto relyric = notesOf (*again, 0);
    relyric[0].note.lyric = "편";
    relyric[1].note.lyric.clear();
    again->setTrack (0, withNotes (again->track (0), relyric));
    CHECK (again->track (0)["notes"][0]["lyric"] == juce::var (juce::String::fromUTF8 ("편")));
    CHECK (! again->track (0)["notes"][1].hasProperty ("lyric"));

    // 문자열이 아니거나 빈 가사는 오류
    auto bad = mrtest::parse (R"({ "tempo": 72, "tracks": [ { "name": "Lead", "instrument": "synth.piano", "notes": [
      { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 80, "lyric": 3 },
      { "pitch": 62, "startBeat": 1, "durationBeat": 1, "velocity": 80, "lyric": "" } ] } ] })");
    CHECK (! bad.composition.has_value());
    CHECK (mrtest::hasError (bad.diagnostics, "$.tracks[0].notes[0].lyric", "expected a string"));
    CHECK (mrtest::hasError (bad.diagnostics, "$.tracks[0].notes[1].lyric", "must not be empty"));
}
