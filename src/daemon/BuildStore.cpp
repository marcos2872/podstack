#include "daemon/BuildStore.h"

#include "core/Paths.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include <sqlite3.h>

#include <utility>

namespace PodStack {
namespace {

void bindText(sqlite3_stmt *statement, int index, const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    sqlite3_bind_text(statement, index, utf8.constData(), utf8.size(),
                      SQLITE_TRANSIENT);
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

QString encodePlatforms(const QStringList &platforms)
{
    QJsonArray array;
    for (const QString &platform : platforms) {
        array.append(platform);
    }
    return QString::fromUtf8(
        QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QStringList decodePlatforms(const QString &text)
{
    QStringList platforms;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    if (!document.isArray()) {
        return platforms;
    }
    for (const QJsonValue &value : document.array()) {
        const QString platform = value.toString().trimmed();
        if (!platform.isEmpty()) {
            platforms.append(platform);
        }
    }
    return platforms;
}

bool hasColumn(sqlite3 *db, const char *table, const char *column)
{
    const QByteArray sql = QByteArrayLiteral("PRAGMA table_info(") + table + ')';
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db, sql.constData(), -1, &statement, nullptr) != SQLITE_OK) {
        return false;
    }
    bool found = false;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        if (columnText(statement, 1) == QLatin1String(column)) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(statement);
    return found;
}

} // namespace

QJsonObject BuildStepRecord::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("buildId"), buildId);
    object.insert(QStringLiteral("index"), index);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("status"), status);
    object.insert(QStringLiteral("startedAt"), startedAt);
    object.insert(QStringLiteral("finishedAt"), finishedAt);
    object.insert(QStringLiteral("logPath"), logPath);
    return object;
}

QJsonObject BuildRecord::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("imageRef"), imageRef);
    object.insert(QStringLiteral("status"), status);
    object.insert(QStringLiteral("startedAt"), startedAt);
    object.insert(QStringLiteral("finishedAt"), finishedAt);
    object.insert(QStringLiteral("durationMs"), durationMs);
    object.insert(QStringLiteral("trigger"), trigger);
    object.insert(QStringLiteral("contextPath"), contextPath);
    object.insert(QStringLiteral("dockerfile"), dockerfile);
    object.insert(QStringLiteral("logPath"), logPath);
    object.insert(QStringLiteral("error"), error);
    QJsonArray platformArray;
    for (const QString &platform : platforms) {
        platformArray.append(platform);
    }
    object.insert(QStringLiteral("platforms"), platformArray);
    QJsonArray steps;
    for (const BuildStepRecord &step : this->steps) {
        steps.append(step.toJson());
    }
    object.insert(QStringLiteral("steps"), steps);
    return object;
}

BuildStore::BuildStore(QString path)
    : m_path(path.isEmpty() ? Paths::dataDir() + QStringLiteral("/podstack.db")
                            : std::move(path))
{
}

BuildStore::~BuildStore()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool BuildStore::fail(const QString &fallback, Error *error)
{
    if (error) {
        *error = Error::make(QStringLiteral("PODSTACK_DB_ERROR"), fallback,
                             m_db ? QString::fromUtf8(sqlite3_errmsg(m_db)) : QString());
    }
    return false;
}

bool BuildStore::open(Error *error)
{
    if (error) {
        *error = {};
    }

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_db) {
        return true;
    }

    QDir().mkpath(QFileInfo(m_path).absolutePath());
    if (sqlite3_open_v2(m_path.toUtf8().constData(), &m_db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr)
        != SQLITE_OK) {
        return fail(QStringLiteral("Não foi possível abrir o banco de builds."), error);
    }

    sqlite3_busy_timeout(m_db, 5000);
    QString sqlError;
    if (!execSql(m_db, "PRAGMA journal_mode=WAL;", &sqlError)
        || !execSql(m_db, "PRAGMA foreign_keys=ON;", &sqlError)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_DB_ERROR"),
                                 QStringLiteral("Falha ao configurar o banco de builds."),
                                 sqlError);
        }
        return false;
    }
    return ensureSchema(error);
}

