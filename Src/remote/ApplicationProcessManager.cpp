/* @@@LICENSE
*
* (c) 2013 Simon Busch <morphis@gravedo.de>
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

#include <QProcess>
#include <QDebug>
#include <QTimer>

#include <rolegen.h>
#include <json.h>

#include "ApplicationProcessManager.h"
#include "ApplicationDescription.h"
#include "ApplicationManager.h"
#include "LaunchPoint.h"

#include "WebAppMgrProxy.h"

WebApplication::WebApplication(const QString &appId, qint64 processId, QObject *parent) :
    ApplicationInfo(appId, processId, APPLICATION_TYPE_WEB)
{
}

void WebApplication::kill()
{
    WebAppMgrProxy::instance()->killApp(processId());
}

ApplicationProcessManager* ApplicationProcessManager::instance()
{
    static ApplicationProcessManager *instance = 0;
    if (!instance)
        instance = new ApplicationProcessManager();

    return instance;
}

ApplicationProcessManager::ApplicationProcessManager() :
    QObject(0),
    mNextProcessId(1000)
{
}

bool ApplicationProcessManager::isRunning(const std::string& appId)
{
    Q_FOREACH(const ApplicationInfo *app, mApplications) {
        if (app->appId() == QString::fromStdString(appId))
            return true;
    }

    return false;
}

std::string ApplicationProcessManager::getPid(const std::string& appId)
{
    const ApplicationInfo *selectedApp = 0;

    Q_FOREACH(const ApplicationInfo *app, mApplications) {
        if (app->appId() == QString::fromStdString(appId)) {
            selectedApp = app;
            break;
        }
    }

    if (selectedApp == 0)
        return std::string("");

    QString processId = QString::number(selectedApp->processId());
    return processId.toStdString();
}

const QList<ApplicationInfo*>& ApplicationProcessManager::runningApplications() const
{
    return mApplications;
}

void ApplicationProcessManager::killByAppId(const std::string& appId, bool notifyUser)
{
    ApplicationInfo *targetApp = 0;

    Q_FOREACH(ApplicationInfo *app, mApplications) {
        if (app->appId() == QString::fromStdString(appId)) {
            targetApp = app;
            break;
        }
    }

    killApp(targetApp);
}

void ApplicationProcessManager::killByProcessId(qint64 processId, bool notifyUser)
{
    ApplicationInfo *targetApp = 0;

    Q_FOREACH(ApplicationInfo *app, mApplications) {
        if (app->processId() == processId) {
            targetApp = app;
            break;
        }
    }

    killApp(targetApp);
}

void ApplicationProcessManager::killApp(ApplicationInfo *app)
{
    if (!app)
        return;

    app->kill();

    const ApplicationDescription *desc = ApplicationManager::instance()->getAppById(app->appId().toStdString());

    std::string appName = "Application";
    std::string appTitle = "";

    if (desc) {
        appName = desc->menuName();
        const LaunchPoint *lp = desc->getDefaultLaunchPoint();
        if (lp)
            appTitle = lp->title();
    }

    ApplicationManager::instance()->postApplicationHasBeenTerminated(appTitle, appName, app->appId().toStdString());

    mApplications.removeAll(app);
    app->deleteLater();
}

namespace {

// How long SAM gets to answer a launch before the launch counts as failed
const int kSamLaunchTimeoutMs = 30000;

struct PendingLaunch
{
    std::string appId;
    LaunchCallback done;
};

void finishLaunch(PendingLaunch *pending, const LaunchResult& result)
{
    if (!result.success)
        g_warning("Launching %s failed: %s", pending->appId.c_str(), result.errorText.c_str());
    if (pending->done)
        pending->done(result);
    delete pending;
}

} // namespace

/*
 * Applications are started by SAM. Its answer (an instanceId, or an
 * errorText) comes back later, so the outcome is passed to done rather
 * than returned. done is called exactly once, possibly before launch()
 * returns.
 */
