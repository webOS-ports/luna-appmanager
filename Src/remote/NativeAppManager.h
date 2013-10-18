/* @@@LICENSE
*
*      Copyright (c) 2009-2013 LG Electronics, Inc.
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

#ifndef NATIVEAPPMANAGER_H
#define NATIVEAPPMANAGER_H

#include "Common.h"

#include <map>
#include <set>
#include <string>

#include "Timer.h"
#include "ApplicationDescription.h"

class NativeAppManager
{
public:
	static NativeAppManager* instance();

	int launchNativeProcess(const std::string& appId, const char* path, char* const argv[], ApplicationDescription::Type appType, int requiredMemory = 0);
	void suspendProcess(int pid);
	void resumeProcess(int pid);
	void killProcess(int pid, bool notifyUser=false);
	void processRemoved(int pid, bool doCleanup=true);
	void addProcessToNukeList(int pid);

private:
	NativeAppManager();
	~NativeAppManager();

	bool nukeProcessTimer();
	std::string appIdFromPid(int pid);

	static void childProcessDiedCallback(GPid pid, gint status, gpointer data);
	void childProcessDied(GPid, gint status);

private:
	typedef std::map<std::string, int> ProcessMap;
	typedef std::set<int> ProcessSet;

	ProcessMap m_nativeProcessMap;
	ProcessMap m_webAppProcessMap;
	ProcessSet m_nukeSet;
	Timer<NativeAppManager> m_nukeProcessTimer;
};

#endif /* NATIVEAPPMANAGER_H */
