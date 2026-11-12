#include "Grooves.h"

#include <map>

namespace daw
{

std::vector<mr::Groove> availableGrooves (const juce::File& folder)
{
    auto list = mr::factoryGrooves();
    if (folder == juce::File() || ! folder.isDirectory())
        return list;
    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.json");
    files.sort();
    for (const auto& f : files)
    {
        std::string error;
        if (auto g = mr::grooveFromJson (juce::JSON::parse (f.loadFileAsString()), error))
        {
            g->id.clear();
            if (g->name.empty())
                g->name = f.getFileNameWithoutExtension().toStdString();
            list.push_back (*g);
        }
    }
    return list;
}

bool saveGroove (const juce::File& folder, mr::Groove groove, const juce::String& name, juce::String& error)
{
    const auto file = juce::File::createLegalFileName (name.trim());
    if (file.isEmpty())
    {
        error = juce::String::fromUTF8 ("이름이 비어 있습니다.");
        return false;
    }
    groove.id.clear();
    groove.name = name.trim().toStdString();
    if (! folder.createDirectory() || ! folder.getChildFile (file + ".json").replaceWithText (juce::JSON::toString (mr::grooveJson (groove))))
    {
        error = juce::String::fromUTF8 ("그루브를 쓰지 못했습니다: ") + folder.getChildFile (file + ".json").getFullPathName();
        return false;
    }
    return true;
}

juce::String grooveName (const mr::Groove& g)
{
    static const std::map<std::string, const char*> names {
        { "swing-16-54", "16분음표 스윙 54%" }, { "swing-16-58", "16분음표 스윙 58%" }, { "swing-16-62", "16분음표 스윙 62%" },
        { "swing-16-66", "16분음표 스윙 66%" }, { "swing-8-55", "8분음표 스윙 55%" }, { "swing-8-60", "8분음표 스윙 60%" },
        { "swing-8-67", "8분음표 스윙 67% (셋잇단)" }, { "laid-back", "느긋하게 (엇박이 늦다)" }, { "push", "서두르게 (엇박이 이르다)" },
        { "backbeat-accent", "2, 4박 강세" },
    };
    if (const auto it = names.find (g.id); it != names.end())
        return juce::String::fromUTF8 (it->second);
    return juce::String::fromUTF8 (g.name.c_str());
}

} // namespace daw
