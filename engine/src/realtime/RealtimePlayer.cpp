#include "realtime/RealtimePlayer.h"

#include "plugins/PluginEffect.h"

#include "realtime/WorkerPool.h"

#include "core/Random.h"
#include "instruments/Registry.h"
#include "instruments/SfzInstrument.h"
#include "plugins/PluginHost.h"
#include "rendering/OfflineRenderer.h"

#include <algorithm>
#include <cmath>

namespace mr
{

// 페이더와 밸런스. 아무 스레드나 값을 바꾸고, 오디오 스레드는 바뀐 값을 블록 하나에 걸쳐 잇는다.
struct RealtimePlayer::Fader
{
    std::atomic<double> gainDb { 0.0 };
    std::atomic<double> pan { 0.0 };
    double appliedDb = 0.0, appliedPan = 0.0;   // 오디오 스레드가 마지막으로 계산한 값
    ChannelGains current, target;
    std::unique_ptr<FaderLanes> lanes;           // 오디오 스레드의 곡선 (비면 고정값)
    Handoff<FaderLanes> nextLanes;

    void set (const MixerSettings& m)
    {
        gainDb.store (m.totalGainDb());   // 채널과 VCA 의 합
        pan.store (m.pan);
    }

    void reset (const MixerSettings& m)
    {
        set (m);
        appliedDb = m.totalGainDb();
        appliedPan = m.pan;
        current = target = faderGains (m);
    }
};

struct RealtimePlayer::SendRouting
{
    std::vector<TrackSend> sends;
    std::vector<float> applied;   // 지금 쓰는 게인 (같은 배치로 바꿔 끼우면 앞의 값에서 잇는다)
};

struct RealtimePlayer::MasterEffects
{
    EffectChain chain;
    std::unique_ptr<Effect> limiter;
};

int RealtimePlayer::masterLatency (const MasterEffects& m)
{
    return m.chain.latencySamples() + (m.limiter != nullptr ? m.limiter->latencySamples() : 0);
}

struct RealtimePlayer::Track
{
    TrackChain chain;
    Handoff<TrackContent> content;          // updateTrack, apply 가 넘긴 새 내용
    Handoff<EffectChain> effects;           // 새 효과 사슬 (바꿔 끼운 뒤 옛 사슬을 담아 내려놓는다)
    Handoff<SendRouting> routing;
    std::unique_ptr<SendRouting> sends;
    Fader fader;
    DelayLine align;
    juce::AudioBuffer<float> pre, post;
    juce::AudioBuffer<float> key;           // 정렬 전의 프리 페이더 신호 (다른 트랙의 사이드체인 키)
    bool isKey = false;                     // 다른 트랙의 압축기가 이 트랙을 키로 쓴다
    juce::MidiBuffer pending;               // 다음 블록 처음에 보낼 이벤트 (끄기, 위치를 옮긴 뒤의 컨트롤러)
    juce::MidiBuffer midi;                  // 이 블록에 악기로 보내는 이벤트
    size_t cursor = 0;
    std::array<std::bitset<128>, 16> sounding;
    std::bitset<16> channelsUsed;
    std::array<std::array<int16_t, 128>, 16> ccState {};
    std::array<int16_t, 16> programState {};
    std::array<int32_t, 16> bendState {};
    std::array<int16_t, 16> pressureState {};
    std::atomic<bool> mute { false };
    std::atomic<bool> solo { false };
    std::atomic<float> peak[2];
    float gain = 1.0f;                      // 뮤트, 솔로를 바꿀 때 블록 하나에 걸쳐 잇는 게인
    int outputBus = -1;                     // 페이더 뒤의 소리를 보낼 버스 (-1 은 마스터)
};

struct RealtimePlayer::Bus
{
    const BusSpec* spec = nullptr;
    Fader fader;
    std::atomic<bool> mute { false };
    float muteGain = 1.0f;
    EffectChain effects;
    Handoff<EffectChain> nextEffects;
    juce::AudioBuffer<float> input;
    std::atomic<float> peak[2];
    int latency = 0;                        // 효과 사슬의 지연 (바꿔 끼워도 같다)
    DelayLine align;                        // 가장 긴 버스 지연에 맞춘다
};

namespace
{

void applyGains (juce::AudioBuffer<float>& b, int n, ChannelGains g)
{
    b.applyGain (0, 0, n, g.left);
    b.applyGain (1, 0, n, g.right);
}

void storeMax (std::atomic<float>& target, float value)
{
    float current = target.load (std::memory_order_relaxed);
    while (value > current && ! target.compare_exchange_weak (current, value, std::memory_order_relaxed))
    {
    }
}

void storePeaks (std::atomic<float> (&peak)[2], const juce::AudioBuffer<float>& b, int n)
{
    storeMax (peak[0], b.getMagnitude (0, 0, n));
    storeMax (peak[1], b.getMagnitude (1, 0, n));
}

bool sameNotes (const std::vector<Note>& a, const std::vector<Note>& b)
{
    return std::equal (a.begin(), a.end(), b.begin(), b.end(), [] (const Note& x, const Note& y)
    {
        return x.pitch == y.pitch && x.startBeat == y.startBeat && x.durationBeat == y.durationBeat && x.velocity == y.velocity
               && x.channel == y.channel && x.articulation == y.articulation && x.humanize == y.humanize;
    });
}

bool sameLanes (const std::map<int, AutomationLane>& a, const std::map<int, AutomationLane>& b)
{
    return std::equal (a.begin(), a.end(), b.begin(), b.end(), [] (const auto& x, const auto& y)
    {
        return x.first == y.first
               && std::equal (x.second.points.begin(), x.second.points.end(), y.second.points.begin(), y.second.points.end(),
                              [] (const AutomationPoint& p, const AutomationPoint& q) { return p.beat == q.beat && p.value == q.value && p.curve == q.curve; });
    });
}

bool samePerformance (const PerformanceSettings& a, const PerformanceSettings& b)
{
    return a.timingHumanizeMs == b.timingHumanizeMs && a.velocityHumanize == b.velocityHumanize && a.noteLengthHumanize == b.noteLengthHumanize
           && a.legatoOverlapMs == b.legatoOverlapMs && a.swing == b.swing && a.swingGrid == b.swingGrid && a.accentPattern == b.accentPattern
           && a.accentGrid == b.accentGrid && a.humanizeSeed == b.humanizeSeed;
}

// 연주 처리한 이벤트와 표현 단계를 바꾸는 부분
bool sameContent (const TrackSpec& a, const TrackSpec& b)
{
    return a.articulation == b.articulation && a.transpose == b.transpose && sameNotes (a.notes, b.notes) && sameLanes (a.automation, b.automation)
           && samePerformance (a.performance, b.performance) && a.clips == b.clips;
}

// 플러그인 효과의 설정(state, program, parameters)만 다르고 나머지는 같은가
bool samePluginEffectsExceptSettings (const std::vector<EffectSpec>& a, const std::vector<EffectSpec>& b)
{
    auto withoutSettings = [] (const juce::var& params)
    {
        auto copy = params.clone();
        if (auto* o = copy.getDynamicObject())
            for (const char* key : { "state", "program", "parameters" })
                o->removeProperty (key);
        return juce::JSON::toString (copy, true);
    };
    return std::equal (a.begin(), a.end(), b.begin(), b.end(), [&] (const EffectSpec& x, const EffectSpec& y)
    {
        if (x.type != y.type || x.bypass != y.bypass || x.path != y.path)
            return false;
        if (x.type == "plugin")
            return withoutSettings (x.params) == withoutSettings (y.params);
        return juce::JSON::toString (x.params, true) == juce::JSON::toString (y.params, true);
    });
}

bool sameEffects (const std::vector<EffectSpec>& a, const std::vector<EffectSpec>& b)
{
    return std::equal (a.begin(), a.end(), b.begin(), b.end(), [] (const EffectSpec& x, const EffectSpec& y)
    {
        return x.type == y.type && x.bypass == y.bypass && x.path == y.path
               && juce::JSON::toString (x.params, true) == juce::JSON::toString (y.params, true);
    });
}

bool samePluginSettings (const PluginSettings& a, const PluginSettings& b)
{
    return a.stateBase64 == b.stateBase64 && a.program == b.program && a.stateFile == b.stateFile
           && std::equal (a.parameters.begin(), a.parameters.end(), b.parameters.begin(), b.parameters.end(), [] (const auto& x, const auto& y)
              {
                  return x.first == y.first && juce::JSON::toString (x.second, true) == juce::JSON::toString (y.second, true);
              });
}

bool sameLane (const AutomationLane& a, const AutomationLane& b)
{
    return std::equal (a.points.begin(), a.points.end(), b.points.begin(), b.points.end(),
                       [] (const AutomationPoint& p, const AutomationPoint& q) { return p.beat == q.beat && p.value == q.value && p.curve == q.curve; });
}

bool sameSends (const std::vector<SendSpec>& a, const std::vector<SendSpec>& b)
{
    return std::equal (a.begin(), a.end(), b.begin(), b.end(), [] (const SendSpec& x, const SendSpec& y)
    {
        return x.bus == y.bus && x.levelDb == y.levelDb && x.preFader == y.preFader && sameLane (x.levelLane, y.levelLane);
    });
}

int noKeyLatency (const std::string&) { return 0; }

// 재생기를 다시 만들지 않고 반영할 수 있는 곡인가 (트랙 효과의 지연과 사이드체인은 apply 가 따로 본다)
bool sameFaderLanes (const MixerSettings& a, const MixerSettings& b)
{
    // 곡선이 하나라도 있으면 곡선 쪽이 고정값(곡선이 없는 칸)도 들고 있다
    const bool anyLane = ! a.gainLane.empty() || ! a.panLane.empty() || ! a.vcaLane.empty();
    return sameLane (a.gainLane, b.gainLane) && sameLane (a.panLane, b.panLane) && sameLane (a.vcaLane, b.vcaLane)
           && (! anyLane || (a.gainDb == b.gainDb && a.vcaDb == b.vcaDb && a.pan == b.pan));
}

bool sameStructure (const Composition& a, const Composition& b)
{
    const bool sameTempo = std::equal (a.tempo.begin(), a.tempo.end(), b.tempo.begin(), b.tempo.end(), [] (const TempoPoint& x, const TempoPoint& y)
    {
        return x.beat == y.beat && x.bpm == y.bpm && x.ramp == y.ramp;
    });
    const bool sameMeters = std::equal (a.meterChanges.begin(), a.meterChanges.end(), b.meterChanges.begin(), b.meterChanges.end(),
                                        [] (const MeterChange& x, const MeterChange& y)
                                        { return x.beat == y.beat && x.beatsPerBar == y.beatsPerBar && x.beatUnit == y.beatUnit; });
    if (! sameTempo || ! sameMeters || a.beatsPerBar != b.beatsPerBar || a.beatUnit != b.beatUnit || ! samePerformance (a.performance, b.performance)
        || a.registries != b.registries || a.sourceFile != b.sourceFile || a.master.targetLufs.has_value() != b.master.targetLufs.has_value()
        || a.tracks.size() != b.tracks.size() || a.buses.size() != b.buses.size() || a.inlineInstruments.size() != b.inlineInstruments.size())
        return false;
    for (size_t i = 0; i < a.inlineInstruments.size(); ++i)
        if (juce::JSON::toString (a.inlineInstruments[i].table, true) != juce::JSON::toString (b.inlineInstruments[i].table, true))
            return false;
    for (size_t i = 0; i < a.tracks.size(); ++i)
        if (a.tracks[i].name != b.tracks[i].name || a.tracks[i].instrument != b.tracks[i].instrument || a.tracks[i].output != b.tracks[i].output)
            return false;
    for (size_t i = 0; i < a.buses.size(); ++i)
        if (a.buses[i].name != b.buses[i].name)
            return false;
    return true;
}

} // namespace

RealtimePlayer::RealtimePlayer (const Composition& c, const PlayerOptions& o, const Progress& progress)
    : composition_ (c), tempo_ (composition_.tempo), meters_ (composition_.meters()), sampleRate_ (o.sampleRate), blockSize_ (o.blockSize)
{
    if (sampleRate_ < 8000.0 || sampleRate_ > 384000.0)
        throw Failure ("invalid-input", "sample rate must be between 8000 and 384000 Hz");
    if (blockSize_ < 16 || blockSize_ > 16384)
        throw Failure ("invalid-input", "block size must be between 16 and 16384");
    for (auto& p : masterPeak_)
        p.store (0.0f);
    loudness_.prepare (sampleRate_);
    tap_ = std::make_unique<float[]> ((size_t) tapSize);
    std::fill (tap_.get(), tap_.get() + tapSize, 0.0f);
    masterFader_ = std::make_unique<Fader>();
    nextMasterEffects_ = std::make_unique<Handoff<MasterEffects>>();

    RenderOptions ro;
    ro.registries = o.registries;
    ro.useDefaultRegistry = o.useDefaultRegistry;
    Diagnostics regDiag;
    registry_ = std::make_unique<Registry> (buildRegistry (composition_, ro, regDiag));
    if (! regDiag.ok())
        throw Failure ("invalid-input", "instrument registry has errors", regDiag.errors);
    warnings_ = regDiag.warnings;

    for (size_t i = 0; i < composition_.buses.size(); ++i)
    {
        auto bus = std::make_unique<Bus>();
        bus->spec = &composition_.buses[i];
        bus->fader.reset (bus->spec->mixer);
        bus->fader.lanes = std::make_unique<FaderLanes> (makeFaderLanes (bus->spec->mixer, tempo_, sampleRate_));
        bus->mute.store (bus->spec->mixer.muted());
        bus->muteGain = bus->spec->mixer.muted() ? 0.0f : 1.0f;
        bus->effects = createEffectChain (bus->spec->effects, composition_.sourceFile.getParentDirectory(), sampleRate_, blockSize_, &tempo_, true);
        bus->effects.alignSidechains (0, noKeyLatency);
        bus->latency = bus->effects.latencySamples();
        bus->input.setSize (2, blockSize_);
        for (auto& p : bus->peak)
            p.store (0.0f);
        busIndex_[bus->spec->name] = i;
        buses_.push_back (std::move (bus));
    }

    auto& topts = chainOptions_;
    topts.sampleRate = sampleRate_;
    topts.blockSize = blockSize_;
    topts.seed = o.seed ? *o.seed : composition_.performance.humanizeSeed.value_or (0);
    topts.seedOverride = o.seed;
    topts.pluginCache = o.pluginCache;
    topts.realtime = true;

    // 뮤트된 트랙도 만든다 (재생 중에 뮤트를 풀 수 있다)
    double lastNoteEnd = 0.0;
    for (size_t i = 0; i < composition_.tracks.size(); ++i)
    {
        const auto& spec = composition_.tracks[i];
        if (progress)
            progress (i, composition_.tracks.size(), spec.name);
        auto t = std::make_unique<Track>();
        t->chain = buildTrackChain (composition_, spec, *registry_, tempo_, busIndex_, topts);
        if (o.prepareClipsInBackground)
            t->chain.created.instrument->prepareClipsInBackground (true);
        if (const auto out = busIndex_.find (spec.output); out != busIndex_.end())
            t->outputBus = (int) out->second;
        warnings_.insert (warnings_.end(), t->chain.warnings.begin(), t->chain.warnings.end());
        lastNoteEnd = std::max (lastNoteEnd, t->chain.content->lastNoteEndSec);
        lastNoteEnd_.push_back (t->chain.content->lastNoteEndSec);
        t->pre.setSize (2, blockSize_);
        t->post.setSize (2, blockSize_);
        t->key.setSize (2, blockSize_);
        t->pending.ensureSize (8192);
        t->midi.ensureSize (65536);
        t->mute.store (spec.mixer.muted());
        t->solo.store (spec.mixer.solo);
        t->fader.reset (spec.mixer);
        t->fader.lanes = std::make_unique<FaderLanes> (makeFaderLanes (spec.mixer, tempo_, sampleRate_));
        t->sends = std::make_unique<SendRouting>();
        t->sends->sends = t->chain.sends;
        for (const auto& send : t->sends->sends)
            t->sends->applied.push_back (send.gain);
        for (auto& p : t->peak)
            p.store (0.0f);
        infos_.push_back ({ spec.name, spec.instrument, t->chain.created.def->name, t->chain.created.instrument->description(),
                            t->chain.content->notes, t->chain.latency });
        tracks_.push_back (std::move (t));
    }
    if (progress)
        progress (composition_.tracks.size(), composition_.tracks.size(), {});
    std::vector<TrackChain*> chains;
    for (auto& t : tracks_)
        chains.push_back (&t->chain);
    alignTrackSidechains (composition_, chains);
    for (size_t i = 0; i < tracks_.size(); ++i)
        infos_[i].latencySamples = tracks_[i]->chain.latency;

    const double endSec = songEndSeconds (composition_, tempo_, lastNoteEnd, &warnings_);
    length_.store ((int64_t) std::ceil (std::max (0.1, endSec + composition_.render.tailSeconds) * sampleRate_));

    for (const auto& t : tracks_)
        maxLatency_ = std::max (maxLatency_, t->chain.latency);
    for (auto& t : tracks_)
        t->align.prepare (2, maxLatency_ - t->chain.latency);

    order_ = trackProcessOrder (composition_);
    for (size_t i = 0; i < composition_.tracks.size(); ++i)
        trackIndex_[composition_.tracks[i].name] = i;
    for (const auto& t : tracks_)
        for (const auto& source : t->chain.effects.sidechainSources())
            if (const auto it = trackIndex_.find (source); it != trackIndex_.end())
                tracks_[it->second]->isKey = true;
    silence_.setSize (2, blockSize_);
    silence_.clear();

    masterFader_->reset (composition_.master.mixer);
    masterFader_->lanes = std::make_unique<FaderLanes> (makeFaderLanes (composition_.master.mixer, tempo_, sampleRate_));
    masterEffects_ = std::make_unique<MasterEffects>();
    masterEffects_->chain = createEffectChain (composition_.master.effects, composition_.sourceFile.getParentDirectory(), sampleRate_, blockSize_, &tempo_, true);
    masterEffects_->chain.alignSidechains (0, noKeyLatency);
    if (composition_.master.targetLufs)
        masterEffects_->limiter = masterEffects_->chain.takeTrailingLimiter();
    for (const auto& b : buses_)
        maxBusLatency_ = std::max (maxBusLatency_, b->latency);
    for (auto& b : buses_)
        b->align.prepare (2, maxBusLatency_ - b->latency);
    directAlign_.prepare (2, maxBusLatency_);
    direct_.setSize (2, blockSize_);
    direct_.clear();
    prepareMasterKeys (masterEffects_->chain);
    outputLatency_.store (maxLatency_ + maxBusLatency_ + masterLatency (*masterEffects_));
    for (size_t i = 0; i < tracks_.size(); ++i)
        notePlugins ("track:" + std::to_string (i), tracks_[i]->chain.effects);
    for (size_t i = 0; i < buses_.size(); ++i)
        notePlugins ("bus:" + std::to_string (i), buses_[i]->effects);
    notePlugins ("master", masterEffects_->chain);
    master_.setSize (2, blockSize_);

    // 함께 처리할 묶음: 키 트랙 쪽이 먼저 오도록 사이드체인의 깊이로 나눈다
    std::vector<int> depth (tracks_.size(), 0);
    for (const size_t ti : order_)
        for (const auto& source : tracks_[ti]->chain.effects.sidechainSources())
            depth[ti] = std::max (depth[ti], depth[trackIndex_.at (source)] + 1);
    for (size_t ti = 0; ti < tracks_.size(); ++ti)
    {
        if ((size_t) depth[ti] >= levels_.size())
            levels_.resize ((size_t) depth[ti] + 1);
        levels_[(size_t) depth[ti]].push_back (ti);
    }
    pool_ = std::make_unique<WorkerPool> (o.threads < 0 ? WorkerPool::defaultWorkers() : o.threads);
}

RealtimePlayer::~RealtimePlayer() = default;

bool RealtimePlayer::clipsPending() const
{
    for (const auto& t : tracks_)
        if (t->chain.created.instrument->clipsPending())
            return true;
    return false;
}

void RealtimePlayer::updateTrack (size_t index, const TrackSpec& track)
{
    collectRetired();
    auto& t = *tracks_.at (index);
    std::vector<Diagnostic> warnings;
    auto content = buildTrackContent (composition_, track, *t.chain.created.def, tempo_, chainOptions_, warnings);
    infos_[index].notes = content->notes;
    lastNoteEnd_[index] = content->lastNoteEndSec;
    // 오디오 스레드는 composition_ 의 트랙을 읽지 않는다 (곡 끝을 다시 재고, apply 가 비교하려고 바꿔 둔다)
    auto& spec = composition_.tracks[index];
    spec.notes = track.notes;
    spec.automation = track.automation;
    spec.articulation = track.articulation;
    spec.transpose = track.transpose;
    spec.performance = track.performance;
    updateLength();
    t.content.post (std::move (content));   // 아직 바꿔 끼우지 않은 앞의 내용은 버린다
}

std::unique_ptr<RealtimePlayer::SendRouting> RealtimePlayer::makeRouting (const std::vector<SendSpec>& sends) const
{
    auto r = std::make_unique<SendRouting>();
    for (const auto& send : sends)
    {
        r->sends.push_back (makeTrackSend (send, busIndex_.at (send.bus), tempo_, sampleRate_));
        r->applied.push_back (r->sends.back().gain);
    }
    return r;
}

bool RealtimePlayer::apply (const Composition& edited)
{
    collectRetired();
    if (! sameStructure (composition_, edited))
        return false;
    const auto dir = edited.sourceFile.getParentDirectory();

    // 먼저 모두 만든다 (실패하면 아무것도 바꾸지 않는다)
    struct TrackPlan
    {
        std::unique_ptr<TrackContent> content;
        std::unique_ptr<EffectChain> effects;
        std::unique_ptr<SendRouting> routing;
        bool plugin = false;
        bool synth = false;
        bool clips = false;
        bool pluginSettings = false;   // 플러그인 효과의 설정만 바뀌었다
    };
    std::vector<TrackPlan> plans (tracks_.size());
    std::vector<Diagnostic> warnings;
    for (size_t i = 0; i < tracks_.size(); ++i)
    {
        const auto& before = composition_.tracks[i];
        const auto& after = edited.tracks[i];
        auto& t = *tracks_[i];
        auto& plan = plans[i];
        if (! sameContent (before, after))
            plan.content = buildTrackContent (composition_, after, *t.chain.created.def, tempo_, chainOptions_, warnings);
        if (! sameEffects (before.effects, after.effects) && samePluginEffectsExceptSettings (before.effects, after.effects))
        {
            // 플러그인 효과의 소리만 바뀌었다: 사슬을 그대로 두고 같은 플러그인에 넣는다 (열린 창이 그 플러그인을 계속 본다)
            plan.pluginSettings = true;
        }
        else if (! sameEffects (before.effects, after.effects))
        {
            plan.effects = std::make_unique<EffectChain> (createEffectChain (after.effects, dir, sampleRate_, blockSize_, &tempo_, true));
            plan.effects->alignSidechains (t.chain.instrumentLatency, [this] (const std::string& source)
            {
                const auto it = trackIndex_.find (source);
                return it != trackIndex_.end() ? tracks_[it->second]->chain.latency : 0;
            });
            // 트랙 정렬과 처리 묶음이 바뀐다
            if (plan.effects->latencySamples() != t.chain.effects.latencySamples()
                || plan.effects->sidechainSources() != t.chain.effects.sidechainSources())
                return false;
        }
        if (! sameSends (before.sends, after.sends))
            plan.routing = makeRouting (after.sends);
        if (! samePluginSettings (before.plugin, after.plugin))
            if (auto* plugin = t.chain.created.instrument->plugin())
            {
                std::vector<Diagnostic> problems;
                checkPluginSettings (*plugin, after.plugin, after.path + ".plugin", problems);
                if (! problems.empty())
                    throw Failure ("invalid-input", "track '" + after.name + "' has plugin settings the plugin does not accept", problems);
                plan.plugin = true;
            }
        plan.synth = before.synth != after.synth && t.chain.created.def->type == "synth";
        plan.clips = before.clips != after.clips && t.chain.created.def->type == "audio";
    }
    std::vector<std::unique_ptr<EffectChain>> busPlans (buses_.size());
    std::vector<bool> busSettings (buses_.size(), false);
    for (size_t i = 0; i < buses_.size(); ++i)
        if (! sameEffects (composition_.buses[i].effects, edited.buses[i].effects))
        {
            if (samePluginEffectsExceptSettings (composition_.buses[i].effects, edited.buses[i].effects))
            {
                busSettings[i] = true;
                continue;
            }
            busPlans[i] = std::make_unique<EffectChain> (createEffectChain (edited.buses[i].effects, dir, sampleRate_, blockSize_, &tempo_, true));
            busPlans[i]->alignSidechains (0, noKeyLatency);
            if (busPlans[i]->latencySamples() != buses_[i]->latency)
                return false;   // 버스 지연 맞추기가 바뀐다
        }
    std::unique_ptr<MasterEffects> masterPlan;
    const bool masterSettings = ! sameEffects (composition_.master.effects, edited.master.effects)
                                && samePluginEffectsExceptSettings (composition_.master.effects, edited.master.effects);
    if (! sameEffects (composition_.master.effects, edited.master.effects) && ! masterSettings)
    {
        masterPlan = std::make_unique<MasterEffects>();
        masterPlan->chain = createEffectChain (edited.master.effects, dir, sampleRate_, blockSize_, &tempo_, true);
        masterPlan->chain.alignSidechains (0, noKeyLatency);
        if (edited.master.targetLufs)
            masterPlan->limiter = masterPlan->chain.takeTrailingLimiter();
        if (masterPlan->chain.sidechainSources() != masterEffects_->chain.sidechainSources())
            return false;   // 마스터 키의 지연선이 바뀐다
    }

    // 넘긴다. composition_ 의 벡터 크기는 그대로라 트랙 사슬과 버스가 가리키는 spec 은 살아 있다.
    for (size_t i = 0; i < tracks_.size(); ++i)
    {
        auto& t = *tracks_[i];
        auto& plan = plans[i];
        const auto& after = edited.tracks[i];
        if (plan.content != nullptr)
        {
            infos_[i].notes = plan.content->notes;
            lastNoteEnd_[i] = plan.content->lastNoteEndSec;
            t.content.post (std::move (plan.content));
        }
        if (plan.effects != nullptr)
        {
            notePlugins ("track:" + std::to_string (i), *plan.effects);
            t.effects.post (std::move (plan.effects));
        }
        if (plan.pluginSettings)
            applyEffectPluginSettings ("track:" + std::to_string (i), after.effects, warnings);
        if (plan.routing != nullptr)
            t.routing.post (std::move (plan.routing));
        if (plan.clips)
        {
            // 파일은 캐시에서 읽는다 (새 파일은 여기서 읽고 바꾼다). 오디오 스레드는 다음 블록에서 바꿔 끼운다
            std::vector<std::string> clipWarnings;
            t.chain.created.instrument->setClips (after.clips, tempo_, clipWarnings);
            for (const auto& w : clipWarnings)
                warnings.push_back ({ after.path + ".clips", w, {} });
        }
        if (plan.synth)
        {
            // 레지스트리 값 위에 트랙 값을 덮어 프리셋을 다시 만든다 (오디오 스레드는 다음 블록에서 바꿔 끼운다)
            auto values = t.chain.created.def->synthParameters;
            for (const auto& [key, value] : after.synth)
                values[key] = value;
            t.chain.created.instrument->setSynthParameters (values);
        }
        if (plan.plugin)
        {
            // 트랙에 상태가 없으면 레지스트리 설정만 넣은 상태에서 시작한다 (트랙 설정을 지우면 원래 소리로). 지금 값과 다른 것만 넣는다.
            auto target = after.plugin;
            if (target.stateBase64.empty() && target.stateFile == juce::File())
                target.stateBase64 = t.chain.created.instrument->pluginBaseState();
            std::vector<Diagnostic> problems;
            applyPluginSettings (*t.chain.created.instrument->plugin(), target, after.path + ".plugin", problems, true);
        }
        t.fader.set (after.mixer);
        if (! sameFaderLanes (composition_.tracks[i].mixer, after.mixer))
        {
            t.fader.nextLanes.collect();
            t.fader.nextLanes.post (std::make_unique<FaderLanes> (makeFaderLanes (after.mixer, tempo_, sampleRate_)));
        }
        t.mute.store (after.mixer.muted());
        t.solo.store (after.mixer.solo);
        composition_.tracks[i] = after;
    }
    for (size_t i = 0; i < buses_.size(); ++i)
    {
        auto& b = *buses_[i];
        if (busPlans[i] != nullptr)
        {
            notePlugins ("bus:" + std::to_string (i), *busPlans[i]);
            b.nextEffects.post (std::move (busPlans[i]));
        }
        if (busSettings[i])
            applyEffectPluginSettings ("bus:" + std::to_string (i), edited.buses[i].effects, warnings);
        b.fader.set (edited.buses[i].mixer);
        if (! sameFaderLanes (composition_.buses[i].mixer, edited.buses[i].mixer))
        {
            b.fader.nextLanes.collect();
            b.fader.nextLanes.post (std::make_unique<FaderLanes> (makeFaderLanes (edited.buses[i].mixer, tempo_, sampleRate_)));
        }
        b.mute.store (edited.buses[i].mixer.muted());
        composition_.buses[i] = edited.buses[i];
    }
    if (masterPlan != nullptr)
    {
        outputLatency_.store (maxLatency_ + maxBusLatency_ + masterLatency (*masterPlan));
        notePlugins ("master", masterPlan->chain);
        nextMasterEffects_->post (std::move (masterPlan));
    }
    if (masterSettings)
        applyEffectPluginSettings ("master", edited.master.effects, warnings);
    masterFader_->set (edited.master.mixer);
    if (! sameFaderLanes (composition_.master.mixer, edited.master.mixer))
    {
        masterFader_->nextLanes.collect();
        masterFader_->nextLanes.post (std::make_unique<FaderLanes> (makeFaderLanes (edited.master.mixer, tempo_, sampleRate_)));
    }
    composition_.master = edited.master;
    composition_.render = edited.render;
    composition_.title = edited.title;
    updateLength();
    return true;
}

void RealtimePlayer::notePlugins (const std::string& owner, EffectChain& chain)
{
    auto& list = effectPlugins_[owner];
    list.assign (chain.size(), nullptr);
    for (size_t i = 0; i < chain.size(); ++i)
        if (auto* plugin = dynamic_cast<PluginEffect*> (&chain.at (i)))
            list[i] = plugin->instance();
}

void RealtimePlayer::applyEffectPluginSettings (const std::string& owner, const std::vector<EffectSpec>& effects, std::vector<Diagnostic>& warnings)
{
    for (size_t i = 0; i < effects.size(); ++i)
        if (effects[i].type == "plugin")
            if (auto plugin = effectPlugin (owner, i))
            {
                std::vector<Diagnostic> problems;
                applyPluginSettings (*plugin, pluginEffectSettings (effects[i].params), effects[i].path, problems, true);
                warnings.insert (warnings.end(), problems.begin(), problems.end());
            }
}

std::shared_ptr<juce::AudioPluginInstance> RealtimePlayer::effectPlugin (const std::string& owner, size_t effect) const
{
    const auto it = effectPlugins_.find (owner);
    return it != effectPlugins_.end() && effect < it->second.size() ? it->second[effect] : nullptr;
}

juce::AudioPluginInstance* RealtimePlayer::pluginInstance (size_t track) const
{
    return track < tracks_.size() ? tracks_[track]->chain.created.instrument->plugin() : nullptr;
}

void RealtimePlayer::collectRetired()
{
    for (auto& t : tracks_)
    {
        t->content.collect();
        t->effects.collect();
        t->routing.collect();
    }
    for (auto& b : buses_)
        b->nextEffects.collect();
    nextMasterEffects_->collect();
}

void RealtimePlayer::updateLength()
{
    const double lastNoteEnd = lastNoteEnd_.empty() ? 0.0 : *std::max_element (lastNoteEnd_.begin(), lastNoteEnd_.end());
    const double endSec = songEndSeconds (composition_, tempo_, lastNoteEnd);
    length_.store ((int64_t) std::ceil (std::max (0.1, endSec + composition_.render.tailSeconds) * sampleRate_));
}

void RealtimePlayer::takeUpdates (int64_t pos)
{
    // 옛것을 아직 지우지 않은 자리는 다음 블록에서 바꿔 끼운다 (take 가 nullptr)
    for (auto& tp : tracks_)
    {
        auto& t = *tp;
        if (auto* fresh = t.content.take())
        {
            t.content.retire (t.chain.content.release());
            t.chain.content.reset (fresh);
            releaseTrack (t);
            resyncTrack (t, pos);
        }
        if (auto* fresh = t.effects.take())
        {
            fresh->continueFrom (t.chain.effects);
            std::swap (t.chain.effects, *fresh);
            t.effects.retire (fresh);
        }
        if (auto* fresh = t.routing.take())
        {
            const auto& old = *t.sends;
            const bool sameLayout = std::equal (old.sends.begin(), old.sends.end(), fresh->sends.begin(), fresh->sends.end(),
                                                [] (const TrackSend& a, const TrackSend& b) { return a.bus == b.bus && a.preFader == b.preFader; });
            if (sameLayout)
                std::copy (old.applied.begin(), old.applied.end(), fresh->applied.begin());
            t.routing.retire (t.sends.release());
            t.sends.reset (fresh);
        }
    }
    for (auto& bp : buses_)
        if (auto* fresh = bp->nextEffects.take())
        {
            fresh->continueFrom (bp->effects);
            std::swap (bp->effects, *fresh);
            bp->nextEffects.retire (fresh);
        }
    if (auto* fresh = nextMasterEffects_->take())
    {
        fresh->chain.continueFrom (masterEffects_->chain);
        if (fresh->limiter != nullptr && masterEffects_->limiter != nullptr)
            fresh->limiter->continueFrom (*masterEffects_->limiter);
        nextMasterEffects_->retire (masterEffects_.release());
        masterEffects_.reset (fresh);
    }
}

void RealtimePlayer::applyFader (juce::AudioBuffer<float>& b, int n, Fader& f, int64_t clock) const
{
    if (auto* fresh = f.nextLanes.take())
    {
        f.nextLanes.retire (f.lanes.release());
        f.lanes.reset (fresh);
    }
    if (f.lanes != nullptr && ! f.lanes->empty())
    {
        // 곡선: 정렬된 신호의 곡 위치에서 (멈춘 동안은 그 자리의 값)
        int64_t last = 0;
        timeline_.forEachSpan (clock, n, [&] (int offset, int count, BlockPosition p)
        {
            applyFaderLanes (b, offset, count, *f.lanes, p);
            last = p.at (count - 1);
        });
        f.current = f.target = faderGainsAt (*f.lanes, last);   // 곡선을 지우면 이 값에서 고정값으로 잇는다
        f.appliedDb = std::numeric_limits<double>::quiet_NaN();
        return;
    }
    const double db = f.gainDb.load (std::memory_order_relaxed);
    const double pan = f.pan.load (std::memory_order_relaxed);
    if (db != f.appliedDb || pan != f.appliedPan)
    {
        MixerSettings m;
        m.gainDb = db;
        m.pan = pan;
        f.target = faderGains (m);
        f.appliedDb = db;
        f.appliedPan = pan;
        if (! chunkRamp_)
            f.current = f.target;
    }
    if (f.current.left != f.target.left || f.current.right != f.target.right)
    {
        b.applyGainRamp (0, 0, n, f.current.left, f.target.left);
        b.applyGainRamp (1, 0, n, f.current.right, f.target.right);
        f.current = f.target;
    }
    else
        applyGains (b, n, f.current);
}

void RealtimePlayer::mixSends (Track& t, int n)
{
    auto& r = *t.sends;
    for (size_t s = 0; s < r.sends.size(); ++s)
    {
        const auto& send = r.sends[s];
        const auto& source = send.preFader ? t.pre : t.post;
        auto& input = buses_[send.bus]->input;
        if (! send.level.empty())
        {
            // 곡선: 정렬된 신호의 곡 위치에서 (되돌아간 뒤에도 소리가 온 자리)
            int64_t last = 0;
            timeline_.forEachSpan (chunkClock_ - maxLatency_, n, [&] (int offset, int count, BlockPosition p)
            {
                addSend (input, offset, source, offset, count, send, p);
                last = p.at (count - 1);
            });
            r.applied[s] = sendGainAt (send, last);   // 곡선을 지우면 이 값에서 잇는다
            continue;
        }
        float& applied = r.applied[s];
        if (! chunkRamp_)
            applied = send.gain;
        for (int ch = 0; ch < 2; ++ch)
        {
            if (applied == send.gain)
                input.addFrom (ch, 0, source, ch, 0, n, send.gain);
            else
                input.addFromWithRamp (ch, 0, source.getReadPointer (ch), n, applied, send.gain);
        }
        applied = send.gain;
    }
}

void RealtimePlayer::play()
{
    stopAt_.store (-1);
    countInLeft_.store (0);
    if (position_.load() >= length_.load() && seekRequest_.load() < 0)
        seek (0);
    playing_.store (true);
}

void RealtimePlayer::stop()
{
    stopAt_.store (-1);
    countInLeft_.store (0);
    playing_.store (false);
}

void RealtimePlayer::setLoopRange (int64_t startSample, int64_t endSample)
{
    if (endSample <= startSample || startSample < 0)
    {
        loopStart_.store (0);
        loopEnd_.store (0);
        return;
    }
    loopStart_.store (startSample);
    loopEnd_.store (endSample);
}

void RealtimePlayer::playWithCountIn (int bars)
{
    if (bars <= 0)
    {
        play();
        return;
    }
    stopAt_.store (-1);
    countInLeft_.store (0);   // 앞의 카운트인은 지운다 (오디오 스레드가 표를 더 읽지 않는다)
    if (position_.load() >= length_.load() && seekRequest_.load() < 0)
        seek (0);
    const int64_t pos = seekRequest_.load() >= 0 ? seekRequest_.load() : position_.load();
    const double startBeat = tempo_.beatAt ((double) pos / sampleRate_);
    // 시작 자리의 박자로 마디를 센다 (6/8 의 한 마디는 4분음표 셋)
    const int beats = std::min ((int) countInClickAt_.size(), std::max (1, (int) std::llround (bars * meters_.barBeatsAt (startBeat))));
    const double firstBeat = startBeat - beats;
    const double startSec = tempo_.secondsAt (firstBeat);
    countInTotal_ = std::max<int64_t> (1, std::llround ((tempo_.secondsAt (startBeat) - startSec) * sampleRate_));
    countInClicks_ = beats;
    for (int k = 0; k < beats; ++k)
    {
        const double beat = firstBeat + k;
        countInClickAt_[(size_t) k] = std::llround ((tempo_.secondsAt (beat) - startSec) * sampleRate_);
        countInAccent_[(size_t) k] = meters_.isBarLine (beat);
    }
    playing_.store (false);
    countInLeft_.store (countInTotal_);
}

void RealtimePlayer::playRange (int64_t from, int64_t to)
{
    from = std::clamp<int64_t> (from, 0, length_.load());
    to = std::clamp<int64_t> (to, from, length_.load());
    seek (from);
    stopAt_.store (to);
    playing_.store (to > from);
}

void RealtimePlayer::seek (int64_t sample)
{
    stopAt_.store (-1);
    countInLeft_.store (0);
    sample = std::clamp<int64_t> (sample, 0, length_.load());
    seekRequest_.store (sample);
    position_.store (sample);
}

void RealtimePlayer::setTrackMute (size_t index, bool mute) { tracks_.at (index)->mute.store (mute); }
void RealtimePlayer::setTrackSolo (size_t index, bool solo) { tracks_.at (index)->solo.store (solo); }
bool RealtimePlayer::trackMute (size_t index) const { return tracks_.at (index)->mute.load(); }
bool RealtimePlayer::trackSolo (size_t index) const { return tracks_.at (index)->solo.load(); }
StereoPeak RealtimePlayer::takeTrackPeaks (size_t index)
{
    auto& t = *tracks_.at (index);
    return { t.peak[0].exchange (0.0f), t.peak[1].exchange (0.0f) };
}

float RealtimePlayer::takeTrackPeak (size_t index)
{
    const auto p = takeTrackPeaks (index);
    return std::max (p.left, p.right);
}

StereoPeak RealtimePlayer::takeBusPeaks (size_t index)
{
    auto& b = *buses_.at (index);
    return { b.peak[0].exchange (0.0f), b.peak[1].exchange (0.0f) };
}

int RealtimePlayer::copyMasterSamples (float* dest, int count) const noexcept
{
    count = std::clamp (count, 0, tapSize / 2);
    const uint32_t end = tapWrite_.load (std::memory_order_acquire);
    for (int i = 0; i < count; ++i)
        dest[i] = tap_[(size_t) ((end - (uint32_t) count + (uint32_t) i) % tapSize)];
    return count;
}

StereoPeak RealtimePlayer::takeMasterPeaks() { return { masterPeak_[0].exchange (0.0f), masterPeak_[1].exchange (0.0f) }; }
float RealtimePlayer::takeMasterPeak (int channel) { return masterPeak_[std::clamp (channel, 0, 1)].exchange (0.0f); }
void RealtimePlayer::setMonitorGainDb (double db) { monitorGainDb_.store (std::clamp (db, -60.0, 36.0)); }

void RealtimePlayer::auditionNoteOn (size_t track, int pitch, int velocity, const std::string& articulation, int channel)
{
    if (track >= tracks_.size())
        return;
    auditionNoteOff (track, pitch);
    const auto& spec = composition_.tracks.at (track);
    const auto& def = *tracks_[track]->chain.created.def;
    HeldNote held;
    held.channel = std::clamp (channel, 1, 16);
    held.pitch = instrumentKey (pitch, spec.transpose + def.transpose, &def.keymap);
    const auto it = def.profile.articulations.find (resolveArticulation (articulation, spec, def.profile));
    if (it != def.profile.articulations.end())
    {
        const auto& art = it->second;
        velocity += art.velocityOffset;
        for (const auto& [cc, value] : art.ccs)
        {
            pushLive ({ track, false, juce::MidiMessage::controllerEvent (held.channel, cc, value) });
            held.controllers = true;
        }
        if (def.type == "sfz")
            if (const int ch = sfzChannelFor (def, art.name); ch > 0)
                held.channel = ch;
    }
    pushLive ({ track, false, juce::MidiMessage::noteOn (held.channel, held.pitch, (juce::uint8) std::clamp (velocity, 1, 127)) });
    held_[{ track, pitch }] = held;
}

void RealtimePlayer::auditionNoteOff (size_t track, int pitch)
{
    const auto it = held_.find ({ track, pitch });
    if (it == held_.end())
        return;
    pushLive ({ track, false, juce::MidiMessage::noteOff (it->second.channel, it->second.pitch) });
    if (it->second.controllers)
        pushLive ({ track, true, {} });
    held_.erase (it);
}

void RealtimePlayer::auditionAllOff()
{
    while (! held_.empty())
        auditionNoteOff (held_.begin()->first.first, held_.begin()->first.second);
}

void RealtimePlayer::auditionController (size_t track, int controller, int value, int channel)
{
    if (track >= tracks_.size())
        return;
    channel = std::clamp (channel, 1, 16);
    if (controller == kPitchBend)
        pushLive ({ track, false, juce::MidiMessage::pitchWheel (channel, std::clamp (value, 0, 16383)) });
    else if (controller >= 0 && controller <= 127)
        pushLive ({ track, false, juce::MidiMessage::controllerEvent (channel, controller, std::clamp (value, 0, 127)) });
}

// ---- 메트로놈 ----

void RealtimePlayer::addClick (int64_t start, bool accent)
{
    for (auto& c : clicks_)
        if (! c.active)
        {
            c = { start, accent, true };
            return;
        }
}

void RealtimePlayer::clearClicks()
{
    for (auto& c : clicks_)
        c.active = false;
}

void RealtimePlayer::scheduleBeatClicks (int64_t pos, int n)
{
    const double beat0 = tempo_.beatAt ((double) pos / sampleRate_);
    double b = std::ceil (beat0 - 1.0e-6);
    for (int guard = 0; guard < 64; ++guard, b += 1.0)
    {
        const int64_t s = tempo_.sampleAt (b, sampleRate_);
        if (s >= pos + n)
            break;
        if (s < pos)
            continue;
        addClick (s, meters_.isBarLine (b));   // 마디의 첫 박은 높은 소리
    }
}

void RealtimePlayer::scheduleCountInClicks (int64_t elapsed, int n)
{
    for (int k = 0; k < countInClicks_; ++k)
    {
        const int64_t s = countInClickAt_[(size_t) k];
        if (s >= elapsed && s < elapsed + n)
            addClick (s, countInAccent_[(size_t) k]);
    }
}

void RealtimePlayer::renderClicks (int64_t axisPos, int n)
{
    const float gain = (float) juce::Decibels::decibelsToGain (metronomeGainDb_.load(), -60.0) * 0.4f;
    constexpr double length = 0.03;
    for (auto& c : clicks_)
    {
        if (! c.active)
            continue;
        if (axisPos - c.start > (int64_t) (length * sampleRate_))
        {
            c.active = false;
            continue;
        }
        const double freq = c.accent ? 1760.0 : 1320.0;
        const float level = c.accent ? gain : gain * 0.7f;
        float* left = master_.getWritePointer (0);
        float* right = master_.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) (axisPos + i - c.start) / sampleRate_;
            if (t < 0.0)
                continue;
            if (t >= length)
                break;
            const float v = level * (float) (std::exp (-t * 180.0) * std::sin (2.0 * juce::MathConstants<double>::pi * freq * t));
            left[i] += v;
            right[i] += v;
        }
    }
}

