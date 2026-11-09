// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/SingleInstance.h"

#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

#include <sys/socket.h>
#include <unistd.h>

namespace PodStack {
namespace {

constexpr int kConnectTimeoutMs = 400;
constexpr int kCommandTimeoutMs = 600;
constexpr int kMaxCommandBytes = 4096;

bool peerIsOwner(QLocalSocket *socket)
{
    if (!socket) {
        return false;
    }
    const qintptr descriptor = socket->socketDescriptor();
    if (descriptor < 0) {
        return false;
    }

    struct ucred credentials {};
    socklen_t length = sizeof(credentials);
    if (::getsockopt(static_cast<int>(descriptor), SOL_SOCKET, SO_PEERCRED, &credentials,
                     &length)
        != 0) {
        return false;
    }
    return credentials.uid == ::getuid();
}

} // namespace

QString SingleInstance::socketPath()
{
    return Paths::runtimeDir() + QStringLiteral("/gui.sock");
}

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{
}

SingleInstance::~SingleInstance()
{
    stop();
}

bool SingleInstance::start(const QString &command, QString *error)
{
    if (m_primary) {
        return true;
    }

    const QString path = socketPath();

    QLocalSocket probe;
    probe.connectToServer(path);
    if (probe.waitForConnected(kConnectTimeoutMs)) {
        if (!command.isEmpty()) {
            const QJsonObject request{{QStringLiteral("command"), command}};
            const QByteArray payload =
                QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
            probe.write(payload);
            probe.flush();
            probe.waitForBytesWritten(kCommandTimeoutMs);
        }
        probe.disconnectFromServer();
        return false;
    }

    // Nenhuma instância viva: remove socket órfão e assume a primazia.
    QLocalServer::removeServer(path);
    if (!QDir().mkpath(Paths::runtimeDir())) {
        if (error) {
            *error = QStringLiteral("Não foi possível criar %1.").arg(Paths::runtimeDir());
        }
        return false;
    }
    QFile::setPermissions(Paths::runtimeDir(), QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                   | QFileDevice::ExeOwner);

    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_server, &QLocalServer::newConnection, this, &SingleInstance::handleNewConnection);
    if (!m_server->listen(path)) {
        if (error) {
            *error = QStringLiteral("Não foi possível escutar em %1: %2")
                         .arg(path, m_server->errorString());
        }
        delete m_server;
        m_server = nullptr;
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    m_primary = true;
    return true;
}

void SingleInstance::stop()
{
    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
    if (m_primary) {
        QLocalServer::removeServer(socketPath());
        m_primary = false;
    }
}

void SingleInstance::handleNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QLocalSocket *socket = m_server->nextPendingConnection();
        if (!socket) {
            continue;
        }
        if (!peerIsOwner(socket)) {
            socket->abort();
            socket->deleteLater();
            continue;
        }

        auto *timeout = new QTimer(socket);
        timeout->setSingleShot(true);
        connect(timeout, &QTimer::timeout, socket, [socket]() { socket->abort(); });
        timeout->start(kCommandTimeoutMs * 2);

        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            if (socket->property("podstackHandled").toBool()) {
                return;
            }
            QByteArray buffer = socket->property("podstackBuffer").toByteArray();
            buffer += socket->readAll();
            socket->setProperty("podstackBuffer", buffer);

            const int newline = buffer.indexOf('\n');
            if (newline < 0 && buffer.size() < kMaxCommandBytes) {
                return;
            }

            socket->setProperty("podstackHandled", true);
            const QByteArray line = newline >= 0 ? buffer.left(newline) : buffer;
            const QJsonObject request = QJsonDocument::fromJson(line).object();
            const QString command = request.value(QStringLiteral("command")).toString();

            if (command == QStringLiteral("quit")) {
                emit quitRequested();
            } else {
                emit showRequested();
            }

            socket->write("{\"ok\":true}\n");
            socket->flush();
            socket->disconnectFromServer();
        });

        connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
    }
}

} // namespace PodStack
