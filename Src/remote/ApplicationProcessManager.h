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

#ifndef APPLICATIONPROCESSMANAGER_H
#define APPLICATIONPROCESSMANAGER_H

#include <string>
#include <QString>
#include <QProcess>
#include <QList>
#include <QTemporaryFile>

#include "ApplicationDescription.h"
#include "Common.h"
#include "WindowTypes.h"

enum ApplicationType {
    APPLICATION_TYPE_NATIVE,
    APPLICATION_TYPE_WEB,
};

class ApplicationInfo : public QObject
{
    Q_OBJECT
public:
    ApplicationInfo(const QString& appId, qint64 processId, ApplicationType type) :
        mAppId(appId),
        mProcessId(processId),
        mType(type)
    {
    }

    QString appId() const { return mAppId; }
    qint64 processId() const { return mProcessId; }
    ApplicationType type() const { return mType; }

    virtual void kill() = 0;

Q_SIGNALS:
    void finished();

private:
    QString mAppId;
    qint64 mProcessId;
    ApplicationType mType;
};

class WebApplication : public ApplicationInfo
{
    Q_OBJECT
public:
    WebApplication(const QString& appId, qint64 processId, QObject *parent = 0);

    virtual void kill();
};

class NativeApplication : public ApplicationInfo
{
    Q_OBJECT
public:
    NativeApplication(const QString& appId, qint64 processId, QObject *parent = 0);
    ~NativeApplication();

    qint64 nativePid() { return mProcess->pid(); }
    QProcess* process() { return mProcess; }

    virtual void kill();

private Q_SLOTS:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onTerminationTimeoutReached();

private:
    QProcess *mProcess;
};

class ApplicationProcessManager : public QObject
{
    Q_OBJECT
public:
    static ApplicationProcessManager* instance();

    std::string launch(std::string appId, std::string params);
    void relaunch(std::string appId, std::string params);

    std::string getPid(std::string appId);
    bool isRunning(std::string appId);
	void killByAppId(std::string appId, bool notifyUser = false);
	void killByProcessId(qint64 processId, bool notifyUser = false);

    QList<ApplicationInfo*> runningApplications() const;

    void notifyApplicationHasStarted(ApplicationInfo *app);
    void notifyApplicationHasFinished(qint64 processId);
    void notifyApplicationHasFinished(ApplicationInfo *app);

    void removeAllWebApplications();

private Q_SLOTS:
    void onApplicationHasFinished();

private:
    ApplicationProcessManager();

    qint64 launchWebApp(const std::string& id, const std::string& params);
    qint64 launchNativeApp(ApplicationDescription *desc, std::string& params);
    qint64 launchQMLApp(ApplicationDescription *desc, std::string& params);
    qint64 launchProcess(const QString& id, const QString& path, const QStringList& parameters, unsigned int requiredRuntimeMemory);

    QString getAppInfoPathFromDesc(ApplicationDescription *desc);

    qint64 newProcessId();

	void killApp(ApplicationInfo *app);

    QList<ApplicationInfo*> mApplications;
    qint64 mNextProcessId;
};

#endif // APPLICATONPROCESSMANAGER_H