void RealtimePlayer::pushLive (const LiveEvent& event)
{
    const auto scope = liveFifo_.write (1);   // 가득 차면(오디오가 멈춰 있을 때) 버린다
    if (scope.blockSize1 > 0)
        liveEvents_[(size_t) scope.startIndex1] = event;
    else if (scope.blockSize2 > 0)
        liveEvents_[(size_t) scope.startIndex2] = event;
}

void RealtimePlayer::takeLiveEvents()
{
    const auto scope = liveFifo_.read (liveFifo_.getNumReady());
    auto take = [this] (int start, int count)
    {
        for (int i = start; i < start + count; ++i)
        {
            const auto& e = liveEvents_[(size_t) i];
            if (e.track >= tracks_.size())
                continue;
            auto& t = *tracks_[e.track];
            if (e.resync)
            {
                resyncTrack (t, position_.load());
                continue;
            }
            t.pending.addEvent (e.message, 0);
            const auto ch = (size_t) std::clamp (e.message.getChannel() - 1, 0, 15);
            t.channelsUsed.set (ch);
            if (e.message.isNoteOn())
                t.sounding[ch].set ((size_t) e.message.getNoteNumber());
            else if (e.message.isNoteOff())
                t.sounding[ch].reset ((size_t) e.message.getNoteNumber());
        }
    };
    take (scope.startIndex1, scope.blockSize1);
    take (scope.startIndex2, scope.blockSize2);
}

