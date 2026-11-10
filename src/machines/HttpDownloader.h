// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"

#include <QString>

#include <functional>

namespace PodStack {

using DownloadProgress = std::function<void(qint64 received, qint64 total)>;
using DownloadCancelled = std::function<bool()>;

/// Download HTTP/HTTPS síncrono para arquivo, com `.part` e rename atômico.
class HttpDownloader {
public:
    static bool download(const QString &url, const QString &destination,
                         const DownloadProgress &progress, const DownloadCancelled &cancelled,
                         Error *error);
};

} // namespace PodStack
