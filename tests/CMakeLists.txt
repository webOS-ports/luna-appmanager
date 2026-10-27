# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
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
# SPDX-License-Identifier: Apache-2.0
#
# Unit tests for luna-appmanager. They build the component's own sources
# against a stand-in for luna-service2 (unit/fake_ls2.cpp), so they run
# without a bus: on the target, or on the build host against the corei7-64
# recipe sysroot.
#
# Off by default so ordinary image builds are unaffected:
#   cmake -DBUILD_TESTING=ON ...

file(GLOB TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp)

# The component's own sources, minus the daemon entry point.
set(COMPONENT_SOURCES)
foreach(source ${SOURCES})
    list(APPEND COMPONENT_SOURCES ${PROJECT_SOURCE_DIR}/${source})
endforeach()
list(REMOVE_ITEM COMPONENT_SOURCES ${PROJECT_SOURCE_DIR}/Src/Main.cpp)

add_executable(luna-appmanager-tests ${TEST_SOURCES} ${COMPONENT_SOURCES})

target_include_directories(luna-appmanager-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)

# unit/fake_ls2.cpp defines the luna-service2 calls the component makes, and
# the executable's definitions are the ones every caller binds to. The library
# is still linked for what libserviceinstaller uses besides them, after it -
# with --as-needed nothing earlier would keep it.
target_link_libraries(luna-appmanager-tests
    ${GLIB2_LIBRARIES}
    ${GTHREAD2_LIBRARIES}
    ${LUNA_SYSMGR_COMMON_LIBRARIES}
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
    ${LS2_LIBRARIES}
    Qt::Core Qt::Gui Qt::Widgets
    pthread)

add_test(NAME luna-appmanager-unit COMMAND luna-appmanager-tests)
