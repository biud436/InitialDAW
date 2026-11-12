#include "TestFramework.h"
#include "TestUtil.h"

#include "composition/Document.h"
#include "instruments/SimpleSynth.h"
#include "io/AudioFile.h"
#include "realtime/RealtimePlayer.h"
#include "rendering/OfflineRenderer.h"
#include "timing/TempoMap.h"

#include <cmath>

using namespace mr;

namespace
{

constexpr double kRate = 48000.0;
constexpr double kPi = 3.141592653589793;

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
    o.output = mrtest::outputDir().getChildFile (name + ".wav");
    renderComposition (c, o);
    return readAudioFile (o.output).buffer;
}

// 한 음 하나: 클럽 키트의 그 건반을 박 0 에서 (템포, 트랙의 synth 값, 곡선은 json 조각으로)
juce::AudioBuffer<float> hit (int pitch, const std::string& name, double tempo = 120.0, const std::string& synth = {}, double tail = 1.0,
                              const std::string& extra = {})
{
    return render (parsed (R"({ "tempo": )" + juce::String (tempo).toStdString() + R"(, "render": { "tailSeconds": )" + juce::String (tail).toStdString()
                           + R"( }, "tracks": [ { "name": "K", "instrument": "synth.club-drums", )" + (synth.empty() ? std::string() : "\"synth\": " + synth + ", ")
                           + extra + R"("notes": [ { "pitch": )" + std::to_string (pitch)
                           + R"(, "startBeat": 0, "durationBeat": 0.5, "velocity": 110 } ] } ] })"),
                   name);
}

double rmsDb (const juce::AudioBuffer<float>& b, double fromSec, double toSec, int channel = 0)
{
    const int from = std::clamp ((int) (fromSec * kRate), 0, b.getNumSamples());
    const int count = std::clamp ((int) ((toSec - fromSec) * kRate), 0, b.getNumSamples() - from);
    return juce::Decibels::gainToDecibels ((double) b.getRMSLevel (channel, from, count), -200.0);
}

double maxAbs (const juce::AudioBuffer<float>& b, double fromSec, double toSec)
{
    const int from = std::clamp ((int) (fromSec * kRate), 0, b.getNumSamples());
    const int to = std::clamp ((int) (toSec * kRate), 0, b.getNumSamples());
    double m = 0.0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = from; i < to; ++i)
            m = std::max (m, (double) std::abs (b.getSample (ch, i)));
    return m;
}

// 2ms 창의 RMS 가운데 가장 큰 값 (dB)
double peakDb (const juce::AudioBuffer<float>& b)
{
    double best = -200.0;
    for (int i = 0; i + 96 <= b.getNumSamples(); i += 96)
        best = std::max (best, juce::Decibels::gainToDecibels ((double) b.getRMSLevel (0, i, 96), -200.0));
    return best;
}

// 20~maxHz 에서 가장 센 성분 (힘으로 찾는다: 포화한 저음은 영점 교차로 세면 틀린다)
double strongestHz (const juce::AudioBuffer<float>& b, double fromSec, double toSec, double maxHz = 400.0)
{
    const int from = (int) (fromSec * kRate), to = (int) (toSec * kRate);
    double bestHz = 0.0, best = -1.0;
    for (double hz = 20.0; hz <= maxHz; hz += 0.25)
    {
        double re = 0.0, im = 0.0;
        for (int i = from; i < to; ++i)
        {
            const double w = 2.0 * kPi * hz * (i - from) / kRate;
            const double x = (double) b.getSample (0, i) + b.getSample (1, i);
            re += x * std::cos (w);
            im += x * std::sin (w);
        }
        if (const double m = re * re + im * im; m > best)
        {
            best = m;
            bestHz = hz;
        }
    }
    return bestHz;
}

