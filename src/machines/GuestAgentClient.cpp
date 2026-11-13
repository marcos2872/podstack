// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/GuestAgentClient.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRandomGenerator>
#include <QThread>

#include <utility>

namespace PodStack {
namespace {

constexpr int kPollIntervalMs = 50;
constexpr int kFileChunkBytes = 512 * 1024;

Error agentError(const QString &message, const QString &detail = {})
{
    return Error::make(QStringLiteral("PODSTACK_IO_ERROR"), message, detail);
}

Error timeoutError(const QString &message)
{
    return Error::make(QStringLiteral("PODSTACK_TIMEOUT"), message);
}

QJsonObject argumentsWithPid(int pid)
{
    return QJsonObject{{QStringLiteral("pid"), pid}};
}

} // namespace

GuestAgentClient::GuestAgentClient(QString socketPath)
    : m_socketPath(std::move(socketPath))
{
}

GuestAgentClient::~GuestAgentClient()
{
    disconnect();
}

bool GuestAgentClient::connect(Error *error)
{
    if (error) {
        *error = {};
    }
    if (m_socketPath.isEmpty()) {
        if (error) {
            *error = agentError(QStringLiteral("Socket do qemu-guest-agent vazio."));
        }
        return false;
    }
    if (m_socket.isOpen()) {
        if (m_synced) {
            return true;
        }
        disconnect();
    }

    m_buffer.clear();
    m_synced = false;
    m_socket.connectToServer(m_socketPath);
    if (!m_socket.waitForConnected(m_connectTimeoutMs)) {
        if (error) {
            *error = agentError(QStringLiteral("qemu-guest-agent não respondeu."),
                                m_socket.errorString());
        }
        m_socket.abort();
        return false;
    }
    if (!syncDelimited(error)) {
        disconnect();
        return false;
    }
    return true;
}

void GuestAgentClient::disconnect()
{
    if (m_socket.isOpen()) {
        m_socket.disconnectFromServer();
        if (m_socket.state() != QLocalSocket::UnconnectedState) {
            m_socket.waitForDisconnected(500);
        }
    }
    m_socket.abort();
    m_buffer.clear();
    m_synced = false;
}

bool GuestAgentClient::isConnected() const
{
    return m_socket.isOpen() && m_synced;
}

bool GuestAgentClient::syncDelimited(Error *error)
{
    const int id = static_cast<int>(QRandomGenerator::global()->generate());
    const QJsonObject request{
        {QStringLiteral("execute"), QStringLiteral("guest-sync-delimited")},
        {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("id"), id}}},
    };
    QByteArray payload;
    payload.append(static_cast<char>(0xFF));
    payload.append(QJsonDocument(request).toJson(QJsonDocument::Compact));
    payload.append('\n');
    if (m_socket.write(payload) != payload.size() || !m_socket.waitForBytesWritten(m_commandTimeoutMs)) {
        if (error) {
            *error = agentError(QStringLiteral("Falha ao sincronizar com o qemu-guest-agent."),
                                m_socket.errorString());
        }
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < m_commandTimeoutMs) {
        QJsonObject response;
        if (!readResponse(&response, static_cast<int>(m_commandTimeoutMs - timer.elapsed()),
                          error)) {
            return false;
        }
        if (response.value(QStringLiteral("return")).toInt(-1) == id) {
            m_synced = true;
            return true;
        }
    }
    if (error) {
        *error = timeoutError(QStringLiteral("Timeout ao sincronizar com o qemu-guest-agent."));
    }
    return false;
}

bool GuestAgentClient::readResponse(QJsonObject *response, int timeoutMs, Error *error)
{
    if (response) {
        *response = {};
    }
    QElapsedTimer timer;
    timer.start();
    while (true) {
        const int newline = m_buffer.indexOf('\n');
        if (newline >= 0) {
            QByteArray line = m_buffer.left(newline);
            m_buffer.remove(0, newline + 1);
            while (!line.isEmpty()
                   && (line.at(0) == static_cast<char>(0xFF) || line.at(0) == '\r'
                       || line.at(0) == ' ')) {
                line.remove(0, 1);
            }
            if (line.trimmed().isEmpty()) {
                continue;
            }
            QJsonParseError parseError{};
            const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
            if (parseError.error == QJsonParseError::NoError && document.isObject()) {
                if (response) {
                    *response = document.object();
                }
                return true;
            }
            continue;
        }

        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0) {
            if (error) {
                *error = timeoutError(QStringLiteral("Timeout aguardando o qemu-guest-agent."));
            }
            return false;
        }
        if (!m_socket.waitForReadyRead(remaining)) {
            if (error) {
                *error = m_socket.error() == QLocalSocket::SocketTimeoutError
                             ? timeoutError(QStringLiteral("Timeout aguardando o "
                                                           "qemu-guest-agent."))
                             : agentError(QStringLiteral("Falha ao ler do qemu-guest-agent."),
                                          m_socket.errorString());
            }
            return false;
        }
        m_buffer.append(m_socket.readAll());
    }
}

