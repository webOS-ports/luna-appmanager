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

class ApplicationProcess : public QProcess
{
public:
    ApplicationProcess(const QString &id, QObject *parent = 0);

    QString id() const;

protected:
    void setupChildProcess();

private:
    QString m_id;
};

class ApplicationProcessManager : public QObject
{
    Q_OBJECT
public:
    static ApplicationProcessManager* instance();

    std::string launch(std::string appId, std::string params, WindowType::Type winType = WindowType::Type_Card);

    bool isRunning(std::string appId);
    void killByAppId(std::string appId);

    QList<ApplicationProcess*> runningApplications() const;

private Q_SLOTS:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    ApplicationProcessManager();

    qint64 launchWebApp(ApplicationDescription *desc, std::string& params, WindowType::Type winType);
    qint64 launchNativeApp(ApplicationDescription *desc, std::string& params, WindowType::Type winType);
    qint64 launchQMLApp(ApplicationDescription *desc, std::string& params, WindowType::Type winType);

    qint64 launchProcess(const QString& id, const QString& path, const QStringList& parameters);

    QList<ApplicationProcess*> m_applications;
};

#endif // APPLICATONPROCESSMANAGER_H
