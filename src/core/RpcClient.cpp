#include "core/RpcClient.h"

#include "core/Paths.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocalSocket>

#include <utility>

namespace PodStack {

RpcClient::RpcClient(QString socketPath)
    : m_socketPath(socketPath.isEmpty() ? Paths::controlSocket() : std::move(socketPath))
{
}

QJsonObject RpcClient::call(const QString &method, const QJsonObject &params, Error *error,
                            int timeoutMs)
{
    if (error) {
        *error = {};
    }

    QLocalSocket socket;
    socket.connectToServer(m_socketPath, QIODevice::ReadWrite);
    if (!socket.waitForConnected(timeoutMs)) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_DAEMON_UNAVAILABLE"),
                QStringLiteral("Não foi possível conectar ao podstackd."),
                socket.errorString(),
                QStringLiteral("Inicie com: systemctl --user start podstack.socket"));
        }
        return {};
    }

    QJsonObject request;
    request.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    request.insert(QStringLiteral("id"), m_nextId++);
    request.insert(QStringLiteral("method"), method);
    request.insert(QStringLiteral("params"), params);

    const QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    if (socket.write(payload) != payload.size() || !socket.waitForBytesWritten(timeoutMs)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Falha ao enviar a requisição ao podstackd."),
                                 socket.errorString());
        }
        return {};
    }

    QElapsedTimer timer;
    timer.start();
    QByteArray buffer;

    while (true) {
        const int newline = buffer.indexOf('\n');
        if (newline >= 0) {
            const QByteArray line = buffer.left(newline);
            buffer.remove(0, newline + 1);

            QJsonParseError parseError{};
            const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                if (error) {
                    *error = Error::make(QStringLiteral("PODSTACK_INVALID_RESPONSE"),
                                         QStringLiteral("Resposta inválida do podstackd."),
                                         parseError.errorString());
                }
                return {};
            }

            const QJsonObject response = document.object();
            const QJsonObject errorObject = response.value(QStringLiteral("error")).toObject();
            if (!errorObject.isEmpty()) {
                if (error) {
                    const QJsonObject data =
                        errorObject.value(QStringLiteral("data")).toObject();
                    const QJsonValue codeValue = errorObject.value(QStringLiteral("code"));
                    error->code = data.value(QStringLiteral("code")).toString();
                    if (error->code.isEmpty()) {
                        error->code = codeValue.isDouble()
                                          ? QString::number(codeValue.toInt())
                                          : codeValue.toString();
                    }
                    error->message = errorObject.value(QStringLiteral("message")).toString();
                    error->detail = data.value(QStringLiteral("detail")).toString();
                    error->remediation = data.value(QStringLiteral("remediation")).toString();
                    error->retryable = data.value(QStringLiteral("retryable")).toBool();
                }
                return {};
            }
            return response.value(QStringLiteral("result")).toObject();
        }

        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_TIMEOUT"),
                                     QStringLiteral("Tempo esgotado aguardando o podstackd."));
            }
            return {};
        }

        socket.waitForReadyRead(remaining);
        buffer.append(socket.readAll());
    }
}

} // namespace PodStack
