/* @@@LICENSE
*
* Copyright (c) 2013 Simon Busch
* Copyright (c) 2016 Herman van Hazendonk <github.com@herrie.org>
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

#include <glib.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>
#include <pbnjson.hpp>
#include <map>
#include <set>
#include <QFile>
#include <QDebug>
#include <fcntl.h>

#include "Common.h"
#include "ApplicationDescription.h"
#include "ApplicationManager.h"
#include "ApplicationDescription.h"
#include "AnimationSettings.h"
#include "HostBase.h"
#include "Logging.h"
#include "Settings.h"
#include "BootManager.h"
#include "Utils.h"
#include "ApplicationProcessManager.h"
#include "WebAppMgrProxy.h"

#include <json.h>
#include <pbnjson.hpp>
#include "JSONUtils.h"

static bool cbGetStatus(LSHandle *handle, LSMessage *message, void* user_data);

static LSMethod s_methods[]  = {
	{ "getStatus", cbGetStatus },
	{ 0, 0 },
};

std::string bootStateToStr(BootState state)
{
	std::string stateStr = "unknown";

	switch (state) {
	case BOOT_STATE_STARTUP:
		stateStr = "startup";
		break;
	case BOOT_STATE_FIRSTUSE:
		stateStr = "firstuse";
		break;
	case BOOT_STATE_NORMAL:
		stateStr = "normal";
		break;
	default:
		break;
	}

	return stateStr;
}

DisplayBlocker::DisplayBlocker() :
    m_service(0),
    m_token(0),
    m_acquired(false)
{
	LSError lserror;
	LSErrorInit(&lserror);

	GMainLoop *mainLoop = HostBase::instance()->mainLoop();

	if (!LSRegister("org.webosports.bootmgr-displayblocker", &m_service, &lserror)) {
		LSErrorPrint(&lserror, stderr);
		LSErrorFree(&lserror);
		return;
	}

	if (!LSGmainAttach(m_service, mainLoop, &lserror)) {
		LSErrorPrint(&lserror, stderr);
		LSErrorFree(&lserror);
		return;
	}
}

DisplayBlocker::~DisplayBlocker()
{
	if (!m_service)
		return;

	LSError lserror;
	LSErrorInit(&lserror);

	if (!LSUnregister(m_service, &lserror)) {
		LSErrorPrint(&lserror, stderr);
		LSErrorFree(&lserror);
	}

	m_service = 0;
}

void DisplayBlocker::acquire(const char *clientName)
{
	if (!m_service)
		return;

	char *parameters = g_strdup_printf("{\"requestBlock\":true,\"client\":\"%s\"}",
					clientName);

	LSError lserror;
	LSErrorInit(&lserror);

	if (!LSCall(m_service, "palm://com.palm.display/control/setProperty",
		parameters, cbRegistrationResponse, this, &m_token, &lserror))
	{
		m_token = 0;
		LSErrorPrint(&lserror, stderr);
		LSErrorFree(&lserror);
	}

	g_free(parameters);
}

bool DisplayBlocker::cbRegistrationResponse(LSHandle *handle, LSMessage *message, void *user_data)
{
	return true;
}

void DisplayBlocker::release()
{
	LSError lserror;

	if (!m_service || !m_token)
		return;

	LSErrorInit(&lserror);

	if (!LSCallCancel(m_service, m_token, &lserror)) {
		LSErrorPrint(&lserror, stderr);
		LSErrorFree(&lserror);
	}

	m_token = 0;
}

void BootStateStartup::handleEvent(BootEvent event)
{
	if (event == BOOT_EVENT_COMPOSITOR_AVAILABLE || event == BOOT_EVENT_WEBAPPMGR_AVAILABLE)
		tryAdvanceState();
}

void BootStateStartup::enter()
{
	tryAdvanceState();
}

void BootStateStartup::tryAdvanceState()
{
	if (!BootManager::instance()->compositorAvailable())
		return;

	if (!WebAppMgrProxy::instance()->connected())
		return;

	advanceState();
}

void BootStateStartup::advanceState()
{
	if (!QFile::exists("/var/luna/preferences/ran-firstuse") || (QFile::exists("/var/luna/preferences/ran-firstuse") && !QFile::exists("/var/luna/preferences/first-use-profile-created")))
		BootManager::instance()->switchState(BOOT_STATE_FIRSTUSE);
	else
		BootManager::instance()->switchState(BOOT_STATE_NORMAL);
}

void BootStateFirstUse::enter()
{
	launchFirstUseApp();
	m_displayBlocker.acquire("org.webosports.bootmgr");
}

void BootStateFirstUse::leave()
{
	ApplicationProcessManager::instance()->killByAppId("org.webosports.app.firstuse");
	ApplicationProcessManager::instance()->killByAppId("org.webosports.app.phone");

	m_displayBlocker.release();
}

void BootStateFirstUse::handleEvent(BootEvent event)
{
	if (event == BOOT_EVENT_FIRST_USE_DONE)
		createLocalAccount();
	else if (event == BOOT_EVENT_PROFILE_CREATED)
		runConfigurator();
	else if (event == BOOT_EVENT_COMPOSITOR_AVAILABLE) {
		// Whatever stopped the apps before, the compositor going away did it
		// now: they get their relaunches back.
		m_relaunchTimes[0].clear();
		m_relaunchTimes[1].clear();
		launchFirstUseApp();
	}
	else if (event == BOOT_EVENT_FIRSTUSE_APP_STOPPED)
		relaunchStopped(0);
	else if (event == BOOT_EVENT_FIRSTUSE_PHONE_STOPPED)
		relaunchStopped(1);
}

void BootStateFirstUse::launchFirstUseApp()
{
	launchFirstUse();
	launchFirstUsePhone();
}

void BootStateFirstUse::launchFirstUse()
{
	ApplicationProcessManager::instance()->launch("org.webosports.app.firstuse", "");
}

void BootStateFirstUse::launchFirstUsePhone()
{
	ApplicationProcessManager::instance()->launch("org.webosports.app.phone", "{\"mode\":\"first-use\"}");
}

// A compositor restart takes first use's apps down with it, and
// BOOT_EVENT_COMPOSITOR_AVAILABLE launches them again - but only when they are
// gone by then. An old instance still on its way out got that launch from SAM
// as a relaunch, died anyway, and nothing launched it again: the device was
// left on the launcher half-way through first use. which: 0 is the first use
// app, 1 the phone app.
void BootStateFirstUse::relaunchStopped(int which)
{
	// Without a compositor, BOOT_EVENT_COMPOSITOR_AVAILABLE launches them once
	// it is back. Once first use is done, its app closes for good.
	if (!BootManager::instance()->compositorAvailable() ||
	    QFile::exists("/var/luna/preferences/ran-firstuse"))
		return;

	// An app that keeps crashing on its own is not launched again forever. A
	// compositor that keeps restarting resets this, see above.
	const qint64 now = g_get_monotonic_time() / 1000;
	QList<qint64> &times = m_relaunchTimes[which];
	while (!times.isEmpty() && now - times.first() > 60000)
		times.removeFirst();
	if (times.size() >= 5) {
		g_warning("%s stopped 5 times within a minute during first use, not launching it again",
			  which == 0 ? "First use" : "Phone");
		return;
	}
	times.append(now);

	g_message("%s stopped during first use, launching it again", which == 0 ? "First use" : "Phone");
	if (which == 0)
		launchFirstUse();
	else
		launchFirstUsePhone();
}

void BootStateFirstUse::createLocalAccount()
{
	LSError error;
	LSErrorInit(&error);

	QFile localProfileMarker("/var/luna/preferences/first-use-profile-created");
	if (!localProfileMarker.exists()) {
		if (!LSCall(BootManager::instance()->service(), "palm://com.palm.service.accounts/createLocalAccount",
					"{}", cbCreateLocalAccount, NULL, NULL, &error)) {
			g_warning("Failed to create local account after first use is done: %s", error.message);
		}
	}
}

bool BootStateFirstUse::cbCreateLocalAccount(LSHandle *handle, LSMessage *message, void *user_data)
{
	return true;
}

void BootStateFirstUse::runConfigurator()
{
	LSError error;
	LSErrorInit(&error);

	QFile localProfileMarker("/var/luna/preferences/first-use-profile-created");
	if (localProfileMarker.exists()) {
		if (!LSCall(BootManager::instance()->service(), "luna://com.palm.configurator/run",
					"{\"types\":[\"activities\"]}", cbRunConfigurator, NULL, NULL, &error)) {
			g_warning("Failed to run configurator after first use is done and profile is created: %s", error.message);
		}
	}
	else {
		BootManager::instance()->switchState(BOOT_STATE_NORMAL);
	}
}

bool BootStateFirstUse::cbRunConfigurator(LSHandle *handle, LSMessage *message, void *user_data)
{
	// regardless wether the local account creation was successfull or not we switch into
	// normal state
	BootManager::instance()->switchState(BOOT_STATE_NORMAL);
	return true;
}

void BootStateNormal::enter()
{
	activateSuspend(true);
	launchBootTimeApps();
}

void BootStateNormal::activateSuspend(bool enable)
{
	int fd;
	const int len = 64;
	char buf[len];
	struct timespec ts;
	int numChars;

	/* never enable suspend on emulator or desktop */
	if (Settings::LunaSettings()->hardwareType == Settings::HardwareTypeEmulator ||
	    Settings::LunaSettings()->hardwareType == Settings::HardwareTypeDesktop)
	    return;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	if (enable) {
		fd = open("/tmp/suspend_active", O_RDWR | O_CREAT,
			  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
		if (fd < 0) {
			g_critical("Could not activate suspend: Could not create /tmp/suspend_active");
			return;
		}

		clock_gettime(CLOCK_MONOTONIC, &ts);

		numChars = snprintf(buf, len, "%ld\n", ts.tv_sec);
		if (numChars > 0) {
			ssize_t result = write(fd, buf, numChars);
			Q_UNUSED(result);
		}

		close(fd);
	}
	else {
		unlink("/tmp/suspend_active");
	}
}

