project(LunaAppManager)

cmake_minimum_required(VERSION 2.8.8)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC TRUE)

include(webOS/webOS)
webos_modules_init(1 0 0 QUALIFIER RC3)
webos_component(1 0 0)

webos_add_compiler_flags(ALL -fno-rtti -fno-exceptions -fvisibility=hidden -fvisibility-inlines-hidden -Wall -fpermissive)
webos_add_compiler_flags(ALL -Wno-unused-parameter -Wno-unused-variable -Wno-reorder -Wno-missing-field-initializers -Wno-extra)
webos_add_compiler_flags(ALL -DFIX_FOR_QT)
webos_add_compiler_flags(ALL -DHAS_NYX)
webos_add_compiler_flags(ALL -DQT_NO_KEYWORDS)
webos_add_compiler_flags(ALL -DHAS_LUNA_PREF)
webos_add_compiler_flags(ALL -DHAS_DISPLAY_TIMEOUT)
webos_add_compiler_flags(ALL -DSECURITY_COMPATIBILITY)

find_package(Qt6 COMPONENTS Core Gui Widgets)
if (NOT Qt6_FOUND)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets)
endif()

find_package(PkgConfig "0.22" REQUIRED)

pkg_check_modules(GLIB2 glib-2.0 REQUIRED)
pkg_check_modules(GTHREAD2 gthread-2.0 REQUIRED)
pkg_check_modules(LS2 luna-service2 REQUIRED)
pkg_check_modules(LUNA_SYSMGR_COMMON LunaSysMgrCommon REQUIRED)
pkg_check_modules(LUNA_SYSMGR_IPC LunaSysMgrIpc REQUIRED)
pkg_check_modules(LUNA_SYSMGR_IPC_MESSAGES LunaSysMgrIpcMessages REQUIRED)
pkg_check_modules(LUNA_PREFS luna-prefs REQUIRED)
pkg_check_modules(NYX nyx REQUIRED)
pkg_check_modules(PBNJSON_CPP pbnjson_cpp REQUIRED)
pkg_check_modules(PBNJSON_C pbnjson_c REQUIRED)
pkg_check_modules(JSON json-c REQUIRED)
pkg_check_modules(SQLITE3 sqlite3 REQUIRED)
pkg_check_modules(OPENSSL openssl REQUIRED)
pkg_check_modules(SERVICEINSTALLER serviceinstaller REQUIRED)
pkg_check_modules(PMLOGLIB PmLogLib REQUIRED)
pkg_check_modules(ROLEGEN rolegen REQUIRED)

include_directories(
    ${GLIB2_INCLUDE_DIRS}
    ${GTHREAD2_INCLUDE_DIRS}
    ${LS2_INCLUDE_DIRS}
    ${LUNA_SYSMGR_COMMON_INCLUDE_DIRS}
    ${LUNA_SYSMGR_IPC_INCLUDE_DIRS}
    ${LUNA_SYSMGR_IPC_MESSAGES_INCLUDE_DIRS}
    ${LUNA_PREFS_INCLUDE_DIRS}
    ${NYX_INCLUDE_DIRS}
    ${PBNJSON_CPP_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIRS}
    ${JSON_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIRS}
    ${SERVICEINSTALLER_INCLUDE_DIRS}
    ${PMLOGLIB_INCLUDE_DIRS}
    ${ROLEGEN_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/Src/base
    ${CMAKE_SOURCE_DIR}/Src/base/application
    ${CMAKE_SOURCE_DIR}/Src/base/settings
    ${CMAKE_SOURCE_DIR}/Src/core
    ${CMAKE_SOURCE_DIR}/Src/remote)

set(HEADERS
    Src/base/LsmUtils.h
    Src/base/MemoryMonitor.h
    Src/base/BootManager.h
    Src/base/application/ServiceDescription.h
    Src/base/application/ApplicationStatus.h
    Src/base/application/ApplicationInstaller.h
    Src/base/application/PackageDescription.h
    Src/base/application/CmdResourceHandlers.h
    Src/base/application/ApplicationManager.h
    Src/base/application/MimeSystem.h
    Src/base/application/LaunchPoint.h
    Src/base/application/ApplicationDescription.h
    Src/base/application/ApplicationInstallerErrors.h
    Src/core/GraphicsDefs.h
    Src/remote/ApplicationProcessManager.h
    Src/remote/WebAppMgrProxy.h)

set(SOURCES
    Src/base/EventReporter.cpp
    Src/base/MemoryMonitor.cpp
    Src/base/LsmUtils.cpp
    Src/base/BootManager.cpp
    Src/base/application/ApplicationDescription.cpp
    Src/base/application/MimeSystem.cpp
    Src/base/application/PackageDescription.cpp
    Src/base/application/ApplicationInstaller.cpp
    Src/base/application/CmdResourceHandlers.cpp
    Src/base/application/ServiceDescription.cpp
    Src/base/application/ApplicationManager.cpp
    Src/base/application/ApplicationStatus.cpp
    Src/base/application/LaunchPoint.cpp
    Src/base/application/ApplicationManagerService.cpp
    Src/remote/ApplicationProcessManager.cpp
    Src/remote/WebAppMgrProxy.cpp
    Src/Main.cpp)

add_executable(LunaAppManager ${SOURCES})
target_link_libraries(LunaAppManager
    ${LS2_LIBRARIES}
    ${GLIB2_LIBRARIES}
    ${GTHREAD2_LIBRARIES}
    ${LUNA_SYSMGR_COMMON_LIBRARIES}
    ${LUNA_SYSMGR_IPC_LIBRARIES}
    ${LUNA_PREFS_LIBRARIES}
    ${NYX_LIBRARIES}
    ${PBNJSON_C_LIBRARIES}
    ${PBNJSON_CPP_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    ${JSON_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${SERVICEINSTALLER_LIBRARIES}
    ${PMLOGLIB_LIBRARIES}
    ${ROLEGEN_LIBRARIES}
    Qt::Core Qt::Gui Qt::Widgets
    pthread)

webos_build_daemon()
webos_build_system_bus_files()
webos_config_build_doxygen(doc Doxyfile)

set(HELPER_SCRIPTS
    scripts/app-install)

install(FILES ${HELPER_SCRIPTS}
     DESTINATION ${WEBOS_INSTALL_SBINDIR}
     PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
                 GROUP_READ WORLD_READ)
