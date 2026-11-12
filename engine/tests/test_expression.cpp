#include "TestFramework.h"
#include "TestUtil.h"

#include "instruments/SfzInstrument.h"
#include "io/AudioFile.h"
#include "rendering/EventBuilder.h"
#include "rendering/OfflineRenderer.h"

using namespace mr;

namespace
{

juce::File fixture (const std::string& rel) { return mrtest::sourceDir().getChildFile ("tests/fixtures/" + rel); }

juce::File registryFile (const std::string& name, const std::string& body)
{
    auto f = mrtest::outputDir().getChildFile (name);
    f.replaceWithText (juce::String::fromUTF8 (("{ \"instruments\": { " + body + " } }").c_str()));
    return f;
}

Composition composition (const std::string& json)
{
    auto r = mrtest::parse (json);
    if (! r.composition)
        for (const auto& e : r.diagnostics.errors)
            std::fprintf (stderr, "    parse error %s: %s\n", e.path.c_str(), e.message.c_str());
    return *r.composition;
}

juce::AudioBuffer<float> render (const Composition& c, const std::string& out, std::optional<juce::File> registry = std::nullopt)
{
    RenderOptions o;
    o.useDefaultRegistry = false;
    if (registry)
        o.registries = { *registry };
    o.bitDepth = 32;
    o.output = mrtest::outputDir().getChildFile (out);
    renderComposition (c, o);
    return readAudioFile (o.output).buffer;
}

float magnitude (const juce::AudioBuffer<float>& b, double fromSec, double toSec, int channel = 0)
{
    const int from = (int) (fromSec * 48000), count = (int) ((toSec - fromSec) * 48000);
    if (from < 0 || count <= 0 || from + count > b.getNumSamples())
    {
        mrtest::check (false, "measurement window inside the buffer", __FILE__, __LINE__,
                       std::to_string (fromSec) + ".." + std::to_string (toSec) + " s of " + std::to_string (b.getNumSamples() / 48000.0));
        return 0.0f;
    }
    return b.getMagnitude (channel, from, count);
}

double frequency (const juce::AudioBuffer<float>& b, double fromSec, double toSec)
{
    const int from = (int) (fromSec * 48000), to = (int) (toSec * 48000);
    int crossings = 0, first = -1, last = -1;
    for (int i = from + 1; i < to; ++i)
        if (b.getSample (0, i - 1) < 0.0f && b.getSample (0, i) >= 0.0f)
        {
            if (first < 0) first = i;
            last = i;
            ++crossings;
        }
    return crossings > 1 ? (crossings - 1) * 48000.0 / (last - first) : 0.0;
}

const char* toneJson = R"({ "tempo": 60, "render": { "tailSeconds": 0.2 },
  "tracks": [ { "name": "T", "instrument": "INSTR", AUTOMATION
    "notes": [ { "pitch": "A4", "startBeat": 0, "durationBeat": 4, "velocity": 127 } ] } ] })";

Composition tone (const std::string& instrument, const std::string& automation)
{
    std::string j = toneJson;
    j.replace (j.find ("INSTR"), 5, instrument);
    j.replace (j.find ("AUTOMATION"), 10, automation.empty() ? std::string {} : automation + ",");
    return composition (j);
}

} // namespace

TEST (native_cc11_shapes_the_synth)
{
    const auto b = render (tone ("synth.sine", R"("cc11": [ { "beat": 0, "value": 127 }, { "beat": 4, "value": 0 } ])"), "cc11_native.wav");
    // 2초(값 63.5)에서 20ms 창으로 잰다. 넓은 창의 최댓값은 창 앞쪽의 큰 값을 잡는다.
    const float early = magnitude (b, 0.01, 0.03), mid = magnitude (b, 1.99, 2.01), late = magnitude (b, 3.8, 3.95);
    CHECK (early > 0.2f);
    CHECK_NEAR (mid / early, 0.25, 0.02);   // 신스는 익스프레션을 제곱으로 쓴다
    CHECK (late < early * 0.01f);
}

