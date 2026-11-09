// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/QmpClient.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

#include <utility>

namespace PodStack {

namespace {

constexpr int kMaxLineBytes = 1024 * 1024;

void qmpError(Error *error, const QJsonObject &response, const QString &command)
{
    if (!error) {
        return;
    }

    const QJsonObject errorObject = response.value(QStringLiteral("error")).toObject();
    const QString errorClass = errorObject.value(QStringLiteral("class")).toString();
    const QString description = errorObject.value(QStringLiteral("desc")).toString();

    QString detail = description;
    if (!errorClass.isEmpty()) {
        detail = description.isEmpty() ? errorClass
                                       : QStringLiteral("%1: %2").arg(errorClass, description);
    }

    *error = Error::make(
        QStringLiteral("PODSTACK_INTERNAL"),
        QStringLiteral("O QMP retornou erro ao executar '%1'.").arg(command), detail);
}

} // namespace

QmpClient::QmpClient(QString socketPath)
    : m_socketPath(std::move(socketPath))
{
}

QmpClient::~QmpClient()
{
    disconnect();
}

bool QmpClient::connect(int timeoutMs, Error *error)
{
    if (error) {
        *error = {};
    }
    disconnect();

    const int connectTimeout = timeoutMs > 0 ? timeoutMs : m_connectTimeoutMs;
    if (m_socketPath.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O caminho do socket QMP está vazio."));
        }
        return false;
    }

    m_socket.connectToServer(m_socketPath, QIODevice::ReadWrite);
    if (!m_socket.waitForConnected(connectTimeout)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível conectar ao socket QMP."),
                                 m_socket.errorString());
        }
        m_socket.abort();
        return false;
    }

    QJsonObject greeting;
    if (!readMessage(&greeting, m_commandTimeoutMs, error)) {
        disconnect();
        return false;
    }
    if (!greeting.contains(QStringLiteral("QMP"))) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Saudação QMP inválida recebida do QEMU."));
        }
        disconnect();
        return false;
    }

    if (!handshake(error)) {
        disconnect();
        return false;
    }
    return true;
}

bool QmpClient::execute(const QString &command, const QJsonObject &arguments,
                        QJsonObject *result, Error *error)
{
    if (error) {
        *error = {};
    }
    if (command.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("O comando QMP é obrigatório."));
        }
        return false;
    }
    if (!isConnected()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O cliente QMP não está conectado."));
        }
        return false;
    }

    return sendCommand(command, arguments, result, m_commandTimeoutMs, error);
}

bool QmpClient::queryStatus(QString *status, Error *error)
{
    QJsonObject result;
    if (!execute(QStringLiteral("query-status"), {}, &result, error)) {
        return false;
    }

    const QString value = result.value(QStringLiteral("status")).toString();
    if (value.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                 QStringLiteral("Resposta de query-status sem o campo status."));
        }
        return false;
    }

    if (status) {
        *status = value;
    }
    return true;
}

bool QmpClient::systemPowerdown(Error *error)
{
    return execute(QStringLiteral("system_powerdown"), {}, nullptr, error);
}

bool QmpClient::quit(Error *error)
{
    if (error) {
        *error = {};
    }
    if (!isConnected()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O cliente QMP não está conectado."));
        }
        return false;
    }

    const int id = m_nextId++;
    QJsonObject request;
    request.insert(QStringLiteral("execute"), QStringLiteral("quit"));
    request.insert(QStringLiteral("id"), id);
    if (!writeMessage(request, m_commandTimeoutMs, error)) {
        return false;
    }

    while (true) {
        QJsonObject response;
        Error readError;
        if (!readMessage(&response, m_commandTimeoutMs, &readError)) {
            const bool peerClosed = m_socket.state() != QLocalSocket::ConnectedState
                                    || m_socket.error() == QLocalSocket::PeerClosedError;
            if (readError.code == QStringLiteral("PODSTACK_IO_ERROR") && peerClosed) {
                disconnect();
                return true;
            }
            if (error) {
                *error = readError;
            }
            return false;
        }

        if (response.contains(QStringLiteral("event"))) {
            continue;
        }
        if (response.value(QStringLiteral("id")).toInt(-1) != id) {
            continue;
        }
        if (response.contains(QStringLiteral("error"))) {
            qmpError(error, response, QStringLiteral("quit"));
            return false;
        }

        disconnect();
        return true;
    }
}

void QmpClient::disconnect()
{
    m_connected = false;
    m_buffer.clear();
    if (m_socket.state() != QLocalSocket::UnconnectedState) {
        m_socket.abort();
    }
}