void RealtimePlayer::releaseAll()
{
    for (auto& t : tracks_)
        releaseTrack (*t);
}

void RealtimePlayer::releaseTrack (Track& t)
{
    for (int ch = 0; ch < 16; ++ch)
    {
        if (t.sounding[(size_t) ch].any())
            for (int note = 0; note < 128; ++note)
                if (t.sounding[(size_t) ch][(size_t) note])
                    t.pending.addEvent (juce::MidiMessage::noteOff (ch + 1, note), 0);
        t.sounding[(size_t) ch].reset();
        if (t.channelsUsed[(size_t) ch])
        {
            t.pending.addEvent (juce::MidiMessage::controllerEvent (ch + 1, 64, 0), 0);
            t.pending.addEvent (juce::MidiMessage::allNotesOff (ch + 1), 0);
        }
    }
}

void RealtimePlayer::applySeek (int64_t target)
{
    releaseAll();
    for (auto& t : tracks_)
        resyncTrack (*t, target);
    position_.store (target);
}

void RealtimePlayer::resyncTrack (Track& t, int64_t target)
{
    const auto& events = t.chain.content->events;
    const auto it = std::lower_bound (events.begin(), events.end(), target,
                                      [] (const ScheduledEvent& e, int64_t s) { return e.sample < s; });
    t.cursor = (size_t) std::distance (events.begin(), it);

    // 새 위치 앞의 마지막 컨트롤러, 프로그램, 피치 벤드, 애프터터치를 다시 보낸다 (음은 다시 켜지 않는다)
    for (auto& row : t.ccState)
        row.fill (-1);
    t.programState.fill (-1);
    t.bendState.fill (-1);
    t.pressureState.fill (-1);
    for (size_t i = 0; i < t.cursor; ++i)
    {
        const auto& m = events[i].message;
        const int ch = std::clamp (m.getChannel() - 1, 0, 15);
        if (m.isController())
            t.ccState[(size_t) ch][(size_t) m.getControllerNumber()] = (int16_t) m.getControllerValue();
        else if (m.isProgramChange())
            t.programState[(size_t) ch] = (int16_t) m.getProgramChangeNumber();
        else if (m.isPitchWheel())
            t.bendState[(size_t) ch] = m.getPitchWheelValue();
        else if (m.isChannelPressure())
            t.pressureState[(size_t) ch] = (int16_t) m.getChannelPressureValue();
    }
    for (int ch = 0; ch < 16; ++ch)
    {
        if (t.programState[(size_t) ch] >= 0)
            t.pending.addEvent (juce::MidiMessage::programChange (ch + 1, t.programState[(size_t) ch]), 0);
        for (int cc = 0; cc < 128; ++cc)
            if (t.ccState[(size_t) ch][(size_t) cc] >= 0)
                t.pending.addEvent (juce::MidiMessage::controllerEvent (ch + 1, cc, t.ccState[(size_t) ch][(size_t) cc]), 0);
        if (t.bendState[(size_t) ch] >= 0)
            t.pending.addEvent (juce::MidiMessage::pitchWheel (ch + 1, t.bendState[(size_t) ch]), 0);
        if (t.pressureState[(size_t) ch] >= 0)
            t.pending.addEvent (juce::MidiMessage::channelPressureChange (ch + 1, t.pressureState[(size_t) ch]), 0);
    }
}

