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

bool ApplicationProcessManager::isRunning(std::string appId)
{
    Q_FOREACH(ApplicationInfo *app, mApplications) {
        if (app->appId() == QString::fromStdString(appId))
            return true;
    }

    return false;
}

std::string ApplicationProcessManager::getPid(std::string appId)
{
    ApplicationInfo *selectedApp = 0;

    Q_FOREACH(ApplicationInfo *app, mApplications) {
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

QList<ApplicationInfo*> ApplicationProcessManager::runningApplications() const
{
    return mApplications;
}

void ApplicationProcessManager::killByAppId(std::string appId, bool notifyUser)
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

    ApplicationDescription *desc = ApplicationManager::instance()->getAppById(app->appId().toStdString());

    std::string appName = "Application";
    std::string appTitle = "";

    if (desc) {
        appName = desc->menuName();
        const LaunchPoint *lp = desc->getDefaultLaunchPoint();
        if (lp)
            appTitle = lp->title();
    }

    ApplicationManager::instance()->postApplicationHasBeenTerminated(appTitle, appName, app->appId().toStdString());
}

std::string ApplicationProcessManager::launch(std::string appId, std::string params)
{
    qDebug() << "Launching application" << QString::fromStdString(appId);

    LSError lserror;
    LSErrorInit(&lserror);
    
    if(params.empty()) params = "{}";
    std::string SAM_params = "{ \"id\": \"" + appId + "\", \"params\": " + params + " }";
    g_warning("Delegating launch call to SAM...");
    if (!LSCall(ApplicationManager::instance()->getServiceHandle(),
                "luna://com.webos.service.applicationManager/launch", SAM_params.c_str(),
                NULL, NULL, NULL, &lserror))
    {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }
    return std::string("");
}

void ApplicationProcessManager::relaunch(std::string appId, std::string params)
{
    ApplicationInfo *targetApp = 0;

    Q_FOREACH(ApplicationInfo *app, mApplications) {
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
    }
}

QString ApplicationProcessManager::getAppInfoPathFromDesc(ApplicationDescription *desc)
{
    QString appInfoFilePath;

    if (desc->filePath().length() == 0)
    {
        QString appDescription = QString::fromStdString(desc->toString());
        QTemporaryFile appInfoFile;
        appInfoFile.setAutoRemove(false);
        appInfoFile.open();
        appInfoFile.write(appDescription.toUtf8());
        appInfoFile.close();

        appInfoFilePath = appInfoFile.fileName();
    }
    else
    {
        appInfoFilePath = QString::fromStdString(desc->filePath());
    }

    return appInfoFilePath;
}
