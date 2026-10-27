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

#include "check.h"

#include <cstdlib>

namespace testing
{

int g_failuresInCase = 0;

std::vector<TestCase>& registry()
{
    static std::vector<TestCase> cases;
    return cases;
}

void reportFailure(const char *file, int line, const std::string &what)
{
    ++g_failuresInCase;
    fprintf(stderr, "    %s:%d: %s\n", file, line, what.c_str());
}

int runAll(const char *suiteFilter)
{
    int run = 0;
    int failed = 0;

    for (size_t i = 0; i < registry().size(); ++i)
    {
        const TestCase &tc = registry()[i];
        if (suiteFilter && strcmp(suiteFilter, tc.suite) != 0)
            continue;

        ++run;
        g_failuresInCase = 0;
        fprintf(stderr, "[ run  ] %s.%s\n", tc.suite, tc.name);
        tc.fn();

        if (g_failuresInCase == 0)
        {
            fprintf(stderr, "[  ok  ] %s.%s\n", tc.suite, tc.name);
        }
        else
        {
            ++failed;
            fprintf(stderr, "[ FAIL ] %s.%s (%d)\n", tc.suite, tc.name, g_failuresInCase);
        }
    }

    fprintf(stderr, "\n%d run, %d failed\n", run, failed);
    return failed == 0 ? 0 : 1;
}

} // namespace testing

int main(int argc, char **argv)
{
    return testing::runAll(argc > 1 ? argv[1] : NULL);
}