// 고역(cutoff 위)의 몫 (dB): juce 2차 고역 통과 두 단
double highShareDb (const juce::AudioBuffer<float>& b, double fromSec, double toSec, double cutoffHz)
{
    juce::IIRFilter hp1, hp2;
    hp1.setCoefficients (juce::IIRCoefficients::makeHighPass (kRate, cutoffHz));
    hp2.setCoefficients (juce::IIRCoefficients::makeHighPass (kRate, cutoffHz));
    const int from = (int) (fromSec * kRate), to = (int) (toSec * kRate);
    double hi = 0.0, all = 0.0;
    for (int i = 0; i < std::min (to, b.getNumSamples()); ++i)
    {
        const float x = b.getSample (0, i);
        const float y = hp2.processSingleSampleRaw (hp1.processSingleSampleRaw (x));
        if (i >= from)
        {
            hi += (double) y * y;
            all += (double) x * x;
        }
    }
    return 10.0 * std::log10 (std::max (1.0e-30, hi) / std::max (1.0e-30, all));
}

double correlation (const juce::AudioBuffer<float>& b, double fromSec, double toSec)
{
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (int i = (int) (fromSec * kRate); i < (int) (toSec * kRate); ++i)
    {
        const double a = b.getSample (0, i), c = b.getSample (1, i);
        lr += a * c;
        ll += a * a;
        rr += c * c;
    }
    return lr / std::sqrt (ll * rr + 1.0e-30);
}

double lastSoundSec (const juce::AudioBuffer<float>& b)
{
    for (int i = b.getNumSamples() - 1; i >= 0; --i)
        if (b.getSample (0, i) != 0.0f || b.getSample (1, i) != 0.0f)
            return (i + 1) / kRate;
    return 0.0;
}

} // namespace

TEST (club_kick_has_a_click_and_a_tuned_body_that_fades_before_the_next_beat)
{
    const auto kick = hit (36, "club_kick");
    const double peak = peakDb (kick);
    CHECK (peak > -12.0);
    CHECK_NEAR (strongestHz (kick, 0.12, 0.3), 55.0, 2.0);                    // A1 at tune 0
    CHECK_NEAR (strongestHz (hit (36, "club_kick_tuned", 120.0, R"({ "tune": -3 })"), 0.12, 0.3), 46.25, 2.0);   // F#1
    CHECK (highShareDb (kick, 0.0, 0.004, 2000.0) > -12.0);                    // the click: energy above 2 kHz in the first 4 ms
    CHECK (highShareDb (kick, 0.0, 0.3, 2000.0) < -20.0);                      // the body carries the energy
    CHECK (rmsDb (kick, 0.395, 0.405) < peak - 60.0);                          // silent at the next beat at 150 BPM
    CHECK (maxAbs (kick, 0.355, 0.4) < 1.0e-4);                                // faded out, no hard stop
    CHECK (lastSoundSec (kick) < 0.375);

    const auto longKick = hit (35, "club_long_kick", 120.0, {}, 2.0);
    CHECK_NEAR (strongestHz (longKick, 0.15, 0.5), 55.0, 2.0);
    CHECK (rmsDb (longKick, 0.5, 0.6) > peakDb (longKick) - 20.0);             // a long 808 body
    CHECK (maxAbs (longKick, 1.345, 1.5) < 1.0e-4);
}

