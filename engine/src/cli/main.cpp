// music-render 명령행. 표준 출력에는 기계가 읽는 JSON 하나만 쓰고, 로그는 표준 오류로 보낸다.
// 종료 코드: 0 성공, 1 입력이나 렌더링 오류, 2 사용법 오류.

#include "analysis/Analysis.h"
#include "composition/Arrange.h"
#include "composition/Composition.h"
#include "composition/Document.h"
#include "composition/Groove.h"
#include "composition/Harmony.h"
#include "composition/Templates.h"
#include "core/Diagnostics.h"
#include "core/Log.h"
#include "instruments/Registry.h"
#include "io/AudioFile.h"
#include "midi/MidiExport.h"
#include "midi/MidiImport.h"
#include "media/Timecode.h"
#include "media/Video.h"
#include "notation/MusicXml.h"
#include "instruments/QuickSampler.h"
#include "analysis/NoteSegments.h"
#include "analysis/PitchTrack.h"
#include "analysis/Onsets.h"
#include "instruments/AudioClips.h"
#include "plugins/PluginHost.h"
#include "realtime/RealtimePlayer.h"
#include "rendering/EventBuilder.h"
#include "rendering/OfflineRenderer.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#ifndef MUSIC_RENDER_VERSION
 #define MUSIC_RENDER_VERSION "0.0.0"
#endif

namespace
{

using namespace mr;

constexpr int exitOk = 0, exitFailed = 1, exitUsage = 2;

const char* usageText = R"(music-render: structured music data to audio

usage:
  music-render validate <song.json> [--registry <file>]... [--strict]
  music-render render <song.json> --output <out.wav|.flac|.ogg|.mp3> [--sample-rate 48000] [--bit-depth 24] [--quality <n>]
                [--loop | --bars <a-b> | --beats <a-b> [--fade 2]]
                [--stems <dir>] [--measure <file.json>] [--seed <n>] [--block-size 512] [--registry <file>]... [--report <file.json>]
  music-render render <song.json> --output <scored.mp4|.mov|.m4v> [--video <movie>] [--video-offset <s>] [--video-sound drop|mix]
                [--sample-rate 48000] [--seed <n>] [--report <file.json>]   # the music on the video (the picture is copied, not re-encoded)
  music-render inspect <audio file>
  music-render video-info <movie>                                              # length, frame rate, size, codec, sound
  music-render video-frame <movie> --at <seconds> --output <frame.png> [--width <px>] [--height <px>]
  music-render video-scenes <movie> [--threshold 0.2] [--ratio 3] [--min-gap 0.4] [--song <song.json>]   # scene cuts (marker candidates)
  music-render video-audio <movie> --output <sound.wav> [--sample-rate <hz>]   # the video's sound as an audio file
  music-render video-pattern --output <movie.mp4> [--fps 30] [--seconds 10] [--size 640x360] [--scenes 2.5,5] [--clicks 1,2] [--rotate 1]
                # a test video: each frame shows its number, scenes change at the given seconds, clicks in the sound
  music-render export-midi <song.json> --output <song.mid> [--humanized] [--seed <n>]
  music-render export-musicxml <song.json> --output <song.musicxml> [--tracks <list>]   # score for notation programs
  music-render sampler <audio> --output <instrument.sfz> [--root <note>] [--one-shot]   # an SFZ instrument from one sound
  music-render transcribe <audio> --output <song.json> [--bpm 120] [--instrument grand-piano] [--from <s>] [--seconds <s>]   # a sung or played melody as notes
  music-render notes <audio> [--from <s>] [--seconds <s>]                        # the notes of a sung or played line (pitch, drift, vibrato)
  music-render tempo <audio> [--from <s>] [--seconds <s>]                        # the tempo of a loop or a recording
  music-render beats <audio> [--from <s>] [--seconds <s>]                        # every beat of a performance whose tempo moves
  music-render slice <audio> --output <kit.sfz> [--root C2] [--from <s>] [--seconds <s>]   # one key per attack
  music-render groove list | extract <song.json> --track <t> [--bars a-b] | extract-audio <audio> --bpm <n> [--from <s>]
                [--grid 1/16] [--length <beats>] --output <groove.json>   # a groove's timing and accents
  music-render track-template list | save <song.json> <track> --output <template.json> [--name <text>]   # a track's sound and chain
  music-render import-midi <song.mid> --output <song.json> [--title <text>] [--instrument <default>] [--force]
  music-render arrange <song.json> <operation> [--output <out.json>]   # edits the song in place unless --output
                insert-bars --at <bar> --count <n> | delete-bars --bars <a-b>
                duplicate --bars <a-b> [--tracks <list>] | copy --bars <a-b> --to <bar> [--tracks <list>] | clear --bars <a-b> [--tracks <list>]
                add-track --name <text> --instrument <name> [--at <n>] | add-track --template <id|template.json> [--name <text>] [--at <n>]
                duplicate-track <track> | remove-track <track>
                rename-track <track> --name <text> | move-track <track> --to <n> | add-marker --at <bar> --name <text> | remove-marker <name>
                move-section <section> --before <section|end> | duplicate-section <section> | delete-section <section>   # a section runs from a marker to the next
                quantize-audio <track> --grid 1/16 [--strength 100] [--swing 0] [--mode stretch|slice] [--clip <n>] | unwarp <track> [--clip <n>]
                groove <track> --groove <id|groove.json> [--timing 100] [--velocity 100]
                follow-tempo <track> [--clip <n>]   # the song's tempo map follows a recorded performance
                tune <track> [--strength 100] [--speed 30] [--key "A minor" | --scale dorian --root D] [--no-formant] [--clip <n>] | untune <track> [--clip <n>]
                pitch-edit <track> [--clip <n>] --note <k> [--semitones <x> | --to <note>] [--flatten 0-100] [--vibrato 0-200] [--clear]
                reverse <track> [--clip <n>] | fade <track> [--clip <n>] [--in <s>] [--out <s>] [--in-shape <shape>] [--out-shape <shape>]
                crossfade <track> [--clip <n>] [--ms 20]
  music-render format <song.json> [--output <out.json>] [--check]   # rewrites the song in the save format (one note per line)
  music-render realtime <song.json> [--seconds 20] [--from 0] [--block-size 256] [--output <out.wav>] [--no-pace]
  music-render instruments [--registry <file>]...
  music-render chords | scales             # chord and scale tables (intervals in semitones from the root)
  music-render templates [--templates <dir>]
  music-render new <template> --output <song.json> [--title <text>] [--templates <dir>]
  music-render plugins scan [<path>...] [--format vst3|au] [--cache <file>]
  music-render plugins describe <plugin> [--format vst3|au] [--name <plugin name>]
  music-render plugins save-state <plugin> --output <state.bin> [--set <param>=<value>]... [--program <n>]
  music-render version

render output:
  the extension picks the format. .mp4, .mov and .m4v put the music on a video (--video, or the song's "video"):
  the picture is copied as it is, the sound is AAC, the song's second (t + offset) plays at the video's second t, and the
  video's own sound is dropped unless --video-sound mix. The output is as long as the video. --bit-depth: WAV 16, 24, 32 (float), FLAC 16, 24. --quality: Ogg Vorbis 0-10
  (default 6), MP3 kbps (default 320). --loop writes from the start to metadata.loop's end with the loop marked
  (WAV smpl chunk, Ogg and FLAC LOOPSTART and LOOPLENGTH). --bars 17-32 (or --beats 64-128) writes that range
  and --fade seconds after it, fading out. Stems are always the whole song as WAV.

arrange:
  bars count from 1 and ranges include both ends (17-24 is eight bars). <track> is a number from 1 or a name;
  --tracks takes 2-4 or Lead,Bass. Copying overwrites the bars it lands on; curves keep their values outside the range.
  Inserting and deleting bars moves every track, tempo point, marker, the loop and render.endBeat.
  A section runs from a marker to the next one (the last to the song's end) and is named by its marker or numbered from 1.
  Moving, duplicating and deleting sections carries notes, clips, curves, tempo, meter and key changes and markers with them,
  and sets each section's meter, key and tempo at its new start. Section edges must be bar lines.
  quantize-audio finds the attacks in an audio track's clips and writes warp markers that pull them to the grid
  (1/16, 1/8t, or beats); --mode slice cuts at the markers and plays each piece unstretched (drums keep their attacks).
  unwarp removes the markers. tune pulls the measured pitch of an audio track's clips to the nearest semitone (or note of
  the key or scale); --speed 0 is instant (flattens vibrato), longer keeps the singer's slides and vibrato. untune removes it.
  pitch-edit changes one note of an audio clip (notes are counted from 1 among the file's notes whose middle lies in the
  part the clip plays; music-render notes lists a whole file's notes): --semitones moves it (fractions allowed), --to puts its centre on a note, --flatten pulls
  the slow drift to the centre, --vibrato scales the fast movement (100 keeps it, 0 removes it). --clear removes the
  note's edit, or every edit of the clip without --note.
  reverse plays a clip backwards (or forwards again) in the same place: the clip's file seconds then count from the
  file's end. fade sets fade lengths and shapes (sine, linear, equalPower, exponential). crossfade fades a clip into
  the next one over their overlap; clips that only touch are lengthened into each other by --ms (half on each side)
  from the sound in their files. Two parts of the same continuous sound get sine fades (amplitudes add up to one),
  different sounds equal-power fades.
  groove moves a track's notes (or an audio track's attacks, as warp markers) to a groove's places and scales velocities
  by its accents (--timing and --velocity are percentages).
  follow-tempo finds every beat of an audio clip played without a click and writes a tempo point per beat, so the song's
  bars follow the performance; the clip plays unstretched with its first beat on a bar line.

options for every command:
  --quiet | --verbose        log level (logs go to stderr, JSON results to stdout)
  --no-default-registry      do not read the built-in engine/instruments/registry.json

exit codes: 0 ok, 1 invalid input or render failure, 2 usage error
)";

struct Args
{
    std::string command;
    std::vector<std::string> positional;
    std::map<std::string, std::vector<std::string>> options;
    std::vector<std::string> flags;

    bool flag (const std::string& name) const { return std::find (flags.begin(), flags.end(), name) != flags.end(); }
    std::optional<std::string> value (const std::string& name) const
    {
        auto it = options.find (name);
        if (it == options.end() || it->second.empty())
            return std::nullopt;
        return it->second.back();
    }
    std::vector<std::string> values (const std::string& name) const
    {
        auto it = options.find (name);
        return it == options.end() ? std::vector<std::string> {} : it->second;
    }
};

class UsageError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

const std::vector<std::string> valueOptions { "--output", "--root", "--sample-rate", "--bit-depth", "--seed", "--block-size", "--registry", "--report",
                                              "--format", "--cache", "--set", "--program", "--name", "--stems", "--measure", "--seconds", "--from",
                                              "--quality", "--bars", "--beats", "--fade", "--templates", "--title",
                                              "--instrument", "--at", "--to", "--tracks", "--count", "--bpm", "--grid", "--strength", "--swing", "--clip", "--mode", "--speed", "--key", "--scale",
                                              "--groove", "--timing", "--velocity", "--length", "--track", "--template",
                                              "--video", "--video-offset", "--video-sound", "--width", "--height", "--threshold", "--ratio", "--min-gap",
                                              "--song", "--fps", "--size", "--scenes", "--clicks", "--rotate", "--before",
                                              "--note", "--semitones", "--flatten", "--vibrato", "--in", "--out", "--in-shape", "--out-shape", "--ms" };
const std::vector<std::string> flagOptions { "--quiet", "--verbose", "--strict", "--humanized", "--no-default-registry", "--no-pace", "--help",
                                             "--loop", "--force", "--one-shot", "--no-formant", "--check", "--clear" };

Args parseArgs (int argc, char** argv)
{
    Args a;
    for (int i = 1; i < argc; ++i)
    {
        std::string s = argv[i];
        if (s.rfind ("--", 0) == 0)
        {
            std::string name = s, value;
            bool inlineValue = false;
            if (auto eq = s.find ('='); eq != std::string::npos)
            {
                name = s.substr (0, eq);
                value = s.substr (eq + 1);
                inlineValue = true;
            }
            if (std::find (valueOptions.begin(), valueOptions.end(), name) != valueOptions.end())
            {
                if (! inlineValue)
                {
                    if (i + 1 >= argc)
                        throw UsageError ("option " + name + " needs a value");
                    value = argv[++i];
                }
                a.options[name].push_back (value);
            }
            else if (std::find (flagOptions.begin(), flagOptions.end(), name) != flagOptions.end())
                a.flags.push_back (name);
            else
            {
                std::vector<std::string> all = valueOptions;
                all.insert (all.end(), flagOptions.begin(), flagOptions.end());
                const auto s2 = closestMatch (name, all);
                throw UsageError ("unknown option " + name + (s2.empty() ? std::string {} : " (did you mean " + s2 + "?)"));
            }
        }
        else if (a.command.empty())
            a.command = s;
        else
            a.positional.push_back (s);
    }
    return a;
}

// ---- JSON 출력 -------------------------------------------------------------

juce::var number (double x, int decimals = 4)
{
    if (! std::isfinite (x) || x <= -1.0e8)
        return juce::var();
    const double scale = std::pow (10.0, decimals);
    return juce::var (std::round (x * scale) / scale);
}

juce::var diagnosticsArray (const std::vector<Diagnostic>& list)
{
    juce::Array<juce::var> arr;
    for (const auto& d : list)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("path", juce::String (d.path));
        o->setProperty ("message", juce::String (d.message));
        if (! d.hint.empty())
            o->setProperty ("hint", juce::String (d.hint));
        arr.add (juce::var (o));
    }
    return arr;
}

juce::var statsObject (const AudioStats& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("duration", number (s.durationSeconds, 4));
    o->setProperty ("samples", juce::var ((juce::int64) s.samples));
    o->setProperty ("sampleRate", number (s.sampleRate, 0));
    o->setProperty ("channels", s.channels);
    o->setProperty ("peakDb", number (s.peakDb, 2));
    o->setProperty ("truePeakDb", number (s.truePeakDb, 2));
    o->setProperty ("rmsDb", number (s.rmsDb, 2));
    o->setProperty ("lufs", s.lufs ? number (*s.lufs, 2) : juce::var());
    o->setProperty ("clipping", s.clipping);
    o->setProperty ("clippedSamples", juce::var ((juce::int64) s.clippedSamples));
    return juce::var (o);
}

