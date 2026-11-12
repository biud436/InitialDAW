#pragma once

#include "composition/Composition.h"

#include <juce_core/juce_core.h>

#include <string>

namespace mrtest
{

inline juce::File sourceDir() { return juce::File (MUSIC_RENDER_SOURCE_DIR); }

inline juce::File outputDir()
{
    juce::File d (MUSIC_RENDER_TEST_OUTPUT);
    d.createDirectory();
    return d;
}

inline mr::ParseResult parse (const std::string& json)
{
    return mr::parseComposition (juce::String::fromUTF8 (json.c_str()), sourceDir().getChildFile ("tests/inline.json"));
}

inline bool hasError (const mr::Diagnostics& d, const std::string& path, const std::string& fragment = {})
{
    for (const auto& e : d.errors)
        if (e.path == path && (fragment.empty() || e.message.find (fragment) != std::string::npos))
            return true;
    return false;
}

inline bool hasWarning (const mr::Diagnostics& d, const std::string& path, const std::string& hintFragment = {})
{
    for (const auto& w : d.warnings)
        if (w.path == path && (hintFragment.empty() || w.hint.find (hintFragment) != std::string::npos))
            return true;
    return false;
}

} // namespace mrtest
