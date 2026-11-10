// SPDX-License-Identifier: GPL-3.0-or-later

#include "k8s/K8sStore.h"

#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <sqlite3.h>

#include <memory>
#include <utility>

namespace PodStack {
namespace {

using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

void bindText(sqlite3_stmt *statement, int index, const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    sqlite3_bind_text(statement, index, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
}

QString columnText(sqlite3_stmt *statement, int column)
{
    const unsigned char *text = sqlite3_column_text(statement, column);
    return text ? QString::fromUtf8(reinterpret_cast<const char *>(text)) : QString();
}

bool execSql(sqlite3 *db, const char *sql, QString *error)
{
    char *message = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &message) != SQLITE_OK) {
        if (error) {
            *error = QString::fromUtf8(message ? message : "erro SQLite");
        }
        sqlite3_free(message);
        return false;
    }
    return true;
}

Statement prepare(sqlite3 *db, const char *sql, bool *ok)
{
    sqlite3_stmt *statement = nullptr;
    *ok = sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK;
    return Statement(statement, sqlite3_finalize);
}

K8sCluster clusterFromRow(sqlite3_stmt *statement)
{
    K8sCluster cluster;
    cluster.id = columnText(statement, 0);
    cluster.name = columnText(statement, 1);
    cluster.provider = columnText(statement, 2);
    cluster.state = columnText(statement, 3);
    cluster.apiPort = sqlite3_column_int(statement, 4);
    cluster.kubeconfigPath = columnText(statement, 5);
    cluster.contextName = columnText(statement, 6);
    cluster.vmMachineId = columnText(statement, 7);
    cluster.createdAt = columnText(statement, 8);
    cluster.updatedAt = columnText(statement, 9);
    cluster.error = columnText(statement, 10);
    return cluster;
}

} // namespace

K8sStore::K8sStore(QString path) : m_path(std::move(path))
{
    if (m_path.isEmpty()) {
        m_path = Paths::dataDir() + QStringLiteral("/podstack.db");
    }
}

K8sStore::~K8sStore()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool K8sStore::fail(const QString &fallback, Error *error) const
{
    if (error) {
        *error = Error::make(QStringLiteral("PODSTACK_DB_ERROR"), fallback,
                             m_db ? QString::fromUtf8(sqlite3_errmsg(m_db)) : QString());
    }
    return false;
}

bool K8sStore::open(Error *error)
{
    if (error) {
        *error = {};
    }

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_db) {
        return true;
    }

    const QFileInfo info(m_path);
    const QString parent = info.absolutePath();
    const QFileInfo parentInfo(parent);
    if (!parentInfo.exists()) {
        if (!QDir().mkpath(parent)) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_DB_ERROR"),
                                     QStringLiteral("Não foi possível criar o diretório do banco."),
                                     parent);
            }
            return false;
        }
        if (!QFile::setPermissions(parent, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                               QFileDevice::ExeOwner)) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_DB_ERROR"),
                                     QStringLiteral("Não foi possível restringir o diretório do "
                                                    "banco a 0700."),
                                     parent);
            }
            return false;
        }
    }

    if (sqlite3_open_v2(m_path.toUtf8().constData(), &m_db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        const QString detail = m_db ? QString::fromUtf8(sqlite3_errmsg(m_db)) : QString();
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        if (error) {
            *error =
                Error::make(QStringLiteral("PODSTACK_DB_ERROR"),
                            QStringLiteral("Não foi possível abrir o banco de clusters."), detail);
        }
        return false;
    }

    sqlite3_busy_timeout(m_db, 5000);
    QString sqlError;
    if (!execSql(m_db, "PRAGMA journal_mode=WAL;", &sqlError) ||
        !execSql(m_db, "PRAGMA foreign_keys=ON;", &sqlError)) {
        const QString &detail = sqlError;
        sqlite3_close(m_db);
        m_db = nullptr;
        if (error) {
            *error =
                Error::make(QStringLiteral("PODSTACK_DB_ERROR"),
                            QStringLiteral("Falha ao configurar o banco de clusters."), detail);
        }
        return false;
    }

    if (!ensureSchema(error)) {
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }

    if (!QFile::setPermissions(m_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        sqlite3_close(m_db);
        m_db = nullptr;
        if (error) {
            *error =
                Error::make(QStringLiteral("PODSTACK_DB_ERROR"),
                            QStringLiteral("Não foi possível restringir o banco a 0600."), m_path);
        }
        return false;
    }
    for (const QString &file :
         {m_path, m_path + QStringLiteral("-wal"), m_path + QStringLiteral("-shm")}) {
        if (QFileInfo::exists(file)) {
            QFile::setPermissions(file, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        }
    }
    return true;
}

bool K8sStore::ensureSchema(Error *error)
{
    const char *schema = R"SQL(
CREATE TABLE IF NOT EXISTS clusters (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL UNIQUE,
  provider TEXT NOT NULL,
  state TEXT NOT NULL,
  api_port INTEGER,
  kubeconfig_path TEXT,
  context_name TEXT,
  vm_machine_id TEXT,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  error TEXT
);
CREATE INDEX IF NOT EXISTS idx_clusters_state ON clusters(state);
)SQL";

    QString sqlError;
    if (!execSql(m_db, schema, &sqlError)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_DB_ERROR"),
                                 QStringLiteral("Falha ao criar o schema de clusters."), sqlError);
        }
        return false;
    }
    return true;
}

