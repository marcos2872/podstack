#pragma once

#include "core/Error.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <mutex>

struct sqlite3;

namespace PodStack {

struct BuildStepRecord {
    QString id;
    QString buildId;
    int index = 0;
    QString name;
    QString status;
    QString startedAt;
    QString finishedAt;
    QString logPath;

    QJsonObject toJson() const;
};

struct BuildRecord {
    QString id;
    QString imageRef;
    QString status;
    QString startedAt;
    QString finishedAt;
    int durationMs = 0;
    QString trigger;
    QString contextPath;
    QString dockerfile;
    QString logPath;
    QString error;
    QStringList platforms;
    QVector<BuildStepRecord> steps;

    QJsonObject toJson() const;
};

/// Histórico persistente de builds em SQLite (tabelas builds/build_steps).
class BuildStore {
public:
    explicit BuildStore(QString path = {});
    ~BuildStore();

    bool open(Error *error = nullptr);
    bool insert(const BuildRecord &record, Error *error = nullptr);
    bool finish(const QString &id, const QString &status, const QString &error,
                int durationMs, Error *storeError = nullptr);
    bool addStep(const BuildStepRecord &step, Error *error = nullptr);
    bool finishSteps(const QString &buildId, const QString &status, Error *error = nullptr);
    QVector<BuildRecord> list(int limit, int offset, Error *error = nullptr);
    BuildRecord get(const QString &id, Error *error = nullptr);
    bool remove(const QString &id, Error *error = nullptr);

    QString path() const { return m_path; }

private:
    bool ensureSchema(Error *error);
    bool fail(const QString &fallback, Error *error);

    sqlite3 *m_db = nullptr;
    QString m_path;
    std::recursive_mutex m_mutex;
};

} // namespace PodStack