TEST (engine_controllers_apply_gain_and_pan)
{
    const auto plain = render (tone ("synth.sine", {}), "ctl_plain.wav");
    const float ref = magnitude (plain, 1.9, 2.1);
    const auto reg = registryFile ("ctl_registry.json", R"("eng": { "type": "synth", "preset": "sine", "controllers": { "cc11": "engine" } })");
    const auto b = render (tone ("eng", R"("cc11": [ { "beat": 0, "value": 127 }, { "beat": 4, "value": 0 } ])"), "cc11_engine.wav", reg);
    CHECK_NEAR (magnitude (b, 1.98, 2.02) / ref, 0.25, 0.02);

    // cc7 은 어느 악기든 엔진의 볼륨이다
    const auto vol = render (tone ("synth.sine", R"("cc7": [ { "beat": 0, "value": 64 } ])"), "cc7.wav");
    CHECK_NEAR (magnitude (vol, 1.0, 2.0) / ref, (64.0 / 127.0) * (64.0 / 127.0), 0.01);

    // cc10 은 엔진의 밸런스: 0 이면 왼쪽 끝
    const auto pan = render (tone ("synth.sine", R"("cc10": [ { "beat": 0, "value": 0 } ])"), "cc10.wav");
    CHECK (magnitude (pan, 1.0, 2.0, 1) < 1.0e-6f);
    CHECK_NEAR (magnitude (pan, 1.0, 2.0, 0) / ref, 1.0, 0.01);
}

TEST (pitch_bend_and_sustain_pedal_reach_the_instrument)
{
    const auto bent = render (tone ("synth.sine", R"("pitchBend": [ { "beat": 0, "value": 1.0 } ])"), "bend.wav");
    CHECK_NEAR (frequency (bent, 0.5, 3.5), 440.0 * std::pow (2.0, 2.0 / 12.0), 1.0);

    const char* pedal = R"({ "tempo": 60, "render": { "tailSeconds": 0.5 },
      "tracks": [ { "name": "P", "instrument": "synth.piano", PEDAL
        "notes": [ { "pitch": "C4", "startBeat": 0, "durationBeat": 0.5, "velocity": 100 } ] } ] })";
    std::string withPedal = pedal, without = pedal;
    withPedal.replace (withPedal.find ("PEDAL"), 5, R"("cc64": [ { "beat": 0, "value": 127, "curve": "step" }, { "beat": 3, "value": 0 } ],)");
    without.replace (without.find ("PEDAL"), 5, "");
    const auto held = render (composition (withPedal), "pedal_down.wav");
    const auto free = render (composition (without), "pedal_up.wav");
    // 페달을 떼는 3박까지가 곡이다. 꼬리 0.5초를 더해 3.5초
    CHECK_EQ (held.getNumSamples(), 168000);
    CHECK_EQ (free.getNumSamples(), 48000);
    CHECK (magnitude (held, 0.7, 0.95) > 20.0f * magnitude (free, 0.7, 0.95));
    CHECK (magnitude (held, 1.5, 2.5) > 1.0e-3f);
}

TEST (engine_dynamics_controls_gain_and_brightness)
{
    const auto reg = registryFile ("dyn_registry.json", R"("dyn": { "type": "synth", "preset": "strings",
        "controllers": { "cc1": "engine" }, "dynamics": { "gainDb": [-20, 0], "brightnessHz": [400, 18000] } })");
    const auto soft = render (tone ("dyn", R"("cc1": [ { "beat": 0, "value": 0 } ])"), "dyn_soft.wav", reg);
    const auto loud = render (tone ("dyn", R"("cc1": [ { "beat": 0, "value": 127 } ])"), "dyn_loud.wav", reg);
    const float ratio = magnitude (soft, 1.0, 3.0) / magnitude (loud, 1.0, 3.0);
    CHECK (ratio < 0.1f);   // -20 dB 에 밝기 필터가 더 줄인다
    CHECK (ratio > 0.01f);
    // 밝기: 여린 쪽은 고역이 훨씬 적다 (1차 차분의 에너지 비)
    auto brightness = [] (const juce::AudioBuffer<float>& b)
    {
        double e = 0.0, d = 0.0;
        for (int i = 48000; i < 144000; ++i)
        {
            const double x = b.getSample (0, i), dx = x - b.getSample (0, i - 1);
            e += x * x;
            d += dx * dx;
        }
        return d / e;
    };
    CHECK (brightness (soft) < 0.5 * brightness (loud));
}

