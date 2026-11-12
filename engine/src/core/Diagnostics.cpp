#include "core/Diagnostics.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace mr
{

void Diagnostics::error (std::string path, std::string message, std::string hint)
{
    errors.push_back ({ std::move (path), std::move (message), std::move (hint) });
}

void Diagnostics::warning (std::string path, std::string message, std::string hint)
{
    warnings.push_back ({ std::move (path), std::move (message), std::move (hint) });
}

void Diagnostics::append (const Diagnostics& other)
{
    errors.insert (errors.end(), other.errors.begin(), other.errors.end());
    warnings.insert (warnings.end(), other.warnings.begin(), other.warnings.end());
}

Failure::Failure (std::string code, const std::string& message, std::vector<Diagnostic> details)
    : std::runtime_error (message), code_ (std::move (code)), details_ (std::move (details))
{
}

int editDistance (const std::string& a, const std::string& b)
{
    // 제한된 Damerau-Levenshtein: 이웃한 두 글자의 자리 바꿈도 한 번으로 센다 (pinao → piano)
    auto same = [] (char x, char y) { return std::tolower ((unsigned char) x) == std::tolower ((unsigned char) y); };
    const size_t n = a.size(), m = b.size();
    std::vector<std::vector<int>> d (n + 1, std::vector<int> (m + 1, 0));
    for (size_t i = 0; i <= n; ++i)
        d[i][0] = (int) i;
    for (size_t j = 0; j <= m; ++j)
        d[0][j] = (int) j;
    for (size_t i = 1; i <= n; ++i)
        for (size_t j = 1; j <= m; ++j)
        {
            const int cost = same (a[i - 1], b[j - 1]) ? 0 : 1;
            d[i][j] = std::min ({ d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost });
            if (i > 1 && j > 1 && same (a[i - 1], b[j - 2]) && same (a[i - 2], b[j - 1]))
                d[i][j] = std::min (d[i][j], d[i - 2][j - 2] + 1);
        }
    return d[n][m];
}

std::string closestMatch (const std::string& word, const std::vector<std::string>& candidates)
{
    std::string best;
    int bestDistance = 1 << 30;
    for (const auto& c : candidates)
    {
        int d = editDistance (word, c);
        // 단위 접미사를 빼고도 견준다 (treshold → thresholdDb)
        for (const char* suffix : { "Db", "Hz", "Ms", "Seconds", "Beat" })
        {
            const std::string s (suffix);
            if (c.size() > s.size() && c.compare (c.size() - s.size(), s.size(), s) == 0)
                d = std::min (d, editDistance (word, c.substr (0, c.size() - s.size())));
        }
        if (d < bestDistance)
        {
            bestDistance = d;
            best = c;
        }
    }
    const int limit = std::max (1, std::min (3, (int) word.size() / 3));
    return bestDistance <= limit ? best : std::string {};
}

std::string formatNumber (double value, int decimals)
{
    if (std::isnan (value))
        return "nan";
    char buffer[64];
    std::snprintf (buffer, sizeof (buffer), "%.*f", decimals, value);
    std::string s (buffer);
    if (s.find ('.') != std::string::npos)
    {
        while (! s.empty() && s.back() == '0')
            s.pop_back();
        if (! s.empty() && s.back() == '.')
            s.pop_back();
    }
    return s;
}

} // namespace mr