void BootStateNormal::launchBootTimeApps()
{
	ApplicationProcessManager::instance()->launch("com.palm.launcher", "{\"launchedAtBoot\":true}");
	ApplicationProcessManager::instance()->launch("com.palm.systemui", "{\"launchedAtBoot\":true}");
	ApplicationManager::instance()->launchBootTimeApps();
}

void BootStateNormal::leave()
{
	activateSuspend(false);
}

void BootStateNormal::handleEvent(BootEvent event)
{
	if (event == BOOT_EVENT_COMPOSITOR_NOT_AVAILABLE || event == BOOT_EVENT_WEBAPPMGR_NOT_AVAILABLE) {
		activateSuspend(false);
		BootManager::instance()->switchState(BOOT_STATE_STARTUP);
	}
}

BootManager* BootManager::instance()
{
	static BootManager *instance = 0;

	if (!instance)
		instance = new BootManager;

	return instance;
}

BootManager::BootManager() :
	m_service(0),
	m_currentState(BOOT_STATE_STARTUP),
	m_compositorAvailable(false),
	m_switchingState(false),
	m_pendingState(BOOT_STATE_STARTUP),
	m_hasPendingState(false),
	m_appLifeEventsToken(LSMESSAGE_TOKEN_INVALID)
{
	m_states[BOOT_STATE_STARTUP] = new BootStateStartup();
	m_states[BOOT_STATE_FIRSTUSE] = new BootStateFirstUse();
	m_states[BOOT_STATE_NORMAL] = new BootStateNormal();

	startService();

	connect(WebAppMgrProxy::instance(), SIGNAL(connectionStatusChanged()),
		this, SLOT(onWebAppMgrConnectionStatusChanged()));

	m_fileWatch.addPath("/var/luna/preferences");
	connect(&m_fileWatch, SIGNAL(directoryChanged(QString)), this, SLOT(onFileChanged(QString)));
	connect(&m_fileWatch, SIGNAL(fileChanged(QString)), this, SLOT(onFileChanged(QString)));

	QTimer::singleShot(0, this, SLOT(onInitialize()));
}