void emit (const juce::var& result)
{
    std::cout << juce::JSON::toString (result, false, 6).toStdString() << std::endl;
}

juce::var failureObject (const std::string& command, const std::string& code, const std::string& message, const std::vector<Diagnostic>& details)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", false);
    o->setProperty ("command", juce::String (command));
    auto* e = new juce::DynamicObject();
    e->setProperty ("code", juce::String (code));
    e->setProperty ("message", juce::String (message));
    e->setProperty ("details", diagnosticsArray (details));
    o->setProperty ("error", juce::var (e));
    return juce::var (o);
}

juce::File fileArg (const std::string& path)
{
    return juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (path.c_str()));
}

std::optional<uint64_t> seedArg (const Args& a)
{
    auto v = a.value ("--seed");
    if (! v)
        return std::nullopt;
    if (v->empty() || v->size() > 19 || ! std::all_of (v->begin(), v->end(), [] (char ch) { return std::isdigit ((unsigned char) ch); }))
        throw UsageError ("--seed must be a non-negative integer");
    return (uint64_t) std::stoull (*v);
}

RenderOptions renderOptions (const Args& a)
{
    RenderOptions o;
    for (const auto& r : a.values ("--registry"))
        o.registries.push_back (fileArg (r));
    o.useDefaultRegistry = ! a.flag ("--no-default-registry");
    o.seed = seedArg (a);
    if (auto cache = a.value ("--cache"))
        o.pluginCache = fileArg (*cache);
    if (auto v = a.value ("--sample-rate"))
    {
        try { o.sampleRate = std::stod (*v); }
        catch (...) { throw UsageError ("--sample-rate must be a number"); }
    }
    if (auto v = a.value ("--bit-depth"))
    {
        try { o.bitDepth = std::stoi (*v); }
        catch (...) { throw UsageError ("--bit-depth must be 16, 24 or 32"); }
        if (o.bitDepth != 16 && o.bitDepth != 24 && o.bitDepth != 32)
            throw UsageError ("--bit-depth must be 16, 24 or 32");
    }
    if (auto v = a.value ("--block-size"))
    {
        try { o.blockSize = std::stoi (*v); }
        catch (...) { throw UsageError ("--block-size must be an integer"); }
    }
    if (auto v = a.value ("--quality"))
    {
        try { o.quality = std::stoi (*v); }
        catch (...) { throw UsageError ("--quality must be an integer"); }
    }
    return o;
}

std::optional<double> numberValue (const Args& a, const std::string& name)
{
    if (auto v = a.value (name))
    {
        try { return std::stod (*v); }
        catch (...) { throw UsageError (name + " must be a number"); }
    }
    return std::nullopt;
}

// "17-32" 를 두 수로
std::pair<double, double> rangeArg (const std::string& name, const std::string& text)
{
    const auto dash = text.find ('-', 1);
    try
    {
        if (dash == std::string::npos)
            throw std::invalid_argument ("no dash");
        size_t used = 0;
        const double a = std::stod (text.substr (0, dash), &used);
        const auto rest = text.substr (dash + 1);
        size_t usedB = 0;
        const double b = std::stod (rest, &usedB);
        if (used != dash || usedB != rest.size())
            throw std::invalid_argument ("trailing text");
        return { a, b };
    }
    catch (...)
    {
        throw UsageError (name + " must look like 17-32");
    }
}

Composition loadOrThrow (const std::string& path, Diagnostics* warningsOut = nullptr)
{
    auto parsed = loadComposition (fileArg (path));
    if (! parsed.composition)
        throw Failure ("invalid-input", "composition has " + std::to_string (parsed.diagnostics.errors.size()) + " error(s)",
                       parsed.diagnostics.errors);
    if (warningsOut != nullptr)
        warningsOut->append (parsed.diagnostics);
    return std::move (*parsed.composition);
}

// ---- 명령 -----------------------------------------------------------------

int cmdValidate (const Args& a)
{
    if (a.positional.size() != 1)
        throw UsageError ("validate needs exactly one composition file");
    auto parsed = loadComposition (fileArg (a.positional[0]));
    Diagnostics d = parsed.diagnostics;

    auto* o = new juce::DynamicObject();
    o->setProperty ("command", "validate");
    o->setProperty ("file", juce::String (fileArg (a.positional[0]).getFullPathName()));
    if (parsed.partial)
    {
        const auto& c = *parsed.partial;
        auto options = renderOptions (a);
        Registry registry = buildRegistry (c, options, d);
        for (const auto& t : c.tracks)
        {
            if (t.instrument.empty())
                continue;
            const auto* def = registry.find (t.instrument);
            if (def == nullptr)
            {
                const auto s = closestMatch (t.instrument, registry.names());
                d.error (t.path + ".instrument", "unknown instrument '" + t.instrument + "'",
                         s.empty() ? "run `music-render instruments` to list them" : "did you mean '" + s + "'?");
                continue;
            }
            checkTrackAgainstInstrument (t, *def, d);
        }
        size_t notes = 0;
        juce::Array<juce::var> instruments;
        for (const auto& t : c.tracks)
        {
            notes += t.notes.size();
            if (! instruments.contains (juce::String (t.instrument)))
                instruments.add (juce::String (t.instrument));
        }
        juce::var videoSummary;
        if (c.video)
        {
            const auto file = videoFileOf (c);
            std::string error;
            if (! file.existsAsFile())
                d.warning ("$.video.file", "video file not found: " + file.getFullPathName().toStdString(), "the path is relative to the song's folder");
            else if (const auto info = media::probeVideo (file.getFullPathName().toStdString(), error))
            {
                auto* vo = new juce::DynamicObject();
                vo->setProperty ("file", file.getFullPathName());
                vo->setProperty ("durationSeconds", number (info->durationSeconds, 3));
                vo->setProperty ("offsetSeconds", number (c.video->offsetSeconds, 4));
                videoSummary = juce::var (vo);
            }
            else
                d.warning ("$.video.file", "cannot open the video: " + error);
        }
        auto* summary = new juce::DynamicObject();
        summary->setProperty ("title", juce::String (c.title));
        if (! videoSummary.isVoid())
            summary->setProperty ("video", videoSummary);
        summary->setProperty ("tracks", (int) c.tracks.size());
        summary->setProperty ("notes", (int) notes);
        summary->setProperty ("instruments", instruments);
        summary->setProperty ("estimatedDuration", number (estimateDurationSeconds (c), 3));
        o->setProperty ("summary", juce::var (summary));
    }
    const bool strictFail = a.flag ("--strict") && ! d.warnings.empty();
    const bool ok = d.ok() && ! strictFail;
    o->setProperty ("ok", ok);
    o->setProperty ("errors", diagnosticsArray (d.errors));
    o->setProperty ("warnings", diagnosticsArray (d.warnings));
    emit (juce::var (o));
    return ok ? exitOk : exitFailed;
}

// 실시간 재생기를 오디오 콜백처럼 블록마다 부르고(기본은 실제 시간에 맞춰), 처리 시간과 출력을 잰다.
int cmdRealtime (const Args& a)
{
    if (a.positional.size() != 1)
        throw UsageError ("realtime needs exactly one composition file");
    auto numberArg = [&] (const std::string& name, double fallback)
    {
        if (auto v = a.value (name))
        {
            try { return std::stod (*v); }
            catch (...) { throw UsageError (name + " must be a number"); }
        }
        return fallback;
    };
    const auto c = loadOrThrow (a.positional[0]);
    const auto ro = renderOptions (a);
    PlayerOptions po;
    po.sampleRate = ro.sampleRate;
    po.blockSize = 512;
    po.registries = ro.registries;
    po.useDefaultRegistry = ro.useDefaultRegistry;
    po.seed = ro.seed;
    po.pluginCache = ro.pluginCache;
    const int callback = a.value ("--block-size") ? ro.blockSize : 256;
    if (callback < 16 || callback > 4096)
        throw UsageError ("--block-size must be between 16 and 4096");
    const bool pace = ! a.flag ("--no-pace");

    const auto buildStart = std::chrono::steady_clock::now();
    RealtimePlayer player (c, po);
    const double buildSeconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - buildStart).count();
    const double from = std::clamp (numberArg ("--from", 0.0), 0.0, player.lengthSeconds());
    const double seconds = std::clamp (numberArg ("--seconds", 20.0), 0.1, player.lengthSeconds() - from + 0.001);
    player.seekSeconds (from);
    player.play();

    const int total = (int) std::llround (seconds * po.sampleRate);
    juce::AudioBuffer<float> out (2, total);
    out.clear();
    const double blockSeconds = callback / po.sampleRate;
    double loadSum = 0.0, maxLoad = 0.0;
    int blocks = 0, late = 0;
    juce::Array<juce::var> lateTimes;   // 늦은 블록의 곡 위치(초)와 부하, 앞의 30개
    auto deadline = std::chrono::steady_clock::now();
    for (int done = 0; done < total; done += callback)
    {
        const int n = std::min (callback, total - done);
        float* ptrs[2] = { out.getWritePointer (0, done), out.getWritePointer (1, done) };
        const auto t0 = std::chrono::steady_clock::now();
        player.process (ptrs, 2, n);
        const double used = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const double load = used / (n / po.sampleRate);
        loadSum += load;
        maxLoad = std::max (maxLoad, load);
        if (load > 1.0)
        {
            ++late;
            if (lateTimes.size() < 30)
                lateTimes.add (juce::var (juce::String (from + done / po.sampleRate, 3) + " s x" + juce::String (load, 1)));
        }
        ++blocks;
        if (pace)
        {
            deadline += std::chrono::duration_cast<std::chrono::steady_clock::duration> (std::chrono::duration<double> (blockSeconds));
            std::this_thread::sleep_until (deadline);
        }
    }
    const auto stats = analyseAudio (out, total, po.sampleRate, 1.0f);

    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", true);
    o->setProperty ("command", "realtime");
    o->setProperty ("from", number (from, 3));
    o->setProperty ("seconds", number (seconds, 3));
    o->setProperty ("blockSize", callback);
    o->setProperty ("paced", pace);
    o->setProperty ("buildSeconds", number (buildSeconds, 3));
    o->setProperty ("blocks", blocks);
    o->setProperty ("averageLoad", number (loadSum / std::max (1, blocks), 4));
    o->setProperty ("maxLoad", number (maxLoad, 4));
    o->setProperty ("lateBlocks", late);
    o->setProperty ("late", lateTimes);
    o->setProperty ("output", statsObject (stats));
    juce::Array<juce::var> tracks;
    for (size_t i = 0; i < player.trackCount(); ++i)
    {
        auto* t = new juce::DynamicObject();
        t->setProperty ("name", juce::String (player.trackInfo (i).name));
        t->setProperty ("engine", juce::String (player.trackInfo (i).description));
        tracks.add (juce::var (t));
    }
    o->setProperty ("tracks", tracks);
    if (auto path = a.value ("--output"))
    {
        const auto f = fileArg (*path);
        writeWav (f, out, total, po.sampleRate, ro.bitDepth);
        o->setProperty ("file", f.getFullPathName());
    }
    o->setProperty ("warnings", diagnosticsArray (player.warnings()));
    emit (juce::var (o));
    return exitOk;
}

// 마디마다의 크기와 대역, 음표 머리의 크기 (dB, 무음은 -120). 스템 파일 없이 크기를 견줄 때 쓴다 (tools/mix-report.py)
juce::var measureObject (const MeasureReport& m, const Composition& c)
{
    auto round2 = [] (double v) { return juce::var (std::round (v * 100.0) / 100.0); };
    auto* o = new juce::DynamicObject();
    o->setProperty ("title", juce::String (c.title));
    o->setProperty ("sampleRate", m.sampleRate);
    juce::Array<juce::var> edges;
    for (double e : m.bandEdges)
        edges.add (round2 (e));
    o->setProperty ("bandEdgesHz", edges);
    juce::Array<juce::var> beats, seconds;
    for (size_t i = 0; i < m.barStartBeats.size(); ++i)
    {
        beats.add (juce::var (std::round (m.barStartBeats[i] * 10000.0) / 10000.0));
        seconds.add (juce::var (std::round (m.barStartSeconds[i] * 10000.0) / 10000.0));
    }
    o->setProperty ("barStartBeats", beats);
    o->setProperty ("barStartSeconds", seconds);
    juce::Array<juce::var> stems;
    for (const auto& st : m.stems)
    {
        auto* so = new juce::DynamicObject();
        so->setProperty ("name", juce::String (st.name));
        so->setProperty ("kind", juce::String (st.kind));
        juce::Array<juce::var> bar, side, bands;
        for (size_t i = 0; i < st.barDb.size(); ++i)
        {
            bar.add (round2 (st.barDb[i]));
            side.add (round2 (st.barSideDb[i]));
            juce::Array<juce::var> b;
            for (double v : st.barBandDb[i])
                b.add (round2 (v));
            bands.add (b);
        }
        so->setProperty ("barDb", bar);
        so->setProperty ("barSideDb", side);
        so->setProperty ("barBandDb", bands);
        if (st.kind == "track")
        {
            juce::Array<juce::var> notes;
            for (const auto& n : st.notes)
            {
                juce::Array<juce::var> row;
                row.add (juce::var (std::round (n.beat * 1000.0) / 1000.0));
                row.add (n.pitch);
                row.add (juce::var (std::round (n.seconds * 1000.0) / 1000.0));
                row.add (round2 (n.attackDb));
                row.add (round2 (n.earlyDb));
                row.add (round2 (n.lateDb));
                notes.add (row);
            }
            so->setProperty ("notes", notes);   // [박, 음높이, 길이 (초), 머리 0.03~0.25 초, 0.05~0.1 초, 0.25~0.35 초]
        }
        stems.add (juce::var (so));
    }
    o->setProperty ("stems", stems);
    return juce::var (o);
}