TEST (club_snares_clap_and_hats_have_realistic_lengths)
{
    const auto snare = hit (38, "club_snare");
    CHECK (peakDb (snare) > -20.0);
    CHECK (maxAbs (snare, 0.295, 0.5) < 1.0e-4);
    const auto roll = hit (40, "club_roll_snare");
    CHECK (rmsDb (roll, 0.05, 0.055) < peakDb (roll) - 18.0);                  // a 1/32 roll at 150 BPM (50 ms) does not smear
    CHECK (maxAbs (roll, 0.11, 0.3) == 0.0);

    const auto clap = hit (39, "club_clap");
    CHECK (peakDb (clap) > -24.0);
    CHECK (correlation (clap, 0.0, 0.3) < 0.8);                                 // two layers with different noise left and right
    CHECK (maxAbs (clap, 0.455, 0.6) < 1.0e-4);

    const auto closed = hit (42, "club_closed_hat");
    CHECK (highShareDb (closed, 0.0, 0.05, 5000.0) > -3.0);
    CHECK (maxAbs (closed, 0.1, 0.3) < 1.0e-4);
    const auto open = hit (46, "club_open_hat", 120.0, {}, 1.5);
    CHECK (rmsDb (open, 0.3, 0.4) > peakDb (open) - 30.0);                     // rings on
    CHECK (maxAbs (open, 0.845, 1.2) < 1.0e-4);

    // the closed hat chokes the open hat
    const auto choked = render (parsed (R"({ "tempo": 120, "render": { "tailSeconds": 1 }, "tracks": [ { "name": "K", "instrument": "synth.club-drums",
      "notes": [ { "pitch": 46, "startBeat": 0, "durationBeat": 0.5, "velocity": 110 }, { "pitch": 44, "startBeat": 0.25, "durationBeat": 0.25, "velocity": 1 } ] } ] })"),
                                "club_choke");
    CHECK (rmsDb (choked, 0.3, 0.4) < rmsDb (open, 0.3, 0.4) - 30.0);

    const auto ride = hit (51, "club_ride", 120.0, {}, 3.5);
    CHECK (rmsDb (ride, 1.5, 1.7) > peakDb (ride) - 40.0);                     // a long wash
    CHECK (maxAbs (ride, 3.2, 3.5) < 1.0e-4);
    CHECK (correlation (ride, 0.2, 1.0) < 0.95);
    CHECK_NEAR (strongestHz (hit (45, "club_tom"), 0.2, 0.5), 144.0, 4.0);
    const auto silent = hit (37, "club_unmapped");
    CHECK (maxAbs (silent, 0.0, 1.0) == 0.0);                                  // keys outside the kit are silent
}

TEST (club_key_outside_the_kit_does_not_take_a_voice)
{
    // 48 목소리가 모두 크래시로 울리는 중에 배치에 없는 건반(37)을 쳐도 울리는 소리가 끊기지 않는다
    auto song = [] (bool unmapped)
    {
        std::string notes;
        for (int i = 0; i < 48; ++i)
            notes += std::string (i > 0 ? ", " : "") + R"({ "pitch": 49, "startBeat": )" + std::to_string (i * 0.01) + R"(, "durationBeat": 0.5, "velocity": 20 })";
        if (unmapped)
            notes += R"(, { "pitch": 37, "startBeat": 1, "durationBeat": 0.5, "velocity": 100 })";
        return parsed (R"({ "tempo": 120, "render": { "tailSeconds": 3 }, "tracks": [ { "name": "K", "instrument": "synth.club-drums", "notes": [ )" + notes
                       + " ] } ] }");
    };
    const auto full = render (song (false), "club_full_voices");
    const auto withKey = render (song (true), "club_full_voices_unmapped");
    CHECK (rmsDb (full, 0.6, 1.0) > -60.0);
    double diff = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < std::min (full.getNumSamples(), withKey.getNumSamples()); ++i)
            diff = std::max (diff, (double) std::abs (full.getSample (ch, i) - withKey.getSample (ch, i)));
    CHECK (diff == 0.0);
}

