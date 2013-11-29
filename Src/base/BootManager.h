/* @@@LICENSE
*
* Copyright (c) 2013 Simon Busch
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

#ifndef BOOTMANAGER_H
#define BOOTMANAGER_H

#include <string>
#include <lunaservice.h>
#include <QTimer>
#include <QObject>
#include <QFileSystemWatcher>

#include "Common.h"

enum BootState {
	BOOT_STATE_STARTUP = 0,
	BOOT_STATE_FIRSTUSE,
	BOOT_STATE_NORMAL,
	BOOT_STATE_MAX,
};

enum BootEvent {
	BOOT_EVENT_FIRST_USE_DONE,
};

class BootStateBase
{
public:
	virtual ~BootStateBase() { }
	virtual void enter() { }
	virtual void leave() { }
	virtual void handleEvent(BootEvent event) = 0;
};

class BootStateStartup : public BootStateBase
{
public:
    virtual void enter();
	virtual void handleEvent(BootEvent event);
};

class BootStateFirstUse : public BootStateBase
{
public:
	virtual void enter();
	virtual void leave();
	virtual void handleEvent(BootEvent event);

private:
	LSMessageToken m_displayBlockToken;

private:
	void createLocalAccount();

	static bool cbCreateLocalAccount(LSHandle *handle, LSMessage *message, void *user_data);
};

class BootStateNormal : public BootStateBase
{
public:
	virtual void enter();
	virtual void leave();
	virtual void handleEvent(BootEvent event);

private:
	void launchBootTimeApps();
};

class BootManager : public QObject
{
	Q_OBJECT

public:
	~BootManager();

	static BootManager* instance();

	void switchState(BootState state);

	BootState currentState() const;

	LSHandle* service() const;

private:
	explicit BootManager();

	void startService();
	void stopService();

	void handleEvent(BootEvent event);

	void postCurrentState();

private Q_SLOTS:
	void onFileChanged(const QString& path);
    void onInitialize();

private:
    LSHandle* m_service;
	BootState m_currentState;
	BootStateBase *m_states[BOOT_STATE_MAX];
	QFileSystemWatcher m_fileWatch;

	friend class BootStateBase;
};

#endif
