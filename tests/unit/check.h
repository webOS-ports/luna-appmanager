// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef __LUNA_APPMANAGER_TEST_CHECK_H__
#define __LUNA_APPMANAGER_TEST_CHECK_H__

/*
 * A test runner small enough to cross-compile with the component and run on a
 * phone. gtest is not in the LuneOS sysroot and pulling it in for a few dozen
 * assertions would mean a new dependency in the recipe for every machine.
 *
 * Register with TEST(suite, name) { ... } and assert with CHECK/CHECK_EQ.
 * Every failure is reported; the run does not stop at the first one.
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace testing
{

struct TestCase
{
    const char *suite;
    const char *name;
    void (*fn)();
};

std::vector<TestCase>& registry();
int runAll(const char *suiteFilter);

//! Incremented by the CHECK macros; read by the runner after each case.
extern int g_failuresInCase;

void reportFailure(const char *file, int line, const std::string &what);

struct Registrar
{
    Registrar(const char *suite, const char *name, void (*fn)())
    {
        TestCase tc = { suite, name, fn };
        registry().push_back(tc);
    }
};

//! Printable form for the values CHECK_EQ compares.
inline std::string show(const std::string &v) { return "\"" + v + "\""; }
inline std::string show(const char *v) { return v ? show(std::string(v)) : std::string("(null)"); }
inline std::string show(bool v) { return v ? "true" : "false"; }
inline std::string show(long long v) { char b[32]; snprintf(b, sizeof(b), "%lld", v); return b; }
inline std::string show(unsigned long long v) { char b[32]; snprintf(b, sizeof(b), "%llu", v); return b; }
inline std::string show(int v) { return show(static_cast<long long>(v)); }
inline std::string show(long v) { return show(static_cast<long long>(v)); }
// size_t is unsigned int on 32-bit ARM and unsigned long on 64-bit targets.
inline std::string show(unsigned int v) { return show(static_cast<unsigned long long>(v)); }
inline std::string show(unsigned long v) { return show(static_cast<unsigned long long>(v)); }

} // namespace testing

#define TEST(suite, name)                                                      \
    static void suite##_##name##_body();                                       \
    static testing::Registrar suite##_##name##_reg(#suite, #name,              \
                                                   suite##_##name##_body);     \
    static void suite##_##name##_body()

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond))                                                           \
            testing::reportFailure(__FILE__, __LINE__,                         \
                std::string("CHECK(") + #cond + ") is false");                 \
    } while (0)

#define CHECK_EQ(actual, expected)                                             \
    do {                                                                       \
        auto _a = (actual);                                                    \
        auto _e = (expected);                                                  \
        if (!(_a == _e))                                                       \
            testing::reportFailure(__FILE__, __LINE__,                         \
                std::string(#actual " == " #expected " : got ")                \
                + testing::show(_a) + ", wanted " + testing::show(_e));        \
    } while (0)

#define CHECK_STR_CONTAINS(haystack, needle)                                   \
    do {                                                                       \
        std::string _h = (haystack);                                           \
        std::string _n = (needle);                                             \
        if (_h.find(_n) == std::string::npos)                                  \
            testing::reportFailure(__FILE__, __LINE__,                         \
                std::string("expected ") + testing::show(_h)                   \
                + " to contain " + testing::show(_n));                         \
    } while (0)

#define CHECK_STR_LACKS(haystack, needle)                                      \
    do {                                                                       \
        std::string _h = (haystack);                                           \
        std::string _n = (needle);                                             \
        if (_h.find(_n) != std::string::npos)                                  \
            testing::reportFailure(__FILE__, __LINE__,                         \
                std::string("expected ") + testing::show(_h)                   \
                + " not to contain " + testing::show(_n));                     \
    } while (0)

#endif