bool GuestAgentClient::sendCommand(const QString &command, const QJsonObject &arguments,
                                   QJsonObject *response, int timeoutMs, Error *error)
{
    if (error) {
        *error = {};
    }
    if (response) {
        *response = {};
    }
    if (!isConnected() && !connect(error)) {
        return false;
    }

    QJsonObject request{{QStringLiteral("execute"), command}};
    if (!arguments.isEmpty()) {
        request.insert(QStringLiteral("arguments"), arguments);
    }
    QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact);
    payload.append('\n');
    if (m_socket.write(payload) != payload.size()
        || !m_socket.waitForBytesWritten(timeoutMs)) {
        if (error) {
            *error = agentError(QStringLiteral("Falha ao enviar comando ao qemu-guest-agent."),
                                m_socket.errorString());
        }
        disconnect();
        return false;
    }

    QJsonObject reply;
    if (!readResponse(&reply, timeoutMs, error)) {
        return false;
    }
    const QJsonObject agentFailure = reply.value(QStringLiteral("error")).toObject();
    if (!agentFailure.isEmpty()) {
        if (error) {
            QString detail = agentFailure.value(QStringLiteral("desc")).toString();
            const QString className = agentFailure.value(QStringLiteral("class")).toString();
            if (!className.isEmpty()) {
                detail = className + QStringLiteral(": ") + detail;
            }
            *error = agentError(QStringLiteral("O qemu-guest-agent recusou o comando."), detail);
        }
        return false;
    }
    if (response) {
        *response = reply;
    }
    return true;
}

bool GuestAgentClient::ping(Error *error)
{
    QJsonObject response;
    return sendCommand(QStringLiteral("guest-ping"), {}, &response, m_commandTimeoutMs, error);
}

bool GuestAgentClient::getOsInfo(QJsonObject *info, Error *error)
{
    QJsonObject response;
    if (!sendCommand(QStringLiteral("guest-get-osinfo"), {}, &response, m_commandTimeoutMs,
                     error)) {
        return false;
    }
    if (info) {
        *info = response.value(QStringLiteral("return")).toObject();
    }
    return true;
}

bool GuestAgentClient::getInfo(QJsonObject *info, Error *error)
{
    QJsonObject response;
    if (!sendCommand(QStringLiteral("guest-info"), {}, &response, m_commandTimeoutMs, error)) {
        return false;
    }
    if (info) {
        *info = response.value(QStringLiteral("return")).toObject();
    }
    return true;
}

bool GuestAgentClient::networkInterfaces(QJsonArray *interfaces, Error *error)
{
    QJsonObject response;
    if (!sendCommand(QStringLiteral("guest-network-get-interfaces"), {}, &response,
                     m_commandTimeoutMs, error)) {
        return false;
    }
    if (interfaces) {
        *interfaces = response.value(QStringLiteral("return")).toArray();
    }
    return true;
}

bool GuestAgentClient::exec(const QString &path, const QStringList &arguments, int timeoutMs,
                            int *exitCode, QByteArray *stdoutData, QByteArray *stderrData,
                            Error *error)
{
    if (exitCode) {
        *exitCode = -1;
    }
    if (stdoutData) {
        stdoutData->clear();
    }
    if (stderrData) {
        stderrData->clear();
    }
    if (path.isEmpty()) {
        if (error) {
            *error = agentError(QStringLiteral("Comando vazio para o qemu-guest-agent."));
        }
        return false;
    }

    QJsonArray argArray;
    for (const QString &argument : arguments) {
        argArray.append(argument);
    }
    const QJsonObject request{
        {QStringLiteral("path"), path},
        {QStringLiteral("arg"), argArray},
        {QStringLiteral("capture-output"), true},
    };
    QJsonObject response;
    if (!sendCommand(QStringLiteral("guest-exec"), request, &response, m_commandTimeoutMs,
                     error)) {
        return false;
    }
    const int pid = response.value(QStringLiteral("return"))
                        .toObject()
                        .value(QStringLiteral("pid"))
                        .toInt();
    if (pid <= 0) {
        if (error) {
            *error = agentError(QStringLiteral("O qemu-guest-agent não retornou o pid."));
        }
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QJsonObject statusResponse;
        if (!sendCommand(QStringLiteral("guest-exec-status"), argumentsWithPid(pid),
                         &statusResponse, m_commandTimeoutMs, error)) {
            return false;
        }
        const QJsonObject status = statusResponse.value(QStringLiteral("return")).toObject();
        if (status.value(QStringLiteral("exited")).toBool()) {
            if (exitCode) {
                *exitCode = status.value(QStringLiteral("exitcode")).toInt(-1);
            }
            if (stdoutData) {
                *stdoutData = QByteArray::fromBase64(
                    status.value(QStringLiteral("out-data")).toString().toLatin1());
            }
            if (stderrData) {
                *stderrData = QByteArray::fromBase64(
                    status.value(QStringLiteral("err-data")).toString().toLatin1());
            }
            return true;
        }
        if (status.contains(QStringLiteral("signal"))) {
            if (error) {
                *error = agentError(QStringLiteral("O comando no guest foi interrompido."),
                                    QString::number(status.value(QStringLiteral("signal")).toInt()));
            }
            return false;
        }
        QThread::msleep(kPollIntervalMs);
    }
    if (error) {
        *error = timeoutError(QStringLiteral("Timeout ao executar o comando no guest."));
    }
    return false;
}