void RealtimePlayer::process (float* const* out, int numChannels, int numSamples, const float* const* in, int numInputs)
{
    input_ = in;
    numInputs_ = in != nullptr ? numInputs : 0;
    process (out, numChannels, numSamples);
    input_ = nullptr;
    numInputs_ = 0;
}

void RealtimePlayer::process (float* const* out, int numChannels, int numSamples)
{
    for (int offset = 0; offset < numSamples;)
    {
        int n = std::min (blockSize_, numSamples - offset);
        const int64_t stopAt = stopAt_.load();
        if (stopAt >= 0 && playing_.load() && seekRequest_.load() < 0)
        {
            const int64_t left = stopAt - position_.load();   // 블록을 나눠 끝 샘플에서 멈춘다
            if (left > 0 && left < n)
                n = (int) left;
        }
        if (looping_.load() && playing_.load() && seekRequest_.load() < 0)
        {
            const int64_t end = loopEnd_.load();
            const int64_t left = end - position_.load();   // 반복 끝 샘플에서 블록을 나눈다
            if (end > loopStart_.load() && left > 0 && left < n)
                n = (int) left;
        }
        if (const int64_t countIn = countInLeft_.load(); countIn > 0 && countIn < n)
            n = (int) countIn;   // 카운트인이 끝나는 샘플에서 나눈다
        processChunk (out, numChannels, offset, n);
        offset += n;
    }
}

