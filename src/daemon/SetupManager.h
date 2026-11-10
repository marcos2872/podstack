// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"
#include "daemon/HelperClient.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

namespace PodStack {

/// Resultado de um check de ambiente do setup.
struct SetupCheck {
    QString id;
    QString status; // ok | warn | missing | optional
    QString version;
    QString detail;
    QString remediation;

    QJsonObject toJson() const;
};

/// Detecção read-only do ambiente e jobs de preparação da máquina.
///
/// Toda alteração privilegiada passa pelo helper allowlisted via D-Bus;
/// este componente nunca executa shell arbitrário.
class SetupManager {
public:
    using Notify = std::function<void(const QString &method, const QJsonObject &params)>;

    SetupManager();

    QList<SetupCheck> check(bool deep = false);
    QJsonObject status();
    QJsonObject validate();
    QJsonObject jobStatus(const QString &jobId) const;
    QJsonObject activeJob() const;

    QJsonObject apply(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject removeDocker(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject cancel(const QJsonObject &params);
    void subscribe(const QString &jobId, const Notify &notify);
    bool complete(bool skipped, Error *error);

private:
    struct Job {
        QString id;
        QString action;
        QString state = QStringLiteral("queued");
        int progress = 0;
        QStringList log;
        QString error;
        QDateTime startedAt;
        QDateTime finishedAt;
        std::atomic_bool cancel{false};
        QList<Notify> watchers;
    };

    std::shared_ptr<Job> createJob(const QString &action, const Notify &notify);
    void runJob(const std::shared_ptr<Job> &job, const QJsonObject &options);
    void finishJob(const std::shared_ptr<Job> &job, const QString &state, const QString &error);
    void publish(const std::shared_ptr<Job> &job, const QString &method);
    void appendLog(const std::shared_ptr<Job> &job, const QString &line);
    void setProgress(const std::shared_ptr<Job> &job, int progress);
    QJsonObject jobToJsonLocked(const Job &job) const;
    std::shared_ptr<Job> findJob(const QString &jobId) const;
    QList<Notify> watchersFor(const QString &jobId) const;

    QStringList actionPackages(const QString &action, const QJsonObject &options) const;
    bool enablePodmanSocket(QString *error) const;
    bool helperCall(const QString &method, const QStringList &arguments, QString *error,
                    int timeoutMs = 900000) const;
    bool helperAvailable() const;
    bool applyDockerPolicy(const QString &policy, QString *error);

    static QString podmanVersion();
    static QString dockerVersion();
    static bool dockerInstalled();
    static bool kvmAvailable();
    static bool kvmWritable();
    static bool commandSucceeds(const QString &program, const QStringList &arguments,
                                int timeoutMs = 2000);

    mutable std::mutex m_mutex;
    HelperClient m_helper;
    QHash<QString, std::shared_ptr<Job>> m_jobs;
    QString m_activeJobId;
    int m_nextJob = 1;
};

} // namespace PodStack
