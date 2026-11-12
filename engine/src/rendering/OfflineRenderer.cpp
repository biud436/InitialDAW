#include "rendering/OfflineRenderer.h"

#include "instruments/AudioClips.h"

#include "core/Log.h"
#include "core/Random.h"
#include "effects/Effects.h"
#include "instruments/AudioClips.h"
#include "io/AudioFile.h"
#include "mixer/Mixer.h"
#include "performance/Performance.h"
#include "rendering/EventBuilder.h"
#include "rendering/Expression.h"
#include "rendering/TrackChain.h"
#include "timing/TempoMap.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <tuple>

namespace mr
{

Registry buildRegistry (const Composition& c, const RenderOptions& options, Diagnostics& d)
{
    Registry registry;
    if (options.useDefaultRegistry)
    {
        const auto def = defaultRegistryFile();
        if (def.existsAsFile())
            registry.addFile (def, d);
    }
    for (const auto& f : c.registries)
        registry.addFile (f, d);
    for (const auto& t : c.inlineInstruments)
        registry.addInline (t.table, c.sourceFile, t.path, d);
    for (const auto& f : options.registries)
        registry.addFile (f, d);
    return registry;
}

double estimateDurationSeconds (const Composition& c)
{
    const TempoMap tempo (c.tempo);
    double end = 0.0;
    if (c.render.endBeat)
        end = tempo.secondsAt (*c.render.endBeat);
    else
        for (const auto& t : c.tracks)
        {
            for (const auto& n : t.notes)
                end = std::max (end, tempo.secondsAt (n.startBeat + n.durationBeat));
            for (const auto& clip : t.clips)
                end = std::max (end, clipEndSeconds (clip, tempo, c.sourceFile.getParentDirectory()));
            for (const auto& [id, lane] : t.automation)
                if (! lane.empty())
                    end = std::max (end, tempo.secondsAt (lane.points.back().beat));
        }
    return end + c.render.tailSeconds;
}

namespace
{

struct Meter
{
    double peak = 0.0;
    double power = 0.0;

    void add (const juce::AudioBuffer<float>& b, int start, int n)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const float* x = b.getReadPointer (ch);
            for (int i = start; i < start + n; ++i)
            {
                const double v = x[i];
                peak = std::max (peak, std::abs (v));
                power += v * v;
            }
        }
    }

    double peakDb() const { return gainToDb (peak); }
    double rmsDb (int64_t total) const { return power > 0.0 && total > 0 ? 10.0 * std::log10 (power / (2.0 * (double) total)) : -1.0e9; }
};

struct TrackState
{
    int outputBus = -1;   // 페이더 뒤의 소리를 보낼 버스 (-1 은 마스터)
    const TrackSpec* spec = nullptr;
    TrackChain chain;
    size_t cursor = 0;
    DelayLine align;
    juce::AudioBuffer<float> pre, post;
    juce::AudioBuffer<float> key;   // 다른 트랙의 사이드체인 키일 때: 정렬 전의 프리 페이더 신호
    std::unique_ptr<StreamingWavWriter> stem;
    std::unique_ptr<StemMeter> measure;
    Meter meter;
    TrackReport report;
};

struct BusState
{
    const BusSpec* spec = nullptr;
    EffectChain chain;   // 만들기 단계에서 (플러그인 효과는 메시지 스레드에서 만든다)
    ChannelGains fader;
    juce::AudioBuffer<float> input;   // 곡 전체 길이 (효과는 루프 뒤에 전체 버퍼로)
    std::unique_ptr<StreamingWavWriter> stem;
    std::unique_ptr<StemMeter> measure;
    Meter meter;
    BusReport report;
};

MeasuredStem measuredStem (const std::string& name, const std::string& kind, StemMeter& meter, const std::vector<int64_t>& bars)
{
    meter.finish();
    MeasuredStem m;
    m.name = name;
    m.kind = kind;
    for (size_t i = 0; i + 1 < bars.size(); ++i)
    {
        m.barDb.push_back (powerDb (meter.power (bars[i], bars[i + 1])));
        m.barSideDb.push_back (powerDb (meter.sidePower (bars[i], bars[i + 1])));
        auto bands = meter.bandPower (bars[i], bars[i + 1]);
        for (auto& v : bands)
            v = powerDb (v);
        m.barBandDb.push_back (bands);
    }
    return m;
}

