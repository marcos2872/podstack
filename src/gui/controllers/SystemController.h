// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/logging/LogReader.h"
#include "core/rpc/StreamClient.h"
#include "gui/controllers/ControllerBase.h"

#include <QFutureWatcher>
#include <QJsonObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace PodStack {

class SystemController : public ControllerBase {
    Q_OBJECT
    Q_PROPERTY(QVariantList logs READ logs NOTIFY logsChanged)
    Q_PROPERTY(QString logsError READ logsError NOTIFY logsChanged)
    Q_PROPERTY(bool logsTruncated READ logsTruncated NOTIFY logsChanged)
    Q_PROPERTY(bool logsBusy READ logsBusy NOTIFY logsChanged)
    Q_PROPERTY(QString diagnosticReportPath READ diagnosticReportPath
               NOTIFY diagnosticReportChanged)
    Q_PROPERTY(QVariantMap diskHealth READ diskHealth NOTIFY diskHealthChanged)
    Q_PROPERTY(QVariantMap activityStats READ activityStats NOTIFY activityChanged)
    Q_PROPERTY(QVariantList activityCpuHistory READ activityCpuHistory NOTIFY activityChanged)
    Q_PROPERTY(QVariantList activityMemHistory READ activityMemHistory NOTIFY activityChanged)
    Q_PROPERTY(bool activityActive READ activityActive NOTIFY activityChanged)

public:
    explicit SystemController(QObject *parent = nullptr);

    QVariantList logs() const { return m_logs; }
    QString logsError() const { return m_logsError; }
    bool logsTruncated() const { return m_logsTruncated; }
    bool logsBusy() const { return m_logsBusy; }
    QString diagnosticReportPath() const { return m_diagnosticReportPath; }
    QVariantMap diskHealth() const { return m_diskHealth; }
    QVariantMap activityStats() const { return m_activityStats; }
    QVariantList activityCpuHistory() const { return m_activityCpuHistory; }
    QVariantList activityMemHistory() const { return m_activityMemHistory; }
    bool activityActive() const { return m_activityActive; }

    Q_INVOKABLE void loadLogs(const QString &level = QString(),
                              const QString &component = QString(), int sinceDays = 7);
    Q_INVOKABLE bool exportLogs(const QString &path);
    Q_INVOKABLE void clearLogs();
    Q_INVOKABLE void generateDiagnosticReport();
    Q_INVOKABLE void checkDiskHealth();
    Q_INVOKABLE void openLogsFolder();
    Q_INVOKABLE void startActivityMonitor();
    Q_INVOKABLE void stopActivityMonitor();

signals:
    void logsChanged();
    void diagnosticReportChanged();
    void diskHealthChanged();
    void diskLow(const QVariantMap &sample);
    void activityChanged();

private:
    struct Task {
        LogReadResult result;
        Error error;
    };
    void onLogsFinished();
    void onDiskFinished();
    void onReportFinished();

    QVariantList m_logs;
    QString m_logsError;
    bool m_logsTruncated = false;
    bool m_logsBusy = false;
    QVector<LogLine> m_lastLogs;
    LogReadOptions m_pendingOptions;
    QString m_pendingComponent;
    QFutureWatcher<Task> m_logsWatcher;
    QFutureWatcher<QJsonObject> m_diskWatcher;
    QFutureWatcher<QJsonObject> m_reportWatcher;
    QString m_diagnosticReportPath;
    QVariantMap m_diskHealth;
    bool m_diskRunning = false;
    QVariantMap m_activityStats;
    QVariantList m_activityCpuHistory;
    QVariantList m_activityMemHistory;
    bool m_activityActive = false;
    StreamClient m_activityStream;
};

} // namespace PodStack
