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

#include "Common.h"

#include "NativeAppManager.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "HostBase.h"
#include "Settings.h"
#include "ApplicationManager.h"
#include "SystemService.h"
#include "MemoryMonitor.h"
#include "CpuAffinity.h"

#include "rolegen.h"

// the prefix that comes before the appid in all app paths
const char *APP_PREFIX = "/media/cryptofs/apps/usr/palm/applications/";

static int kNukeProcessTimeoutMs = 2000;

static int PrvForkFunction(void* data) __attribute__((unused));
static pid_t PrvLaunchProcess(char* const args[]);

// strips one level of directory from the path
// /foo/bar/your mom/ becomes /foo/bar/
// /foo/bar/ becomes /foo/
// /foo/ becomes /
// This function was copied as-is from libhelpers
static void stripOneDirectoryLevel(char *path)
{
	// start off at the last character
	char *p = path + strlen(path) - 1;

	if ( p <= path )
	{
		// there's nothing left to do
		path[0] = 0;
		return;
	}

	// we use a do-while rather than a while to ensure
	// we take a step before checking. In most cases,
	// we will start off with p at a slash. wouldn't
	// want to suddenly end right there.
	do
	{
		p--;
		if ( p == path )
		{
			// we reached the beginning without ever finding a slash?
			// there's something weird going on here. just clear it out
			// I guess
			path[0] = 0;
			return;
		}
	} while ( *p != '/' );

	// p is currently sitting on a slash. We want
	// to be 1 character after that (because we want to keep
	// the slash character in the string)
	p++;

	// end the string here.
	*p = 0;

	// that's all there is to do.
}

// This function was copied as-is from libhelpers
static int HApp_GetBaseFile(const char *fullPath, char *outBuffer, int outBufferLen)
{
	char workingPath[PATH_MAX];
	strncpy(workingPath, fullPath, PATH_MAX);
	stripOneDirectoryLevel(workingPath);

	// did we strip the whole thing? If so, that means there was no path at all.
	if ( workingPath[0] == 0 )
	{
		// just give them back what they sent us. There's no path to remove
		strncpy(outBuffer, fullPath, outBufferLen);
		return 1;
	}

	// it stripped something. The part it stripped is the part we want.
	int len = strlen(workingPath);

	// we want to start our copying in the full path at the character
	// after the "/". That means taking the length of the stripped
	// path then adding that to the fullPath pointer. That'll be
	// the first character of the base name
	const char *startCopy = fullPath + len;
	strncpy(outBuffer, startCopy, outBufferLen);
	return 1;
}

NativeAppManager* NativeAppManager::instance()
{
	static NativeAppManager* s_server = 0;
	if (G_UNLIKELY(s_server == 0)) {
		s_server = new NativeAppManager;
	}

	return s_server;
}

NativeAppManager::NativeAppManager() :
	m_nukeProcessTimer(HostBase::instance()->masterTimer(), this, &NativeAppManager::nukeProcessTimer)
{
	static const int kGroupId = 1000;  // group "luna"
}

NativeAppManager::~NativeAppManager()
{
}

int NativeAppManager::launchNativeProcess(const std::string& appId, const char* path,
	char* const argv[], ApplicationDescription::Type appType, int requiredMemory)
{
	ProcessMap::const_iterator it = m_nativeProcessMap.find(appId);
	if (it != m_nativeProcessMap.end()) {
		g_debug("%s: Process %s (%s) already running. Not launching",
				__PRETTY_FUNCTION__, appId.c_str(), path);

		// FIXME handle possible relaunches through something like a ls2 API?

		return it->second;
	}

	if (!MemoryMonitor::instance()->allowNewNativeAppLaunch(requiredMemory)){ 
		g_warning("%s: Low memory condition Not allowing native app for appId: %s. RequiredMemory = %d",
				  __PRETTY_FUNCTION__, appId.c_str(), requiredMemory);
		return 0; // return 0 for low memory failure
	}
	
	// push a role file. This will allow the native app to make
	// luna-service calls. This call is fast, and safe to call when the role file
	// already exists, so we just call it every launch.
	pdkGenerateRole(appId, path);

	int i = 0, existingArgumentCount;
	for (existingArgumentCount = 0; argv[existingArgumentCount]; existingArgumentCount++);

	const char *newArgs[existingArgumentCount + 2];
	newArgs[0] = path;

	for (int n = 1; n < existingArgumentCount; n++)
		newArgs[n] = argv[existingArgumentCount];

	newArgs[existingArgumentCount + 1] = NULL;

	g_message("%s: Process %s launching", __PRETTY_FUNCTION__, appId.c_str());

	// fire off the process (jailer will preserve the lib path value in the new environment)
	pid_t pid = PrvLaunchProcess((char **) newArgs);

	if (pid < 0) {
		g_critical("%s:%d failed to fork: %s", __PRETTY_FUNCTION__, __LINE__,
				strerror(errno));
		return -1; // return -1 for launch error
	}
	else if (requiredMemory > 0)
	{
		// add a memory watch for this native app
		MemoryMonitor::instance()->monitorNativeProcessMemory(pid, requiredMemory);
	}

	m_nativeProcessMap[appId] = pid;
	g_message("%s: Process %s (%s) launched with pid: %d", __PRETTY_FUNCTION__,
			  appId.c_str(), path, pid);

	g_child_watch_add_full(G_PRIORITY_HIGH, pid, NativeAppManager::childProcessDiedCallback,
		this, NULL);

	return pid;
}