BootManager::~BootManager()
{
	stopService();

	delete m_states[BOOT_STATE_STARTUP];
	delete m_states[BOOT_STATE_FIRSTUSE];
	delete m_states[BOOT_STATE_NORMAL];
}

void BootManager::onInitialize()
{
	switchState(BOOT_STATE_STARTUP);
}

bool BootManager::cbSamStatus(LSHandle *handle, const char *serviceName, bool connected, void *user_data)
{
	BootManager *self = BootManager::instance();

	if (!connected) {
		// The subscription went with SAM.
		self->m_appLifeEventsToken = LSMESSAGE_TOKEN_INVALID;
		return true;
	}

	LSError error;
	LSErrorInit(&error);

	if (!LSCall(handle, "luna://com.webos.service.applicationmanager/getAppLifeEvents",
		    "{\"subscribe\":true}", cbAppLifeEvents, NULL, &self->m_appLifeEventsToken, &error)) {
		g_warning("Failed to subscribe to SAM's app life events: %s", error.message);
		LSErrorFree(&error);
		self->m_appLifeEventsToken = LSMESSAGE_TOKEN_INVALID;
	}

	return true;
}

bool BootManager::cbAppLifeEvents(LSHandle *handle, LSMessage *message, void *user_data)
{
	const char *payload = LSMessageGetPayload(message);
	json_object *json = payload ? json_tokener_parse(payload) : NULL;
	json_object *value = NULL;
	std::string event, appId;

	if (json && json_object_object_get_ex(json, "event", &value) && json_object_get_string(value))
		event = json_object_get_string(value);
	if (json && json_object_object_get_ex(json, "appId", &value) && json_object_get_string(value))
		appId = json_object_get_string(value);
	if (json)
		json_object_put(json);

	if (event != "stop")
		return true;

	if (appId == "org.webosports.app.firstuse")
		BootManager::instance()->handleEvent(BOOT_EVENT_FIRSTUSE_APP_STOPPED);
	else if (appId == "org.webosports.app.phone")
		BootManager::instance()->handleEvent(BOOT_EVENT_FIRSTUSE_PHONE_STOPPED);

	return true;
}