bool GuestAgentClient::readFile(const QString &path, qint64 maxBytes, QByteArray *data,
                                Error *error)
{
    if (data) {
        data->clear();
    }
    if (path.isEmpty()) {
        if (error) {
            *error = agentError(QStringLiteral("Caminho de arquivo vazio no guest."));
        }
        return false;
    }
    const qint64 limit = qBound<qint64>(0LL, maxBytes, static_cast<qint64>(kMaxFileBytes));

    QJsonObject opened;
    if (!sendCommand(QStringLiteral("guest-file-open"),
                     QJsonObject{{QStringLiteral("path"), path},
                                 {QStringLiteral("mode"), QStringLiteral("r")}},
                     &opened, m_commandTimeoutMs, error)) {
        return false;
    }
    const int handle = opened.value(QStringLiteral("return")).toInt(-1);
    if (handle < 0) {
        if (error) {
            *error = agentError(QStringLiteral("O qemu-guest-agent não abriu o arquivo."), path);
        }
        return false;
    }

    bool ok = true;
    QByteArray buffer;
    while (buffer.size() < limit) {
        const qint64 remaining = limit - buffer.size();
        const int count = static_cast<int>(qMin<qint64>(kFileChunkBytes, remaining));
        QJsonObject chunkResponse;
        if (!sendCommand(QStringLiteral("guest-file-read"),
                         QJsonObject{{QStringLiteral("handle"), handle},
                                     {QStringLiteral("count"), count}},
                         &chunkResponse, m_commandTimeoutMs, error)) {
            ok = false;
            break;
        }
        const QJsonObject chunk = chunkResponse.value(QStringLiteral("return")).toObject();
        const int bytesRead = chunk.value(QStringLiteral("count")).toInt();
        const QByteArray decoded = QByteArray::fromBase64(
            chunk.value(QStringLiteral("buf-b64")).toString().toLatin1());
        buffer.append(decoded);
        if (bytesRead <= 0 || decoded.isEmpty()) {
            break;
        }
    }
    QJsonObject ignored;
    Error closeError;
    if (!sendCommand(QStringLiteral("guest-file-close"),
                     QJsonObject{{QStringLiteral("handle"), handle}}, &ignored,
                     m_commandTimeoutMs, &closeError)) {
        ok = false;
        if (error && error->isEmpty()) {
            *error = closeError;
        }
    }
    if (ok && data) {
        *data = buffer;
    }
    return ok;
}

bool GuestAgentClient::writeFile(const QString &path, const QByteArray &data, Error *error)
{
    if (path.isEmpty()) {
        if (error) {
            *error = agentError(QStringLiteral("Caminho de arquivo vazio no guest."));
        }
        return false;
    }
    if (data.size() > kMaxFileBytes) {
        if (error) {
            *error = agentError(QStringLiteral("Arquivo grande demais para o canal agent."),
                                QString::number(data.size()));
        }
        return false;
    }

    QJsonObject opened;
    if (!sendCommand(QStringLiteral("guest-file-open"),
                     QJsonObject{{QStringLiteral("path"), path},
                                 {QStringLiteral("mode"), QStringLiteral("wb")}},
                     &opened, m_commandTimeoutMs, error)) {
        return false;
    }
    const int handle = opened.value(QStringLiteral("return")).toInt(-1);
    if (handle < 0) {
        if (error) {
            *error = agentError(QStringLiteral("O qemu-guest-agent não abriu o arquivo."), path);
        }
        return false;
    }

    bool ok = true;
    qint64 offset = 0;
    while (offset < data.size()) {
        const QByteArray chunk = data.mid(offset, kFileChunkBytes);
        QJsonObject writeResponse;
        if (!sendCommand(QStringLiteral("guest-file-write"),
                         QJsonObject{{QStringLiteral("handle"), handle},
                                     {QStringLiteral("buf-b64"),
                                      QString::fromLatin1(chunk.toBase64())}},
                         &writeResponse, m_commandTimeoutMs, error)) {
            ok = false;
            break;
        }
        const int written = writeResponse.value(QStringLiteral("return"))
                                .toObject()
                                .value(QStringLiteral("count"))
                                .toInt();
        if (written <= 0) {
            if (error) {
                *error = agentError(QStringLiteral("Falha ao gravar o arquivo no guest."), path);
            }
            ok = false;
            break;
        }
        offset += written;
    }

    QJsonObject ignored;
    Error closeError;
    if (!sendCommand(QStringLiteral("guest-file-close"),
                     QJsonObject{{QStringLiteral("handle"), handle}}, &ignored,
                     m_commandTimeoutMs, &closeError)) {
        ok = false;
        if (error && error->isEmpty()) {
            *error = closeError;
        }
    }
    return ok;
}

} // namespace PodStack
