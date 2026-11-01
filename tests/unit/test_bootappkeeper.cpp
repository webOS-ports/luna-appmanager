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
 * BootAppKeeper on its own: which boot time and KeepAlive apps it launches
 * again, when, and when it stops. Every case builds its own keeper and its
 * own clock, so they do not depend on each other.
 */

#include "check.h"

#include "BootAppKeeper.h"

namespace
{

const char *LAUNCHER = "com.palm.launcher";
const char *PHONE = "org.webosports.app.phone";
const char *EMAIL = "com.palm.app.email";
const char *PINNED = "org.webosports.app.pinned";   // KeepAlive, not at boot
const char *OTHER = "org.webosports.app.other";
const char *AT_BOOT = "{\"launchedAtBoot\":true}";

typedef std::vector<BootAppKeeper::App> Launches;

struct Keeper
{
    BootAppKeeper keeper;
    long long now = 1000000;

    Keeper()
    {
        std::vector<BootAppKeeper::App> boot;
        const char *ids[] = { LAUNCHER, PHONE, EMAIL };
        for (const char *id : ids) {
            BootAppKeeper::App app;
            app.id = id;
            app.params = AT_BOOT;
            boot.push_back(app);
        }
        std::set<std::string> keepAlive;
        keepAlive.insert(PHONE);
        keepAlive.insert(PINNED);
        keeper.setApps(boot, keepAlive);
        // SAM was there before luna-appmanager looked.
        keeper.samUp(now, true);
    }

    void event(const char *name, const char *appId, bool active = true)
    {
        keeper.lifeEvent(name, appId, now, active);
    }

    //! Moves the clock to the next check and answers it with running.
    Launches check(const std::set<std::string> &running = std::set<std::string>())
    {
        if (!keeper.hasPending())
            return Launches();
        if (keeper.nextDueMs() > now)
            now = keeper.nextDueMs();
        return keeper.launchesFor(keeper.takeDue(now), running, now,
                                  std::function<bool(const std::string &)>());
    }

    void samRestart()
    {
        keeper.samDown();
        keeper.samUp(now, true);
    }
};

bool has(const Launches &launches, const char *appId)
{
    for (const BootAppKeeper::App &app : launches) {
        if (app.id == appId)
            return true;
    }
    return false;
}

std::string paramsOf(const Launches &launches, const char *appId)
{
    for (const BootAppKeeper::App &app : launches) {
        if (app.id == appId)
            return app.params;
    }
    return "<not launched>";
}

std::set<std::string> only(const char *appId)
{
    std::set<std::string> running;
    running.insert(appId);
    return running;
}

} // namespace

TEST(BootAppKeeper, samThereFromTheStartLaunchesNothing)
{
    Keeper k;
    CHECK(!k.keeper.hasPending());
    k.keeper.samUp(k.now, true);
    CHECK(!k.keeper.hasPending());
}

TEST(BootAppKeeper, samBackLaunchesEveryBootAppOnceItSettled)
{
    Keeper k;
    const long long start = k.now;
    k.samRestart();
    CHECK(k.keeper.hasPending());
    CHECK_EQ(k.keeper.nextDueMs(), start + BootAppKeeper::kSamSettleMs);

    // Nothing is due a moment early.
    CHECK_EQ(int(k.keeper.takeDue(start + BootAppKeeper::kSamSettleMs - 1).size()), 0);

    const Launches launches = k.check();
    CHECK_EQ(int(launches.size()), 3);
    CHECK_EQ(paramsOf(launches, PHONE), std::string(AT_BOOT));
    CHECK_EQ(paramsOf(launches, LAUNCHER), std::string(AT_BOOT));
    CHECK_EQ(paramsOf(launches, EMAIL), std::string(AT_BOOT));
    CHECK(!k.keeper.hasPending());
}

TEST(BootAppKeeper, samBackLeavesRunningAppsAlone)
{
    Keeper k;
    k.samRestart();
    std::set<std::string> running;
    running.insert(LAUNCHER);
    running.insert(EMAIL);
    const Launches launches = k.check(running);
    CHECK_EQ(int(launches.size()), 1);
    CHECK(has(launches, PHONE));
}

TEST(BootAppKeeper, samGoingAgainDropsWhatWasDue)
{
    Keeper k;
    k.samRestart();
    k.keeper.samDown();
    CHECK(!k.keeper.hasPending());
    k.keeper.samUp(k.now, true);
    CHECK(k.keeper.hasPending());
}

TEST(BootAppKeeper, samBackOutsideNormalOperationLaunchesNothing)
{
    Keeper k;
    k.keeper.samDown();
    k.keeper.samUp(k.now, false);
    CHECK(!k.keeper.hasPending());
    // Nor later: that coming back is used up.
    k.keeper.samUp(k.now, true);
    CHECK(!k.keeper.hasPending());
}

