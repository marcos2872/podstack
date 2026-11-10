// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"

#include <QObject>

class QTcpServer;
class QUdpSocket;

namespace PodStack {

/// DNS userspace mínimo para `*.podstack.local`.
///
/// Escuta em loopback na porta 5353 (UDP e, quando possível, TCP) e responde
/// A/AAAA com 127.0.0.1/::1. O drop-in do `systemd-resolved` instalado pelo
/// helper encaminha apenas essa zona para cá.
class DnsServer : public QObject {
    Q_OBJECT
public:
    explicit DnsServer(QObject *parent = nullptr);

    bool listen(quint16 port, Error *error = nullptr);
    void close();

    /// Porta efetiva do listener (0 quando não está escutando).
    quint16 port() const;

    /// Resposta DNS para uma consulta; vazio quando a mensagem é inválida.
    static QByteArray answer(const QByteArray &query);

private:
    void readDatagrams();
    void acceptTcp();

    QUdpSocket *m_udp = nullptr;
    QTcpServer *m_tcp = nullptr;
};

} // namespace PodStack
