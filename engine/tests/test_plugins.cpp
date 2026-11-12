#include "TestFramework.h"
#include "TestUtil.h"

#include "io/AudioFile.h"
#include "plugins/PluginHost.h"
#include "realtime/RealtimePlayer.h"
#include "rendering/OfflineRenderer.h"

using namespace mr;

namespace
{

juce::File writeFile (const std::string& name, const std::string& text)
{
    auto f = mrtest::outputDir().getChildFile (name);
    f.replaceWithText (juce::String::fromUTF8 (text.c_str()));
    return f;
}

Composition toneOn (const std::string& instrument, double startBeat = 1.0)
{
    auto r = mrtest::parse (R"({ "tempo": 120, "render": { "tailSeconds": 0.5 },
      "tracks": [ { "name": "Tone", "instrument": "placeholder",
                    "notes": [ { "pitch": "A4", "startBeat": 1, "durationBeat": 2, "velocity": 127 } ] } ] })");
    auto c = *r.composition;
    c.tracks[0].instrument = instrument;
    c.tracks[0].notes[0].startBeat = startBeat;
    return c;
}

juce::AudioBuffer<float> renderWith (const Composition& c, const juce::File& registry, const std::string& out)
{
    RenderOptions o;
    o.useDefaultRegistry = false;
    o.registries = { registry };
    o.output = mrtest::outputDir().getChildFile (out);
    o.bitDepth = 32;
    renderComposition (c, o);
    return readAudioFile (o.output).buffer;
}

int firstSampleAbove (const juce::AudioBuffer<float>& b, float threshold)
{
    for (int i = 0; i < b.getNumSamples(); ++i)
        if (std::abs (b.getSample (0, i)) > threshold)
            return i;
    return -1;
}

} // namespace

#ifdef MUSIC_RENDER_TEST_PLUGIN_VST3

namespace
{

std::string testPluginPath() { return juce::File (MUSIC_RENDER_TEST_PLUGIN_VST3).getFullPathName().toStdString(); }

juce::File testRegistry (const std::string& extra = {})
{
    return writeFile ("plugin_registry.json", R"({ "instruments": {
        "testsynth": { "type": "vst3", "plugin": ")" + testPluginPath() + R"(" })" + extra + R"( } })");
}

} // namespace

TEST (plugin_scan_finds_the_test_synth)
{
    std::vector<Diagnostic> problems;
    auto found = PluginHost::get().scan ("vst3", { testPluginPath() }, problems);
    REQUIRE (found.size() == 1);
    CHECK (found[0].name == "MusicRender Test Synth");
    CHECK (found[0].isInstrument);
    CHECK (problems.empty());

    auto inFolder = PluginHost::get().scan ("vst3", { juce::File (testPluginPath()).getParentDirectory().getFullPathName().toStdString() }, problems);
    CHECK (std::any_of (inFolder.begin(), inFolder.end(), [] (const juce::PluginDescription& d) { return d.name == "MusicRender Test Synth"; }));

    const auto cache = mrtest::outputDir().getChildFile ("plugin_cache.json");
    writePluginCache (cache, found);
    auto cached = findCachedPlugin (cache, "vst3", "musicrender test synth");
    REQUIRE (cached.has_value());
    CHECK (cached->fileOrIdentifier == found[0].fileOrIdentifier);
}