TEST (keyswitches_are_sent_only_when_the_articulation_changes)
{
    const auto reg = registryFile ("ks_registry.json", R"("ks": { "type": "synth", "preset": "sine",
        "articulations": { "sustain": { "keyswitch": 24 }, "staccato": { "keyswitch": 25, "lengthFactor": 0.5 },
                           "marcato": { "program": 5, "cc": { "cc32": 3 }, "velocityOffset": 10 } } })");
    Registry r;
    Diagnostics d;
    r.addFile (reg, d);
    REQUIRE (d.ok());
    const auto* def = r.find ("ks");
    REQUIRE (def != nullptr);
    CHECK_EQ (def->profile.defaultArticulation, std::string ("sustain"));

    auto c = composition (R"({ "tempo": 120, "tracks": [ { "name": "K", "instrument": "ks", "notes": [
        { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 80 },
        { "pitch": 62, "startBeat": 1, "durationBeat": 1, "velocity": 80 },
        { "pitch": 64, "startBeat": 2, "durationBeat": 1, "velocity": 80, "articulation": "staccato" },
        { "pitch": 65, "startBeat": 3, "durationBeat": 1, "velocity": 80, "articulation": "staccato" },
        { "pitch": 67, "startBeat": 4, "durationBeat": 1, "velocity": 80, "articulation": "marcato" },
        { "pitch": 69, "startBeat": 5, "durationBeat": 1, "velocity": 80, "articulation": "sustain" } ] } ] })");
    const TempoMap tempo (c.tempo);
    PerformanceContext pctx { tempo, 4.0, "K", 0, &def->profile, {} };
    const auto performed = perform (c.tracks[0].notes, ResolvedPerformance {}, pctx);
    CHECK_NEAR (performed[2].endSec - performed[2].startSec, 0.25, 1e-9);   // 스타카토는 절반 길이
    CHECK_EQ (performed[4].velocity, 90);
    const auto events = buildTrackEvents (performed, { tempo, 48000.0, *def, c.tracks[0] });

    std::vector<std::pair<int64_t, int>> keyswitches;
    int programs = 0, cc32 = 0;
    for (size_t i = 0; i < events.size(); ++i)
    {
        const auto& m = events[i].message;
        if (m.isNoteOn() && (m.getNoteNumber() == 24 || m.getNoteNumber() == 25))
        {
            keyswitches.push_back ({ events[i].sample, m.getNoteNumber() });
            // 키스위치 바로 뒤에 같은 샘플의 음이 온다
            REQUIRE (i + 1 < events.size());
            CHECK (events[i + 1].message.isNoteOn() && events[i + 1].sample == events[i].sample);
        }
        programs += m.isProgramChange() && m.getProgramChangeNumber() == 5 ? 1 : 0;
        cc32 += m.isController() && m.getControllerNumber() == 32 ? 1 : 0;
    }
    REQUIRE (keyswitches.size() == 3);   // sustain, staccato, (marcato 는 키스위치가 없다), sustain
    CHECK (keyswitches[0] == std::make_pair ((int64_t) 0, 24));
    CHECK (keyswitches[1] == std::make_pair ((int64_t) 48000, 25));
    CHECK (keyswitches[2] == std::make_pair ((int64_t) 120000, 24));
    CHECK_EQ (programs, 1);
    CHECK_EQ (cc32, 1);
}

TEST (articulation_and_controller_validation)
{
    const auto reg = registryFile ("art_registry.json", R"("ks": { "type": "synth", "preset": "sine",
        "articulations": { "sustain": { "keyswitch": 24 }, "staccato": { "keyswitch": 25 } } },
        "bad": { "type": "synth", "preset": "sine", "controllers": { "cc64": "engine" } })");
    Registry r;
    Diagnostics d;
    r.addFile (reg, d);
    CHECK (mrtest::hasError (d, reg.getFullPathName().toStdString() + ":$.instruments.bad.controllers.cc64", "cannot process"));

    auto c = composition (R"({ "tempo": 120, "tracks": [
        { "name": "A", "instrument": "ks", "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 80, "articulation": "stacato" } ] },
        { "name": "B", "instrument": "synth.sine", "articulation": "pizzicato", "notes": [] } ] })");
    Diagnostics t;
    checkTrackAgainstInstrument (c.tracks[0], *r.find ("ks"), t);
    checkTrackAgainstInstrument (c.tracks[1], *r.find ("synth.sine"), t);
    bool suggested = false;
    for (const auto& e : t.errors)
        suggested = suggested || (e.path == "$.tracks[0].notes[0].articulation" && e.hint.find ("staccato") != std::string::npos);
    CHECK (suggested);
    CHECK (mrtest::hasError (t, "$.tracks[1].articulation", "has no articulations"));
}

