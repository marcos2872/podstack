#pragma once

#include "core/error/Error.h"
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
    ~RpcServer() override;

    bool listen(const QString &socketPath, Error *error);

    /// Para de aceitar conexões. Chamado no shutdown antes de aguardar as
    /// threads de trabalho, para não abrir uma janela de socket sem resposta.
    void close();

private:
    /// fd 3 quando o systemd usa socket activation (LISTEN_FDS/LISTEN_PID).
    static bool systemdListenFd(qintptr *descriptor);
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();
    void handleLine(QLocalSocket *socket, const QByteArray &line);
    bool authenticate(QLocalSocket *socket) const;
    void send(QLocalSocket *socket, const QJsonObject &message);

    Daemon *m_daemon;
    QLocalServer m_server;
    QHash<QLocalSocket *, QByteArray> m_buffers;
    QHash<QLocalSocket *, quint64> m_connections;
    quint64 m_nextConnection = 1;
};

} // namespace PodStack
