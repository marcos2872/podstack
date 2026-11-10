// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"
#include "k8s/K8sCluster.h"

#include <QString>
#include <QVector>

#include <mutex>

struct sqlite3;

namespace PodStack {

/// Índice SQLite dos clusters Kubernetes (mesmo `podstack.db` do BuildStore/MachineStore).
class K8sStore {
public:
    explicit K8sStore(QString path = {});
    ~K8sStore();

    K8sStore(const K8sStore &) = delete;
    K8sStore &operator=(const K8sStore &) = delete;

    bool open(Error *error = nullptr);
    bool upsert(const K8sCluster &cluster, Error *error = nullptr);
    K8sCluster get(const QString &name, Error *error = nullptr) const;
    QVector<K8sCluster> list(Error *error = nullptr) const;
    bool remove(const QString &name, Error *error = nullptr);

    QString path() const { return m_path; }

private:
    bool ensureSchema(Error *error);
    bool fail(const QString &fallback, Error *error) const;

    sqlite3 *m_db = nullptr;
    QString m_path;
    mutable std::recursive_mutex m_mutex;
};

} // namespace PodStack
