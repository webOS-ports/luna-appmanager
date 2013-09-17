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

#include <QDebug>

#include "InputEventMonitor.h"
#include "HostBase.h"
#include "InputControl.h"
#include "DisplayManager.h"
#include "CustomEvents.h"

InputEventMonitor::InputEventMonitor(QObject *parent) :
    QObject(parent)
{
    setupEventSources();
}

InputEventMonitor* InputEventMonitor::instance()
{
    static InputEventMonitor* s_instance = 0;
    if (G_UNLIKELY(s_instance == 0))
        s_instance = new InputEventMonitor;

    return s_instance;
}

void InputEventMonitor::setupEventSources()
{
    nyx_error_t error = NYX_ERROR_NONE;

    InputControl *keyboardControl = HostBase::instance()->getInputControlKeys();
    if (keyboardControl) {
        mKeyboardHandle = keyboardControl->getHandle();
        if (NULL == mKeyboardHandle) {
            qWarning() << __PRETTY_FUNCTION__ <<  "Unable to obtain handle for keyboard input control";
            return;
        }

        int fd = -1;
        error = nyx_device_get_event_source(mKeyboardHandle, &fd);
        if (error != NYX_ERROR_NONE || fd <= 0) {
            qWarning() << __PRETTY_FUNCTION__ << "Unable to obtain keyboard event_source";
            return;
        }

        mKeyboardNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(mKeyboardNotifier, SIGNAL(activated(int)), this, SLOT(readKeyboardData()));
    }
    else {
        qWarning() << __PRETTY_FUNCTION__ << "Got invalid keyboard input control object";
    }

    InputControl *touchpanelControl = HostBase::instance()->getInputControlTouchpanel();
    if (touchpanelControl) {
        mTouchpanelHandle = touchpanelControl->getHandle();
        if (NULL == mTouchpanelHandle) {
            qWarning() << __PRETTY_FUNCTION__ <<  "Unable to obtain handle for keyboard input control";
            return;
        }

        int fd = -1;
        error = nyx_device_get_event_source(mTouchpanelHandle, &fd);
        if (error != NYX_ERROR_NONE || fd <= 0) {
            qWarning() << __PRETTY_FUNCTION__ << "Unable to obtain keyboard event_source";
            return;
        }

        mTouchpanelNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(mTouchpanelNotifier, SIGNAL(activated(int)), this, SLOT(readTouchpanelData()));
    }
    else {
        qWarning() << __PRETTY_FUNCTION__ << "Got invalid touchpanel input control object";
    }
}

void InputEventMonitor::readKeyboardData()
{
    nyx_error_t error = NYX_ERROR_NONE;
    nyx_event_handle_t event_handle = NULL;

    while ((error = nyx_device_get_event(mKeyboardHandle, &event_handle)) == NYX_ERROR_NONE && event_handle != NULL)
    {
        int keycode = 0;
        bool is_press = false;

        error = nyx_keys_event_get_key(event_handle, &keycode);
        if (error == NYX_ERROR_NONE)
            error = nyx_keys_event_get_key_is_press(event_handle, &is_press);
        if (error != NYX_ERROR_NONE) {
            qWarning() << __PRETTY_FUNCTION__ << "Unable to obtain event_handle properties";
            return;
        }

        if (keycode == NYX_KEYS_CUSTOM_KEY_POWER_ON)
            DisplayManager::instance()->updateState(is_press ? DISPLAY_EVENT_POWER_BUTTON_DOWN : DISPLAY_EVENT_POWER_BUTTON_UP);

        error = nyx_device_release_event(mKeyboardHandle, event_handle);
        if (error != NYX_ERROR_NONE) {
            qWarning() << __PRETTY_FUNCTION__ << "Unable to release m_nyxKeysHandle event";
            return;
        }

        event_handle = NULL;
    }
}

void InputEventMonitor::readTouchpanelData()
{
    nyx_error_t error = NYX_ERROR_NONE;
    nyx_event_handle_t event_handle = NULL;
    nyx_touchpanel_event_item_t* touches;
    int count = 0;

    while ((error = nyx_device_get_event(mTouchpanelHandle, &event_handle)) == NYX_ERROR_NONE && event_handle != NULL)
    {
        error = nyx_touchpanel_event_get_touches(event_handle, &touches, &count);
        if (error != NYX_ERROR_NONE)
        {
            qWarning() << __PRETTY_FUNCTION__ << "Unable to obtain touchpanel event touches";
            return;
        }

        if (count > 0)
             DisplayManager::instance()->handleTouchEvent();

        nyx_device_release_event(mTouchpanelHandle, event_handle);
        if (error != NYX_ERROR_NONE)
        {
            qWarning() << __PRETTY_FUNCTION__ << "Unable to release touchpanel event_handle event";
            return;
        }

        event_handle = NULL;
    }
}
