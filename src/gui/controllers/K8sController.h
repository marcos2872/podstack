// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/rpc/StreamClient.h"
#include "gui/controllers/ControllerBase.h"

#include <QFutureWatcher>
#include <QJsonObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

namespace PodStack {

class K8sController : public ControllerBase {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(QVariantList pods READ pods NOTIFY changed)
    Q_PROPERTY(QVariantList services READ services NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QStringList logLines READ logLines NOTIFY logsChanged)
    Q_PROPERTY(QString logError READ logError NOTIFY logsChanged)
    Q_PROPERTY(bool logActive READ logActive NOTIFY logsChanged)
    Q_PROPERTY(bool portForwardActive READ portForwardActive NOTIFY forwardChanged)
    Q_PROPERTY(QString portForwardId READ portForwardId NOTIFY forwardChanged)
    Q_PROPERTY(QString portForwardError READ portForwardError NOTIFY forwardChanged)
    Q_PROPERTY(bool jobActive READ jobActive NOTIFY jobChanged)
    Q_PROPERTY(QString jobStage READ jobStage NOTIFY jobChanged)
    Q_PROPERTY(int jobProgress READ jobProgress NOTIFY jobChanged)
    Q_PROPERTY(QString jobError READ jobError NOTIFY jobChanged)

public:
    explicit K8sController(QObject *parent = nullptr);

    QString state() const { return m_state; }
    bool enabled() const { return m_enabled; }
    QVariantList pods() const { return m_pods; }
    QVariantList services() const { return m_services; }
    QString error() const { return m_error; }
    bool loading() const { return m_loading; }
    QStringList logLines() const { return m_logLines; }
    QString logError() const { return m_logError; }
    bool logActive() const { return m_logActive; }
    bool portForwardActive() const { return m_portForwardActive; }
    QString portForwardId() const { return m_portForwardId; }
    QString portForwardError() const { return m_portForwardError; }
    bool jobActive() const { return m_jobActive; }
    QString jobStage() const { return m_jobStage; }
    int jobProgress() const { return m_jobProgress; }
    QString jobError() const { return m_jobError; }

    Q_INVOKABLE void refreshStatus();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void startWatch();
    Q_INVOKABLE void stopWatch();
    Q_INVOKABLE void startLogs(const QString &namespaceName, const QString &pod,
                               const QString &container = QString());
    Q_INVOKABLE void stopLogs();
    Q_INVOKABLE void clearLogs();
    Q_INVOKABLE void deletePod(const QString &namespaceName, const QString &name);
    Q_INVOKABLE void deleteService(const QString &namespaceName, const QString &name);
    Q_INVOKABLE void startPortForward(const QString &namespaceName, const QString &name,
                                      int localPort, int remotePort,
                                      const QString &resource = QStringLiteral("pod"));
    Q_INVOKABLE void stopPortForward();
    Q_INVOKABLE void enable();
    Q_INVOKABLE void disable(bool force = false);
    Q_INVOKABLE void reset();

signals:
    void changed();
    void logsChanged();
    void forwardChanged();
    void jobChanged();

protected:
    void onActionFinished(const QString &method, const QJsonObject &params,
                          const QJsonObject &result) override;

private:
    QString m_state;
    bool m_enabled = false;
    QVariantList m_pods;
    QVariantList m_services;
    QString m_error;
    bool m_loading = false;
    QStringList m_logLines;
    QString m_logError;
    bool m_logActive = false;
    bool m_portForwardActive = false;
    QString m_portForwardId;
    QString m_portForwardError;
    bool m_jobActive = false;
    QString m_jobStage;
    int m_jobProgress = 0;
    QString m_jobError;

    QFutureWatcher<QJsonObject> m_statusWatcher;
    QFutureWatcher<QJsonObject> m_podsWatcher;
    QFutureWatcher<QJsonObject> m_servicesWatcher;
    QFutureWatcher<QJsonObject> m_forwardWatcher;
    StreamClient m_logsStream;
    StreamClient m_jobStream;
    QTimer m_timer;
};

} // namespace PodStack