int cmdRender (const Args& a)
{
    if (a.positional.size() != 1)
        throw UsageError ("render needs exactly one composition file");
    auto out = a.value ("--output");
    if (! out)
        throw UsageError ("render needs --output <file.wav|.flac|.ogg|.mp3>");
    Diagnostics parseWarnings;
    const auto c = loadOrThrow (a.positional[0], &parseWarnings);
    auto options = renderOptions (a);
    options.output = fileArg (*out);
    const bool movie = media::isVideoFileName (*out);
    juce::File videoSource;
    double videoOffset = 0.0;
    std::optional<media::ScoreVideoReport> scored;
    media::ScoreVideoOptions scoreOptions;
    if (movie)
    {
        if (a.flag ("--loop") || a.value ("--bars") || a.value ("--beats") || a.value ("--stems"))
            throw UsageError ("a scored video follows the video's own length; --loop, --bars, --beats and --stems do not apply");
        if (auto v = a.value ("--video"))
            videoSource = fileArg (*v);
        else
            videoSource = videoFileOf (c);
        if (videoSource == juce::File())
            throw UsageError ("a .mp4, .mov or .m4v output needs --video <movie> or the song's \"video\"");
        if (! a.value ("--video") && c.video)
            videoOffset = c.video->offsetSeconds;
        if (const auto v = numberValue (a, "--video-offset"))
            videoOffset = *v;
        const auto sound = a.value ("--video-sound").value_or ("drop");
        if (sound != "drop" && sound != "mix")
            throw UsageError ("--video-sound takes drop or mix");
        scoreOptions.mixOriginal = sound == "mix";
        const auto movieFile = options.output;
        options.output = juce::File();
        options.onAudio = [&, movieFile] (const juce::AudioBuffer<float>& audio, double sampleRate)
        {
            const media::ScoreAudio music { audio.getArrayOfReadPointers(), audio.getNumChannels(), audio.getNumSamples(), sampleRate, videoOffset };
            std::string error;
            scored = media::writeScoredVideo (videoSource.getFullPathName().toStdString(), music, movieFile.getFullPathName().toStdString(),
                                              scoreOptions, error);
            if (! scored)
                throw Failure ("io", error);
        };
    }
    const auto type = movie ? std::optional<AudioFileType> (AudioFileType::wav) : audioFileTypeFor (options.output);
    if (! type)
        throw UsageError ("--output must end in .wav, .flac, .ogg, .mp3, .mp4, .mov or .m4v");
    options.fileType = *type;
    if (*type == AudioFileType::flac && options.bitDepth == 32)
        throw UsageError ("FLAC holds 16 or 24 bits");
    if (auto stems = a.value ("--stems"))
        options.stemsDir = fileArg (*stems);
    const auto measurePath = a.value ("--measure");
    options.measure = measurePath.has_value();
    options.loop = a.flag ("--loop");
    const auto bars = a.value ("--bars"), beats = a.value ("--beats");
    if ((bars && beats) || (options.loop && (bars || beats)))
        throw UsageError ("use only one of --loop, --bars and --beats");
    if (bars)
    {
        const auto [first, last] = rangeArg ("--bars", *bars);
        if (first < 1.0 || last < first || first != std::floor (first) || last != std::floor (last))
            throw UsageError ("--bars counts whole bars from 1, e.g. 17-32");
        const auto meters = c.meters();
        options.range = std::make_pair (meters.barStart ((int) first - 1), meters.barStart ((int) last));
    }
    if (beats)
    {
        const auto range = rangeArg ("--beats", *beats);
        if (range.first < 0.0 || range.second <= range.first)
            throw UsageError ("--beats must start at 0 or later and end after it starts");
        options.range = range;
    }
    if (auto v = a.value ("--fade"))
    {
        if (! options.range)
            throw UsageError ("--fade goes with --bars or --beats");
        try { options.rangeFadeSeconds = std::stod (*v); }
        catch (...) { throw UsageError ("--fade must be a number of seconds"); }
    }

    auto report = renderComposition (c, options);

    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", true);
    o->setProperty ("command", "render");
    if (scored)
    {
        o->setProperty ("output", fileArg (*out).getFullPathName());
        o->setProperty ("format", juce::String (scored->fileType));
        auto* vo = new juce::DynamicObject();
        vo->setProperty ("source", videoSource.getFullPathName());
        vo->setProperty ("offsetSeconds", number (videoOffset, 4));
        vo->setProperty ("durationSeconds", number (scored->durationSeconds, 4));
        vo->setProperty ("videoSamples", juce::var ((juce::int64) scored->videoSamples));
        vo->setProperty ("musicSeconds", number (scored->musicSeconds, 4));
        vo->setProperty ("originalSound", scored->mixedOriginal ? "mix" : "drop");
        vo->setProperty ("aacBitrate", scoreOptions.aacBitrate);
        o->setProperty ("video", juce::var (vo));
    }
    else
    {
        o->setProperty ("output", juce::String (report.output));
        o->setProperty ("format", juce::String (report.format));
        if (options.fileType == AudioFileType::wav || options.fileType == AudioFileType::flac)
            o->setProperty ("bitDepth", report.bitDepth);
        else
            o->setProperty ("quality", report.quality);
    }
    if (report.loop.applied)
    {
        const auto& l = report.loop;
        auto* lo = new juce::DynamicObject();
        lo->setProperty ("startBeat", number (l.startBeat, 4));
        lo->setProperty ("endBeat", number (l.endBeat, 4));
        lo->setProperty ("startSample", juce::var ((juce::int64) l.startSample));
        lo->setProperty ("endSample", juce::var ((juce::int64) l.endSample));
        lo->setProperty ("lengthSamples", juce::var ((juce::int64) (l.endSample - l.startSample)));
        lo->setProperty ("crossfadeSamples", l.crossfadeSamples);
        lo->setProperty ("seamWindowSeconds", number (l.seamWindowSeconds, 3));
        lo->setProperty ("seamDb", ! l.seamDb ? juce::var() : *l.seamDb <= -1.0e8 ? juce::var ("-inf") : number (*l.seamDb, 1));
        lo->setProperty ("seamLevelDb", l.seamLevelDb ? number (*l.seamLevelDb, 1) : juce::var());
        o->setProperty ("loop", juce::var (lo));
    }
    if (report.range.applied)
    {
        const auto& r = report.range;
        auto* ro = new juce::DynamicObject();
        ro->setProperty ("startBeat", number (r.startBeat, 4));
        ro->setProperty ("endBeat", number (r.endBeat, 4));
        ro->setProperty ("startSeconds", number (r.startSeconds, 4));
        ro->setProperty ("endSeconds", number (r.endSeconds, 4));
        ro->setProperty ("fadeSeconds", number (r.fadeSeconds, 4));
        o->setProperty ("range", juce::var (ro));
    }
    o->setProperty ("seed", juce::String (std::to_string (report.seed)));
    o->setProperty ("master", statsObject (report.stats));
    juce::Array<juce::var> tracks;
    for (const auto& t : report.tracks)
    {
        auto* to = new juce::DynamicObject();
        to->setProperty ("name", juce::String (t.name));
        to->setProperty ("instrument", juce::String (t.instrument));
        to->setProperty ("resolved", juce::String (t.resolved));
        to->setProperty ("engine", juce::String (t.description));
        to->setProperty ("notes", (int) t.notes);
        to->setProperty ("latencySamples", t.latencySamples);
        to->setProperty ("audible", t.audible);
        if (! t.stem.empty())
            to->setProperty ("stem", juce::String (t.stem));
        to->setProperty ("peakDb", number (t.peakDb, 2));
        to->setProperty ("rmsDb", number (t.rmsDb, 2));
        tracks.add (juce::var (to));
    }
    o->setProperty ("tracks", tracks);
    juce::Array<juce::var> buses;
    for (const auto& b : report.buses)
    {
        auto* bo = new juce::DynamicObject();
        bo->setProperty ("name", juce::String (b.name));
        bo->setProperty ("peakDb", number (b.peakDb, 2));
        bo->setProperty ("rmsDb", number (b.rmsDb, 2));
        if (! b.stem.empty())
            bo->setProperty ("stem", juce::String (b.stem));
        buses.add (juce::var (bo));
    }
    o->setProperty ("buses", buses);
    if (! report.masterStem.empty())
        o->setProperty ("masterStem", juce::String (report.masterStem));
    if (report.normalisation.applied)
    {
        auto* no = new juce::DynamicObject();
        no->setProperty ("targetLufs", number (report.normalisation.targetLufs, 2));
        no->setProperty ("gainDb", number (report.normalisation.gainDb, 2));
        no->setProperty ("passes", report.normalisation.passes);
        no->setProperty ("limited", report.normalisation.limited);
        o->setProperty ("normalisation", juce::var (no));
    }
    auto warnings = parseWarnings.warnings;
    warnings.insert (warnings.end(), report.warnings.begin(), report.warnings.end());
    o->setProperty ("warnings", diagnosticsArray (warnings));
    o->setProperty ("renderSeconds", number (report.renderSeconds, 3));
    if (measurePath)
    {
        const auto f = fileArg (*measurePath);
        if (! f.replaceWithText (juce::JSON::toString (measureObject (report.measure, c), true, 4)))
            throw Failure ("io", "cannot write measurements " + *measurePath);
        o->setProperty ("measure", f.getFullPathName());
    }
    const juce::var result (o);
    if (auto reportPath = a.value ("--report"))
        if (! fileArg (*reportPath).replaceWithText (juce::JSON::toString (result, false, 6)))
            throw Failure ("io", "cannot write report " + *reportPath);
    emit (result);
    return exitOk;
}

// ---- 영상 -----------------------------------------------------------------

std::string movieArg (const Args& a, const std::string& command)
{
    if (a.positional.size() != 1)
        throw UsageError (command + " needs exactly one movie");
    return fileArg (a.positional[0]).getFullPathName().toStdString();
}

juce::var videoInfoObject (const media::VideoInfo& info)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("durationSeconds", number (info.durationSeconds, 4));
    o->setProperty ("duration", juce::String (media::timecodeAt (info.durationSeconds, info.rate)));
    o->setProperty ("frameRate", number (info.rate.fps(), 4));
    o->setProperty ("frameRateExact", juce::String (info.rate.num) + "/" + juce::String (info.rate.den));
    o->setProperty ("dropFrame", info.rate.dropFrame());
    o->setProperty ("frames", juce::var ((juce::int64) info.frames));
    o->setProperty ("width", info.width);
    o->setProperty ("height", info.height);
    o->setProperty ("codec", juce::String (info.codec));
    if (info.hasAudio)
    {
        auto* ao = new juce::DynamicObject();
        ao->setProperty ("sampleRate", number (info.audioSampleRate, 0));
        ao->setProperty ("channels", info.audioChannels);
        o->setProperty ("audio", juce::var (ao));
    }
    else
        o->setProperty ("audio", juce::var());
    return juce::var (o);
}

int cmdVideoInfo (const Args& a)
{
    const auto path = movieArg (a, "video-info");
    std::string error;
    const auto info = media::probeVideo (path, error);
    if (! info)
        throw Failure ("invalid-input", error);
    auto o = videoInfoObject (*info);
    o.getDynamicObject()->setProperty ("ok", true);
    o.getDynamicObject()->setProperty ("command", "video-info");
    o.getDynamicObject()->setProperty ("file", juce::String (path));
    emit (o);
    return exitOk;
}

int cmdVideoFrame (const Args& a)
{
    const auto path = movieArg (a, "video-frame");
    const auto at = numberValue (a, "--at");
    const auto out = a.value ("--output");
    if (! at || ! out)
        throw UsageError ("video-frame needs --at <seconds> and --output <frame.png>");
    const int width = (int) numberValue (a, "--width").value_or (0.0), height = (int) numberValue (a, "--height").value_or (0.0);
    media::VideoFrames frames (path);
    if (! frames.ok())
        throw Failure ("invalid-input", frames.error());
    const auto image = frames.frameAt (*at, width, height);
    if (! image)
        throw Failure ("invalid-input", "no frame at " + std::to_string (*at) + " s (the video is "
                                            + std::to_string (frames.info().durationSeconds) + " s long)");
    const auto file = fileArg (*out).getFullPathName().toStdString();
    std::string error;
    if (! media::writePng (*image, file, error))
        throw Failure ("io", error);
    const auto rate = frames.info().rate;
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", true);
    o->setProperty ("command", "video-frame");
    o->setProperty ("output", juce::String (file));
    o->setProperty ("seconds", number (image->seconds, 6));
    o->setProperty ("frame", juce::var ((juce::int64) media::frameAtSeconds (image->seconds, rate)));
    o->setProperty ("timecode", juce::String (media::timecodeAt (image->seconds, rate)));
    o->setProperty ("width", image->width);
    o->setProperty ("height", image->height);
    emit (juce::var (o));
    return exitOk;
}

int cmdVideoScenes (const Args& a)
{
    const auto path = movieArg (a, "video-scenes");
    media::SceneOptions options;
    options.threshold = numberValue (a, "--threshold").value_or (options.threshold);
    options.ratio = numberValue (a, "--ratio").value_or (options.ratio);
    options.minGapSeconds = numberValue (a, "--min-gap").value_or (options.minGapSeconds);
    if (! (options.threshold > 0.0 && options.threshold < 1.0) || ! (options.ratio >= 1.0) || options.minGapSeconds < 0.0)
        throw UsageError ("--threshold is between 0 and 1, --ratio at least 1, --min-gap not negative");
    std::optional<Composition> song;
    if (auto s = a.value ("--song"))
        song = loadOrThrow (*s);
    std::string error;
    const auto info = media::probeVideo (path, error);
    if (! info)
        throw Failure ("invalid-input", error);
    const auto scenes = media::detectSceneChanges (path, options, error);
    if (! scenes)
        throw Failure ("invalid-input", error);
    std::optional<TempoMap> tempo;
    if (song)
        tempo.emplace (song->tempo);
    const double offset = song && song->video ? song->video->offsetSeconds : 0.0;
    juce::Array<juce::var> list;
    for (const auto& sc : *scenes)
    {
        auto* so = new juce::DynamicObject();
        so->setProperty ("seconds", number (sc.seconds, 6));
        so->setProperty ("frame", juce::var ((juce::int64) sc.frame));
        so->setProperty ("timecode", juce::String (media::timecodeAt (sc.seconds, info->rate)));
        so->setProperty ("score", number (sc.score, 3));
        if (tempo)
        {
            const double songSeconds = sc.seconds + offset;
            so->setProperty ("songSeconds", number (songSeconds, 4));
            if (songSeconds >= 0.0)
            {
                const double beat = tempo->beatAt (songSeconds);
                so->setProperty ("beat", number (beat, 4));
                const auto meters = song->meters();
                const int bar = meters.barAt (beat);
                so->setProperty ("bar", bar + 1);
                so->setProperty ("beatInBar", number (beat - meters.barStart (bar) + 1.0, 3));
            }
        }
        list.add (juce::var (so));
    }
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", true);
    o->setProperty ("command", "video-scenes");
    o->setProperty ("file", juce::String (path));
    o->setProperty ("frames", juce::var ((juce::int64) info->frames));
    o->setProperty ("count", (int) scenes->size());
    o->setProperty ("scenes", list);
    emit (juce::var (o));
    return exitOk;
}

