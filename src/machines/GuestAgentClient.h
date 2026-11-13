// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QString>
#include <QStringList>

namespace PodStack {

/// Cliente bloqueante do qemu-guest-agent sobre socket Unix.
///
/// O protocolo é JSON por linha; na conexão o cliente envia
/// `guest-sync-delimited` com o prefixo `0xFF` para descartar respostas
/// antigas do guest. Deve ser usado em worker threads.
class GuestAgentClient {
public:
    static constexpr int kDefaultConnectTimeoutMs = 5000;
    static constexpr int kDefaultCommandTimeoutMs = 10000;
    static constexpr int kMaxFileBytes = 1024 * 1024;

    explicit GuestAgentClient(QString socketPath);
    ~GuestAgentClient();

    GuestAgentClient(const GuestAgentClient &) = delete;
    GuestAgentClient &operator=(const GuestAgentClient &) = delete;

    bool connect(Error *error = nullptr);
    void disconnect();
    bool isConnected() const;

    QString socketPath() const { return m_socketPath; }
    void setConnectTimeout(int timeoutMs) { m_connectTimeoutMs = timeoutMs; }
    void setCommandTimeout(int timeoutMs) { m_commandTimeoutMs = timeoutMs; }

    bool ping(Error *error = nullptr);
    bool getOsInfo(QJsonObject *info, Error *error = nullptr);
    bool getInfo(QJsonObject *info, Error *error = nullptr);
    bool networkInterfaces(QJsonArray *interfaces, Error *error = nullptr);

    /// Executa um comando no guest e aguarda a saída (limite de `timeoutMs`).
    bool exec(const QString &path, const QStringList &arguments, int timeoutMs, int *exitCode,
              QByteArray *stdoutData, QByteArray *stderrData, Error *error = nullptr);

    /// Lê até `maxBytes` de um arquivo do guest (limite duro de 1 MiB).
    bool readFile(const QString &path, qint64 maxBytes, QByteArray *data, Error *error = nullptr);
    /// Grava um arquivo pequeno no guest (limite duro de 1 MiB).
    bool writeFile(const QString &path, const QByteArray &data, Error *error = nullptr);

private:
    bool syncDelimited(Error *error);
    bool sendCommand(const QString &command, const QJsonObject &arguments, QJsonObject *response,
                     int timeoutMs, Error *error);
    bool readResponse(QJsonObject *response, int timeoutMs, Error *error);

    QString m_socketPath;
    QLocalSocket m_socket;
    QByteArray m_buffer;
    int m_nextId = 1;
    int m_connectTimeoutMs = kDefaultConnectTimeoutMs;
    int m_commandTimeoutMs = kDefaultCommandTimeoutMs;
    bool m_synced = false;
};

} // namespace PodStack
