#include "rendering/EventBuilder.h"

#include "instruments/SfzInstrument.h"
#include "instruments/SimpleSynth.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace mr
{

std::string resolveArticulation (const std::string& noteArticulation, const TrackSpec& track, const InstrumentProfile& profile)
{
    if (! noteArticulation.empty())
        return noteArticulation;
    if (! track.articulation.empty())
        return track.articulation;
    return profile.defaultArticulation;
}

void checkTrackAgainstInstrument (const TrackSpec& t, const InstrumentDef& def, Diagnostics& d)
{
    const auto& p = def.profile;
    std::vector<std::string> names;
    for (const auto& [n, a] : p.articulations)
        names.push_back (n);

    auto check = [&] (const std::string& art, const std::string& path)
    {
        if (art.empty())
            return;
        if (p.articulations.empty())
        {
            d.error (path, "instrument '" + def.name + "' has no articulations", "define them in the instrument registry");
            return;
        }
        if (p.articulations.count (art) == 0)
        {
            const auto s = closestMatch (art, names);
            d.error (path, "instrument '" + def.name + "' has no articulation '" + art + "'",
                     s.empty() ? "available: " + juce::StringArray ([&] { juce::StringArray a; for (auto& n : names) a.add (n); return a; }()).joinIntoString (", ").toStdString()
                               : "did you mean '" + s + "'?");
        }
        else if (def.type == "sfz" && def.file == juce::File() && p.articulations.at (art).file == juce::File())
            d.error (path, "articulation '" + art + "' has no SFZ file and the instrument has no default file");
    };
    check (t.articulation, t.path + ".articulation");
    for (size_t i = 0; i < t.notes.size(); ++i)
        check (t.notes[i].articulation, t.path + ".notes[" + std::to_string (i) + "].articulation");
    if (def.type == "sfz" && def.file == juce::File() && t.articulation.empty() && p.defaultArticulation.empty())
        d.error (t.path, "instrument '" + def.name + "' needs an articulation (it has no default file)");

    // 내장 신스의 값은 그 프리셋의 엔진이 쓰는 것만 소리를 바꾼다 (북의 tune 은 패드에, bendRange 는 pluck 에 쓰이지 않는다)
    if (def.type == "synth" && ! t.synth.empty())
    {
        const auto params = synthParameters (def.preset);
        std::string list;
        for (const auto& p : params)
            list += (list.empty() ? "" : ", ") + p.key;
        if (! params.empty())
            for (const auto& [key, value] : t.synth)
                if (std::none_of (params.begin(), params.end(), [&key] (const ParameterInfo& p) { return p.key == key; }))
                    d.warning (t.path + ".synth." + key, "'" + key + "' has no effect on " + def.name, "it takes: " + list);
    }

    for (const auto& [id, lane] : t.automation)
    {
        if (id >= 128)
            continue;
        const auto mode = p.modeFor (id, def.type);
        if (mode == ControllerMode::ignore)
            d.warning (t.path + "." + controllerName (id), "instrument '" + def.name + "' ignores " + controllerName (id));
        if ((mode == ControllerMode::engine || mode == ControllerMode::both) && ! engineCanHandle (id, p))
            d.error (t.path + "." + controllerName (id), "the engine cannot process " + controllerName (id));
    }
}

std::vector<ScheduledEvent> buildTrackEvents (const std::vector<PerformedNote>& notes, const EventBuildContext& ctx)
{
    std::vector<ScheduledEvent> events;
    size_t order = 0;
    const auto& def = ctx.instrument;
    const auto& profile = def.profile;
    const double sr = ctx.sampleRate;
    auto toSample = [sr] (double seconds) { return (int64_t) std::llround (seconds * sr); };
    auto push = [&] (int64_t sample, int priority, const juce::MidiMessage& m) { events.push_back ({ sample, priority, order++, m }); };

    // ---- 음과 아티큘레이션 ----
    std::string currentKeyswitch, currentProgram, currentCcs;
    for (const auto& n : notes)
    {
        const int64_t on = toSample (n.startSec);
        const int64_t off = std::max (on + 1, toSample (n.endSec));
        int channel = n.channel;
        const ArticulationDef* art = nullptr;
        if (! n.articulation.empty())
            if (auto it = profile.articulations.find (n.articulation); it != profile.articulations.end())
                art = &it->second;
        if (art != nullptr)
        {
            if (art->keyswitch && currentKeyswitch != art->name)
            {
                push (on, 2, juce::MidiMessage::noteOn (channel, *art->keyswitch, (juce::uint8) art->keyswitchVelocity));
                push (on + std::max<int64_t> (1, toSample (0.001)), 1, juce::MidiMessage::noteOff (channel, *art->keyswitch));
                currentKeyswitch = art->name;
            }
            if (art->program && currentProgram != art->name)
            {
                push (on, 0, juce::MidiMessage::programChange (channel, *art->program));
                currentProgram = art->name;
            }
            if (! art->ccs.empty() && currentCcs != art->name)
            {
                for (const auto& [cc, value] : art->ccs)
                    push (on, 0, juce::MidiMessage::controllerEvent (channel, cc, value));
                currentCcs = art->name;
            }
            if (def.type == "sfz")
                if (int ch = sfzChannelFor (def, art->name); ch > 0)
                    channel = ch;
        }
        push (on, 3, juce::MidiMessage::noteOn (channel, n.pitch, (juce::uint8) n.velocity));
        push (off, 1, juce::MidiMessage::noteOff (channel, n.pitch));
    }

    // ---- 악기로 보내는 컨트롤러 곡선 ----
    for (const auto& [id, lane] : ctx.track.automation)
    {
        if (lane.empty())
            continue;
        if (id < 128)
        {
            const auto mode = profile.modeFor (id, def.type);
            if (mode != ControllerMode::native && mode != ControllerMode::both)
                continue;
        }
        // 정해진 간격의 점과 곡선의 꺾이는 점에서 읽는다
        std::set<int64_t> samples;
        const double first = ctx.tempo.secondsAt (lane.points.front().beat);
        const double last = ctx.tempo.secondsAt (lane.points.back().beat);
        samples.insert (0);
        for (const auto& p : lane.points)
            samples.insert (toSample (ctx.tempo.secondsAt (p.beat)));
        const double step = ctx.controlIntervalMs / 1000.0;
        for (double t = first; t <= last + step * 0.5; t += step)
            samples.insert (toSample (t));

        int lastValue = -1;
        for (int64_t s : samples)
        {
            const double value = lane.valueAt (ctx.tempo.beatAt ((double) s / sr));
            int quantised = 0;
            juce::MidiMessage m;
            if (id == kPitchBend)
            {
                quantised = std::clamp ((int) std::lround ((value + 1.0) * 8191.5), 0, 16383);
                m = juce::MidiMessage::pitchWheel (1, quantised);
            }
            else if (id == kAftertouch)
            {
                quantised = std::clamp ((int) std::lround (value), 0, 127);
                m = juce::MidiMessage::channelPressureChange (1, quantised);
            }
            else
            {
                quantised = std::clamp ((int) std::lround (value), 0, 127);
                m = juce::MidiMessage::controllerEvent (1, id, quantised);
            }
            if (quantised != lastValue)
            {
                push (s, 0, m);
                lastValue = quantised;
            }
        }
    }

    std::sort (events.begin(), events.end(), [] (const ScheduledEvent& a, const ScheduledEvent& b)
    {
        if (a.sample != b.sample) return a.sample < b.sample;
        if (a.priority != b.priority) return a.priority < b.priority;
        return a.order < b.order;
    });
    return events;
}

} // namespace mr