TEST (club_effects_last_whole_beats_and_end_on_the_beat)
{
    // riser 16 beats at 120 BPM: grows 30 dB and brightens, ends at 8.0 s
    const auto riser = hit (66, "club_riser16", 120.0, {}, 8.5);   // a drum sound outlasting its note needs a tail to be heard
    CHECK (rmsDb (riser, 7.8, 7.98) > rmsDb (riser, 0.2, 0.6) + 20.0);
    CHECK (highShareDb (riser, 7.5, 7.95, 4000.0) > highShareDb (riser, 0.5, 1.5, 4000.0) + 15.0);
    CHECK (maxAbs (riser, 7.9, 7.98) > 0.05);
    CHECK (maxAbs (riser, 8.0005, 8.5) == 0.0);
    // the same riser 8 beats at 150 BPM ends at 3.2 s
    const auto fast = hit (65, "club_riser8_150", 150.0, {}, 3.5);
    CHECK (maxAbs (fast, 3.15, 3.19) > 0.05 && maxAbs (fast, 3.2005, 3.6) == 0.0);
    // on an accelerating tempo it ends where the tempo map puts beat 8
    const auto ramp = render (parsed (R"({ "tempo": [ { "beat": 0, "bpm": 100, "ramp": true }, { "beat": 8, "bpm": 160 } ], "render": { "tailSeconds": 4 },
      "tracks": [ { "name": "K", "instrument": "synth.club-drums", "notes": [ { "pitch": 65, "startBeat": 0, "durationBeat": 1, "velocity": 110 } ] } ] })"),
                              "club_riser_ramp");
    const double end = TempoMap ({ TempoPoint { 0.0, 100.0, true }, TempoPoint { 8.0, 160.0, false } }).secondsAt (8.0);
    CHECK (maxAbs (ramp, end - 0.05, end - 0.013) > 0.05);
    CHECK (maxAbs (ramp, end + 0.0005, end + 0.5) == 0.0);

    // reverse cymbal 2 beats: swells about 39 dB into beat 2 (1.0 s at 120 BPM)
    const auto rev = hit (68, "club_reverse2");
    CHECK (rmsDb (rev, 0.92, 0.99) > rmsDb (rev, 0.02, 0.1) + 25.0);
    CHECK (maxAbs (rev, 1.0005, 1.5) == 0.0);
    // downlifter 8 beats: darker and 20 dB quieter by its end, then gone at 4.0 s
    const auto down = hit (63, "club_downlifter8", 120.0, {}, 4.0);
    CHECK (rmsDb (down, 0.05, 0.3) > rmsDb (down, 2.8, 3.2) + 15.0);
    CHECK (highShareDb (down, 0.05, 0.5, 4000.0) > highShareDb (down, 2.5, 3.0, 4000.0) + 15.0);
    CHECK (maxAbs (down, 4.0005, 4.5) == 0.0);

    // impact: a sub boom near E1 that fades out by 3.6 s instead of stopping
    const auto impact = hit (60, "club_impact", 120.0, {}, 4.0);
    CHECK (peakDb (impact) > -12.0);
    CHECK_NEAR (strongestHz (impact, 1.0, 2.0, 80.0), 41.2, 2.0);
    CHECK (rmsDb (impact, 3.3, 3.55) < peakDb (impact) - 40.0);
    CHECK (maxAbs (impact, 3.595, 4.0) < 1.0e-4);
}