int cmdVideoAudio (const Args& a)
{
    const auto path = movieArg (a, "video-audio");
    const auto out = a.value ("--output");
    if (! out)
        throw UsageError ("video-audio needs --output <sound.wav>");
    const auto file = fileArg (*out);
    const auto type = audioFileTypeFor (file);
    if (! type)
        throw UsageError ("--output must end in .wav, .flac, .ogg or .mp3");
    std::string error;
    const auto sound = media::readVideoAudio (path, numberValue (a, "--sample-rate").value_or (0.0), error);
    if (! sound)
        throw Failure ("invalid-input", error);
    juce::AudioBuffer<float> buffer ((int) sound->channels.size(), (int) sound->frames);
    for (size_t ch = 0; ch < sound->channels.size(); ++ch)
        buffer.copyFrom ((int) ch, 0, sound->channels[ch].data(), (int) sound->frames);
    writeAudioFile (file, buffer, 0, buffer.getNumSamples(), sound->sampleRate, { *type, 24, -1 });
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", true);
    o->setProperty ("command", "video-audio");
    o->setProperty ("output", file.getFullPathName());
    o->setProperty ("sampleRate", number (sound->sampleRate, 0));
    o->setProperty ("channels", (int) sound->channels.size());
    o->setProperty ("seconds", number ((double) sound->frames / sound->sampleRate, 4));
    o->setProperty ("stats", statsObject (analyseAudio (buffer, buffer.getNumSamples(), sound->sampleRate, 1.0f)));
    emit (juce::var (o));
    return exitOk;
}

std::vector<double> numberList (const Args& a, const std::string& name)
{
    std::vector<double> list;
    if (auto v = a.value (name))
        for (const auto& part : juce::StringArray::fromTokens (juce::String (*v), ",", ""))
        {
            const auto t = part.trim();
            if (t.isEmpty() || ! t.containsOnly ("0123456789.-"))
                throw UsageError (name + " takes numbers separated by commas, e.g. 2.5,5");
            list.push_back (t.getDoubleValue());
        }
    return list;
}

int cmdVideoPattern (const Args& a)
{
    const auto out = a.value ("--output");
    if (! out)
        throw UsageError ("video-pattern needs --output <movie.mp4>");
    media::TestPattern p;
    p.rate = media::frameRateFromFps (numberValue (a, "--fps").value_or (30.0));
    const double seconds = numberValue (a, "--seconds").value_or (10.0);
    if (! p.rate.valid() || p.rate.fps() > 120.0 || ! (seconds > 0.0) || seconds > 3600.0)
        throw UsageError ("--fps is up to 120 and --seconds up to 3600");
    p.frames = (int) std::max<int64_t> (1, (int64_t) std::llround (seconds * p.rate.fps()));
    if (auto size = a.value ("--size"))
    {
        const auto v = juce::String (*size);
        p.width = v.upToFirstOccurrenceOf ("x", false, false).getIntValue();
        p.height = v.fromFirstOccurrenceOf ("x", false, false).getIntValue();
    }
    for (const double s : numberList (a, "--scenes"))
        p.sceneCuts.push_back (media::frameAtSeconds (s + 1.0e-9, p.rate));
    p.clickSeconds = numberList (a, "--clicks");
    p.quarterTurns = (int) numberValue (a, "--rotate").value_or (0.0);
    const auto file = fileArg (*out).getFullPathName().toStdString();
    std::string error;
    if (! media::writeTestPattern (file, p, error))
        throw Failure ("io", error);
    const auto info = media::probeVideo (file, error);
    auto o = info ? videoInfoObject (*info) : juce::var (new juce::DynamicObject());
    o.getDynamicObject()->setProperty ("ok", true);
    o.getDynamicObject()->setProperty ("command", "video-pattern");
    o.getDynamicObject()->setProperty ("output", juce::String (file));
    emit (o);
    return exitOk;
}

int cmdInspect (const Args& a)
{
    if (a.positional.size() != 1)
        throw UsageError ("inspect needs exactly one audio file");
    const auto file = fileArg (a.positional[0]);
    const auto data = readAudioFile (file);
    // 정수 파일은 최대 코드값에 닿으면 클리핑으로 본다
    const float threshold = data.floatingPoint ? 1.0f : (float) (1.0 - 1.5 / std::pow (2.0, data.bitDepth - 1));
    const auto stats = analyseAudio (data.buffer, (int) data.samples, data.sampleRate, threshold);
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", true);
    o->setProperty ("command", "inspect");
    o->setProperty ("file", juce::String (file.getFullPathName()));
    o->setProperty ("format", juce::String (data.format));
    o->setProperty ("bitDepth", data.bitDepth);
    o->setProperty ("floatingPoint", data.floatingPoint);
    auto statsVar = statsObject (stats);
    for (const auto& p : statsVar.getDynamicObject()->getProperties())
        o->setProperty (p.name, p.value);
    if (const auto comments = readVorbisComments (file); ! comments.empty())
    {
        auto* tags = new juce::DynamicObject();
        for (const auto& c : comments)
            if (const auto eq = c.find ('='); eq != std::string::npos)
                tags->setProperty (juce::String::fromUTF8 (c.substr (0, eq).c_str()).toUpperCase(), juce::String::fromUTF8 (c.substr (eq + 1).c_str()));
        o->setProperty ("tags", juce::var (tags));
    }
    emit (juce::var (o));
    return exitOk;
}

int cmdExportMidi (const Args& a)
{
    if (a.positional.size() != 1)
        throw UsageError ("export-midi needs exactly one composition file");
    auto out = a.value ("--output");
    if (! out)
        throw UsageError ("export-midi needs --output <file.mid>");
    const auto c = loadOrThrow (a.positional[0]);
    const auto report = exportMidi (c, fileArg (*out), a.flag ("--humanized"), seedArg (a));
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", true);
    o->setProperty ("command", "export-midi");
    o->setProperty ("output", juce::String (fileArg (*out).getFullPathName()));
    o->setProperty ("tracks", report.tracks);
    o->setProperty ("notes", report.notes);
    o->setProperty ("lyrics", report.lyrics);
    o->setProperty ("markers", report.markers);
    o->setProperty ("lengthBeats", number (report.lengthBeats, 3));
    o->setProperty ("humanized", a.flag ("--humanized"));
    emit (juce::var (o));
    return exitOk;
}

size_t trackArg (const Composition& c, const std::string& text);

int cmdSampler (const Args& a)
{
    if (a.positional.size() != 1)
        throw UsageError ("sampler needs exactly one audio file");
    const auto out = a.value ("--output");
    if (! out)
        throw UsageError ("sampler needs --output <instrument.sfz>");
    SamplerOptions options;
    options.oneShot = a.flag ("--one-shot");
    if (const auto root = a.value ("--root"))
    {
        const auto key = parseNoteName (*root);
        if (! key)
            throw UsageError ("--root takes a note such as C4 or A#3");
        options.rootKey = *key;
    }
    const auto result = writeSamplerSfz (fileArg (*out), fileArg (a.positional[0]), options);
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", true);
    o->setProperty ("command", "sampler");
    o->setProperty ("output", juce::String (fileArg (*out).getFullPathName()));
    o->setProperty ("rootKey", result.rootKey);
    o->setProperty ("tuneCents", number (result.tuneCents, 1));
    if (result.pitch)
    {
        o->setProperty ("frequency", number (result.pitch->frequency, 2));
        o->setProperty ("clarity", number (result.pitch->clarity, 2));
    }
    emit (juce::var (o));
    return exitOk;
}

// 격자: 1/16 (16분음표), 1/8t (셋잇단 8분음표), 0.25 (박)
double gridArg (const std::string& text)
{
    const juce::String t (text);
    double grid = 0.0;
    if (t.contains ("/"))
    {
        const auto denominator = t.fromFirstOccurrenceOf ("/", false, false).trimCharactersAtEnd ("tT").getDoubleValue();
        const auto numerator = t.upToFirstOccurrenceOf ("/", false, false).getDoubleValue();
        if (denominator > 0.0)
            grid = 4.0 * numerator / denominator * (t.endsWithIgnoreCase ("t") ? 2.0 / 3.0 : 1.0);
    }
    else
        grid = t.getDoubleValue();
    if (! (grid > 0.0) || grid > 16.0)
        throw UsageError ("--grid takes 1/16, 1/8t or a length in beats");
    return grid;
}

int cmdTempo (const Args& a)
{
    if (a.positional.size() != 1)
        throw UsageError ("tempo needs exactly one audio file");
    const double from = std::max (0.0, numberValue (a, "--from").value_or (0.0));
    const auto seconds = numberValue (a, "--seconds");
    const auto audio = fileArg (a.positional[0]);
    const auto onsets = detectOnsets (audio, from, seconds);
    const auto tempo = estimateTempo (audio, from, seconds);
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", tempo.has_value());
    o->setProperty ("command", "tempo");
    o->setProperty ("onsets", (int) onsets.size());
    if (tempo)
    {
        o->setProperty ("bpm", number (tempo->bpm, 3));
        o->setProperty ("confidence", number (tempo->confidence, 2));
        o->setProperty ("firstBeatSeconds", number (tempo->firstBeatSeconds, 4));
    }
    else
        o->setProperty ("error", "fewer than four attacks");
    emit (juce::var (o));
    return tempo ? exitOk : exitFailed;
}

int cmdBeats (const Args& a)
{
    if (a.positional.size() != 1)
        throw UsageError ("beats needs exactly one audio file");
    const double from = std::max (0.0, numberValue (a, "--from").value_or (0.0));
    const auto beats = trackBeats (fileArg (a.positional[0]), from, numberValue (a, "--seconds"));
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", beats.size() >= 2);
    o->setProperty ("command", "beats");
    o->setProperty ("count", (int) beats.size());
    juce::Array<juce::var> list;
    for (const double b : beats)
        list.add (number (b, 4));
    o->setProperty ("beats", list);
    if (beats.size() >= 2)
    {
        std::vector<double> bpm;
        for (size_t i = 1; i < beats.size(); ++i)
            bpm.push_back (60.0 / (beats[i] - beats[i - 1]));
        auto sorted = bpm;
        std::sort (sorted.begin(), sorted.end());
        o->setProperty ("bpmMin", number (sorted.front(), 2));
        o->setProperty ("bpmMedian", number (sorted[sorted.size() / 2], 2));
        o->setProperty ("bpmMax", number (sorted.back(), 2));
    }
    else
        o->setProperty ("error", "fewer than four attacks");
    emit (juce::var (o));
    return beats.size() >= 2 ? exitOk : exitFailed;
}

int cmdSlice (const Args& a)
{
    if (a.positional.size() != 1)
        throw UsageError ("slice needs exactly one audio file");
    const auto out = a.value ("--output");
    if (! out)
        throw UsageError ("slice needs --output <kit.sfz>");
    int firstKey = 36;
    if (const auto root = a.value ("--root"))
    {
        const auto key = parseNoteName (*root);
        if (! key)
            throw UsageError ("--root takes a note such as C2");
        firstKey = *key;
    }
    const double from = std::max (0.0, numberValue (a, "--from").value_or (0.0));
    const auto seconds = numberValue (a, "--seconds");
    const auto audio = fileArg (a.positional[0]);
    auto onsets = detectOnsets (audio, from, seconds);
    if (onsets.empty())
        throw Failure ("invalid-input", "no attacks found in " + audio.getFullPathName().toStdString());
    const auto info = audioFileInfo (audio);
    const double end = seconds ? from + *seconds : (info ? info->seconds() : onsets.back() + 1.0);
    const int count = std::min ((int) onsets.size(), 128 - firstKey);
    onsets.resize ((size_t) std::max (0, count));
    writeSliceSfz (fileArg (*out), audio, onsets, end, firstKey);
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", true);
    o->setProperty ("command", "slice");
    o->setProperty ("output", juce::String (fileArg (*out).getFullPathName()));
    o->setProperty ("slices", (int) onsets.size());
    o->setProperty ("firstKey", juce::String (noteName (firstKey)));
    juce::Array<juce::var> starts;
    for (const double s : onsets)
        starts.add (number (s, 4));
    o->setProperty ("starts", starts);
    emit (juce::var (o));
    return exitOk;
}

int cmdNotes (const Args& a)
{
    if (a.positional.size() != 1)
        throw UsageError ("notes needs exactly one audio file");
    const double from = std::max (0.0, numberValue (a, "--from").value_or (0.0));
    const auto segments = noteSegments (fileArg (a.positional[0]), from, numberValue (a, "--seconds"));
    juce::Array<juce::var> list;
    for (size_t i = 0; i < segments.size(); ++i)
    {
        const auto& s = segments[i];
        const int pitch = (int) std::lround (s.midi);
        auto* n = new juce::DynamicObject();
        n->setProperty ("note", (int) i + 1);
        n->setProperty ("startSeconds", number (s.startSeconds, 3));
        n->setProperty ("endSeconds", number (s.endSeconds, 3));
        n->setProperty ("pitch", juce::String (noteName (pitch)));
        n->setProperty ("cents", number (100.0 * (s.midi - pitch), 1));
        n->setProperty ("driftCents", number (s.driftCents, 1));
        n->setProperty ("vibratoCents", number (s.vibratoCents, 1));
        n->setProperty ("velocity", s.velocity);
        list.add (juce::var (n));
    }
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", true);
    o->setProperty ("command", "notes");
    o->setProperty ("notes", list);
    emit (juce::var (o));
    return exitOk;
}

