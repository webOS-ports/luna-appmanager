#!/bin/sh
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
# Run the unit tests under valgrind, using an OE recipe workdir's x86-64
# sysroot so the exact libraries from the image are what gets exercised.
#
#   tests/hardening/run-valgrind.sh <build-dir> <recipe-sysroot>
#
# where <build-dir> is a cmake build configured with -DBUILD_TESTING=ON for
# corei7-64 and <recipe-sysroot> is that workdir's recipe-sysroot. Fails on
# any memory error or definite leak.

set -eu

BUILD="${1:?build dir}"
SYSROOT="${2:?recipe sysroot}"

exec valgrind \
    --error-exitcode=42 \
    --leak-check=full \
    --show-leak-kinds=definite \
    --errors-for-leak-kinds=definite \
    "$SYSROOT/usr/lib/ld-linux-x86-64.so.2" \
    --library-path "$SYSROOT/usr/lib" \
    "$BUILD/tests/luna-appmanager-tests"