TEST (sfz_routes_articulation_files_and_crossfades_layers)
{
    if (! sfzAvailable())
    {
        std::fprintf (stderr, "    skip: built without sfizz\n");
        return;
    }
    const auto reg = registryFile ("sfz_registry.json",
        R"("tone": { "type": "sfz", "file": ")" + fixture ("sfz/sine.sfz").getFullPathName().toStdString() + R"(",
             "articulations": { "sustain": {}, "high": { "file": ")" + fixture ("sfz/octave.sfz").getFullPathName().toStdString() + R"(" } } },
           "layered": { "type": "sfz", "file": ")" + fixture ("sfz/layers.sfz").getFullPathName().toStdString() + R"(",
             "dynamics": { "layers": [ 40, 110 ], "gainDb": [0, 0] } })");

    const auto routed = render (composition (R"({ "tempo": 60, "render": { "tailSeconds": 0.2 }, "tracks": [ { "name": "S", "instrument": "tone", "notes": [
        { "pitch": "A4", "startBeat": 0, "durationBeat": 1, "velocity": 100 },
        { "pitch": "A4", "startBeat": 1.5, "durationBeat": 1, "velocity": 100, "articulation": "high" } ] } ] })"), "sfz_routed.wav", reg);
    CHECK_NEAR (frequency (routed, 0.2, 0.9), 440.0, 1.5);
    CHECK_NEAR (frequency (routed, 1.7, 2.4), 880.0, 3.0);

    // 다이내믹스 곡선이 없으면 벨로시티가 레이어를 고른다
    const auto byVelocity = render (composition (R"({ "tempo": 60, "render": { "tailSeconds": 0.2 }, "tracks": [ { "name": "L", "instrument": "layered", "notes": [
        { "pitch": "A4", "startBeat": 0, "durationBeat": 1, "velocity": 30 },
        { "pitch": "A4", "startBeat": 1.5, "durationBeat": 1, "velocity": 120 } ] } ] })"), "sfz_velocity.wav", reg);
    CHECK_NEAR (frequency (byVelocity, 0.2, 0.9), 440.0, 1.5);
    CHECK_NEAR (frequency (byVelocity, 1.7, 2.4), 440.0 * std::pow (2.0, 7.0 / 12.0), 2.5);

    // CC1 곡선이 있으면 두 레이어를 함께 울리고 CC1 로 크로스페이드한다
    const auto faded = render (composition (R"({ "tempo": 60, "render": { "tailSeconds": 0.2 }, "tracks": [ { "name": "L", "instrument": "layered",
        "cc1": [ { "beat": 0, "value": 0 }, { "beat": 1, "value": 0 }, { "beat": 3, "value": 127 } ],
        "notes": [ { "pitch": "A4", "startBeat": 0, "durationBeat": 4, "velocity": 64 } ] } ] })"), "sfz_crossfade.wav", reg);
    CHECK_NEAR (frequency (faded, 0.2, 0.9), 440.0, 1.5);
    CHECK_NEAR (frequency (faded, 3.2, 3.9), 440.0 * std::pow (2.0, 7.0 / 12.0), 2.5);

    // 결정성
    const auto again = render (composition (R"({ "tempo": 60, "render": { "tailSeconds": 0.2 }, "tracks": [ { "name": "L", "instrument": "layered",
        "cc1": [ { "beat": 0, "value": 0 }, { "beat": 1, "value": 0 }, { "beat": 3, "value": 127 } ],
        "notes": [ { "pitch": "A4", "startBeat": 0, "durationBeat": 4, "velocity": 64 } ] } ] })"), "sfz_crossfade2.wav", reg);
    double maxDiff = 0.0;
    for (int i = 0; i < faded.getNumSamples(); ++i)
        maxDiff = std::max (maxDiff, (double) std::abs (faded.getSample (0, i) - again.getSample (0, i)));
    CHECK (maxDiff == 0.0);

    // 리전 옵코드: CC102 가 공격 시간을 늘린다. 아티큘레이션이 CC102 를 보낸다.
    const auto soft = registryFile ("sfz_opcodes.json",
        R"("soft": { "type": "sfz", "file": ")" + fixture ("sfz/sine.sfz").getFullPathName().toStdString() + R"(",
             "sfzRegionOpcodes": "ampeg_attack_oncc102=1.0",
             "articulations": { "sustain": { "cc": { "cc102": 0 } }, "legato": { "cc": { "cc102": 127 } } } })");
    const auto attacks = render (composition (R"({ "tempo": 60, "render": { "tailSeconds": 0.2 }, "tracks": [ { "name": "S", "instrument": "soft", "notes": [
        { "pitch": "A4", "startBeat": 0, "durationBeat": 2, "velocity": 100, "articulation": "sustain" },
        { "pitch": "A4", "startBeat": 3, "durationBeat": 2, "velocity": 100, "articulation": "legato" } ] } ] })"), "sfz_attack.wav", soft);
    CHECK (magnitude (attacks, 0.05, 0.15) > 0.5f * magnitude (attacks, 1.0, 1.5));     // 공격 1ms: 처음부터 크다
    CHECK (magnitude (attacks, 3.05, 3.15) < 0.25f * magnitude (attacks, 4.2, 4.7));    // 공격 1초: 천천히 커진다

    // #include 로 들어오는 리전에도 옵코드가 붙는다
    const auto incl = registryFile ("sfz_include.json",
        R"("incl": { "type": "sfz", "file": ")" + fixture ("sfz/included.sfz").getFullPathName().toStdString() + R"(", "sfzRegionOpcodes": "ampeg_attack=1.0" })");
    const auto included = render (composition (R"({ "tempo": 60, "render": { "tailSeconds": 0.2 }, "tracks": [ { "name": "I", "instrument": "incl", "notes": [
        { "pitch": "A4", "startBeat": 0, "durationBeat": 2, "velocity": 100 } ] } ] })"), "sfz_include.wav", incl);
    CHECK (magnitude (included, 1.2, 1.7) > 0.01f);
    CHECK (magnitude (included, 0.05, 0.15) < 0.25f * magnitude (included, 1.2, 1.7));

    // 없는 SFZ 파일은 instrument 오류다
    const auto missing = registryFile ("sfz_missing.json", R"("ghost": { "type": "sfz", "file": "nowhere/ghost.sfz" })");
    bool threw = false;
    try { render (tone ("ghost", {}), "sfz_missing.wav", missing); }
    catch (const Failure& f) { threw = std::string (f.what()).find ("SFZ file not found") != std::string::npos; }
    CHECK (threw);
}