int cmdTranscribe (const Args& a)
{
    if (a.positional.size() != 1)
        throw UsageError ("transcribe needs exactly one audio file");
    const auto out = a.value ("--output");
    if (! out)
        throw UsageError ("transcribe needs --output <song.json>");
    const auto output = fileArg (*out);
    if (output.exists() && ! a.flag ("--force"))
        throw Failure ("io", output.getFullPathName().toStdString() + " already exists (use --force to replace it)");
    auto numberOption = [&] (const std::string& name) -> std::optional<double>
    {
        if (auto v = a.value (name))
        {
            try { return std::stod (*v); }
            catch (...) { throw UsageError (name + " must be a number"); }
        }
        return std::nullopt;
    };
    const double bpm = numberOption ("--bpm").value_or (120.0);
    if (bpm < 10.0 || bpm > 400.0)
        throw UsageError ("--bpm must be between 10 and 400");
    const double from = std::max (0.0, numberOption ("--from").value_or (0.0));
    const auto seconds = numberOption ("--seconds");
    const auto audio = fileArg (a.positional[0]);
    const auto notes = transcribeMelody (audio, from, seconds);
    const auto instrument = a.value ("--instrument").value_or ("grand-piano");
    const auto title = a.value ("--title").value_or (audio.getFileNameWithoutExtension().toStdString());
    // 받아 적은 구간의 처음이 0 박이다
    const auto list = transcribedNotesJson (notes, [&] (double s) { return (s - from) * bpm / 60.0; });
    auto* track = new juce::DynamicObject();
    track->setProperty ("name", juce::String::fromUTF8 (title.c_str()));
    track->setProperty ("instrument", juce::String (instrument));
    track->setProperty ("notes", list);
    auto* root = new juce::DynamicObject();
    root->setProperty ("title", juce::String::fromUTF8 (title.c_str()));
    root->setProperty ("tempo", bpm);
    root->setProperty ("tracks", juce::Array<juce::var> { juce::var (track) });
    const juce::var song (root);
    const auto parsed = parseCompositionValue (song, output);
    if (! parsed.composition)
        throw Failure ("invalid-input", "the transcribed song does not validate", parsed.diagnostics.errors);
    const auto parent = output.getParentDirectory();
    if (! parent.isDirectory() && ! parent.createDirectory())
        throw Failure ("io", "cannot create directory " + parent.getFullPathName().toStdString());
    if (! output.replaceWithText (juce::String::fromUTF8 ((writeJson (song) + "\n").c_str())))
        throw Failure ("io", "cannot write " + output.getFullPathName().toStdString());
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", true);
    o->setProperty ("command", "transcribe");
    o->setProperty ("output", output.getFullPathName());
    o->setProperty ("notes", list.size());
    if (! notes.empty())
    {
        int low = 127, high = 0;
        for (const auto& n : notes)
        {
            low = std::min (low, n.pitch);
            high = std::max (high, n.pitch);
        }
        o->setProperty ("lowest", juce::String (noteName (low)));
        o->setProperty ("highest", juce::String (noteName (high)));
        o->setProperty ("firstSeconds", number (notes.front().startSeconds, 3));
        o->setProperty ("lastSeconds", number (notes.back().endSeconds, 3));
    }
    emit (juce::var (o));
    return exitOk;
}

int cmdExportMusicXml (const Args& a)
{
    if (a.positional.size() != 1)
        throw UsageError ("export-musicxml needs exactly one composition file");
    const auto out = a.value ("--output");
    if (! out)
        throw UsageError ("export-musicxml needs --output <file.musicxml>");
    const auto c = loadOrThrow (a.positional[0]);
    Diagnostics d;
    const Registry registry = buildRegistry (c, renderOptions (a), d);
    ScoreOptions options;
    if (const auto text = a.value ("--tracks"))
    {
        juce::StringArray parts;
        parts.addTokens (juce::String::fromUTF8 (text->c_str()), ",", "");
        for (const auto& part : parts)
            options.tracks.push_back (trackArg (c, part.trim().toStdString()));
    }
    // 건반 이름이 있는 악기는 타악기 보표
    options.percussion = [&registry] (const TrackSpec& t)
    {
        const auto* def = registry.find (t.instrument);
        return def != nullptr && ! def->keyNames.empty();
    };
    const auto score = buildScore (c, options);
    writeMusicXml (score, fileArg (*out));
    int notes = 0;
    juce::Array<juce::var> parts;
    for (const auto& part : score.parts)
    {
        parts.add (juce::String::fromUTF8 (part.name.c_str()));
        for (const auto& m : part.measures)
            for (const auto& staff : m.staves)
                for (const auto& e : staff)
                    for (const auto& p : e.pitches)
                        notes += p.tieStop ? 0 : 1;
    }
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", true);
    o->setProperty ("command", "export-musicxml");
    o->setProperty ("output", juce::String (fileArg (*out).getFullPathName()));
    o->setProperty ("parts", parts);
    o->setProperty ("measures", score.parts.empty() ? 0 : (int) score.parts.front().measures.size());
    o->setProperty ("notes", notes);
    o->setProperty ("key", juce::String (keyName (score.key)));
    o->setProperty ("keyEstimated", score.keyEstimated);
    emit (juce::var (o));
    return exitOk;
}

int cmdImportMidi (const Args& a)
{
    if (a.positional.size() != 1)
        throw UsageError ("import-midi needs exactly one MIDI file");
    const auto out = a.value ("--output");
    if (! out)
        throw UsageError ("import-midi needs --output <song.json>");
    const auto output = fileArg (*out);
    if (output.exists() && ! a.flag ("--force"))
        throw Failure ("io", output.getFullPathName().toStdString() + " already exists (use --force to replace it)");
    Composition empty;
    Diagnostics d;
    const Registry registry = buildRegistry (empty, renderOptions (a), d);
    MidiImportOptions options;
    options.title = a.value ("--title").value_or ("");
    options.registry = &registry;
    if (auto instrument = a.value ("--instrument"))
    {
        if (registry.find (*instrument) == nullptr)
            throw Failure ("instrument", "unknown instrument '" + *instrument + "'");
        options.defaultInstrument = *instrument;
    }
    MidiImportReport report;
    const auto root = importMidi (fileArg (a.positional[0]), options, report);
    const auto parsed = parseCompositionValue (root, output);
    if (! parsed.composition)
        throw Failure ("invalid-input", "the imported song does not validate", parsed.diagnostics.errors);
    const auto parent = output.getParentDirectory();
    if (! parent.isDirectory() && ! parent.createDirectory())
        throw Failure ("io", "cannot create directory " + parent.getFullPathName().toStdString());
    if (! output.replaceWithText (juce::String::fromUTF8 ((writeJson (root) + "\n").c_str())))
        throw Failure ("io", "cannot write " + output.getFullPathName().toStdString());
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", true);
    o->setProperty ("command", "import-midi");
    o->setProperty ("output", output.getFullPathName());
    o->setProperty ("title", root.getProperty ("title", ""));
    o->setProperty ("tracks", report.tracks);
    o->setProperty ("notes", report.notes);
    o->setProperty ("lyrics", report.lyrics);
    o->setProperty ("markers", report.markers);
    o->setProperty ("controllerPoints", report.controllerPoints);
    o->setProperty ("tempoPoints", report.tempoPoints);
    o->setProperty ("lengthBeats", number (report.lengthBeats, 3));
    juce::Array<juce::var> instruments, warnings;
    for (const auto& t : *root["tracks"].getArray())
        instruments.add (t["instrument"]);
    for (const auto& w : report.warnings)
        warnings.add (juce::String (w));
    o->setProperty ("instruments", instruments);
    o->setProperty ("warnings", warnings);
    emit (juce::var (o));
    return exitOk;
}

// 트랙 하나: 1부터 센 번호나 이름
size_t trackArg (const Composition& c, const std::string& text)
{
    for (size_t i = 0; i < c.tracks.size(); ++i)
        if (c.tracks[i].name == text)
            return i;
    char* end = nullptr;
    const long n = std::strtol (text.c_str(), &end, 10);
    if (end != text.c_str() && *end == 0 && n >= 1 && (size_t) n <= c.tracks.size())
        return (size_t) n - 1;
    std::vector<std::string> names;
    for (const auto& t : c.tracks)
        names.push_back (t.name);
    const auto s = closestMatch (text, names);
    throw Failure ("invalid-input", "unknown track '" + text + "'" + (s.empty() ? std::string {} : " (did you mean '" + s + "'?)"));
}

// --tracks: "2-4" 또는 "Lead,Bass" (없으면 모든 트랙)
std::vector<size_t> tracksArg (const Args& a, const Composition& c)
{
    std::vector<size_t> out;
    const auto text = a.value ("--tracks");
    if (! text)
    {
        for (size_t i = 0; i < c.tracks.size(); ++i)
            out.push_back (i);
        return out;
    }
    if (const auto dash = text->find ('-'); dash != std::string::npos && dash > 0 && text->find (',') == std::string::npos
        && std::all_of (text->begin(), text->end(), [] (char ch) { return (ch >= '0' && ch <= '9') || ch == '-'; }))
    {
        const auto [first, last] = rangeArg ("--tracks", *text);
        for (int i = (int) first; i <= (int) last; ++i)
            out.push_back (trackArg (c, std::to_string (i)));
        return out;
    }
    juce::StringArray parts;
    parts.addTokens (juce::String::fromUTF8 (text->c_str()), ",", "");
    for (const auto& part : parts)
        out.push_back (trackArg (c, part.trim().toStdString()));
    std::sort (out.begin(), out.end());
    // 구간 편집은 이어진 트랙만 (붙이는 자리가 순서대로다)
    for (size_t i = 1; i < out.size(); ++i)
        if (out[i] != out[i - 1] + 1)
            throw UsageError ("--tracks must name adjacent tracks");
    return out;
}

// 그루브: 기본 그루브의 id 또는 JSON 파일
Groove grooveArg (const std::string& text)
{
    if (const auto* g = findFactoryGroove (text))
        return *g;
    const auto file = fileArg (text);
    if (! file.existsAsFile())
        throw UsageError ("--groove takes a groove id (music-render groove list) or a groove file");
    std::string error;
    const auto g = grooveFromJson (juce::JSON::parse (file.loadFileAsString()), error);
    if (! g)
        throw Failure ("invalid-input", file.getFullPathName().toStdString() + ": " + error);
    return *g;
}

juce::var grooveSummary (const Groove& g)
{
    auto o = grooveJson (g);
    o.getDynamicObject()->setProperty ("lengthBeats", g.lengthBeats());
    return o;
}

int cmdGroove (const Args& a)
{
    if (a.positional.empty())
        throw UsageError ("groove needs list, extract or extract-audio");
    const auto& what = a.positional[0];
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", true);
    o->setProperty ("command", "groove");
    if (what == "list")
    {
        juce::Array<juce::var> list;
        for (const auto& g : factoryGrooves())
            list.add (grooveSummary (g));
        o->setProperty ("grooves", list);
        emit (juce::var (o));
        return exitOk;
    }
    if (what != "extract" && what != "extract-audio")
        throw UsageError ("groove needs list, extract or extract-audio");
    if (a.positional.size() != 2)
        throw UsageError ("groove " + what + " needs one " + (what == "extract" ? "song" : "audio file"));
    const auto out = a.value ("--output");
    if (! out)
        throw UsageError ("groove " + what + " needs --output <groove.json>");
    const double grid = gridArg (a.value ("--grid").value_or ("1/16"));
    Groove g;
    if (what == "extract")
    {
        std::string error;
        auto doc = CompositionDocument::load (fileArg (a.positional[1]), error);
        if (! doc)
            throw Failure ("invalid-input", error);
        const auto parsed = doc->parse();
        if (! parsed.composition)
            throw Failure ("invalid-input", "composition has errors", parsed.diagnostics.errors);
        const auto& c = *parsed.composition;
        const auto track = a.value ("--track");
        if (! track)
            throw UsageError ("groove extract needs --track");
        const auto& notes = c.tracks[trackArg (c, *track)].notes;
        const auto meters = c.meters();
        double from = 0.0, to = 1.0e9;
        if (const auto bars = a.value ("--bars"))
        {
            const auto [first, last] = rangeArg ("--bars", *bars);
            from = meters.barStart ((int) first - 1);
            to = meters.barStart ((int) last);
        }
        g = grooveFromNotes (notes, from, to, grid, numberValue (a, "--length").value_or (meters.barBeatsAt (from)));
    }
    else
    {
        const auto bpm = numberValue (a, "--bpm");
        if (! bpm || *bpm < 20.0 || *bpm > 400.0)
            throw UsageError ("groove extract-audio needs --bpm (20-400), with --from at the first beat");
        const double from = std::max (0.0, numberValue (a, "--from").value_or (0.0));
        const auto audio = fileArg (a.positional[1]);
        const auto onsets = detectOnsets (audio, from, numberValue (a, "--seconds"));
        std::vector<double> beats;
        for (const double t : onsets)
            beats.push_back ((t - from) * *bpm / 60.0);
        g = grooveFromHits (beats, onsetLevelsDb (audio, onsets), grid, numberValue (a, "--length").value_or (4.0));
        o->setProperty ("attacks", (int) onsets.size());
    }
    g.name = a.value ("--name").value_or (fileArg (*out).getFileNameWithoutExtension().toStdString());
    if (! fileArg (*out).replaceWithText (juce::JSON::toString (grooveJson (g))))
        throw Failure ("io", "cannot write " + *out);
    o->setProperty ("output", fileArg (*out).getFullPathName());
    o->setProperty ("groove", grooveSummary (g));
    emit (juce::var (o));
    return exitOk;
}

