#include "TestFramework.h"
#include "TestUtil.h"

#include "composition/Document.h"
#include "instruments/SimpleSynth.h"
#include "io/AudioFile.h"
#include "realtime/RealtimePlayer.h"
#include "rendering/OfflineRenderer.h"

#include <cmath>

using namespace mr;

namespace
{

constexpr double kRate = 48000.0;

// 화음과 짧은 음, 끝난 뒤의 꼬리 (릴리스를 잴 수 있게)
std::string song (const std::string& instrument, const std::string& synth = {})
{
    return R"({ "tempo": 120, "render": { "tailSeconds": 2.0 }, "tracks": [ { "name": "S", "instrument": ")" + instrument + "\""
           + (synth.empty() ? std::string() : ", \"synth\": " + synth) + R"(,
      "notes": [ { "pitch": "A3", "startBeat": 0, "durationBeat": 2, "velocity": 100 }, { "pitch": "E4", "startBeat": 0, "durationBeat": 2, "velocity": 90 },
                 { "pitch": "C5", "startBeat": 2, "durationBeat": 0.5, "velocity": 70 } ] } ] })";
}

Composition parsed (const std::string& json)
{
    auto r = mrtest::parse (json);
    if (! r.composition)
        for (const auto& e : r.diagnostics.errors)
            std::fprintf (stderr, "    parse error %s: %s\n", e.path.c_str(), e.message.c_str());
    return *r.composition;
}

juce::AudioBuffer<float> render (const Composition& c, const std::string& name)
{
    RenderOptions o;
    o.useDefaultRegistry = false;
    o.bitDepth = 32;
    o.output = mrtest::outputDir().getChildFile (name);
    renderComposition (c, o);
    return readAudioFile (o.output).buffer;
}

double rms (const juce::AudioBuffer<float>& b, double from, double to)
{
    return b.getRMSLevel (0, (int) (from * kRate), (int) ((to - from) * kRate));
}

// 밝기: 이웃 샘플 차이의 RMS 를 RMS 로 나눈 값 (높은 대역이 많을수록 크다)
double brightness (const juce::AudioBuffer<float>& b, double from, double to)
{
    double diff = 0.0, power = 0.0;
    for (int i = (int) (from * kRate) + 1; i < (int) (to * kRate); ++i)
    {
        const double x = b.getSample (0, i), d = x - b.getSample (0, i - 1);
        diff += d * d;
        power += x * x;
    }
    return std::sqrt (diff / std::max (1.0e-12, power));
}

double maxDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    double d = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < std::min (a.getNumSamples(), b.getNumSamples()); ++i)
            d = std::max (d, (double) std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
    return d;
}

