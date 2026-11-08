#pragma once

#include "core/Error.h"
#include "daemon/Daemon.h"

#include <QHash>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>

namespace PodStack {

/// Servidor JSON-RPC 2.0 sobre o Unix socket de controle.
class RpcServer : public QObject {
public:
    explicit RpcServer(Daemon *daemon, QObject *parent = nullptr);

    bool listen(const QString &socketPath, Error *error);

private:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();
    void handleLine(QLocalSocket *socket, const QByteArray &line);
    bool authenticate(QLocalSocket *socket) const;
    void send(QLocalSocket *socket, const QJsonObject &message);

    Daemon *m_daemon;
    QLocalServer m_server;
    QHash<QLocalSocket *, QByteArray> m_buffers;
};

} // namespace PodStack