int cmdTrackTemplate (const Args& a)
{
    if (a.positional.empty() || (a.positional[0] != "list" && a.positional[0] != "save"))
        throw UsageError ("track-template needs list or save");
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", true);
    o->setProperty ("command", "track-template");
    if (a.positional[0] == "list")
    {
        juce::Array<juce::var> list;
        for (const auto& [id, t] : factoryTrackTemplates())
        {
            auto* item = new juce::DynamicObject();
            item->setProperty ("id", juce::String (id));
            item->setProperty ("name", t.getProperty ("name", ""));
            item->setProperty ("instrument", t["track"]["instrument"]);
            item->setProperty ("effects", t["track"]["effects"].size());
            list.add (juce::var (item));
        }
        o->setProperty ("templates", list);
        emit (juce::var (o));
        return exitOk;
    }
    if (a.positional.size() != 3)
        throw UsageError ("track-template save needs a song and a track");
    const auto out = a.value ("--output");
    if (! out)
        throw UsageError ("track-template save needs --output <template.json>");
    const auto source = fileArg (a.positional[1]);
    std::string error;
    auto doc = CompositionDocument::load (source, error);
    if (! doc)
        throw Failure ("invalid-input", error);
    const auto parsed = doc->parse();
    if (! parsed.composition)
        throw Failure ("invalid-input", "composition has errors", parsed.diagnostics.errors);
    const auto index = trackArg (*parsed.composition, a.positional[2]);
    const auto name = a.value ("--name").value_or (parsed.composition->tracks[index].name);
    const auto t = trackTemplateOf (doc->root(), index, name, source.getParentDirectory());
    if (! fileArg (*out).replaceWithText (juce::JSON::toString (t)))
        throw Failure ("io", "cannot write " + *out);
    o->setProperty ("output", fileArg (*out).getFullPathName());
    o->setProperty ("name", juce::String::fromUTF8 (name.c_str()));
    emit (juce::var (o));
    return exitOk;
}

int cmdArrange (const Args& a)
{
    if (a.positional.size() < 2)
        throw UsageError ("arrange needs a composition file and an operation (see music-render help)");
    const auto source = fileArg (a.positional[0]);
    std::string error;
    auto doc = CompositionDocument::load (source, error);
    if (! doc)
        throw Failure ("invalid-input", error);
    const auto parsed = doc->parse();
    if (! parsed.composition)
        throw Failure ("invalid-input", "composition has " + std::to_string (parsed.diagnostics.errors.size()) + " error(s)",
                       parsed.diagnostics.errors);
    const auto& c = *parsed.composition;
    const auto meters = c.meters();
    const auto& op = a.positional[1];
    auto need = [&a] (const char* name, const std::string& forOp)
    {
        auto v = a.value (name);
        if (! v)
            throw UsageError (forOp + " needs " + name);
        return *v;
    };
    auto barsArg = [&] (const std::string& forOp)
    {
        const auto [first, last] = rangeArg ("--bars", need ("--bars", forOp));
        if (first < 1 || last < first)
            throw UsageError ("--bars must look like 17-24 with bars counted from 1");
        return std::make_pair (meters.barStart ((int) first - 1), meters.barStart ((int) last));
    };
    auto barArg = [&] (const char* name, const std::string& forOp)
    {
        const double bar = std::stod (need (name, forOp));
        if (bar < 1)
            throw UsageError (std::string (name) + " counts bars from 1");
        return meters.barStart ((int) std::lround (bar) - 1);
    };
    auto trackOperand = [&] () -> size_t
    {
        if (a.positional.size() < 3)
            throw UsageError (op + " needs a track (number from 1 or name)");
        return trackArg (c, a.positional[2]);
    };

    const auto& root = doc->root();
    const auto folder = source.getParentDirectory();   // 길이가 적히지 않은 클립의 파일
    juce::var after;
    if (op == "insert-bars")
    {
        // 넣는 마디는 넣는 자리 앞의 박자로 (그 자리의 박자 바꾸기는 뒤로 밀린다)
        const double at = barArg ("--at", op);
        const double barBeats = meters.barBeatsAt (at > 0.0 ? at - 1.0e-6 : 0.0);
        after = withTimeInserted (root, at, std::stod (need ("--count", op)) * barBeats, folder);
    }
    else if (op == "delete-bars")
    {
        const auto [from, to] = barsArg (op);
        after = withTimeDeleted (root, from, to, folder);
    }
    else if (op == "duplicate" || op == "copy" || op == "clear")
    {
        const auto [from, to] = barsArg (op);
        const auto tracks = tracksArg (a, c);
        if (op == "clear")
            after = withRangeCleared (root, tracks, from, to, folder);
        else
        {
            const auto clip = copyRange (root, tracks, from, to, folder);
            after = withClipPasted (root, clip, tracks.front(), op == "copy" ? barArg ("--to", op) : to, folder);
        }
    }
    else if (op == "add-track")
    {
        const size_t at = a.value ("--at") ? std::min (c.tracks.size(), (size_t) std::max (1, std::stoi (*a.value ("--at"))) - 1) : c.tracks.size();
        if (const auto templ = a.value ("--template"))
        {
            // 기본 템플릿의 id 또는 템플릿 파일
            juce::var t;
            for (const auto& [id, value] : factoryTrackTemplates())
                if (id == *templ)
                    t = value;
            if (t.isVoid())
            {
                const auto f = fileArg (*templ);
                if (! f.existsAsFile())
                    throw UsageError ("--template takes a template id (music-render track-template list) or a template file");
                t = juce::JSON::parse (f.loadFileAsString());
            }
            after = withTrackFromTemplate (root, at, t);
            if (after.isVoid())
                throw Failure ("invalid-input", *templ + " is not a track template");
            if (const auto name = a.value ("--name"))
                after = withTrackRenamed (after, at, *name);
        }
        else
            after = withTrackInserted (root, at, newTrackJson (need ("--name", op), need ("--instrument", op)));
    }
    else if (op == "duplicate-track")
        after = withTrackDuplicated (root, trackOperand());
    else if (op == "remove-track")
    {
        if (c.tracks.size() < 2)
            throw Failure ("invalid-input", "a song needs at least one track");
        after = withTrackRemoved (root, trackOperand());
    }
    else if (op == "rename-track")
    {
        const auto index = trackOperand();
        const auto name = need ("--name", op);
        for (size_t i = 0; i < c.tracks.size(); ++i)
            if (i != index && c.tracks[i].name == name)
                throw Failure ("invalid-input", "a track named '" + name + "' already exists");
        after = withTrackRenamed (root, index, name);
    }
    else if (op == "move-track")
    {
        const auto index = trackOperand();
        const int to = std::stoi (need ("--to", op));
        if (to < 1 || (size_t) to > c.tracks.size())
            throw UsageError ("--to must be a track position from 1 to " + std::to_string (c.tracks.size()));
        after = withTrackMoved (root, index, (size_t) to - 1);
    }
    else if (op == "follow-tempo")
    {
        // 곡의 템포를 오디오 클립의 연주에 맞춘다 (--clip 이 없으면 첫 들리는 클립)
        const auto index = trackOperand();
        const auto& track = c.tracks[index];
        size_t k = 0;
        if (const auto only = a.value ("--clip"))
            k = (size_t) std::max (1, std::stoi (*only)) - 1;
        else
            while (k < track.clips.size() && track.clips[k].muted)
                ++k;
        if (k >= track.clips.size())
            throw Failure ("invalid-input", "track '" + track.name + "' has no such audio clip");
        const auto& clip = track.clips[k];
        const auto beats = trackBeats (clipSourceFile (clip, folder), clip.offsetSeconds, clipLengthSeconds (clip, folder));
        after = withTempoFollowingClip (root, index, k, beats, meters);
        if (after.isVoid())
            throw Failure ("invalid-input", "found " + std::to_string (beats.size()) + " beats in the clip; following the tempo needs at least three");
    }
    else if (op == "groove")
    {
        // 음표 트랙은 음표를, 오디오 트랙은 클립의 어택(워프 마커)을 그루브로
        const auto index = trackOperand();
        const auto& track = c.tracks[index];
        const auto groove = grooveArg (need ("--groove", op));
        const double timing = std::clamp (numberValue (a, "--timing").value_or (100.0), 0.0, 100.0) / 100.0;
        const double velocity = std::clamp (numberValue (a, "--velocity").value_or (100.0), 0.0, 100.0) / 100.0;
        after = root.clone();
        auto trackJson = (*after.getProperty ("tracks", juce::var()).getArray())[(int) index];
        if (! track.clips.empty())
        {
            AudioQuantizeOptions options;
            options.groove = groove;
            options.strength = timing;
            for (size_t k = 0; k < track.clips.size(); ++k)
            {
                if (track.clips[k].muted)
                    continue;
                auto clip = track.clips[k];
                const double length = clipLengthSeconds (clip, folder);
                clip.warp = quantizedWarp (clip, TempoMap (c.tempo), length, detectOnsets (clipSourceFile (clip, folder), clip.offsetSeconds, length), options);
                if (clip.warp.empty())
                    continue;
                clip.stretch = 1.0;
                trackJson = withClip (trackJson, k, clip);
            }
        }
        else
        {
            const auto result = applyGroove (editNotes (trackJson, track), {}, groove, timing, velocity);
            trackJson = withNotes (trackJson, result.notes);
        }
        after.getProperty ("tracks", juce::var()).getArray()->set ((int) index, trackJson);
    }
    else if (op == "tune" || op == "untune")
    {
        // 오디오 트랙의 클립 음높이 교정 (--clip 이 없으면 모든 들리는 클립)
        const auto index = trackOperand();
        const auto& track = c.tracks[index];
        if (track.clips.empty())
            throw Failure ("invalid-input", "track '" + track.name + "' has no audio clips");
        std::optional<PitchCorrection> correction;
        if (op == "tune")
        {
            PitchCorrection pc;
            pc.strength = std::clamp (numberValue (a, "--strength").value_or (100.0), 0.0, 100.0) / 100.0;
            pc.speedMs = std::clamp (numberValue (a, "--speed").value_or (30.0), 0.0, 1000.0);
            pc.formant = ! a.flag ("--no-formant");
            pc.key = a.value ("--key").value_or ("");
            pc.scale = a.value ("--scale").value_or ("");
            pc.root = a.value ("--root").value_or ("");
            if (! pc.scale.empty() && pc.root.empty())
                throw UsageError ("--scale needs --root");
            correction = pc;
        }
        after = root.clone();
        auto trackJson = (*after.getProperty ("tracks", juce::var()).getArray())[(int) index];
        for (size_t k = 0; k < track.clips.size(); ++k)
        {
            if (const auto only = a.value ("--clip"); only ? (size_t) std::stoi (*only) != k + 1 : track.clips[k].muted)
                continue;
            auto clip = track.clips[k];
            clip.pitchCorrection = correction;
            if (correction)
                clip.warpSlices = false;   // 자른 조각은 늘이지 않는다
            trackJson = withClip (trackJson, k, clip);
        }
        after.getProperty ("tracks", juce::var()).getArray()->set ((int) index, trackJson);
    }
    else if (op == "pitch-edit")
    {
        // 오디오 트랙 클립의 음 하나 (음 번호는 클립이 트는 구간의 음 덩어리를 1 부터 센다)
        const auto index = trackOperand();
        const auto& track = c.tracks[index];
        if (track.clips.empty())
            throw Failure ("invalid-input", "track '" + track.name + "' has no audio clips");
        size_t k = 0;
        if (const auto only = a.value ("--clip"))
        {
            const int n = std::stoi (*only);
            if (n < 1 || (size_t) n > track.clips.size())
                throw UsageError ("--clip must be a clip number from 1 to " + std::to_string (track.clips.size()));
            k = (size_t) n - 1;
        }
        else if (track.clips.size() > 1)
            throw UsageError ("track '" + track.name + "' has " + std::to_string (track.clips.size()) + " clips: pitch-edit needs --clip <n>");
        auto clip = track.clips[k];
        if (clip.warpSlices)
            throw Failure ("invalid-input", "a sliced clip plays at the file's own pitch (quantize-audio --mode stretch first)");
        const bool clear = a.flag ("--clear");
        const auto noteArg = a.value ("--note");
        if (! noteArg && ! clear)
            throw UsageError ("pitch-edit needs --note <k> (music-render notes lists them)");
        if (! noteArg)
            clip.pitchEdits.clear();
        else
        {
            // 앱과 같은 번호: 파일 전체의 음 가운데 클립이 트는 구간의 음
            const auto all = fileNoteSegments (clipSourceFile (clip, folder));
            const auto [first, last] = segmentRange (*all, clip.offsetSeconds, clip.offsetSeconds + clipLengthSeconds (clip, folder));
            const int n = std::stoi (*noteArg);
            if (n < 1 || (size_t) n > last - first)
                throw UsageError ("--note must be a note number from 1 to " + std::to_string (last - first) + " (the clip has "
                                  + std::to_string (last - first) + " notes)");
            const auto& s = (*all)[first + (size_t) n - 1];
            // 이 음에 걸친 편집은 지우고, 처음 것의 값을 이어 쓴다
            PitchEdit e;
            bool had = false;
            std::vector<PitchEdit> kept;
            for (const auto& old : clip.pitchEdits)
            {
                if (old.seconds < s.endSeconds && old.endSeconds() > s.startSeconds)
                {
                    if (! had)
                        e = old;
                    had = true;
                }
                else
                    kept.push_back (old);
            }
            if (! clear)
            {
                e.seconds = s.startSeconds;
                e.lengthSeconds = s.endSeconds - s.startSeconds;
                const auto to = a.value ("--to");
                if (to && a.value ("--semitones"))
                    throw UsageError ("pitch-edit takes --semitones or --to, not both");
                if (const auto semitones = numberValue (a, "--semitones"))
                    e.semitones = std::clamp (*semitones, -24.0, 24.0);
                if (to)
                {
                    const auto target = parseNoteName (*to);
                    if (! target)
                        throw UsageError ("--to must be a note name like E4 or Bb3");
                    e.semitones = std::clamp (*target - (s.midi + clip.pitchSemitones), -24.0, 24.0);   // 들리는 가운데 높이를 그 음에
                }
                if (const auto flatten = numberValue (a, "--flatten"))
                    e.flatten = std::clamp (*flatten, 0.0, 100.0) / 100.0;
                if (const auto vibrato = numberValue (a, "--vibrato"))
                    e.vibrato = std::clamp (*vibrato, 0.0, 200.0) / 100.0;
                kept.push_back (e);
                std::sort (kept.begin(), kept.end(), [] (const PitchEdit& x, const PitchEdit& y) { return x.seconds < y.seconds; });
            }
            clip.pitchEdits = kept;
        }
        after = root.clone();
        auto trackJson = (*after.getProperty ("tracks", juce::var()).getArray())[(int) index];
        trackJson = withClip (trackJson, k, clip);
        after.getProperty ("tracks", juce::var()).getArray()->set ((int) index, trackJson);
    }
    else if (op == "reverse" || op == "fade" || op == "crossfade")
    {
        // 오디오 트랙의 클립 (--clip 이 없으면 모든 들리는 클립)
        const auto index = trackOperand();
        const auto& track = c.tracks[index];
        if (track.clips.empty())
            throw Failure ("invalid-input", "track '" + track.name + "' has no audio clips");
        std::vector<size_t> chosen;
        if (const auto only = a.value ("--clip"))
        {
            const int n = std::stoi (*only);
            if (n < 1 || (size_t) n > track.clips.size())
                throw UsageError ("--clip must be a clip number from 1 to " + std::to_string (track.clips.size()));
            chosen.push_back ((size_t) n - 1);
        }
        else
            for (size_t k = 0; k < track.clips.size(); ++k)
                if (! track.clips[k].muted)
                    chosen.push_back (k);
        const TempoMap tempo (c.tempo);
        auto clips = track.clips;
        std::vector<bool> changed (clips.size(), false);
        auto shapeArg = [&a] (const char* option) -> std::optional<FadeShape>
        {
            const auto name = a.value (option);
            if (! name)
                return std::nullopt;
            const auto shape = parseFadeShape (*name);
            if (! shape)
                throw UsageError (std::string (option) + " is sine, linear, equalPower or exponential");
            return shape;
        };
        if (op == "reverse")
            for (const size_t k : chosen)
            {
                const auto info = audioFileInfo (clipFile (clips[k], folder));
                if (! info)
                    throw Failure ("invalid-input", "cannot read " + clips[k].file);
                clips[k] = withClipReversed (clips[k], info->seconds());
                changed[k] = true;
            }
        else if (op == "fade")
        {
            const auto in = numberValue (a, "--in"), out = numberValue (a, "--out");
            const auto inShape = shapeArg ("--in-shape"), outShape = shapeArg ("--out-shape");
            if (! in && ! out && ! inShape && ! outShape)
                throw UsageError ("fade needs --in, --out, --in-shape or --out-shape");
            for (const size_t k : chosen)
            {
                auto& clip = clips[k];
                const double length = clipEndSeconds (clip, tempo, folder) - tempo.secondsAt (clip.startBeat);
                if (in)
                    clip.fadeInSeconds = std::clamp (*in, 0.0, length);
                if (out)
                    clip.fadeOutSeconds = std::clamp (*out, 0.0, length);
                if (inShape)
                    clip.fadeInShape = *inShape;
                if (outShape)
                    clip.fadeOutShape = *outShape;
                changed[k] = true;
            }
        }
        else
        {
            // 클립과 그 뒤에서 겹치거나 맞붙은 다음 클립 (시작 순서, 들리는 클립끼리)
            const double length = std::clamp (numberValue (a, "--ms").value_or (20.0), 1.0, 10000.0) / 1000.0;
            std::vector<size_t> order;
            for (size_t k = 0; k < clips.size(); ++k)
                if (! clips[k].muted)
                    order.push_back (k);
            std::stable_sort (order.begin(), order.end(), [&] (size_t x, size_t y) { return clips[x].startBeat < clips[y].startBeat; });
            int made = 0;
            for (const size_t k : chosen)
            {
                const auto at = std::find (order.begin(), order.end(), k);
                if (at == order.end() || at + 1 == order.end())
                    continue;
                if (const auto pair = crossfadedClips (clips[k], clips[*(at + 1)], tempo, folder, length))
                {
                    clips[k] = pair->first;
                    clips[*(at + 1)] = pair->second;
                    changed[k] = changed[*(at + 1)] = true;
                    ++made;
                }
            }
            if (made == 0)
                throw Failure ("invalid-input", "no clip overlaps or touches the next one");
        }
        after = root.clone();
        auto trackJson = (*after.getProperty ("tracks", juce::var()).getArray())[(int) index];
        for (size_t k = 0; k < clips.size(); ++k)
            if (changed[k])
                trackJson = withClip (trackJson, k, clips[k]);
        after.getProperty ("tracks", juce::var()).getArray()->set ((int) index, trackJson);
    }
    else if (op == "quantize-audio" || op == "unwarp")
    {
        // 오디오 트랙의 클립 (--clip 이 없으면 모든 들리는 클립)
        const auto index = trackOperand();
        const auto& track = c.tracks[index];
        if (track.clips.empty())
            throw Failure ("invalid-input", "track '" + track.name + "' has no audio clips");
        std::vector<size_t> chosen;
        if (const auto clip = a.value ("--clip"))
        {
            const int n = std::stoi (*clip);
            if (n < 1 || (size_t) n > track.clips.size())
                throw UsageError ("--clip must be a clip number from 1 to " + std::to_string (track.clips.size()));
            chosen.push_back ((size_t) n - 1);
        }
        else
            for (size_t k = 0; k < track.clips.size(); ++k)
                if (! track.clips[k].muted)
                    chosen.push_back (k);
        AudioQuantizeOptions options;
        if (op == "quantize-audio")
        {
            options.grid = gridArg (need ("--grid", op));
            options.strength = std::clamp (numberValue (a, "--strength").value_or (100.0), 0.0, 100.0) / 100.0;
            options.swing = std::clamp (numberValue (a, "--swing").value_or (0.0), 0.0, 100.0) / 100.0;
            if (const auto mode = a.value ("--mode"); mode && *mode != "stretch" && *mode != "slice")
                throw UsageError ("--mode is stretch or slice");
        }
        after = root.clone();
        auto trackJson = (*after.getProperty ("tracks", juce::var()).getArray())[(int) index];
        for (const size_t k : chosen)
        {
            auto clip = track.clips[k];
            if (op == "unwarp")
            {
                clip.warp.clear();
                clip.warpSlices = false;
            }
            else
            {
                const double length = clipLengthSeconds (clip, folder);
                clip.warp = quantizedWarp (clip, TempoMap (c.tempo), length, detectOnsets (clipSourceFile (clip, folder), clip.offsetSeconds, length), options);
                if (clip.warp.empty())
                    continue;   // 옮길 어택이 없다
                clip.stretch = 1.0;   // 마커가 지금의 늘이기를 담는다
                if (const auto mode = a.value ("--mode"))
                    clip.warpSlices = *mode == "slice";
                if (clip.warpSlices)
                {
                    clip.pitchSemitones = 0.0;   // 자른 조각은 파일의 높이로 튼다
                    clip.pitchCorrection.reset();
                    clip.pitchEdits.clear();
                }
            }
            trackJson = withClip (trackJson, k, clip);
        }
        after.getProperty ("tracks", juce::var()).getArray()->set ((int) index, trackJson);
    }
    else if (op == "add-marker" || op == "remove-marker")
    {
        after = root.clone();
        if (op == "add-marker")
            after.getDynamicObject()->setProperty ("markers", withMarkerAdded (root, barArg ("--at", op), need ("--name", op)));
        else
        {
            if (a.positional.size() < 3)
                throw UsageError ("remove-marker needs the marker's name");
            const auto it = std::find_if (c.markers.begin(), c.markers.end(), [&] (const Marker& m) { return m.name == a.positional[2]; });
            if (it == c.markers.end())
                throw Failure ("invalid-input", "no marker named '" + a.positional[2] + "'");
            const auto markers = withMarkerRemoved (root, (size_t) (it - c.markers.begin()));
            if (markers.isVoid())
                after.getDynamicObject()->removeProperty ("markers");
            else
                after.getDynamicObject()->setProperty ("markers", markers);
        }
    }
    else if (op == "move-section" || op == "duplicate-section" || op == "delete-section")
    {
        // 구간: 번호 (1부터) 나 그 마커의 이름
        const auto sections = sectionsOf (root, folder);
        auto sectionArg = [&] (const std::string& text, bool allowEnd) -> size_t
        {
            if (allowEnd && text == "end")
                return sections.size();
            if (! text.empty() && std::all_of (text.begin(), text.end(), [] (char ch) { return std::isdigit ((unsigned char) ch); }))
            {
                const auto n = (size_t) std::stoul (text);
                if (n >= 1 && n <= sections.size())
                    return n - 1;
            }
            for (size_t i = 0; i < sections.size(); ++i)
                if (sections[i].name == text)
                    return i;
            throw Failure ("invalid-input", "no section '" + text + "' (sections are numbered from 1 or named by their markers)");
        };
        if (a.positional.size() < 3)
            throw UsageError (op + " needs a section (number from 1 or marker name)");
        const size_t section = sectionArg (a.positional[2], false);
        std::string reason;
        if (op == "move-section")
            after = withSectionMoved (root, section, sectionArg (need ("--before", op), true), &reason, folder);
        else if (op == "duplicate-section")
            after = withSectionDuplicated (root, section, &reason, folder);
        else
            after = withSectionDeleted (root, section, &reason, folder);
        if (after.isVoid())
            throw Failure ("invalid-input", reason);
    }
    else
        throw UsageError ("unknown arrange operation '" + op + "'");

    const auto output = a.value ("--output") ? fileArg (*a.value ("--output")) : source;
    auto edited = CompositionDocument::fromText (juce::String::fromUTF8 (writeJson (after).c_str()), output, error);
    if (! edited)
        throw Failure ("invalid-input", error);
    const auto check = edited->parse();
    if (! check.composition)
        throw Failure ("invalid-input", "the edited song does not validate", check.diagnostics.errors);
    if (! edited->save (output, error))
        throw Failure ("io", error);

    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", true);
    o->setProperty ("command", "arrange");
    o->setProperty ("operation", juce::String (op));
    o->setProperty ("output", output.getFullPathName());
    juce::Array<juce::var> changed, names, markers, sectionList;
    for (const auto& id : changedProperties (root, after))
        changed.add (id.toString());
    for (const auto& t : check.composition->tracks)
        names.add (juce::String::fromUTF8 (t.name.c_str()));
    const auto editedMeters = check.composition->meters();
    auto barNumber = [&editedMeters] (double beat)
    {
        return number (editedMeters.barAt (beat) + 1.0 + editedMeters.beatInBar (beat) / editedMeters.barBeatsAt (beat), 3);
    };
    for (const auto& m : check.composition->markers)
    {
        auto* mo = new juce::DynamicObject();
        mo->setProperty ("bar", barNumber (m.beat));
        mo->setProperty ("name", juce::String::fromUTF8 (m.name.c_str()));
        markers.add (juce::var (mo));
    }
    for (const auto& sec : sectionsOf (after, output.getParentDirectory()))
    {
        auto* so = new juce::DynamicObject();
        so->setProperty ("name", juce::String::fromUTF8 (sec.name.c_str()));
        so->setProperty ("firstBar", barNumber (sec.start));
        so->setProperty ("bars", editedMeters.barAt (sec.end) - editedMeters.barAt (sec.start));
        sectionList.add (juce::var (so));
    }
    o->setProperty ("changed", changed);
    o->setProperty ("tracks", names);
    o->setProperty ("markers", markers);
    o->setProperty ("sections", sectionList);
    emit (juce::var (o));
    return exitOk;
}

