// 사람이 읽는 로그는 표준 오류로만 간다. 표준 출력은 기계가 읽는 JSON 의 자리다.
#pragma once

#include <string>

namespace mr::log
{

enum class Level { quiet, info, debug };

void setLevel (Level level);
Level level();
void info (const std::string& message);
void warn (const std::string& message);
void debug (const std::string& message);

} // namespace mr::log