bool BuildStore::ensureSchema(Error *error)
{
    const char *schema = R"SQL(
CREATE TABLE IF NOT EXISTS builds (
    id TEXT PRIMARY KEY,
    image_ref TEXT NOT NULL DEFAULT '',
    status TEXT NOT NULL,
    started_at TEXT NOT NULL,
    finished_at TEXT,
    duration_ms INTEGER NOT NULL DEFAULT 0,
    trigger TEXT NOT NULL DEFAULT 'gui',
    context_path TEXT NOT NULL DEFAULT '',
    dockerfile TEXT NOT NULL DEFAULT 'Dockerfile',
    log_path TEXT NOT NULL DEFAULT '',
    error TEXT,
    platforms TEXT NOT NULL DEFAULT '[]'
);
CREATE TABLE IF NOT EXISTS build_steps (
    id TEXT PRIMARY KEY,
    build_id TEXT NOT NULL REFERENCES builds(id) ON DELETE CASCADE,
    idx INTEGER NOT NULL DEFAULT 0,
    name TEXT NOT NULL DEFAULT '',
    status TEXT NOT NULL DEFAULT 'running',
    started_at TEXT,
    finished_at TEXT,
    log_path TEXT
);
CREATE INDEX IF NOT EXISTS idx_build_steps_build ON build_steps(build_id, idx);
CREATE INDEX IF NOT EXISTS idx_builds_started ON builds(started_at DESC);
)SQL";

    QString sqlError;
    if (!execSql(m_db, schema, &sqlError)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_DB_ERROR"),
                                 QStringLiteral("Falha ao criar o schema de builds."),
                                 sqlError);
        }
        return false;
    }

    // Migração idempotente para bancos criados antes de platforms.
    if (!hasColumn(m_db, "builds", "platforms")) {
        if (!execSql(m_db,
                     "ALTER TABLE builds ADD COLUMN platforms TEXT NOT NULL DEFAULT '[]';",
                     &sqlError)) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_DB_ERROR"),
                                     QStringLiteral("Falha ao migrar o schema de builds."),
                                     sqlError);
            }
            return false;
        }
    }
    return true;
}

bool BuildStore::insert(const BuildRecord &record, Error *error)
{
    if (error) {
        *error = {};
    }
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!open(error)) {
        return false;
    }

    const char *sql = R"SQL(
INSERT OR REPLACE INTO builds
(id, image_ref, status, started_at, finished_at, duration_ms, trigger,
 context_path, dockerfile, log_path, error, platforms)
VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
)SQL";

    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &statement, nullptr) != SQLITE_OK) {
        return fail(QStringLiteral("Falha ao preparar o insert do build."), error);
    }

    bindText(statement, 1, record.id);
    bindText(statement, 2, record.imageRef);
    bindText(statement, 3, record.status);
    bindText(statement, 4, record.startedAt);
    bindText(statement, 5, record.finishedAt);
    sqlite3_bind_int(statement, 6, record.durationMs);
    bindText(statement, 7, record.trigger);
    bindText(statement, 8, record.contextPath);
    bindText(statement, 9, record.dockerfile);
    bindText(statement, 10, record.logPath);
    bindText(statement, 11, record.error);
    bindText(statement, 12, encodePlatforms(record.platforms));

    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!ok) {
        return fail(QStringLiteral("Falha ao gravar o build."), error);
    }
    return true;
}

bool BuildStore::finish(const QString &id, const QString &status, const QString &errorText,
                        int durationMs, Error *error)
{
    if (error) {
        *error = {};
    }
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!open(error)) {
        return false;
    }

    const char *sql = R"SQL(
UPDATE builds SET status = ?, finished_at = ?, duration_ms = ?, error = ? WHERE id = ?
)SQL";

    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &statement, nullptr) != SQLITE_OK) {
        return fail(QStringLiteral("Falha ao preparar a finalização do build."), error);
    }

    bindText(statement, 1, status);
    bindText(statement, 2,
             QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    sqlite3_bind_int(statement, 3, durationMs);
    bindText(statement, 4, errorText);
    bindText(statement, 5, id);

    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!ok) {
        return fail(QStringLiteral("Falha ao finalizar o build."), error);
    }
    return true;
}

bool BuildStore::addStep(const BuildStepRecord &step, Error *error)
{
    if (error) {
        *error = {};
    }
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!open(error)) {
        return false;
    }

    const char *sql = R"SQL(
INSERT OR REPLACE INTO build_steps
(id, build_id, idx, name, status, started_at, finished_at, log_path)
VALUES (?, ?, ?, ?, ?, ?, ?, ?)
)SQL";

    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &statement, nullptr) != SQLITE_OK) {
        return fail(QStringLiteral("Falha ao preparar o step do build."), error);
    }

    bindText(statement, 1, step.id);
    bindText(statement, 2, step.buildId);
    sqlite3_bind_int(statement, 3, step.index);
    bindText(statement, 4, step.name);
    bindText(statement, 5, step.status);
    bindText(statement, 6, step.startedAt);
    bindText(statement, 7, step.finishedAt);
    bindText(statement, 8, step.logPath);

    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!ok) {
        return fail(QStringLiteral("Falha ao gravar o step do build."), error);
    }
    return true;
}

bool BuildStore::finishSteps(const QString &buildId, const QString &status, Error *error)
{
    if (error) {
        *error = {};
    }
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!open(error)) {
        return false;
    }

    const char *sql = R"SQL(
