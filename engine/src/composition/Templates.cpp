#include "composition/Templates.h"

#include "composition/Composition.h"
#include "composition/Document.h"

#include <algorithm>
#include <cstdlib>

namespace mr
{

juce::File defaultTemplateDirectory()
{
    if (const char* env = std::getenv ("MUSIC_RENDER_TEMPLATES"); env != nullptr && *env != '\0')
        return juce::File (juce::String::fromUTF8 (env));
    return juce::File (juce::String::fromUTF8 (MUSIC_RENDER_DEFAULT_TEMPLATES));
}

std::vector<SongTemplate> listTemplates (const juce::File& directory, Diagnostics& d)
{
    std::vector<SongTemplate> list;
    if (! directory.isDirectory())
    {
        d.warning ("templates", "no template folder at " + directory.getFullPathName().toStdString());
        return list;
    }
    for (const auto& file : directory.findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        const auto path = file.getFileName().toStdString();
        auto parsed = loadComposition (file);
        if (! parsed.composition)
        {
            d.warning (path, "not a valid song: " + (parsed.diagnostics.errors.empty() ? std::string ("unreadable") : parsed.diagnostics.errors[0].message));
            continue;
        }
        const auto& c = *parsed.composition;
        juce::var root;
        juce::JSON::parse (file.loadFileAsString(), root);
        const auto info = root.getProperty ("metadata", {}).getProperty ("template", {});
        SongTemplate t;
        t.id = file.getFileNameWithoutExtension().toStdString();
        t.name = info.getProperty ("name", juce::String (t.id)).toString().toStdString();
        t.description = info.getProperty ("description", "").toString().toStdString();
        t.source = info.getProperty ("source", "").toString().toStdString();
        t.sourceTitle = info.getProperty ("sourceTitle", "").toString().toStdString();
        t.order = (int) info.getProperty ("order", 1000);
        t.file = file;
        t.bpm = c.tempo.empty() ? 120.0 : c.tempo.front().bpm;
        t.beatsPerBar = c.beatsPerBar;
        t.beatUnit = c.beatUnit;
        t.targetLufs = c.master.targetLufs;
        t.loop = c.loop.has_value();
        if (c.loop)
        {
            t.loopStartBeat = c.loop->startBeat;
            t.loopEndBeat = c.loop->endBeat;
        }
        for (const auto& track : c.tracks)
        {
            t.trackNames.push_back (track.name);
            t.instruments.push_back (track.instrument);
        }
        list.push_back (std::move (t));
    }
    std::sort (list.begin(), list.end(), [] (const SongTemplate& a, const SongTemplate& b)
               { return a.order != b.order ? a.order < b.order : a.id < b.id; });
    return list;
}

std::string songFromTemplate (const SongTemplate& t, const std::string& title)
{
    juce::var root;
    if (const auto r = juce::JSON::parse (t.file.loadFileAsString(), root); r.failed() || ! root.isObject())
        throw Failure ("io", "cannot read template " + t.file.getFullPathName().toStdString());
    auto* obj = root.getDynamicObject();
    obj->setProperty ("title", juce::String::fromUTF8 (title.c_str()));
    if (auto* metadata = obj->getProperty ("metadata").getDynamicObject())
        metadata->setProperty ("template", juce::String (t.id));
    return writeJson (root);
}

void createSongFromTemplate (const SongTemplate& t, const std::string& title, const juce::File& output)
{
    if (output.exists())
        throw Failure ("io", output.getFullPathName().toStdString() + " already exists; a new song never replaces a file");
    const auto text = songFromTemplate (t, title);
    if (const auto parsed = parseComposition (juce::String::fromUTF8 (text.c_str()), output); ! parsed.composition)
        throw Failure ("invalid-input", "the template does not make a valid song", parsed.diagnostics.errors);
    const auto parent = output.getParentDirectory();
    if (! parent.isDirectory() && ! parent.createDirectory())
        throw Failure ("io", "cannot create directory " + parent.getFullPathName().toStdString());
    if (! output.replaceWithText (juce::String::fromUTF8 (text.c_str()), false, false, "\n"))
        throw Failure ("io", "cannot write " + output.getFullPathName().toStdString());
}

} // namespace mr
