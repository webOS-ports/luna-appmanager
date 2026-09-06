/* @@@LICENSE
*
*      Copyright (c) 2008-2013 LG Electronics, Inc.
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
/**
 * @file
 * 
 * Main entry point for Luna
 */




#include "Common.h"

#include "HostBase.h"
#include "ApplicationDescription.h"
#include "ApplicationManager.h"
#include "Localization.h"


#include "MemoryMonitor.h"
#include "Settings.h"
#include "ApplicationInstaller.h"
#include "Preferences.h"
#include "Logging.h"
#include "EventReporter.h"
#include "BootManager.h"

#include "WebAppMgrProxy.h"

#include <ProcessKiller.h>

#include <sys/time.h>
#include <sys/resource.h>
#include <glib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>

#include <signal.h>
#include <stdarg.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/file.h>

#include <QApplication>
#include <QtGui>
#include <QtGlobal> 

/* Convenience macro for simulating crashes for debugging purposes only:
#define crash() {                               \
        volatile int *ip = (volatile int *)0;   \
        *ip = 0;                                \
    }
*/


//#define PRINT_MALLOC_STATS
#ifdef __cplusplus
extern "C" {
#endif
extern void malloc_stats(void);
#ifdef __cplusplus
};
#endif

static gchar* s_uiStr = NULL;
static gchar* s_appToLaunchStr = NULL;
static gchar* s_logLevelStr = NULL;
static gboolean s_useSysLog = false;
static gboolean s_colorLog = true;
static gboolean s_useTerminal = false;
static gboolean s_forceSoftwareRendering = false;
static gchar* s_mallocStatsFileStr = NULL;
static int s_mallocStatsInterval = -1;

/**
 * Writes malloc statistics to stderr
 * 
 * @param	data			Data of some sort - currently unused
 * 
 * @return				Always returns TRUE
 */
static gboolean mallocStatsCb(gpointer data)
{
	char buf[30];
	time_t cur_time;
	(void) time(&cur_time);
	static pid_t my_pid = 0;
	static char process_name[16] = { 0 };

	if (!my_pid) {
		my_pid = getpid();
	}

	if (!process_name[0]) {
		::prctl(PR_GET_NAME, (unsigned long)process_name, 0, 0, 0);
		process_name[sizeof(process_name) - 1] = '\0';
	}

	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);

    flock(STDERR_FILENO, LOCK_EX);

	(void) fflush(stderr);
	(void) fprintf(stderr, "\nMALLOC STATS FOR PROCESS: \"%s\" (PID: %d) AT [%ld.%ld] %s", process_name, my_pid, ts.tv_sec, ts.tv_nsec, ctime_r(&cur_time, buf));
	(void) fflush(stderr);
	malloc_stats();
	(void) fprintf(stderr, "\n\n");
	(void) fflush(stderr);
	fsync(STDERR_FILENO);

    flock(STDERR_FILENO, LOCK_UN);
	
	
	return TRUE;
}

/**
 * Sets up mallocStatsCb to run at a specified time interval
 * 
 * @param	mainLoop		Pointer to main loop of parent
 * @param	secs			Number of seconds between calls to mallocStatsCb
 */
static void initMallocStatsCb(GMainLoop* mainLoop, int secs)
{
	// negative means no stats
	if ((secs < 0) || (s_mallocStatsFileStr == NULL)) return;

	GSource *timeoutSource = g_timeout_source_new_seconds(secs);
	g_source_set_callback(timeoutSource, mallocStatsCb, NULL, NULL);
	g_source_attach(timeoutSource, g_main_loop_get_context(mainLoop));
	g_source_unref(timeoutSource);
}

/**
 * Attempts to open a file for logging malloc statistics
 * 
 * Logs a critical error message if unable to open the given file.
 * 
 * @param	mallocStatsFile			Filename of file to log malloc statistics to
 */
static void setupMallocStats(const char* mallocStatsFile)
{
	const FILE* file = ::freopen(mallocStatsFile, "a+", stderr);

	if (!file) {
		g_critical("Unable to open file: %s", mallocStatsFile);
	}
	// cppcheck-suppress resourceLeak
}

#if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
/**
 * Calls different message logging functions depending on message type
 * 
 * For the different possible values of type, it calls:
 * - QtDebugMsg = g_debug
 * - QtWarningMsg = g_warning
 * - QtCriticalMsg = g_critical
 * - QtFatalMsg = g_error
 * - Anything else = g_message
 * 
 * @param	type		Message type
 * @param	str		Message to log
 */
