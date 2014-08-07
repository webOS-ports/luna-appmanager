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

#include "ApplicationProcessManager.h"
#include "ApplicationDescription.h"
#include "ApplicationManager.h"

#include "WebAppMgrProxy.h"

#define WEBAPP_LAUNCHER_PATH    "/usr/sbin/webapp-launcher"
#define QMLAPP_LAUNCHER_PATH    "/usr/sbin/luna-qml-launcher"

NativeApplication::NativeApplication(const QString &appId, qint64 processId, QProcess *process, QObject *parent) :
    ApplicationInfo(appId, processId, APPLICATION_TYPE_NATIVE),
    mProcess(process)
{
    connect(mProcess, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(onProcessFinished(int,QProcess::ExitStatus)));
}

void NativeApplication::kill()
{
}

void NativeApplication::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    mProcess->close();
    delete mProcess;

    Q_EMIT finished();
}

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

void ApplicationProcessManager::killByAppId(std::string appId)
{
    Q_FOREACH(ApplicationInfo *app, mApplications) {
        if (app->appId() == QString::fromStdString(appId)) {
            app->kill();
            break;
        }
    }
}

std::string ApplicationProcessManager::launch(std::string appId, std::string params)
{
    qDebug() << "Launching application" << QString::fromStdString(appId);

    ApplicationDescription* desc = ApplicationManager::instance()->getPendingAppById(appId);
    if (!desc) {
        desc = ApplicationManager::instance()->getAppById(appId);
        if (!desc) {
            g_warning("Failed to find application description for app %s",
                      appId.c_str());
            return std::string("");
        }
    }

    bool running = false;
    qint64 processId = 0;

    Q_FOREACH(ApplicationInfo *app, mApplications) {
        if (app->appId() == QString::fromStdString(appId)) {
            running = true;
            processId = app->processId();
            break;
        }
    }

    if (!running) {
        processId = 0;
        switch (desc->type()) {
        case ApplicationDescription::Type_Web:
            processId = launchWebApp(appId, params);
            break;
        case ApplicationDescription::Type_Native:
        case ApplicationDescription::Type_PDK:
        case ApplicationDescription::Type_Qt:
            processId = launchNativeApp(desc, params);
            break;
        case ApplicationDescription::Type_QML:
            processId = launchQMLApp(desc, params);
            break;
        default:
            break;
        }
    }
    else {
        qWarning("Application %s is already running. Sending relaunch signal ...",
                 appId.c_str());
        // FIXME send relaunch signal
    }

    if (processId <= 0)
        return std::string("");

    return QString::number(processId).toStdString();
}

qint64 ApplicationProcessManager::newProcessId()
{
    return mNextProcessId++;
}

qint64 ApplicationProcessManager::launchWebApp(const std::string& id, const std::string& params)
{
    int64_t processId = newProcessId();

    std::string launchingAppId = "";
    std::string launchingProcId = "";
    std::string errMsg = "";

    WebAppMgrProxy::instance()->launchApp(id, params, processId, launchingAppId, launchingProcId, errMsg);

    return processId;
}

qint64 ApplicationProcessManager::launchProcess(const QString& id, const QString &path, const QStringList &parameters)
{
    qDebug() << "Starting process" << id << path << parameters;

    QProcess *process = new QProcess();

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("XDG_RUNTIME_DIR","/tmp/luna-session");
    environment.insert("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
    environment.insert("QT_IM_MODULE", "Maliit");
    environment.insert("SDL_VIDEODRIVER", "wayland");

    process->setProcessEnvironment(environment);
    process->setProcessChannelMode(QProcess::ForwardedChannels);

    // NOTE: Currently we're just forking once so the new process will be a child of ours and
    // will exit once we exit.
    process->start(path, parameters);
    process->waitForStarted();

    if (process->state() != QProcess::Running) {
        qDebug() << "Failed to start process";
        process->deleteLater();
        return -1;
    }

    int64_t processId = newProcessId();
    notifyApplicationHasStarted(new NativeApplication(id, process, processId));

    return processId;
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

qint64 ApplicationProcessManager::launchNativeApp(ApplicationDescription *desc, std::string &params)
{
    QStringList parameters;
    parameters << QString("'%1'").arg(params.c_str());

    QString entryPoint = QString::fromStdString(desc->entryPoint());
    if (entryPoint.startsWith("file://"))
        entryPoint = entryPoint.right(entryPoint.length() - 7);

    return launchProcess(QString::fromStdString(desc->id()), entryPoint, parameters);
}

qint64 ApplicationProcessManager::launchQMLApp(ApplicationDescription *desc, std::string &params)
{
    QStringList parameters;
    QString appParams = QString::fromStdString(params);
    parameters << getAppInfoPathFromDesc(desc);
    if (appParams.length() > 0)
        parameters << appParams;

    return launchProcess(QString::fromStdString(desc->id()), QMLAPP_LAUNCHER_PATH, parameters);
}