// 마디의 경계 (샘플), 곡 끝 (꼬리 포함) 까지
std::vector<std::pair<double, int64_t>> barBounds (const Composition& c, const TempoMap& tempo, double sr, int64_t total)
{
    const auto meters = c.meters();
    std::vector<std::pair<double, int64_t>> out;
    for (int bar = 0;; ++bar)
    {
        const double beat = meters.barStart (bar);
        const int64_t at = (int64_t) std::llround (tempo.secondsAt (beat) * sr);
        if (at >= total)
        {
            out.emplace_back (tempo.beatAt ((double) total / sr), total);
            break;
        }
        out.emplace_back (beat, at);
    }
    return out;
}

MeasureReport measureReport (const Composition& c, const TempoMap& tempo, double sr, int64_t total, std::vector<TrackState>& tracks,
                             std::vector<BusState>& buses, StemMeter& master)
{
    MeasureReport r;
    r.applied = true;
    r.sampleRate = sr;
    r.bandEdges = measureBandEdges (sr);
    const auto bounds = barBounds (c, tempo, sr, total);
    std::vector<int64_t> bars;
    for (const auto& [beat, at] : bounds)
    {
        r.barStartBeats.push_back (beat);
        r.barStartSeconds.push_back ((double) at / sr);
        bars.push_back (at);
    }
    auto window = [&] (StemMeter& m, int64_t at, double from, double to)
    {
        return powerDb (m.power (at + (int64_t) std::llround (from * sr), at + (int64_t) std::llround (to * sr)));
    };
    for (auto& ts : tracks)
    {
        if (! ts.measure)
            continue;
        auto stem = measuredStem (ts.spec->name, "track", *ts.measure, bars);
        std::map<std::pair<int, int>, std::vector<std::pair<size_t, int64_t>>> open;   // (채널, 음높이) → 켜진 음 (번호, 샘플)
        for (const auto& e : ts.chain.content->events)
            if (e.message.isNoteOn())
            {
                NoteLevel n;
                n.beat = tempo.beatAt ((double) e.sample / sr);
                n.pitch = e.message.getNoteNumber();
                n.attackDb = window (*ts.measure, e.sample, 0.03, 0.25);
                n.earlyDb = window (*ts.measure, e.sample, 0.05, 0.1);
                n.lateDb = window (*ts.measure, e.sample, 0.25, 0.35);
                open[{ e.message.getChannel(), n.pitch }].emplace_back (stem.notes.size(), e.sample);
                stem.notes.push_back (n);
            }
            else if (e.message.isNoteOff())
                if (auto it = open.find ({ e.message.getChannel(), e.message.getNoteNumber() }); it != open.end() && ! it->second.empty())
                {
                    const auto [index, on] = it->second.front();
                    stem.notes[index].seconds = (double) (e.sample - on) / sr;
                    it->second.erase (it->second.begin());
                }
        r.stems.push_back (std::move (stem));
    }
    for (auto& bs : buses)
        if (bs.measure)
            r.stems.push_back (measuredStem (bs.spec->name, "bus", *bs.measure, bars));
    r.stems.push_back (measuredStem ("master", "master", master, bars));
    return r;
}

void fadeOut (juce::AudioBuffer<float>& buffer, int64_t total, double sampleRate, double seconds)
{
    const int64_t n = std::min<int64_t> (total, (int64_t) std::llround (seconds * sampleRate));
    if (n <= 0)
        return;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* x = buffer.getWritePointer (ch);
        for (int64_t i = 0; i < n; ++i)
        {
            const double t = (double) i / (double) n;   // 0 → 1
            x[total - n + i] *= (float) (0.5 * (1.0 + std::cos (3.141592653589793 * t)));
        }
    }
}

void applyGains (juce::AudioBuffer<float>& b, int start, int n, ChannelGains g)
{
    b.applyGain (0, start, n, g.left);
    b.applyGain (1, start, n, g.right);
}

void raisedCosineFade (juce::AudioBuffer<float>& buffer, int64_t from, int64_t count, bool in)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* x = buffer.getWritePointer (ch);
        for (int64_t i = 0; i < count; ++i)
        {
            const double t = (double) i / (double) count;   // 0 → 1
            const double g = 0.5 * (1.0 + std::cos (3.141592653589793 * t));
            x[from + i] *= (float) (in ? 1.0 - g : g);
        }
    }
}

