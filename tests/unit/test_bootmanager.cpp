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
 * BootManager during first use, driven the way the bus drives it: the
 * compositor, WebAppMgr and SAM come and go, and SAM reports app life events.
 * What is checked is what reaches SAM - the launch calls.
 *
 * BootManager is a singleton, so the cases build on each other and run in the
 * order they are written in. They need a device that has not finished first
 * use (or the build host): with /var/luna/preferences/ran-firstuse present
 * BootManager goes straight to the normal state, and the cases say so and do
 * nothing.
 */

#include "check.h"
#include "fake_ls2.h"

#include "BootManager.h"

#include <QCoreApplication>
#include <QFile>

#include <json.h>

namespace
{

const char *FIRSTUSE = "org.webosports.app.firstuse";
const char *PHONE = "org.webosports.app.phone";
const char *COMPOSITOR = "com.webos.surfacemanager-cardshell";
const char *SAM = "com.webos.service.applicationmanager";

bool g_skip = false;
size_t g_launchesAnswered = 0;

void runEvents()
{
    for (int i = 0; i < 5; ++i)
        QCoreApplication::processEvents();
}

// Kept for the whole run, like BootManager itself: QCoreApplication::instance()
// is what still points at it.
void ensureApplication()
{
    static int argc = 1;
    static char name[] = "luna-appmanager-tests";
    static char *argv[] = { name, NULL };
    if (!QCoreApplication::instance())
        new QCoreApplication(argc, argv);
}

//! The "id" and "params" of a launch call's payload.
std::string launchField(const fakels::Call &call, const char *field)
{
    std::string value;
    json_object *json = json_tokener_parse(call.payload.c_str());
    json_object *item = NULL;
    if (json && json_object_object_get_ex(json, field, &item))
        value = json_object_to_json_string_ext(item, JSON_C_TO_STRING_PLAIN);
    if (json)
        json_object_put(json);
    return value;
}

std::vector<fakels::Call> launches()
{
    return fakels::callsTo("/launch");
}

int launchesOf(const char *appId)
{
    const std::string quoted = std::string("\"") + appId + "\"";
    const std::vector<fakels::Call> all = launches();
    int n = 0;
    for (size_t i = 0; i < all.size(); ++i) {
        if (launchField(all[i], "id") == quoted)
            ++n;
    }
    return n;
}

//! SAM answers every launch so far, as it would.
void answerLaunches()
{
    const std::vector<fakels::Call> all = launches();
    for (; g_launchesAnswered < all.size(); ++g_launchesAnswered)
        fakels::reply(all[g_launchesAnswered], "{\"returnValue\":true,\"instanceId\":\"fake\"}");
}

//! Sends payload on BootManager's newest getAppLifeEvents subscription.
bool lifeEventPayload(const std::string &payload)
{
    const std::vector<fakels::Call> subscriptions = fakels::callsTo("/getAppLifeEvents");
    if (subscriptions.empty())
        return false;
    const bool handled = fakels::reply(subscriptions.back(), payload);
    runEvents();
    answerLaunches();
    return handled;
}

bool lifeEvent(const char *event, const char *appId)
{
    return lifeEventPayload(std::string("{\"event\":\"") + event + "\",\"appId\":\"" + appId +
                            "\",\"returnValue\":true,\"subscribed\":true}");
}

void compositor(bool up)
{
    fakels::serverStatus(COMPOSITOR, up);
    runEvents();
    answerLaunches();
}

bool skipped()
{
    if (g_skip)
        fprintf(stderr, "    skipped: first use has already run here\n");
    return g_skip;
}

} // namespace

