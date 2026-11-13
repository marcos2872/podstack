// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"
#include "machines/MachineMeta.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <mutex>

namespace PodStack {

/// App exposto pelo catálogo do guest (Linux `.desktop` ou Windows Start Menu).
struct GuestApp {
    QString appId;
    QString name;
    QString icon; // conteúdo em base64 (png/svg/ico), vazio quando não resolvido
    QStringList categories;
    QString desktopFile;
    QString exec;

    QJsonObject toJson(int runningSessions = 0) const;
};

/// Catálogo de apps do guest com cache por machine (TTL de 5 min).
class GuestAppCatalog {
public:
    static constexpr int kCacheTtlMs = 5 * 60 * 1000;

    QJsonArray list(const MachineMeta &machine, bool refresh, Error *error = nullptr);
    void invalidate(const QString &machineId);
    QDateTime cachedAt(const QString &machineId) const;

private:
    struct Cache {
        QJsonArray items;
        QDateTime cachedAt;
        QString signature;
    };

    mutable std::mutex m_mutex;
    QHash<QString, Cache> m_cache;
};

} // namespace PodStack
