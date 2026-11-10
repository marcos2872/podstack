// SPDX-License-Identifier: GPL-3.0-or-later

#include "MockQmpServer.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>

#include <chrono>
#include <future>
#include <utility>

namespace {

void sendJson(QLocalSocket *socket, const QJsonObject &message)
{
    socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    socket->flush();
}

} // namespace

MockQmpServer::MockQmpServer()
{
    static std::atomic<int> sequence{0};
    m_path = QDir::tempPath()
             + QStringLiteral("/podstack-qmp-%1-%2.sock")
                   .arg(QCoreApplication::applicationPid())
                   .arg(++sequence);

    std::promise<bool> ready;
    std::future<bool> listening = ready.get_future();

    m_thread = QThread::create([this, promise = std::move(ready)]() mutable {
        QLocalServer server;
        QLocalServer::removeServer(m_path);
        const bool ok = server.listen(m_path);
        promise.set_value(ok);
        if (!ok) {
            return;
        }

        QObject::connect(&server, &QLocalServer::newConnection, &server, [this, &server] {
            while (QLocalSocket *socket = server.nextPendingConnection()) {
                {
                    QMutexLocker locker(&m_mutex);
                    ++m_connections;
                }

                if (!noGreeting()) {
                    QJsonObject version;
                    version.insert(QStringLiteral("qemu"),
                                   QJsonObject{{QStringLiteral("major"), 9},
                                               {QStringLiteral("minor"), 0},
                                               {QStringLiteral("micro"), 0}});
                    QJsonObject qmp;
                    qmp.insert(QStringLiteral("version"), version);
                    qmp.insert(QStringLiteral("capabilities"), QJsonArray{});
                    QJsonObject greeting;
                    greeting.insert(QStringLiteral("QMP"), qmp);
                    sendJson(socket, greeting);
                }

                auto process = [this, socket] {
                    QByteArray buffer =
                        socket->property("podstackQmpBuffer").toByteArray();
                    buffer += socket->readAll();
                    while (true) {
                        const int newline = buffer.indexOf('\n');
                        if (newline < 0) {
                            break;
                        }
                        QByteArray line = buffer.left(newline);
                        buffer.remove(0, newline + 1);
                        if (line.endsWith('\r')) {
                            line.chop(1);
                        }
                        if (line.trimmed().isEmpty()) {
                            continue;
                        }
                        handleLine(socket, line);
                    }
                    socket->setProperty("podstackQmpBuffer", buffer);
                };

                QObject::connect(socket, &QLocalSocket::readyRead, socket, process);
                QObject::connect(socket, &QLocalSocket::disconnected, socket,
                                 &QObject::deleteLater);
                if (socket->bytesAvailable() > 0) {
                    process();
                }
            }
        });

        QEventLoop loop;
        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, &loop, [this, &loop] {
            if (m_stop.load()) {
                loop.quit();
            }
        });
        timer.start(20);
        loop.exec();

        server.close();
        QLocalServer::removeServer(m_path);
    });
    m_thread->start();

    if (listening.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
        m_listening = listening.get();
    }
}

MockQmpServer::~MockQmpServer()
{
    m_stop.store(true);
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
    QLocalServer::removeServer(m_path);
}

void MockQmpServer::setStatus(const QString &status)
{
    QMutexLocker locker(&m_mutex);
    m_status = status;
}

void MockQmpServer::setFailCommand(const QString &command)
{
    QMutexLocker locker(&m_mutex);
    m_failCommand = command;
}

void MockQmpServer::setSilentCommand(const QString &command)
{
    QMutexLocker locker(&m_mutex);
    m_silentCommand = command;
}

void MockQmpServer::setInterleavedEvent(const QString &command)
{
    QMutexLocker locker(&m_mutex);
    m_interleavedEvent = command;
}

void MockQmpServer::setNoGreeting(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_noGreeting = enabled;
}

void MockQmpServer::setQuitSendsReturn(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_quitSendsReturn = enabled;
}

int MockQmpServer::commandCount(const QString &command) const
{
    QMutexLocker locker(&m_mutex);
    return m_commandCounts.value(command, 0);
}

int MockQmpServer::connectionCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_connections;
}

MockQmpServer::Behavior MockQmpServer::behaviorFor(const QString &command)
{
    QMutexLocker locker(&m_mutex);
    ++m_commandCounts[command];

    Behavior behavior;
    behavior.status = m_status;
    behavior.fail = !m_failCommand.isEmpty() && command == m_failCommand;
    behavior.silent = !m_silentCommand.isEmpty() && command == m_silentCommand;
    behavior.interleave = !m_interleavedEvent.isEmpty() && command == m_interleavedEvent;
    behavior.quitReturn = m_quitSendsReturn;
    return behavior;
}

bool MockQmpServer::noGreeting() const
{
    QMutexLocker locker(&m_mutex);
    return m_noGreeting;
}

void MockQmpServer::handleLine(QLocalSocket *socket, const QByteArray &line)
{
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }

    const QJsonObject request = document.object();
    const QString command = request.value(QStringLiteral("execute")).toString();
    const QJsonValue id = request.value(QStringLiteral("id"));
    const Behavior behavior = behaviorFor(command);

    if (command == QStringLiteral("quit")) {
        QJsonObject data;
        data.insert(QStringLiteral("guest"), false);
        data.insert(QStringLiteral("reason"), QStringLiteral("host-qmp-quit"));
        QJsonObject event;
        event.insert(QStringLiteral("event"), QStringLiteral("SHUTDOWN"));
        event.insert(QStringLiteral("data"), data);
        sendJson(socket, event);

        if (behavior.quitReturn) {
            QJsonObject response;
            response.insert(QStringLiteral("return"), QJsonObject{});
            if (!id.isUndefined()) {
                response.insert(QStringLiteral("id"), id);
            }
            sendJson(socket, response);
        }

        socket->disconnectFromServer();
        return;
    }

    if (behavior.silent) {
        return;
    }

    if (behavior.fail) {
        QJsonObject errorObject;
        errorObject.insert(QStringLiteral("class"), QStringLiteral("GenericError"));
        errorObject.insert(QStringLiteral("desc"),
                           QStringLiteral("falha simulada pelo MockQmpServer"));
        QJsonObject response;
        response.insert(QStringLiteral("error"), errorObject);
        if (!id.isUndefined()) {
            response.insert(QStringLiteral("id"), id);
        }
        sendJson(socket, response);
        return;
    }

    if (behavior.interleave) {
        QJsonObject event;
        event.insert(QStringLiteral("event"), QStringLiteral("RESUME"));
        event.insert(QStringLiteral("data"), QJsonObject{});
        sendJson(socket, event);
    }

    QJsonObject returnValue;
    if (command == QStringLiteral("query-status")) {
        returnValue.insert(QStringLiteral("status"), behavior.status);
    }

    QJsonObject response;
    response.insert(QStringLiteral("return"), returnValue);
    if (!id.isUndefined()) {
        response.insert(QStringLiteral("id"), id);
    }
    sendJson(socket, response);
}
