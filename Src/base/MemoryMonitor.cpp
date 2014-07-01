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




#include "Common.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <strings.h>


#include "MemoryMonitor.h"

#include "Settings.h"
#include "Time.h"
#include "HostBase.h"

static const int kTimerMs = 5000;
static const int kLowMemExpensiveTimeoutMultiplier = 2;
static const int kNativeMaxMemoryViolationThreshold = 1;

static const std::string sKBLabel("kb");
static const std::string sMBLabel("mb");
static const std::string sProcRSS("VmRSS");
static const std::string sProcSwap("VmSwap");

#define OOM_ADJ_PATH			"/proc/%d/oom_adj"
#define OOM_SCORE_ADJ_PATH		"/proc/%d/oom_score_adj"

#define OOM_ADJ_VALUE			-17
#define OOM_SCORE_ADJ_VALUE		-1000

MemoryMonitor* MemoryMonitor::instance()
{
	static MemoryMonitor* s_instance = 0;
	if (G_UNLIKELY(s_instance == 0))
		s_instance = new MemoryMonitor;

	return s_instance;
}

MemoryMonitor::MemoryMonitor()
	: m_timer(HostBase::instance()->masterTimer(), this, &MemoryMonitor::timerTicked)
	, m_currRssUsage(0)
	, m_state(MemoryMonitor::Normal)
{
	m_fileName[kFileNameLen - 1] = 0;
	snprintf(m_fileName, kFileNameLen - 1, "/proc/%d/statm", getpid());

	/* Adjust OOM killer so we're never killed for memory reasons */
	adjustOomScore();
}

MemoryMonitor::~MemoryMonitor()
{
}

void MemoryMonitor::adjustOomScore()
{
	struct stat st;
	int score_value = 0;

	/* Adjust OOM killer so we're never killed for memory reasons */
	char oom_adj_path[kFileNameLen];
	snprintf(oom_adj_path, kFileNameLen - 1, OOM_SCORE_ADJ_PATH, getpid());
	if (stat(oom_adj_path, &st) == -1) {
		snprintf(oom_adj_path, kFileNameLen - 1, OOM_ADJ_PATH, getpid());
		if (stat(oom_adj_path, &st) == -1) {
			g_warning("Failed to adjust OOM value");
			return;
		}
		else {
			score_value = OOM_ADJ_VALUE;
		}
	}
	else {
		score_value = OOM_SCORE_ADJ_VALUE;
	}

	FILE* f = fopen(oom_adj_path, "wb");
	if (f) {
		fprintf(f, "%i\n", score_value);
		fclose(f);
	}
}

void MemoryMonitor::start()
{
	if (m_timer.running())
		return;

	m_timer.start(kTimerMs);
}

static const char* nameForState(MemoryMonitor::MemState state)
{
	switch (state) {
	case (MemoryMonitor::Low):
		return "Low";
	case (MemoryMonitor::Critical):
		return "Critical";
	case (MemoryMonitor::Medium):
		return "Medium";
	default:
		break;
	}

	return "Normal";
}

bool MemoryMonitor::timerTicked()
{
	if (m_state == Normal)
		return true;

	m_currRssUsage = getCurrentRssUsage();

	g_warning("SysMgr MemoryMonitor: LOW MEMORY: State: %s, current RSS usage: %dMB\n",
			  nameForState(m_state), m_currRssUsage);

	return true;
}

int MemoryMonitor::getCurrentRssUsage() const
{
	FILE* f = fopen(m_fileName, "rb");
	if (!f)
		return m_currRssUsage;

	int totalSize, rssSize;

	int result = fscanf(f, "%d %d", &totalSize, &rssSize);
	(void) result;

	fclose(f);

	return (rssSize * 4096) / (1024 * 1024);
}

int MemoryMonitor::getProcessMemInfo(pid_t pid)
{
	int procRss  = -1;
	int procSwap = -1;
	char fileName[kFileNameLen];
	fileName[kFileNameLen - 1] = 0;

	snprintf(fileName, kFileNameLen - 1, "/proc/%d/status", pid);
	std::ifstream status(fileName);

	if (!status)
		return -1;

	std::string field;
	std::string label;

	while(status >> field) {
		// strip off the ':' on the end of each label
		field = field.substr(0, field.length() - 1);
		
		if (field == sProcRSS) {
			status >> procRss;
			status >> label;

			// Make sure the value is in megabytes
			if (!strcasecmp(label.c_str(), sKBLabel.c_str()))
				procRss /= 1024;
			else if (strcasecmp(label.c_str(), sMBLabel.c_str()))
				procRss /= 1024 * 1024;

			if (procSwap != -1)
				break;
		}
		else if (field == sProcSwap) {
			status >> procSwap;
			status >> label;

			//Make sure the value is in megabytes
			if (!strcasecmp(label.c_str(), sKBLabel.c_str()))
				procSwap /= 1024;
			else if (strcasecmp(label.c_str(), sMBLabel.c_str()))
				procSwap /= 1024 * 1024;

			if(procRss != -1)
				break;
		}
	}

	status.close();

	if ((-1 == procRss) || (-1 == procSwap))
		return -1;

	return procRss + procSwap;
}

void MemoryMonitor::monitorNativeProcessMemory(pid_t pid, int maxMemAllowed, pid_t updateFromPid)
{
}

bool MemoryMonitor::allowNewNativeAppLaunch(int appMemoryRequirement)
{
	if (m_state >= Low)
		// already in Low or critical memory states, so do not allow new apps to be launched
		return false;
	
	// OK to launch new app with specified memory requirements
	return true;
}