TEST(BootAppKeeper, anAppClosedOnPurposeStaysClosedWhenSamComesBack)
{
    Keeper k;
    k.event("launch", EMAIL);
    k.event("close", EMAIL);
    k.event("stop", EMAIL);
    CHECK(k.keeper.closedOnPurpose(EMAIL));
    CHECK(!k.keeper.hasPending());

    k.samRestart();
    const Launches launches = k.check();
    CHECK(!has(launches, EMAIL));
    CHECK(has(launches, PHONE));
    CHECK(has(launches, LAUNCHER));
}

TEST(BootAppKeeper, anAppOpenedAgainAfterItsCloseIsKeptAgain)
{
    Keeper k;
    k.event("close", EMAIL);
    k.event("stop", EMAIL);
    k.event("launch", EMAIL);
    CHECK(!k.keeper.closedOnPurpose(EMAIL));
    k.samRestart();
    CHECK(has(k.check(), EMAIL));
}

TEST(BootAppKeeper, anAppThatDiedIsLaunchedWhenSamComesBack)
{
    Keeper k;
    // Not KeepAlive: dying by itself launches nothing at once ...
    k.event("stop", EMAIL);
    CHECK(!k.keeper.hasPending());
    // ... but it was not closed on purpose either.
    k.samRestart();
    CHECK(has(k.check(), EMAIL));
}

TEST(BootAppKeeper, anAppBeingClosedWhenSamWentWasClosedOnPurpose)
{
    Keeper k;
    k.event("close", EMAIL);
    k.samRestart();
    CHECK(!has(k.check(), EMAIL));
}

TEST(BootAppKeeper, aKeepAliveAppClosedOnPurposeIsKeptAnyway)
{
    Keeper k;
    k.event("close", PHONE);
    k.event("stop", PHONE);
    CHECK(k.keeper.hasPending());
    const Launches launches = k.check();
    CHECK(has(launches, PHONE));
    CHECK_EQ(paramsOf(launches, PHONE), std::string(AT_BOOT));

    k.event("close", PHONE);
    k.samRestart();
    CHECK(has(k.check(), PHONE));
}

TEST(BootAppKeeper, aKeepAliveAppThatStopsIsLaunchedASecondLater)
{
    Keeper k;
    const long long start = k.now;
    k.event("stop", PHONE);
    CHECK(k.keeper.hasPending());
    CHECK_EQ(k.keeper.nextDueMs(), start + BootAppKeeper::kStoppedDelayMs);
    const Launches launches = k.check();
    CHECK_EQ(int(launches.size()), 1);
    CHECK_EQ(paramsOf(launches, PHONE), std::string(AT_BOOT));
}

TEST(BootAppKeeper, aKeepAliveAppRunningAgainByTheCheckIsLeftAlone)
{
    Keeper k;
    k.event("stop", PHONE);
    CHECK_EQ(int(k.check(only(PHONE)).size()), 0);
    // That was not a launch: the next stop waits a second again.
    const long long at = k.now;
    k.event("stop", PHONE);
    CHECK_EQ(k.keeper.nextDueMs(), at + BootAppKeeper::kStoppedDelayMs);
}

TEST(BootAppKeeper, otherAppsThatStopAreLeftAlone)
{
    Keeper k;
    k.event("stop", EMAIL);
    k.event("stop", LAUNCHER);
    k.event("stop", OTHER);
    k.event("stop", "");
    CHECK(!k.keeper.hasPending());
}

TEST(BootAppKeeper, stopsOutsideNormalOperationAreLeftAlone)
{
    Keeper k;
    k.event("stop", PHONE, false);
    CHECK(!k.keeper.hasPending());
}

TEST(BootAppKeeper, otherEventsLaunchNothing)
{
    Keeper k;
    const char *events[] = { "launch", "splash", "preload", "foreground", "background",
                             "pause", "close", "Stop", "", "stopped" };
    for (const char *e : events)
        k.event(e, PHONE);
    CHECK(!k.keeper.hasPending());
}

TEST(BootAppKeeper, backsOffAndGivesUpAfterFiveLaunchesInAMinute)
{
    Keeper k;
    const long long waits[] = { 1000, 2000, 4000, 8000, 16000 };
    for (long long wait : waits) {
        const long long at = k.now;
        k.event("stop", PHONE);
        CHECK_EQ(k.keeper.nextDueMs() - at, wait);
        CHECK(has(k.check(), PHONE));
        k.event("launch", PHONE);
    }
    CHECK(!k.keeper.gaveUp(PHONE));

    k.event("stop", PHONE);
    CHECK(!k.keeper.hasPending());
    CHECK(k.keeper.gaveUp(PHONE));
}

TEST(BootAppKeeper, launchesAgainOnceTheMinuteIsOver)
{
    Keeper k;
    const long long first = k.now;
    for (int i = 0; i < BootAppKeeper::kMaxLaunches; ++i) {
        k.event("stop", PHONE);
        k.check();
    }
    k.event("stop", PHONE);
    CHECK(!k.keeper.hasPending());

    // The first launch was a second in; a minute after it, it no longer counts.
    k.now = first + BootAppKeeper::kStoppedDelayMs + BootAppKeeper::kWindowMs + 1;
    k.event("stop", PHONE);
    CHECK(k.keeper.hasPending());
    // Four launches still count: the fifth waits 16 seconds.
    CHECK_EQ(k.keeper.nextDueMs() - k.now, 16000LL);
    CHECK(has(k.check(), PHONE));
}

