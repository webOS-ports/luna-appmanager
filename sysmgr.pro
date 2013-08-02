# @@@LICENSE
#
#      Copyright (c) 2010-2013 LG Electronics, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# LICENSE@@@
TEMPLATE = app

CONFIG += qt

TARGET_TYPE = 

ENV_BUILD_TYPE = $$(BUILD_TYPE)
!isEmpty(ENV_BUILD_TYPE) {
	CONFIG -= release debug
	CONFIG += $$ENV_BUILD_TYPE
}

# Prevent conflict with usage of "signal" in other libraries
CONFIG += no_keywords

CONFIG += link_pkgconfig
PKGCONFIG = glib-2.0 gthread-2.0 LunaSysMgrIpc LunaSysMgrCommon

QT = core gui network
QT += declarative

VPATH = \
    ./Src \
    ./Src/base \
    ./Src/base/application \
    ./Src/base/settings \
    ./Src/core

INCLUDEPATH = $$VPATH

# For shipping version of the code, as opposed to a development build. Set this to 1 late in the process...
DEFINES += SHIPPING_VERSION=0

# Uncomment to compile in trace statements in the code for debugging
# DEFINES += ENABLE_TRACING

# DEFINES += HAVE_CALLGRIND=1

# This allows the use of the % for faster QString concatentation
# See the QString documentation for more information
# DEFINES += QT_USE_FAST_CONCATENATION

# Uncomment this for all QString concatenations using +
# to go through the faster % instead.  Not sure what impact
# this has performance wise or behaviour wise.
# See the QString documentation for more information
# DEFINES += QT_USE_FAST_OPERATOR_PLUS

SOURCES = \
    AmbientLightSensor.cpp \
    AnimationSettings.cpp \
    ApplicationDescription.cpp \
    ApplicationInstaller.cpp \
    ApplicationManager.cpp \
    ApplicationManagerService.cpp \
    ApplicationStatus.cpp \
    BackupManager.cpp \
    CmdResourceHandlers.cpp \
    CpuAffinity.cpp \
    DeviceInfo.cpp \
    DisplayManager.cpp \
    DisplayStates.cpp \
    EASPolicyManager.cpp \
    EventReporter.cpp \
    HapticsController.cpp \
    JSONUtils.cpp \
    KeywordMap.cpp \
    LaunchPoint.cpp \
    Logging.cpp \
    LsmUtils.cpp \
    Main.cpp \
    MallocHooks.cpp \
    MemoryMonitor.cpp \
    MetaKeyManager.cpp \
    MimeSystem.cpp \
    PackageDescription.cpp \
    Preferences.cpp \
    Security.cpp \
    ServiceDescription.cpp \
    Settings.cpp \
    SuspendBlocker.cpp \
    SystemService.cpp

HEADERS = \
    AmbientLightSensor.h \
    AnimationEquations.h \
    AnimationSettings.h \
    ApplicationDescription.h \
    ApplicationInstallerErrors.h \
    ApplicationInstaller.h \
    ApplicationManager.h \
    ApplicationStatus.h \
    BackupManager.h \
    CircularBuffer.h \
    CmdResourceHandlers.h \
    CpuAffinity.h \
    DeviceInfo.h \
    DisplayManager.h \
    DIsplayStates.h \
    EASPolicyManager.h \
    EventReporter.h \
    GraphicsDefs.h \
    HapticsController.h \
    LaunchPoint.h \
    LsmUtils.h \
    MemoryMonitor.h \
    MetaKeyManager.h \
    MimeSystem.h \
    PackageDescription.h \
    Preferences.h \
    PtrArray.h \
    Security.h \
    ServiceDescription.h \
    SharedGlobalProperties.h \
    SuspendBlocker.h \
    SystemService.h

QMAKE_CXXFLAGS += -fno-rtti -fno-exceptions -fvisibility=hidden -fvisibility-inlines-hidden -Wall -fpermissive
QMAKE_CXXFLAGS += -DFIX_FOR_QT
#-DNO_WEBKIT_INIT

# Override the default (-Wall -W) from g++.conf mkspec (see linux-g++.conf)
QMAKE_CXXFLAGS_WARN_ON += -Wno-unused-parameter -Wno-unused-variable -Wno-reorder -Wno-missing-field-initializers -Wno-extra

LIBS += -lcjson -lLunaSysMgrIpc -lluna-service2 -lpbnjson_c -lpbnjson_cpp -lssl -lsqlite3 -lssl -lcrypto -lnyx


linux-g++ {
    include(desktop.pri)
} else:linux-g++-64 {
    include(desktop.pri)
} else:linux-qemux86-g++ {
	include(emulator.pri)
	QMAKE_CXXFLAGS += -fno-strict-aliasing
} else {
    ## First, check to see if this in an emulator build
    include(emulator.pri)
    contains (CONFIG_BUILD, webosemulator) {
        QMAKE_CXXFLAGS += -fno-strict-aliasing
    } else {
        ## Neither a desktop nor an emulator build, so must be a device
        include(device.pri)
    }
}

contains(CONFIG_BUILD, haptics) {
    DEFINES += HAPTICS=1
}

contains(CONFIG_BUILD, nyx) {
    DEFINES += HAS_NYX
}

contains(CONFIG_BUILD, memchute) {
    LIBS += -lmemchute
    DEFINES += HAS_MEMCHUTE
}

DESTDIR = ./$${BUILD_TYPE}-$${MACHINE_NAME}

OBJECTS_DIR = $$DESTDIR/.obj
MOC_DIR = $$DESTDIR/.moc

TARGET = LunaSysMgr

# Comment these out to get verbose output
#QMAKE_CXX = @echo Compiling $(@)...; $$QMAKE_CXX
#QMAKE_LINK = @echo Linking $(@)...; $$QMAKE_LINK
#QMAKE_MOC = @echo Mocing $(@)...; $$QMAKE_MOC