// 두 창 [a - n, a) 와 [b - n, b) 의 차이 (b 쪽 크기에 대한 dB) 와 b 쪽 크기
std::pair<std::optional<double>, std::optional<double>> seamDifference (const juce::AudioBuffer<float>& x, int64_t a, int64_t b, int64_t n)
{
    double power = 0.0, diff = 0.0;
    for (int ch = 0; ch < x.getNumChannels(); ++ch)
    {
        const float* p = x.getReadPointer (ch);
        for (int64_t i = 0; i < n; ++i)
        {
            const double end = p[b - n + i], begin = p[a - n + i];
            power += end * end;
            diff += (end - begin) * (end - begin);
        }
    }
    if (n <= 0 || power <= 0.0)
        return { std::nullopt, std::nullopt };
    const double level = 10.0 * std::log10 (power / (double) (n * x.getNumChannels()));
    return { diff > 0.0 ? 10.0 * std::log10 (diff / power) : -1.0e9, level };
}

void setProgress (const RenderOptions& o, float value)
{
    if (o.progress != nullptr)
        o.progress->store (value);
}

} // namespace

struct OfflineRender::State
{
    State (const Composition& composition, const RenderOptions& o) : c (composition), options (o), tempo (c.tempo) {}

    Composition c;
    RenderOptions options;
    TempoMap tempo;
    Registry registry;
    RenderReport report;
    std::vector<BusState> buses;
    std::map<std::string, size_t> busIndex;
    std::vector<TrackState> tracks;
    EffectChain masterChain;
    double lastNoteEnd = 0.0;
    std::chrono::steady_clock::time_point clockStart;
    bool ran = false;
};