TEST (plugin_outside_the_installed_folders_is_refused)
{
    // 곡 폴더에 딸려 온 플러그인 파일 (시험용 신스의 사본)
    const auto folder = mrtest::outputDir().getChildFile ("song_with_plugin");
    folder.deleteRecursively();
    const auto copy = folder.getChildFile ("fx/Bundled.vst3");
    REQUIRE (copy.getParentDirectory().createDirectory());
    REQUIRE (juce::File (testPluginPath()).copyDirectoryTo (copy));
    const auto emptyCache = mrtest::outputDir().getChildFile ("no_plugin_cache.json");
    emptyCache.deleteFile();

    juce::PluginDescription chosen;
    std::string error;
    auto refused = PluginHost::get().load ("vst3", copy.getFullPathName().toStdString(), "", 48000.0, 512, chosen, error);
    CHECK (refused == nullptr);
    CHECK (error.find ("outside the installed plugin folders") != std::string::npos);
    CHECK (! pluginLocationAllowed (copy, emptyCache));
    // .. 으로 설치 폴더를 지나는 경로도 실제 경로로 견준다
    const auto dotted = juce::File ("/Library/Audio/Plug-Ins/../../../" + copy.getFullPathName().substring (1));
    CHECK (! pluginLocationAllowed (dotted, emptyCache));
    // 곡의 악기도 같다: 레지스트리 항목이 사본을 가리키면 렌더링하지 않는다
    const auto registry = writeFile ("bundled_registry.json", R"({ "instruments": {
        "bundled": { "type": "vst3", "plugin": ")" + copy.getFullPathName().toStdString() + R"(" } } })");
    bool threw = false;
    try
    {
        renderWith (toneOn ("bundled"), registry, "bundled_tone.wav");
    }
    catch (const std::exception& e)
    {
        threw = std::string (e.what()).find ("outside the installed plugin folders") != std::string::npos;
    }
    CHECK (threw);

    // 플러그인 찾기로 캐시에 들어간 것, 사용자가 직접 지정한 것은 불러온다
    const auto cache = mrtest::outputDir().getChildFile ("bundled_plugin_cache.json");
    std::vector<Diagnostic> problems;
    PluginHost::get().allowLocation (folder.getFullPathName().toStdString());
    auto found = PluginHost::get().scan ("vst3", { copy.getFullPathName().toStdString() }, problems);
    REQUIRE (found.size() == 1);
    writePluginCache (cache, found);
    CHECK (pluginLocationAllowed (copy, cache));
    auto allowed = PluginHost::get().load ("vst3", copy.getFullPathName().toStdString(), "", 48000.0, 512, chosen, error);
    CHECK (allowed != nullptr);
}

TEST (plugin_renders_the_composition_with_exact_timing)
{
    const auto buffer = renderWith (toneOn ("testsynth"), testRegistry(), "plugin_tone.wav");
    CHECK_EQ (buffer.getNumSamples(), 96000);    // 끝 = 3박(1.5초) + 꼬리 0.5초
    const int onset = firstSampleAbove (buffer, 1.0e-4f);
    CHECK (onset >= 24000 && onset <= 24002);    // 1박 = 0.5초 = 24000 샘플
    // 사인 440 Hz, 진폭 = Level 0.8 * 벨로시티 1.0 * 0.5
    CHECK_NEAR (buffer.getMagnitude (0, 30000, 30000), 0.4, 0.002);
    int crossings = 0;
    for (int i = 30001; i < 66000; ++i)
        if (buffer.getSample (0, i - 1) < 0.0f && buffer.getSample (0, i) >= 0.0f)
            ++crossings;
    CHECK_NEAR (crossings / (36000.0 / 48000.0), 440.0, 2.0);
}

TEST (plugin_parameters_programs_and_state_apply)
{
    const auto byName = renderWith (toneOn ("quiet"), testRegistry (R"(, "quiet": { "type": "vst3", "plugin": ")" + testPluginPath() + R"(", "parameters": { "Level": 0.25 } })"), "plugin_param.wav");
    CHECK_NEAR (byName.getMagnitude (0, 30000, 30000), 0.125, 0.002);

    const auto byProgram = renderWith (toneOn ("prog"), testRegistry (R"(, "prog": { "type": "vst3", "plugin": ")" + testPluginPath() + R"(", "program": 1 })"), "plugin_program.wav");
    CHECK_NEAR (byProgram.getMagnitude (0, 30000, 30000), 0.125, 0.002);

    // 상태: 호스트에서 Level 을 0.5 로 바꾸고 저장한 상태를 레지스트리의 stateFile 로 준다
    juce::PluginDescription chosen;
    std::string error;
    auto instance = PluginHost::get().load ("vst3", testPluginPath(), "", 48000.0, 512, chosen, error);
    REQUIRE (instance != nullptr);
    PluginSettings s;
    s.parameters["level"] = 0.5;   // 매개변수 ID 로도 찾는다
    std::vector<Diagnostic> problems;
    applyPluginSettings (*instance, s, "$", problems);
    CHECK (problems.empty());
    juce::MemoryBlock state;
    instance->getStateInformation (state);
    const auto stateFile = mrtest::outputDir().getChildFile ("testsynth_state.bin");
    stateFile.replaceWithData (state.getData(), state.getSize());
    const auto fromState = renderWith (toneOn ("saved"), testRegistry (R"(, "saved": { "type": "vst3", "plugin": ")" + testPluginPath() + R"(", "stateFile": ")" + stateFile.getFullPathName().toStdString() + R"(" })"), "plugin_state.wav");
    CHECK_NEAR (fromState.getMagnitude (0, 30000, 30000), 0.25, 0.002);

    // 모르는 매개변수 이름은 제안과 함께 실패한다
    PluginSettings bad;
    bad.parameters["Levle"] = 0.5;
    problems.clear();
    applyPluginSettings (*instance, bad, "$.instruments.x", problems);
    REQUIRE (problems.size() == 1);
    CHECK (problems[0].hint.find ("Level") != std::string::npos);
}

