// macOS: 자판 연주에 쓰는 자판을 앱의 자판 처리(JUCE)보다 먼저 받는다 (NSEvent 지역 모니터).
// 물리 위치(가상 키 코드)로 읽어 한글 입력기와 상관없고, 연주에 쓴 이벤트는 앱에 넘기지 않아 쓰지 않은 자판의 경고음이 나지 않는다.
// ⌘, ⌃, ⌥ 와 함께 누른 자판, 다른 창(플러그인 화면, 대화상자)의 자판은 그대로 넘긴다.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

namespace daw
{

class NativeKeys final
{
public:
    // handler (글자, 누름, 되풀이): true 면 그 이벤트를 삼킨다. owner 의 창에 온 자판만 묻는다.
    using Handler = std::function<bool (int code, bool down, bool repeat)>;
    NativeKeys (juce::Component& owner, Handler handler);
    ~NativeKeys();

    static bool available();   // 이 운영체제에서 쓸 수 있는가

private:
    juce::Component& owner_;
    Handler handler_;
    void* monitor_ = nullptr;
};

} // namespace daw