bool K8sStore::upsert(const K8sCluster &cluster, Error *error)
{
    if (error) {
        *error = {};
    }
    if (cluster.id.isEmpty() || cluster.name.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("O cluster precisa de id e nome."));
        }
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!open(error)) {
        return false;
    }

    bool ok = false;
    Statement statement =
        prepare(m_db,
                "INSERT INTO clusters (id, name, provider, state, api_port, kubeconfig_path, "
                "context_name, vm_machine_id, created_at, updated_at, error) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                "ON CONFLICT(id) DO UPDATE SET name=excluded.name, provider=excluded.provider, "
                "state=excluded.state, api_port=excluded.api_port, "
                "kubeconfig_path=excluded.kubeconfig_path, context_name=excluded.context_name, "
                "vm_machine_id=excluded.vm_machine_id, created_at=excluded.created_at, "
                "updated_at=excluded.updated_at, error=excluded.error;",
                &ok);
    if (!ok || !statement) {
        return fail(QStringLiteral("Não foi possível preparar o upsert do cluster."), error);
    }

    bindText(statement.get(), 1, cluster.id);
    bindText(statement.get(), 2, cluster.name);
    bindText(statement.get(), 3, cluster.provider);
    bindText(statement.get(), 4, cluster.state);
    sqlite3_bind_int(statement.get(), 5, cluster.apiPort);
    bindText(statement.get(), 6, cluster.kubeconfigPath);
    bindText(statement.get(), 7, cluster.contextName);
    bindText(statement.get(), 8, cluster.vmMachineId);
    bindText(statement.get(), 9, cluster.createdAt);
    bindText(statement.get(), 10, cluster.updatedAt);
    bindText(statement.get(), 11, cluster.error);

    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        return fail(QStringLiteral("Não foi possível salvar o cluster."), error);
    }
    return true;
}

K8sCluster K8sStore::get(const QString &name, Error *error) const
{
    if (error) {
        *error = {};
    }

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db && !const_cast<K8sStore *>(this)->open(error)) {
        return {};
    }

    bool ok = false;
    Statement statement =
        prepare(m_db,
                "SELECT id, name, provider, state, api_port, kubeconfig_path, context_name, "
                "vm_machine_id, created_at, updated_at, error FROM clusters WHERE name = ?;",
                &ok);
    if (!ok || !statement) {
        fail(QStringLiteral("Não foi possível consultar o cluster."), error);
        return {};
    }
    bindText(statement.get(), 1, name);
    const int step = sqlite3_step(statement.get());
    if (step == SQLITE_DONE) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Cluster Kubernetes não encontrado."), name);
        }
        return {};
    }
    if (step != SQLITE_ROW) {
        fail(QStringLiteral("Não foi possível consultar o cluster."), error);
        return {};
    }
    return clusterFromRow(statement.get());
}

QVector<K8sCluster> K8sStore::list(Error *error) const
{
    if (error) {
        *error = {};
    }

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_db && !const_cast<K8sStore *>(this)->open(error)) {
        return {};
    }

    bool ok = false;
    Statement statement =
        prepare(m_db,
                "SELECT id, name, provider, state, api_port, kubeconfig_path, context_name, "
                "vm_machine_id, created_at, updated_at, error FROM clusters ORDER BY name;",
                &ok);
    if (!ok || !statement) {
        fail(QStringLiteral("Não foi possível listar os clusters."), error);
        return {};
    }

    QVector<K8sCluster> clusters;
    int step = SQLITE_ROW;
    while ((step = sqlite3_step(statement.get())) == SQLITE_ROW) {
        clusters.append(clusterFromRow(statement.get()));
    }
    if (step != SQLITE_DONE) {
        fail(QStringLiteral("Não foi possível listar os clusters."), error);
        return {};
    }
    return clusters;
}

bool K8sStore::remove(const QString &name, Error *error)
{
    if (error) {
        *error = {};
    }

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!open(error)) {
        return false;
    }

    bool ok = false;
    Statement statement = prepare(m_db, "DELETE FROM clusters WHERE name = ?;", &ok);
    if (!ok || !statement) {
        return fail(QStringLiteral("Não foi possível remover o cluster."), error);
    }
    bindText(statement.get(), 1, name);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        return fail(QStringLiteral("Não foi possível remover o cluster."), error);
    }
    return true;
}

} // namespace PodStack
