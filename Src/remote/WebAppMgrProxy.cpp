/* @@@LICENSE
*
*      Copyright (c) 2010-2013 LG Electronics, Inc.
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

#include "Common.h"

#include <string.h>
#include <cjson/json.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/prctl.h>

#include "WebAppMgrProxy.h"
#include "Settings.h"
#include "ApplicationManager.h"
#include "Event.h"
#include "Logging.h"
#include "MemoryMonitor.h"
#include "Time.h"
#include "HostBase.h"
#include "NativeAppManager.h"

static WebAppMgrProxy* s_instance = NULL;
static gchar* s_appToLaunchWhenConnectedStr = NULL;

void WebAppMgrProxy::setAppToLaunchUponConnection(char* app)
{
	s_appToLaunchWhenConnectedStr = app;
}

WebAppMgrProxy::WebAppMgrProxy() :
    mConnected(false),
    mService(0)
{
    connectWebAppMgr();
}

gboolean WebAppMgrProxy::retryConnectWebAppMgr(gpointer user_data)
{
    WebAppMgrProxy::instance()->connectWebAppMgr();
    return FALSE;
}

void WebAppMgrProxy::connectWebAppMgr()
{
    if (connected()) {
        g_critical("%s (%d): ERROR: WebAppMgr instance already Connected!!!",
                   __PRETTY_FUNCTION__, __LINE__);
        return;
    }

    // Initialize the LunaService connection for sending app-run information
    LSError err;
    LSErrorInit(&err);
    if(!LSRegister(NULL, &mService, &err)) {
        g_warning("Could not register service client: %s", err.message);
        g_warning("Will retry after some time ...");
        g_timeout_add_full(G_PRIORITY_DEFAULT, 2000, &WebAppMgrProxy::retryConnectWebAppMgr, NULL, NULL);
        LSErrorFree(&err);
        mService=0;
        return;
    }

    LSGmainAttach(mService, HostBase::instance()->mainLoop(), &err);

    g_message("Waiting for WebAppMgr to become available ...");

    if (!LSCall(mService, "palm://com.palm.bus/signal/registerServerStatus",
                "{\"serviceName\":\"org.webosports.webappmanager\"}",
                webAppManagerServiceStatusCb, this, NULL, &err)) {
        g_warning("Failed to listen for WebAppMgr service status: %s", err.message);
        LSErrorFree(&err);
        return;
    }

}

bool WebAppMgrProxy::webAppManagerServiceStatusCb(LSHandle *handle, LSMessage *message, void *user_data)
{
    const char* payload = LSMessageGetPayload(message);
    if (!message)
        return true;

    json_object* json = json_tokener_parse(payload);
    if (!json || is_error(json))
            return true;

    json_object* value = json_object_object_get(json, "connected");
    if (!value) {
        json_object_put(json);
        return true;
    }

    WebAppMgrProxy *proxy = static_cast<WebAppMgrProxy*>(user_data);

    bool connected = json_object_get_boolean(value);
    if (connected)
        proxy->onWebAppManagerConnected();
    else
        proxy->onWebAppManagerDisconnected();

    json_object_put(json);

    return true;
}

void WebAppMgrProxy::onWebAppManagerConnected()
{
    g_message("%s", __PRETTY_FUNCTION__);

    mConnected = true;
}

void WebAppMgrProxy::onWebAppManagerDisconnected()
{
    g_message("%s", __PRETTY_FUNCTION__);

    mConnected = false;
}

bool WebAppMgrProxy::connected()
{
    return mConnected;
}

WebAppMgrProxy* WebAppMgrProxy::instance()
{
	if (G_UNLIKELY(!s_instance)) {
		s_instance = new WebAppMgrProxy();
	}

	return s_instance;
}

WebAppMgrProxy::~WebAppMgrProxy()
{
	s_instance = NULL;
}

void WebAppMgrProxy::launchUrl(const char* url, WindowType::Type winType,
                               ApplicationDescription *appDesc, const char* procId,
                               const char* params, const char* launchingAppId,
                               const char* launchingProcId)
{
	LSError err;
	LSErrorInit(&err);

	std::string windowType = "card";
	switch (winType) {
	case WindowType::Type_Launcher:
		windowType = "launcher";
		break;
	case WindowType::Type_Dashboard:
		windowType = "dashboard";
		break;
	case WindowType::Type_PopupAlert:
		windowType = "popupAlert";
		break;
	case WindowType::Type_BannerAlert:
		windowType = "bannerAlert";
		break;
	case WindowType::Type_StatusBar:
		windowType = "statusBar";
		break;
	}

	json_object *obj = json_object_new_object();
	json_object_object_add(obj, "url", json_object_new_string(url));
	json_object_object_add(obj, "windowType", json_object_new_string(windowType.c_str()));
	if (appDesc)
		json_object_object_add(obj, "appDesc", appDesc->toJSON());
	json_object_object_add(obj, "params", json_object_new_string(params));
	json_object_object_add(obj, "launchingAppId", json_object_new_string(launchingAppId));
	json_object_object_add(obj, "launchingProcId", json_object_new_string(launchingProcId));

	if (!LSCall(mService, "palm://org.webosports.webappmanager/launchUrl",
			json_object_to_json_string(obj),
			NULL, NULL, NULL, &err)) {
		LSErrorPrint(&err, stderr);
		LSErrorFree(&err);
	}

	json_object_put(obj);
}

std::string WebAppMgrProxy::appLaunch(const std::string& appId,
                                      const std::string& params,
                                      const std::string& launchingAppId,
                                      const std::string& launchingProcId,
                                      std::string& errMsg)
{
	std::string appIdToLaunch = appId;
	std::string paramsToLaunch = params;
	errMsg.erase();

	ApplicationDescription* desc = ApplicationManager::instance()->getPendingAppById(appIdToLaunch);
	if (!desc)
		desc = ApplicationManager::instance()->getAppById(appIdToLaunch);

	if( !desc )
	{
		errMsg = std::string("\"") + appIdToLaunch + "\" was not found";
		g_debug( "WebAppMgrProxy::appLaunch failed, %s.\n", errMsg.c_str() );
		return "";
	}

	if( appIdToLaunch.empty() ) {
		errMsg = "No appId";
		return "";
	}

	//if execution lock is in place, refuse the launch
	if (desc->canExecute() == false) {
		errMsg = std::string("\"") + appIdToLaunch + "\" has been locked";
		luna_warn("WebAppMgrProxy","appLaunch failed, '%s' has been locked (probably in process of being deleted from the system)",appId.c_str());
		return "";
	}

	// redirect all launch requests for pending applications to app catalog, UNLESS it's a special SUC app
	//TODO: the sucApps thing is done : App Catalog disabled when SUC update is downloading; it's not safe, but it's needed

	if ( (desc->status() != ApplicationDescription::Status_Ready)
		&& (Settings::LunaSettings()->sucApps.find(desc->id()) == Settings::LunaSettings()->sucApps.end())
		)
	{
		desc = ApplicationManager::instance()->getAppById("com.palm.app.swmanager");
		if (desc) {
			appIdToLaunch = desc->id();
			paramsToLaunch = "{}";
		}
		else {
			g_warning("%s: Failed to find app descriptor for com.palm.app.swmanager", __PRETTY_FUNCTION__);
			return "";
		}
	}

	if (G_UNLIKELY(Settings::LunaSettings()->perfTesting)) {
	    g_message("SYSMGR PERF: APP LAUNCHED app id: %s, start time: %d", appId.c_str(), Time::curTimeMs());
	}

	if (desc->type() == ApplicationDescription::Type_Web) {
		// Verify that the app doesn't have a security issue
		if (!desc->securityChecksVerified())
			return "";

		LSError err;
		LSErrorInit(&err);

		json_object *obj = json_object_new_object();
		json_object_object_add(obj, "appDesc", desc->toJSON());
		json_object_object_add(obj, "params", json_object_new_string(paramsToLaunch.c_str()));
		json_object_object_add(obj, "launchingAppId", json_object_new_string(launchingAppId.c_str()));
		json_object_object_add(obj, "launchingProcId", json_object_new_string(launchingProcId.c_str()));

		if (!LSCall(mService, "palm://org.webosports.webappmanager/launchApp",
				json_object_to_json_string(obj),
				NULL, NULL, NULL, &err)) {
			LSErrorPrint(&err, stderr);
			LSErrorFree(&err);
		}

		json_object_put(obj);

		// FIXME: $$$ Can't get the resulting process ID at this point (asynchronous call)
		return "success";
    }
    else if (desc->type() == ApplicationDescription::Type_Native ||
             desc->type() == ApplicationDescription::Type_PDK ||
             desc->type() == ApplicationDescription::Type_Qt) {
        // Verify that the app doesn't have a security issue
        if (!desc->securityChecksVerified())
            return "";
		// Launch Native apps here
		return launchNativeApp(desc, paramsToLaunch, launchingAppId, launchingProcId, errMsg);
    }
    else if (desc->type() == ApplicationDescription::Type_SysmgrBuiltin)
	{
		if (launchingAppId == "com.palm.launcher")
		{
			desc->startSysmgrBuiltIn(paramsToLaunch);
			return "success";
		}
	}

	g_warning("%s: Attempted to launch application with unknown type", __PRETTY_FUNCTION__);
	return "";
}

std::string WebAppMgrProxy::appLaunchModal(const std::string& appId,
                                      	   const std::string& params,
                                      	   const std::string& launchingAppId,
                                      	   const std::string& launchingProcId,
                                      	   std::string& errMsg, bool isHeadless, bool isParentPdk)
{
	std::string appIdToLaunch = appId;
	std::string paramsToLaunch = params;
	errMsg.erase();

	ApplicationDescription* desc = ApplicationManager::instance()->getPendingAppById(appIdToLaunch);
	if (!desc)
		desc = ApplicationManager::instance()->getAppById(appIdToLaunch);

	if( !desc )
	{
		errMsg = std::string("\"") + appIdToLaunch + "\" was not found";
		g_debug( "WebAppMgrProxy::appLaunch failed, %s.\n", errMsg.c_str() );
		return "";
	}

	if( appIdToLaunch.empty() ) {
		errMsg = "No appId";
		return "";
	}

	//if execution lock is in place, refuse the launch
	if (desc->canExecute() == false) {
		errMsg = std::string("\"") + appIdToLaunch + "\" has been locked";
		luna_warn("WebAppMgrProxy","appLaunch failed, '%s' has been locked (probably in process of being deleted from the system)",appId.c_str());
		return "";
	}

	// redirect all launch requests for pending applications to app catalog
	if (desc->status() != ApplicationDescription::Status_Ready) {
		desc = ApplicationManager::instance()->getAppById("com.palm.app.swmanager");
		if (desc) {
			appIdToLaunch = desc->id();
			paramsToLaunch = "{}";
		}
		else {
			g_warning("%s: Failed to find app descriptor for app catalog", __PRETTY_FUNCTION__);
			return "";
		}
	}

	if (desc->type() == ApplicationDescription::Type_Web) {
		std::string appDescJson;
		desc->getAppDescriptionString(appDescJson);
#if 0
		sendAsyncMessage(new View_ProcMgr_LaunchChild(appDescJson, paramsToLaunch, launchingAppId, launchingProcId, isHeadless, isParentPdk));
#endif
        return "success";
	}

	g_warning("%s: Attempted to launch application with unknown type", __PRETTY_FUNCTION__);
	return "";
}

std::string WebAppMgrProxy::launchNativeApp(const ApplicationDescription* desc,
                                            const std::string& params,
                                            const std::string& launchingAppId,
                                            const std::string& launchingProcId,
                                            std::string& errMsg )
{
	std::string ret;
	// construct the path for launching
	std::string path = desc->entryPoint();
	if (path.find("file://", 0) != std::string::npos)
		path = path.substr(7);

	// assemble the args list. We'll need exactly 3 entries.
	// 1) the path to the exe
	// 2) The sent in "params" value
	// 3) a NULL to terminate the list

	// this has to be a char *, not const char *, because of the 
	// rather unusual use of "char *const argV[]" on the recieving end
	// of the function we're going to call. So we declare it appropriate
	// for the call, and cast as we assign.
    char *argV[3];
    argV[0] = (char *)path.c_str();
	
	if ( params.size() > 0 )
	{
		// send the params
		argV[1] = (char *)params.c_str();
		argV[2] = NULL;
	}
	else
	{
		// no params. Just end the list
		argV[1] = NULL;
	}
	
	int pid = NativeAppManager::instance()->launchNativeProcess(desc->id(), path.c_str(), argV, desc->type(), desc->runtimeMemoryRequired());
	if (pid <= 0) {
		if (pid < 0) // 0 indicates low memory, -1 indicates launch error
		{
			g_critical("%s: %d Failed to launch native app %s with path: %s",
			           __PRETTY_FUNCTION__, __LINE__,
			           desc->id().c_str(), path.c_str());
			errMsg = "Failed to launch process";
		}
		return std::string();
	}

	char* retStr = 0;
	asprintf(&retStr, "n-%d", pid);

	ret = retStr;
	free(retStr);

	return ret;
}


std::string WebAppMgrProxy::launchBootTimeApp(const char* appId)
{
	if (!appId)
		return "";

	ApplicationDescription* desc = ApplicationManager::instance()->getAppById(appId);
	if (!desc) {
		luna_warn("WebAppMgrProxy", "launch failed, '%s' was not found.", appId);
		return "";
	}

	//if execution lock is in place, refuse the launch
	if (desc->canExecute() == false) {
		luna_warn("WebAppMgrProxy","launch failed, '%s' has been locked (probably in process of being deleted from the system)",appId);
		return "";
	}

	LSError err;
	LSErrorInit(&err);

	json_object *obj = json_object_new_object();
	json_object_object_add(obj, "appDesc", desc->toJSON());
	json_object *params = json_object_new_object();
	json_object_object_add(params, "launchedAtBoot", json_object_new_boolean(true));
	json_object_object_add(obj, "params", params);

	if (!LSCall(mService, "palm://org.webosports.webappmanager/launchApp",
			json_object_to_json_string(obj),
			NULL, NULL, NULL, &err)) {
		LSErrorPrint(&err, stderr);
		LSErrorFree(&err);
	}

	json_object_put(obj);

	// FIXME: $$$ Can't get the resulting process ID at this point (asynchronous call)
	return "success";
}

void WebAppMgrProxy::serviceRequestHandler_listRunningApps(bool includeSysApps)
{
#if 0
	sendAsyncMessage(new View_Mgr_ListOfRunningAppsRequest(includeSysApps));
#endif
}
