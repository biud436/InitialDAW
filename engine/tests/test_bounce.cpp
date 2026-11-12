#include "TestFramework.h"
#include "TestUtil.h"

#include "io/AudioFile.h"
#include "rendering/Bounce.h"
#include "rendering/OfflineRenderer.h"

#include <cmath>

using namespace mr;

namespace
{

// 4마디 쉬고 시작하는 리드(트랙 효과, 페이더, 밸런스, 센드), 사이드체인 키 트랙, 다른 트랙
std::string song (const std::string& leadMixer = R"({ "gainDb": -4, "pan": 0.4 })", const std::string& extraTracks = {})
{
    return R"({ "tempo": 120, "timeSignature": [3, 4], "render": { "tailSeconds": 1 },
  "buses": [ { "name": "echo", "effects": [ { "type": "delay", "timeBeats": 0.5, "feedback": 0.3, "mix": 1 } ] } ],
  "tracks": [
    { "name": "Kick", "instrument": "synth.edm-drums", "notes": [ { "pitch": 36, "startBeat": 12, "durationBeat": 0.5, "velocity": 110 },
                                                                   { "pitch": 36, "startBeat": 14, "durationBeat": 0.5, "velocity": 110 } ] },
    { "name": "Lead", "instrument": "synth.pluck", "mixer": )" + leadMixer + R"(,
      "effects": [ { "type": "eq", "bands": [ { "type": "lowShelf", "freqHz": 200, "gainDb": -6 } ] },
                   { "type": "compressor", "thresholdDb": -30, "ratio": 4, "sidechain": "Kick" } ],
      "sends": [ { "bus": "echo", "levelDb": -8 } ],
      "notes": [ { "pitch": 64, "startBeat": 12.5, "durationBeat": 1, "velocity": 90 },
                 { "pitch": 67, "startBeat": 14, "durationBeat": 2, "velocity": 100 } ] },
    { "name": "Pad", "instrument": "synth.pad", "notes": [ { "pitch": 48, "startBeat": 0, "durationBeat": 20, "velocity": 70 } ] } )"
           + extraTracks + " ] }";
}

Composition parsed (const std::string& text)
{
    auto r = mrtest::parse (text);
    if (! r.composition)
        for (const auto& e : r.diagnostics.errors)
            std::fprintf (stderr, "    parse error %s: %s\n", e.path.c_str(), e.message.c_str());
    REQUIRE (r.composition.has_value());
    return *r.composition;
}

juce::AudioBuffer<float> render (const Composition& c, const char* name)
{
    RenderOptions o;
    o.useDefaultRegistry = false;
    o.bitDepth = 32;
    o.output = mrtest::outputDir().getChildFile (name);
    renderComposition (c, o);
    return readAudioFile (o.output).buffer;
}

double maxDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int n)
{
    double d = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
            d = std::max (d, (double) std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
    return d;
}

} // namespace

TEST (bounce_keeps_only_the_track_and_its_keys)
{
    const auto c = bounceComposition (parsed (song()), 1);
    CHECK (! c.tracks[0].mixer.mute);   // 사이드체인 키
    CHECK (! c.tracks[1].mixer.mute);
    CHECK (c.tracks[2].mixer.mute);
    CHECK (c.buses[0].effects.empty());
}

TEST (bounced_audio_track_sounds_like_the_track)
{
    const auto file = mrtest::outputDir().getChildFile ("bounce-lead.wav");
    file.deleteFile();
    const auto original = parsed (song());
    BounceOptions bo;
    bo.useDefaultRegistry = false;
    BounceResult r;
    {
        BounceRender bounce (original, 1, bo);
        r = bounce.run (file);
    }
    CHECK (! r.silent);
    CHECK_NEAR (r.startBeat, 12.0, 1.0e-9);   // 3/4 의 다섯째 마디 (첫 음은 12.5 박)
    CHECK (r.lengthSeconds > 2.0 && r.lengthSeconds < 6.0);
    CHECK (file.existsAsFile());

    // 원래 트랙을 끄고 같은 믹서와 센드의 오디오 트랙을 더한 곡이 원래 곡과 같다 (24비트 안)
    const auto clip = R"(, { "name": "Lead audio", "instrument": "audio", "mixer": { "gainDb": -4, "pan": 0.4 },
      "sends": [ { "bus": "echo", "levelDb": -8 } ],
      "clips": [ { "file": ")" + file.getFullPathName().toStdString() + R"(", "startBeat": )" + juce::String (r.startBeat).toStdString()
                      + R"(, "lengthSeconds": )" + juce::String (r.lengthSeconds, 9).toStdString() + " } ] }";
    const auto text = song (R"({ "gainDb": -4, "pan": 0.4, "mute": true })", clip);
    const auto bounced = parsed (text);
    REQUIRE (bounced.tracks.size() == 4);
    CHECK (bounced.tracks[1].mixer.mute);
    const auto a = render (original, "bounce-original.wav");
    const auto b = render (bounced, "bounce-replaced.wav");
    const int n = std::min (a.getNumSamples(), b.getNumSamples());
    CHECK (std::abs (a.getNumSamples() - b.getNumSamples()) <= 1);
    const double diff = maxDiff (a, b, n);
    std::printf ("    bounce: start beat %.2f, %.2f s, peak %.1f dBFS, max difference %.2e\n", r.startBeat, r.lengthSeconds, r.peakDb, diff);
    CHECK (diff < 2.0e-5);
}

TEST (bounce_of_a_silent_track_writes_nothing)
{
    const auto file = mrtest::outputDir().getChildFile ("bounce-silent.wav");
    file.deleteFile();
    auto c = parsed (song());
    c.tracks[1].notes.clear();
    BounceOptions bo;
    bo.useDefaultRegistry = false;
    BounceRender bounce (c, 1, bo);
    const auto r = bounce.run (file);
    CHECK (r.silent);
    CHECK (! file.exists());
}