void RealtimePlayer::processChunk (float* const* out, int numChannels, int offset, int n)
{
    const int64_t seekTo = seekRequest_.exchange (-1);
    if (seekTo >= 0)
        applySeek (seekTo);
    takeUpdates (position_.load());
    takeLiveEvents();
    const int64_t countInLeft = countInLeft_.load();
    const bool countingIn = countInLeft > 0;
    const bool playing = playing_.load() && ! countingIn;
    if (wasPlaying_ && ! playing)
        releaseAll();
    chunkRamp_ = wasPlaying_;   // 멈춰 있다가 바꾼 값은 바로 쓴다 (재생 전에 고친 곡이 새로 만든 재생기와 같다)
    wasPlaying_ = playing;
    const int64_t pos = position_.load();
    timeline_.record (clock_, pos, ! playing);   // 멈춘 동안은 곡선과 덕킹을 그 자리의 값에 붙든다
    chunkClock_ = clock_;
    clock_ += n;

    master_.clear (0, n);
    for (auto& b : buses_)
        b->input.clear (0, n);

    chunkPos_ = pos;
    chunkN_ = n;
    chunkOffset_ = offset;
    chunkPlaying_ = playing;
    chunkAnySolo_ = std::any_of (tracks_.begin(), tracks_.end(), [] (const auto& t) { return t->solo.load(); });

    // 트랙을 묶음마다 나눠 처리하고, 합치기는 정해진 순서로 한다 (일꾼 수와 상관없이 결과가 같다)
    for (const auto& level : levels_)
    {
        currentLevel_ = &level;
        pool_->run (level.size(), [] (void* self, size_t i)
        {
            auto* p = static_cast<RealtimePlayer*> (self);
            p->renderTrack ((*p->currentLevel_)[i]);
        }, this);
    }
    direct_.clear (0, n);
    for (const size_t ti : order_)
    {
        auto& t = *tracks_[ti];
        mixSends (t, n);
        auto& destination = t.outputBus >= 0 ? buses_[(size_t) t.outputBus]->input : direct_;   // 서브그룹 버스나 마스터
        for (int ch = 0; ch < 2; ++ch)
            destination.addFrom (ch, 0, t.post, ch, 0, n);
    }
    if (maxBusLatency_ > 0)
        directAlign_.process (direct_, n);   // 버스 효과의 지연만큼 늦춰 버스와 맞춘다
    for (int ch = 0; ch < 2; ++ch)
        master_.addFrom (ch, 0, direct_, ch, 0, n);

    pool_->run (buses_.size(), [] (void* self, size_t i) { static_cast<RealtimePlayer*> (self)->renderBus (i); }, this);
    for (auto& b : buses_)
        for (int ch = 0; ch < 2; ++ch)
            master_.addFrom (ch, 0, b->input, ch, 0, n);

    const int64_t masterClock = chunkClock_ - maxLatency_ - maxBusLatency_;   // 마스터에 모인 소리의 시계
    applyFader (master_, n, *masterFader_, masterClock);
    for (auto& key : masterKeys_)
    {
        // 정렬된 프리 페이더 신호를 버스 지연만큼 늦춘다
        const auto it = trackIndex_.find (key->source);
        const auto& source = it != trackIndex_.end() ? tracks_[it->second]->pre : silence_;
        for (int ch = 0; ch < 2; ++ch)
            key->signal.copyFrom (ch, 0, source, ch, 0, n);
        if (maxBusLatency_ > 0)
            key->align.process (key->signal, n);
        masterEffects_->chain.setSidechainInput (key->source, key->signal.getReadPointer (0), key->signal.getReadPointer (1));
    }
    masterEffects_->chain.process (master_, n, timeline_, masterClock);
    const float monitorTarget = (float) juce::Decibels::decibelsToGain (monitorGainDb_.load(), -60.0);
    if (monitorGain_ != monitorTarget || monitorTarget != 1.0f)
    {
        for (int ch = 0; ch < 2; ++ch)
            master_.applyGainRamp (ch, 0, n, monitorGain_, monitorTarget);
        monitorGain_ = monitorTarget;
    }
    if (masterEffects_->limiter != nullptr)
        masterEffects_->limiter->process (master_, n);
    storeMax (masterPeak_[0], master_.getMagnitude (0, 0, n));
    storeMax (masterPeak_[1], master_.getMagnitude (1, 0, n));
    loudness_.process (master_.getReadPointer (0), master_.getReadPointer (1), n);
    {
        const float* l = master_.getReadPointer (0);
        const float* r = master_.getReadPointer (1);
        uint32_t w = tapWrite_.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
            tap_[(size_t) ((w + (uint32_t) i) % tapSize)] = 0.5f * (l[i] + r[i]);
        tapWrite_.store (w + (uint32_t) n, std::memory_order_release);
    }

    // 메트로놈과 카운트인의 클릭은 미터 뒤에 더한다 (곡의 소리가 아니다)
    if (countingIn)
    {
        const int64_t elapsed = countInTotal_ - countInLeft;
        scheduleCountInClicks (elapsed, n);
        renderClicks (elapsed, n);
    }
    else if (playing && metronome_.load())
    {
        scheduleBeatClicks (pos, n);
        renderClicks (pos, n);
    }
    else
        clearClicks();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        if (out[ch] == nullptr)
            continue;
        if (ch < 2)
            std::copy (master_.getReadPointer (ch), master_.getReadPointer (ch) + n, out[ch] + offset);
        else
            std::fill (out[ch] + offset, out[ch] + offset + n, 0.0f);
    }

    if (countingIn)
    {
        // 카운트인이 끝나면 다음 블록부터 재생한다 (process 가 끝 샘플에서 블록을 나눴다)
        int64_t expected = countInLeft;   // 그 사이에 멈췄거나 다시 시작했으면 그쪽을 따른다
        if (countInLeft_.compare_exchange_strong (expected, std::max<int64_t> (0, countInLeft - n)) && countInLeft - n <= 0)
        {
            clearClicks();
            playing_.store (true);
        }
        return;
    }

    if (playing)
    {
        const int64_t next = pos + n;
        const int64_t length = length_.load();
        const int64_t stopAt = stopAt_.load();
        const int64_t loopStart = loopStart_.load(), loopEnd = loopEnd_.load();
        if (stopAt >= 0 && next >= stopAt && seekRequest_.load() < 0)
        {
            playing_.store (false);
            stopAt_.store (-1);
            position_.store (std::min (stopAt, length));
        }
        else if (looping_.load() && loopEnd > loopStart && pos < loopEnd && next >= loopEnd && seekRequest_.load() < 0)
        {
            applySeek (loopStart);   // 되돌아간다: 울리는 음을 끄고 컨트롤러를 그 자리의 값으로
            clearClicks();
        }
        else if (next >= length)
        {
            playing_.store (false);
            position_.store (length);
        }
        else if (seekRequest_.load() < 0)
            position_.store (next);
    }
}

