// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"

#include <QJsonObject>
#include <QLocalSocket>
#include <QString>

namespace PodStack {

/// Cliente QMP bloqueante sobre socket Unix. Deve ser usado em worker threads.
class QmpClient {
public:
    static constexpr int kDefaultConnectTimeoutMs = 5000;
    static constexpr int kDefaultCommandTimeoutMs = 2000;

    explicit QmpClient(QString socketPath);
    ~QmpClient();

    QmpClient(const QmpClient &) = delete;
    QmpClient &operator=(const QmpClient &) = delete;

    /// Conecta e executa o handshake `qmp_capabilities`. Timeout <= 0 usa o valor
    /// configurado em setConnectTimeout().
    bool connect(int timeoutMs = kDefaultConnectTimeoutMs, Error *error = nullptr);
    bool connect(Error *error) { return connect(m_connectTimeoutMs, error); }

    /// Envia um comando e aguarda a resposta com o mesmo `id`, ignorando eventos.
    bool execute(const QString &command, const QJsonObject &arguments,
                 QJsonObject *result, Error *error);

    bool queryStatus(QString *status, Error *error);
    bool systemPowerdown(Error *error);

    /// Envia `quit`; sucesso também quando o QEMU fecha o socket sem responder.
    bool quit(Error *error);

    void disconnect();
    bool isConnected() const;

    void setConnectTimeout(int timeoutMs);
    int connectTimeout() const { return m_connectTimeoutMs; }
    void setCommandTimeout(int timeoutMs);
    int commandTimeout() const { return m_commandTimeoutMs; }

    QString socketPath() const { return m_socketPath; }

private:
    bool writeMessage(const QJsonObject &message, int timeoutMs, Error *error);
    bool readMessage(QJsonObject *message, int timeoutMs, Error *error);
    bool sendCommand(const QString &command, const QJsonObject &arguments,
                     QJsonObject *result, int timeoutMs, Error *error);
    bool handshake(Error *error);

    QLocalSocket m_socket;
    QString m_socketPath;
    QByteArray m_buffer;
    int m_nextId = 1;
    int m_connectTimeoutMs = kDefaultConnectTimeoutMs;
    int m_commandTimeoutMs = kDefaultCommandTimeoutMs;
    bool m_connected = false;
};

} // namespace PodStack
