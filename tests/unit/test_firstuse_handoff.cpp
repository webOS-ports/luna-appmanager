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

/*
 * FirstUseHandoff on its own: what first use's hand-off file launches, and
 * everything it refuses. Each case writes its own file in its own temporary
 * directory, so they do not depend on each other.
 */

#include "check.h"

#include "FirstUseHandoff.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

namespace
{

typedef std::vector<FirstUseHandoff::Launch> Launches;

const char *BACKUP = "com.palm.app.backup";
const char *ACCOUNTS = "com.palm.app.accounts";

//! A fresh directory, removed with everything in it when the case ends.
struct TempDir
{
    std::string path;

    TempDir()
    {
        const char *base = getenv("TMPDIR");
        std::string pattern = std::string(base && *base ? base : "/tmp") + "/firstuse-handoff-XXXXXX";
        char *made = mkdtemp(&pattern[0]);
        path = made ? made : "";
    }

    ~TempDir()
    {
        if (!path.empty())
            (void)system(("rm -rf '" + path + "'").c_str());
    }

    std::string file(const char *name) const { return path + "/" + name; }
};

bool write(const std::string &path, const std::string &content)
{
    FILE *f = fopen(path.c_str(), "w");
    if (!f)
        return false;
    const bool ok = fwrite(content.data(), 1, content.size(), f) == content.size();
    return fclose(f) == 0 && ok;
}

bool exists(const std::string &path)
{
    struct stat st;
    return lstat(path.c_str(), &st) == 0;
}

std::string read(const std::string &path)
{
    std::string out;
    FILE *f = fopen(path.c_str(), "r");
    if (!f)
        return "<missing>";
    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        out.append(buf, n);
    fclose(f);
    return out;
}

Launches takeContent(const TempDir &dir, const std::string &content)
{
    const std::string path = dir.file("firstuse-handoff.json");
    if (!write(path, content))
        return Launches(1, FirstUseHandoff::Launch{"<could not write>", ""});
    return FirstUseHandoff::take(path.c_str());
}

} // namespace

TEST(FirstUseHandoff, aMissingFileLaunchesNothing)
{
    TempDir dir;
    CHECK(!dir.path.empty());
    CHECK_EQ(int(FirstUseHandoff::take(dir.file("firstuse-handoff.json").c_str()).size()), 0);
}

TEST(FirstUseHandoff, restoreOpensTheBackupAppOnItsRestoreFlow)
{
    TempDir dir;
    const Launches launches = takeContent(dir, "{\"restore\":true}");
    CHECK_EQ(int(launches.size()), 1);
    if (launches.size() == 1) {
        CHECK_EQ(launches[0].appId, std::string(BACKUP));
        CHECK_EQ(launches[0].params, std::string("{\"restore\":true}"));
    }
    CHECK(!exists(dir.file("firstuse-handoff.json")));
}

TEST(FirstUseHandoff, accountsOpensTheAccountsApp)
{
    TempDir dir;
    const Launches launches = takeContent(dir, "{\"accounts\":true}");
    CHECK_EQ(int(launches.size()), 1);
    if (launches.size() == 1)
        CHECK_EQ(launches[0].appId, std::string(ACCOUNTS));
}

TEST(FirstUseHandoff, bothLaunchTheRestoreFirst)
{
    TempDir dir;
    const Launches launches = takeContent(dir, "{ \"accounts\": true, \"restore\": true }\n");
    CHECK_EQ(int(launches.size()), 2);
    if (launches.size() == 2) {
        CHECK_EQ(launches[0].appId, std::string(BACKUP));
        CHECK_EQ(launches[1].appId, std::string(ACCOUNTS));
    }
}

TEST(FirstUseHandoff, itIsReadOnce)
{
    TempDir dir;
    CHECK_EQ(int(takeContent(dir, "{\"restore\":true}").size()), 1);
    CHECK_EQ(int(FirstUseHandoff::take(dir.file("firstuse-handoff.json").c_str()).size()), 0);
}

