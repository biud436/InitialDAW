#include "notation/Key.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cctype>
#include <cmath>

namespace mr
{

namespace
{

constexpr int naturalPc[7] = { 0, 2, 4, 5, 7, 9, 11 };
constexpr const char* letters = "CDEFGAB";
constexpr int sharpOrder[7] = { 3, 0, 4, 1, 5, 2, 6 };   // F C G D A E B
constexpr int flatOrder[7] = { 6, 2, 5, 1, 4, 0, 3 };    // B E A D G C F

int lineOfFifths (int step) { return std::array<int, 7> { 0, 2, 4, -1, 1, 3, 5 }[(size_t) step]; }   // C D E F G A B

int mod12 (int v) { return ((v % 12) + 12) % 12; }

} // namespace

std::optional<KeySignature> parseKeyName (const std::string& text)
{
    std::string s;
    for (const char c : text)
        s += c;
    while (! s.empty() && std::isspace ((unsigned char) s.back()))
        s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace ((unsigned char) s[i]))
        ++i;
    if (i >= s.size())
        return std::nullopt;
    const char letter = (char) std::toupper ((unsigned char) s[i]);
    const char* at = std::strchr (letters, letter);
    if (at == nullptr || letter == 0)
        return std::nullopt;
    const int step = (int) (at - letters);
    ++i;
    int alter = 0;
    while (i < s.size() && (s[i] == '#' || s[i] == 'b'))
        alter += s[i++] == '#' ? 1 : -1;
    while (i < s.size() && std::isspace ((unsigned char) s[i]))
        ++i;
    std::string mode;
    for (; i < s.size(); ++i)
        mode += (char) std::tolower ((unsigned char) s[i]);
    bool minor = false;
    if (mode == "minor")
        minor = true;
    else if (! mode.empty() && mode != "major")
        return std::nullopt;
    const int fifths = lineOfFifths (step) + 7 * alter - (minor ? 3 : 0);
    if (fifths < -7 || fifths > 7)
        return std::nullopt;
    return KeySignature { fifths, minor };
}

int tonicPitchClass (const KeySignature& key)
{
    // 장조의 으뜸음: 5도권 자리 x 7 반음, 단조는 그 아래 단3도
    return mod12 (key.fifths * 7 + (key.minor ? 9 : 0));
}

std::string keyName (const KeySignature& key)
{
    // 으뜸음 글자: 5도권 자리 (단조는 +3) 에서
    const int line = key.fifths + (key.minor ? 3 : 0);   // -7..10
    const int base = ((line + 1) % 7 + 7) % 7;           // F C G D A E B 의 차례
    constexpr int fcgdaeb[7] = { 3, 0, 4, 1, 5, 2, 6 };
    const int step = fcgdaeb[base];
    const int alter = (int) std::floor ((line + 1) / 7.0);
    std::string name (1, letters[step]);
    for (int a = 0; a < alter; ++a)
        name += '#';
    for (int a = 0; a > alter; --a)
        name += 'b';
    return name + (key.minor ? " minor" : " major");
}

KeySignature keyFor (int tonic, bool minor)
{
    // 장조 으뜸음의 조표 (F# 장조는 샵), 단조 으뜸음의 조표 (Eb 단조는 플랫)
    static const int majorFifths[12] = { 0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5 };
    static const int minorFifths[12] = { -3, 4, -1, -6, 1, -4, 3, -2, 5, 0, -5, 2 };
    const int pc = mod12 (tonic);
    return { minor ? minorFifths[pc] : majorFifths[pc], minor };
}

KeySignature relativeKey (const KeySignature& key)
{
    return key.minor ? keyFor (tonicPitchClass (key) + 3, false) : keyFor (tonicPitchClass (key) + 9, true);
}