UPDATE build_steps SET status = ?, finished_at = ? WHERE build_id = ? AND status = 'running'
)SQL";
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &statement, nullptr) != SQLITE_OK) {
        return fail(QStringLiteral("Falha ao finalizar os steps do build."), error);
    }
    bindText(statement, 1, status);
    bindText(statement, 2, QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    bindText(statement, 3, buildId);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!ok) {
        return fail(QStringLiteral("Falha ao finalizar os steps do build."), error);
    }
    return true;
}

QVector<BuildRecord> BuildStore::list(int limit, int offset, Error *error)
{
    if (error) {
        *error = {};
    }
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!open(error)) {
        return {};
    }

    const char *sql = R"SQL(
SELECT id, image_ref, status, started_at, finished_at, duration_ms, trigger,
       context_path, dockerfile, log_path, error, platforms
FROM builds ORDER BY started_at DESC LIMIT ? OFFSET ?
)SQL";

    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &statement, nullptr) != SQLITE_OK) {
        fail(QStringLiteral("Falha ao listar builds."), error);
        return {};
    }

    const int safeLimit = qBound(1, limit, 500);
    const int safeOffset = qMax(0, offset);
    sqlite3_bind_int(statement, 1, safeLimit);
    sqlite3_bind_int(statement, 2, safeOffset);

    QVector<BuildRecord> records;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        BuildRecord record;
        record.id = columnText(statement, 0);
        record.imageRef = columnText(statement, 1);
        record.status = columnText(statement, 2);
        record.startedAt = columnText(statement, 3);
        record.finishedAt = columnText(statement, 4);
        record.durationMs = sqlite3_column_int(statement, 5);
        record.trigger = columnText(statement, 6);
        record.contextPath = columnText(statement, 7);
        record.dockerfile = columnText(statement, 8);
        record.logPath = columnText(statement, 9);
        record.error = columnText(statement, 10);
        record.platforms = decodePlatforms(columnText(statement, 11));
        records.append(record);
    }
    sqlite3_finalize(statement);
    return records;
}

BuildRecord BuildStore::get(const QString &id, Error *error)
{
    if (error) {
        *error = {};
    }
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!open(error)) {
        return {};
    }

    const char *sql = R"SQL(
SELECT id, image_ref, status, started_at, finished_at, duration_ms, trigger,
       context_path, dockerfile, log_path, error, platforms
FROM builds WHERE id = ?
)SQL";

    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &statement, nullptr) != SQLITE_OK) {
        fail(QStringLiteral("Falha ao consultar o build."), error);
        return {};
    }
    bindText(statement, 1, id);

    BuildRecord record;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        record.id = columnText(statement, 0);
        record.imageRef = columnText(statement, 1);
        record.status = columnText(statement, 2);
        record.startedAt = columnText(statement, 3);
        record.finishedAt = columnText(statement, 4);
        record.durationMs = sqlite3_column_int(statement, 5);
        record.trigger = columnText(statement, 6);
        record.contextPath = columnText(statement, 7);
        record.dockerfile = columnText(statement, 8);
        record.logPath = columnText(statement, 9);
        record.error = columnText(statement, 10);
        record.platforms = decodePlatforms(columnText(statement, 11));
    }
    sqlite3_finalize(statement);

    if (record.id.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Build não encontrado."), id);
        }
        return {};
    }

    const char *stepsSql = R"SQL(
SELECT id, build_id, idx, name, status, started_at, finished_at, log_path
FROM build_steps WHERE build_id = ? ORDER BY idx
)SQL";
    if (sqlite3_prepare_v2(m_db, stepsSql, -1, &statement, nullptr) == SQLITE_OK) {
        bindText(statement, 1, id);
        while (sqlite3_step(statement) == SQLITE_ROW) {
            BuildStepRecord step;
            step.id = columnText(statement, 0);
            step.buildId = columnText(statement, 1);
            step.index = sqlite3_column_int(statement, 2);
            step.name = columnText(statement, 3);
            step.status = columnText(statement, 4);
            step.startedAt = columnText(statement, 5);
            step.finishedAt = columnText(statement, 6);
            step.logPath = columnText(statement, 7);
            record.steps.append(step);
        }
        sqlite3_finalize(statement);
    }
    return record;
}

bool BuildStore::remove(const QString &id, Error *error)
{
    if (error) {
        *error = {};
    }
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!open(error)) {
        return false;
    }

    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM builds WHERE id = ?", -1, &statement, nullptr)
        != SQLITE_OK) {
        return fail(QStringLiteral("Falha ao preparar a remoção do build."), error);
    }
    bindText(statement, 1, id);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!ok) {
        return fail(QStringLiteral("Falha ao remover o build."), error);
    }
    return true;
}

} // namespace PodStack