void ApplicationProcessManager::launch(const std::string& appId, const std::string& params,
                                       const LaunchCallback& done)
{
    qDebug() << "Launching application" << QString::fromStdString(appId);

    PendingLaunch *pending = new PendingLaunch{appId, done};

    // Built with json-c, so an appId or target with quotes in it cannot break
    // the payload. params is a JSON object for nearly every caller; anything
    // else goes through as the string it is.
    json_object *payload = json_object_new_object();
    json_object_object_add(payload, "id", json_object_new_string(appId.c_str()));
    json_object *paramsObj = params.empty() ? json_object_new_object() : json_tokener_parse(params.c_str());
    if (!paramsObj || !json_object_is_type(paramsObj, json_type_object)) {
        if (paramsObj)
            json_object_put(paramsObj);
        paramsObj = json_object_new_string(params.c_str());
    }
    json_object_object_add(payload, "params", paramsObj);

    LSError lserror;
    LSErrorInit(&lserror);
    LSMessageToken token = LSMESSAGE_TOKEN_INVALID;
    LSHandle *handle = ApplicationManager::instance()->getServiceHandle();
    bool sent = LSCallOneReply(handle, "luna://com.webos.service.applicationManager/launch",
                               json_object_to_json_string(payload), launchReplyCb, pending, &token, &lserror);
    json_object_put(payload);

    if (!sent) {
        LaunchResult result;
        result.errorText = std::string("Could not ask SAM to launch ") + appId + ": " +
                (lserror.message ? lserror.message : "unknown error");
        LSErrorFree(&lserror);
        finishLaunch(pending, result);
        return;
    }

    if (!LSCallSetTimeout(handle, token, kSamLaunchTimeoutMs, &lserror)) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }
}

bool ApplicationProcessManager::launchReplyCb(LSHandle *handle, LSMessage *message, void *user_data)
{
    PendingLaunch *pending = static_cast<PendingLaunch*>(user_data);
    LaunchResult result;

    const char *payload = LSMessageGetPayload(message);
    json_object *reply = payload ? json_tokener_parse(payload) : NULL;
    json_object *value = NULL;

    if (reply && json_object_object_get_ex(reply, "returnValue", &value))
        result.success = json_object_get_boolean(value);

    if (result.success) {
        if (json_object_object_get_ex(reply, "instanceId", &value) ||
            json_object_object_get_ex(reply, "processId", &value))
            result.processId = json_object_get_string(value);
        // An older SAM gives no instance id; the app's id stands in for it
        if (result.processId.empty())
            result.processId = pending->appId;
    }
    else if (reply && json_object_object_get_ex(reply, "errorText", &value) && json_object_get_string(value)) {
        result.errorText = json_object_get_string(value);
    }
    else {
        result.errorText = std::string("SAM did not launch ") + pending->appId;
    }

    if (reply)
        json_object_put(reply);

    finishLaunch(pending, result);
    return true;
}

void ApplicationProcessManager::relaunch(const std::string& appId, const std::string& params)
{
    const ApplicationInfo *targetApp = 0;

    Q_FOREACH(const ApplicationInfo *app, mApplications) {
        if (app->appId() == QString::fromStdString(appId)) {
            targetApp = app;
            break;
        }
    }

    if (!targetApp)
        return;

    if (targetApp->type() == APPLICATION_TYPE_WEB) {
        WebAppMgrProxy::instance()->relaunch(appId, params);
    }
    else {
        qWarning("No wait to handle relaunch for application %s of type %d",
                 targetApp->appId().toUtf8().constData(), targetApp->type());
    }
}

qint64 ApplicationProcessManager::newProcessId()
{
    return mNextProcessId++;
}

void ApplicationProcessManager::notifyApplicationHasFinished(qint64 processId)
{
    ApplicationInfo *appToRemove = 0;
    Q_FOREACH(ApplicationInfo *appInfo, mApplications) {
        if (appInfo->processId() == processId) {
            appToRemove = appInfo;
            break;
        }
    }

    if (!appToRemove)
        return;

    notifyApplicationHasFinished(appToRemove);
}

void ApplicationProcessManager::notifyApplicationHasFinished(ApplicationInfo *app)
{
    qDebug() << __PRETTY_FUNCTION__ << app->appId();

    // FIXME do we have to do something else?

    mApplications.removeAll(app);
    delete app;
}

void ApplicationProcessManager::notifyApplicationHasStarted(ApplicationInfo *app)
{
    qDebug() << __PRETTY_FUNCTION__ << app->appId();
    connect(app, SIGNAL(finished()), this, SLOT(onApplicationHasFinished()));
    mApplications.append(app);
}

void ApplicationProcessManager::onApplicationHasFinished()
{
    ApplicationInfo *app = static_cast<ApplicationInfo*>(sender());
    notifyApplicationHasFinished(app);
}

void ApplicationProcessManager::removeAllWebApplications()
{
    qDebug() << __PRETTY_FUNCTION__;

    QList<ApplicationInfo*> appsToRemove;

    Q_FOREACH(ApplicationInfo *app, mApplications) {
        if (app->type() == APPLICATION_TYPE_WEB)
            appsToRemove.append(app);
    }

    Q_FOREACH(ApplicationInfo *app, appsToRemove) {
        mApplications.removeAll(app);
        app->deleteLater();
    }
}
