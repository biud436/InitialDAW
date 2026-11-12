// 매개변수 하나의 설명. 효과와 내장 신스가 같은 모양으로 내놓고, 검사와 앱의 화면이 같은 표를 쓴다.
#pragma once

#include <string>
#include <vector>

namespace mr
{

struct ParameterInfo
{
    std::string key;
    double minimum = 0.0;
    double maximum = 1.0;
    double defaultValue = 0.0;
    std::string unit;              // dB, Hz, ms, s, cents, semitones, ratio, percent(0..1 을 % 로), pan(-1..1 을 L, R 로), dB/oct, beats(4분음표 박), 비면 수만
    bool logarithmic = false;      // 주파수, 시간, 비율
    double skewMid = 0.0;          // 0 이 아니면 화면의 손잡이 가운데가 이 값 (0 을 포함하는 넓은 범위)
    bool toggle = false;           // 0 또는 1
    std::vector<double> choices;   // 이 값들만 (eq 의 기울기)
    std::vector<std::string> choiceNames;   // choices 의 이름: 곡에는 이름으로 쓴다 (내장 신스의 filterMode)
    bool optional = false;         // 없으면 꺼짐 (eq 의 차단) 또는 다른 값에서 정한다 (reverb 의 dryDb, wetDb)
    bool hidden = false;           // 화면에 내지 않는다 (reverb 의 seed)
    std::string mode;              // reverb: 이 방식에서만 쓴다 (비면 모든 방식)
    std::string section;           // 내장 신스: 패널의 단면 (oscillator, filter, amp, vibrato, pitch, output, decay)
    bool automatable = false;      // 효과: 효과 객체의 automation 에 곡선을 둘 수 있다
};

} // namespace mr
