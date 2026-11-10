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
 * com.palm.applicationManager/updateLaunchPointIcon and SAM. An icon an
 * application puts on its default launch point (Calendar's date) is handed
 * on to SAM's updateLaunchPoint, which keeps it in its database: the shell
 * fills its launcher from SAM, and the icon has to outlast a restart.
 *
 * One application, com.example.daily, is scanned from a folder of the test's
 * own, with its icon and a second one to change to.
 */

#include "check.h"
#include "fake_ls2.h"

#include "ApplicationManager.h"
#include "LaunchPoint.h"
#include "Settings.h"

#include <QCoreApplication>
#include <QImage>

#include <json.h>

#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

namespace
{

const char *APPMGR = "com.palm.applicationManager";
const char *APP = "com.example.daily";
const char *DEFAULT_LP = "com.example.daily_default";
const char *SAM_UPDATE = "luna://com.webos.service.applicationmanager/updateLaunchPoint";

int removeEntry(const char *path, const struct stat *, int, struct FTW *)
{
    return remove(path);
}

//! The folder the application is scanned from; removed when the run ends.
struct AppsFolder
{
    std::string path;

    AppsFolder()
    {
        const char *base = getenv("TMPDIR");
        std::string pattern = std::string(base && *base ? base : "/tmp") + "/launchpoint-icon-XXXXXX";
        char *made = mkdtemp(&pattern[0]);
        path = made ? made : "";
    }

    ~AppsFolder()
    {
        // Depth first, without following links.
        if (!path.empty() && nftw(path.c_str(), removeEntry, 16, FTW_DEPTH | FTW_PHYS) != 0)
            perror(path.c_str());
    }

    std::string app() const { return path + "/apps/" + APP; }
};

bool write(const std::string &path, const std::string &content)
{
    FILE *f = fopen(path.c_str(), "w");
    if (!f)
        return false;
    const bool ok = fwrite(content.data(), 1, content.size(), f) == content.size();
    return fclose(f) == 0 && ok;
}

bool writeIcon(const std::string &path)
{
    QImage icon(4, 4, QImage::Format_ARGB32);
    icon.fill(0xff336699);
    return icon.save(QString::fromStdString(path), "PNG");
}

const AppsFolder &folder()
{
    static AppsFolder f;
    return f;
}

void setUp()
{
    static bool scanned = false;
    if (!scanned) {
        static int argc = 1;
        static char name[] = "luna-appmanager-tests";
        static char *argv[] = { name, NULL };
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);

        const AppsFolder &f = folder();
        CHECK(!f.path.empty());
        CHECK_EQ(mkdir((f.path + "/apps").c_str(), 0700), 0);
        CHECK_EQ(mkdir(f.app().c_str(), 0700), 0);
        CHECK_EQ(mkdir((f.path + "/launchpoints").c_str(), 0700), 0);
        CHECK(write(f.app() + "/appinfo.json",
                    std::string("{\"id\":\"") + APP + "\",\"version\":\"1.0.0\",\"type\":\"web\","
                    "\"title\":\"Daily\",\"main\":\"index.html\",\"icon\":\"icon.png\"}"));
        CHECK(write(f.app() + "/index.html", "<html></html>"));
        CHECK(writeIcon(f.app() + "/icon.png"));
        CHECK(writeIcon(f.app() + "/icon-02.png"));

        Settings *settings = Settings::LunaSettings();
        settings->uiType = Settings::UI_MINIMAL;
        settings->lunaAppsPath = f.path + "/apps/";
        settings->lunaAppsPaths.clear();
        settings->lunaAppsPaths.push_back(settings->lunaAppsPath);
        settings->lunaLaunchPointsPath = f.path + "/launchpoints/";
        ApplicationManager::instance()->scan();
        scanned = true;
    }
    fakels::clearCalls();
    fakels::clearReplies();
}

std::string field(const std::string &payload, const char *name)
{
    std::string value;
    json_object *json = json_tokener_parse(payload.c_str());
    json_object *item = NULL;
    if (json && json_object_object_get_ex(json, name, &item))
        value = json_object_get_string(item);
    if (json)
        json_object_put(json);
    return value;
}

//! Calls updateLaunchPointIcon as appId; returns its reply.
std::string updateIcon(const char *appId, const std::string &launchPointId, const std::string &icon)
{
    const size_t before = fakels::messageReplies().size();
    CHECK(fakels::callMethod(APPMGR, "/", "updateLaunchPointIcon",
                             "{\"launchPointId\":\"" + launchPointId + "\",\"icon\":\"" + icon + "\"}",
                             appId));
    const std::vector<std::string> &replies = fakels::messageReplies();
    CHECK_EQ(replies.size(), before + 1);
    return replies.size() > before ? replies.back() : std::string();
}

} // namespace

