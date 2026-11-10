#include "core/RpcStream.h"

#include "core/Paths.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocalSocket>
#include <QPair>
#include <QVector>

#include <utility>

namespace PodStack {
namespace {

void fillErrorFromObject(const QJsonObject &object, Error *error)
{
    if (!error) {
        return;
    }
    error->code = object.value(QStringLiteral("code")).toString();
    error->message = object.value(QStringLiteral("message")).toString();
    error->detail = object.value(QStringLiteral("detail")).toString();
    error->remediation = object.value(QStringLiteral("remediation")).toString();
    error->retryable = object.value(QStringLiteral("retryable")).toBool();
    if (error->code.isEmpty()) {
        error->code = QStringLiteral("PODSTACK_ENGINE_ERROR");
    }
    if (error->message.isEmpty()) {
        error->message = QStringLiteral("Falha na operação em stream.");
    }
}

} // namespace

RpcStream::RpcStream(QString socketPath)
    : m_socketPath(socketPath.isEmpty() ? Paths::controlSocket() : std::move(socketPath))
{
}

bool RpcStream::run(const QString &method, const QJsonObject &params, const Event &onEvent,
                    Error *error, int timeoutMs, bool requireDone)
{
    if (error) {
        *error = {};
    }

    QLocalSocket socket;
    socket.connectToServer(m_socketPath, QIODevice::ReadWrite);
    if (!socket.waitForConnected(30000)) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_DAEMON_UNAVAILABLE"),
                QStringLiteral("Não foi possível conectar ao podstackd."),
                socket.errorString(),
                QStringLiteral("Inicie com: systemctl --user start podstack.socket"));
        }
        return false;
    }

    QJsonObject request;
    request.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    request.insert(QStringLiteral("id"), 1);
    request.insert(QStringLiteral("method"), method);
    request.insert(QStringLiteral("params"), params);

    const QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    if (socket.write(payload) != payload.size() || !socket.waitForBytesWritten(30000)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Falha ao enviar a requisição ao podstackd."),
                                 socket.errorString());
        }
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    QByteArray buffer;
    bool subscribed = false;
    // O daemon pode emitir o primeiro delta antes da resposta da subscription
    // (o handle roda em outra thread). Guarda e reenvia em ordem para não
    // perder, por exemplo, o tail inicial do console serial.
    QVector<QPair<QString, QJsonObject>> pendingEvents;

    while (true) {
        int newline = -1;
        while ((newline = buffer.indexOf('\n')) >= 0) {
            const QByteArray line = buffer.left(newline);
            buffer.remove(0, newline + 1);

            QJsonParseError parseError{};
            const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                continue;
            }

            const QJsonObject message = document.object();
            const QString messageMethod = message.value(QStringLiteral("method")).toString();

            if (messageMethod.isEmpty()) {
                const QJsonObject errorObject = message.value(QStringLiteral("error")).toObject();
                if (!errorObject.isEmpty()) {
                    if (error) {
                        const QJsonObject data =
                            errorObject.value(QStringLiteral("data")).toObject();
                        fillErrorFromObject(data, error);
                        error->message = errorObject.value(QStringLiteral("message")).toString();
                    }
                    return false;
                }
                subscribed = !message.value(QStringLiteral("result"))
                                  .toObject()
                                  .value(QStringLiteral("subscriptionId"))
                                  .toString()
                                  .isEmpty();
                if (subscribed && !pendingEvents.isEmpty()) {
                    for (const auto &pending : pendingEvents) {
                        if (onEvent) {
                            onEvent(pending.first, pending.second);
                        }
                        if (requireDone && pending.first.endsWith(QStringLiteral(".done"))) {
                            return true;
                        }
                        if (pending.first.endsWith(QStringLiteral(".error"))) {
                            fillErrorFromObject(
                                pending.second.value(QStringLiteral("error")).toObject(),
                                error);
                            return false;
                        }
                    }
                    pendingEvents.clear();
                }
                continue;
            }

            const QJsonObject messageParams = message.value(QStringLiteral("params")).toObject();
            if (!subscribed) {
                pendingEvents.append(qMakePair(messageMethod, messageParams));
                continue;
            }
            if (onEvent) {
                onEvent(messageMethod, messageParams);
            }

            if (requireDone && messageMethod.endsWith(QStringLiteral(".done"))) {
                return true;
            }
            if (messageMethod.endsWith(QStringLiteral(".error"))) {
                fillErrorFromObject(
                    messageParams.value(QStringLiteral("error")).toObject(), error);
                return false;
            }
        }

        if (timeoutMs > 0 && timer.elapsed() > timeoutMs) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_TIMEOUT"),
                                     QStringLiteral("Tempo esgotado no stream."));
            }
            return false;
        }

        const int waitMs = timeoutMs > 0
                               ? qMax(1, timeoutMs - static_cast<int>(timer.elapsed()))
                               : 60000;
        if (socket.state() != QLocalSocket::ConnectedState && socket.bytesAvailable() == 0) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                     QStringLiteral("Conexão com o podstackd foi encerrada."));
            }
            return false;
        }

        if (socket.waitForReadyRead(waitMs)) {
            buffer.append(socket.readAll());
        } else if (socket.bytesAvailable() > 0) {
            buffer.append(socket.readAll());
        }
    }
}

} // namespace PodStack
