#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QString>

namespace PodStack {

/// Cliente de subscriptions (logs/stats) sobre o socket JSON-RPC.
class StreamClient : public QObject {
    Q_OBJECT

public:
    explicit StreamClient(QObject *parent = nullptr);

    void subscribeLogs(const QString &id, int tail, bool follow);
    void subscribeStats(const QString &id, int intervalSeconds = 1);
    void subscribeSystemStats(int intervalSeconds = 2);
    void subscribeMachineStats(const QString &id, int intervalSeconds = 2);
    void startMachineConsole(const QString &id, int tailBytes = 64 * 1024);
    void stopMachineConsole();
    void pullImage(const QString &reference, const QString &platform = {});
    void subscribeComposeLogs(const QString &project, const QString &service = {});
    void subscribeK8sLogs(const QString &namespaceName, const QString &pod,
                          const QString &container = {});
    void startK8sJob(const QString &method, const QJsonObject &params);
    void startBuild(const QJsonObject &params);
    /// Inicia `machines.create` e entrega `machines.delta`/`machines.done` sem unsubscribe:
    /// o daemon remove a subscription quando o job termina.
    void startMachineCreate(const QJsonObject &params);
    void startMachineExport(const QJsonObject &params);
    void startMachineImport(const QJsonObject &params);
    void startMachineClone(const QJsonObject &params);
    void startSetup(const QJsonObject &params);
    void startSetupRemoveDocker(const QJsonObject &params);
    void subscribeSetup(const QString &jobId);
    void subscribeEvents();
    void unsubscribe();

signals:
    void logLine(const QString &line);
    void stats(const QJsonObject &stats);
    void systemStats(const QJsonObject &sample);
    void machineStats(const QJsonObject &sample);
    void machineConsoleEvent(const QJsonObject &event);
    void pullEvent(const QJsonObject &event);
    void pullFinished(bool ok, const QString &error);
    void subscriptionReady(const QJsonObject &result);
    void composeLog(const QString &service, const QString &line);
    void buildEvent(const QJsonObject &event);
    void buildFinished(const QJsonObject &result);
    void machineCreateEvent(const QJsonObject &event);
    void machineCreateFinished(const QJsonObject &result);
    void setupEvent(const QJsonObject &event);
    void setupFinished(const QJsonObject &result);
    void eventReceived(const QJsonObject &event);
    void k8sLogLine(const QString &line);
    void k8sLogFinished(bool ok, const QString &error);
    void k8sJobEvent(const QJsonObject &event);
    void k8sJobFinished(const QJsonObject &result);
    void error(const QString &message);

private:
    void startMachineJob(const QString &method, const QJsonObject &params);
    void stop(bool sendUnsubscribe);
    void connectSocket();
    void send(const QJsonObject &message);
    void onConnected();
    void onReadyRead();
    void onError();

    QLocalSocket m_socket;
    QByteArray m_buffer;
    QString m_subscriptionId;
    QString m_machineConsoleSubscription;
    QString m_kind;
    QString m_unsubscribeMethod;
    QJsonObject m_pending;
    QJsonObject m_lastResult;
    bool m_pendingSend = false;
};

} // namespace PodStack
