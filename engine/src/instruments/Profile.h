// 악기 프로필: 아티큘레이션을 어떻게 바꾸는지, 컨트롤러를 누가 처리하는지, 다이내믹스를 어떻게 흉내 내는지.
// 악기마다 방식이 달라 코드에 박지 않고 레지스트리에 적는다.
#pragma once

#include "core/Diagnostics.h"

#include <juce_core/juce_core.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mr
{

struct ArticulationDef
{
    std::string name;
    std::optional<int> keyswitch;        // 이 음을 눌러 바꾼다 (바뀔 때만 보낸다)
    int keyswitchVelocity = 100;
    juce::File file;                     // sfz: 이 아티큘레이션의 SFZ 파일 (별도 악기로 불러 음을 보낸다)
    std::optional<int> program;          // 프로그램 체인지
    std::vector<std::pair<int, int>> ccs; // 음 앞에 보낼 CC 번호와 값
    double lengthFactor = 1.0;           // 길이 배율 (스타카토 0.4 등)
    int velocityOffset = 0;
    bool legato = false;                 // 음을 겹쳐 잇는다 (performance.legatoOverlapMs, 없으면 40ms)
};

// native: MIDI 로 악기에 보낸다. engine: 엔진이 처리한다 (cc7, cc11 = 게인, cc10 = 팬, 다이내믹스 컨트롤러 = 게인과 밝기).
// both: 둘 다. ignore: 버린다.
enum class ControllerMode { native, engine, both, ignore };

struct DynamicsSpec
{
    int controller = 1;                       // 다이내믹스를 정하는 CC
    double gainDbLow = -24.0, gainDbHigh = 0.0;   // 값 0 과 127 의 게인
    std::optional<double> brightnessLowHz, brightnessHighHz;   // 저역 통과 차단 주파수 (값 0 과 127)
    std::vector<int> layers;                  // sfz: 이 벨로시티들로 각 음을 함께 울리고 다이내믹스 값으로 크로스페이드한다
};

struct InstrumentProfile
{
    std::map<std::string, ArticulationDef> articulations;
    std::string defaultArticulation;
    std::map<int, ControllerMode> controllers;   // 적지 않은 컨트롤러는 defaultMode()
    DynamicsSpec dynamics;

    ControllerMode modeFor (int controller, const std::string& instrumentType) const;
    static ControllerMode defaultMode (int controller, const std::string& instrumentType, int dynamicsController);
};

std::string controllerModeName (ControllerMode m);

// 엔진이 처리할 수 있는 컨트롤러인가 (cc7, cc10, cc11, 다이내믹스 컨트롤러)
bool engineCanHandle (int controller, const InstrumentProfile& profile);

} // namespace mr
