#include "daemon/RpcServer.h"

#include "core/Logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPointer>
#include <QtConcurrent>

#include <sys/socket.h>
#include <unistd.h>

namespace PodStack {
namespace {
constexpr int kMaxRequestBytes = 8 * 1024 * 1024;
constexpr int kMaxLineBytes = 8 * 1024 * 1024;
constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kInternalError = -32603;
} // namespace

RpcServer::RpcServer(Daemon *daemon, QObject *parent)
    : QObject(parent)
    , m_daemon(daemon)
{
    connect(&m_server, &QLocalServer::newConnection, this, &RpcServer::onNewConnection);
}

RpcServer::~RpcServer()
{
    // Os sockets são filhos do QLocalServer; se ele destruir antes do
    // m_buffers, o sinal disconnected volta para onDisconnected e acessa o
    // hash já liberado. Fecha e desconecta explicitamente primeiro.
    m_server.close();
    const QList<QLocalSocket *> sockets = m_buffers.keys();
    for (QLocalSocket *socket : sockets) {
        QObject::disconnect(socket, nullptr, this, nullptr);
        socket->disconnectFromServer();
    }
    m_buffers.clear();
}

bool RpcServer::systemdListenFd(qintptr *descriptor)
{
    if (!descriptor) {
        return false;
    }
    const QByteArray pidText = qgetenv("LISTEN_PID");
    const QByteArray fdsText = qgetenv("LISTEN_FDS");
    if (pidText.isEmpty() || fdsText.isEmpty()) {
        return false;
    }
    bool pidOk = false;
    bool fdsOk = false;
    const qint64 pid = pidText.toLongLong(&pidOk);
    const int fds = fdsText.toInt(&fdsOk);
    if (!pidOk || !fdsOk || pid != QCoreApplication::applicationPid() || fds < 1) {
        return false;
    }
    // SD_LISTEN_FDS_START; o socket unit entrega um único socket.
    *descriptor = 3;
    return true;
}

bool RpcServer::listen(const QString &socketPath, Error *error)
{
    if (error) {
        *error = {};
    }

    qintptr systemdFd = -1;
    if (systemdListenFd(&systemdFd)) {
        if (!m_server.listen(systemdFd)) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                     QStringLiteral("Não foi possível usar o socket do systemd."),
                                     m_server.errorString());
            }
            return false;
        }
        if (!socketPath.isEmpty() && QFileInfo::exists(socketPath)) {
            QFile::setPermissions(socketPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        }
        qCInfo(lcRpc) << "podstackd usando socket do systemd:" << socketPath;
        return true;
    }

    const QFileInfo info(socketPath);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível criar o diretório do socket."),
                                 info.absolutePath());
        }
        return false;
    }

    QFile::remove(socketPath);
    if (!m_server.listen(socketPath)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível escutar no socket."),
                                 m_server.errorString());
        }
        return false;
    }

    QFile::setPermissions(socketPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    qCInfo(lcRpc) << "podstackd ouvindo em" << socketPath;
    return true;
}

void RpcServer::onNewConnection()
{
    while (m_server.hasPendingConnections()) {
        QLocalSocket *socket = m_server.nextPendingConnection();
        if (!authenticate(socket)) {
            qCWarning(lcRpc) << "conexão rejeitada: UID diferente";
            socket->disconnectFromServer();
            socket->deleteLater();
            continue;
        }

        connect(socket, &QLocalSocket::readyRead, this, &RpcServer::onReadyRead);
        connect(socket, &QLocalSocket::disconnected, this, &RpcServer::onDisconnected);
        m_buffers.insert(socket, {});
        qCInfo(lcRpc) << "cliente conectado";
    }
}

