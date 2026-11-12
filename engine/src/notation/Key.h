// 조와 음 이름 철자. 조표는 5도권의 자리(fifths: 샵이면 양수, 플랫이면 음수)와 장단조다.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace mr
{

struct KeySignature
{
    int fifths = 0;       // -7..7
    bool minor = false;
    bool operator== (const KeySignature& o) const noexcept { return fifths == o.fifths && minor == o.minor; }
    bool operator!= (const KeySignature& o) const noexcept { return ! (*this == o); }
};

struct KeyChange
{
    double beat = 0.0;
    KeySignature key;
};

// "Bb minor", "F# major", "C" (장조). 영어 음 이름과 major, minor. 조표가 일곱 개를 넘으면 nullopt
std::optional<KeySignature> parseKeyName (const std::string& name);
std::string keyName (const KeySignature& key);       // "Bb minor"
int tonicPitchClass (const KeySignature& key);        // 0 = C
// 음높이마다의 무게(길이 합)로 조를 고른다: Krumhansl-Kessler 장조, 단조 표와의 상관이 가장 큰 조
KeySignature estimateKey (const std::vector<double>& pitchClassWeights);
// 으뜸음과 장단조의 조표 (샵과 플랫이 같은 수면 장조는 F#, 단조는 Eb 처럼 흔한 쪽)
KeySignature keyFor (int tonicPitchClass, bool minor);
KeySignature relativeKey (const KeySignature& key);   // 나란한조

struct Spelling
{
    int step = 0;     // 0 C, 1 D, 2 E, 3 F, 4 G, 5 A, 6 B
    int alter = 0;    // -2..2
    int octave = 4;   // 과학적 표기 (C4 = 60)
};
int keyAlter (const KeySignature& key, int step);      // 조표가 그 글자에 주는 변화 (-1, 0, 1)
// 조의 음계에 든 음인가 (장조는 장음계, 단조는 자연 단음계)
bool inKeyScale (const KeySignature& key, int pitch);
// 조표의 음은 그 글자로, 조 밖의 음은 조와 장단조에 따라 샵이나 플랫으로 (단조의 이끎음과 올린 6도는 샵 쪽)
Spelling spellPitch (int pitch, const KeySignature& key);
std::string spellingName (const Spelling& s);          // "F#4", "Bb3"

} // namespace mr
