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
    void unsubscribe();

signals:
    void logLine(const QString &line);
    void stats(const QJsonObject &stats);
    void error(const QString &message);

private:
    void stop(bool sendUnsubscribe);
    void connectSocket();
    void send(const QJsonObject &message);
    void onConnected();
    void onReadyRead();
    void onError();

    QLocalSocket m_socket;
    QByteArray m_buffer;
    QString m_subscriptionId;
    QString m_kind;
    QJsonObject m_pending;
    bool m_pendingSend = false;
};

} // namespace PodStack