TEST (drum_tune_and_pitch_bend_move_every_kit)
{
    // tune 0 and bend range 2 written out are the same samples as nothing written
    for (const char* preset : { "drums", "edm-drums", "trailer-drums", "club-drums" })
    {
        auto song = [preset] (const std::string& synth)
        {
            return parsed (std::string (R"({ "tempo": 120, "render": { "tailSeconds": 0.5 }, "tracks": [ { "name": "K", "instrument": "synth.)") + preset + "\", "
                           + synth + R"("notes": [ { "pitch": 36, "startBeat": 0, "durationBeat": 0.5, "velocity": 110 }, { "pitch": 38, "startBeat": 0.5, "durationBeat": 0.5, "velocity": 100 },
                                        { "pitch": 41, "startBeat": 1, "durationBeat": 0.5, "velocity": 100 }, { "pitch": 42, "startBeat": 1.5, "durationBeat": 0.5, "velocity": 100 } ] } ] })");
        };
        const auto plain = render (song (""), "drum_plain");
        const auto zero = render (song (R"("synth": { "tune": 0, "bendRange": 2 }, )"), "drum_zero");
        double diff = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < plain.getNumSamples(); ++i)
                diff = std::max (diff, (double) std::abs (plain.getSample (ch, i) - zero.getSample (ch, i)));
        if (! CHECK_EQ (diff, 0.0))
            std::fprintf (stderr, "    preset %s\n", preset);
    }
    // tune an octave up doubles the edm kick tail (46 Hz) and the taiko (62 Hz)
    auto one = [] (const char* preset, int pitch, const std::string& synth, const std::string& extra = {})
    {
        return render (parsed (std::string (R"({ "tempo": 120, "render": { "tailSeconds": 1 }, "tracks": [ { "name": "K", "instrument": "synth.)") + preset + "\", "
                               + (synth.empty() ? std::string() : "\"synth\": " + synth + ", ") + extra + R"("notes": [ { "pitch": )" + std::to_string (pitch)
                               + R"(, "startBeat": 0, "durationBeat": 0.5, "velocity": 110 } ] } ] })"),
                       "drum_one");
    };
    CHECK_NEAR (strongestHz (one ("edm-drums", 36, R"({ "tune": 12 })"), 0.15, 0.35), 92.0, 3.0);
    CHECK_NEAR (strongestHz (one ("trailer-drums", 36, R"({ "tune": 12 })"), 0.15, 0.5), 124.0, 5.0);
    // pitch bend with a 12 semitone range moves a ringing tom (80 Hz) up an octave
    CHECK_NEAR (strongestHz (one ("drums", 41, R"({ "bendRange": 12 })", R"("pitchBend": [ { "beat": 0, "value": 1 } ], )"), 0.2, 0.5), 160.5, 4.0);
    CHECK_NEAR (strongestHz (one ("drums", 41, {}, R"("pitchBend": [ { "beat": 0, "value": 1 } ], )"), 0.2, 0.5), 80.5 * std::pow (2.0, 2.0 / 12.0), 3.0);
    // a rising bend glides a ringing club tom (144 Hz) up: past 1.6 times its pitch in the last fifth of the rise
    const auto rising = one ("club-drums", 45, R"({ "bendRange": 12 })", R"("pitchBend": [ { "beat": 0, "value": 0, "curve": "linear" }, { "beat": 1, "value": 1 } ], )");
    CHECK (strongestHz (rising, 0.4, 0.5) > 1.6 * strongestHz (one ("club-drums", 45, {}), 0.4, 0.5));
}

TEST (club_kit_plays_the_same_in_realtime_with_bends_and_beat_effects)
{
    const auto c = parsed (R"({ "tempo": [ { "beat": 0, "bpm": 128, "ramp": true }, { "beat": 8, "bpm": 140 } ], "render": { "tailSeconds": 3.7 },
      "tracks": [ { "name": "K", "instrument": "synth.club-drums", "synth": { "tune": -1, "bendRange": 7 },
                    "pitchBend": [ { "beat": 0, "value": 0 }, { "beat": 6, "value": 0.6, "curve": "exponential" } ],
                    "notes": [ { "pitch": 36, "startBeat": 0, "durationBeat": 0.5, "velocity": 110 }, { "pitch": 39, "startBeat": 1, "durationBeat": 0.5, "velocity": 100 },
                               { "pitch": 40, "startBeat": 2, "durationBeat": 0.125, "velocity": 90 }, { "pitch": 40, "startBeat": 2.125, "durationBeat": 0.125, "velocity": 95 },
                               { "pitch": 64, "startBeat": 2, "durationBeat": 1, "velocity": 100 }, { "pitch": 67, "startBeat": 5, "durationBeat": 1, "velocity": 100 },
                               { "pitch": 60, "startBeat": 6, "durationBeat": 1, "velocity": 110 }, { "pitch": 51, "startBeat": 6, "durationBeat": 1, "velocity": 90 } ] } ] })");
    const auto offline = render (c, "club_offline");
    PlayerOptions po;
    po.useDefaultRegistry = false;
    po.threads = 0;
    RealtimePlayer player (c, po);
    player.play();
    juce::AudioBuffer<float> live (2, offline.getNumSamples());
    for (int done = 0; done < live.getNumSamples(); done += 300)
    {
        const int n = std::min (300, live.getNumSamples() - done);
        float* ptrs[2] = { live.getWritePointer (0, done), live.getWritePointer (1, done) };
        player.process (ptrs, 2, n);
    }
    double diff = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < live.getNumSamples(); ++i)
            diff = std::max (diff, (double) std::abs (offline.getSample (ch, i) - live.getSample (ch, i)));
    CHECK (diff < 1.0e-5);
    CHECK (maxAbs (offline, 0.0, 4.0) > 0.05);
}

