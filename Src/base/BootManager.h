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
	BOOT_EVENT_COMPOSITOR_AVAILABLE,
	BOOT_EVENT_COMPOSITOR_NOT_AVAILABLE,
};

class DisplayBlocker
{
public:
	DisplayBlocker();
	~DisplayBlocker();

	void acquire(const char *clientName);
	void release();

	static bool cbRegistrationResponse(LSHandle *handle, LSMessage *message, void *user_data);

private:
	LSHandle *m_service;
	LSMessageToken m_token;
	bool m_acquired;
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
	DisplayBlocker m_displayBlocker;

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
	void activateSuspend(bool enable);
	void launchBootTimeApps();
};

class BootManager : public QObject
{
	Q_OBJECT

public:
	~BootManager();

	static BootManager* instance();

	bool compositorAvailable() const;

	void switchState(BootState state);
	void handleEvent(BootEvent event);

	BootState currentState() const;

	bool isBootFinished() const;

	LSHandle* service() const;

Q_SIGNALS:
	void compositorAvailableChanged();
	void bootFinished();

private:
	explicit BootManager();

	void startService();
	void stopService();

	void postCurrentState();

	void setCompositorAvailabe(bool value);

private Q_SLOTS:
	void onFileChanged(const QString& path);
    void onInitialize();

private:
    LSHandle* m_service;
	BootState m_currentState;
	BootStateBase *m_states[BOOT_STATE_MAX];
	QFileSystemWatcher m_fileWatch;
	bool m_compositorAvailable;

	friend class BootStateBase;
};

#endif