static bool cbCompositorAvailable(LSHandle *handle, const char *serviceName, bool connected, void *user_data)
{
	g_message("compositor status: %s", connected ? "connected" : "disconnected");

	BootManager::instance()->handleEvent(connected ? BOOT_EVENT_COMPOSITOR_AVAILABLE :
					BOOT_EVENT_COMPOSITOR_NOT_AVAILABLE);
	return true;
}

void BootManager::startService()
{
	bool result;
	LSError error;
	LSErrorInit(&error);

	GMainLoop *mainLoop = HostBase::instance()->mainLoop();

	g_message("BootManager starting...");

	if (!LSRegister("org.webosports.bootmgr", &m_service, &error)) {
		g_warning("Failed in BootManager: %s", error.message);
		LSErrorFree(&error);
		return;
	}

	if (!LSRegisterCategory(m_service, "/", s_methods, NULL, NULL, &error)) {
		g_warning("Failed in BootManager: %s", error.message);
		LSErrorFree(&error);
		return;
	}

	if (!LSGmainAttach(m_service, mainLoop, &error)) {
		g_warning("Failed in BootManager: %s", error.message);
		LSErrorFree(&error);
		return;
	}

	if (!LSRegisterServerStatusEx(m_service, "com.webos.surfacemanager-cardshell",
				cbCompositorAvailable, NULL, NULL, &error)) {
		g_warning("Failed to register for compositor status");
		LSErrorFree(&error);
	}

	if (!LSRegisterServerStatusEx(m_service, "com.webos.service.applicationmanager",
				cbSamStatus, NULL, NULL, &error)) {
		g_warning("Failed to register for SAM status");
		LSErrorFree(&error);
	}
}

void BootManager::stopService()
{
	LSError error;
	LSErrorInit(&error);
	bool result;

	result = LSUnregister(m_service, &error);
	if (!result)
		LSErrorFree(&error);

	m_service = 0;
}