void NativeAppManager::suspendProcess(int pid)
{
	::kill(pid, SIGSTOP);
}

void NativeAppManager::resumeProcess(int pid)
{
	::kill(pid, SIGCONT);
}

void NativeAppManager::killProcess(int pid, bool notifyUser)
{
	g_warning("%s: killing process: %d", __PRETTY_FUNCTION__, pid);

	if(notifyUser) {
		std::string nullString;
		std::string appId = appIdFromPid(pid);
		std::string appName("Application");
		std::string appTitle;

		if(!appId.empty()){
			ApplicationDescription*  desc = ApplicationManager::instance()->getAppById(appId);
			if(desc){
				appName = desc->menuName();
				if (desc->getDefaultLaunchPoint()){
					appTitle = desc->getDefaultLaunchPoint()->title();
				}
			}
		}

		SystemService::instance()->postApplicationHasBeenTerminated(appTitle, appName, appId);
	}

	::kill(pid, SIGKILL);
	processRemoved(pid, false);
}

void NativeAppManager::processRemoved(int pid, bool doCleanup)
{
	if (doCleanup)
		::waitid(P_PID, pid, NULL, WEXITED | WNOHANG);

	for (ProcessMap::iterator it = m_nativeProcessMap.begin();
		 it != m_nativeProcessMap.end(); ++it) {

		if (it->second == pid) {
			g_message("%s: pid: %d, appId: %s:", __PRETTY_FUNCTION__,
					  pid, it->first.c_str());
			m_nativeProcessMap.erase(it);
			return;
		}
	}

	for (ProcessMap::iterator it = m_webAppProcessMap.begin();
		 it != m_webAppProcessMap.end(); ++it) {

		if (it->second == pid) {
			g_message("%s: pid: %d, appId: %s:", __PRETTY_FUNCTION__,
					  pid, it->first.c_str());
			m_webAppProcessMap.erase(it);
			return;
		}
	}
}

void NativeAppManager::addProcessToNukeList(int pid)
{
	m_nukeSet.insert(pid);
	m_nukeProcessTimer.stop();
	m_nukeProcessTimer.start(kNukeProcessTimeoutMs, true);
}

bool NativeAppManager::nukeProcessTimer()
{
	for (ProcessSet::const_iterator it = m_nukeSet.begin();
		 it != m_nukeSet.end(); ++it) {
		killProcess(*it);
	}

	m_nukeSet.clear();

	return false;
}

std::string NativeAppManager::appIdFromPid(int pid)
{
	const int len = 256;
	gchar buf[len];
	gchar  *exeName;
	GError *error = 0;

	snprintf(buf, len-1, "/proc/%d/exe", pid);
	exeName = g_file_read_link(buf, &error);

	if (error)
	{
		g_critical("NativeAppManager::appIdFromPid failed: %s", error->message);
		g_error_free(error);
	}

	
	if (NULL != exeName) {
		snprintf(buf, len-1, "%s", exeName);
		free(exeName);
	}
	else {
		return std::string();
	}

	std::string appInstallRelative = Settings::LunaSettings()->appInstallRelative;
	gchar* location = g_strrstr(buf, appInstallRelative.c_str());
	if (!location)
		return std::string();

	gchar* appIdStart = location + strlen(appInstallRelative.c_str());

	while (true) {
		if (*appIdStart == 0) {
			// reached end of string
			return std::string();
		}

		if (*appIdStart != '/')
			break;

		appIdStart++;
	}

	gchar* appIdEnd = appIdStart;
	while (true) {
		if (*appIdEnd == 0) {
			// reached end of string
			return std::string();
		}

		if (*appIdEnd == '/')
			break;

		appIdEnd++;
	}

	if (appIdEnd <= appIdStart)
	{
		return std::string();
	}
	
	return std::string(appIdStart, appIdEnd - appIdStart);
}

void NativeAppManager::childProcessDiedCallback(GPid pid, gint status, gpointer data)
{
	NativeAppManager* server = (NativeAppManager*) data;
	server->childProcessDied(pid, status);
}

void NativeAppManager::childProcessDied(GPid pid, gint status)
{
	g_message("%s: pid: %d, status %d", __PRETTY_FUNCTION__, pid, status); 
	processRemoved(pid, false);
	g_spawn_close_pid(pid);
}

static int PrvForkFunction(void* data)
{
	// Move this process into the root cgroup
	int pid = getpid();

	const int maxSize = 64;
	char buf[maxSize];

	snprintf(buf, maxSize - 1, "%d", pid);
	buf[maxSize - 1] = 0;

	int fd = ::open("/dev/cgroup/tasks", O_WRONLY);
	if (fd >= 0) {
		ssize_t result = ::write(fd, buf, ::strlen(buf) + 1);
		Q_UNUSED(result);
		::close(fd);
	}
	else {
		perror("root cgroup tasks file not found");
	}

	// Reset cpu affinity before entering the jail because the jail
	// prevents pdk apps from accessing libaffinity system files.
	resetCpuAffinity(0);

	setenv("XDG_RUNTIME_DIR", "/tmp/luna-session", 1);

	// Now exec the process
	char ** argv = (char **) data;
	int ret = ::execv(argv[0], argv);
	if (ret < 0)
		perror("execv failed");

	return 0;
}

static pid_t PrvLaunchProcess(char* const args[])
{
	pid_t pid = ::fork();
	if (pid < 0)
		return pid;

	if (pid == 0) {
		(void) PrvForkFunction((char**)args);
		exit(-1);
	}

	return pid;
}
