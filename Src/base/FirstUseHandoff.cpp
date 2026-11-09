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

#include "FirstUseHandoff.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <glib.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace FirstUseHandoff
{

const char *const kPath = "/var/luna/preferences/firstuse-handoff.json";

namespace
{

std::string g_testPath;

//! Up to kMaxSize + 1 bytes of the regular file at path, so an oversized one
//! shows as such. Not through a symlink, and without blocking on a FIFO.
//! False when there is nothing usable there.
bool readRegularFile(const char *path, QByteArray &out)
{
    const int fd = ::open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (errno != ENOENT)
            g_warning("First use hand-off %s: not opened: %s", path, strerror(errno));
        return false;
    }

    struct stat st;
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        g_warning("First use hand-off %s: not a regular file, ignored", path);
        ::close(fd);
        return false;
    }

    char buf[kMaxSize + 1];
    size_t total = 0;
    while (total < sizeof(buf)) {
        const ssize_t n = ::read(fd, buf + total, sizeof(buf) - total);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        total += static_cast<size_t>(n);
    }
    ::close(fd);
    out = QByteArray(buf, static_cast<qsizetype>(total));
    return true;
}

bool isTrue(const QJsonObject &object, const char *key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    return value.isBool() && value.toBool();
}

} // namespace

const char *path()
{
    return g_testPath.empty() ? kPath : g_testPath.c_str();
}

void setPathForTesting(const char *path)
{
    g_testPath = path ? path : "";
}

std::vector<Launch> take(const char *path)
{
    std::vector<Launch> launches;

    QByteArray data;
    const bool found = readRegularFile(path, data);

    // Removed whatever it held, before anything is launched: one shot. unlink
    // takes a symlink itself away, not what it points to.
    if (::unlink(path) != 0 && errno != ENOENT)
        g_warning("First use hand-off %s: not removed: %s", path, strerror(errno));

    if (!found)
        return launches;

    if (data.size() > kMaxSize) {
        g_warning("First use hand-off %s: longer than %ld bytes, ignored", path, kMaxSize);
        return launches;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        g_warning("First use hand-off %s: not a JSON object, ignored", path);
        return launches;
    }

    const QJsonObject object = document.object();
    if (isTrue(object, "restore"))
        launches.push_back(Launch{"com.palm.app.backup", "{\"restore\":true}"});
    if (isTrue(object, "accounts"))
        launches.push_back(Launch{"com.palm.app.accounts", "{}"});

    return launches;
}

} // namespace FirstUseHandoff