TEST (plugin_latency_is_compensated_sample_exactly)
{
    const auto plain = renderWith (toneOn ("testsynth"), testRegistry(), "plugin_nolatency.wav");
    const auto delayed = renderWith (toneOn ("late"), testRegistry (R"(, "late": { "type": "vst3", "plugin": ")" + testPluginPath() + R"(", "parameters": { "Latency": "1000" } })"), "plugin_latency.wav");
    REQUIRE (plain.getNumSamples() == delayed.getNumSamples());
    double maxDiff = 0.0;
    for (int i = 0; i < plain.getNumSamples(); ++i)
        maxDiff = std::max (maxDiff, (double) std::abs (plain.getSample (0, i) - delayed.getSample (0, i)));
    CHECK (maxDiff < 1.0e-7);
}

TEST (latency_alignment_keeps_mixed_tracks_in_time)
{
    // 지연이 있는 플러그인 트랙과 지연이 없는 신스 트랙을 섞어도, 지연이 없을 때와 샘플까지 같다
    auto twoTracks = [] (const std::string& pluginInstrument)
    {
        auto r = mrtest::parse (R"({ "tempo": 120, "render": { "tailSeconds": 0.5 }, "buses": [ { "name": "fx" } ],
          "tracks": [ { "name": "Plug", "instrument": "x", "sends": [ { "bus": "fx", "levelDb": -6 } ],
                        "notes": [ { "pitch": "A4", "startBeat": 1, "durationBeat": 1, "velocity": 100 } ] },
                      { "name": "Synth", "instrument": "synth.sine",
                        "notes": [ { "pitch": "E5", "startBeat": 1, "durationBeat": 1, "velocity": 100 } ] } ] })");
        auto c = *r.composition;
        c.tracks[0].instrument = pluginInstrument;
        return c;
    };
    const auto reg = testRegistry (R"(, "late": { "type": "vst3", "plugin": ")" + testPluginPath() + R"(", "parameters": { "Latency": "777" } })");
    const auto plain = renderWith (twoTracks ("testsynth"), reg, "align_plain.wav");
    const auto late = renderWith (twoTracks ("late"), reg, "align_late.wav");
    REQUIRE (plain.getNumSamples() == late.getNumSamples());
    double maxDiff = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < plain.getNumSamples(); ++i)
            maxDiff = std::max (maxDiff, (double) std::abs (plain.getSample (ch, i) - late.getSample (ch, i)));
    CHECK (maxDiff < 1.0e-7);
}


namespace
{

// 시험용 신스를 곡 안의 악기 정의로 쓰는 곡 (레지스트리 파일 없이)
std::string inlineSong (const std::string& trackExtra)
{
    return R"({ "tempo": 120, "render": { "tailSeconds": 0.5 },
      "instruments": [ { "synth": { "type": "vst3", "plugin": ")" + testPluginPath() + R"(", "parameters": { "Level": 0.5 } } } ],
      "tracks": [ { "name": "Tone", "instrument": "synth" )" + trackExtra + R"(,
                    "notes": [ { "pitch": "A4", "startBeat": 1, "durationBeat": 6, "velocity": 127 } ] } ] })";
}

Composition parsedSong (const std::string& json)
{
    auto r = mrtest::parse (json);
    if (! r.composition)
        for (const auto& e : r.diagnostics.errors)
            std::fprintf (stderr, "    parse error %s: %s\n", e.path.c_str(), e.message.c_str());
    return *r.composition;
}

