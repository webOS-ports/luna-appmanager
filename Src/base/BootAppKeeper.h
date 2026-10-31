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

#ifndef BOOTAPPKEEPER_H
#define BOOTAPPKEEPER_H

#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

/**
 * Decides which of the boot time and KeepAlive apps to launch again, and
 * when. It keeps no bus connection and reads no clock: BootManager feeds it
 * SAM's app life events, SAM going away and coming back, and the time, asks
 * SAM which apps run once something is due, and launches what this hands
 * back.
 *
 * Two things launch an app again:
 *
 * - SAM coming back after it was seen gone. SAM's apps live in its control
 *   group, so a restart of sam.service, by hand, by a deploy script or by
 *   systemd after a crash, ends every app it started, and nothing launched
 *   them again: the phone app was gone until the next boot. Every boot time
 *   app is launched again, except one closed on purpose that is not
 *   KeepAlive, and so is every KeepAlive app that ran when SAM went.
 *
 * - A KeepAlive app stopping, whether it died or was closed. Legacy webOS
 *   kept such an app alive when its card was closed; here the app is
 *   launched again, hidden, the way it is at boot. One that keeps stopping
 *   is launched after 1, 2, 4, 8 and 16 seconds, and then no more until a
 *   minute has gone by since the oldest of those launches, someone else
 *   launches it, or SAM comes back.
 *
 * An app that is running by the time the check is made is left alone.
 */
class BootAppKeeper
{
public:
    struct App
    {
        std::string id;
        std::string params;
    };

    //! Launches of one app within this window count towards the limit.
    static constexpr long long kWindowMs = 60000;
    //! Launches of one app within kWindowMs before it is given up on.
    static constexpr int kMaxLaunches = 5;
    //! Wait after an app stopped before the first launch; doubles for each
    //! launch within kWindowMs.
    static constexpr long long kStoppedDelayMs = 1000;
    //! Wait after SAM came back, for it to learn the web apps WebAppMgr
    //! still runs, before it is asked what runs.
    static constexpr long long kSamSettleMs = 3000;

    BootAppKeeper();

    /**
     * bootApps: launched at boot, with their parameters, in launch order.
     * keepAlive: apps to keep running. Set when normal operation starts;
     * what was learned of the apps so far is kept.
     */
    void setApps(const std::vector<App> &bootApps, const std::set<std::string> &keepAlive);

    /**
     * One of SAM's app life events. active: whether to act on it, that is
     * whether the device is in normal operation; what is learned is kept
     * either way.
     */
    void lifeEvent(const std::string &event, const std::string &appId, long long nowMs, bool active);

    //! SAM went away. Its apps went with it; whatever was due is dropped.
    void samDown();

    //! SAM is there. Only once it was seen gone does this launch anything.
    void samUp(long long nowMs, bool active);

    //! Forgets what was due, as when normal operation ends or starts over.
    void cancelPending();

    bool hasPending() const;
    //! When the earliest pending check is due. Only valid with hasPending().
    long long nextDueMs() const;

    //! The apps whose check is due by nowMs, taken off the pending list.
    std::vector<std::string> takeDue(long long nowMs);

    /**
     * What to launch of due, given what SAM says runs. canLaunch can veto an
     * app (one being installed, say) without that counting as a launch.
     * Every app returned counts as launched at nowMs.
     */
    std::vector<App> launchesFor(const std::vector<std::string> &due,
                                 const std::set<std::string> &running,
                                 long long nowMs,
                                 const std::function<bool(const std::string &)> &canLaunch);

    bool isKeepAlive(const std::string &appId) const;
    bool closedOnPurpose(const std::string &appId) const;
    bool gaveUp(const std::string &appId) const;

private:
    std::string paramsFor(const std::string &appId) const;
    void schedule(const std::string &appId, long long dueMs);
    //! Drops launches older than kWindowMs and returns how many are left.
    size_t recentLaunches(const std::string &appId, long long nowMs);

    std::vector<App> m_bootApps;
    std::set<std::string> m_keepAlive;

    std::set<std::string> m_running;          //!< seen launched, not seen stopped
    std::set<std::string> m_runningAtSamDown;
    std::set<std::string> m_closing;          //!< "close" seen, "stop" not yet
    std::set<std::string> m_closedOnPurpose;
    std::set<std::string> m_gaveUp;
    std::map<std::string, std::deque<long long> > m_launches;
    std::map<std::string, long long> m_pending;   //!< app -> when to check
    bool m_samSeenDown;
};

#endif
