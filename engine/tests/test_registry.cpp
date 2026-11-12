#include "TestFramework.h"
#include "TestUtil.h"

#include "instruments/Registry.h"
#include "performance/Performance.h"

using namespace mr;

TEST (built_in_synths_are_always_available)
{
    Registry r;
    for (const auto* name : { "synth.sine", "synth.piano", "synth.pad", "synth.strings", "synth.bass", "synth.pluck", "synth.drums" })
        CHECK (r.find (name) != nullptr);
    auto created = createInstrument (r, "synth.piano", { 48000.0, 512, 1 });
    CHECK (created.instrument != nullptr);
    CHECK_EQ (created.instrument->description(), std::string ("synth:piano"));
}

TEST (typo_suggestions_count_transpositions)
{
    CHECK_EQ (editDistance ("pinao", "piano"), 1);
    CHECK_EQ (editDistance ("velocty", "velocity"), 1);
    CHECK_EQ (closestMatch ("pinao", { "pad", "piano", "strings" }), std::string ("piano"));
    CHECK_EQ (closestMatch ("zzzz", { "pad", "piano" }), std::string ());
}

TEST (unknown_instrument_fails_with_a_suggestion)
{
    Registry r;
    try
    {
        createInstrument (r, "synth.pinao", { 48000.0, 512, 1 });
        CHECK (false);
    }
    catch (const Failure& f)
    {
        CHECK_EQ (f.code(), std::string ("instrument"));
        REQUIRE (! f.details().empty());
        CHECK (f.details().back().hint.find ("synth.piano") != std::string::npos);
    }
}

TEST (registry_paths_expand_environment_variables)
{
    const juce::File base ("/tmp/mr-base");
    setenv ("MR_TEST_SAMPLES", "/opt/samples", 1);
    CHECK_EQ (resolveRegistryPath ("${MR_TEST_SAMPLES}/a.sfz", base).getFullPathName().toStdString(), std::string ("/opt/samples/a.sfz"));
    CHECK_EQ (resolveRegistryPath ("${MR_TEST_UNSET_VAR:-../lib}/b.sfz", base).getFullPathName().toStdString(), std::string ("/tmp/lib/b.sfz"));
    CHECK_EQ (resolveRegistryPath ("c.vst3", base).getFullPathName().toStdString(), std::string ("/tmp/mr-base/c.vst3"));
}

TEST (registry_file_errors_have_paths_and_fallback_works)
{
    const auto file = mrtest::outputDir().getChildFile ("registry_test.json");
    file.replaceWithText (R"({ "instruments": {
        "grand": { "type": "vst3", "plugin": "missing/Grand.vst3", "fallback": "soft" },
        "noplugin": { "type": "vst3" },
        "soft":  { "type": "synth", "preset": "piano" },
        "typo":  { "type": "synht", "preset": "piano" },
        "bad":   { "type": "synth", "preset": "pianno" },
        "loop":  { "type": "sfz", "fallback": "loop" } } })");
    Registry r;
    Diagnostics d;
    r.addFile (file, d);
    const auto base = file.getFullPathName().toStdString() + ":$.instruments.";
    CHECK (mrtest::hasError (d, base + "typo.type"));
    CHECK (mrtest::hasError (d, base + "bad.preset"));
    CHECK (mrtest::hasError (d, base + "noplugin", "needs"));
    CHECK (r.find ("typo") == nullptr);

    auto created = createInstrument (r, "grand", { 48000.0, 512, 1 });
    REQUIRE (created.instrument != nullptr);
    CHECK_EQ (created.def->name, std::string ("soft"));
    REQUIRE (created.notes.size() == 1);
    CHECK (created.notes[0].find ("plugin not found") != std::string::npos);

    bool threw = false;
    try { createInstrument (r, "loop", { 48000.0, 512, 1 }); }
    catch (const Failure&) { threw = true; }
    CHECK (threw);

    Diagnostics missing;
    r.addFile (mrtest::outputDir().getChildFile ("does_not_exist.json"), missing);
    CHECK (! missing.ok());
}

TEST (registry_keymap_sends_mapped_keys_to_the_instrument)
{
    const auto file = mrtest::outputDir().getChildFile ("registry_keymap.json");
    file.replaceWithText (R"({ "instruments": {
        "kit": { "type": "synth", "preset": "drums", "keymap": { "36": 48, "38": 50 } },
        "badkey": { "type": "synth", "preset": "drums", "keymap": { "C4": 60 } },
        "badvalue": { "type": "synth", "preset": "drums", "keymap": { "36": 200 } } } })");
    Registry r;
    Diagnostics d;
    r.addFile (file, d);
    const auto* kit = r.find ("kit");
    REQUIRE (kit != nullptr);
    CHECK_EQ (kit->keymap.size(), (size_t) 2);
    CHECK_EQ (kit->keymap.at (36), 48);
    CHECK_EQ (d.errors.size(), (size_t) 2);   // 이름 건반과 범위 밖의 값
    CHECK_EQ (instrumentKey (36, 0, &kit->keymap), 48);
    CHECK_EQ (instrumentKey (26, 12, &kit->keymap), 50);   // 이조한 뒤의 건반으로 찾는다
    CHECK_EQ (instrumentKey (40, 0, &kit->keymap), 40);    // 없는 건반은 그대로
    CHECK_EQ (instrumentKey (126, 5, nullptr), 127);
}