OfflineRender::OfflineRender (const Composition& composition, const RenderOptions& options)
    : state_ (std::make_unique<State> (composition, options))
{
    auto& s = *state_;
    const auto& c = s.c;
    s.clockStart = std::chrono::steady_clock::now();
    if (options.sampleRate < 8000.0 || options.sampleRate > 384000.0)
        throw Failure ("invalid-input", "sample rate must be between 8000 and 384000 Hz");
    if (options.blockSize < 16 || options.blockSize > 16384)
        throw Failure ("invalid-input", "block size must be between 16 and 16384");
    if (options.loop && options.range)
        throw Failure ("invalid-input", "a loop export and a range export cannot be combined");
    if (options.loop && ! c.loop)
        throw Failure ("invalid-input", "the song has no loop (metadata.loop with startBeat and endBeat)");
    if (options.range && (options.range->first < 0.0 || options.range->second <= options.range->first))
        throw Failure ("invalid-input", "the range must start at beat 0 or later and end after it starts");
    if (options.rangeFadeSeconds < 0.0 || options.rangeFadeSeconds > 60.0)
        throw Failure ("invalid-input", "the range fade must be between 0 and 60 seconds");

    if (options.output != juce::File())
        checkAudioFileFormat ({ options.fileType, options.bitDepth, options.quality }, options.sampleRate, 2, options.loop);

    Diagnostics regDiag;
    s.registry = buildRegistry (c, options, regDiag);
    if (! regDiag.ok())
        throw Failure ("invalid-input", "instrument registry has errors", regDiag.errors);

    auto& report = s.report;
    report.warnings = regDiag.warnings;
    report.bitDepth = options.bitDepth;
    report.format = audioFileTypeName (options.fileType);
    report.quality = options.quality >= 0 ? options.quality : defaultQuality (options.fileType);
    report.seed = options.seed ? *options.seed : c.performance.humanizeSeed.value_or (0);
    report.output = options.output.getFullPathName().toStdString();

    // ---- 버스 ----
    s.buses = std::vector<BusState> (c.buses.size());
    for (size_t i = 0; i < c.buses.size(); ++i)
    {
        auto& bs = s.buses[i];
        bs.spec = &c.buses[i];
        bs.fader = c.buses[i].mixer.muted() ? ChannelGains { 0.0f, 0.0f } : faderGains (c.buses[i].mixer);
        bs.report.name = c.buses[i].name;
        s.busIndex[c.buses[i].name] = i;
    }

    // ---- 트랙: 악기, 연주 처리, 이벤트 ----
    const bool anySolo = std::any_of (c.tracks.begin(), c.tracks.end(), [] (const TrackSpec& t) { return t.mixer.solo; });
    s.tracks = std::vector<TrackState> (c.tracks.size());
    for (size_t ti = 0; ti < c.tracks.size(); ++ti)
    {
        auto& ts = s.tracks[ti];
        ts.spec = &c.tracks[ti];
        ts.report.name = ts.spec->name;
        ts.report.instrument = ts.spec->instrument;
        ts.report.audible = ! ts.spec->mixer.muted() && (! anySolo || ts.spec->mixer.solo);
        if (! ts.report.audible)
            continue;

        TrackChainOptions topts;
        topts.sampleRate = options.sampleRate;
        topts.blockSize = options.blockSize;
        topts.seed = report.seed;
        topts.seedOverride = options.seed;
        topts.pluginCache = options.pluginCache;
        ts.chain = buildTrackChain (c, *ts.spec, s.registry, s.tempo, s.busIndex, topts);
        if (const auto out = s.busIndex.find (ts.spec->output); out != s.busIndex.end())
            ts.outputBus = (int) out->second;
        report.warnings.insert (report.warnings.end(), ts.chain.warnings.begin(), ts.chain.warnings.end());
        s.lastNoteEnd = std::max (s.lastNoteEnd, ts.chain.content->lastNoteEndSec);
        ts.report.resolved = ts.chain.created.def->name;
        ts.report.description = ts.chain.created.instrument->description();
        ts.report.notes = ts.chain.content->notes;

        ts.pre.setSize (2, options.blockSize);
        ts.post.setSize (2, options.blockSize);
    }

    // 트랙 사이의 사이드체인 키를 압축기 자리의 소리에 맞춘다 (키 트랙보다 덜 늦은 트랙은 압축기 앞에서 소리를 늦춘다)
    std::vector<TrackChain*> chains (s.tracks.size(), nullptr);
    for (size_t ti = 0; ti < s.tracks.size(); ++ti)
        if (s.tracks[ti].report.audible)
            chains[ti] = &s.tracks[ti].chain;
    alignTrackSidechains (c, chains);
    for (auto& ts : s.tracks)
        if (ts.report.audible)
            ts.report.latencySamples = ts.chain.latency;
    for (const auto& ts : s.tracks)
        if (ts.report.audible)
            for (const auto& source : ts.chain.effects.sidechainSources())
                for (auto& key : s.tracks)
                    if (key.spec->name == source && key.report.audible && key.key.getNumSamples() == 0)
                        key.key.setSize (2, options.blockSize);

    // 버스와 마스터의 효과 사슬 (버스의 키는 버스 입력처럼 정렬되어 있다)
    for (auto& bs : s.buses)
    {
        bs.chain = createEffectChain (bs.spec->effects, c.sourceFile.getParentDirectory(), options.sampleRate, options.blockSize, &s.tempo);
        bs.chain.alignSidechains (0, [] (const std::string&) { return 0; });
    }
    s.masterChain = createEffectChain (c.master.effects, c.sourceFile.getParentDirectory(), options.sampleRate, options.blockSize, &s.tempo);
    s.masterChain.alignSidechains (0, [] (const std::string&) { return 0; });
}

OfflineRender::~OfflineRender() = default;

