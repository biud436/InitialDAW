#include "TestFramework.h"
#include "TestUtil.h"

#include "midi/MidiExport.h"
#include "midi/MidiImport.h"
#include "notation/MusicXml.h"
#include "notation/Notation.h"

using namespace mr;

namespace
{

Composition song (const std::string& notes, const std::string& extra = {}, const std::string& instrument = "synth.pluck")
{
    auto r = mrtest::parse (R"({ "tempo": 120, "timeSignature": [4, 4])" + extra + R"(, "tracks": [ { "name": "Lead", "instrument": ")" + instrument
                            + R"(", "notes": [ )" + notes + " ] } ] }");
    if (! r.composition)
        for (const auto& e : r.diagnostics.errors)
            std::fprintf (stderr, "    parse error %s: %s\n", e.path.c_str(), e.message.c_str());
    REQUIRE (r.composition.has_value());
    return *r.composition;
}

std::string n (int pitch, double start, double length, int velocity = 80)
{
    return "{ \"pitch\": " + std::to_string (pitch) + ", \"startBeat\": " + juce::String (start).toStdString() + ", \"durationBeat\": "
           + juce::String (length).toStdString() + ", \"velocity\": " + std::to_string (velocity) + " }";
}

std::string pitches (const NotatedEvent& e)
{
    std::string s;
    for (const auto& p : e.pitches)
        s += (s.empty() ? "" : " ") + spellingName (p.spelling);
    return e.rest ? "rest" : s;
}

} // namespace

TEST (key_names_and_spelling)
{
    for (int f = -7; f <= 7; ++f)
        for (const bool minor : { false, true })
        {
            const KeySignature k { f, minor };
            const auto back = parseKeyName (keyName (k));
            CHECK (back.has_value() && *back == k);
        }
    CHECK (keyName ({ -5, true }) == "Bb minor");
    CHECK (keyName ({ 6, false }) == "F# major");
    CHECK (! parseKeyName ("H major").has_value());
    CHECK (! parseKeyName ("G# major").has_value());   // 여덟 샵
    CHECK_EQ (parseKeyName ("C")->fifths, 0);
    // 철자
    const KeySignature bbMinor { -5, true }, eMajor { 4, false }, cMajor {}, dMinor { -1, true };
    CHECK (spellingName (spellPitch (61, bbMinor)) == "Db4");
    CHECK (spellingName (spellPitch (69, bbMinor)) == "A4");    // 이끎음
    CHECK (spellingName (spellPitch (66, bbMinor)) == "Gb4");
    CHECK (spellingName (spellPitch (68, eMajor)) == "G#4");
    CHECK (spellingName (spellPitch (63, eMajor)) == "D#4");
    CHECK (spellingName (spellPitch (70, cMajor)) == "Bb4");
    CHECK (spellingName (spellPitch (66, cMajor)) == "F#4");
    CHECK (spellingName (spellPitch (61, dMinor)) == "C#4");    // 이끎음
    CHECK (spellingName (spellPitch (71, dMinor)) == "B4");     // 올린 6도
    CHECK (spellingName (spellPitch (72, { 7, false })) == "B#4");   // C# 장조의 B#
    // 추정: C 장조의 음계와 A 화성 단음계
    std::vector<double> major (12, 0.0), minor (12, 0.0);
    for (const int pc : { 0, 2, 4, 5, 7, 9, 11 })
        major[(size_t) pc] = 1.0;
    major[0] = major[7] = major[4] = 3.0;
    for (const int pc : { 9, 11, 0, 2, 4, 5, 8 })
        minor[(size_t) pc] = 1.0;
    minor[9] = minor[4] = minor[0] = 3.0;
    CHECK (estimateKey (major) == KeySignature {});
    CHECK (estimateKey (minor) == (KeySignature { 0, true }));
}

