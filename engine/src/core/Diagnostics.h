// 검증과 렌더링이 내는 오류와 경고. path 는 JSON 경로($.tracks[0].notes[3].pitch)다.
#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace mr
{

struct Diagnostic
{
    std::string path;
    std::string message;
    std::string hint;
};

class Diagnostics
{
public:
    void error (std::string path, std::string message, std::string hint = {});
    void warning (std::string path, std::string message, std::string hint = {});
    void append (const Diagnostics& other);
    bool ok() const noexcept { return errors.empty(); }

    std::vector<Diagnostic> errors;
    std::vector<Diagnostic> warnings;
};

// 명령을 끝내는 오류. code 는 기계가 읽는 분류(invalid-input, io, instrument, render)다.
class Failure : public std::runtime_error
{
public:
    Failure (std::string code, const std::string& message, std::vector<Diagnostic> details = {});

    const std::string& code() const noexcept { return code_; }
    const std::vector<Diagnostic>& details() const noexcept { return details_; }

private:
    std::string code_;
    std::vector<Diagnostic> details_;
};

int editDistance (const std::string& a, const std::string& b);

// 오타 제안. 가장 가까운 후보가 충분히 가까울 때만 돌려주고, 아니면 빈 문자열이다.
std::string closestMatch (const std::string& word, const std::vector<std::string>& candidates);

std::string formatNumber (double value, int decimals = 3);

} // namespace mr
