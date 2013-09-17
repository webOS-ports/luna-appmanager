/* @@@LICENSE
*
* Copyright (c) 2013 Simon Busch <morphis@gravedo.de>
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

#ifndef INPUTEVENTMONITOR_H
#define INPUTEVENTMONITOR_H

#include <QObject>
#include <QSocketNotifier>

#include <nyx/nyx_client.h>

class InputControl;

class InputEventMonitor : public QObject
{
    Q_OBJECT
public:
    explicit InputEventMonitor(QObject *parent = 0);

    static InputEventMonitor* instance();

private Q_SLOTS:
    void readKeyboardData();
    void readTouchpanelData();

private:
    QSocketNotifier *mKeyboardNotifier;
    nyx_device_handle_t mKeyboardHandle;

    QSocketNotifier *mTouchpanelNotifier;
    nyx_device_handle_t mTouchpanelHandle;


    void setupEventSources();
};

#endif // INPUTEVENTMONITOR_H