TEST (score_measures_values_and_ties)
{
    // C4 4분, E4 8분, F#4 점4분(박을 넘어 8분 + 4분 붙임줄), 둘째 마디 처음부터 셋째 마디로 넘는 온음표
    const auto c = song (n (60, 0, 1) + ", " + n (64, 1, 0.5) + ", " + n (66, 1.5, 1.5) + ", " + n (67, 6, 4), R"(, "key": "C major")");
    const auto score = buildScore (c);
    REQUIRE (score.parts.size() == 1);
    const auto& part = score.parts[0];
    CHECK_EQ (part.measures.size(), (size_t) 3);
    CHECK (part.clefs.size() == 1 && part.clefs[0] == Clef::treble);
    const auto& m1 = part.measures[0].staves[0];
    REQUIRE (m1.size() == 5);
    CHECK (m1[0].type == "quarter" && pitches (m1[0]) == "C4");
    CHECK (m1[1].type == "eighth" && pitches (m1[1]) == "E4");
    CHECK (m1[2].type == "eighth" && pitches (m1[2]) == "F#4" && m1[2].pitches[0].tieStart && ! m1[2].pitches[0].tieStop);
    CHECK (m1[3].type == "quarter" && m1[3].pitches[0].tieStop && ! m1[3].pitches[0].tieStart);
    CHECK (m1[2].pitches[0].accidental == std::optional<int> (1) && ! m1[3].pitches[0].accidental);
    CHECK (m1[4].rest && m1[4].type == "quarter");
    // 둘째 마디: 반쉼표, 반음표(붙임줄 시작) / 셋째 마디: 반음표(붙임줄 끝), 반쉼표
    const auto& m2 = part.measures[1].staves[0];
    REQUIRE (m2.size() == 2);
    CHECK (m2[0].rest && m2[0].type == "half");
    CHECK (m2[1].type == "half" && m2[1].pitches[0].tieStart);
    const auto& m3 = part.measures[2].staves[0];
    CHECK (m3[0].type == "half" && m3[0].pitches[0].tieStop && ! m3[0].pitches[0].tieStart);
    CHECK (part.measures[0].timeChanged && part.measures[0].keyChanged && ! part.measures[1].timeChanged);
    CHECK (part.measures[0].tempo.size() == 1 && part.measures[0].tempo[0].bpm == 120.0);
}

TEST (score_triplets_rests_accidentals_and_chords)
{
    // 셋잇단 8분 셋, 4분 화음 C E G, 빈 둘째 마디, 셋째 마디 F#, F#, F (제자리표)
    const auto c = song (n (60, 0, 1.0 / 3) + ", " + n (62, 1.0 / 3, 1.0 / 3) + ", " + n (64, 2.0 / 3, 1.0 / 3) + ", " + n (60, 1, 1) + ", "
                         + n (64, 1, 1) + ", " + n (67, 1, 1) + ", " + n (66, 8, 1) + ", " + n (66, 9, 1) + ", " + n (65, 10, 1), R"(, "key": "C major")");
    const auto score = buildScore (c);
    const auto& m1 = score.parts[0].measures[0].staves[0];
    REQUIRE (m1.size() >= 5);
    CHECK (m1[0].triplet && m1[0].type == "eighth" && m1[0].tupletStart && ! m1[0].tupletStop);
    CHECK (m1[2].triplet && m1[2].tupletStop);
    CHECK_EQ (m1[0].duration, 8);
    CHECK (pitches (m1[3]) == "C4 E4 G4" && m1[3].type == "quarter" && ! m1[3].triplet);
    const auto& m2 = score.parts[0].measures[1].staves[0];
    CHECK (m2.size() == 1 && m2[0].measureRest && m2[0].duration == 96);
    const auto& m3 = score.parts[0].measures[2].staves[0];
    CHECK (m3[0].pitches[0].accidental == std::optional<int> (1));
    CHECK (! m3[1].pitches[0].accidental);
    CHECK (m3[2].pitches[0].accidental == std::optional<int> (0));
}