TEST(BootAppKeeper, aLaunchBySomeoneElseEndsTheGivingUp)
{
    Keeper k;
    for (int i = 0; i < BootAppKeeper::kMaxLaunches; ++i) {
        k.event("stop", PHONE);
        k.check();
    }
    k.event("stop", PHONE);
    CHECK(k.keeper.gaveUp(PHONE));

    // The user opens the phone app.
    k.event("launch", PHONE);
    CHECK(!k.keeper.gaveUp(PHONE));
    const long long at = k.now;
    k.event("stop", PHONE);
    CHECK_EQ(k.keeper.nextDueMs() - at, BootAppKeeper::kStoppedDelayMs);
}

TEST(BootAppKeeper, samComingBackEndsTheGivingUp)
{
    Keeper k;
    for (int i = 0; i < BootAppKeeper::kMaxLaunches; ++i) {
        k.event("stop", PHONE);
        k.check();
    }
    k.event("stop", PHONE);
    CHECK(k.keeper.gaveUp(PHONE));

    k.samRestart();
    CHECK(!k.keeper.gaveUp(PHONE));
    CHECK(has(k.check(), PHONE));
    // And it has its launches back.
    const long long at = k.now;
    k.event("stop", PHONE);
    CHECK_EQ(k.keeper.nextDueMs() - at, 2000LL);
}

TEST(BootAppKeeper, theLimitHoldsAtTheCheckToo)
{
    Keeper k;
    // Stops that come before the check are folded into one.
    k.event("stop", PHONE);
    k.event("stop", PHONE);
    CHECK_EQ(int(k.check().size()), 1);

    // Four more make five; a check that was already due launches nothing.
    for (int i = 1; i < BootAppKeeper::kMaxLaunches; ++i) {
        k.event("stop", PHONE);
        k.check();
    }
    std::vector<std::string> due(1, PHONE);
    CHECK_EQ(int(k.keeper.launchesFor(due, std::set<std::string>(), k.now,
                                      std::function<bool(const std::string &)>()).size()), 0);
    CHECK(k.keeper.gaveUp(PHONE));
}

TEST(BootAppKeeper, aKeepAliveAppRunningWhenSamWentIsLaunchedWithoutBootParams)
{
    Keeper k;
    k.event("launch", PINNED);
    k.event("launch", OTHER);
    k.samRestart();
    const Launches launches = k.check();
    CHECK(has(launches, PINNED));
    CHECK_EQ(paramsOf(launches, PINNED), std::string());
    CHECK(!has(launches, OTHER));
}

TEST(BootAppKeeper, aKeepAliveAppNotRunningWhenSamWentStaysClosed)
{
    Keeper k;
    k.event("launch", PINNED);
    k.event("close", PINNED);
    k.event("stop", PINNED);
    k.check();               // launched again, as KeepAlive
    k.event("stop", PINNED);
    k.keeper.cancelPending();
    k.samRestart();
    CHECK(!has(k.check(), PINNED));
}

TEST(BootAppKeeper, appsSamSaysRunAreKnownToHaveRunWhenItGoes)
{
    Keeper k;
    k.samRestart();
    k.check(only(PINNED));   // running after the restart, no event for it
    k.samRestart();
    CHECK(has(k.check(), PINNED));
}

TEST(BootAppKeeper, aVetoedAppIsNotLaunchedAndNotCounted)
{
    Keeper k;
    k.event("stop", PHONE);
    k.now = k.keeper.nextDueMs();
    const std::vector<std::string> due = k.keeper.takeDue(k.now);
    const Launches launches = k.keeper.launchesFor(due, std::set<std::string>(), k.now,
        [](const std::string &appId) { return appId != PHONE; });
    CHECK_EQ(int(launches.size()), 0);
    const long long at = k.now;
    k.event("stop", PHONE);
    CHECK_EQ(k.keeper.nextDueMs() - at, BootAppKeeper::kStoppedDelayMs);
}

TEST(BootAppKeeper, theEarliestCheckComesFirst)
{
    Keeper k;
    k.samRestart();                          // due in 3 s
    const long long at = k.now;
    k.event("stop", PHONE);                  // due in 1 s
    CHECK_EQ(k.keeper.nextDueMs(), at + BootAppKeeper::kStoppedDelayMs);
    k.now = k.keeper.nextDueMs();
    const std::vector<std::string> due = k.keeper.takeDue(k.now);
    CHECK_EQ(int(due.size()), 1);
    CHECK(k.keeper.hasPending());
    CHECK_EQ(k.keeper.nextDueMs(), at + BootAppKeeper::kSamSettleMs);
}
