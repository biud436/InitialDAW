// 작은 테스트 틀. TEST(이름) 로 등록하고 CHECK, CHECK_NEAR, REQUIRE 로 확인한다.
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace mrtest
{

struct Case
{
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& cases()
{
    static std::vector<Case> list;
    return list;
}

struct Registrar
{
    Registrar (const char* name, std::function<void()> fn) { cases().push_back ({ name, std::move (fn) }); }
};

struct State
{
    int checks = 0;
    int failures = 0;
    const char* current = "";
};

inline State& state()
{
    static State s;
    return s;
}

struct RequireFailed {};

inline bool check (bool ok, const char* expr, const char* file, int line, const std::string& detail = {})
{
    ++state().checks;
    if (! ok)
    {
        ++state().failures;
        std::fprintf (stderr, "  FAIL %s (%s:%d): %s%s%s\n", state().current, file, line, expr,
                      detail.empty() ? "" : " | ", detail.c_str());
    }
    return ok;
}

template <typename A, typename B>
std::string describe (const A& a, const B& b)
{
    std::ostringstream s;
    s.precision (10);
    s << "got " << a << ", expected " << b;
    return s.str();
}

} // namespace mrtest

#define TEST(name)                                                       \
    static void name();                                                  \
    static mrtest::Registrar name##_registrar (#name, name);             \
    static void name()

#define CHECK(cond) mrtest::check ((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) mrtest::check ((a) == (b), #a " == " #b, __FILE__, __LINE__, mrtest::describe ((a), (b)))
#define CHECK_NEAR(a, b, tol) mrtest::check (std::abs ((double) (a) - (double) (b)) <= (tol), #a " ~= " #b, __FILE__, __LINE__, mrtest::describe ((a), (b)))
#define REQUIRE(cond) do { if (! mrtest::check ((cond), #cond, __FILE__, __LINE__)) throw mrtest::RequireFailed {}; } while (0)