RenderReport OfflineRender::run()
{
    auto& s = *state_;
    if (s.ran)
        throw Failure ("render", "an offline render runs only once");
    s.ran = true;
    const auto& c = s.c;
    const auto& options = s.options;
    const auto& tempo = s.tempo;
    auto& report = s.report;
    auto& tracks = s.tracks;
    auto& buses = s.buses;
    const double sr = options.sampleRate;
    const int block = options.blockSize;
    setProgress (options, 0.0f);

    // ---- 길이: 마지막 음의 끝, 또는 컨트롤러 곡선의 마지막 점(페달을 떼는 시각 등) 중 늦은 쪽 ----
    const double endSec = songEndSeconds (c, tempo, s.lastNoteEnd, &report.warnings);
    const double totalSec = std::max (0.1, endSec + c.render.tailSeconds);
    const int64_t total = (int64_t) std::ceil (totalSec * sr);
    if (total > (int64_t) std::numeric_limits<int>::max())
        throw Failure ("invalid-input", "rendered length is too long");

    // ---- 내보낼 구간: 반복이나 박 범위 ----
    int64_t outStart = 0, outEnd = total;
    if (options.loop)
    {
        auto& lr = report.loop;
        lr.applied = true;
        lr.startBeat = c.loop->startBeat;
        lr.endBeat = c.loop->endBeat;
        lr.startSample = (int64_t) std::llround (tempo.secondsAt (lr.startBeat) * sr);
        lr.endSample = (int64_t) std::llround (tempo.secondsAt (lr.endBeat) * sr);
        if (lr.endSample > total)
            throw Failure ("invalid-input", "the loop ends after the song (" + formatNumber (lr.endSample / sr, 2) + " s, the song is "
                                                + formatNumber (total / sr, 2) + " s with its tail)");
        if (lr.endSample <= lr.startSample)
            throw Failure ("invalid-input", "the loop is shorter than one sample");
        outEnd = lr.endSample;
    }
    if (options.range)
    {
        auto& rr = report.range;
        rr.applied = true;
        rr.startBeat = options.range->first;
        rr.endBeat = options.range->second;
        rr.startSeconds = tempo.secondsAt (rr.startBeat);
        rr.endSeconds = std::min (tempo.secondsAt (rr.endBeat), (double) total / sr);
        outStart = (int64_t) std::llround (rr.startSeconds * sr);
        if (outStart >= total)
            throw Failure ("invalid-input", "the range starts after the song ends (" + formatNumber (total / sr, 2) + " s)");
        const int64_t endSample = std::min (total, (int64_t) std::llround (rr.endSeconds * sr));
        outEnd = std::min (total, endSample + (int64_t) std::llround (options.rangeFadeSeconds * sr));
        rr.fadeSeconds = (double) (outEnd - endSample) / sr;
    }

    // ---- 지연 보상: 모든 트랙을 가장 긴 지연에 맞춘다 ----
    int maxLatency = 0;
    for (const auto& ts : tracks)
        if (ts.report.audible)
            maxLatency = std::max (maxLatency, ts.chain.latency);
    for (auto& ts : tracks)
        if (ts.report.audible)
            ts.align.prepare (2, maxLatency - ts.chain.latency);

    // ---- 스템 ----
    if (options.stemsDir)
    {
        const auto dir = *options.stemsDir;
        if (! dir.isDirectory() && ! dir.createDirectory())
            throw Failure ("io", "cannot create stems directory " + dir.getFullPathName().toStdString());
        std::map<std::string, int> used;
        auto unique = [&] (std::string file)
        {
            if (used[file]++ == 0)
                return file;
            return file.substr (0, file.size() - 4) + "_" + std::to_string (used[file]) + ".wav";
        };
        used["master.wav"] = 1;
        const auto& only = options.stemTracks;
        for (auto& ts : tracks)
            if (ts.report.audible && (only.empty() || std::find (only.begin(), only.end(), ts.spec->name) != only.end()))
            {
                const auto f = dir.getChildFile (unique (stemFileName (ts.spec->name)));
                ts.stem = std::make_unique<StreamingWavWriter> (f, sr, 2, options.bitDepth);
                ts.report.stem = f.getFullPathName().toStdString();
            }
        for (auto& bs : buses)
        {
            if (! only.empty())
                break;
            const auto f = dir.getChildFile (unique ("bus-" + stemFileName (bs.spec->name)));
            bs.stem = std::make_unique<StreamingWavWriter> (f, sr, 2, options.bitDepth);
            bs.report.stem = f.getFullPathName().toStdString();
        }
    }

    // ---- 측정 ----
    if (options.measure)
    {
        for (auto& ts : tracks)
            if (ts.report.audible)
                ts.measure = std::make_unique<StemMeter> (sr);
        for (auto& bs : buses)
            bs.measure = std::make_unique<StemMeter> (sr);
    }

    log::info ("rendering " + std::to_string (c.tracks.size()) + " track(s), " + std::to_string (c.buses.size()) + " bus(es), "
               + formatNumber (totalSec, 2) + " s at " + std::to_string ((int) sr) + " Hz");

    juce::AudioBuffer<float> master (2, (int) total);
    master.clear();
    for (auto& bs : buses)
    {
        bs.input.setSize (2, (int) total);
        bs.input.clear();
    }
    juce::MidiBuffer midi;

    // ---- 사이드체인: 키 트랙을 먼저 처리한다. 버스의 키는 곡 전체 길이로 모아 둔다 ----
    const auto order = trackProcessOrder (c);
    std::map<std::string, size_t> trackIndex;
    for (size_t i = 0; i < tracks.size(); ++i)
        trackIndex[c.tracks[i].name] = i;
    juce::AudioBuffer<float> silence (2, block);
    silence.clear();
    std::map<size_t, juce::AudioBuffer<float>> busKeys;   // 트랙 번호 → 정렬된 프리 페이더 신호 (곡 전체, 버스와 마스터의 키)
    auto collectKeys = [&] (const std::vector<EffectSpec>& effects)
    {
        for (const auto& e : effects)
            if (e.type == "compressor")
                if (const auto v = e.params.getProperty ("sidechain", juce::var()); v.isString())
                    if (auto it = trackIndex.find (v.toString().toStdString()); it != trackIndex.end() && busKeys.count (it->second) == 0)
                    {
                        busKeys[it->second].setSize (2, (int) total);
                        busKeys[it->second].clear();
                    }
    };
    for (const auto& b : c.buses)
        collectKeys (b.effects);
    collectKeys (c.master.effects);

    // 반복 s 에서 정렬된 블록은 곡의 [s - maxLatency, s - maxLatency + n) 이다.
    const int64_t renderEnd = total + maxLatency;
    for (int64_t start = 0; start < renderEnd; start += block)
    {
        if (options.cancel != nullptr && options.cancel->load())
            throw Failure ("cancelled", "render was cancelled");
        setProgress (options, (float) (0.9 * (double) start / (double) renderEnd));
        const int n = (int) std::min<int64_t> (block, renderEnd - start);
        const int64_t dst = start - maxLatency;
        const int srcOffset = (int) std::max<int64_t> (0, -dst);
        const int count = (int) std::min<int64_t> (n - srcOffset, total - (dst + srcOffset));
        const bool valid = count > 0;

        for (const size_t ti : order)
        {
            auto& ts = tracks[ti];
            if (! ts.report.audible)
                continue;
            midi.clear();
            while (ts.cursor < ts.chain.content->events.size() && ts.chain.content->events[ts.cursor].sample < start + n)
            {
                const auto& e = ts.chain.content->events[ts.cursor++];
                midi.addEvent (e.message, (int) std::max<int64_t> (0, e.sample - start));
            }
            ts.chain.created.instrument->render (ts.pre, n, midi, start);
            ts.pre.applyGain (0, n, ts.chain.instrumentGain);
            ts.chain.content->expression.process (ts.pre, n, start - ts.chain.instrumentLatency);
            for (const auto& source : ts.chain.effects.sidechainSources())
            {
                // 키 트랙은 이 반복에서 이미 처리했다 (정렬 전의 프리 페이더 신호, 압축기가 자기 자리에 맞춘다). 뮤트된 키는 무음이다.
                const auto& key = tracks[trackIndex.at (source)];
                const auto& signal = key.report.audible ? key.key : silence;
                ts.chain.effects.setSidechainInput (source, signal.getReadPointer (0), signal.getReadPointer (1));
            }
            ts.chain.effects.process (ts.pre, n, start - ts.chain.instrumentLatency);
            if (ts.key.getNumSamples() > 0)
                for (int ch = 0; ch < 2; ++ch)
                    ts.key.copyFrom (ch, 0, ts.pre, ch, 0, n);
            ts.align.process (ts.pre, n);

            if (! valid)
                continue;
            if (auto key = busKeys.find (ti); key != busKeys.end())
                for (int ch = 0; ch < 2; ++ch)
                    key->second.copyFrom (ch, (int) (dst + srcOffset), ts.pre, ch, srcOffset, count);
            for (int ch = 0; ch < 2; ++ch)
                ts.post.copyFrom (ch, srcOffset, ts.pre, ch, srcOffset, count);
            if (ts.chain.faderLanes.empty())
                applyGains (ts.post, srcOffset, count, ts.chain.fader);
            else
                applyFaderLanes (ts.post, srcOffset, count, ts.chain.faderLanes, BlockPosition { dst + srcOffset, false });

            for (const auto& send : ts.chain.sends)
                addSend (buses[send.bus].input, (int) (dst + srcOffset), send.preFader ? ts.pre : ts.post, srcOffset, count, send,
                         BlockPosition { dst + srcOffset, false });
            auto& destination = ts.outputBus >= 0 ? buses[(size_t) ts.outputBus].input : master;   // 서브그룹 버스나 마스터
            for (int ch = 0; ch < 2; ++ch)
                destination.addFrom (ch, (int) (dst + srcOffset), ts.post, ch, srcOffset, count);
            ts.meter.add (ts.post, srcOffset, count);
            if (ts.measure)
                ts.measure->add (options.stemsPreFader ? ts.pre : ts.post, srcOffset, count);
            if (ts.stem)
                ts.stem->write (options.stemsPreFader ? ts.pre : ts.post, srcOffset, count);
        }

    }
    setProgress (options, 0.9f);

    // ---- 버스: 효과는 전체 버퍼로 (지연을 되돌린다), 페이더, 마스터로 ----
    for (auto& bs : buses)
    {
        auto& chain = bs.chain;
        SidechainKeys keys;
        for (const auto& source : chain.sidechainSources())
            if (auto it = busKeys.find (trackIndex.at (source)); it != busKeys.end())
                keys[source] = &it->second;
        processOffline (chain, bs.input, (int) total, block, &keys);
        if (const auto lanes = makeFaderLanes (bs.spec->mixer, tempo, sr); ! lanes.empty() && ! bs.spec->mixer.muted())
            applyFaderLanes (bs.input, 0, (int) total, lanes, BlockPosition { 0, false });
        else
            applyGains (bs.input, 0, (int) total, bs.fader);
        for (int ch = 0; ch < 2; ++ch)
            master.addFrom (ch, 0, bs.input, ch, 0, (int) total);
        bs.meter.add (bs.input, 0, (int) total);
        if (bs.measure)
            bs.measure->add (bs.input, 0, (int) total);
        if (bs.stem)
            bs.stem->write (bs.input, 0, (int) total);
        bs.input.setSize (0, 0);
    }
    if (options.cancel != nullptr && options.cancel->load())
        throw Failure ("cancelled", "render was cancelled");
    setProgress (options, 0.94f);

    // ---- 마스터: 페이더, 효과, 목표 LUFS ----
    if (const auto lanes = makeFaderLanes (c.master.mixer, tempo, sr); ! lanes.empty())
        applyFaderLanes (master, 0, (int) total, lanes, BlockPosition { 0, false });
    else
        applyGains (master, 0, (int) total, faderGains (c.master.mixer));
    auto& masterChain = s.masterChain;
    SidechainKeys masterKeys;
    for (const auto& source : masterChain.sidechainSources())
        if (auto it = busKeys.find (trackIndex.at (source)); it != busKeys.end())
            masterKeys[source] = &it->second;
    if (c.master.targetLufs)
    {
        // 끝의 리미터 앞까지 처리하고 재서 맞춘 뒤 리미터를 거친다. 리미터가 라우드니스를 깎으면 다시 맞춘다.
        auto limiter = masterChain.takeTrailingLimiter();
        processOffline (masterChain, master, (int) total, block, &masterKeys);
        auto& norm = report.normalisation;
        norm.applied = true;
        norm.targetLufs = *c.master.targetLufs;
        norm.limited = limiter != nullptr;
        auto measured = integratedLoudness (master, (int) total, sr);
        if (! measured)
            report.warnings.push_back ({ "$.master.targetLufs", "the mix is silent or shorter than 400 ms; loudness was not normalised", {} });
        else if (limiter == nullptr)
        {
            norm.gainDb = *c.master.targetLufs - *measured;
            master.applyGain (0, (int) total, (float) std::pow (10.0, norm.gainDb / 20.0));
            norm.passes = 1;
        }
        else
        {
            juce::AudioBuffer<float> preLimiter;
            preLimiter.makeCopyOf (master);
            EffectChain limiterChain;
            limiterChain.add (std::move (limiter));
            double gainDb = *c.master.targetLufs - *measured;
            for (int pass = 1; pass <= 4; ++pass)
            {
                master.makeCopyOf (preLimiter);
                master.applyGain (0, (int) total, (float) std::pow (10.0, gainDb / 20.0));
                limiterChain.prepare (sr, block);
                processOffline (limiterChain, master, (int) total, block);
                norm.gainDb = gainDb;
                norm.passes = pass;
                const auto after = integratedLoudness (master, (int) total, sr);
                if (! after || std::abs (*after - *c.master.targetLufs) <= 0.1)
                    break;
                gainDb += *c.master.targetLufs - *after;
            }
        }
    }
    else
        processOffline (masterChain, master, (int) total, block, &masterKeys);
    fadeOut (master, total, sr, c.render.fadeOutSeconds);
    if (options.measure)
    {
        StemMeter masterMeter (sr);
        masterMeter.add (master, 0, (int) total);
        report.measure = measureReport (c, tempo, sr, total, tracks, buses, masterMeter);
    }
    if (options.cancel != nullptr && options.cancel->load())
        throw Failure ("cancelled", "render was cancelled");
    setProgress (options, 0.97f);
    if (options.stemsDir && options.stemTracks.empty())
    {
        const auto f = options.stemsDir->getChildFile ("master.wav");
        writeWav (f, master, (int) total, sr, options.bitDepth);
        report.masterStem = f.getFullPathName().toStdString();
    }

    // ---- 자르기: 반복은 끝을 반복 시작 앞의 소리로 잇는다, 구간은 앞뒤를 줄인다 ----
    AudioFileTags tags;
    tags.title = c.title;
    if (report.loop.applied)
    {
        auto& lr = report.loop;
        const int64_t window = std::min<int64_t> ((int64_t) std::llround (0.5 * sr), lr.startSample);
        lr.seamWindowSeconds = (double) window / sr;
        std::tie (lr.seamDb, lr.seamLevelDb) = seamDifference (master, lr.startSample, lr.endSample, window);
        const int64_t n = std::min ({ (int64_t) std::llround (0.02 * sr), lr.endSample - lr.startSample, lr.startSample });
        for (int ch = 0; ch < master.getNumChannels(); ++ch)
        {
            float* x = master.getWritePointer (ch);
            for (int64_t i = 0; i < n; ++i)
            {
                const float w = (float) (i + 1) / (float) n;
                x[lr.endSample - n + i] = x[lr.endSample - n + i] * (1.0f - w) + x[lr.startSample - n + i] * w;
            }
        }
        lr.crossfadeSamples = (int) n;
        tags.loopStart = lr.startSample;
        tags.loopEnd = lr.endSample;
    }
    if (report.range.applied)
    {
        const int64_t fadeIn = outStart > 0 ? std::min<int64_t> ((int64_t) std::llround (0.01 * sr), outEnd - outStart) : 0;
        raisedCosineFade (master, outStart, fadeIn, true);
        const int64_t fadeStart = outEnd - (int64_t) std::llround (report.range.fadeSeconds * sr);
        raisedCosineFade (master, fadeStart, outEnd - fadeStart, false);
    }

    const juce::AudioBuffer<float> written (master.getArrayOfWritePointers(), master.getNumChannels(), (int) outStart, (int) (outEnd - outStart));
    if (options.output != juce::File())
        writeAudioFile (options.output, written, 0, written.getNumSamples(), sr, { options.fileType, options.bitDepth, report.quality }, tags);
    if (options.onAudio)
        options.onAudio (written, sr);

    report.stats = analyseAudio (written, written.getNumSamples(), sr, 1.0f);
    for (auto& ts : tracks)
    {
        if (ts.stem)
            ts.stem->close();
        ts.report.peakDb = ts.meter.peakDb();
        ts.report.rmsDb = ts.meter.rmsDb (total);
        report.tracks.push_back (ts.report);
    }
    for (auto& bs : buses)
    {
        if (bs.stem)
            bs.stem->close();
        bs.report.peakDb = bs.meter.peakDb();
        bs.report.rmsDb = bs.meter.rmsDb (total);
        report.buses.push_back (bs.report);
    }
    if (report.stats.clipping)
        report.warnings.push_back ({ "$", "the master clips (" + std::to_string (report.stats.clippedSamples)
                                     + " samples at or above 0 dBFS)", "lower track or master gainDb, or add a limiter (phase 5)" });

    report.renderSeconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - s.clockStart).count();
    log::info ("wrote " + report.output + " (" + formatNumber (report.stats.durationSeconds, 2) + " s, peak "
               + formatNumber (report.stats.peakDb, 1) + " dBFS) in " + formatNumber (report.renderSeconds, 2) + " s");
    setProgress (options, 1.0f);
    return report;
}

RenderReport renderComposition (const Composition& c, const RenderOptions& options)
{
    OfflineRender render (c, options);
    return render.run();
}

} // namespace mr