TEST(BootManager, entersFirstUseOnceTheCompositorAndWebAppMgrAreUp)
{
    if (QFile::exists("/var/luna/preferences/ran-firstuse")) {
        g_skip = true;
        skipped();
        return;
    }

    ensureApplication();
    BootManager::instance();
    runEvents();
    CHECK_EQ(int(BootManager::instance()->currentState()), int(BOOT_STATE_STARTUP));

    CHECK_EQ(fakels::serverStatus(COMPOSITOR, true), 1);
    runEvents();
    CHECK_EQ(int(BootManager::instance()->currentState()), int(BOOT_STATE_STARTUP));
    CHECK_EQ(int(launches().size()), 0);

    const std::vector<fakels::Call> wam = fakels::callsTo("/registerServerStatus");
    CHECK_EQ(int(wam.size()), 1);
    if (wam.empty())
        return;
    fakels::reply(wam[0], "{\"serviceName\":\"com.palm.webappmanager\",\"connected\":true}");
    runEvents();
    answerLaunches();

    CHECK_EQ(int(BootManager::instance()->currentState()), int(BOOT_STATE_FIRSTUSE));
    CHECK_EQ(launchesOf(FIRSTUSE), 1);
    CHECK_EQ(launchesOf(PHONE), 1);
}

TEST(BootManager, subscribesToAppLifeEventsWhenSamComesUp)
{
    if (skipped())
        return;

    CHECK_EQ(int(fakels::callsTo("/getAppLifeEvents").size()), 0);
    CHECK_EQ(fakels::serverStatus(SAM, true), 1);

    const std::vector<fakels::Call> subscriptions = fakels::callsTo("/getAppLifeEvents");
    CHECK_EQ(int(subscriptions.size()), 1);
    if (!subscriptions.empty()) {
        CHECK_EQ(subscriptions[0].uri,
                 std::string("luna://com.webos.service.applicationmanager/getAppLifeEvents"));
        CHECK_STR_CONTAINS(subscriptions[0].payload, "\"subscribe\":true");
    }
}

TEST(BootManager, launchesFirstUseAgainWhenItStops)
{
    if (skipped())
        return;

    const int firstUse = launchesOf(FIRSTUSE);
    const int phone = launchesOf(PHONE);

    CHECK(lifeEvent("stop", FIRSTUSE));
    CHECK_EQ(launchesOf(FIRSTUSE), firstUse + 1);
    CHECK_EQ(launchesOf(PHONE), phone);
}

TEST(BootManager, launchesThePhoneAgainInFirstUseMode)
{
    if (skipped())
        return;

    const int firstUse = launchesOf(FIRSTUSE);
    const int phone = launchesOf(PHONE);

    CHECK(lifeEvent("stop", PHONE));
    CHECK_EQ(launchesOf(PHONE), phone + 1);
    CHECK_EQ(launchesOf(FIRSTUSE), firstUse);

    const std::vector<fakels::Call> all = launches();
    if (!all.empty())
        CHECK_EQ(launchField(all.back(), "params"), std::string("{\"mode\":\"first-use\"}"));
}

TEST(BootManager, ignoresOtherEventsAppsAndMalformedPayloads)
{
    if (skipped())
        return;

    const size_t before = launches().size();

    const char *events[] = { "launch", "splash", "foreground", "background", "close", "Stop" };
    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); ++i)
        lifeEvent(events[i], FIRSTUSE);
    lifeEvent("stop", "org.webosports.app.firstuse.other");
    lifeEvent("stop", "com.palm.launcher");

    const char *payloads[] = {
        "",
        "not json",
        "null",
        "[]",
        "[{\"event\":\"stop\",\"appId\":\"org.webosports.app.firstuse\"}]",
        "\"stop\"",
        "{}",
        "{\"returnValue\":true,\"subscribed\":true}",
        "{\"returnValue\":false,\"errorCode\":-1,\"errorText\":\"denied\"}",
        // What luna-service2 hands a subscription when SAM goes down.
        "{\"serviceName\":\"com.webos.service.applicationmanager\",\"returnValue\":false,"
            "\"errorCode\":-1,\"errorText\":\"com.webos.service.applicationmanager is not running.\"}",
        "{\"event\":\"stop\"}",
        "{\"appId\":\"org.webosports.app.firstuse\"}",
        "{\"event\":null,\"appId\":\"org.webosports.app.firstuse\"}",
        "{\"event\":\"stop\",\"appId\":null}",
        "{\"event\":\"stop\",\"appId\":42}",
        "{\"event\":[\"stop\"],\"appId\":\"org.webosports.app.firstuse\"}",
        "{\"event\":\"stop\",\"appId\":\"org.webosports.app.firstuse\"",
    };
    for (size_t i = 0; i < sizeof(payloads) / sizeof(payloads[0]); ++i)
        CHECK(lifeEventPayload(payloads[i]));

    CHECK_EQ(launches().size(), before);
}

