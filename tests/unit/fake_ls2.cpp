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

#include "fake_ls2.h"

#include <glib.h>
#include <time.h>

#include <list>

// The component is built with -fvisibility=hidden; these have to stay
// visible so that they, not libluna-service2's, are what every caller binds
// to.
#define FAKE_LS2 extern "C" __attribute__((visibility("default")))

struct LSHandle
{
    std::string name;
};

struct LSMessage
{
    std::string payload;
    LSMessageToken token = LSMESSAGE_TOKEN_INVALID;
    int refs = 0;
};

struct LSSubscriptionIter
{
    int unused;
};

namespace
{

struct StatusWatch
{
    LSHandle *handle = nullptr;
    std::string serviceName;
    LSServerStatusFunc func = nullptr;
    void *context = nullptr;
};

std::vector<fakels::Call> g_calls;
// A list, so that the handles stay where they are while more are added.
std::list<LSHandle> g_handles;
std::vector<StatusWatch> g_watches;
LSMessageToken g_nextToken = 1;
bool g_failNextCall = false;
long long g_timeOffset = 0;

void setError(LSError *error, const char *message)
{
    if (!error)
        return;
    error->error_code = -1;
    error->message = g_strdup(message);
    error->file = __FILE__;
    error->line = __LINE__;
    error->func = __func__;
}

bool recordCall(LSHandle *sh, const char *uri, const char *payload, LSFilterFunc callback,
                void *context, LSMessageToken *token, LSError *error)
{
    if (g_failNextCall) {
        g_failNextCall = false;
        setError(error, "fake bus refused the call");
        return false;
    }
    fakels::Call call;
    call.service = sh ? sh->name : std::string();
    call.uri = uri ? uri : "";
    call.payload = payload ? payload : "";
    call.callback = callback;
    call.context = context;
    call.token = g_nextToken++;
    call.cancelled = false;
    g_calls.push_back(call);
    if (token)
        *token = call.token;
    return true;
}

} // namespace

namespace fakels
{

const std::vector<Call>& calls()
{
    return g_calls;
}

std::vector<Call> callsTo(const std::string &uriSuffix)
{
    std::vector<Call> found;
    for (size_t i = 0; i < g_calls.size(); ++i) {
        const std::string &uri = g_calls[i].uri;
        if (uri.size() >= uriSuffix.size() &&
            uri.compare(uri.size() - uriSuffix.size(), uriSuffix.size(), uriSuffix) == 0)
            found.push_back(g_calls[i]);
    }
    return found;
}

void clearCalls()
{
    g_calls.clear();
}

void failNextCall()
{
    g_failNextCall = true;
}

bool reply(const Call &call, const std::string &payload)
{
    if (!call.callback || call.cancelled)
        return false;
    LSMessage message;
    message.payload = payload;
    message.token = call.token;
    message.refs = 1;
    LSHandle handle;
    handle.name = call.service;
    return call.callback(&handle, &message, call.context);
}

int serverStatus(const std::string &serviceName, bool connected)
{
    // Copied: a watcher may register more watches while it is told.
    const std::vector<StatusWatch> watches = g_watches;
    int told = 0;
    for (size_t i = 0; i < watches.size(); ++i) {
        if (watches[i].serviceName != serviceName)
            continue;
        watches[i].func(watches[i].handle, serviceName.c_str(), connected, watches[i].context);
        ++told;
    }
    return told;
}

} // namespace fakels

void advanceMonotonicTime(long long microseconds)
{
    g_timeOffset += microseconds;
}

// What the component reads the time from to rate-limit relaunches.
FAKE_LS2 gint64 g_get_monotonic_time(void)
{
    struct timespec ts = { 0, 0 };
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (gint64)ts.tv_sec * 1000000 + ts.tv_nsec / 1000 + g_timeOffset;
}

FAKE_LS2 bool LSErrorInit(LSError *error)
{
    if (!error)
        return false;
    error->error_code = 0;
    error->message = NULL;
    error->file = NULL;
    error->line = 0;
    error->func = NULL;
    error->padding = NULL;
    error->magic = 0;
    return true;
}

FAKE_LS2 void LSErrorFree(LSError *error)
{
    if (!error)
        return;
    g_free(error->message);
    LSErrorInit(error);
}

FAKE_LS2 bool LSErrorIsSet(LSError *error)
{
    return error && error->error_code != 0;
}

FAKE_LS2 void LSErrorPrint(LSError *error, FILE *out)
{
    if (error && out)
        fprintf(out, "LSError %d: %s\n", error->error_code, error->message ? error->message : "");
}