void RpcServer::onReadyRead()
{
    auto *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket) {
        return;
    }

    QByteArray &buffer = m_buffers[socket];
    buffer.append(socket->readAll());
    if (buffer.size() > kMaxRequestBytes) {
        QJsonObject response;
        response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        response.insert(QStringLiteral("id"), QJsonValue::Null);
        QJsonObject errorObject;
        errorObject.insert(QStringLiteral("code"), kInvalidRequest);
        errorObject.insert(QStringLiteral("message"), QStringLiteral("Requisição grande demais."));
        response.insert(QStringLiteral("error"), errorObject);
        send(socket, response);
        socket->disconnectFromServer();
        return;
    }

    int newline = -1;
    while ((newline = buffer.indexOf('\n')) >= 0) {
        const QByteArray line = buffer.left(newline);
        buffer.remove(0, newline + 1);
        if (line.size() > kMaxLineBytes) {
            continue;
        }
        handleLine(socket, line);
    }
}

void RpcServer::onDisconnected()
{
    auto *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket) {
        return;
    }
    m_buffers.remove(socket);
    socket->deleteLater();
    qCInfo(lcRpc) << "cliente desconectado";
}

bool RpcServer::authenticate(QLocalSocket *socket) const
{
#ifdef Q_OS_LINUX
    struct ucred credentials {};
    socklen_t length = sizeof(credentials);
    if (getsockopt(static_cast<int>(socket->socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &credentials, &length)
        != 0) {
        return false;
    }
    return credentials.uid == ::getuid();
#else
    Q_UNUSED(socket);
    return true;
#endif
}

void RpcServer::handleLine(QLocalSocket *socket, const QByteArray &line)
{
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        QJsonObject response;
        response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        response.insert(QStringLiteral("id"), QJsonValue::Null);
        QJsonObject errorObject;
        errorObject.insert(QStringLiteral("code"), kParseError);
        errorObject.insert(QStringLiteral("message"), QStringLiteral("JSON inválido."));
        errorObject.insert(QStringLiteral("data"), parseError.errorString());
        response.insert(QStringLiteral("error"), errorObject);
        send(socket, response);
        return;
    }

    const QJsonObject request = document.object();
    const QJsonValue id = request.value(QStringLiteral("id"));
    const QString method = request.value(QStringLiteral("method")).toString();
    const QJsonObject params = request.value(QStringLiteral("params")).toObject();

    if (method.isEmpty()) {
        QJsonObject response;
        response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        response.insert(QStringLiteral("id"), id);
        QJsonObject errorObject;
        errorObject.insert(QStringLiteral("code"), kInvalidRequest);
        errorObject.insert(QStringLiteral("message"), QStringLiteral("Método ausente."));
        response.insert(QStringLiteral("error"), errorObject);
        send(socket, response);
        return;
    }

    QPointer<QLocalSocket> guard(socket);
    const Daemon::Notify notify = [this, guard](const QString &notificationMethod,
                                                const QJsonObject &notificationParams) {
        if (!guard) {
            return;
        }
        QJsonObject message;
        message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        message.insert(QStringLiteral("method"), notificationMethod);
        message.insert(QStringLiteral("params"), notificationParams);
        const QByteArray data = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';

        QMetaObject::invokeMethod(
            this,
            [guard, data] {
                if (guard && guard->state() == QLocalSocket::ConnectedState) {
                    guard->write(data);
                    guard->flush();
                }
            },
            Qt::QueuedConnection);
    };

    (void)QtConcurrent::run([this, guard, id, method, params, notify] {
        Error error;
        const QJsonObject result = m_daemon->handle(method, params, notify, &error);

        QJsonObject response;
        response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        response.insert(QStringLiteral("id"), id);

        if (!error.isEmpty()) {
            QJsonObject errorObject;
            errorObject.insert(QStringLiteral("code"), kInternalError);
            errorObject.insert(QStringLiteral("message"), error.message);
            errorObject.insert(QStringLiteral("data"), error.toJson());
            response.insert(QStringLiteral("error"), errorObject);
        } else {
            response.insert(QStringLiteral("result"), result);
        }

        QMetaObject::invokeMethod(
            this,
            [this, guard, response] {
                if (guard) {
                    send(guard, response);
                }
            },
            Qt::QueuedConnection);
    });
}

void RpcServer::send(QLocalSocket *socket, const QJsonObject &message)
{
    if (!socket || socket->state() != QLocalSocket::ConnectedState) {
        return;
    }
    socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    socket->flush();
}

} // namespace PodStack
