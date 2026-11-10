// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"
#include "machines/MachineMeta.h"

#include <QString>
#include <QVector>

#include <mutex>

struct sqlite3;

namespace PodStack {

/// Índice SQLite das machines (mesmo `podstack.db` do BuildStore, conexão própria).
class MachineStore {
public:
    explicit MachineStore(QString path = {});
    ~MachineStore();

    MachineStore(const MachineStore &) = delete;
    MachineStore &operator=(const MachineStore &) = delete;

    bool open(Error *error = nullptr);
    bool upsert(const MachineMeta &meta, Error *error = nullptr);
    bool updateState(const QString &id, const QString &state, const QString &errorText,
                     Error *error = nullptr);
    bool remove(const QString &id, Error *error = nullptr);
    MachineMeta get(const QString &id, Error *error = nullptr) const;
    MachineMeta getByName(const QString &name, Error *error = nullptr) const;
    QVector<MachineMeta> list(Error *error = nullptr) const;

    QString path() const { return m_path; }

private:
    bool ensureSchema(Error *error);
    bool loadRelations(MachineMeta &meta, Error *error) const;
    bool fail(const QString &fallback, Error *error) const;

    sqlite3 *m_db = nullptr;
    QString m_path;
    mutable std::recursive_mutex m_mutex;
};

} // namespace PodStack