void qtMsgHandler(QtMsgType type, const char *str) {
    switch(type)
    {
	case QtDebugMsg:
	    g_debug("QDebug: %s", str);
	    break;
	case QtWarningMsg:
	    g_warning("QWarning: %s", str);
	    break;
	case QtCriticalMsg:
	    g_critical("QCritical: %s", str);
	    break;
	case QtFatalMsg:
	    g_error("QFatal: %s", str);
	    break;
	default:
	    g_message("QMessage: %s", str);
	    break;
    }
}
#else
void qtMsgHandler(QtMsgType type, const QMessageLogContext&, const QString& str) {
    switch(type)
    {
    case QtDebugMsg:
        g_debug("QDebug: %s", qPrintable(str));
        break;
    case QtWarningMsg:
        g_warning("QWarning: %s", qPrintable(str));
        break;
    case QtCriticalMsg:
        g_critical("QCritical: %s", qPrintable(str));
        break;
    case QtFatalMsg:
        g_error("QFatal: %s", qPrintable(str));
        break;
    default:
        g_message("QMessage: %s", qPrintable(str));
        break;
    }
}
#endif

/**
 * Parses command-line options
 *
 * This function parses command-line arguments and sets the corresponding variables
 * in Settings::LunaSettings()
 *
 * @param	argc		Number of arguments
 * @param	argv		Pointer to list of char* pointers for each of the arguments
 */
static void parseCommandlineOptions(int argc, char** argv)
{
    GError* error = NULL;
    GOptionContext* context = NULL;

	static GOptionEntry entries[] = {
		{ "ui",  'u',  0, G_OPTION_ARG_STRING, static_cast<void*>(&s_uiStr), "UI type to launch (minimal, luna)", "name" },
		{ "app", 'a', 0, G_OPTION_ARG_STRING,  static_cast<void*>(&s_appToLaunchStr), "App Id of app to launch", "id" },
		{ "logger", 'l', 0, G_OPTION_ARG_STRING,  static_cast<void*>(&s_logLevelStr), "log level", "level"},
		{ "syslog", 's', 0, G_OPTION_ARG_NONE, &s_useSysLog, "Use syslog", NULL },
		{ "colorlogging", 'c', 0, G_OPTION_ARG_NONE, &s_colorLog, "Color logging on or off", NULL },	// use -c=0 or --colorlogging=0 to disable color logging
		{ "terminal", 't', 0, G_OPTION_ARG_NONE, &s_useTerminal, "Use terminal for logs", NULL },
		{ "force-software-rendering", 'S', 0, G_OPTION_ARG_NONE, &s_forceSoftwareRendering, "Force Software rendering", NULL},
		{ "malloc-stats-file", 'm', 0, G_OPTION_ARG_STRING,  static_cast<void*>(&s_mallocStatsFileStr), "File for logging malloc stats", "file" },
		{ "malloc-stats-interval", 'i', 0, G_OPTION_ARG_INT,  &s_mallocStatsInterval, "Interval at which to log malloc stats", "seconds" },
		{ NULL }
	};

	context = g_option_context_new(NULL);
	g_option_context_add_main_entries (context, entries, NULL);
	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_warning("Failed to parse command line options: %s", error ? error->message : "unknown error");
		if (error)
			g_error_free(error);
	}

#if !defined(HAVE_OPENGL)
	// if there's no OpenGL, then implicitely force software rendering
	s_forceSoftwareRendering = true;
#endif

	if (s_uiStr && strcasecmp(s_uiStr, "minimal") == 0)
		Settings::LunaSettings()->uiType = Settings::UI_MINIMAL;
	else
		Settings::LunaSettings()->uiType = Settings::UI_LUNA;

	Settings::LunaSettings()->logger_useSyslog = s_useSysLog;
	Settings::LunaSettings()->logger_useColor = s_colorLog;
	Settings::LunaSettings()->logger_useTerminal = s_useTerminal;
#if SHIPPING_VERSION
	Settings::LunaSettings()->logger_level = G_LOG_LEVEL_CRITICAL;
#else
	Settings::LunaSettings()->logger_level = G_LOG_LEVEL_DEBUG;
