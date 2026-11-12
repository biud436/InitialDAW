#include "instruments/Registry.h"

#include "composition/Composition.h"
#include "core/Json.h"
#include "instruments/AudioClips.h"
#include "instruments/SfzInstrument.h"
#include "instruments/SimpleSynth.h"

#include <cstdlib>
#include <set>

#ifndef MUSIC_RENDER_DEFAULT_REGISTRY
 #define MUSIC_RENDER_DEFAULT_REGISTRY ""
#endif

namespace mr
{

namespace
{

const std::vector<std::string> entryKeys { "type", "preset", "file", "plugin", "pluginName", "parameters", "program", "state", "stateFile",
                                           "articulations", "defaultArticulation", "controllers", "dynamics", "sfzRegionOpcodes",
                                           "gainDb", "transpose", "keymap", "keyNames", "fallback", "description", "comment" };
const std::vector<std::string> articulationKeys { "keyswitch", "keyswitchVelocity", "file", "program", "cc", "lengthFactor", "velocityOffset", "legato", "comment" };
const std::vector<std::string> dynamicsKeys { "controller", "gainDb", "brightnessHz", "layers", "comment" };

std::optional<int> readControllerNumber (const juce::var& v, const std::string& path, Diagnostics& d)
{
    if (v.isString())
    {
        auto id = parseControllerName (v.toString().toStdString());
        if (id && *id < 128)
            return id;
        d.error (path, "expected a controller like \"cc1\"");
        return std::nullopt;
    }
    if (auto n = json::readInteger (v, path, d, 0, 127))
        return (int) *n;
    return std::nullopt;
}

void readProfile (json::ObjectReader& r, InstrumentDef& def, const juce::File& base, Diagnostics& d)
{
    auto& p = def.profile;
    if (const auto* dyn = r.object ("dynamics", false))
    {
        json::ObjectReader dr (*dyn, r.pathOf ("dynamics"), d);
        dr.known ("comment");
        if (const auto* c = dr.get ("controller"))
            if (auto n = readControllerNumber (*c, dr.pathOf ("controller"), d))
                p.dynamics.controller = *n;
        if (auto* g = dr.array ("gainDb", false))
        {
            if (g->size() != 2)
                d.error (dr.pathOf ("gainDb"), "gainDb must be [atValue0, atValue127]");
            else
            {
                p.dynamics.gainDbLow = json::readNumber ((*g)[0], dr.pathOf ("gainDb") + "[0]", d, -96.0, 24.0).value_or (-24.0);
                p.dynamics.gainDbHigh = json::readNumber ((*g)[1], dr.pathOf ("gainDb") + "[1]", d, -96.0, 24.0).value_or (0.0);
            }
        }
        if (auto* b = dr.array ("brightnessHz", false))
        {
            if (b->size() != 2)
                d.error (dr.pathOf ("brightnessHz"), "brightnessHz must be [atValue0, atValue127]");
            else
            {
                p.dynamics.brightnessLowHz = json::readNumber ((*b)[0], dr.pathOf ("brightnessHz") + "[0]", d, 20.0, 24000.0);
                p.dynamics.brightnessHighHz = json::readNumber ((*b)[1], dr.pathOf ("brightnessHz") + "[1]", d, 20.0, 24000.0);
            }
        }
        if (auto* l = dr.array ("layers", false))
            for (int i = 0; i < l->size(); ++i)
                if (auto v = json::readInteger ((*l)[i], json::indexPath (dr.pathOf ("layers"), (size_t) i), d, 1, 127))
                    p.dynamics.layers.push_back ((int) *v);
        dr.warnUnknown (dynamicsKeys);
    }

    if (const auto* ctl = r.object ("controllers", false))
    {
        json::ObjectReader cr (*ctl, r.pathOf ("controllers"), d);
        for (const auto& key : cr.keys())
        {
            auto id = parseControllerName (key);
            const auto modeText = cr.string (key, true).value_or ("");
            if (! id)
            {
                d.error (cr.pathOf (key), "unknown controller '" + key + "'", "use cc0..cc127, pitchBend or aftertouch");
                continue;
            }
            ControllerMode mode;
            if (modeText == "native") mode = ControllerMode::native;
            else if (modeText == "engine") mode = ControllerMode::engine;
            else if (modeText == "both") mode = ControllerMode::both;
            else if (modeText == "ignore") mode = ControllerMode::ignore;
            else
            {
                d.error (cr.pathOf (key), "unknown controller mode '" + modeText + "'", "use native, engine, both or ignore");
                continue;
            }
            if ((mode == ControllerMode::engine || mode == ControllerMode::both) && ! engineCanHandle (*id, p))
            {
                d.error (cr.pathOf (key), "the engine cannot process " + key, "the engine handles cc7 (volume), cc10 (pan), cc11 (expression) and the dynamics controller");
                continue;
            }
            p.controllers[*id] = mode;
        }
    }

    if (const auto* arts = r.object ("articulations", false))
    {
        json::ObjectReader ar (*arts, r.pathOf ("articulations"), d);
        for (const auto& name : ar.keys())
        {
            json::ObjectReader a (*ar.get (name), ar.pathOf (name), d);
            if (! a.ok())
                continue;
            a.known ("comment");
            ArticulationDef art;
            art.name = name;
            if (auto ks = a.integer ("keyswitch", false, 0, 127))
                art.keyswitch = (int) *ks;
            art.keyswitchVelocity = (int) a.integer ("keyswitchVelocity", false, 1, 127).value_or (100);
            if (auto f = a.string ("file", false))
                art.file = resolveRegistryPath (*f, base);
            if (auto prog = a.integer ("program", false, 0, 127))
                art.program = (int) *prog;
            if (const auto* cc = a.object ("cc", false))
            {
                json::ObjectReader ccr (*cc, a.pathOf ("cc"), d);
                for (const auto& key : ccr.keys())
                {
                    auto id = parseControllerName (key);
                    auto v = ccr.integer (key, true, 0, 127);
                    if (! id || *id > 127)
                        d.error (ccr.pathOf (key), "expected a controller like \"cc32\"");
                    else if (v)
                        art.ccs.push_back ({ *id, (int) *v });
                }
            }
            art.lengthFactor = a.number ("lengthFactor", false, 0.05, 4.0).value_or (1.0);
            art.velocityOffset = (int) a.integer ("velocityOffset", false, -64, 64).value_or (0);
            art.legato = a.boolean ("legato", false).value_or (name == "legato");
            a.warnUnknown (articulationKeys);
            if (art.file != juce::File() && def.type != "sfz")
                d.error (a.pathOf ("file"), "articulation files are only for sfz instruments", "use keyswitch, program or cc for plugins");
            p.articulations[name] = art;
        }
    }
    if (auto da = r.string ("defaultArticulation", false))
    {
        p.defaultArticulation = *da;
        if (p.articulations.count (*da) == 0)
        {
            std::vector<std::string> names;
            for (const auto& [n, a] : p.articulations)
                names.push_back (n);
            const auto s = closestMatch (*da, names);
            d.error (r.pathOf ("defaultArticulation"), "no articulation named '" + *da + "'", s.empty() ? std::string {} : "did you mean '" + s + "'?");
        }
    }
    else if (! p.articulations.empty())
        p.defaultArticulation = p.articulations.count ("sustain") > 0 ? "sustain" : p.articulations.begin()->first;
}
const std::vector<std::string> knownTypes { "synth", "sfz", "vst3", "au" };

} // namespace

namespace
{

// 내장 드럼 신스의 건반 이름 (SimpleSynth 의 배치와 같다)
std::map<int, std::string> builtInKeyNames (const std::string& preset)
{
    if (preset == "drums" || preset == "edm-drums")
        return { { 35, "킥 2" }, { 36, "킥" }, { 37, "사이드 스틱" }, { 38, "스네어" }, { 39, "클랩" }, { 40, "스네어 2" },
                 { 41, "플로어 탐 2" }, { 42, "닫힌 하이햇" }, { 43, "플로어 탐" }, { 44, "페달 하이햇" }, { 45, "탐 4" }, { 46, "열린 하이햇" },
                 { 47, "탐 3" }, { 48, "탐 2" }, { 49, "크래시" }, { 50, "탐 1" }, { 51, "라이드" }, { 52, "차이나" }, { 53, "라이드 벨" },
                 { 55, "스플래시" }, { 57, "크래시 2" }, { 59, "라이드 2" } };
    if (preset == "trailer-drums")
        return { { 35, "붐" }, { 36, "타이코" }, { 38, "타격" }, { 39, "클랩" }, { 40, "타격 2" }, { 41, "낮은 북" }, { 43, "가운데 북" },
                 { 44, "울림 끊기" }, { 45, "높은 북" }, { 47, "높은 북 2" }, { 56, "앤빌" } };
    if (preset == "club-drums")
        return { { 35, "긴 킥" }, { 36, "킥" }, { 38, "스네어" }, { 39, "클랩" }, { 40, "롤 스네어" }, { 41, "탐 6" }, { 42, "닫힌 하이햇" },
                 { 43, "탐 5" }, { 44, "페달 하이햇" }, { 45, "탐 4" }, { 46, "열린 하이햇" }, { 47, "탐 3" }, { 48, "탐 2" }, { 49, "크래시" },
                 { 50, "탐 1" }, { 51, "라이드" }, { 60, "임팩트" }, { 62, "다운리프터 4박" }, { 63, "다운리프터 8박" }, { 64, "상승음 4박" },
                 { 65, "상승음 8박" }, { 66, "상승음 16박" }, { 67, "역방향 심벌 1박" }, { 68, "역방향 심벌 2박" }, { 69, "역방향 심벌 4박" } };
    return {};
}

std::string builtInDescription (const std::string& preset)
{
    if (preset == "club-drums")
        return "built-in synth drums for club music: 36 kick (A1 body at tune 0, gone in 0.36 s), 35 long kick (1.35 s), 38 snare, 40 roll snare "
               "(0.11 s), 39 layered clap, 42 closed hi-hat, 44 pedal hi-hat, 46 open hi-hat, 49 crash, 51 ride, 41-50 toms, 60 impact (E1 sub boom, "
               "3.6 s), 62/63 downlifter 4/8 beats, 64/65/66 noise riser 4/8/16 beats, 67/68/69 reverse cymbal 1/2/4 beats (beat lengths follow the "
               "tempo map and end on the beat). Track synth values: tune (semitones), bendRange (pitch bend semitones), levelDb";
    if (preset == "808")
        return "built-in 808 bass: a sine that starts at phase 0, drops an octave into the note in 0.06 s (pitchEnv, pitchEnvDecay), decays "
               "for 4 s to a quarter and is saturated (drive). Mono: a note that overlaps a held note glides to it in glide seconds without a new "
               "attack, a note after a gap cuts the previous one";
    if (preset == "edm-drums")
        return "built-in synth drums: 36 kick (46 Hz), 38 snare, 39 clap, 42/46 hi-hats, 49 crash, 51 ride. tune and pitch bend move every drum";
    if (preset == "trailer-drums")
        return "built-in trailer drums: 35 boom, 36 taiko, 38 hit, 41/43/45 drums, 56 anvil, 39 clap, 44 chokes everything. tune and pitch bend move every drum";
    return "built-in synth preset";
}

} // namespace

Registry::Registry()
{
    {
        InstrumentDef d;   // 오디오 트랙: 트랙의 clips 를 튼다
        d.name = "audio";
        d.type = "audio";
        d.description = "audio clips";
        d.path = "<built-in>";
        defs_[d.name] = d;
    }
    for (const auto& preset : simpleSynthPresets())
    {
        InstrumentDef d;
        d.name = "synth." + preset;
        d.type = "synth";
        d.preset = preset;
        d.description = builtInDescription (preset);
        d.path = "<built-in>";
        d.keyNames = builtInKeyNames (preset);
        defs_[d.name] = d;
    }
}

void Registry::addFile (const juce::File& file, Diagnostics& d)
{
    const std::string fileLabel = file.getFullPathName().toStdString();
    if (! file.existsAsFile())
    {
        d.error (fileLabel, "instrument registry not found");
        return;
    }
    juce::var root;
    const auto parsed = juce::JSON::parse (file.loadFileAsString(), root);
    if (parsed.failed())
    {
        d.error (fileLabel, describeJsonError (parsed.getErrorMessage()));
        return;
    }
    files_.push_back (file);
    addRoot (root, file, fileLabel + ":$", d);
}

void Registry::addInline (const juce::var& table, const juce::File& songFile, const std::string& path, Diagnostics& d)
{
    addRoot (table, songFile, songFile.getFileName().toStdString() + ":" + path, d);
}

void Registry::addRoot (const juce::var& root, const juce::File& file, const std::string& rootPath, Diagnostics& d)
{
    json::ObjectReader top (root, rootPath, d);
    if (! top.ok())
        return;
    for (const auto& k : { "$schema", "comment", "description" })
        top.known (k);

    // {"instruments": {...}} 또는 이름에서 정의로 바로
    const juce::var* table = &root;
    std::string base = rootPath;
    if (top.has ("instruments"))
    {
        table = top.object ("instruments", true);
        base += ".instruments";
        top.warnUnknown ({ "instruments", "$schema", "comment", "description" });
        if (table == nullptr)
            return;
    }

    json::ObjectReader entries (*table, base, d);
    if (! entries.ok())
        return;
    for (const auto& name : entries.keys())
    {
        if (table == &root && (name == "$schema" || name == "comment" || name == "description"))
            continue;
        const auto path = base + "." + name;
        json::ObjectReader r (*entries.get (name), path, d);
        if (! r.ok())
            continue;
        r.known ("comment");
        InstrumentDef def;
        def.name = name;
        def.path = path;
        def.registryFile = file;
        def.type = r.string ("type", true).value_or ("");
        def.preset = r.string ("preset", false).value_or ("");
        def.gainDb = r.number ("gainDb", false, -60.0, 24.0).value_or (0.0);
        def.transpose = (int) r.integer ("transpose", false, -48, 48).value_or (0);
        if (const auto* keymap = r.object ("keymap", false))
        {
            json::ObjectReader kr (*keymap, r.pathOf ("keymap"), d);
            for (const auto& key : kr.keys())
            {
                const auto from = juce::String (key).trim();
                const int fromKey = from.getIntValue();
                if (! from.containsOnly ("0123456789") || from.isEmpty() || fromKey > 127)
                {
                    d.error (r.pathOf ("keymap") + "." + key, "keymap keys are MIDI note numbers 0..127", "e.g. \"keymap\": { \"36\": 48 }");
                    continue;
                }
                if (const auto to = kr.integer (key, true, 0, 127))
                    def.keymap[fromKey] = (int) *to;
            }
        }
        if (const auto* names = r.object ("keyNames", false))
        {
            json::ObjectReader nr (*names, r.pathOf ("keyNames"), d);
            for (const auto& key : nr.keys())
            {
                const auto k = juce::String (key).trim();
                if (! k.containsOnly ("0123456789") || k.isEmpty() || k.getIntValue() > 127)
                {
                    d.error (r.pathOf ("keyNames") + "." + key, "keyNames keys are MIDI note numbers 0..127", "e.g. \"keyNames\": { \"36\": \"킥\" }");
                    continue;
                }
                if (const auto name = nr.string (key, true))
                    def.keyNames[k.getIntValue()] = *name;
            }
        }
        def.fallback = r.string ("fallback", false).value_or ("");
        def.description = r.string ("description", false, true).value_or ("");
        def.plugin = r.string ("plugin", false).value_or ("");
        def.pluginName = r.string ("pluginName", false).value_or ("");
        if (const auto* params = r.object ("parameters", false))
        {
            json::ObjectReader pr (*params, r.pathOf ("parameters"), d);
            for (const auto& key : pr.keys())
            {
                const auto* v = pr.get (key);
                if (def.type == "synth")
                {
                    // 내장 신스: 프리셋 값을 덮는 이름 붙인 소리
                    const auto* p = findSynthParameter (key);
                    if (p == nullptr)
                        d.error (pr.pathOf (key), "unknown synth parameter '" + key + "'", "did you mean '" + closestMatch (key, synthParameterKeys()) + "'?");
                    else if (auto n = readSynthValue (pr, key, *p, d))
                        def.synthParameters[key] = *n;
                }
                else if (v->isString() || json::isNumber (*v))
                    def.pluginSettings.parameters[key] = *v;
                else
                    d.error (pr.pathOf (key), "parameter value must be a number 0..1 or text, got " + json::typeName (*v));
            }
        }
        if (auto program = r.integer ("program", false, 0, 100000))
            def.pluginSettings.program = (int) *program;
        def.pluginSettings.stateBase64 = r.string ("state", false).value_or ("");
        if (auto stateFile = r.string ("stateFile", false))
            def.pluginSettings.stateFile = resolveRegistryPath (*stateFile, file.getParentDirectory());
        if (auto sfz = r.string ("file", false))
            def.file = resolveRegistryPath (*sfz, file.getParentDirectory());
        def.sfzRegionOpcodes = r.string ("sfzRegionOpcodes", false, true).value_or ("");
        if (! def.sfzRegionOpcodes.empty() && def.type != "sfz")
            d.error (path + ".sfzRegionOpcodes", "sfzRegionOpcodes is only for sfz instruments");
        if (def.sfzRegionOpcodes.find ('<') != std::string::npos)
            d.error (path + ".sfzRegionOpcodes", "give opcodes only (name=value), not SFZ headers");
        readProfile (r, def, file.getParentDirectory(), d);
        r.warnUnknown (entryKeys);

        if (def.type == "sfz")
        {
            bool allFiles = ! def.profile.articulations.empty();
            for (const auto& [n, a] : def.profile.articulations)
                allFiles = allFiles && a.file != juce::File();
            if (def.file == juce::File() && ! allFiles)
            {
                d.error (path, "an sfz instrument needs \"file\" (or a file for every articulation)");
                continue;
            }
        }
        else if (def.file != juce::File())
            d.error (path + ".file", "\"file\" is only for sfz instruments");

        if ((def.type == "vst3" || def.type == "au") && def.plugin.empty() && def.pluginName.empty())
        {
            d.error (path, "a " + def.type + " instrument needs \"plugin\" (path or identifier) or \"pluginName\" (from `music-render plugins scan`)");
            continue;
        }
        if (def.type == "vst3" && ! def.plugin.empty())
            def.plugin = resolveRegistryPath (def.plugin, file.getParentDirectory()).getFullPathName().toStdString();

        if (! def.type.empty() && std::find (knownTypes.begin(), knownTypes.end(), def.type) == knownTypes.end())
        {
            const auto s = closestMatch (def.type, knownTypes);
            d.error (path + ".type", "unknown instrument type '" + def.type + "'", s.empty() ? "use synth, sfz, vst3 or au" : "did you mean '" + s + "'?");
            continue;
        }
        if (def.type == "synth")
        {
            const auto presetsList = simpleSynthPresets();
            if (std::find (presetsList.begin(), presetsList.end(), def.preset) == presetsList.end())
            {
                const auto s = closestMatch (def.preset, presetsList);
                d.error (path + ".preset", "unknown synth preset '" + def.preset + "'",
                         s.empty() ? "available: sine, piano, pad, strings, bass, pluck, drums" : "did you mean '" + s + "'?");
                continue;
            }
        }
        defs_[name] = def;   // 뒤에 읽은 파일이 앞의 정의를 덮는다
    }
}

const InstrumentDef* Registry::find (const std::string& name) const
{
    auto it = defs_.find (name);
    return it == defs_.end() ? nullptr : &it->second;
}

std::vector<std::string> Registry::names() const
{
    std::vector<std::string> out;
    for (const auto& [name, def] : defs_)
        out.push_back (name);
    return out;
}

juce::File resolveRegistryPath (const std::string& raw, const juce::File& base)
{
    std::string out;
    for (size_t i = 0; i < raw.size(); ++i)
    {
        if (raw[i] == '$' && i + 1 < raw.size() && raw[i + 1] == '{')
        {
            const auto close = raw.find ('}', i + 2);
            if (close != std::string::npos)
            {
                std::string inner = raw.substr (i + 2, close - i - 2), fallback;
                if (auto dash = inner.find (":-"); dash != std::string::npos)
                {
                    fallback = inner.substr (dash + 2);
                    inner = inner.substr (0, dash);
                }
                const char* env = std::getenv (inner.c_str());
                out += (env != nullptr && *env != '\0') ? std::string (env) : fallback;
                i = close;
                continue;
            }
        }
        out += raw[i];
    }
    const auto s = juce::String::fromUTF8 (out.c_str());
    if (s.startsWith ("~/"))
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (s.substring (2));
    return base.getChildFile (s);
}

juce::File defaultRegistryFile()
{
    if (const char* env = std::getenv ("MUSIC_RENDER_REGISTRY"); env != nullptr && *env != '\0')
        return juce::File (juce::String::fromUTF8 (env));
    return juce::File (juce::String::fromUTF8 (MUSIC_RENDER_DEFAULT_REGISTRY));
}

namespace
{

std::unique_ptr<Instrument> instantiate (const InstrumentDef& def, const InstrumentContext& ctx, std::string& error)
{
    if (def.type == "synth")
        return makeSimpleSynth (def.preset, ctx.seed, error, &def.synthParameters, ctx.trackSynth, ctx.tempo);
    if (def.type == "vst3" || def.type == "au")
    {
        std::string plugin = def.plugin;
        if (plugin.empty())
        {
            const auto cache = ctx.pluginCache == juce::File() ? defaultPluginCache() : ctx.pluginCache;
            auto found = findCachedPlugin (cache, def.type, def.pluginName);
            if (! found)
            {
                error = "plugin '" + def.pluginName + "' is not in the plugin cache " + cache.getFullPathName().toStdString()
                        + " (run `music-render plugins scan` first)";
                return nullptr;
            }
            plugin = found->fileOrIdentifier.toStdString();
        }
        PluginHostContext pctx { ctx.sampleRate, ctx.blockSize, ctx.tempo, ctx.beatsPerBar, ctx.beatUnit, ctx.realtime };
        pctx.meters = ctx.meters;
        return makePluginInstrument (def.type, plugin, def.pluginName, def.pluginSettings, pctx, def.path, error,
                                     ctx.trackPlugin != nullptr && ! ctx.trackPlugin->empty() ? ctx.trackPlugin : nullptr, ctx.trackPluginPath);
    }
    if (def.type == "sfz")
        return makeSfzInstrument (def, ctx, error);
    if (def.type == "audio")
    {
        static const std::vector<AudioClip> none;
        static const TempoMap defaultTempo ({ TempoPoint { 0.0, 120.0, false } });
        std::vector<std::string> ignored;
        return std::make_unique<AudioClipPlayer> (ctx.trackClips != nullptr ? *ctx.trackClips : none, ctx.tempo != nullptr ? *ctx.tempo : defaultTempo,
                                                  ctx.songFolder, ctx.sampleRate, ctx.warnings != nullptr ? *ctx.warnings : ignored);
    }
    error = "instrument type '" + def.type + "' is not available in this build yet";
    return nullptr;
}

} // namespace

CreatedInstrument createInstrument (const Registry& registry, const std::string& name, const InstrumentContext& ctx)
{
    CreatedInstrument out;
    std::set<std::string> visited;
    std::string current = name;
    std::vector<Diagnostic> chain;
    while (true)
    {
        const auto* def = registry.find (current);
        if (def == nullptr)
        {
            const auto suggestion = closestMatch (current, registry.names());
            std::string message = "unknown instrument '" + current + "'";
            if (current != name)
                message += " (fallback of '" + name + "')";
            chain.push_back ({ "instrument", message, suggestion.empty() ? "run `music-render instruments` to list them" : "did you mean '" + suggestion + "'?" });
            throw Failure ("instrument", message, chain);
        }
        std::string error;
        auto inst = instantiate (*def, ctx, error);
        if (inst != nullptr)
        {
            inst->prepare (ctx.sampleRate, ctx.blockSize);
            out.instrument = std::move (inst);
            out.def = def;
            return out;
        }
        chain.push_back ({ def->path, "cannot create instrument '" + current + "': " + error, {} });
        if (def->fallback.empty() || ! visited.insert (current).second)
            throw Failure ("instrument", "cannot create instrument '" + name + "': " + error, chain);
        out.notes.push_back ("instrument '" + current + "' unavailable (" + error + "), using fallback '" + def->fallback + "'");
        current = def->fallback;
    }
}

} // namespace mr
