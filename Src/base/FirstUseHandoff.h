/* @@@LICENSE
*
* Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
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

#ifndef FIRSTUSEHANDOFF_H
#define FIRSTUSEHANDOFF_H

#include <string>
#include <vector>

/*
 * What first use asked to have opened once the device is set up.
 *
 * Some of what first use offers cannot happen while it runs: the Accounts app
 * needs the profile account, the launcher and a normal session, and a restore
 * ends in a restart that would bring first use back. So first use only records
 * the choice, in a small JSON object, and BootManager acts on it when it enters
 * the normal state:
 *
 *   { "restore": true }    the Backup app, opened on its restore flow
 *   { "accounts": true }   the Accounts app
 *
 * Only those two keys are read, and only a JSON true turns one on. The file
 * cannot name an app or parameters of its own: what it can launch is fixed
 * here. It is read once and removed before anything is launched, so an app
 * that takes the device down is not opened again on every boot.
 */
namespace FirstUseHandoff
{

struct Launch
{
    std::string appId;
    std::string params;
};

//! The file first use writes: /var/luna/preferences/firstuse-handoff.json.
extern const char *const kPath;

//! Where BootManager looks: kPath, unless a test said otherwise.
const char *path();

//! Points path() somewhere else, for the unit tests; NULL goes back to kPath.
void setPathForTesting(const char *path);

//! The most of the file that is read; anything longer is refused.
const long kMaxSize = 4096;

//! Reads the hand-off at path, removes it, and returns what to launch, in
//! order. A missing, unreadable, oversized or malformed file, or anything
//! but a regular file (a symlink is not followed), launches nothing.
std::vector<Launch> take(const char *path);

} // namespace FirstUseHandoff

#endif // FIRSTUSEHANDOFF_H
