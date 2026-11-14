// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/rpc/StreamClient.h"
#include "gui/controllers/ControllerBase.h"

#include <QFutureWatcher>
#include <QJsonObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace PodStack {

class MachineDetailController : public ControllerBase {
    Q_OBJECT
    Q_PROPERTY(QStringList logs READ logs NOTIFY logsChanged)
    Q_PROPERTY(QString logsCapturedAt READ logsCapturedAt NOTIFY logsChanged)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)
    Q_PROPERTY(QVariantList cpuHistory READ cpuHistory NOTIFY statsChanged)
    Q_PROPERTY(QVariantList memHistory READ memHistory NOTIFY statsChanged)
    Q_PROPERTY(QVariantList netRxHistory READ netRxHistory NOTIFY statsChanged)
    Q_PROPERTY(QVariantList netTxHistory READ netTxHistory NOTIFY statsChanged)
    Q_PROPERTY(QString statsError READ statsError NOTIFY statsChanged)
    Q_PROPERTY(bool consoleActive READ consoleActive NOTIFY consoleChanged)
    Q_PROPERTY(QString consoleText READ consoleText NOTIFY consoleChanged)
    Q_PROPERTY(QString consoleError READ consoleError NOTIFY consoleChanged)
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    Q_PROPERTY(QString filesPath READ filesPath NOTIFY filesChanged)
    Q_PROPERTY(bool filesBusy READ filesBusy NOTIFY filesChanged)
    Q_PROPERTY(QString filesError READ filesError NOTIFY filesChanged)

public:
    explicit MachineDetailController(QObject *parent = nullptr);

    QStringList logs() const { return m_machineLogLines; }
    QString logsCapturedAt() const { return m_machineLogsCapturedAt; }
    QVariantMap stats() const { return m_machineStats; }
    QVariantList cpuHistory() const { return m_machineCpuHistory; }
    QVariantList memHistory() const { return m_machineMemHistory; }
    QVariantList netRxHistory() const { return m_machineNetRxHistory; }
    QVariantList netTxHistory() const { return m_machineNetTxHistory; }
    QString statsError() const { return m_machineStatsError; }
    bool consoleActive() const { return m_machineConsoleActive; }
    QString consoleText() const { return m_machineConsoleText; }
    QString consoleError() const { return m_machineConsoleError; }
    QVariantList files() const { return m_machineFiles; }
    QString filesPath() const { return m_machineFilesPath; }
    bool filesBusy() const { return m_machineFilesBusy; }
    QString filesError() const { return m_machineFilesError; }

    Q_INVOKABLE void loadLogs(const QString &id, int tail = 200);
    Q_INVOKABLE void startStats(const QString &id);
    Q_INVOKABLE void stopStats();
    Q_INVOKABLE void followConsole(const QString &id, int tailBytes = 64 * 1024);
    Q_INVOKABLE void stopConsole();
    Q_INVOKABLE void refreshFiles(const QString &id, const QString &path);
    Q_INVOKABLE void downloadFile(const QString &id, const QString &path,
                                  const QString &fileName);
    Q_INVOKABLE void uploadFile(const QString &id, const QString &remoteDir,
                                const QString &localPath);
    Q_INVOKABLE void copyLogs();

signals:
    void logsChanged();
    void statsChanged();
    void consoleChanged();
    void filesChanged();

private:
    void onMachineLogsFinished();
    void onMachineFilesFinished();
    void startMachineFilesRequest(const QString &operation, const QString &id,
                                  const QString &method, const QJsonObject &params,
                                  int timeoutMs);

    QFutureWatcher<QJsonObject> m_machineLogsWatcher;
    QFutureWatcher<QJsonObject> m_machineFilesWatcher;
    StreamClient m_machineStatsStream;
    StreamClient m_machineConsoleStream;

    QStringList m_machineLogLines;
    QString m_machineLogsCapturedAt;
    QVariantMap m_machineStats;
    QVariantList m_machineCpuHistory;
    QVariantList m_machineMemHistory;
    QVariantList m_machineNetRxHistory;
    QVariantList m_machineNetTxHistory;
    QString m_machineStatsError;
    bool m_machineConsoleActive = false;
    QString m_machineConsoleText;
    QString m_machineConsoleError;
    QVariantList m_machineFiles;
    QString m_machineFilesPath = QStringLiteral("/");
    bool m_machineFilesBusy = false;
    QString m_machineFilesError;
    QString m_pendingMachineLogsId;
    int m_pendingMachineLogsTail = 200;
    QString m_machineFilesMachineId;
    QString m_machineFilesOperation;
};

} // namespace PodStack
