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

#ifndef WEBAPPMGRPROXY_H
#define WEBAPPMGRPROXY_H

#include <glib.h>
#include <luna-service2/lunaservice.h>

#include <QMap>

#include "Common.h"
#include "WindowTypes.h"
#include "ApplicationDescription.h"

class WebAppMgrProxy : public QObject
{
    Q_OBJECT

public:
    static void setAppToLaunchUponConnection(char* app);

    static WebAppMgrProxy* instance();

    bool connected();

    virtual ~WebAppMgrProxy();

    void launchUrl(const char* url, WindowType::Type winType=WindowType::Type_Card,
                   ApplicationDescription *appDesc = 0, qint64 processId = 0,
                   const char* params="", const char* launchingAppId="",
                   const char* launchingProcId="");

    std::string launchApp(const std::string& appId,
                          const std::string& params,
                          qint64 processId,
                          const std::string& launchingAppId,
                          const std::string& launchingProcId,
                          std::string& errMsg);

    void relaunch(const std::string& appId,
                  const std::string& params);

    void killApp(qint64 processId);

    static gboolean retryConnectWebAppMgr(gpointer user_data);
    static bool webAppManagerServiceStatusCb(LSHandle *handle, LSMessage *message, void *user_data);
    static bool listRunningAppsCb(LSHandle *handle, LSMessage *message, void *user_data);
//    static bool appEventCb(LSHandle *handle, LSMessage *message, void *user_data);


Q_SIGNALS:
    void connectionStatusChanged();
    void signalAppLaunchPreventedUnderLowMemory();
    void signalLowMemoryActionsRequested (bool allowExpensive);

private:
    WebAppMgrProxy();

    WebAppMgrProxy(const WebAppMgrProxy&);
    WebAppMgrProxy& operator=(const WebAppMgrProxy&);

    void connectWebAppMgr();
    void onWebAppManagerConnected();
    void onWebAppManagerDisconnected();

    void updateRunningApps(const char *payload);
//    void handleAppEvent(const char *payload);

    bool mConnected;
    LSHandle *mService;
};

#endif /* WEBAPPMGRPROXY_H */
