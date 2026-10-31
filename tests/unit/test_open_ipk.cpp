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
 * com.palm.applicationManager/open and packages. Only the trusted installer
 * apps may have open install an ipk, whether the target names one or the
 * download only turns out to be one once it is done (a redirect renames it).
 * com.palm.appinstaller/install still installs what it downloads.
 *
 * open downloads a remote target only for a mime type whose handler is not
 * streamed; the caller names the mime type. The image has such handlers
 * (text/x-vcard for com.palm.app.contacts in command-resource-handlers.json),
 * and the tests add that one.
 *
 * The installer runs in minimal mode here, so an install that is let through
 * is queued and never run: nothing is installed on the host. The test then
 * plays the installer finishing it (oneCommandProcessed(), which asserts
 * that a command was queued).
 */

#include "check.h"
#include "fake_ls2.h"

#include "ApplicationInstaller.h"
#include "ApplicationManager.h"
#include "MimeSystem.h"
#include "Settings.h"

#include <QCoreApplication>

#include <json.h>

namespace
{

const char *APPMGR = "com.palm.applicationManager";
const char *THIRD_PARTY = "com.example.thirdparty";
const char *TRUSTED_INSTALLER = "org.webosports.app.settings";
const char *UNAUTHORIZED = "Unauthorized call to open an ipk";

void setUp()
{
    static int argc = 1;
    static char name[] = "luna-appmanager-tests";
    static char *argv[] = { name, NULL };
    if (!QCoreApplication::instance())
        new QCoreApplication(argc, argv);
    // Keeps the installer from running a queued install, and from
    // registering its own service.
    Settings::LunaSettings()->uiType = Settings::UI_MINIMAL;
    ApplicationManager::instance();
    ApplicationInstaller::instance();
    static bool handlerAdded = false;
    if (!handlerAdded) {
        std::string extension("vcf");
        MimeSystem::instance()->addResourceHandler(extension, "text/x-vcard", true,
                                                   "com.palm.app.contacts", NULL, true);
        handlerAdded = true;
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

std::string joined(const std::vector<std::string> &all)
{
    std::string s;
    for (size_t i = 0; i < all.size(); ++i)
        s += all[i] + "\n";
    return s;
}

//! Opens target as appId; returns open's reply.
std::string open(const char *appId, const std::string &target)
{
    const size_t before = fakels::messageReplies().size();
    CHECK(fakels::callMethod(APPMGR, "/", "open",
                             "{\"target\":\"" + target + "\",\"mime\":\"text/x-vcard\"}", appId));
    const std::vector<std::string> &replies = fakels::messageReplies();
    CHECK_EQ(replies.size(), before + 1);
    return replies.size() > before ? replies.back() : std::string();
}

//! The download manager finishes the newest download, saved as savedAs.
//! Returns what was relayed to the subscribers of ticket.
std::string finishDownload(const std::string &ticket, const std::string &savedAs)
{
    const std::vector<fakels::Call> downloads = fakels::callsTo("/download");
    CHECK_EQ(downloads.size(), 1u);
    if (downloads.empty())
        return std::string();
    fakels::reply(downloads.back(),
                  "{\"ticket\":\"7\",\"completed\":true,\"returnValue\":true,\"target\":\"" + savedAs + "\"}");
    return joined(fakels::subscriptionReplies(ticket));
}

} // namespace

TEST(OpenIpk, TargetNamingAnIpkIsRefused)
{
    setUp();
    const std::string reply = open(THIRD_PARTY, "http://example.invalid/app.ipk");
    CHECK_STR_CONTAINS(reply, UNAUTHORIZED);
    CHECK(fakels::callsTo("/download").empty());
}

TEST(OpenIpk, DownloadThatIsAnIpkIsNotInstalledForAnApp)
{
    setUp();
    const std::string reply = open(THIRD_PARTY, "http://example.invalid/download");
    CHECK_STR_CONTAINS(reply, "Download requested");
    const std::string ticket = field(reply, "ticket");
    CHECK(!ticket.empty());
    const std::string relayed = finishDownload(ticket, "/media/internal/downloads/app.ipk");
    CHECK_STR_CONTAINS(relayed, UNAUTHORIZED);
}

TEST(OpenIpk, DownloadThatIsAnIpkIsInstalledForATrustedInstaller)
{
    setUp();
    const std::string reply = open(TRUSTED_INSTALLER, "http://example.invalid/download");
    CHECK_STR_CONTAINS(reply, "Download requested");
    const std::string relayed = finishDownload(field(reply, "ticket"), "/media/internal/downloads/app.ipk");
    CHECK_STR_LACKS(relayed, UNAUTHORIZED);
    CHECK_STR_LACKS(relayed, "installation of ipkg failed");
    ApplicationInstaller::instance()->oneCommandProcessed();
}

TEST(OpenIpk, ReplyThatStartsADownloadIsASuccess)
{
    setUp();
    const std::string reply = open(THIRD_PARTY, "http://example.invalid/download");
    CHECK_STR_CONTAINS(reply, "Download requested");
    CHECK_EQ(field(reply, "returnValue"), std::string("true"));
    CHECK_STR_LACKS(reply, "errorText");
    finishDownload(field(reply, "ticket"), "/media/internal/downloads/doc.pdf");
    const std::vector<fakels::Call> launches = fakels::callsTo("/launch");
    CHECK_EQ(launches.size(), 1u);
    if (!launches.empty())
        fakels::reply(launches.back(), "{\"returnValue\":true,\"instanceId\":\"fake\"}");
}

TEST(OpenIpk, ReplyToAnOpenThatFailsIsAFailure)
{
    setUp();
    const std::string reply = open(THIRD_PARTY, "http://example.invalid/app.ipk");
    CHECK_EQ(field(reply, "returnValue"), std::string("false"));
    CHECK(!field(reply, "errorText").empty());
}

TEST(OpenIpk, DownloadThatIsNoIpkIsNotRefused)
{
    setUp();
    const std::string reply = open(THIRD_PARTY, "http://example.invalid/download");
    const std::string relayed = finishDownload(field(reply, "ticket"), "/media/internal/downloads/doc.pdf");
    CHECK_STR_LACKS(relayed, UNAUTHORIZED);
    // Opened in its handler instead; SAM answers the launch.
    const std::vector<fakels::Call> launches = fakels::callsTo("/launch");
    CHECK_EQ(launches.size(), 1u);
    if (!launches.empty()) {
        CHECK_STR_CONTAINS(launches.back().payload, "com.palm.app.contacts");
        fakels::reply(launches.back(), "{\"returnValue\":true,\"instanceId\":\"fake\"}");
    }
}

TEST(OpenIpk, AppinstallerInstallStillInstallsItsDownload)
{
    setUp();
    const unsigned long ticket = ApplicationManager::generateNewTicket();
    LSHandle *handle = NULL;
    LSError error;
    LSErrorInit(&error);
    CHECK(LSRegister("com.palm.appinstaller", &handle, &error));
    CHECK(ApplicationInstaller::instance()->downloadAndInstall(handle, "http://example.invalid/app.ipk",
                                                                NULL, NULL, ticket, true));
    char key[32];
    snprintf(key, sizeof(key), "%lu", ticket);
    const std::string relayed = finishDownload(key, "/media/internal/downloads/app.ipk");
    CHECK_STR_CONTAINS(relayed, "\"completed\"");
    CHECK_STR_LACKS(relayed, UNAUTHORIZED);
    ApplicationInstaller::instance()->oneCommandProcessed();
}