TEST(BootManager, leavesTheLaunchToTheCompositorComingBack)
{
    if (skipped())
        return;

    const int firstUse = launchesOf(FIRSTUSE);
    const int phone = launchesOf(PHONE);

    compositor(false);
    lifeEvent("stop", FIRSTUSE);
    lifeEvent("stop", PHONE);
    CHECK_EQ(launchesOf(FIRSTUSE), firstUse);
    CHECK_EQ(launchesOf(PHONE), phone);

    // BOOT_EVENT_COMPOSITOR_AVAILABLE launches both, once each.
    compositor(true);
    CHECK_EQ(launchesOf(FIRSTUSE), firstUse + 1);
    CHECK_EQ(launchesOf(PHONE), phone + 1);
}

TEST(BootManager, givesUpAfterFiveLaunchesInAMinute)
{
    if (skipped())
        return;

    // The compositor just came back, so both have all five.
    const int firstUse = launchesOf(FIRSTUSE);
    for (int i = 0; i < 5; ++i)
        lifeEvent("stop", FIRSTUSE);
    CHECK_EQ(launchesOf(FIRSTUSE), firstUse + 5);

    lifeEvent("stop", FIRSTUSE);
    CHECK_EQ(launchesOf(FIRSTUSE), firstUse + 5);

    // The phone app counts on its own.
    const int phone = launchesOf(PHONE);
    lifeEvent("stop", PHONE);
    CHECK_EQ(launchesOf(PHONE), phone + 1);
}

TEST(BootManager, launchesAgainOnceTheMinuteIsOver)
{
    if (skipped())
        return;

    const int firstUse = launchesOf(FIRSTUSE);
    advanceMonotonicTime(59LL * 1000 * 1000);
    lifeEvent("stop", FIRSTUSE);
    CHECK_EQ(launchesOf(FIRSTUSE), firstUse);

    advanceMonotonicTime(2LL * 1000 * 1000);
    lifeEvent("stop", FIRSTUSE);
    CHECK_EQ(launchesOf(FIRSTUSE), firstUse + 1);
}

TEST(BootManager, theCompositorComingBackResetsTheLimit)
{
    if (skipped())
        return;

    // Use up the rest of this minute's launches.
    for (int i = 0; i < 4; ++i)
        lifeEvent("stop", FIRSTUSE);
    int firstUse = launchesOf(FIRSTUSE);
    lifeEvent("stop", FIRSTUSE);
    CHECK_EQ(launchesOf(FIRSTUSE), firstUse);

    compositor(false);
    compositor(true);
    firstUse = launchesOf(FIRSTUSE);
    lifeEvent("stop", FIRSTUSE);
    CHECK_EQ(launchesOf(FIRSTUSE), firstUse + 1);
}

TEST(BootManager, subscribesAgainWhenSamComesBack)
{
    if (skipped())
        return;

    const size_t subscriptions = fakels::callsTo("/getAppLifeEvents").size();
    fakels::serverStatus(SAM, false);
    fakels::serverStatus(SAM, true);
    CHECK_EQ(fakels::callsTo("/getAppLifeEvents").size(), subscriptions + 1);

    advanceMonotonicTime(61LL * 1000 * 1000);
    const int firstUse = launchesOf(FIRSTUSE);
    lifeEvent("stop", FIRSTUSE);
    CHECK_EQ(launchesOf(FIRSTUSE), firstUse + 1);
}

TEST(BootManager, survivesASubscriptionTheBusRefuses)
{
    if (skipped())
        return;

    const size_t subscriptions = fakels::callsTo("/getAppLifeEvents").size();
    fakels::serverStatus(SAM, false);
    fakels::failNextCall();
    fakels::serverStatus(SAM, true);
    CHECK_EQ(fakels::callsTo("/getAppLifeEvents").size(), subscriptions);

    // The next time SAM comes up it is asked again.
    fakels::serverStatus(SAM, false);
    fakels::serverStatus(SAM, true);
    CHECK_EQ(fakels::callsTo("/getAppLifeEvents").size(), subscriptions + 1);
}
