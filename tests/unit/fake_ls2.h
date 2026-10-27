// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef __LUNA_APPMANAGER_TEST_FAKE_LS2_H__
#define __LUNA_APPMANAGER_TEST_FAKE_LS2_H__

/*
 * A stand-in for the luna-service2 calls the component makes, so that its
 * real sources run without a bus. Every call is recorded; a test answers a
 * call with reply(), and plays a service coming or going with
 * serverStatus(), which is how the component would hear of it from the bus.
 */

#include <luna-service2/lunaservice.h>

#include <string>
#include <vector>

namespace fakels
{

struct Call
{
    std::string service;   //!< name the calling handle registered as
    std::string uri;
    std::string payload;
    LSFilterFunc callback;
    void *context;
    LSMessageToken token;
    bool cancelled;
};

//! Every LSCall and LSCallOneReply so far, oldest first.
const std::vector<Call>& calls();

//! Calls whose uri ends with uriSuffix, oldest first.
std::vector<Call> callsTo(const std::string &uriSuffix);

void clearCalls();

//! The next LSCall or LSCallOneReply fails, as when the bus refuses it.
void failNextCall();

//! Hands payload to the call's callback, as a reply from the bus would.
bool reply(const Call &call, const std::string &payload);

//! Tells whoever watches serviceName (LSRegisterServerStatusEx) that it
//! connected or went away. Returns how many watchers there were.
int serverStatus(const std::string &serviceName, bool connected);

} // namespace fakels

//! Monotonic time for the component, in microseconds: the real clock plus
//! whatever the test moved it on by.
void advanceMonotonicTime(long long microseconds);

#endif
