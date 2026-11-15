// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/rpc/StreamClient.h"
#include "gui/controllers/ControllerBase.h"

#include <QJsonObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace PodStack {

class ContainerController;

/// Estado e ações do overlay de detalhe de um container (9 abas).
class ContainerDetailController : public ControllerBase {
    Q_OBJECT
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY detailChanged)
    Q_PROPERTY(QString error READ error NOTIFY detailChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY detailChanged)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)
    Q_PROPERTY(QVariantList cpuHistory READ cpuHistory NOTIFY statsChanged)
    Q_PROPERTY(QVariantList memHistory READ memHistory NOTIFY statsChanged)
    Q_PROPERTY(QVariantList netRxHistory READ netRxHistory NOTIFY statsChanged)
    Q_PROPERTY(QVariantList netTxHistory READ netTxHistory NOTIFY statsChanged)
    Q_PROPERTY(QVariantList diskReadHistory READ diskReadHistory NOTIFY statsChanged)
    Q_PROPERTY(QVariantList diskWriteHistory READ diskWriteHistory NOTIFY statsChanged)
    Q_PROPERTY(QString statsError READ statsError NOTIFY statsChanged)
    Q_PROPERTY(QStringList logLines READ logLines NOTIFY logsChanged)
    Q_PROPERTY(QString logsError READ logsError NOTIFY logsChanged)

public:
    explicit ContainerDetailController(ContainerController *containers, QObject *parent = nullptr);

    QVariantMap detail() const { return m_detail; }
    QString error() const { return m_error; }
    bool loading() const { return m_loading; }
    QVariantMap stats() const { return m_stats; }
    QVariantList cpuHistory() const { return m_cpuHistory; }
    QVariantList memHistory() const { return m_memHistory; }
    QVariantList netRxHistory() const { return m_netRxHistory; }
    QVariantList netTxHistory() const { return m_netTxHistory; }
    QVariantList diskReadHistory() const { return m_diskReadHistory; }
    QVariantList diskWriteHistory() const { return m_diskWriteHistory; }
    QString statsError() const { return m_statsError; }
    QStringList logLines() const { return m_logLines; }
    QString logsError() const { return m_logsError; }

    Q_INVOKABLE void load(const QString &id);
    Q_INVOKABLE void copyInspect();
    Q_INVOKABLE void startStats(const QString &id);
    Q_INVOKABLE void stopStats();
    Q_INVOKABLE void startLogs(const QString &id, int tail = 200, bool follow = true);
    Q_INVOKABLE void stopLogs();
    Q_INVOKABLE void clearLogs();
    Q_INVOKABLE void lifecycle(const QString &action, const QString &id);
    Q_INVOKABLE void remove(const QString &id, bool force);

signals:
    void detailChanged();
    void statsChanged();
    void logsChanged();

private:
    ContainerController *m_containers = nullptr;
    QVariantMap m_detail;
    QJsonObject m_detailJson;
    QString m_error;
    bool m_loading = false;
    QString m_pendingId;

    QVariantMap m_stats;
    QVariantList m_cpuHistory;
    QVariantList m_memHistory;
    QVariantList m_netRxHistory;
    QVariantList m_netTxHistory;
    QVariantList m_diskReadHistory;
    QVariantList m_diskWriteHistory;
    QString m_statsError;
    QStringList m_logLines;
    QString m_logsError;
    bool m_logsPending = false;
    QTimer *m_logsTimer = nullptr;
    StreamClient m_statsStream;
    StreamClient m_logsStream;
};

} // namespace PodStack
