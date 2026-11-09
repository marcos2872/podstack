// SPDX-License-Identifier: GPL-3.0-or-later

#include "MockHttpServer.h"

#include <QEventLoop>
#include <QHostAddress>
#include <QMutexLocker>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <future>
#include <utility>

namespace {

QByteArray reasonPhrase(int status)
{
    switch (status) {
    case 200:
        return QByteArrayLiteral("OK");
    case 204:
        return QByteArrayLiteral("No Content");
    case 404:
        return QByteArrayLiteral("Not Found");
    case 500:
        return QByteArrayLiteral("Internal Server Error");
    default:
        return QByteArrayLiteral("Error");
    }
}

} // namespace

MockHttpServer::MockHttpServer()
{
    std::promise<quint16> ready;
    std::future<quint16> port = ready.get_future();

    m_thread = QThread::create([this, promise = std::move(ready)]() mutable {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, 0)) {
            promise.set_value(0);
            return;
        }
        promise.set_value(server.serverPort());

        QObject::connect(&server, &QTcpServer::newConnection, &server, [this, &server] {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                auto process = [this, socket] {
                    QByteArray buffer = socket->property("podstackBuffer").toByteArray();
                    buffer += socket->readAll();
                    if (!buffer.contains("\r\n\r\n")) {
                        socket->setProperty("podstackBuffer", buffer);
                        return;
                    }

                    const int lineEnd = buffer.indexOf("\r\n");
                    const QList<QByteArray> requestLine =
                        buffer.left(lineEnd).split(' ');
                    QString path = requestLine.size() >= 2
                                       ? QString::fromLatin1(requestLine.at(1))
                                       : QStringLiteral("/");
                    const int query = path.indexOf(QLatin1Char('?'));
                    if (query >= 0) {
                        path.truncate(query);
                    }

                    const Response response = responseFor(path);
                    countRequest(path);

                    QByteArray header = QByteArrayLiteral("HTTP/1.1 ")
                                        + QByteArray::number(response.status) + ' '
                                        + reasonPhrase(response.status) + QByteArrayLiteral("\r\n")
                                        + QByteArrayLiteral("Content-Type: "
                                                            "application/octet-stream\r\n")
                                        + QByteArrayLiteral("Content-Length: ")
                                        + QByteArray::number(response.body.size())
                                        + QByteArrayLiteral("\r\nConnection: close\r\n\r\n");
                    socket->write(header);
                    socket->write(response.body);
                    socket->flush();
                    socket->disconnectFromHost();
                };

                QObject::connect(socket, &QTcpSocket::readyRead, socket, process);
                QObject::connect(socket, &QTcpSocket::disconnected, socket,
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
    });
    m_thread->start();

    if (port.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
        m_port = port.get();
    }
}

MockHttpServer::~MockHttpServer()
{
    m_stop.store(true);
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
}

void MockHttpServer::setResponse(const QString &path, int status, const QByteArray &body)
{
    QMutexLocker locker(&m_mutex);
    m_responses.insert(path, Response{status, body});
}

QString MockHttpServer::url(const QString &path) const
{
    return QStringLiteral("http://127.0.0.1:%1%2").arg(m_port).arg(path);
}

int MockHttpServer::requestCount(const QString &path) const
{
    QMutexLocker locker(&m_mutex);
    return m_requests.value(path, 0);
}

int MockHttpServer::totalRequests() const
{
    QMutexLocker locker(&m_mutex);
    return m_totalRequests;
}

MockHttpServer::Response MockHttpServer::responseFor(const QString &path) const
{
    QMutexLocker locker(&m_mutex);
    return m_responses.value(path, Response{404, {}});
}

void MockHttpServer::countRequest(const QString &path)
{
    QMutexLocker locker(&m_mutex);
    ++m_requests[path];
    ++m_totalRequests;
}