TEST(FirstUseHandoff, onlyAJsonTrueTurnsSomethingOn)
{
    const char *notTrue[] = {
        "{\"restore\":false}", "{\"restore\":\"true\"}", "{\"restore\":1}",
        "{\"restore\":null}", "{\"restore\":[true]}", "{\"restore\":{}}", "{}",
        "{\"Restore\":true}"
    };
    for (size_t i = 0; i < sizeof(notTrue) / sizeof(notTrue[0]); ++i) {
        TempDir dir;
        CHECK_EQ(int(takeContent(dir, notTrue[i]).size()), 0);
        CHECK(!exists(dir.file("firstuse-handoff.json")));
    }
}

TEST(FirstUseHandoff, theFileCannotNameAnAppOrParameters)
{
    TempDir dir;
    const Launches launches = takeContent(dir,
        "{\"appId\":\"com.evil\",\"id\":\"com.evil\",\"params\":{\"x\":1},"
        "\"launch\":[\"com.evil\"],\"restore\":true}");
    CHECK_EQ(int(launches.size()), 1);
    if (launches.size() == 1) {
        CHECK_EQ(launches[0].appId, std::string(BACKUP));
        CHECK_EQ(launches[0].params, std::string("{\"restore\":true}"));
    }
}

TEST(FirstUseHandoff, aMalformedFileLaunchesNothingAndIsRemoved)
{
    const char *bad[] = {
        "", "   ", "{", "{\"restore\":true", "[{\"restore\":true}]", "true", "\"restore\"",
        "{\"restore\":true} trailing", "{\"restore\":true}{\"accounts\":true}",
        "\xef\xbb\xbf\xff{\"restore\":true}"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        TempDir dir;
        CHECK_EQ(int(takeContent(dir, bad[i]).size()), 0);
        CHECK(!exists(dir.file("firstuse-handoff.json")));
    }
}

TEST(FirstUseHandoff, anOversizedFileIsRefused)
{
    TempDir dir;
    // Valid JSON asking for a restore, padded past the limit.
    std::string big = "{\"restore\":true,\"pad\":\"" +
                      std::string(FirstUseHandoff::kMaxSize, 'x') + "\"}";
    CHECK_EQ(int(takeContent(dir, big).size()), 0);
    CHECK(!exists(dir.file("firstuse-handoff.json")));

    // Exactly at the limit is still read.
    TempDir dir2;
    std::string exact = "{\"restore\":true,\"pad\":\"\"}";
    exact.insert(exact.size() - 2, std::string(FirstUseHandoff::kMaxSize - exact.size(), 'x'));
    CHECK_EQ(long(exact.size()), FirstUseHandoff::kMaxSize);
    CHECK_EQ(int(takeContent(dir2, exact).size()), 1);
}

TEST(FirstUseHandoff, aSymlinkIsNotFollowedAndOnlyTheLinkIsRemoved)
{
    TempDir dir;
    const std::string target = dir.file("target.json");
    const std::string link = dir.file("firstuse-handoff.json");
    CHECK(write(target, "{\"restore\":true}"));
    CHECK_EQ(symlink(target.c_str(), link.c_str()), 0);

    CHECK_EQ(int(FirstUseHandoff::take(link.c_str()).size()), 0);
    CHECK(!exists(link));
    CHECK_EQ(read(target), std::string("{\"restore\":true}"));
}

TEST(FirstUseHandoff, aDirectoryOrFifoIsRefusedWithoutBlocking)
{
    TempDir dir;
    const std::string path = dir.file("firstuse-handoff.json");

    CHECK_EQ(mkdir(path.c_str(), 0700), 0);
    CHECK_EQ(int(FirstUseHandoff::take(path.c_str()).size()), 0);
    rmdir(path.c_str());

    // A FIFO with no writer: opening it blocking would hang boot.
    CHECK_EQ(mkfifo(path.c_str(), 0600), 0);
    CHECK_EQ(int(FirstUseHandoff::take(path.c_str()).size()), 0);
    CHECK(!exists(path));
}

TEST(FirstUseHandoff, pathIsTheRealFileUnlessATestSaysOtherwise)
{
    CHECK_EQ(std::string(FirstUseHandoff::path()),
             std::string("/var/luna/preferences/firstuse-handoff.json"));
    FirstUseHandoff::setPathForTesting("/somewhere/else.json");
    CHECK_EQ(std::string(FirstUseHandoff::path()), std::string("/somewhere/else.json"));
    FirstUseHandoff::setPathForTesting(NULL);
    CHECK_EQ(std::string(FirstUseHandoff::path()), std::string(FirstUseHandoff::kPath));
}
