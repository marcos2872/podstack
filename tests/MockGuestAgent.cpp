// SPDX-License-Identifier: GPL-3.0-or-later

#include "MockGuestAgent.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>

#include <future>
#include <utility>

namespace {

void sendJson(QLocalSocket *socket, const QJsonObject &object, bool syncPrefix = false)
{
    QByteArray payload;
    if (syncPrefix) {
        payload.append(static_cast<char>(0xFF));
    }
    payload.append(QJsonDocument(object).toJson(QJsonDocument::Compact));
    payload.append('\n');
    socket->write(payload);
    socket->flush();
}

} // namespace

MockGuestAgent::MockGuestAgent()
    : MockGuestAgent(QString())
{
}

MockGuestAgent::MockGuestAgent(QString socketPath)
{
    m_path = socketPath.isEmpty()
                 ? QStringLiteral("/tmp/podstack-mock-ga-%1.sock")
                       .arg(reinterpret_cast<quintptr>(this))
                 : std::move(socketPath);

    std::promise<bool> ready;
    std::future<bool> listening = ready.get_future();

    m_thread = QThread::create([this, promise = std::move(ready)]() mutable {
        QLocalServer::removeServer(m_path);
        QLocalServer server;
        if (!server.listen(m_path)) {
            promise.set_value(false);
            return;
        }
        promise.set_value(true);

        QObject::connect(&server, &QLocalServer::newConnection, &server, [this, &server] {
            while (QLocalSocket *socket = server.nextPendingConnection()) {
                auto process = [this, socket] {
                    QByteArray buffer = socket->property("podstackBuffer").toByteArray();
                    buffer += socket->readAll();
                    int newline = buffer.indexOf('\n');
                    while (newline >= 0) {
                        const QByteArray line = buffer.left(newline);
                        buffer.remove(0, newline + 1);
                        handleLine(socket, line);
                        newline = buffer.indexOf('\n');
                    }
                    socket->setProperty("podstackBuffer", buffer);
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

MockGuestAgent::~MockGuestAgent()
{
    m_stop.store(true);
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
}

void MockGuestAgent::setOsInfo(const QJsonObject &info)
{
    QMutexLocker locker(&m_mutex);
    m_osInfo = info;
}

void MockGuestAgent::setFileContent(const QByteArray &data)
{
    QMutexLocker locker(&m_mutex);
    m_fileContent = data;
}

void MockGuestAgent::setFailCommand(const QString &command)
{
    QMutexLocker locker(&m_mutex);
    m_failCommand = command;
}

void MockGuestAgent::setFailCommandOnce(const QString &command)
{
    QMutexLocker locker(&m_mutex);
    m_failOnce = command;
}

void MockGuestAgent::setSilentCommand(const QString &command)
{
    QMutexLocker locker(&m_mutex);
    m_silentCommand = command;
}

void MockGuestAgent::setExecSignal(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_execSignal = enabled;
}

void MockGuestAgent::setExecPid(int pid)
{
    QMutexLocker locker(&m_mutex);
    m_execPid = pid;
}

void MockGuestAgent::setExecOutput(const QByteArray &output)
{
    QMutexLocker locker(&m_mutex);
    m_execOutput = output;
}

void MockGuestAgent::setFileOpenError(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_fileOpenError = enabled;
}

void MockGuestAgent::setFileWriteCount(int count)
{
    QMutexLocker locker(&m_mutex);
    m_fileWriteCount = count;
}

QByteArray MockGuestAgent::writtenData() const
{
    QMutexLocker locker(&m_mutex);
    return m_writtenData;
}

int MockGuestAgent::requestCount(const QString &command) const
{
    QMutexLocker locker(&m_mutex);
    return m_counts.value(command, 0);
}

QStringList MockGuestAgent::commands() const
{
    QMutexLocker locker(&m_mutex);
    return m_commands;
}

QJsonObject MockGuestAgent::osInfo() const
{
    QMutexLocker locker(&m_mutex);
    if (!m_osInfo.isEmpty()) {
        return m_osInfo;
    }
    return QJsonObject{
        {QStringLiteral("name"), QStringLiteral("Microsoft Windows")},
        {QStringLiteral("pretty-name"), QStringLiteral("Microsoft Windows 11 Pro")},
        {QStringLiteral("version"), QStringLiteral("10.0.26300")},
        {QStringLiteral("variant"), QStringLiteral("client")},
    };
}

QByteArray MockGuestAgent::fileContent() const
{
    QMutexLocker locker(&m_mutex);
    return m_fileContent;
}

void MockGuestAgent::handleLine(QLocalSocket *socket, const QByteArray &line)
{
    QByteArray trimmed = line;
    while (!trimmed.isEmpty() && trimmed.at(0) == static_cast<char>(0xFF)) {
        trimmed.remove(0, 1);
    }
    const QJsonDocument document = QJsonDocument::fromJson(trimmed);
    if (!document.isObject()) {
        return;
    }
    const QJsonObject request = document.object();
    const QString command = request.value(QStringLiteral("execute")).toString();
    const QJsonObject arguments = request.value(QStringLiteral("arguments")).toObject();
    {
        QMutexLocker locker(&m_mutex);
        ++m_counts[command];
        m_commands.append(command);
    }
    QString failCommand;
    QString failOnce;
    QString silentCommand;
    bool execSignal = false;
    int execPid = 1;
    bool fileOpenError = false;
    int fileWriteCount = -1;
    {
        QMutexLocker locker(&m_mutex);
        failCommand = m_failCommand;
        failOnce = m_failOnce;
        silentCommand = m_silentCommand;
        execSignal = m_execSignal;
        execPid = m_execPid;
        fileOpenError = m_fileOpenError;
        fileWriteCount = m_fileWriteCount;
    }
    auto sendError = [socket] {
        sendJson(socket, QJsonObject{{QStringLiteral("error"),
                                      QJsonObject{{QStringLiteral("class"),
                                                   QStringLiteral("GenericError")},
                                                  {QStringLiteral("desc"),
                                                   QStringLiteral("falha simulada")}}}});
    };
    if (!silentCommand.isEmpty() && command == silentCommand) {
        return;
    }
    if (!failCommand.isEmpty() && command == failCommand) {
        sendError();
        return;
    }
    if (!failOnce.isEmpty() && command == failOnce) {
        {
            QMutexLocker locker(&m_mutex);
            m_failOnce.clear();
        }
        sendError();
        return;
    }

    if (command == QStringLiteral("guest-sync-delimited")) {
        sendJson(socket,
                 QJsonObject{{QStringLiteral("return"),
                              arguments.value(QStringLiteral("id")).toInt()}},
                 true);
        return;
    }
    if (command == QStringLiteral("guest-ping")) {
        sendJson(socket, QJsonObject{{QStringLiteral("return"), QJsonObject{}}});
        return;
    }
    if (command == QStringLiteral("guest-get-osinfo")) {
        sendJson(socket, QJsonObject{{QStringLiteral("return"), osInfo()}});
        return;
    }
    if (command == QStringLiteral("guest-info")) {
        sendJson(socket, QJsonObject{{QStringLiteral("return"),
                                      QJsonObject{{QStringLiteral("version"),
                                                   QStringLiteral("9.2.0")}}}});
        return;
    }
    if (command == QStringLiteral("guest-exec")) {
        sendJson(socket,
                 QJsonObject{{QStringLiteral("return"),
                              QJsonObject{{QStringLiteral("pid"), execPid}}}});
        return;
    }
    if (command == QStringLiteral("guest-exec-status")) {
        if (execSignal) {
            sendJson(socket, QJsonObject{{QStringLiteral("return"),
                                          QJsonObject{{QStringLiteral("exited"), false},
                                                      {QStringLiteral("signal"), 9}}}});
            return;
        }
        const QByteArray output = [this] {
            QMutexLocker locker(&m_mutex);
            return m_execOutput;
        }();
        sendJson(socket, QJsonObject{{QStringLiteral("return"),
                                      QJsonObject{{QStringLiteral("exited"), true},
                                                  {QStringLiteral("exitcode"), 0},
                                                  {QStringLiteral("out-data"),
                                                   QString::fromLatin1(output.toBase64())}}}});
        return;
    }
    if (command == QStringLiteral("guest-file-open")) {
        if (fileOpenError) {
            sendError();
            return;
        }
        {
            QMutexLocker locker(&m_mutex);
            m_fileRead = false;
        }
        sendJson(socket,
                 QJsonObject{{QStringLiteral("return"), 7}});
        return;
    }
    if (command == QStringLiteral("guest-file-read")) {
        const QByteArray content = fileContent();
        bool alreadyRead = false;
        {
            QMutexLocker locker(&m_mutex);
            alreadyRead = m_fileRead;
            m_fileRead = true;
        }
        sendJson(socket, QJsonObject{{QStringLiteral("return"),
                                      QJsonObject{{QStringLiteral("count"),
                                                   alreadyRead ? 0 : content.size()},
                                                  {QStringLiteral("buf-b64"),
                                                   alreadyRead
                                                       ? QString()
                                                       : QString::fromLatin1(
                                                             content.toBase64())}}}});
        return;
    }
    if (command == QStringLiteral("guest-file-write")) {
        const QByteArray chunk = QByteArray::fromBase64(
            arguments.value(QStringLiteral("buf-b64")).toString().toLatin1());
        if (fileWriteCount >= 0) {
            sendJson(socket,
                     QJsonObject{{QStringLiteral("return"),
                                  QJsonObject{{QStringLiteral("count"), fileWriteCount}}}});
            return;
        }
        {
            QMutexLocker locker(&m_mutex);
            m_writtenData += chunk;
        }
        sendJson(socket, QJsonObject{{QStringLiteral("return"),
                                      QJsonObject{{QStringLiteral("count"), chunk.size()}}}});
        return;
    }
    if (command == QStringLiteral("guest-file-close")) {
        sendJson(socket, QJsonObject{{QStringLiteral("return"), QJsonObject{}}});
        return;
    }
    if (command == QStringLiteral("guest-network-get-interfaces")) {
        sendJson(socket,
                 QJsonObject{{QStringLiteral("return"),
                              QJsonArray{QJsonObject{
                                  {QStringLiteral("name"), QStringLiteral("eth0")},
                                  {QStringLiteral("ip-addresses"),
                                   QJsonArray{QJsonObject{
                                       {QStringLiteral("ip-address-type"),
                                        QStringLiteral("ipv4")},
                                       {QStringLiteral("ip-address"),
                                        QStringLiteral("10.0.2.15")}}}}}}}});
        return;
    }
    sendJson(socket, QJsonObject{{QStringLiteral("return"), QJsonObject{}}});
}
