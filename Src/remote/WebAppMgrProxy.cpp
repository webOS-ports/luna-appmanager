/* @@@LICENSE
 *
 * Copyright (c) 2010-2013 LG Electronics, Inc.
 *               2014 Simon Busch <morphis@gravedo.de>
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
#include <json.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/prctl.h>

#include <QDebug>

#include "WebAppMgrProxy.h"
#include "Settings.h"
#include "ApplicationManager.h"
#include "Event.h"
#include "Logging.h"
#include "MemoryMonitor.h"
#include "Time.h"
#include "HostBase.h"
#include "ApplicationProcessManager.h"

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
    if (!json)
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

    Q_EMIT connectionStatusChanged();

    LSError err;
    LSErrorInit(&err);

    // Initial sync of all running web applications
    if (!LSCallOneReply(mService, "luna://org.webosports.webappmanager/listRunningApps","{}",
                        listRunningAppsCb, this, NULL, &err)) {
        g_warning("Failed to list all running apps: %s", err.message);
        LSErrorFree(&err);
    }

    if (!LSCall(mService, "luna://org.webosports.webappmanager/registerForAppEvents","{\"subscribe\":true}",
                        appEventCb, this, NULL, &err)) {
        g_warning("Failed to list all running apps: %s", err.message);
        LSErrorFree(&err);
    }
}

void WebAppMgrProxy::onWebAppManagerDisconnected()
{
    g_message("%s", __PRETTY_FUNCTION__);

    mConnected = false;

    ApplicationProcessManager::instance()->removeAllWebApplications();

    Q_EMIT connectionStatusChanged();
}

bool WebAppMgrProxy::listRunningAppsCb(LSHandle *handle, LSMessage *message, void *user_data)
{
    WebAppMgrProxy *proxy = static_cast<WebAppMgrProxy*>(user_data);
    proxy->updateRunningApps(LSMessageGetPayload(message));
    return true;
}

void WebAppMgrProxy::updateRunningApps(const char *payload)
{
    qDebug() << __PRETTY_FUNCTION__ << payload;

    json_object *json = json_tokener_parse(payload);
    if (!json)
        return;

    json_object *appsValue = json_object_object_get(json, "apps");
    if (appsValue && json_object_is_type(appsValue, json_type_array)) {
        for (int i = 0; i < json_object_array_length(appsValue); i++) {
            struct json_object *appEntry = json_object_array_get_idx(appsValue, i);

            std::string appId = json_object_get_string(json_object_object_get(appEntry, "appId"));
            // FIXME need to switch json parser in order to support int64 directly
            qint64 processId = (qint64) json_object_get_int(json_object_object_get(appEntry, "processId"));

            WebApplication *webApp = new WebApplication(QString::fromStdString(appId), processId);
            ApplicationProcessManager::instance()->notifyApplicationHasStarted(webApp);
        }
    }

cleanup:
    json_object_put(json);
}

bool WebAppMgrProxy::appEventCb(LSHandle *handle, LSMessage *message, void *user_data)
{
    WebAppMgrProxy *proxy = static_cast<WebAppMgrProxy*>(user_data);
    proxy->handleAppEvent(LSMessageGetPayload(message));
    return true;
}

void WebAppMgrProxy::handleAppEvent(const char *payload)
{
    json_object *appIdObj, *eventObj, *processIdObj;
    std::string event, appId;
    qint64 processId;

    qDebug() << __PRETTY_FUNCTION__ << payload;

    json_object *json = json_tokener_parse(payload);
    if (!json)
        return;

    eventObj = json_object_object_get(json, "event");
    if (!eventObj || !json_object_is_type(eventObj, json_type_string))
        goto cleanup;

    appIdObj = json_object_object_get(json, "appId");
    if (!appIdObj || !json_object_is_type(appIdObj, json_type_string))
        goto cleanup;

    processIdObj = json_object_object_get(json, "processId");
    if (!processIdObj || !json_object_is_type(processIdObj, json_type_int))
        goto cleanup;

    event = json_object_get_string(eventObj);
    appId = json_object_get_string(appIdObj);
    processId = json_object_get_int(processIdObj);

    if (event == "start") {
        WebApplication *app = new WebApplication(QString::fromStdString(appId), processId);
        ApplicationProcessManager::instance()->notifyApplicationHasStarted(app);
    }
    else if (event == "close")
        ApplicationProcessManager::instance()->notifyApplicationHasFinished(processId);

cleanup:
    json_object_put(json);
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

void WebAppMgrProxy::killApp(qint64 processId)
{
    LSError err;
    LSErrorInit(&err);

    if (!connected()) {
        g_warning("WebAppManager is not connected so can't kill app with process id %d",
                  processId);
        return;
    }

    char *payload = g_strdup_printf("{\"processId\":%llu}", processId);

    if (!LSCallOneReply(mService, "luna://org.webosports.webappmanager/killApp", payload,
                        NULL, NULL, NULL, &err)) {
        g_warning("Failed to kill app with process id %i: %s", processId, err.message);
        LSErrorFree(&err);
    }

    g_free(payload);
}

void WebAppMgrProxy::launchUrl(const char* url, WindowType::Type winType,
                               ApplicationDescription *appDesc, qint64 processId,
                               const char* params, const char* launchingAppId,
                               const char* launchingProcId)
{
    LSError err;
    LSErrorInit(&err);

    if (!connected()) {
        g_warning("WebAppManager is not connected so can't launch url %s for app %s",
                  url, appDesc->id().c_str());
        return;
    }

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
    json_object_object_add(obj, "processId", json_object_new_int(processId));
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

std::string WebAppMgrProxy::launchApp(const std::string& appId,
                                      const std::string& params,
                                      qint64 processId,
                                      const std::string& launchingAppId,
                                      const std::string& launchingProcId,
                                      std::string& errMsg)
{
    std::string appIdToLaunch = appId;
    std::string paramsToLaunch = params;
    errMsg.erase();

    if (!connected()) {
        g_warning("WebAppManager is not connected so can't launch app %s",
                  appId.c_str());
        return "";
    }

    ApplicationDescription* desc = ApplicationManager::instance()->getPendingAppById(appIdToLaunch);
    if (!desc)
        desc = ApplicationManager::instance()->getAppById(appIdToLaunch);

    if (!desc) {
        errMsg = std::string("\"") + appIdToLaunch + "\" was not found";
        g_debug( "WebAppMgrProxy::appLaunch failed, %s.\n", errMsg.c_str() );
        return "";
    }

    if (appIdToLaunch.empty()) {
        errMsg = "No appId";
        return "";
    }

    // if execution lock is in place, refuse the launch
    if (desc->canExecute() == false) {
        errMsg = std::string("\"") + appIdToLaunch + "\" has been locked";
        luna_warn("WebAppMgrProxy","appLaunch failed, '%s' has been locked (probably in process of being deleted from the system)",appId.c_str());
        return "";
    }

    if ((desc->status() != ApplicationDescription::Status_Ready)
         && (Settings::LunaSettings()->sucApps.find(desc->id()) == Settings::LunaSettings()->sucApps.end()))
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

    if (G_UNLIKELY(Settings::LunaSettings()->perfTesting))
        g_message("SYSMGR PERF: APP LAUNCHED app id: %s, start time: %d", appId.c_str(), Time::curTimeMs());

    if (desc->type() == ApplicationDescription::Type_Web) {
        // Verify that the app doesn't have a security issue
        if (!desc->securityChecksVerified())
            return "";

        LSError err;
        LSErrorInit(&err);

        json_object *obj = json_object_new_object();
        json_object_object_add(obj, "appDesc", desc->toJSON());
        json_object_object_add(obj, "params", json_object_new_string(paramsToLaunch.c_str()));
        json_object_object_add(obj, "processId", json_object_new_int(processId));
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
    else if (desc->type() == ApplicationDescription::Type_SysmgrBuiltin) {
        if (launchingAppId == "com.palm.launcher") {
            desc->startSysmgrBuiltIn(paramsToLaunch);
            return "success";
        }
    }

    g_warning("%s: Attempted to launch application with unknown type", __PRETTY_FUNCTION__);
    return "";
}

void WebAppMgrProxy::relaunch(const std::string &appId, const std::string &params)
{
    LSError lserror;
    LSErrorInit(&lserror);

    if (!connected()) {
        g_warning("WebAppManager is not connected so can't relaunch app %s",
                  appId.c_str());
        return;
    }

    json_object *obj = json_object_new_object();
    json_object_object_add(obj, "appId", json_object_new_string(appId.c_str()));
    json_object_object_add(obj, "params", json_object_new_string(params.c_str()));

    if (!LSCallOneReply(mService, "luna://org.webosports.webappmanager/relaunch",
                        json_object_to_json_string(obj), NULL, NULL, NULL, &lserror)) {
        g_warning("Failed to send relaunch signa to WebAppMgr: %s", lserror.message);
        LSErrorFree(&lserror);
    }

    json_object_put(obj);
}

void WebAppMgrProxy::clearMemoryCaches()
{
    LSError lserror;
    LSErrorInit(&lserror);

    if (!connected()) {
        g_warning("WebAppManager is not connected so can't clear its memory caches");
        return;
    }

    if (!LSCallOneReply(mService, "luna://org.webosports.webappmanager/clearMemoryCaches",
                        "{}", NULL, NULL, NULL, &lserror)) {
        g_warning("Failed to clear memory caches: %s", lserror.message);
        LSErrorFree(&lserror);
    }
}

void WebAppMgrProxy::clearMemoryCaches(qint64 processId)
{
    LSError lserror;
    LSErrorInit(&lserror);

    if (!connected()) {
        g_warning("WebAppManager is not connected so can't clear its memory caches for process id %d",
                  processId);
        return;
    }

    json_object *obj = json_object_new_object();
    json_object_object_add(obj, "processId", json_object_new_int(processId));

    if (!LSCallOneReply(mService, "luna://org.webosports.webappmanager/clearMemoryCaches",
                        json_object_to_json_string(obj), NULL, NULL, NULL, &lserror)) {
        g_warning("Failed to clear memory caches for process %d: %s", processId, lserror.message);
        LSErrorFree(&lserror);
    }

    json_object_put(obj);
}

void WebAppMgrProxy::clearMemoryCaches(const std::string &appId)
{
    LSError lserror;
    LSErrorInit(&lserror);

    if (!connected()) {
        g_warning("WebAppManager is not connected so can't clear its memory caches for app %s",
                  appId.c_str());
        return;
    }

    json_object *obj = json_object_new_object();
    json_object_object_add(obj, "appId", json_object_new_string(appId.c_str()));

    if (!LSCallOneReply(mService, "luna://org.webosports.webappmanager/clearMemoryCaches",
                        json_object_to_json_string(obj), NULL, NULL, NULL, &lserror)) {
        g_warning("Failed to clear memory caches for app %s: %s", appId.c_str(), lserror.message);
        LSErrorFree(&lserror);
    }

    json_object_put(obj);

}