void RealtimePlayer::renderTrack (size_t index)
{
    auto& t = *tracks_[index];
    const int n = chunkN_;
    const int64_t pos = chunkPos_;
    t.midi.clear();
    t.midi.addEvents (t.pending, 0, -1, 0);
    t.pending.clear();
    if (chunkPlaying_)
    {
        const auto& events = t.chain.content->events;
        while (t.cursor < events.size() && events[t.cursor].sample < pos + n)
        {
            const auto& e = events[t.cursor++];
            const auto& m = e.message;
            t.midi.addEvent (m, (int) std::max<int64_t> (0, e.sample - pos));
            const int ch = std::clamp (m.getChannel() - 1, 0, 15);
            t.channelsUsed.set ((size_t) ch);
            if (m.isNoteOn())
                t.sounding[(size_t) ch].set ((size_t) m.getNoteNumber());
            else if (m.isNoteOff())
                t.sounding[(size_t) ch].reset ((size_t) m.getNoteNumber());
        }
    }

    t.chain.created.instrument->setTransportPlaying (chunkPlaying_);
    t.chain.created.instrument->render (t.pre, n, t.midi, pos);
    t.pre.applyGain (0, n, t.chain.instrumentGain);
    if (numInputs_ > 0 && (int) index == inputMonitor_.load (std::memory_order_relaxed))
        for (int ch = 0; ch < 2; ++ch)
            if (const float* source = input_[std::min (ch, numInputs_ - 1)]; source != nullptr)
                t.pre.addFrom (ch, 0, source + chunkOffset_, n);   // 입력 모니터링: 악기 자리에 입력
    const int64_t clock = chunkClock_ - t.chain.instrumentLatency;   // 악기에서 나온 소리의 시계
    auto& expression = t.chain.content->expression;
    if (expression.active())
        timeline_.forEachSpan (clock, n, [&] (int offset, int count, BlockPosition p)
        {
            if (offset == 0 && count == n)
                expression.process (t.pre, n, p.start, p.held);
            else
            {
                juce::AudioBuffer<float> part (t.pre.getArrayOfWritePointers(), 2, offset, count);
                expression.process (part, count, p.start, p.held);
            }
        });
    for (const auto& source : t.chain.effects.sidechainSources())
    {
        const auto& key = tracks_[trackIndex_.at (source)]->key;   // 앞 묶음에서 이미 처리했다 (정렬 전)
        t.chain.effects.setSidechainInput (source, key.getReadPointer (0), key.getReadPointer (1));
    }
    t.chain.effects.process (t.pre, n, timeline_, clock);
    if (t.isKey)
        for (int ch = 0; ch < 2; ++ch)
            t.key.copyFrom (ch, 0, t.pre, ch, 0, n);
    t.align.process (t.pre, n);

    const float target = (! t.mute.load() && (! chunkAnySolo_ || t.solo.load())) ? 1.0f : 0.0f;
    if (! chunkRamp_)
        t.gain = target;   // 멈춰 있을 때 바꾼 뮤트와 솔로는 바로 쓴다 (파일에서 끈 트랙이 첫 블록에 새지 않는다)
    if (t.gain != target || target != 1.0f)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            t.pre.applyGainRamp (ch, 0, n, t.gain, target);
            if (t.isKey)
                t.key.applyGainRamp (ch, 0, n, t.gain, target);
        }
        t.gain = target;
    }
    for (int ch = 0; ch < 2; ++ch)
        t.post.copyFrom (ch, 0, t.pre, ch, 0, n);
    applyFader (t.post, n, t.fader, chunkClock_ - maxLatency_);
    storePeaks (t.peak, t.post, n);
}