juce::AudioBuffer<float> renderSong (const Composition& c, const std::string& out)
{
    RenderOptions o;
    o.useDefaultRegistry = false;
    o.output = mrtest::outputDir().getChildFile (out);
    o.bitDepth = 32;
    renderComposition (c, o);
    return readAudioFile (o.output).buffer;
}

juce::AudioBuffer<float> pullPlayer (RealtimePlayer& p, double seconds)
{
    const int total = (int) std::llround (seconds * 48000.0);
    juce::AudioBuffer<float> out (2, total);
    for (int done = 0; done < total; done += 512)
    {
        const int n = std::min (512, total - done);
        float* ptrs[2] = { out.getWritePointer (0, done), out.getWritePointer (1, done) };
        p.process (ptrs, 2, n);
    }
    return out;
}

} // namespace

TEST (song_defines_instruments_and_track_plugin_settings_follow_the_registry)
{
    // 곡 안의 정의(Level 0.5)로 렌더링하고, 트랙의 설정이 그 뒤에 덮는다
    const auto plain = renderSong (parsedSong (inlineSong ("")), "inline_plain.wav");
    CHECK_NEAR (plain.getMagnitude (0, 30000, 30000), 0.25, 0.002);
    const auto quieter = renderSong (parsedSong (inlineSong (R"(, "plugin": { "parameters": { "Level": 0.25 } })")), "inline_track.wav");
    CHECK_NEAR (quieter.getMagnitude (0, 30000, 30000), 0.125, 0.002);
    const auto byProgram = renderSong (parsedSong (inlineSong (R"(, "plugin": { "program": 1 })")), "inline_program.wav");
    CHECK_NEAR (byProgram.getMagnitude (0, 30000, 30000), 0.125, 0.002);

    // 트랙의 상태: 플러그인에서 Level 을 0.9 로 바꾼 상태를 base64 로
    juce::PluginDescription chosen;
    std::string error;
    auto instance = PluginHost::get().load ("vst3", testPluginPath(), "", 48000.0, 512, chosen, error);
    REQUIRE (instance != nullptr);
    PluginSettings s;
    s.parameters["Level"] = 0.9;
    std::vector<Diagnostic> problems;
    applyPluginSettings (*instance, s, "$", problems);
    const auto state = pluginStateBase64 (*instance);
    const auto fromState = renderSong (parsedSong (inlineSong (R"(, "plugin": { "state": ")" + state + R"(" })")), "inline_state.wav");
    CHECK_NEAR (fromState.getMagnitude (0, 30000, 30000), 0.45, 0.002);

    // 곡 안의 정의의 잘못은 그 자리의 경로로, 트랙 설정의 잘못은 파서가 알린다
    auto badDef = mrtest::parse (R"({ "tempo": 120, "instruments": [ { "x": { "type": "vst4" } } ],
        "tracks": [ { "name": "A", "instrument": "x", "notes": [] } ] })");
    REQUIRE (badDef.composition.has_value());
    RenderOptions ro;
    ro.useDefaultRegistry = false;
    Diagnostics d;
    buildRegistry (*badDef.composition, ro, d);
    CHECK (! d.ok() && d.errors[0].path.find ("$.instruments[0].x.type") != std::string::npos);
    auto badState = mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "A", "instrument": "synth.sine", "plugin": { "state": "%%%", "level": 1 }, "notes": [] } ] })");
    CHECK (mrtest::hasError (badState.diagnostics, "$.tracks[0].plugin.state"));
    CHECK (! badState.diagnostics.warnings.empty());   // 모르는 키

    // 플러그인이 아닌 악기에 준 설정은 경고만 한다
    auto onSynth = parsedSong (R"({ "tempo": 120, "tracks": [ { "name": "A", "instrument": "synth.sine", "plugin": { "program": 2 },
        "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 90 } ] } ] })");
    RenderOptions o2;
    o2.useDefaultRegistry = false;
    const auto report = renderComposition (onSynth, o2);
    CHECK (std::any_of (report.warnings.begin(), report.warnings.end(), [] (const Diagnostic& w) { return w.path == "$.tracks[0].plugin"; }));
}

