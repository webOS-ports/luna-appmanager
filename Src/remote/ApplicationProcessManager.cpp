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

#define WEBAPP_LAUNCHER_PATH    "/usr/sbin/webapp-launcher"
#define QMLAPP_LAUNCHER_PATH    "/usr/bin/qt5/qmlscene"

ApplicationProcess::ApplicationProcess(const QString& id, QObject *parent) :
    QProcess(parent),
    m_id(id)
{
}

void ApplicationProcess::setupChildProcess()
{
}

QString ApplicationProcess::id() const
{
    return m_id;
}

ApplicationProcessManager* ApplicationProcessManager::instance()
{
    static ApplicationProcessManager *instance = 0;
    if (!instance)
        instance = new ApplicationProcessManager();

    return instance;
}

ApplicationProcessManager::ApplicationProcessManager() :
    QObject(0)
{
}

bool ApplicationProcessManager::isRunning(std::string appId)
{
    Q_FOREACH(ApplicationProcess *app, m_applications) {
        if (app->id() == QString::fromStdString(appId))
            return true;
    }

    return false;
}

QList<ApplicationProcess*> ApplicationProcessManager::runningApplications() const
{
    return m_applications;
}

void ApplicationProcessManager::killByAppId(std::string appId)
{
    Q_FOREACH(ApplicationProcess *app, m_applications) {
        if (app->id() == QString::fromStdString(appId)) {
            app->kill();
        }
    }
}

std::string ApplicationProcessManager::launch(std::string appId, std::string params, WindowType::Type winType)
{
    qDebug() << "Launching application" << QString::fromStdString(appId);

    ApplicationDescription* desc = ApplicationManager::instance()->getPendingAppById(appId);
    if (!desc)
        desc = ApplicationManager::instance()->getAppById(appId);

    bool running = false;
    qint64 pid = 0;
    Q_FOREACH(ApplicationProcess *app, m_applications) {
        if (app->id() == QString::fromStdString(appId)) {
            running = true;
            pid = app->pid();
            break;
        }
    }

    if (!running) {
        pid = 0;
        switch (desc->type()) {
        case ApplicationDescription::Type_Web:
            pid = launchWebApp(desc, params, winType);
            break;
        case ApplicationDescription::Type_Native:
        case ApplicationDescription::Type_PDK:
        case ApplicationDescription::Type_Qt:
            pid = launchNativeApp(desc, params, winType);
            break;
        case ApplicationDescription::Type_QML:
            pid = launchQMLApp(desc, params, winType);
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

    if (pid <= 0)
        return std::string("");

    QString processId = QString::number(pid);
    return processId.toStdString();
}

qint64 ApplicationProcessManager::launchProcess(const QString& id, const QString &path, const QStringList &parameters)
{
    qDebug() << "Starting process" << id << path << parameters;

    ApplicationProcess *process = new ApplicationProcess(id);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("XDG_RUNTIME_DIR","/tmp/luna-session");
    environment.insert("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
    environment.insert("QT_IM_MODULE", "Maliit");

    process->setProcessEnvironment(environment);
    process->setProcessChannelMode(QProcess::ForwardedChannels);

    connect(process, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(onProcessFinished(int,QProcess::ExitStatus)));

    // NOTE: Currently we're just forking once so the new process will be a child of ours and
    // will exit once we exit.
    process->start(path, parameters);
    process->waitForStarted();

    if (process->state() != QProcess::Running) {
        qDebug() << "Failed to start process";
        process->deleteLater();
        return -1;
    }

    m_applications.append(process);

    return process->pid();
}

void ApplicationProcessManager::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    ApplicationProcess *process = static_cast<ApplicationProcess*>(sender());

    qDebug() << "Application" << process->id() << "exited";

    m_applications.removeAll(process);
    delete process;
}

qint64 ApplicationProcessManager::launchWebApp(ApplicationDescription *desc, std::string &params, WindowType::Type winType)
{
    QString appDescription = QString::fromStdString(desc->toString());
    QTemporaryFile appInfoFile;
    appInfoFile.setAutoRemove(false);
    appInfoFile.open();
    appInfoFile.write(appDescription.toUtf8());
    appInfoFile.close();

    QString winTypeStr = "card";
    switch (winType) {
    case WindowType::Type_Launcher:
        winTypeStr = "launcher";
        break;
    default:
        break;
    }

    QStringList parameters;
    parameters << "-u" << QString::fromStdString(desc->entryPoint());
    parameters << "-w" << winTypeStr;
    if (params.length() > 0)
        parameters << "-p" << QString::fromStdString(params);
    parameters << "-a" << appInfoFile.fileName();

    return launchProcess(QString::fromStdString(desc->id()), WEBAPP_LAUNCHER_PATH, parameters);
}

qint64 ApplicationProcessManager::launchNativeApp(ApplicationDescription *desc, std::string &params, WindowType::Type winType)
{
    QStringList parameters;
    parameters << QString::fromStdString(params);

    return launchProcess(QString::fromStdString(desc->id()), QString::fromStdString(desc->entryPoint()), parameters);
}

qint64 ApplicationProcessManager::launchQMLApp(ApplicationDescription *desc, std::string &params, WindowType::Type winType)
{
    return -1;
}