juce::File templateDirArg (const Args& a)
{
    if (auto dir = a.value ("--templates"))
        return fileArg (*dir);
    return defaultTemplateDirectory();
}

juce::var templateObject (const SongTemplate& t)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", juce::String (t.id));
    o->setProperty ("name", juce::String::fromUTF8 (t.name.c_str()));
    o->setProperty ("description", juce::String::fromUTF8 (t.description.c_str()));
    if (! t.source.empty())
        o->setProperty ("source", juce::String (t.source));
    if (! t.sourceTitle.empty())
        o->setProperty ("sourceTitle", juce::String::fromUTF8 (t.sourceTitle.c_str()));
    o->setProperty ("file", t.file.getFullPathName());
    o->setProperty ("bpm", number (t.bpm, 3));
    o->setProperty ("timeSignature", juce::String (t.beatsPerBar) + "/" + juce::String (t.beatUnit));
    o->setProperty ("targetLufs", t.targetLufs ? number (*t.targetLufs, 1) : juce::var());
    o->setProperty ("loop", t.loop);
    juce::Array<juce::var> tracks;
    for (size_t i = 0; i < t.trackNames.size(); ++i)
    {
        auto* tr = new juce::DynamicObject();
        tr->setProperty ("name", juce::String::fromUTF8 (t.trackNames[i].c_str()));
        tr->setProperty ("instrument", juce::String (t.instruments[i]));
        tracks.add (juce::var (tr));
    }
    o->setProperty ("tracks", tracks);
    return juce::var (o);
}

int cmdHarmony (const Args& a)
{
    const bool chords = a.command == "chords";
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", true);
    o->setProperty ("command", juce::String (a.command));
    juce::Array<juce::var> groups;
    for (const auto& g : chords ? chordGroups() : scaleGroups())
    {
        if (chords)
        {
            groups.add (juce::String (g));
            continue;
        }
        auto* go = new juce::DynamicObject();
        go->setProperty ("id", juce::String (g));
        go->setProperty ("name", juce::String (scaleGroupName (g)));
        groups.add (juce::var (go));
    }
    o->setProperty ("groups", groups);
    juce::Array<juce::var> list;
    auto intervalsOf = [] (const std::vector<int>& v)
    {
        juce::Array<juce::var> arr;
        for (const int i : v)
            arr.add (i);
        return arr;
    };
    if (chords)
        for (const auto& c : chordTypes())
        {
            auto* e = new juce::DynamicObject();
            e->setProperty ("id", juce::String (c.id));
            e->setProperty ("symbol", juce::String (chordSymbol (60, c)));
            e->setProperty ("group", juce::String (c.group));
            e->setProperty ("name", juce::String (c.name));
            e->setProperty ("intervals", intervalsOf (c.intervals));
            list.add (juce::var (e));
        }
    else
        for (const auto& sc : scaleTypes())
        {
            auto* e = new juce::DynamicObject();
            e->setProperty ("id", juce::String (sc.id));
            e->setProperty ("group", juce::String (sc.group));
            e->setProperty ("name", juce::String (sc.name));
            e->setProperty ("intervals", intervalsOf (sc.intervals));
            list.add (juce::var (e));
        }
    o->setProperty (chords ? "chords" : "scales", list);
    emit (juce::var (o));
    return exitOk;
}

