#include "rendering/TrackChain.h"

#include "core/Random.h"
#include "instruments/AudioClips.h"
#include "performance/Performance.h"

#include <algorithm>

namespace mr
{

TrackChain buildTrackChain (const Composition& c, const TrackSpec& track, const Registry& registry, const TempoMap& tempo,
                            const std::map<std::string, size_t>& busIndex, const TrackChainOptions& o)
{
    TrackChain tc;
    tc.spec = &track;

    InstrumentContext ictx;
    ictx.sampleRate = o.sampleRate;
    ictx.blockSize = o.blockSize;
    ictx.seed = combineSeed (o.seed, hashString (track.name));
    ictx.tempo = &tempo;
    ictx.beatsPerBar = c.beatsPerBar;
    ictx.beatUnit = c.beatUnit;
    ictx.meters = std::make_shared<const MeterMap> (c.meters());
    ictx.pluginCache = o.pluginCache;
    ictx.realtime = o.realtime;
    for (const auto& [id, lane] : track.automation)
        if (! lane.empty())
            ictx.automated.push_back (id);
    ictx.trackPlugin = &track.plugin;
    ictx.trackSynth = &track.synth;
    ictx.trackPluginPath = track.path + ".plugin";
    std::vector<std::string> clipWarnings;
    ictx.trackClips = &track.clips;
    ictx.songFolder = c.sourceFile.getParentDirectory();
    ictx.warnings = &clipWarnings;
    tc.created = createInstrument (registry, track.instrument, ictx);
    for (const auto& w : clipWarnings)
        tc.warnings.push_back ({ track.path + ".clips", w, {} });
    if (! track.clips.empty() && tc.created.def->type != "audio")
        tc.warnings.push_back ({ track.path + ".clips", "clips are ignored: the track plays '" + tc.created.def->name
                                 + "', not \"audio\"", "use \"instrument\": \"audio\" for a track of audio clips" });
    if (tc.created.def->type == "audio" && ! track.notes.empty())
        tc.warnings.push_back ({ track.path + ".notes", "notes are ignored on an audio track", {} });

    for (const auto& note : tc.created.notes)
        tc.warnings.push_back ({ track.path + ".instrument", note, {} });
    if (! track.synth.empty() && tc.created.def->type != "synth")
        tc.warnings.push_back ({ track.path + ".synth", "synth values are ignored: the track plays '" + tc.created.def->name
                                 + "' (" + tc.created.def->type + "), not a built-in synth", {} });
    if (! track.plugin.empty() && tc.created.instrument->plugin() == nullptr)
        tc.warnings.push_back ({ track.path + ".plugin", "plugin settings are ignored: the track plays '" + tc.created.def->name
                                 + "' (" + tc.created.def->type + "), not a plugin", {} });

    tc.instrumentGain = (float) juce::Decibels::decibelsToGain (tc.created.def->gainDb);
    tc.fader = faderGains (track.mixer);
    tc.faderLanes = makeFaderLanes (track.mixer, tempo, o.sampleRate);
    for (const auto& send : track.sends)
        tc.sends.push_back (makeTrackSend (send, busIndex.at (send.bus), tempo, o.sampleRate));

    tc.content = buildTrackContent (c, track, *tc.created.def, tempo, o, tc.warnings);
    tc.effects = createEffectChain (track.effects, c.sourceFile.getParentDirectory(), o.sampleRate, o.blockSize, &tempo, o.realtime);
    tc.instrumentLatency = std::max (0, tc.created.instrument->latencySamples());
    tc.latency = tc.instrumentLatency + tc.effects.latencySamples();
    return tc;
}

TrackSend makeTrackSend (const SendSpec& send, size_t bus, const TempoMap& tempo, double sampleRate)
{
    TrackSend s { bus, (float) juce::Decibels::decibelsToGain (send.levelDb, -120.0), send.preFader, {} };
    if (! send.levelLane.empty())
        s.level = SampleLane (send.levelLane, std::make_shared<const TempoMap> (tempo), sampleRate, false);
    return s;
}

float sendGainAt (const TrackSend& send, int64_t position)
{
    if (send.level.empty())
        return send.gain;
    const int64_t grid = automationGridStart (position);
    const double g0 = juce::Decibels::decibelsToGain (send.level.atGrid (grid), -120.0);
    const double g1 = juce::Decibels::decibelsToGain (send.level.atGrid (grid + kAutomationGrid), -120.0);
    return (float) (g0 + (g1 - g0) * ((double) (position - grid) * (1.0 / kAutomationGrid)));
}

void addSend (juce::AudioBuffer<float>& destination, int destStart, const juce::AudioBuffer<float>& source, int sourceStart, int n,
              const TrackSend& send, BlockPosition position)
{
    if (send.level.empty())
    {
        for (int ch = 0; ch < 2; ++ch)
            destination.addFrom (ch, destStart, source, ch, sourceStart, n, send.gain);
        return;
    }
    // 격자 점의 선형 게인 사이를 직선으로 잇는다 (-120 dB 는 0)
    forEachGridStep (position, n, [&] (int offset, int count, int64_t grid)
    {
        const double g0 = juce::Decibels::decibelsToGain (send.level.atGrid (grid), -120.0);
        const double g1 = juce::Decibels::decibelsToGain (send.level.atGrid (grid + kAutomationGrid), -120.0);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* x = source.getReadPointer (ch, sourceStart + offset);
            float* y = destination.getWritePointer (ch, destStart + offset);
            for (int j = 0; j < count; ++j)
                y[j] += (float) (x[j] * (g0 + (g1 - g0) * gridFraction (position, offset, j, grid)));
        }
    });
}