bool QmpClient::isConnected() const
{
    return m_connected && m_socket.state() == QLocalSocket::ConnectedState;
}

void QmpClient::setConnectTimeout(int timeoutMs)
{
    m_connectTimeoutMs = timeoutMs > 0 ? timeoutMs : kDefaultConnectTimeoutMs;
}

void QmpClient::setCommandTimeout(int timeoutMs)
{
    m_commandTimeoutMs = timeoutMs > 0 ? timeoutMs : kDefaultCommandTimeoutMs;
}

bool QmpClient::handshake(Error *error)
{
    if (!sendCommand(QStringLiteral("qmp_capabilities"), {}, nullptr, m_commandTimeoutMs,
                     error)) {
        return false;
    }
    m_connected = true;
    return true;
}

bool QmpClient::sendCommand(const QString &command, const QJsonObject &arguments,
                            QJsonObject *result, int timeoutMs, Error *error)
{
    const int id = m_nextId++;

    QJsonObject request;
    request.insert(QStringLiteral("execute"), command);
    if (!arguments.isEmpty()) {
        request.insert(QStringLiteral("arguments"), arguments);
    }
    request.insert(QStringLiteral("id"), id);

    if (!writeMessage(request, timeoutMs, error)) {
        return false;
    }

    while (true) {
        QJsonObject response;
        if (!readMessage(&response, timeoutMs, error)) {
            return false;
        }

        // Eventos assíncronos podem chegar antes da resposta; são ignorados.
        if (response.contains(QStringLiteral("event"))) {
            continue;
        }
        if (response.value(QStringLiteral("id")).toInt(-1) != id) {
            continue;
        }
        if (response.contains(QStringLiteral("error"))) {
            qmpError(error, response, command);
            return false;
        }

        if (result) {
            *result = response.value(QStringLiteral("return")).toObject();
        }
        return true;
    }
}

bool QmpClient::writeMessage(const QJsonObject &message, int timeoutMs, Error *error)
{
    const QByteArray payload =
        QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    const qint64 written = m_socket.write(payload);
    if (written != payload.size() || !m_socket.waitForBytesWritten(timeoutMs)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Falha ao enviar comando ao QEMU via QMP."),
                                 m_socket.errorString());
        }
        return false;
    }
    return true;
}

bool QmpClient::readMessage(QJsonObject *message, int timeoutMs, Error *error)
{
    QElapsedTimer timer;
    timer.start();

    while (true) {
        const int newline = m_buffer.indexOf('\n');
        if (newline >= 0) {
            QByteArray line = m_buffer.left(newline);
            m_buffer.remove(0, newline + 1);
            if (line.endsWith('\r')) {
                line.chop(1);
            }
            if (line.trimmed().isEmpty()) {
                continue;
            }
            if (line.size() > kMaxLineBytes) {
                if (error) {
                    *error = Error::make(
                        QStringLiteral("PODSTACK_IO_ERROR"),
                        QStringLiteral("Linha QMP acima do limite de 1 MiB."));
                }
                return false;
            }

            QJsonParseError parseError{};
            const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                if (error) {
                    *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                         QStringLiteral("Resposta QMP inválida."),
                                         parseError.errorString());
                }
                return false;
            }

            if (message) {
                *message = document.object();
            }
            return true;
        }

        if (m_buffer.size() > kMaxLineBytes) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                     QStringLiteral("Linha QMP acima do limite de 1 MiB."));
            }
            return false;
        }

        if (m_socket.state() != QLocalSocket::ConnectedState) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                     QStringLiteral("Conexão QMP encerrada."),
                                     m_socket.errorString());
            }
            return false;
        }

        const qint64 remaining = timeoutMs - timer.elapsed();
        if (remaining <= 0) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_TIMEOUT"),
                                     QStringLiteral("Tempo esgotado aguardando resposta do QMP."));
            }
            return false;
        }

        if (!m_socket.waitForReadyRead(static_cast<int>(remaining))) {
            if (timer.elapsed() >= timeoutMs) {
                if (error) {
                    *error = Error::make(
                        QStringLiteral("PODSTACK_TIMEOUT"),
                        QStringLiteral("Tempo esgotado aguardando resposta do QMP."));
                }
                return false;
            }
            if (m_socket.state() != QLocalSocket::ConnectedState) {
                if (error) {
                    *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                         QStringLiteral("Conexão QMP encerrada."),
                                         m_socket.errorString());
                }
                return false;
            }
            continue;
        }

        m_buffer += m_socket.readAll();
    }
}

} // namespace PodStack