void RealtimePlayer::renderBus (size_t index)
{
    auto& b = *buses_[index];
    const int n = chunkN_;
    for (const auto& source : b.effects.sidechainSources())
    {
        const auto it = trackIndex_.find (source);
        const auto& key = it != trackIndex_.end() ? tracks_[it->second]->pre : silence_;
        b.effects.setSidechainInput (source, key.getReadPointer (0), key.getReadPointer (1));
    }
    b.effects.process (b.input, n, timeline_, chunkClock_ - maxLatency_);
    applyFader (b.input, n, b.fader, chunkClock_ - maxLatency_ - b.latency);   // 효과를 지난 소리의 곡 위치
    const float target = b.mute.load() ? 0.0f : 1.0f;
    if (! chunkRamp_)
        b.muteGain = target;
    if (b.muteGain != target || target != 1.0f)
    {
        for (int ch = 0; ch < 2; ++ch)
            b.input.applyGainRamp (ch, 0, n, b.muteGain, target);
        b.muteGain = target;
    }
    storePeaks (b.peak, b.input, n);
    if (maxBusLatency_ > 0)
        b.align.process (b.input, n);
}

void RealtimePlayer::prepareMasterKeys (const EffectChain& chain)
{
    masterKeys_.clear();
    for (const auto& source : chain.sidechainSources())
    {
        auto key = std::make_unique<MasterKey>();
        key->source = source;
        key->align.prepare (2, maxBusLatency_);
        key->signal.setSize (2, blockSize_);
        key->signal.clear();
        masterKeys_.push_back (std::move (key));
    }
}

} // namespace mr