std::unique_ptr<TrackContent> buildTrackContent (const Composition& c, const TrackSpec& track, const InstrumentDef& def,
                                                 const TempoMap& tempo, const TrackChainOptions& o, std::vector<Diagnostic>& warnings)
{
    Diagnostics profileDiag;
    checkTrackAgainstInstrument (track, def, profileDiag);
    if (! profileDiag.ok())
        throw Failure ("invalid-input", "track '" + track.name + "' does not fit instrument '" + def.name + "'", profileDiag.errors);
    warnings.insert (warnings.end(), profileDiag.warnings.begin(), profileDiag.warnings.end());

    auto content = std::make_unique<TrackContent>();
    content->spec = track;
    const auto& spec = content->spec;
    const auto perf = resolvePerformance (c.performance, spec.performance, o.seedOverride);
    const auto meters = c.meters();
    PerformanceContext pctx { tempo, c.barBeats(), spec.name, spec.transpose + def.transpose, &def.profile, spec.articulation, &def.keymap, &meters };
    const auto performed = perform (spec.notes, perf, pctx);
    for (const auto& n : performed)
        content->lastNoteEndSec = std::max (content->lastNoteEndSec, n.endSec);
    content->notes = performed.size();
    if (def.type == "audio")
        for (const auto& clip : spec.clips)
            content->lastNoteEndSec = std::max (content->lastNoteEndSec, clipEndSeconds (clip, tempo, c.sourceFile.getParentDirectory()));

    EventBuildContext ectx { tempo, o.sampleRate, def, spec };
    content->events = buildTrackEvents (performed, ectx);
    content->expression.configure (spec, def.profile, def.type, tempo, o.sampleRate);
    return content;
}

std::vector<size_t> trackProcessOrder (const Composition& c)
{
    const size_t n = c.tracks.size();
    std::map<std::string, size_t> index;
    for (size_t i = 0; i < n; ++i)
        index[c.tracks[i].name] = i;
    std::vector<std::vector<size_t>> keys (n);
    for (size_t i = 0; i < n; ++i)
        for (const auto& e : c.tracks[i].effects)
            if (e.type == "compressor")
                if (const auto v = e.params.getProperty ("sidechain", juce::var()); v.isString())
                    if (auto it = index.find (v.toString().toStdString()); it != index.end() && it->second != i)
                        keys[i].push_back (it->second);
    std::vector<size_t> order;
    std::vector<bool> placed (n, false);
    // 키가 모두 놓인 트랙 가운데 번호가 가장 작은 것을 고른다 (고리는 파서가 막는다)
    while (order.size() < n)
    {
        bool progressed = false;
        for (size_t i = 0; i < n; ++i)
        {
            if (placed[i])
                continue;
            if (std::all_of (keys[i].begin(), keys[i].end(), [&] (size_t k) { return placed[k]; }))
            {
                placed[i] = true;
                order.push_back (i);
                progressed = true;
                break;
            }
        }
        if (! progressed)
            for (size_t i = 0; i < n; ++i)
                if (! placed[i])
                {
                    placed[i] = true;
                    order.push_back (i);
                }
    }
    return order;
}

void alignTrackSidechains (const Composition& c, const std::vector<TrackChain*>& chains)
{
    std::map<std::string, size_t> index;
    for (size_t i = 0; i < c.tracks.size(); ++i)
        index[c.tracks[i].name] = i;
    for (const size_t ti : trackProcessOrder (c))
    {
        auto* chain = ti < chains.size() ? chains[ti] : nullptr;
        if (chain == nullptr || chain->effects.sidechainSources().empty())
            continue;
        chain->effects.alignSidechains (chain->instrumentLatency, [&] (const std::string& source)
        {
            const auto it = index.find (source);
            const auto* key = it != index.end() && it->second < chains.size() ? chains[it->second] : nullptr;
            return key != nullptr ? key->latency : 0;
        });
        chain->latency = chain->instrumentLatency + chain->effects.latencySamples();
    }
}

double songEndSeconds (const Composition& c, const TempoMap& tempo, double lastNoteEndSec, std::vector<Diagnostic>* warnings)
{
    double lastEnd = lastNoteEndSec;
    for (const auto& t : c.tracks)
        for (const auto& [id, lane] : t.automation)
            if (! lane.empty())
                lastEnd = std::max (lastEnd, tempo.secondsAt (lane.points.back().beat));
    if (! c.render.endBeat)
        return lastEnd;
    const double endSec = tempo.secondsAt (*c.render.endBeat);
    if (warnings != nullptr && lastEnd > endSec + 1.0e-6)
        warnings->push_back ({ "$.render.endBeat", "notes continue " + formatNumber (lastEnd - endSec, 2)
                               + " s past endBeat and are cut at the end of the tail", {} });
    return endSec;
}

} // namespace mr
