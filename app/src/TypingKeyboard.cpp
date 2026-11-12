#include "TypingKeyboard.h"

namespace daw
{

namespace
{

struct TypingKey
{
    int code;
    int offset;
};

const std::vector<TypingKey>& typingKeys()
{
    static const std::vector<TypingKey> keys {
        // 아래 옥타브: Z 가 C
        { 'Z', 0 }, { 'S', 1 }, { 'X', 2 }, { 'D', 3 }, { 'C', 4 }, { 'V', 5 }, { 'G', 6 }, { 'B', 7 }, { 'H', 8 }, { 'N', 9 },
        { 'J', 10 }, { 'M', 11 }, { ',', 12 }, { 'L', 13 }, { '.', 14 }, { ';', 15 }, { '/', 16 },
        // 위 옥타브: Q 가 C
        { 'Q', 12 }, { '2', 13 }, { 'W', 14 }, { '3', 15 }, { 'E', 16 }, { 'R', 17 }, { '5', 18 }, { 'T', 19 }, { '6', 20 },
        { 'Y', 21 }, { '7', 22 }, { 'U', 23 }, { 'I', 24 }, { '9', 25 }, { 'O', 26 }, { '0', 27 }, { 'P', 28 }, { '[', 29 },
        { '=', 30 }, { ']', 31 },
    };
    return keys;
}

int normalised (int keyCode)
{
    return keyCode >= 'a' && keyCode <= 'z' ? keyCode - 'a' + 'A' : keyCode;
}

} // namespace

int TypingKeyboard::asciiForVirtualKey (int virtualKey)
{
    // HIToolbox/Events.h 의 kVK_ANSI_* (자판의 물리 위치)
    switch (virtualKey)
    {
        case 0x06: return 'Z';  case 0x01: return 'S';  case 0x07: return 'X';  case 0x02: return 'D';  case 0x08: return 'C';
        case 0x09: return 'V';  case 0x05: return 'G';  case 0x0B: return 'B';  case 0x04: return 'H';  case 0x2D: return 'N';
        case 0x26: return 'J';  case 0x2E: return 'M';  case 0x2B: return ',';  case 0x25: return 'L';  case 0x2F: return '.';
        case 0x29: return ';';  case 0x2C: return '/';
        case 0x0C: return 'Q';  case 0x13: return '2';  case 0x0D: return 'W';  case 0x14: return '3';  case 0x0E: return 'E';
        case 0x0F: return 'R';  case 0x17: return '5';  case 0x11: return 'T';  case 0x16: return '6';  case 0x10: return 'Y';
        case 0x1A: return '7';  case 0x20: return 'U';  case 0x22: return 'I';  case 0x19: return '9';  case 0x1F: return 'O';
        case 0x1D: return '0';  case 0x23: return 'P';  case 0x21: return '[';  case 0x18: return '=';  case 0x1E: return ']';
        default: return 0;
    }
}

bool TypingKeyboard::nativeKey (int code, bool down, bool repeat)
{
    code = normalised (code);
    if (offsetOf (code) < 0)
        return false;
    if (down)
    {
        if (! listening() || juce::Component::getCurrentlyModalComponent() != nullptr)
            return false;   // 꺼짐, 글 입력 칸, 메뉴와 대화상자: 앱의 자판 처리에 맡긴다
        if (! repeat)
        {
            pressed_.insert (code);
            update();
        }
        return true;
    }
    const bool mine = pressed_.erase (code) > 0;
    update();
    return mine;   // 누를 때 받은 자판이면 뗌도 받는다
}

int TypingKeyboard::offsetOf (int keyCode)
{
    keyCode = normalised (keyCode);
    for (const auto& k : typingKeys())
        if (k.code == keyCode)
            return k.offset;
    return -1;
}

void TypingKeyboard::setEnabled (bool enabled)
{
    enabled_ = enabled;
    if (! enabled_)
        releaseAll();
}

void TypingKeyboard::setBase (int pitch)
{
    base_ = juce::jlimit (lowestBase, highestBase, pitch - ((pitch % 12) + 12) % 12);
}

void TypingKeyboard::releaseAll()
{
    pressed_.clear();
    update ([] (int) { return false; });
}

bool TypingKeyboard::listening() const
{
    if (! enabled_)
        return false;
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    return dynamic_cast<juce::TextInputTarget*> (focused) == nullptr;
}

bool TypingKeyboard::update (const std::function<bool (int)>& isDown)
{
    const bool listen = isDown ? true : listening();
    const auto mods = juce::ModifierKeys::currentModifiers;
    const bool shortcut = ! isDown && (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown());
    bool changed = false;
    for (const auto& k : typingKeys())
    {
        const bool down = listen && (isDown ? isDown (k.code)
                                            : native_ ? pressed_.count (k.code) > 0 : juce::KeyPress::isKeyCurrentlyDown (k.code));
        const auto it = down_.find (k.code);
        if (down && it == down_.end() && ! shortcut)
        {
            const int pitch = base_ + k.offset;
            if (pitch > 127)
                continue;
            down_[k.code] = pitch;
            if (sounding_[pitch]++ == 0 && onNote)
                onNote (pitch, true);
            changed = true;
        }
        else if (! down && it != down_.end())
        {
            const int pitch = it->second;
            down_.erase (it);
            if (--sounding_[pitch] <= 0)
            {
                sounding_.erase (pitch);
                if (onNote)
                    onNote (pitch, false);
            }
            changed = true;
        }
    }
    return changed;
}

std::map<int, juce::String> TypingKeyboard::labels() const
{
    std::map<int, juce::String> out;
    for (const auto& k : typingKeys())
    {
        const int pitch = base_ + k.offset;
        if (pitch > 127)
            continue;
        auto& text = out[pitch];
        text = text.isEmpty() ? juce::String::charToString ((juce::juce_wchar) k.code) : text + " " + juce::String::charToString ((juce::juce_wchar) k.code);
    }
    return out;
}

std::vector<int> TypingKeyboard::held() const
{
    std::vector<int> out;
    for (const auto& [pitch, count] : sounding_)
        if (count > 0)
            out.push_back (pitch);
    return out;
}

bool TypingKeyboard::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (native_ || ! listening())
        return false;   // 운영체제에서 먼저 받을 때는 여기로 온 자판은 연주에 쓰지 않은 것이다
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown() || offsetOf (key.getKeyCode()) < 0)
        return false;
    update();
    return true;   // 연주에 쓰는 자판은 단축키(Q 격자 맞추기, X 믹서)로 넘기지 않는다
}

bool TypingKeyboard::keyStateChanged (bool, juce::Component*)
{
    return native_ ? false : update();
}

} // namespace daw