TEST (synth_bend_range_and_filter_modes)
{
    auto sine = [] (const std::string& synth)
    {
        return render (parsed (R"({ "tempo": 120, "render": { "tailSeconds": 0.2 }, "tracks": [ { "name": "S", "instrument": "synth.sine", )"
                               + (synth.empty() ? std::string() : "\"synth\": " + synth + ", ")
                               + R"("pitchBend": [ { "beat": 0, "value": 1 } ], "notes": [ { "pitch": "A3", "startBeat": 0, "durationBeat": 2, "velocity": 100 } ] } ] })"),
                       "synth_bend");
    };
    CHECK_NEAR (strongestHz (sine ({}), 0.3, 0.8, 1000.0), 220.0 * std::pow (2.0, 2.0 / 12.0), 1.5);   // ±2 semitones by default
    CHECK_NEAR (strongestHz (sine (R"({ "bendRange": 12 })"), 0.3, 0.8, 1000.0), 440.0, 1.5);
    CHECK_NEAR (strongestHz (sine (R"({ "bendRange": 7 })"), 0.3, 0.8, 1000.0), 220.0 * std::pow (2.0, 7.0 / 12.0), 1.5);

    // the filter modes on white noise (cutoff 2 kHz): low-pass keeps the lows, high-pass removes them, band-pass keeps the middle
    auto noise = [] (const std::string& mode, int cc74 = 127)
    {
        return render (parsed (R"({ "tempo": 120, "render": { "tailSeconds": 0.2 }, "tracks": [ { "name": "N", "instrument": "synth.noise",
          "synth": { "filterMode": ")" + mode + R"(", "cutoffHz": 2000, "resonance": 0 }, "cc74": [ { "beat": 0, "value": )" + std::to_string (cc74) + R"( } ],
          "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 2, "velocity": 100 } ] } ] })"),
                       "synth_noise_" + mode);
    };
    auto lowShareDb = [] (const juce::AudioBuffer<float>& b)
    {
        juce::IIRFilter lp1, lp2;
        lp1.setCoefficients (juce::IIRCoefficients::makeLowPass (kRate, 300.0));
        lp2.setCoefficients (juce::IIRCoefficients::makeLowPass (kRate, 300.0));
        double lo = 0.0, all = 0.0;
        for (int i = 0; i < (int) (0.9 * kRate); ++i)
        {
            const float x = b.getSample (0, i);
            const float y = lp2.processSingleSampleRaw (lp1.processSingleSampleRaw (x));
            if (i >= (int) (0.2 * kRate))
            {
                lo += (double) y * y;
                all += (double) x * x;
            }
        }
        return 10.0 * std::log10 (lo / all);
    };
    const auto lp = noise ("lowpass"), hp = noise ("highpass"), bp = noise ("bandpass");
    CHECK (lowShareDb (hp) < lowShareDb (lp) - 25.0);
    CHECK (highShareDb (hp, 0.2, 0.9, 8000.0) > highShareDb (lp, 0.2, 0.9, 8000.0) + 20.0);
    CHECK (lowShareDb (bp) < lowShareDb (lp) - 8.0);
    CHECK (highShareDb (bp, 0.2, 0.9, 8000.0) < highShareDb (hp, 0.2, 0.9, 8000.0) - 8.0);
    // CC74 moves a high-pass cutoff down: more lows at CC74 30 than at 127
    CHECK (lowShareDb (noise ("highpass", 30)) > lowShareDb (hp) + 15.0);

    // the panel's response is the same formula
    const std::vector<double> freqs { 100.0, 1000.0, 10000.0 };
    const auto hpDb = synthFilterResponseDb (1000.0, 0.0, kRate, freqs, 1);
    CHECK (hpDb[0] < -35.0);
    CHECK_NEAR (hpDb[1], -6.02, 0.05);
    CHECK_NEAR (hpDb[2], 0.0, 0.2);
    const auto bpDb = synthFilterResponseDb (1000.0, 0.5, kRate, freqs, 2);
    CHECK_NEAR (bpDb[1], 0.0, 0.05);
    CHECK (bpDb[0] < -15.0 && bpDb[2] < -15.0);

    // names and numbers are read, wrong values are errors with a hint
    CHECK (mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "N", "instrument": "synth.noise", "synth": { "filterMode": 2 }, "notes": [] } ] })").composition.has_value());
    auto badName = mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "N", "instrument": "synth.noise", "synth": { "filterMode": "hipass" }, "notes": [] } ] })");
    CHECK (mrtest::hasError (badName.diagnostics, "$.tracks[0].synth.filterMode", "lowpass, highpass, bandpass"));
    auto badNumber = mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "N", "instrument": "synth.noise", "synth": { "filterMode": 1.5 }, "notes": [] } ] })");
    CHECK (mrtest::hasError (badNumber.diagnostics, "$.tracks[0].synth.filterMode"));
    auto badBend = mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "N", "instrument": "synth.lead", "synth": { "bendRange": 30 }, "notes": [] } ] })");
    CHECK (mrtest::hasError (badBend.diagnostics, "$.tracks[0].synth.bendRange"));
    auto badTune = mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "N", "instrument": "synth.club-drums", "synth": { "tune": -30 }, "notes": [] } ] })");
    CHECK (mrtest::hasError (badTune.diagnostics, "$.tracks[0].synth.tune"));
    RenderOptions o;
    o.useDefaultRegistry = false;
    Diagnostics d;
    const auto named = parsed (R"({ "tempo": 120, "instruments": [ { "whoosh": { "type": "synth", "preset": "noise", "parameters": { "filterMode": "bandpass" } } } ],
      "tracks": [ { "name": "N", "instrument": "whoosh", "notes": [] } ] })");
    const auto registry = buildRegistry (named, o, d);
    CHECK (d.ok() && registry.find ("whoosh") != nullptr && registry.find ("whoosh")->synthParameters.at ("filterMode") == 2.0);

    // the document writes the name and reads it back as a number
    std::string error;
    auto doc = CompositionDocument::fromText (R"({ "tempo": 120, "tracks": [ { "name": "N", "instrument": "synth.noise", "notes": [] } ] })",
                                              mrtest::sourceDir().getChildFile ("tests/inline.json"), error);
    REQUIRE (doc.has_value());
    const auto track = withSynthValue (doc->track (0), "filterMode", juce::String ("highpass"));
    CHECK_EQ (writeJson (track["synth"]), std::string (R"({"filterMode": "highpass"})"));
    auto reparsed = mrtest::parse (R"({ "tempo": 120, "tracks": [ { "name": "N", "instrument": "synth.noise", "synth": )" + writeJson (track["synth"]) + R"(, "notes": [] } ] })");
    REQUIRE (reparsed.composition.has_value());
    CHECK_EQ (reparsed.composition->tracks[0].synth.at ("filterMode"), 1.0);
}

TEST (club_kit_names_its_keys)
{
    Registry registry;
    const auto* club = registry.find ("synth.club-drums");
    REQUIRE (club != nullptr);
    CHECK (club->keyNames.at (36) == juce::String::fromUTF8 ("킥").toStdString());
    CHECK (club->keyNames.at (66) == juce::String::fromUTF8 ("상승음 16박").toStdString());
    CHECK (club->keyNames.count (37) == 0);
    const auto params = synthParameters ("club-drums");
    CHECK (std::any_of (params.begin(), params.end(), [] (const ParameterInfo& p) { return p.key == "tune" && p.minimum == -24.0 && p.maximum == 24.0; }));
    CHECK (std::any_of (params.begin(), params.end(), [] (const ParameterInfo& p) { return p.key == "bendRange" && p.defaultValue == 2.0; }));
}