TEST (score_meter_and_key_changes_and_grand_staff)
{
    // 3/4 로 바뀐 셋째 마디, 셋째 마디의 E 장조, 건반 악기의 큰보표
    const auto c = song (n (48, 0, 1) + ", " + n (72, 0, 1) + ", " + n (50, 4, 1) + ", " + n (74, 4, 1) + ", " + n (68, 8, 3) + ", " + n (40, 8, 3),
                         R"(, "key": "C minor", "meterChanges": [ { "beat": 8, "timeSignature": [3, 4] } ], "keyChanges": [ { "beat": 8, "key": "E major" } ])",
                         "synth.piano");
    const auto score = buildScore (c);
    const auto& part = score.parts[0];
    CHECK (! score.keyEstimated && score.key == (KeySignature { -3, true }));
    REQUIRE (part.measures.size() == 3);
    CHECK (part.clefs.size() == 2 && part.clefs[1] == Clef::bass);
    CHECK (part.measures[2].timeChanged && part.measures[2].beatsPerBar == 3 && part.measures[2].length == 72);
    CHECK (part.measures[2].keyChanged && part.measures[2].key == (KeySignature { 4, false }));
    CHECK (pitches (part.measures[0].staves[1][0]) == "C3" && pitches (part.measures[0].staves[0][0]) == "C5");
    CHECK (pitches (part.measures[2].staves[0][0]) == "G#4" && ! part.measures[2].staves[0][0].pitches[0].accidental);   // 조표의 G#
    CHECK (part.measures[2].staves[0][0].type == "half" && part.measures[2].staves[0][0].dots == 1);
    // 조가 C 단조인 첫 마디의 Ab 는 조표
    const auto c2 = song (n (68, 0, 1), R"(, "key": "C minor")");
    CHECK (pitches (buildScore (c2).parts[0].measures[0].staves[0][0]) == "Ab4");
}

TEST (musicxml_document)
{
    const auto c = song (n (60, 0, 1.0 / 3) + ", " + n (62, 1.0 / 3, 1.0 / 3) + ", " + n (64, 2.0 / 3, 1.0 / 3) + ", " + n (66, 1, 2) + ", " + n (67, 3, 2),
                         R"(, "title": "시험", "key": "G major", "markers": [ { "beat": 0, "name": "A" } ])");
    const auto text = musicXml (buildScore (c));
    const auto xml = juce::parseXML (juce::String::fromUTF8 (text.c_str()));
    REQUIRE (xml != nullptr);
    CHECK (xml->getTagName() == "score-partwise" && xml->getStringAttribute ("version") == "4.0");
    CHECK (juce::String::fromUTF8 (text.c_str()).contains ("<!DOCTYPE score-partwise"));
    const auto* part = xml->getChildByName ("part");
    REQUIRE (part != nullptr);
    CHECK_EQ (part->getNumChildElements(), 2);   // 두 마디 (붙임줄로 넘는 음)
    const auto* m1 = part->getChildByName ("measure");
    CHECK (m1->getChildByName ("attributes")->getChildByName ("divisions")->getAllSubText() == "24");
    CHECK (m1->getChildByName ("attributes")->getChildByName ("key")->getChildByName ("fifths")->getAllSubText() == "1");
    int notes = 0, triplets = 0, ties = 0, durations = 0;
    for (auto* note : m1->getChildWithTagNameIterator ("note"))
    {
        ++notes;
        durations += note->getChildByName ("chord") == nullptr ? note->getChildByName ("duration")->getAllSubText().getIntValue() : 0;
        triplets += note->getChildByName ("time-modification") != nullptr ? 1 : 0;
        for (auto* tie : note->getChildWithTagNameIterator ("tie"))
            ties += tie->getStringAttribute ("type") == "start" ? 1 : 0;
    }
    CHECK_EQ (durations, 96);   // 마디를 채운다
    CHECK_EQ (triplets, 3);
    CHECK_EQ (ties, 1);         // G4 가 다음 마디로
    CHECK (juce::String::fromUTF8 (text.c_str()).contains ("<rehearsal>A</rehearsal>"));
    CHECK (juce::String::fromUTF8 (text.c_str()).contains ("<per-minute>120</per-minute>"));
}

