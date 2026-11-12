// 컴퓨터 자판으로 건반 연주. 아래 줄 Z X C V B N M , . / 가 흰건반, 그 위 줄 S D G H J L ; 가 검은건반이고,
// Q W E R T Y U I O P [ ] 가 한 옥타브 위의 흰건반, 숫자 줄 2 3 5 6 7 9 0 = 이 검은건반이다 (Z 와 Q 가 옥타브 차이).
// 누르고 떼는 것은 자판의 상태로 읽어 화음과 동시 누르기를 다룬다. ⌘, ⌃, ⌥ 와 함께 누른 자판은 단축키로 남긴다.
// macOS 에서는 NativeKeys 가 자판을 JUCE 보다 먼저 물리 위치로 받아 nativeKey 로 넘긴다 (입력기와 상관없고, 경고음이 나지 않는다).
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <map>
#include <set>
#include <vector>

namespace daw
{

class TypingKeyboard final : public juce::KeyListener
{
public:
    static constexpr int lowestBase = 24;    // Z 가 놓일 수 있는 가장 낮은 C (C1)
    static constexpr int highestBase = 96;   // 가장 높은 C (C7)

    std::function<void (int pitch, bool on)> onNote;   // 울리기 시작하거나 그친 음 (같은 음을 두 자판이 누르면 한 번)

    void setEnabled (bool enabled);
    bool isEnabled() const noexcept { return enabled_; }
    void setBase (int pitch);                         // Z 의 음높이 (C 로 내린다)
    int base() const noexcept { return base_; }
    void releaseAll();

    // 운영체제가 먼저 받은 자판 (code 는 물리 위치의 글자). 연주에 썼으면 true (그 이벤트를 앱에 넘기지 않는다)
    bool nativeKey (int code, bool down, bool repeat);
    void setNativeKeys (bool native) { native_ = native; }   // 켜면 자판의 상태를 nativeKey 가 받은 것으로 읽는다
    static int asciiForVirtualKey (int virtualKey);         // macOS 가상 키 코드 -> 글자 (연주에 쓰는 자판만, 나머지 0)

    // 자판의 상태를 읽어 누르고 뗀 것을 알린다. 바뀐 것이 있으면 true. isDown 을 주면 그것으로 읽는다 (시험)
    bool update (const std::function<bool (int keyCode)>& isDown = {});

    std::map<int, juce::String> labels() const;       // 음높이 -> 그 음을 내는 자판 글자
    std::vector<int> held() const;                    // 울리는 음
    static int offsetOf (int keyCode);                // 자판 -> Z 에서의 반음, 연주에 쓰지 않는 자판은 -1

    bool keyPressed (const juce::KeyPress& key, juce::Component* origin) override;
    bool keyStateChanged (bool isKeyDown, juce::Component* origin) override;

private:
    bool listening() const;   // 켜져 있고 글 입력 칸에 초점이 없다

    bool enabled_ = true;
    bool native_ = false;
    int base_ = 60;
    std::set<int> pressed_;         // nativeKey 가 받은 누른 자판
    std::map<int, int> down_;       // 누른 자판 -> 누를 때의 음높이 (누른 채 옥타브를 바꿔도 같은 음을 끈다)
    std::map<int, int> sounding_;   // 음높이 -> 누르고 있는 자판 수
};

} // namespace daw
