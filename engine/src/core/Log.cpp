#include "core/Log.h"

#include <cstdio>

namespace mr::log
{

static Level currentLevel = Level::info;

void setLevel (Level l) { currentLevel = l; }
Level level() { return currentLevel; }

void info (const std::string& message)
{
    if (currentLevel != Level::quiet)
        std::fprintf (stderr, "[music-render] %s\n", message.c_str());
}

void warn (const std::string& message)
{
    std::fprintf (stderr, "[music-render] warning: %s\n", message.c_str());
}

void debug (const std::string& message)
{
    if (currentLevel == Level::debug)
        std::fprintf (stderr, "[music-render] debug: %s\n", message.c_str());
}

} // namespace mr::log