int cmdTemplates (const Args& a)
{
    Diagnostics d;
    const auto dir = templateDirArg (a);
    const auto list = listTemplates (dir, d);
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", true);
    o->setProperty ("command", "templates");
    o->setProperty ("folder", dir.getFullPathName());
    juce::Array<juce::var> arr;
    for (const auto& t : list)
        arr.add (templateObject (t));
    o->setProperty ("templates", arr);
    o->setProperty ("warnings", diagnosticsArray (d.warnings));
    emit (juce::var (o));
    return exitOk;
}

int cmdNew (const Args& a)
{
    if (a.positional.size() != 1)
        throw UsageError ("new needs exactly one template id (music-render templates lists them)");
    const auto out = a.value ("--output");
    if (! out)
        throw UsageError ("new needs --output <song.json>");
    Diagnostics d;
    const auto list = listTemplates (templateDirArg (a), d);
    const auto it = std::find_if (list.begin(), list.end(), [&] (const SongTemplate& t) { return t.id == a.positional[0]; });
    if (it == list.end())
    {
        std::vector<std::string> ids;
        for (const auto& t : list)
            ids.push_back (t.id);
        const auto s = closestMatch (a.positional[0], ids);
        throw Failure ("invalid-input", "unknown template '" + a.positional[0] + "'" + (s.empty() ? std::string {} : " (did you mean '" + s + "'?)"));
    }
    const auto output = fileArg (*out);
    const auto title = a.value ("--title").value_or (output.getFileNameWithoutExtension().toStdString());
    createSongFromTemplate (*it, title, output);
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", true);
    o->setProperty ("command", "new");
    o->setProperty ("output", output.getFullPathName());
    o->setProperty ("title", juce::String::fromUTF8 (title.c_str()));
    o->setProperty ("template", templateObject (*it));
    emit (juce::var (o));
    return exitOk;
}

// 곡을 읽어 앱의 저장과 같은 형식으로 다시 쓴다. --check 는 쓰지 않고 바뀌는지만 알린다.
int cmdFormat (const Args& a)
{
    if (a.positional.size() != 1)
        throw UsageError ("format needs exactly one composition file");
    const auto source = fileArg (a.positional[0]);
    std::string error;
    auto doc = CompositionDocument::load (source, error);
    if (! doc)
        throw Failure ("invalid-input", error);
    juce::MemoryBlock bytes;
    source.loadFileAsData (bytes);
    const auto text = doc->toJson();
    const bool changed = text != std::string (static_cast<const char*> (bytes.getData()), bytes.getSize());
    const auto output = a.value ("--output") ? fileArg (*a.value ("--output")) : source;
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", true);
    o->setProperty ("command", "format");
    o->setProperty ("changed", changed);
    if (! a.flag ("--check"))
    {
        if (output != source || changed)
        {
            output.getParentDirectory().createDirectory();
            if (! doc->save (output, error))
                throw Failure ("io", error);
        }
        o->setProperty ("output", output.getFullPathName());
    }
    emit (juce::var (o));
    return exitOk;
}

int cmdInstruments (const Args& a)
{
    Composition empty;
    Diagnostics d;
    Registry registry = buildRegistry (empty, renderOptions (a), d);
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", d.ok());
    o->setProperty ("command", "instruments");
    juce::Array<juce::var> files;
    for (const auto& f : registry.files())
        files.add (f.getFullPathName());
    o->setProperty ("registries", files);
    juce::Array<juce::var> list;
    for (const auto& [name, def] : registry.all())
    {
        auto* e = new juce::DynamicObject();
        e->setProperty ("name", juce::String (name));
        e->setProperty ("type", juce::String (def.type));
        if (! def.preset.empty())
            e->setProperty ("preset", juce::String (def.preset));
        if (! def.fallback.empty())
            e->setProperty ("fallback", juce::String (def.fallback));
        if (def.file != juce::File())
        {
            e->setProperty ("file", def.file.getFullPathName());
            e->setProperty ("available", def.file.existsAsFile());
        }
        if (! def.profile.articulations.empty())
        {
            juce::Array<juce::var> arts;
            for (const auto& [n, a] : def.profile.articulations)
                arts.add (juce::String (n));
            e->setProperty ("articulations", arts);
            e->setProperty ("defaultArticulation", juce::String (def.profile.defaultArticulation));
        }
        if (! def.description.empty())
            e->setProperty ("description", juce::String (def.description));
        if (! def.keyNames.empty())
        {
            auto* names = new juce::DynamicObject();
            for (const auto& [key, text] : def.keyNames)
                names->setProperty (juce::String (key), juce::String::fromUTF8 (text.c_str()));
            e->setProperty ("keyNames", juce::var (names));
        }
        e->setProperty ("source", juce::String (def.path));
        list.add (juce::var (e));
    }
    o->setProperty ("instruments", list);
    o->setProperty ("errors", diagnosticsArray (d.errors));
    o->setProperty ("warnings", diagnosticsArray (d.warnings));
    emit (juce::var (o));
    return d.ok() ? exitOk : exitFailed;
}

std::string pluginFormatFor (const Args& a, const std::string& plugin)
{
    if (auto f = a.value ("--format"))
    {
        if (*f != "vst3" && *f != "au")
            throw UsageError ("--format must be vst3 or au");
        return *f;
    }
    if (plugin.rfind ("AudioUnit:", 0) == 0)
        return "au";
    return "vst3";
}

std::string pluginPathArg (const std::string& format, const std::string& plugin)
{
    return format == "vst3" ? fileArg (plugin).getFullPathName().toStdString() : plugin;
}

int cmdPlugins (const Args& a)
{
    if (a.positional.empty())
        throw UsageError ("plugins needs a subcommand: scan, describe or save-state");
    const auto sub = a.positional[0];
    std::vector<std::string> rest (a.positional.begin() + 1, a.positional.end());
    auto& host = PluginHost::get();

    if (sub == "scan")
    {
        std::vector<std::string> formats;
        if (auto f = a.value ("--format"))
            formats = { pluginFormatFor (a, "") };
        else if (! rest.empty())
            formats = { "vst3" };
        else
            formats = { "vst3", "au" };
        std::vector<juce::PluginDescription> all;
        std::vector<Diagnostic> problems;
        for (const auto& f : formats)
            if (host.supports (f))
            {
                auto found = host.scan (f, rest, problems);
                all.insert (all.end(), found.begin(), found.end());
            }
        const auto cache = a.value ("--cache") ? fileArg (*a.value ("--cache")) : defaultPluginCache();
        writePluginCache (cache, all);
        auto* o = new juce::DynamicObject();
        o->setProperty ("ok", true);
        o->setProperty ("command", "plugins scan");
        o->setProperty ("cache", cache.getFullPathName());
        juce::Array<juce::var> list;
        for (const auto& d : all)
            list.add (describeEntry (d));
        o->setProperty ("plugins", list);
        o->setProperty ("problems", diagnosticsArray (problems));
        emit (juce::var (o));
        return exitOk;
    }

    if (sub == "describe" || sub == "save-state")
    {
        if (rest.size() != 1)
            throw UsageError ("plugins " + sub + " needs exactly one plugin (a .vst3 path or an AudioUnit identifier)");
        const auto format = pluginFormatFor (a, rest[0]);
        const auto plugin = pluginPathArg (format, rest[0]);
        host.allowLocation (plugin);   // 명령줄에 직접 준 플러그인
        juce::PluginDescription chosen;
        std::string error;
        auto instance = host.load (format, plugin, a.value ("--name").value_or (""), 48000.0, 512, chosen, error);
        if (instance == nullptr)
            throw Failure ("instrument", error);

        PluginSettings settings;
        for (const auto& kv : a.values ("--set"))
        {
            const auto eq = kv.find ('=');
            if (eq == std::string::npos || eq == 0)
                throw UsageError ("--set needs <parameter>=<value>, got " + kv);
            const auto key = kv.substr (0, eq), value = kv.substr (eq + 1);
            char* end = nullptr;
            const double number = std::strtod (value.c_str(), &end);
            if (end != nullptr && *end == '\0' && ! value.empty())
                settings.parameters[key] = number;
            else
                settings.parameters[key] = juce::String::fromUTF8 (value.c_str());
        }
        if (auto p = a.value ("--program"))
        {
            try { settings.program = std::stoi (*p); }
            catch (...) { throw UsageError ("--program must be an integer"); }
        }
        std::vector<Diagnostic> problems;
        applyPluginSettings (*instance, settings, "$", problems);
        if (! problems.empty())
            throw Failure ("invalid-input", "cannot apply plugin settings", problems);
        instance->prepareToPlay (48000.0, 512);

        auto result = describeInstance (*instance, chosen);
        auto* o = result.getDynamicObject();
        o->setProperty ("ok", true);
        o->setProperty ("command", juce::String ("plugins " + sub));
        if (sub == "save-state")
        {
            const auto out = a.value ("--output");
            if (! out)
                throw UsageError ("plugins save-state needs --output <state.bin>");
            juce::MemoryBlock state;
            instance->getStateInformation (state);
            const auto file = fileArg (*out);
            file.getParentDirectory().createDirectory();
            if (! file.replaceWithData (state.getData(), state.getSize()))
                throw Failure ("io", "cannot write " + file.getFullPathName().toStdString());
            o->setProperty ("output", file.getFullPathName());
            o->setProperty ("stateBytes", (int) state.getSize());
        }
        instance->releaseResources();
        emit (result);
        return exitOk;
    }
    const auto s2 = closestMatch (sub, { "scan", "describe", "save-state" });
    throw UsageError ("unknown plugins subcommand '" + sub + "'" + (s2.empty() ? std::string {} : " (did you mean '" + s2 + "'?)"));
}

} // namespace

int main (int argc, char** argv)
{
    std::string command = "unknown";
    try
    {
        const auto args = parseArgs (argc, argv);
        command = args.command.empty() ? "help" : args.command;
        if (args.flag ("--quiet"))
            log::setLevel (log::Level::quiet);
        if (args.flag ("--verbose"))
            log::setLevel (log::Level::debug);

        if (args.command.empty() || args.command == "help" || args.flag ("--help"))
        {
            std::fputs (usageText, stderr);
            return args.command.empty() ? exitUsage : exitOk;
        }
        if (args.command == "version" || args.command == "--version")
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("ok", true);
            o->setProperty ("command", "version");
            o->setProperty ("version", MUSIC_RENDER_VERSION);
            o->setProperty ("juce", juce::String (JUCE_MAJOR_VERSION) + "." + juce::String (JUCE_MINOR_VERSION) + "." + juce::String (JUCE_BUILDNUMBER));
            emit (juce::var (o));
            return exitOk;
        }
        if (args.command == "validate") return cmdValidate (args);
        if (args.command == "render" || args.command == "plugins" || args.command == "realtime")
        {
            HostSession session;   // 플러그인을 쓸 수 있는 동안 살아 있고, 돌아가기 전에 정리된다
            if (args.command == "realtime")
                return cmdRealtime (args);
            return args.command == "render" ? cmdRender (args) : cmdPlugins (args);
        }
        if (args.command == "inspect") return cmdInspect (args);
        if (args.command == "video-info") return cmdVideoInfo (args);
        if (args.command == "video-frame") return cmdVideoFrame (args);
        if (args.command == "video-scenes") return cmdVideoScenes (args);
        if (args.command == "video-audio") return cmdVideoAudio (args);
        if (args.command == "video-pattern") return cmdVideoPattern (args);
        if (args.command == "export-midi") return cmdExportMidi (args);
        if (args.command == "export-musicxml") return cmdExportMusicXml (args);
        if (args.command == "sampler") return cmdSampler (args);
        if (args.command == "transcribe") return cmdTranscribe (args);
        if (args.command == "notes") return cmdNotes (args);
        if (args.command == "tempo") return cmdTempo (args);
        if (args.command == "slice") return cmdSlice (args);
        if (args.command == "groove") return cmdGroove (args);
        if (args.command == "beats") return cmdBeats (args);
        if (args.command == "track-template") return cmdTrackTemplate (args);
        if (args.command == "import-midi") return cmdImportMidi (args);
        if (args.command == "arrange") return cmdArrange (args);
        if (args.command == "format") return cmdFormat (args);
        if (args.command == "instruments") return cmdInstruments (args);
        if (args.command == "templates") return cmdTemplates (args);
        if (args.command == "chords" || args.command == "scales") return cmdHarmony (args);
        if (args.command == "new") return cmdNew (args);

        const auto s = closestMatch (args.command, { "validate", "render", "realtime", "inspect", "video-info", "video-frame", "video-scenes",
                                                     "video-audio", "video-pattern", "export-midi", "export-musicxml", "sampler", "transcribe", "notes", "tempo", "beats", "slice", "groove", "track-template", "instruments", "templates", "new", "chords", "scales",
                                                     "arrange", "import-midi", "format", "plugins", "version", "help" });
        throw UsageError ("unknown command '" + args.command + "'" + (s.empty() ? std::string {} : " (did you mean '" + s + "'?)"));
    }
    catch (const UsageError& e)
    {
        emit (failureObject (command, "usage", e.what(), {}));
        std::fprintf (stderr, "%s\n%s", e.what(), usageText);
        return exitUsage;
    }
    catch (const Failure& e)
    {
        emit (failureObject (command, e.code(), e.what(), e.details()));
        log::warn (e.what());
        for (const auto& d : e.details())
            log::warn ("  " + d.path + ": " + d.message + (d.hint.empty() ? std::string {} : " (" + d.hint + ")"));
        return exitFailed;
    }
    catch (const std::exception& e)
    {
        emit (failureObject (command, "internal", e.what(), {}));
        log::warn (std::string ("internal error: ") + e.what());
        return exitFailed;
    }
}
