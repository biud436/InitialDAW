#include "instruments/Profile.h"

#include "composition/Composition.h"

namespace mr
{

ControllerMode InstrumentProfile::defaultMode (int controller, const std::string& type, int dynamicsController)
{
    // 볼륨과 팬은 어느 악기든 엔진이 처리한다 (악기마다 해석이 달라 결과를 예측하기 어렵다)
    if (controller == 7 || controller == 10)
        return ControllerMode::engine;
    if (type == "sfz")
    {
        if (controller == dynamicsController)
            return ControllerMode::both;    // 레이어 크로스페이드(악기)와 게인, 밝기(엔진)
        if (controller == 11)
            return ControllerMode::engine;
    }
    return ControllerMode::native;
}

ControllerMode InstrumentProfile::modeFor (int controller, const std::string& type) const
{
    if (auto it = controllers.find (controller); it != controllers.end())
        return it->second;
    return defaultMode (controller, type, dynamics.controller);
}

std::string controllerModeName (ControllerMode m)
{
    switch (m)
    {
        case ControllerMode::native: return "native";
        case ControllerMode::engine: return "engine";
        case ControllerMode::both: return "both";
        case ControllerMode::ignore: return "ignore";
    }
    return "native";
}

bool engineCanHandle (int controller, const InstrumentProfile& profile)
{
    return controller == 7 || controller == 10 || controller == 11 || controller == profile.dynamics.controller;
}

} // namespace mr