juce::AudioBuffer<float> pull (RealtimePlayer& p, double seconds)
{
    const int total = (int) std::llround (seconds * kRate);
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

TEST (synth_parameter_defaults_reproduce_every_preset)
{
    // 모든 값을 기본값으로 적어도 프리셋 그대로와 한 샘플도 다르지 않다
    for (const auto& preset : simpleSynthPresets())
    {
        const auto params = synthParameters (preset);
        REQUIRE (! params.empty());
        juce::String values = "{";
        for (size_t i = 0; i < params.size(); ++i)
        {
            const auto& p = params[i];
            CHECK (p.defaultValue >= p.minimum && p.defaultValue <= p.maximum);
            CHECK (! p.section.empty());
            values << (i > 0 ? ", " : "") << "\"" << p.key << "\": " << juce::String (p.defaultValue, 17);
        }
        values << "}";
        const auto plain = render (parsed (song ("synth." + preset)), "synth_plain.wav");
        const auto explicitDefaults = render (parsed (song ("synth." + preset, values.toStdString())), "synth_defaults.wav");
        const double diff = maxDiff (plain, explicitDefaults);
        if (! CHECK_EQ (diff, 0.0))
            std::fprintf (stderr, "    preset %s\n", preset.c_str());
    }
    CHECK_EQ (synthEngineName ("pad"), std::string ("subtractive"));
    CHECK_EQ (synthEngineName ("piano"), std::string ("piano"));
    CHECK_EQ (synthEngineName ("edm-drums"), std::string ("drums"));
    CHECK (synthParameters ("nonsense").empty());
}

TEST (synth_values_change_the_sound)
{
    const auto plain = render (parsed (song ("synth.pad")), "synth_pad.wav");
    const auto dark = render (parsed (song ("synth.pad", R"({ "cutoffHz": 250, "filterEnvHz": 0, "filterVelHz": 0 })")), "synth_pad_dark.wav");
    CHECK (brightness (dark, 1.0, 1.9) < brightness (plain, 1.0, 1.9) * 0.6);

    // 레벨 +6.02 dB 는 진폭 두 배 (출력 게인은 포화 뒤라 선형이다)
    const auto louder = render (parsed (song ("synth.pad", R"({ "levelDb": 6.0206 })")), "synth_pad_loud.wav");
    CHECK_NEAR (rms (louder, 0.5, 1.9) / rms (plain, 0.5, 1.9), 2.0, 0.002);

    // 릴리스가 길면 음이 끝난 뒤(1.25초)에도 오래 남는다
    const auto shortTail = render (parsed (song ("synth.lead", R"({ "release": 0.05 })")), "synth_lead_short.wav");
    const auto longTail = render (parsed (song ("synth.lead", R"({ "release": 2.0 })")), "synth_lead_long.wav");
    CHECK (rms (longTail, 1.6, 2.2) > 20.0 * rms (shortTail, 1.6, 2.2) + 1.0e-6);

    // 어택이 길면 처음 0.05초가 조용하다
    const auto slow = render (parsed (song ("synth.lead", R"({ "attack": 0.8 })")), "synth_lead_slow.wav");
    const auto fast = render (parsed (song ("synth.lead")), "synth_lead_fast.wav");
    CHECK (rms (slow, 0.0, 0.05) < 0.1 * rms (fast, 0.0, 0.05));

    // 피아노: 고음의 울림을 짧게
    const auto piano = render (parsed (song ("synth.piano")), "synth_piano.wav");
    const auto dry = render (parsed (song ("synth.piano", R"({ "decayHigh": 0.15, "decayLow": 0.4 })")), "synth_piano_dry.wav");
    CHECK (rms (dry, 0.7, 0.95) < 0.3 * rms (piano, 0.7, 0.95));

    // 필터를 끄면 컷오프와 상관없이 밝다
    const auto open = render (parsed (song ("synth.pad", R"({ "filter": 0, "cutoffHz": 100 })")), "synth_pad_open.wav");
    CHECK (brightness (open, 1.0, 1.9) > brightness (plain, 1.0, 1.9));
}

TEST (synth_values_come_from_registry_definitions_and_are_checked)
{
    // 곡 안의 정의로 이름 붙인 신스 소리, 그 위에 트랙 값
    const auto named = parsed (R"({ "tempo": 120, "render": { "tailSeconds": 1 },
      "instruments": [ { "dark-pad": { "type": "synth", "preset": "pad", "parameters": { "cutoffHz": 250, "filterEnvHz": 0, "filterVelHz": 0 } } } ],
      "tracks": [ { "name": "S", "instrument": "dark-pad", "notes": [ { "pitch": "A3", "startBeat": 0, "durationBeat": 2, "velocity": 100 } ] } ] })");
    const auto direct = parsed (R"({ "tempo": 120, "render": { "tailSeconds": 1 },
      "tracks": [ { "name": "S", "instrument": "synth.pad", "synth": { "cutoffHz": 250, "filterEnvHz": 0, "filterVelHz": 0 },
                    "notes": [ { "pitch": "A3", "startBeat": 0, "durationBeat": 2, "velocity": 100 } ] } ] })");
    CHECK_EQ (maxDiff (render (named, "synth_named.wav"), render (direct, "synth_direct.wav")), 0.0);

    auto badKey = mrtest::parse (song ("synth.pad", R"({ "cutof": 200 })"));
    CHECK (mrtest::hasError (badKey.diagnostics, "$.tracks[0].synth.cutof", "unknown synth parameter"));
    CHECK (badKey.diagnostics.errors[0].hint.find ("cutoffHz") != std::string::npos);
    auto badRange = mrtest::parse (song ("synth.pad", R"({ "resonance": 2 })"));
    CHECK (mrtest::hasError (badRange.diagnostics, "$.tracks[0].synth.resonance"));

    RenderOptions o;
    o.useDefaultRegistry = false;
    Diagnostics d;
    const auto badDef = parsed (R"({ "tempo": 120, "instruments": [ { "x": { "type": "synth", "preset": "pad", "parameters": { "atack": 1 } } } ],
      "tracks": [ { "name": "S", "instrument": "x", "notes": [] } ] })");
    buildRegistry (badDef, o, d);
    CHECK (! d.ok());

    // 대체 악기가 내장 신스면 신스 값을 쓰고 경고하지 않는다
    auto onSfz = parsed (R"({ "tempo": 120, "instruments": [ { "keys": { "type": "sfz", "file": "missing.sfz", "fallback": "synth.piano" } } ],
      "tracks": [ { "name": "S", "instrument": "keys", "synth": { "release": 1 }, "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 90 } ] } ] })");
    const auto report = renderComposition (onSfz, o);
    CHECK (std::none_of (report.warnings.begin(), report.warnings.end(), [] (const Diagnostic& w) { return w.path == "$.tracks[0].synth"; }));   // 대체한 synth.piano 는 내장 신스
}

TEST (realtime_apply_changes_synth_values_like_a_fresh_player)
{
    PlayerOptions po;
    po.useDefaultRegistry = false;
    po.threads = 0;
    const auto before = parsed (song ("synth.supersaw"));
    const auto after = parsed (song ("synth.supersaw", R"({ "cutoffHz": 600, "resonance": 0.5, "attack": 0.05, "release": 0.6, "detune": 1.8, "levelDb": -3 })"));
    RealtimePlayer fresh (after, po), applied (before, po);
    CHECK (applied.apply (after));
    fresh.play();
    applied.play();
    const double seconds = (double) fresh.length() / kRate;
    const auto a = pull (fresh, seconds), b = pull (applied, seconds);
    CHECK (rms (a, 0.2, 0.9) > 0.005);
    CHECK_EQ (maxDiff (a, b), 0.0);

    // 재생 중에 바꾸면 다음 블록부터 어두워진다 (울리는 음도)
    RealtimePlayer live (before, po);
    live.play();
    const auto first = pull (live, 0.5);
    CHECK (live.apply (parsed (song ("synth.supersaw", R"({ "cutoffHz": 150, "filterEnvHz": 0, "filterVelHz": 0 })"))));
    const auto second = pull (live, 0.45);
    CHECK (brightness (second, 0.15, 0.45) < 0.5 * brightness (first, 0.2, 0.5));
    CHECK (live.apply (before));
}

TEST (synth_filter_response_and_waveform_for_the_panel)
{
    const std::vector<double> freqs { 100.0, 1000.0, 10000.0 };
    const auto flat = synthFilterResponseDb (1000.0, 0.0, kRate, freqs);   // k = 2 (Q 0.5): 차단 주파수에서 -6 dB
    CHECK_NEAR (flat[0], 0.0, 0.2);
    CHECK_NEAR (flat[1], -6.02, 0.05);
    CHECK (flat[2] < -35.0);
    const auto peaky = synthFilterResponseDb (1000.0, 0.9, kRate, { 1000.0 });   // k = 0.2 (Q 5): +14 dB
    CHECK_NEAR (peaky[0], 13.98, 0.1);

    const auto sine = synthWaveform ("sine", {}, 64);
    REQUIRE (sine.size() == 64);
    CHECK_NEAR (sine[16], 1.0, 1.0e-6);   // 4분의 1 주기에서 꼭대기
    CHECK_NEAR (sine[48], -1.0, 1.0e-6);
    CHECK (synthWaveform ("pad", {}, 128).size() == 128);
    CHECK (synthWaveform ("piano", {}, 128).empty());
}

TEST (document_writes_synth_values)
{
    std::string error;
    auto doc = CompositionDocument::fromText (R"({ "tempo": 120, "tracks": [ { "name": "S", "instrument": "synth.pad", "notes": [] } ] })",
                                              mrtest::sourceDir().getChildFile ("tests/inline.json"), error);
    REQUIRE (doc.has_value());
    auto track = withSynthValue (doc->track (0), "cutoffHz", 800.0);
    track = withSynthValue (track, "release", 1.5);
    CHECK_EQ (writeJson (track["synth"]), std::string (R"({"cutoffHz": 800.0, "release": 1.5})"));
    track = withSynthValue (track, "cutoffHz", juce::var());
    track = withSynthValue (track, "release", juce::var());
    CHECK (! track.hasProperty ("synth"));
    CHECK (! withInstrument (withSynthValue (track, "attack", 0.1), "synth.lead").hasProperty ("synth"));
}

namespace
{

// 위로 지나는 영점(샘플 사이를 직선으로 이어 찾는다)으로 잰 주파수
std::vector<double> upwardCrossings (const juce::AudioBuffer<float>& b, double from, double to)
{
    std::vector<double> ups;
    for (int i = (int) (from * kRate) + 1; i < (int) (to * kRate); ++i)
    {
        const double a = b.getSample (0, i - 1), c = b.getSample (0, i);
        if (a < 0.0 && c >= 0.0)
            ups.push_back ((i - 1 + a / (a - c)) / kRate);
    }
    return ups;
}

double frequency (const juce::AudioBuffer<float>& b, double from, double to)
{
    const auto ups = upwardCrossings (b, from, to);
    return ups.size() < 2 ? 0.0 : (double) (ups.size() - 1) / (ups.back() - ups.front());
}

std::string bassSong (const std::string& notes, const std::string& synth = {})
{
    return R"({ "tempo": 120, "render": { "tailSeconds": 0.5 }, "tracks": [ { "name": "808", "instrument": "synth.808", )"
           + (synth.empty() ? std::string() : "\"synth\": " + synth + ", ")
           + R"("performance": { "timingHumanizeMs": 0, "velocityHumanize": 0, "noteLengthHumanize": 0 }, "notes": [ )" + notes + " ] } ] }";
}

std::string bassNote (int pitch, double start, double length)
{
    return "{ \"pitch\": " + std::to_string (pitch) + ", \"startBeat\": " + juce::String (start).toStdString() + ", \"durationBeat\": "
           + juce::String (length).toStdString() + ", \"velocity\": 100 }";
}

} // namespace

TEST (synth_808_drops_into_the_note_and_glides_to_overlapping_notes)
{
    const double c2 = 65.406, g2 = 97.999;
    // C2 0~1박, 겹친 G2 0.75~2박 (0.375초에 미끄러지기 시작), 쉰 뒤 C2 3~4박, 맞붙은 E2 4~5박
    const auto b = render (parsed (bassSong (bassNote (36, 0, 1) + ", " + bassNote (43, 0.75, 1.25) + ", " + bassNote (36, 3, 1) + ", "
                                             + bassNote (40, 4, 1))),
                           "synth_808.wav");
    // 위상 0 에서 시작하고, 첫 주기는 한 옥타브 위에서 내려앉는 중이라 C2 의 한 주기보다 짧다
    CHECK_EQ (b.getSample (0, 0), 0.0f);
    CHECK (b.getSample (0, 1) > 0.0f);
    const auto first = upwardCrossings (b, 0.0, 0.05);
    REQUIRE (! first.empty());
    CHECK (first[0] < 0.75 / c2);
    CHECK_NEAR (frequency (b, 0.15, 0.37), c2, c2 * 0.002);

    // 겹친 G2: 0.08초에 걸쳐 미끄러지고, 공격을 다시 하지 않는다
    const double mid = frequency (b, 0.395, 0.435);
    CHECK (mid > c2 * 1.1 && mid < g2 * 0.92);
    CHECK_NEAR (frequency (b, 0.5, 0.95), g2, g2 * 0.002);
    const double before = rms (b, 0.30, 0.37), after = rms (b, 0.38, 0.45);
    CHECK (after > before * 0.8 && after < before * 1.05);

    // 쉰 뒤의 음은 새로 친다 (다시 위상 0 에서, 내려앉는 첫 주기)
    const auto again = upwardCrossings (b, 1.5, 1.55);
    REQUIRE (! again.empty());
    CHECK (again[0] - 1.5 < 0.75 / c2);
    CHECK (std::abs (b.getSample (0, (int) (1.5 * kRate))) < 1.0e-6f);
    // 맞붙은 음(같은 샘플에 앞 음을 놓는다)도 새로 친다: 앞 음을 10ms 에 끊은 뒤 E2 가 위에서 내려앉는 중이다
    CHECK (frequency (b, 2.012, 2.06) > 82.407 * 1.04);
    CHECK_NEAR (frequency (b, 2.2, 2.45), 82.407, 82.407 * 0.002);
}

TEST (synth_808_mono_returns_to_held_notes_and_cuts_released_ones)
{
    const double c2 = 65.406;
    // 누르고 있는 C2 안의 짧은 G2: G2 를 놓으면 C2 로 돌아간다
    const auto back = render (parsed (bassSong (bassNote (36, 0, 2) + ", " + bassNote (43, 0.5, 0.5))), "synth_808_back.wav");
    CHECK_NEAR (frequency (back, 0.35, 0.49), 97.999, 0.3);
    CHECK_NEAR (frequency (back, 0.65, 0.95), c2, c2 * 0.002);

    // 같은 음을 겹쳐 누르면 먼저 누른 음을 놓아도 소리가 이어진다
    const auto same = render (parsed (bassSong (bassNote (36, 0, 2) + ", " + bassNote (36, 1, 2))), "synth_808_same.wav");
    CHECK (rms (same, 1.1, 1.4) > 0.6 * rms (same, 0.8, 0.95));

    // 릴리스가 길어도 다음 음이 앞 음을 10ms 에 끊는다: 두 음의 렌더에서 뒤 음만의 렌더를 빼면 끊은 뒤로는 남는 것이 없다
    const std::string longRelease = R"({ "release": 2.0 })";
    const auto both = render (parsed (bassSong (bassNote (36, 0, 2) + ", " + bassNote (43, 2.5, 1), longRelease)), "synth_808_both.wav");
    const auto alone = render (parsed (bassSong (bassNote (43, 2.5, 1), longRelease)), "synth_808_alone.wav");
    auto residual = [&] (double from, double to)
    {
        double peak = 0.0;
        for (int i = (int) (from * kRate); i < (int) (to * kRate); ++i)
            peak = std::max (peak, (double) std::abs (both.getSample (0, i) - alone.getSample (0, i)));
        return peak;
    };
    CHECK (residual (1.05, 1.24) > 0.05);   // 앞 음의 긴 릴리스
    CHECK (residual (1.27, 2.2) < 1.0e-5);
    // 모노를 끄면 두 소리가 겹친다
    const auto poly = render (parsed (bassSong (bassNote (36, 0, 2) + ", " + bassNote (43, 2.5, 1), R"({ "release": 2.0, "mono": 0 })")),
                              "synth_808_poly.wav");
    double polyPeak = 0.0;
    for (int i = (int) (1.3 * kRate); i < (int) (1.5 * kRate); ++i)
        polyPeak = std::max (polyPeak, (double) std::abs (poly.getSample (0, i) - alone.getSample (0, i)));
    CHECK (polyPeak > 0.01);
}

TEST (synth_808_realtime_matches_the_offline_render)
{
    PlayerOptions po;
    po.useDefaultRegistry = false;
    po.threads = 0;
    const auto c = parsed (bassSong (bassNote (36, 0, 1) + ", " + bassNote (43, 0.75, 1.25) + ", " + bassNote (39, 1.9, 0.6) + ", "
                                     + bassNote (36, 3, 1)));
    RealtimePlayer player (c, po);
    player.play();
    const auto live = pull (player, (double) player.length() / kRate);
    const auto offline = render (c, "synth_808_offline.wav");
    CHECK (rms (offline, 0.1, 1.0) > 0.05);
    CHECK (maxDiff (live, offline) < 1.0e-6);
}
