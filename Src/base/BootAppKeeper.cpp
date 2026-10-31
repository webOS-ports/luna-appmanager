/* @@@LICENSE
*
* Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */

#include "BootAppKeeper.h"

#include <glib.h>

namespace
{

//! Events SAM sends for an app that is, or is about to be, running.
bool isRunningEvent(const std::string &event)
{
    return event == "launch" || event == "splash" || event == "preload" ||
           event == "foreground" || event == "background" || event == "pause";
}

} // namespace

BootAppKeeper::BootAppKeeper() :
    m_samSeenDown(false)
{
}

void BootAppKeeper::setApps(const std::vector<App> &bootApps, const std::set<std::string> &keepAlive)
{
    m_bootApps = bootApps;
    m_keepAlive = keepAlive;
}

bool BootAppKeeper::isKeepAlive(const std::string &appId) const
{
    return m_keepAlive.count(appId) != 0;
}

bool BootAppKeeper::closedOnPurpose(const std::string &appId) const
{
    return m_closedOnPurpose.count(appId) != 0;
}

bool BootAppKeeper::gaveUp(const std::string &appId) const
{
    return m_gaveUp.count(appId) != 0;
}

std::string BootAppKeeper::paramsFor(const std::string &appId) const
{
    for (const App &app : m_bootApps) {
        if (app.id == appId)
            return app.params;
    }
    // KeepAlive but not launched at boot: launched the way the ExecuteUnlock
    // path in ApplicationManager launches a boot time app again.
    return std::string();
}

void BootAppKeeper::schedule(const std::string &appId, long long dueMs)
{
    std::map<std::string, long long>::iterator it = m_pending.find(appId);
    if (it == m_pending.end())
        m_pending[appId] = dueMs;
    else if (dueMs < it->second)
        it->second = dueMs;
}

size_t BootAppKeeper::recentLaunches(const std::string &appId, long long nowMs)
{
    std::map<std::string, std::deque<long long> >::iterator it = m_launches.find(appId);
    if (it == m_launches.end())
        return 0;
    std::deque<long long> &times = it->second;
    while (!times.empty() && nowMs - times.front() > kWindowMs)
        times.pop_front();
    return times.size();
}

void BootAppKeeper::lifeEvent(const std::string &event, const std::string &appId, long long nowMs, bool active)
{
    if (appId.empty())
        return;

    if (isRunningEvent(event)) {
        m_running.insert(appId);
        m_closing.erase(appId);
        m_closedOnPurpose.erase(appId);
        // Launched by someone else after it was given up on: it gets its
        // launches back.
        if (m_gaveUp.erase(appId))
            m_launches.erase(appId);
        return;
    }

    if (event == "close") {
        m_closing.insert(appId);
        return;
    }

    if (event != "stop")
        return;

    m_running.erase(appId);
    // SAM sends "close" when it was asked to close the app - the card swiped
    // away, or another app asking - and "stop" alone when the app ended
    // without being asked: it crashed, quit by itself or was killed.
    if (m_closing.erase(appId))
        m_closedOnPurpose.insert(appId);

    if (!active || !isKeepAlive(appId))
        return;

    const size_t recent = recentLaunches(appId, nowMs);
    if (recent >= static_cast<size_t>(kMaxLaunches)) {
        if (m_gaveUp.insert(appId).second)
            g_warning("%s stopped again after %d launches within a minute, not launching it again",
                      appId.c_str(), kMaxLaunches);
        return;
    }

    schedule(appId, nowMs + (kStoppedDelayMs << recent));
}

void BootAppKeeper::samDown()
{
    m_samSeenDown = true;
    m_runningAtSamDown.insert(m_running.begin(), m_running.end());
    m_running.clear();
    // Being closed when SAM went: that close was asked for.
    m_closedOnPurpose.insert(m_closing.begin(), m_closing.end());
    m_closing.clear();
    // Nothing can be checked or launched without SAM; samUp() starts over.
    m_pending.clear();
}

void BootAppKeeper::samUp(long long nowMs, bool active)
{
    if (!m_samSeenDown)
        return;
    m_samSeenDown = false;

    std::set<std::string> wasRunning;
    wasRunning.swap(m_runningAtSamDown);

    if (!active)
        return;

    // SAM going away ended these apps, not anything they did: they start
    // with no launches counted against them. One closed on purpose is
    // weeded out by launchesFor(), which also sees a close that comes
    // before the check.
    for (const App &app : m_bootApps) {
        m_launches.erase(app.id);
        m_gaveUp.erase(app.id);
        schedule(app.id, nowMs + kSamSettleMs);
    }

    // Scheduling a boot time app again changes nothing.
    for (const std::string &appId : wasRunning) {
        if (!isKeepAlive(appId))
            continue;
        m_launches.erase(appId);
        m_gaveUp.erase(appId);
        schedule(appId, nowMs + kSamSettleMs);
    }
}

void BootAppKeeper::cancelPending()
{
    m_pending.clear();
}

bool BootAppKeeper::hasPending() const
{
    return !m_pending.empty();
}

long long BootAppKeeper::nextDueMs() const
{
    long long next = 0;
    bool first = true;
    for (const std::pair<const std::string, long long> &entry : m_pending) {
        if (first || entry.second < next)
            next = entry.second;
        first = false;
    }
    return next;
}

std::vector<std::string> BootAppKeeper::takeDue(long long nowMs)
{
    std::vector<std::string> due;
    std::map<std::string, long long>::iterator it = m_pending.begin();
    while (it != m_pending.end()) {
        if (it->second <= nowMs) {
            due.push_back(it->first);
            it = m_pending.erase(it);
        }
        else {
            ++it;
        }
    }
    return due;
}

std::vector<BootAppKeeper::App> BootAppKeeper::launchesFor(const std::vector<std::string> &due,
                                                           const std::set<std::string> &running,
                                                           long long nowMs,
                                                           const std::function<bool(const std::string &)> &canLaunch)
{
    // SAM does not tell of apps that were running before it came back; this
    // is how those are learned.
    m_running.insert(running.begin(), running.end());

    std::vector<App> launches;
    for (const std::string &appId : due) {
        if (running.count(appId))
            continue;
        if (closedOnPurpose(appId) && !isKeepAlive(appId))
            continue;
        if (canLaunch && !canLaunch(appId))
            continue;

        if (recentLaunches(appId, nowMs) >= static_cast<size_t>(kMaxLaunches)) {
            if (m_gaveUp.insert(appId).second)
                g_warning("%s was launched %d times within a minute, not launching it again",
                          appId.c_str(), kMaxLaunches);
            continue;
        }

        m_launches[appId].push_back(nowMs);
        App app;
        app.id = appId;
        app.params = paramsFor(appId);
        launches.push_back(app);
    }
    return launches;
}
