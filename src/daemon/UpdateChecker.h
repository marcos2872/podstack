// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"

#include <QJsonObject>
#include <QString>

#include <mutex>

namespace PodStack {

/// Verifica o manifesto de updates assinado. Nunca baixa nem instala
/// artefatos: apenas compara a versão e devolve os metadados.
class UpdateChecker {
public:
    QJsonObject check(const QString &requestedChannel, bool force, Error *error);

    void clearCache();

    static constexpr qint64 kCacheTtlMs = 6LL * 60 * 60 * 1000;

private:
    struct FetchResult {
        long status = 0;
        QByteArray body;
        QString error;
    };

    static FetchResult fetch(const QString &url, int timeoutMs, int maxBytes);

    std::mutex m_mutex;
    QJsonObject m_cached;
    qint64 m_cachedAtMs = 0;
};

} // namespace PodStack