FAKE_LS2 bool LSRegister(const char *name, LSHandle **sh, LSError *error)
{
    LSHandle handle;
    handle.name = name ? name : "";
    g_handles.push_back(handle);
    *sh = &g_handles.back();
    return true;
}

FAKE_LS2 bool LSUnregister(LSHandle *service, LSError *error)
{
    return service != NULL;
}

FAKE_LS2 bool LSRegisterCategory(LSHandle *sh, const char *category, LSMethod *methods,
                                 LSSignal *signals, LSProperty *properties, LSError *error)
{
    return sh != NULL;
}

FAKE_LS2 bool LSGmainAttach(LSHandle *sh, GMainLoop *mainLoop, LSError *error)
{
    return sh != NULL;
}

FAKE_LS2 bool LSRegisterServerStatusEx(LSHandle *sh, const char *serviceName,
                                       LSServerStatusFunc func, void *ctxt,
                                       void **cookie, LSError *error)
{
    StatusWatch watch;
    watch.handle = sh;
    watch.serviceName = serviceName ? serviceName : "";
    watch.func = func;
    watch.context = ctxt;
    g_watches.push_back(watch);
    if (cookie)
        *cookie = NULL;
    return true;
}

FAKE_LS2 bool LSCall(LSHandle *sh, const char *uri, const char *payload, LSFilterFunc callback,
                     void *user_data, LSMessageToken *ret_token, LSError *error)
{
    return recordCall(sh, uri, payload, callback, user_data, ret_token, error);
}

FAKE_LS2 bool LSCallOneReply(LSHandle *sh, const char *uri, const char *payload,
                             LSFilterFunc callback, void *ctx, LSMessageToken *ret_token,
                             LSError *error)
{
    return recordCall(sh, uri, payload, callback, ctx, ret_token, error);
}

FAKE_LS2 bool LSCallCancel(LSHandle *sh, LSMessageToken token, LSError *error)
{
    for (size_t i = 0; i < g_calls.size(); ++i) {
        if (g_calls[i].token == token)
            g_calls[i].cancelled = true;
    }
    return true;
}

FAKE_LS2 bool LSCallSetTimeout(LSHandle *sh, LSMessageToken token, int timeout_ms, LSError *error)
{
    return true;
}

FAKE_LS2 const char *LSMessageGetPayload(LSMessage *message)
{
    return message ? message->payload.c_str() : NULL;
}

FAKE_LS2 const char *LSMessageGetApplicationID(LSMessage *message)
{
    return NULL;
}

FAKE_LS2 LSMessageToken LSMessageGetResponseToken(LSMessage *reply)
{
    return reply ? reply->token : LSMESSAGE_TOKEN_INVALID;
}

FAKE_LS2 bool LSMessageIsSubscription(LSMessage *message)
{
    return false;
}

FAKE_LS2 bool LSMessageGetSubscribe(LSMessage *message)
{
    return false;
}

FAKE_LS2 void LSMessageRef(LSMessage *message)
{
    if (message)
        ++message->refs;
}

FAKE_LS2 void LSMessageUnref(LSMessage *message)
{
    if (message)
        --message->refs;
}

FAKE_LS2 bool LSMessageReply(LSHandle *sh, LSMessage *message, const char *replyPayload,
                             LSError *error)
{
    return true;
}

FAKE_LS2 bool LSSubscriptionProcess(LSHandle *sh, LSMessage *message, bool *subscribed,
                                    LSError *error)
{
    if (subscribed)
        *subscribed = false;
    return true;
}

FAKE_LS2 bool LSSubscriptionAdd(LSHandle *sh, const char *key, LSMessage *message,
                                LSError *error)
{
    return true;
}

FAKE_LS2 bool LSSubscriptionReply(LSHandle *sh, const char *key, const char *payload,
                                  LSError *error)
{
    return true;
}

FAKE_LS2 bool LSSubscriptionPost(LSHandle *sh, const char *category, const char *method,
                                 const char *payload, LSError *error)
{
    return true;
}

FAKE_LS2 bool LSSubscriptionAcquire(LSHandle *sh, const char *key, LSSubscriptionIter **ret_iter,
                                    LSError *error)
{
    static LSSubscriptionIter iter;
    if (ret_iter)
        *ret_iter = &iter;
    return true;
}

FAKE_LS2 void LSSubscriptionRelease(LSSubscriptionIter *iter)
{
}

FAKE_LS2 bool LSSubscriptionHasNext(LSSubscriptionIter *iter)
{
    return false;
}

FAKE_LS2 LSMessage *LSSubscriptionNext(LSSubscriptionIter *iter)
{
    return NULL;
}
