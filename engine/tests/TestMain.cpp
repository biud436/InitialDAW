#include "TestFramework.h"

#include "plugins/PluginHost.h"

#include <cstring>
#include <exception>

int main (int argc, char** argv)
{
    mr::HostSession session;   // 플러그인 테스트용 JUCE 런타임. main 이 끝나기 전에 정리된다
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (const auto& c : mrtest::cases())
    {
        if (filter != nullptr && std::strstr (c.name, filter) == nullptr)
            continue;
        mrtest::state().current = c.name;
        const int before = mrtest::state().failures;
        try
        {
            c.fn();
        }
        catch (const mrtest::RequireFailed&)
        {
        }
        catch (const std::exception& e)
        {
            ++mrtest::state().failures;
            std::fprintf (stderr, "  FAIL %s: exception: %s\n", c.name, e.what());
        }
        ++run;
        std::fprintf (stderr, "%s %s\n", mrtest::state().failures == before ? "ok  " : "FAIL", c.name);
    }
    std::fprintf (stderr, "\n%d tests, %d checks, %d failures\n", run, mrtest::state().checks, mrtest::state().failures);
    return mrtest::state().failures == 0 ? 0 : 1;
}