#endif
	Settings::LunaSettings()->forceSoftwareRendering = s_forceSoftwareRendering;
	if (s_forceSoftwareRendering)
		Settings::LunaSettings()->atlasEnabled = false;

    if (s_logLevelStr)
    {
        if (0 == strcasecmp(s_logLevelStr, "error"))
            Settings::LunaSettings()->logger_level = G_LOG_LEVEL_ERROR;
        else if (0 == strcasecmp(s_logLevelStr, "critical"))
            Settings::LunaSettings()->logger_level = G_LOG_LEVEL_CRITICAL;
        else if (0 == strcasecmp(s_logLevelStr, "warning"))
            Settings::LunaSettings()->logger_level = G_LOG_LEVEL_WARNING;
        else if (0 == strcasecmp(s_logLevelStr, "message"))
            Settings::LunaSettings()->logger_level = G_LOG_LEVEL_MESSAGE;
        else if (0 == strcasecmp(s_logLevelStr, "info"))
            Settings::LunaSettings()->logger_level = G_LOG_LEVEL_INFO;
        else if (0 == strcasecmp(s_logLevelStr, "debug"))
            Settings::LunaSettings()->logger_level = G_LOG_LEVEL_DEBUG;
    }

    g_option_context_free(context);
}

/**
 * Crashes the program
 */
static void generateGoodBacktraceTerminateHandler()
{
	volatile int* p = 0;
	// cppcheck-suppress nullPointer
	*p = 0;
	exit(-1);
}

/**
 * Number of arguments Luna was started with
 * 
 * This variable is set in main().
 * 
 * @see main()
 */
int appArgc = 0;

/**
 * Pointer to char* of arguments Luna was started with
 * 
 * This variable is set in main().
 * 
 * @see main()
 */
char** appArgv = 0;

/**
 * Main program entry point
 *
 * This function is the one called by the operating system to start Luna.
 * 
 * This function sets {@link appArgc appArgc} and {@link appArgv appArgv}.
 * 
 * @see appArgc
 * @see appArgv
 *
 * @param	argc		Number of command-line arguments
 * @param	argv		Pointer to list of char* of each of the arguments
 *
 * @return			0 = success, anything else = failure
 */
int main( int argc, char** argv)
{
	appArgc = argc;
	appArgv = argv;

	std::set_terminate(generateGoodBacktraceTerminateHandler);

	g_debug("SysMgr compiled against Qt %s, running on %s", QT_VERSION_STR, qVersion());

	// Command-Line options
	parseCommandlineOptions(argc, argv);

	if (s_mallocStatsFileStr) {
		setupMallocStats(s_mallocStatsFileStr);
	}

	// Load Settings (first!)
	Settings* settings = Settings::LunaSettings();

	// Initialize logging handler
	g_log_set_default_handler(logFilter, NULL);

#if defined(TARGET_DESKTOP)
	// use terminal logging when running on desktop
	settings->logger_useTerminal = true;
#endif

	// disable color logging using an environment variable. Useful when run from QtCreator
	const char* useColor = ::getenv("COLOR_LOGGING");
	if (useColor)
		settings->logger_useColor = (useColor[0] != 0 && useColor[0] != '0');

	HostBase* host = HostBase::instance();
	// the resolution is just a hint, the actual
	// resolution may get picked up from the fb driver on arm
	host->init(settings->displayWidth, settings->displayHeight);

#if 0
	// Tie LunaSysMgr to Processor 0
	setCpuAffinity(getpid(), 1);
#endif

	// Safe to create logging threads now
	logInit();

#if !defined(TARGET_DESKTOP)
	// Set "nice" property
	setpriority(PRIO_PROCESS,getpid(),-1);
#endif

#if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
	qInstallMsgHandler(qtMsgHandler);
#else
	qInstallMessageHandler(qtMsgHandler);
#endif

	QCoreApplication app(argc, argv);

	// We need this to start up services and input controls provided by the host
	// implementation
	host->show();

	initMallocStatsCb(HostBase::instance()->mainLoop(), s_mallocStatsInterval);

	// Initialize Preferences handler
	(void) Preferences::instance();

	(void) LocalePreferences::instance();

	// Initialize Localization handler
	(void) Localization::instance();

	// Initialize the Boot Manager
	BootManager::instance();

	// Initialize the WebAppMgr Proxy
	WebAppMgrProxy::instance();

	// Initialize the application mgr
	ApplicationManager::instance()->init();

	// Initialize the Application Installer
	ApplicationInstaller::instance();

	// Initialize the Event Reporter
	EventReporter::init(host->mainLoop());

	// Initialize the SysMgr MemoryMonitor
	MemoryMonitor::instance()->start();

	app.exec();

	return 0;
}
