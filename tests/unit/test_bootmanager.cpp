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

#include "Settings.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QMetaObject>
#include <QThread>

#include <json.h>

namespace
{

const char *FIRSTUSE = "org.webosports.app.firstuse";
const char *PHONE = "org.webosports.app.phone";
const char *COMPOSITOR = "com.webos.surfacemanager-cardshell";
const char *SAM = "com.webos.service.applicationmanager";
const char *LAUNCHER = "com.palm.launcher";
const char *SYSTEMUI = "com.palm.systemui";
const char *EMAIL = "com.palm.app.email";
const char *AT_BOOT = "{\"launchedAtBoot\":true}";

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

TEST(BootManager, launchesFirstUseAgainWhenSamComesBack)
{
    if (skipped())
        return;

    // Use up the limit; SAM coming back resets it, like the compositor
    // coming back.
    for (int i = 0; i < 5; ++i)
        lifeEvent("stop", FIRSTUSE);
    int firstUse = launchesOf(FIRSTUSE);
    int phone = launchesOf(PHONE);
    lifeEvent("stop", FIRSTUSE);
    CHECK_EQ(launchesOf(FIRSTUSE), firstUse);

    fakels::serverStatus(SAM, false);
    fakels::serverStatus(SAM, true);
    runEvents();
    answerLaunches();
    CHECK_EQ(launchesOf(FIRSTUSE), firstUse + 1);
    CHECK_EQ(launchesOf(PHONE), phone + 1);
    CHECK_EQ(launchField(launches().back(), "params"), std::string("{\"mode\":\"first-use\"}"));

    firstUse = launchesOf(FIRSTUSE);
    lifeEvent("stop", FIRSTUSE);
    CHECK_EQ(launchesOf(FIRSTUSE), firstUse + 1);

    // SAM there all along: nothing came back.
    firstUse = launchesOf(FIRSTUSE);
    fakels::serverStatus(SAM, true);
    CHECK_EQ(launchesOf(FIRSTUSE), firstUse);

    // Without the compositor that is left to the compositor coming back.
    phone = launchesOf(PHONE);
    compositor(false);
    fakels::serverStatus(SAM, false);
    fakels::serverStatus(SAM, true);
    CHECK_EQ(launchesOf(FIRSTUSE), firstUse);
    CHECK_EQ(launchesOf(PHONE), phone);
    compositor(true);
    CHECK_EQ(launchesOf(FIRSTUSE), firstUse + 1);
    CHECK_EQ(launchesOf(PHONE), phone + 1);
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

/*
 * Normal operation: the boot time and KeepAlive apps launched again when SAM
 * comes back and when a KeepAlive app stops. These run whether or not first
 * use could, and put BootManager in the normal state themselves. Apart from
 * one case that waits for the real timer, the checks are made due by moving
 * the clock and run by calling the timer's slot.
 */

namespace
{

size_t g_runningAnswered = 0;

std::vector<fakels::Call> runningQueries()
{
    return fakels::callsTo("/com.webos.service.applicationmanager/running");
}

//! Runs the check the timer would, now.
void keeperCheck()
{
    QMetaObject::invokeMethod(BootManager::instance(), "onAppKeeperTimeout");
    runEvents();
}

//! SAM answers every query for running apps so far with payload.
int answerRunningWith(const std::string &payload)
{
    const std::vector<fakels::Call> all = runningQueries();
    int answered = 0;
    for (; g_runningAnswered < all.size(); ++g_runningAnswered, ++answered)
        fakels::reply(all[g_runningAnswered], payload);
    runEvents();
    answerLaunches();
    return answered;
}

//! ... with these apps running.
int answerRunning(const std::vector<const char *> &appIds)
{
    std::string list;
    for (size_t i = 0; i < appIds.size(); ++i) {
        if (i)
            list += ",";
        list += std::string("{\"id\":\"") + appIds[i] + "\",\"processid\":\"1\"}";
    }
    return answerRunningWith("{\"running\":[" + list + "],\"returnValue\":true,\"subscribed\":false}");
}

void samRestarts()
{
    fakels::serverStatus(SAM, false);
    fakels::serverStatus(SAM, true);
    runEvents();
}

std::string lastLaunchParamsOf(const char *appId)
{
    const std::string quoted = std::string("\"") + appId + "\"";
    const std::vector<fakels::Call> all = launches();
    for (size_t i = all.size(); i > 0; --i) {
        if (launchField(all[i - 1], "id") == quoted)
            return launchField(all[i - 1], "params");
    }
    return "<not launched>";
}

const long long kSecond = 1000LL * 1000;

} // namespace

TEST(BootManagerNormal, launchesTheShellAppsOnEnteringNormalOperation)
{
    ensureApplication();
    Settings *settings = Settings::LunaSettings();
    // No /tmp/suspend_active when this runs on a device.
    settings->hardwareType = Settings::HardwareTypeDesktop;
    settings->appsToLaunchAtBoot.clear();
    settings->appsToLaunchAtBoot.insert(PHONE);
    settings->appsToLaunchAtBoot.insert(EMAIL);
    settings->appsToKeepAlive.clear();
    settings->appsToKeepAlive.insert(PHONE);

    BootManager::instance();
    runEvents();
    // SAM is up, and subscribed to, before normal operation starts.
    fakels::serverStatus(SAM, true);

    const int launcher = launchesOf(LAUNCHER);
    const int systemui = launchesOf(SYSTEMUI);
    BootManager::instance()->switchState(BOOT_STATE_NORMAL);
    runEvents();
    answerLaunches();
    CHECK_EQ(int(BootManager::instance()->currentState()), int(BOOT_STATE_NORMAL));
    CHECK_EQ(launchesOf(LAUNCHER), launcher + 1);
    CHECK_EQ(launchesOf(SYSTEMUI), systemui + 1);
    // Nothing to check yet.
    keeperCheck();
    CHECK_EQ(int(runningQueries().size()), 0);
}

TEST(BootManagerNormal, launchesTheBootAppsAgainWhenSamComesBack)
{
    const int launcher = launchesOf(LAUNCHER);
    const int systemui = launchesOf(SYSTEMUI);
    const int phone = launchesOf(PHONE);
    const int email = launchesOf(EMAIL);
    const size_t queries = runningQueries().size();

    samRestarts();
    // SAM gets a while to learn what WebAppMgr runs.
    keeperCheck();
    CHECK_EQ(runningQueries().size(), queries);

    advanceMonotonicTime(3 * kSecond);
    keeperCheck();
    CHECK_EQ(runningQueries().size(), queries + 1);
    CHECK_EQ(answerRunning({ LAUNCHER, EMAIL }), 1);

    CHECK_EQ(launchesOf(PHONE), phone + 1);
    CHECK_EQ(lastLaunchParamsOf(PHONE), std::string(AT_BOOT));
    CHECK_EQ(launchesOf(SYSTEMUI), systemui + 1);
    CHECK_EQ(lastLaunchParamsOf(SYSTEMUI), std::string(AT_BOOT));
    CHECK_EQ(launchesOf(LAUNCHER), launcher);
    CHECK_EQ(launchesOf(EMAIL), email);
}

TEST(BootManagerNormal, launchesThePhoneAgainWhenItStops)
{
    // This one waits for the timer itself.
    const int phone = launchesOf(PHONE);
    const size_t queries = runningQueries().size();
    CHECK(lifeEvent("stop", PHONE));

    QElapsedTimer waited;
    waited.start();
    while (runningQueries().size() == queries && waited.elapsed() < 5000) {
        QThread::msleep(20);
        runEvents();
    }
    CHECK_EQ(runningQueries().size(), queries + 1);
    CHECK(waited.elapsed() >= 900);
    CHECK_EQ(answerRunning({ LAUNCHER, SYSTEMUI }), 1);
    CHECK_EQ(launchesOf(PHONE), phone + 1);
    CHECK_EQ(lastLaunchParamsOf(PHONE), std::string(AT_BOOT));
}

TEST(BootManagerNormal, leavesARunningPhoneAlone)
{
    const int phone = launchesOf(PHONE);
    CHECK(lifeEvent("stop", PHONE));
    advanceMonotonicTime(20 * kSecond);
    keeperCheck();
    CHECK_EQ(answerRunning({ PHONE }), 1);
    CHECK_EQ(launchesOf(PHONE), phone);
}

TEST(BootManagerNormal, keepsAnAppClosedOnPurposeClosedWhenSamComesBack)
{
    const int phone = launchesOf(PHONE);
    const int email = launchesOf(EMAIL);
    CHECK(lifeEvent("close", EMAIL));
    CHECK(lifeEvent("stop", EMAIL));
    samRestarts();
    advanceMonotonicTime(3 * kSecond);
    keeperCheck();
    CHECK_EQ(answerRunning({}), 1);
    CHECK_EQ(launchesOf(EMAIL), email);
    CHECK_EQ(launchesOf(PHONE), phone + 1);

    // Opened again, it is kept again.
    CHECK(lifeEvent("launch", EMAIL));
    samRestarts();
    advanceMonotonicTime(3 * kSecond);
    keeperCheck();
    CHECK_EQ(answerRunning({ PHONE, LAUNCHER, SYSTEMUI }), 1);
    CHECK_EQ(launchesOf(EMAIL), email + 1);
}

TEST(BootManagerNormal, launchesNothingWithoutAnAnswerFromSam)
{
    const int phone = launchesOf(PHONE);
    const char *payloads[] = {
        "",
        "not json",
        "null",
        "[]",
        "{}",
        "{\"returnValue\":true}",
        "{\"returnValue\":true,\"running\":{}}",
        "{\"returnValue\":true,\"running\":\"org.webosports.app.phone\"}",
        "{\"returnValue\":false,\"running\":[]}",
        "{\"returnValue\":\"yes\",\"running\":[]}",
        "{\"returnValue\":false,\"errorCode\":-1,\"errorText\":\"denied\"}",
        "{\"serviceName\":\"com.webos.service.applicationmanager\",\"returnValue\":false,"
            "\"errorCode\":-1,\"errorText\":\"com.webos.service.applicationmanager is not running.\"}",
        "{\"returnValue\":true,\"running\":[",
    };
    for (const char *payload : payloads) {
        CHECK(lifeEvent("stop", PHONE));
        advanceMonotonicTime(20 * kSecond);
        keeperCheck();
        CHECK_EQ(answerRunningWith(payload), 1);
    }
    CHECK_EQ(launchesOf(PHONE), phone);
}

TEST(BootManagerNormal, readsOnlyTheIdsOfWhatRuns)
{
    advanceMonotonicTime(61 * kSecond);
    const int phone = launchesOf(PHONE);
    CHECK(lifeEvent("stop", PHONE));
    advanceMonotonicTime(2 * kSecond);
    keeperCheck();
    // Nothing here names the phone app as running.
    CHECK_EQ(answerRunningWith("{\"returnValue\":true,\"running\":[1,null,\"org.webosports.app.phone\","
                               "{\"id\":5},{\"id\":null},{\"appId\":\"org.webosports.app.phone\"},[]]}"), 1);
    CHECK_EQ(launchesOf(PHONE), phone + 1);
}

TEST(BootManagerNormal, ignoresAnAnswerFromBeforeSamWent)
{
    advanceMonotonicTime(61 * kSecond);
    const int phone = launchesOf(PHONE);
    CHECK(lifeEvent("stop", PHONE));
    advanceMonotonicTime(2 * kSecond);
    keeperCheck();
    const std::vector<fakels::Call> stale = runningQueries();
    CHECK(!stale.empty());

    samRestarts();
    if (!stale.empty())
        fakels::reply(stale.back(), "{\"running\":[],\"returnValue\":true}");
    g_runningAnswered = runningQueries().size();
    runEvents();
    answerLaunches();
    CHECK_EQ(launchesOf(PHONE), phone);

    // SAM coming back brings its own check.
    advanceMonotonicTime(3 * kSecond);
    keeperCheck();
    CHECK_EQ(answerRunning({ LAUNCHER, SYSTEMUI, EMAIL }), 1);
    CHECK_EQ(launchesOf(PHONE), phone + 1);
}

TEST(BootManagerNormal, survivesAQueryTheBusRefuses)
{
    advanceMonotonicTime(61 * kSecond);
    const int phone = launchesOf(PHONE);
    const size_t queries = runningQueries().size();
    CHECK(lifeEvent("stop", PHONE));
    advanceMonotonicTime(2 * kSecond);
    fakels::failNextCall();
    keeperCheck();
    CHECK_EQ(runningQueries().size(), queries);
    CHECK_EQ(launchesOf(PHONE), phone);

    // The next stop is checked as usual.
    CHECK(lifeEvent("stop", PHONE));
    advanceMonotonicTime(2 * kSecond);
    keeperCheck();
    CHECK_EQ(answerRunning({}), 1);
    CHECK_EQ(launchesOf(PHONE), phone + 1);
}

TEST(BootManagerNormal, dropsTheChecksWhenNormalOperationEnds)
{
    advanceMonotonicTime(61 * kSecond);
    const int phone = launchesOf(PHONE);
    const size_t queries = runningQueries().size();
    CHECK(lifeEvent("stop", PHONE));
    compositor(false);
    CHECK_EQ(int(BootManager::instance()->currentState()), int(BOOT_STATE_STARTUP));
    advanceMonotonicTime(20 * kSecond);
    keeperCheck();
    CHECK_EQ(runningQueries().size(), queries);

    // A stop or SAM coming back outside normal operation is no reason either:
    // entering it launches every boot time app anyway.
    CHECK(lifeEvent("stop", PHONE));
    samRestarts();
    advanceMonotonicTime(20 * kSecond);
    keeperCheck();
    CHECK_EQ(runningQueries().size(), queries);
    CHECK_EQ(launchesOf(PHONE), phone);

    compositor(true);
    BootManager::instance()->switchState(BOOT_STATE_NORMAL);
    runEvents();
    answerLaunches();
    CHECK_EQ(int(BootManager::instance()->currentState()), int(BOOT_STATE_NORMAL));
}

TEST(BootManagerNormal, givesUpOnAPhoneThatKeepsStopping)
{
    advanceMonotonicTime(61 * kSecond);
    const int phone = launchesOf(PHONE);
    // Launched after 1, 2, 4, 8 and 16 seconds: within the minute.
    for (int i = 0; i < BootAppKeeper::kMaxLaunches; ++i) {
        CHECK(lifeEvent("stop", PHONE));
        advanceMonotonicTime((1LL << i) * kSecond);
        keeperCheck();
        CHECK_EQ(answerRunning({}), 1);
        CHECK(lifeEvent("launch", PHONE));
    }
    CHECK_EQ(launchesOf(PHONE), phone + BootAppKeeper::kMaxLaunches);

    const size_t queries = runningQueries().size();
    CHECK(lifeEvent("stop", PHONE));
    advanceMonotonicTime(16 * kSecond);
    keeperCheck();
    CHECK_EQ(runningQueries().size(), queries);
    CHECK_EQ(launchesOf(PHONE), phone + BootAppKeeper::kMaxLaunches);

    // SAM coming back gives it its launches back.
    samRestarts();
    advanceMonotonicTime(3 * kSecond);
    keeperCheck();
    CHECK_EQ(answerRunning({ LAUNCHER, SYSTEMUI, EMAIL }), 1);
    CHECK_EQ(launchesOf(PHONE), phone + BootAppKeeper::kMaxLaunches + 1);
}