TEST (midi_carries_key_signatures)
{
    const auto c = song (n (60, 0, 1) + ", " + n (62, 8, 1), R"(, "key": "Bb minor", "keyChanges": [ { "beat": 4, "key": "D major" } ])");
    const auto file = mrtest::outputDir().getChildFile ("keys.mid");
    exportMidi (c, file, false, std::nullopt);
    MidiImportReport report;
    MidiImportOptions options;
    const auto root = importMidi (file, options, report);
    CHECK (root["key"].toString() == "Bb minor");
    CHECK (root["keyChanges"].size() == 1 && (double) root["keyChanges"][0]["beat"] == 4.0 && root["keyChanges"][0]["key"].toString() == "D major");
}

TEST (score_reads_detached_notes_to_the_next_start)
{
    // 12/8 의 끊어 쓴 8분음표 (0.4 박), 한 박 뒤에 다음 음이 오는 0.84 박 (4분음표), 마지막 4분음표 뒤의 진짜 쉼표
    auto c = song (n (60, 0, 0.4) + ", " + n (62, 0.5, 0.4) + ", " + n (64, 1, 0.4) + ", " + n (65, 1.5, 0.84) + ", " + n (67, 2.5, 0.4) + ", "
                   + n (69, 3, 1), R"(, "key": "C major")");
    c.beatsPerBar = 12;
    c.beatUnit = 8;
    const auto score = buildScore (c);
    const auto& m1 = score.parts[0].measures[0].staves[0];
    REQUIRE (m1.size() >= 7);
    CHECK (m1[0].type == "eighth" && m1[1].type == "eighth" && m1[2].type == "eighth" && ! m1[0].rest && ! m1[1].rest);
    CHECK (m1[3].type == "quarter" && m1[3].dots == 0 && pitches (m1[3]) == "F4");
    CHECK (m1[4].type == "eighth" && pitches (m1[4]) == "G4");
    CHECK (m1[5].type == "quarter" && pitches (m1[5]) == "A4");
    CHECK (m1[6].rest);   // 그 뒤의 쉼표는 남는다
}

TEST (score_gathers_strummed_chords)
{
    // 줄마다 0.02 박씩 늦게 친 화음 (끝도 조금씩 다르다)과 그 뒤 8분음표 둘
    const auto c = song (n (55, 0, 0.98) + ", " + n (59, 0.02, 0.96) + ", " + n (62, 0.04, 0.95) + ", " + n (67, 0.06, 0.94) + ", " + n (67, 1, 0.5)
                         + ", " + n (69, 1.5, 0.5), R"(, "key": "G major")");
    const auto score = buildScore (c);
    const auto& m1 = score.parts[0].measures[0].staves[0];
    REQUIRE (m1.size() >= 3);
    CHECK (pitches (m1[0]) == "G3 B3 D4 G4" && m1[0].type == "quarter" && ! m1[0].triplet);
    for (const auto& p : m1[0].pitches)
        CHECK (! p.tieStart && ! p.tieStop);
    CHECK (m1[1].type == "eighth" && pitches (m1[1]) == "G4" && m1[2].type == "eighth");
}

TEST (key_at_a_beat_and_its_scale)
{
    const auto c = song (n (60, 0, 1), R"(, "key": "C minor", "keyChanges": [ { "beat": 8, "key": "E major" } ])");
    CHECK (keyAt (c, 0.0) == (KeySignature { -3, true }));
    CHECK (keyAt (c, 7.99) == (KeySignature { -3, true }));
    CHECK (keyAt (c, 8.0) == (KeySignature { 4, false }));
    CHECK (! keyAt (song (n (60, 0, 1)), 4.0).has_value());   // 적히지 않은 조는 추정하지 않는다
    // C 단조: C D Eb F G Ab Bb, E 장조: E F# G# A B C# D#
    const KeySignature cMinor { -3, true }, eMajor { 4, false };
    std::string inC, inE;
    for (int p = 60; p < 72; ++p)
    {
        inC += inKeyScale (cMinor, p) ? "1" : "0";
        inE += inKeyScale (eMajor, p) ? "1" : "0";
    }
    CHECK_EQ (inC, std::string ("101101011010"));
    CHECK_EQ (inE, std::string ("010110101101"));
    CHECK (inKeyScale (cMinor, 36) && inKeyScale (cMinor, 0) && ! inKeyScale (cMinor, 1));
}