TEST (vsco_strings_render_when_the_samples_are_present)
{
    const auto vsco = mrtest::sourceDir().getChildFile ("../external/vsco2-ce/ViolinEnsSusVib.sfz");
    if (! sfzAvailable() || ! vsco.existsAsFile())
    {
        std::fprintf (stderr, "    skip: VSCO 2 CE samples not present (./build.sh --samples)\n");
        return;
    }
    RenderOptions o;
    o.bitDepth = 32;
    o.output = mrtest::outputDir().getChildFile ("vsco_violins.wav");
    auto c = composition (R"({ "tempo": 72, "render": { "tailSeconds": 1 }, "tracks": [ { "name": "Vln", "instrument": "violins",
        "cc1": [ { "beat": 0, "value": 30 }, { "beat": 4, "value": 110 } ],
        "notes": [ { "pitch": "E4", "startBeat": 0, "durationBeat": 4, "velocity": 70 }, { "pitch": "B4", "startBeat": 0, "durationBeat": 4, "velocity": 70 } ] } ] })");
    const auto report = renderComposition (c, o);
    CHECK_EQ (report.tracks[0].description, std::string ("sfz:violins"));
    CHECK (report.stats.peakDb > -40.0);
    // 크레셴도: 끝이 처음보다 크다
    const auto b = readAudioFile (o.output).buffer;
    CHECK (b.getRMSLevel (0, (int) (2.6 * 48000), 24000) > 2.0f * b.getRMSLevel (0, (int) (0.4 * 48000), 24000));
}