void BootManager::switchState(BootState state)
{
	if (m_switchingState) {
		m_pendingState = state;
		m_hasPendingState = true;
		return;
	}

	m_switchingState = true;
	m_pendingState = state;
	m_hasPendingState = true;

	while (m_hasPendingState) {
		BootState nextState = m_pendingState;
		m_hasPendingState = false;

		qDebug() << __PRETTY_FUNCTION__ << "Switching to state" << QString::fromStdString(bootStateToStr(nextState));

		m_states[m_currentState]->leave();
		m_currentState = nextState;
		m_states[m_currentState]->enter();

		postCurrentState();
	}

	m_switchingState = false;
}

void BootManager::handleEvent(BootEvent event)
{
    if (event == BOOT_EVENT_COMPOSITOR_AVAILABLE)
        setCompositorAvailabe(true);
    else if (event == BOOT_EVENT_COMPOSITOR_NOT_AVAILABLE)
        setCompositorAvailabe(false);

    m_states[m_currentState]->handleEvent(event);
}

void BootManager::onFileChanged(const QString& path)
{
	if (QFile::exists("/var/luna/preferences/ran-firstuse") && !QFile::exists("/var/luna/preferences/first-use-profile-created"))
		handleEvent(BOOT_EVENT_FIRST_USE_DONE);
	else if (QFile::exists("/var/luna/preferences/ran-firstuse") && QFile::exists("/var/luna/preferences/first-use-profile-created"))
		handleEvent(BOOT_EVENT_PROFILE_CREATED);
}

void BootManager::onWebAppMgrConnectionStatusChanged()
{
	if (WebAppMgrProxy::instance()->connected())
		handleEvent(BOOT_EVENT_WEBAPPMGR_AVAILABLE);
	else
		handleEvent(BOOT_EVENT_WEBAPPMGR_NOT_AVAILABLE);
}

BootState BootManager::currentState() const
{
	return m_currentState;
}

bool BootManager::isBootFinished() const
{
    return m_currentState == BOOT_STATE_NORMAL ||
           m_currentState == BOOT_STATE_FIRSTUSE;
}

LSHandle* BootManager::service() const
{
	return m_service;
}

void BootManager::setCompositorAvailabe(bool value)
{
    if (value == m_compositorAvailable)
        return;

    m_compositorAvailable = value;
    Q_EMIT compositorAvailableChanged();
}

bool BootManager::compositorAvailable() const
{
	return m_compositorAvailable;
}

void BootManager::postCurrentState()
{
	LSError error;
	json_object* json = 0;

    if (m_currentState == BOOT_STATE_FIRSTUSE ||
        m_currentState == BOOT_STATE_NORMAL)
        Q_EMIT bootFinished();

	LSErrorInit(&error);

	json = json_object_new_object();

	std::string stateStr = bootStateToStr(m_currentState);
	json_object_object_add(json, (char*) "state", json_object_new_string(stateStr.c_str()));

	if (!LSSubscriptionPost(m_service, "/", "getStatus", json_object_to_json_string(json), &error))
		LSErrorFree (&error);

	json_object_put(json);
}

bool cbGetStatus(LSHandle *handle, LSMessage *message, void *user_data)
{
	SUBSCRIBE_SCHEMA_RETURN(handle, message);

	bool success = true;
	LSError error;
	json_object* json = json_object_new_object();
	bool subscribed = false;
	bool firstUse = false;
	std::string stateStr;

	LSErrorInit(&error);

	if (LSMessageIsSubscription(message)) {
		if (!LSSubscriptionProcess(handle, message, &subscribed, &error)) {
			LSErrorFree (&error);
			goto done;
		}
	}

	stateStr = bootStateToStr(BootManager::instance()->currentState());
	json_object_object_add(json, "state", json_object_new_string(stateStr.c_str()));

done:
	json_object_object_add(json, "returnValue", json_object_new_boolean(success));
	json_object_object_add(json, "subscribed", json_object_new_boolean(subscribed));

	if (!LSMessageReply(handle, message, json_object_to_json_string(json), &error))
		LSErrorFree (&error);

	json_object_put(json);

	return true;
}
