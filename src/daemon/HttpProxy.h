// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"
#include "podman/PodmanClient.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

class QTcpServer;

namespace PodStack {

/// Proxy HTTP de entrada para `*.localhost`.
///
/// Escuta apenas em loopback (`127.0.0.1` e, quando disponível, `::1`) na porta
/// `network.httpPort` e encaminha `http://<nome>.localhost[:porta]/...` para a
/// primeira porta TCP publicada do container `<nome>`. Não requer root nem
/// altera `/etc/hosts`.
class HttpProxy : public QObject {
    Q_OBJECT
public:
    /// Fonte de containers para o proxy (permite teste sem Podman).
    using ContainerProvider = std::function<bool(QVector<ContainerInfo> *containers, Error *error)>;

    explicit HttpProxy(ContainerProvider provider, QObject *parent = nullptr);

    /// Escuta em `127.0.0.1:<port>` (e `::1` quando disponível). `port` 0
    /// escolhe uma porta livre (usado em testes).
    bool listen(quint16 port, Error *error = nullptr);
    void close();

    /// Porta efetiva do listener IPv4 (0 quando não está escutando).
    quint16 port() const;

    /// Extrai o nome de `Host: <nome>.localhost[:porta]`.
    static bool hostName(const QByteArray &hostHeader, QString *name);

    /// Primeira porta TCP publicada do container, no formato que o
    /// `PodmanClient` produz (`[ip:]porta->porta/tcp`).
    static bool publishedPort(const ContainerInfo &container, QString *host, quint16 *port);

    /// Ajusta os timeouts de conexão e de inatividade (milissegundos).
    void setTimeouts(int connectTimeoutMs, int idleTimeoutMs);

private:
    void accept(QTcpServer *server);

    ContainerProvider m_provider;
    QTcpServer *m_v4 = nullptr;
    QTcpServer *m_v6 = nullptr;
    int m_connectTimeoutMs = 10000;
    int m_idleTimeoutMs = 60000;
};

} // namespace PodStack