KeySignature estimateKey (const std::vector<double>& w)
{
    static const double major[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double minor[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    double weights[12] = {};
    double total = 0.0;
    for (size_t i = 0; i < 12 && i < w.size(); ++i)
    {
        weights[i] = w[i];
        total += w[i];
    }
    if (total <= 0.0)
        return {};
    auto correlate = [&weights] (const double* profile, int tonic)
    {
        double mx = 0.0, my = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            mx += weights[i];
            my += profile[mod12 (i - tonic)];
        }
        mx /= 12.0;
        my /= 12.0;
        double sxy = 0.0, sxx = 0.0, syy = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double x = weights[i] - mx, y = profile[mod12 (i - tonic)] - my;
            sxy += x * y;
            sxx += x * x;
            syy += y * y;
        }
        return sxx > 0.0 && syy > 0.0 ? sxy / std::sqrt (sxx * syy) : 0.0;
    };
    KeySignature best;
    double bestR = -2.0;
    for (int tonic = 0; tonic < 12; ++tonic)
    {
        if (const double r = correlate (major, tonic); r > bestR + 1.0e-12)
        {
            bestR = r;
            best = keyFor (tonic, false);
        }
        if (const double r = correlate (minor, tonic); r > bestR + 1.0e-12)
        {
            bestR = r;
            best = keyFor (tonic, true);
        }
    }
    return best;
}

int keyAlter (const KeySignature& key, int step)
{
    for (int i = 0; i < std::min (7, std::abs (key.fifths)); ++i)
        if ((key.fifths > 0 ? sharpOrder[i] : flatOrder[i]) == step)
            return key.fifths > 0 ? 1 : -1;
    return 0;
}

Spelling spellPitch (int pitch, const KeySignature& key)
{
    const int pc = mod12 (pitch);
    // 조표의 음
    for (int s = 0; s < 7; ++s)
        if (mod12 (naturalPc[s] + keyAlter (key, s)) == pc)
        {
            const int alter = keyAlter (key, s);
            return { s, alter, (pitch - alter) / 12 - 1 };
        }
    // 조 밖의 음: 으뜸음에서의 자리로 샵(+1)이나 플랫(-1) 쪽을 고른다
    const int rel = mod12 (pc - tonicPitchClass (key));
    static const int majorDirection[12] = { 0, 1, 0, -1, 0, 0, 1, 0, 1, 0, -1, 0 };    // #1 b3 #4 #5 b7
    static const int minorDirection[12] = { 0, -1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1 };     // b2 #3 #4 #6 #7
    int direction = key.minor ? minorDirection[rel] : majorDirection[rel];
    if (direction == 0)
        direction = key.fifths < 0 ? -1 : 1;
    auto candidate = [&] (int dir) -> std::optional<Spelling>
    {
        for (int s = 0; s < 7; ++s)
        {
            const int alter = keyAlter (key, s) + dir;
            if (alter < -2 || alter > 2 || mod12 (naturalPc[s] + alter) != pc)
                continue;
            return Spelling { s, alter, (pitch - alter) / 12 - 1 };
        }
        return std::nullopt;
    };
    if (const auto c = candidate (direction))
        return *c;
    if (const auto c = candidate (-direction))
        return *c;
    // 가까운 글자의 샵 (마지막 수단)
    for (int s = 6; s >= 0; --s)
        if (const int alter = pc - naturalPc[s]; alter >= 0 && alter <= 2)
            return { s, alter, (pitch - alter) / 12 - 1 };
    return { 0, 0, pitch / 12 - 1 };
}

std::string spellingName (const Spelling& s)
{
    std::string name (1, letters[s.step]);
    for (int a = 0; a < s.alter; ++a)
        name += '#';
    for (int a = 0; a > s.alter; --a)
        name += 'b';
    return name + std::to_string (s.octave);
}

bool inKeyScale (const KeySignature& key, int pitch)
{
    static const bool major[12] { true, false, true, false, true, true, false, true, false, true, false, true };
    static const bool minor[12] { true, false, true, true, false, true, false, true, true, false, true, false };
    const int degree = (((pitch - tonicPitchClass (key)) % 12) + 12) % 12;
    return key.minor ? minor[degree] : major[degree];
}

} // namespace mr
