// 플러그인에 줄 설정: 상태, 프로그램, 매개변수. 레지스트리 항목과 트랙(곡)이 함께 쓴다.
#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <optional>
#include <string>

namespace mr
{

struct PluginSettings
{
    std::map<std::string, juce::var> parameters;   // 이름(또는 ID) → 0..1 숫자, 또는 매개변수의 글 ("-12 dB")
    std::optional<int> program;
    juce::File stateFile;
    std::string stateBase64;

    bool empty() const noexcept { return parameters.empty() && ! program && stateFile == juce::File() && stateBase64.empty(); }
};

// 플러그인 상태의 글 표기: 표준 base64 (RFC 4648). 파이썬 생성기의 base64 모듈과 같다.
inline std::string encodePluginState (const juce::MemoryBlock& state)
{
    return juce::Base64::toBase64 (state.getData(), state.getSize()).toStdString();
}

inline bool decodePluginState (const std::string& text, juce::MemoryBlock& state)
{
    state.reset();
    juce::MemoryOutputStream out (state, false);
    const bool ok = juce::Base64::convertFromBase64 (out, juce::String::fromUTF8 (text.c_str()));
    out.flush();
    return ok;
}

} // namespace mr