TEST (realtime_apply_puts_track_plugin_settings_into_the_running_plugin)
{
    const auto song = parsedSong (inlineSong (""));
    PlayerOptions po;
    po.useDefaultRegistry = false;
    po.threads = 0;
    RealtimePlayer player (song, po);
    auto* plugin = player.pluginInstance (0);
    REQUIRE (plugin != nullptr);
    const auto baseState = pluginStateBase64 (*plugin);
    player.play();
    const auto before = pullPlayer (player, 1.0);
    CHECK_NEAR (before.getMagnitude (0, 30000, 18000), 0.25, 0.003);

    // 매개변수: 같은 플러그인 객체에 넣는다 (다시 만들지 않는다)
    CHECK (player.apply (parsedSong (inlineSong (R"(, "plugin": { "parameters": { "Level": 0.25 } })"))));
    CHECK (player.pluginInstance (0) == plugin);
    const auto quieter = pullPlayer (player, 0.5);
    CHECK_NEAR (quieter.getMagnitude (0, 6000, 12000), 0.125, 0.003);

    // 같은 설정을 다시 반영하면 상태가 그대로이고, 트랙 설정을 지우면 곡 안의 정의(Level 0.5)로 돌아간다
    const auto edited = pluginStateBase64 (*plugin);
    CHECK (player.apply (parsedSong (inlineSong (R"(, "plugin": { "parameters": { "Level": 0.25 } }, "comment": "same")"))));
    CHECK (pluginStateBase64 (*plugin) == edited);
    CHECK (player.apply (song));
    CHECK (pluginStateBase64 (*plugin) == baseState);
    const auto restored = pullPlayer (player, 0.5);
    CHECK_NEAR (restored.getMagnitude (0, 6000, 12000), 0.25, 0.003);

    // 상태로 주면 상태대로, 없는 매개변수는 거절하고 아무것도 바꾸지 않는다
    PluginSettings louder;
    louder.parameters["Level"] = 1.0;
    std::vector<Diagnostic> problems;
    juce::PluginDescription chosen;
    std::string error;
    auto other = PluginHost::get().load ("vst3", testPluginPath(), "", 48000.0, 512, chosen, error);
    REQUIRE (other != nullptr);
    applyPluginSettings (*other, louder, "$", problems);
    const auto loudState = pluginStateBase64 (*other);
    CHECK (player.apply (parsedSong (inlineSong (R"(, "plugin": { "state": ")" + loudState + R"(" })"))));
    CHECK_NEAR ((double) plugin->getParameters()[0]->getValue(), 1.0, 1.0e-6);
    bool threw = false;
    try { player.apply (parsedSong (inlineSong (R"(, "plugin": { "parameters": { "Nope": 0.5 } })"))); }
    catch (const Failure&) { threw = true; }
    CHECK (threw);
    CHECK_NEAR ((double) plugin->getParameters()[0]->getValue(), 1.0, 1.0e-6);

    // 악기 정의를 바꾸면 다시 만들어야 한다
    auto redefined = inlineSong ("");
    redefined.replace (redefined.find ("\"Level\": 0.5"), 12, "\"Level\": 0.6");
    CHECK (! player.apply (parsedSong (redefined)));
}
#endif

TEST (missing_plugin_fails_clearly_or_falls_back)
{
    const auto reg = writeFile ("missing_plugin.json", R"({ "instruments": {
        "ghost": { "type": "vst3", "plugin": "missing/Ghost.vst3" },
        "ghostish": { "type": "vst3", "plugin": "missing/Ghost.vst3", "fallback": "synth.sine" } } })");
    Registry r;
    Diagnostics d;
    r.addFile (reg, d);
    REQUIRE (d.ok());
    bool threw = false;
    try { createInstrument (r, "ghost", { 48000.0, 512, 1 }); }
    catch (const Failure& f)
    {
        threw = true;
        CHECK_EQ (f.code(), std::string ("instrument"));
        CHECK (std::string (f.what()).find ("plugin not found") != std::string::npos);
    }
    CHECK (threw);
    auto created = createInstrument (r, "ghostish", { 48000.0, 512, 1 });
    CHECK_EQ (created.def->name, std::string ("synth.sine"));
}

#if JUCE_MAC
TEST (apple_dls_synth_renders_through_audio_unit)
{
    const auto reg = writeFile ("au_registry.json", R"({ "instruments": {
        "dls": { "type": "au", "plugin": "AudioUnit:Synths/aumu,dls ,appl" } } })");
    const auto buffer = renderWith (toneOn ("dls", 0.0), reg, "au_dls.wav");
    CHECK (buffer.getMagnitude (0, 0, 48000) > 0.01f);
}
#endif