TEST(LaunchPointIcon, theAppIsThere)
{
    setUp();
    CHECK(ApplicationManager::instance()->getLaunchPointById(DEFAULT_LP) != NULL);
}

TEST(LaunchPointIcon, anAppsOwnIconIsHandedToSam)
{
    setUp();
    const std::string reply = updateIcon(APP, DEFAULT_LP, "icon-02.png");
    CHECK_EQ(field(reply, "returnValue"), std::string("true"));

    const std::vector<fakels::Call> calls = fakels::callsTo("/updateLaunchPoint");
    CHECK_EQ(calls.size(), 1u);
    if (calls.empty())
        return;
    CHECK_EQ(calls[0].uri, std::string(SAM_UPDATE));
    CHECK_EQ(field(calls[0].payload, "launchPointId"), std::string(DEFAULT_LP));
    // The path the icon was resolved to here, so SAM needs no folder for it.
    CHECK_EQ(field(calls[0].payload, "icon"), folder().app() + "/icon-02.png");
}

TEST(LaunchPointIcon, aFileUrlIsHandedOnAsAPath)
{
    setUp();
    updateIcon(APP, DEFAULT_LP, "file://" + folder().app() + "/icon.png");
    const std::vector<fakels::Call> calls = fakels::callsTo("/updateLaunchPoint");
    CHECK_EQ(calls.size(), 1u);
    if (!calls.empty())
        CHECK_EQ(field(calls[0].payload, "icon"), folder().app() + "/icon.png");
}

TEST(LaunchPointIcon, anIconThatIsNoImageIsNotHandedOn)
{
    setUp();
    const std::string reply = updateIcon(APP, DEFAULT_LP, "missing.png");
    CHECK_EQ(field(reply, "returnValue"), std::string("false"));
    CHECK(fakels::callsTo("/updateLaunchPoint").empty());
}

TEST(LaunchPointIcon, anotherAppsLaunchPointIsNotHandedOn)
{
    setUp();
    const std::string reply = updateIcon("com.example.other", DEFAULT_LP, "icon-02.png");
    CHECK_EQ(field(reply, "returnValue"), std::string("false"));
    CHECK(fakels::callsTo("/updateLaunchPoint").empty());
}

// A dynamic launch point is kept in a file here (LaunchPoint::toFile); SAM
// has no launch point by that id.
TEST(LaunchPointIcon, aDynamicLaunchPointStaysHere)
{
    setUp();
    const std::string id = ApplicationManager::instance()->addLaunchPoint(
        APP, "Daily too", "Daily too", folder().app() + "/icon.png", "{}");
    CHECK(!id.empty());
    fakels::clearCalls();

    const std::string reply = updateIcon(APP, id, "icon-02.png");
    CHECK_EQ(field(reply, "returnValue"), std::string("true"));
    CHECK(fakels::callsTo("/updateLaunchPoint").empty());
}

// SAM's answer changes nothing for the app, which has its reply already.
TEST(LaunchPointIcon, samRefusingIsOnlyLogged)
{
    setUp();
    const std::string reply = updateIcon(APP, DEFAULT_LP, "icon-02.png");
    CHECK_EQ(field(reply, "returnValue"), std::string("true"));
    const std::vector<fakels::Call> calls = fakels::callsTo("/updateLaunchPoint");
    CHECK_EQ(calls.size(), 1u);
    if (!calls.empty()) {
        CHECK(fakels::reply(calls[0], "{\"returnValue\":false,\"errorText\":\"cannot find launch point\"}"));
        CHECK(fakels::reply(calls[0], "not json"));
    }
}

TEST(LaunchPointIcon, aBusThatRefusesTheCallDoesNotFailTheUpdate)
{
    setUp();
    fakels::failNextCall();
    const std::string reply = updateIcon(APP, DEFAULT_LP, "icon-02.png");
    CHECK_EQ(field(reply, "returnValue"), std::string("true"));
}
