#include "MockPodmanServer.h"

#include <QByteArray>
#include <QDir>
#include <QString>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>

namespace {

QByteArray httpResponse(int status, const QByteArray &reason, const QByteArray &body,
                        const QByteArray &extraHeaders = {})
{
    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
    response += "Content-Type: application/json\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n";
    response += extraHeaders;
    response += "\r\n";
    response += body;
    return response;
}

} // namespace

MockPodmanServer::MockPodmanServer()
{
    m_path = QDir::tempPath() + QStringLiteral("/podstack-mock-%1.sock").arg(::getpid());
    ::unlink(m_path.toUtf8().constData());

    m_listenFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_listenFd < 0) {
        return;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const QByteArray pathBytes = m_path.toUtf8();
    std::strncpy(address.sun_path, pathBytes.constData(), sizeof(address.sun_path) - 1);

    if (::bind(m_listenFd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0
        || ::listen(m_listenFd, 16) != 0) {
        ::close(m_listenFd);
        m_listenFd = -1;
        return;
    }

    if (::pipe(m_stopPipe) != 0) {
        ::close(m_listenFd);
        m_listenFd = -1;
        return;
    }

    m_thread = std::thread(&MockPodmanServer::run, this);
}

MockPodmanServer::~MockPodmanServer()
{
    m_stopping.store(true);
    if (m_stopPipe[1] >= 0) {
        [[maybe_unused]] const ssize_t written = ::write(m_stopPipe[1], "x", 1);
    }
    if (m_listenFd >= 0) {
        ::shutdown(m_listenFd, SHUT_RDWR);
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
    if (m_listenFd >= 0) {
        ::close(m_listenFd);
    }
    if (m_stopPipe[0] >= 0) {
        ::close(m_stopPipe[0]);
    }
    if (m_stopPipe[1] >= 0) {
        ::close(m_stopPipe[1]);
    }
    ::unlink(m_path.toUtf8().constData());
}

QString MockPodmanServer::lastMethod() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastMethod;
}

QString MockPodmanServer::lastPath() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastPath;
}

void MockPodmanServer::run()
{
    while (!m_stopping.load()) {
        const int clientFd = ::accept(m_listenFd, nullptr, nullptr);
        if (clientFd < 0) {
            break;
        }
        handleClient(clientFd);
        ::close(clientFd);
    }
}

void MockPodmanServer::handleClient(int clientFd)
{
    QByteArray request;
    char buffer[4096];

    while (!request.contains("\r\n\r\n")) {
        const ssize_t received = ::read(clientFd, buffer, sizeof(buffer));
        if (received <= 0) {
            return;
        }
        request.append(buffer, static_cast<int>(received));
    }

    const int headerEnd = request.indexOf("\r\n\r\n");
    const QByteArray headers = request.left(headerEnd);
    int contentLength = 0;
    for (const QByteArray &line : headers.split('\n')) {
        if (line.toLower().startsWith("content-length:")) {
            contentLength = line.mid(15).trimmed().toInt();
            break;
        }
    }

    QByteArray body = request.mid(headerEnd + 4);
    while (body.size() < contentLength) {
        const ssize_t received = ::read(clientFd, buffer, sizeof(buffer));
        if (received <= 0) {
            break;
        }
        body.append(buffer, static_cast<int>(received));
    }

    const QByteArray requestLine = headers.left(headers.indexOf("\r\n"));
    const QList<QByteArray> parts = requestLine.split(' ');
    const QString method = parts.size() > 0 ? QString::fromLatin1(parts.at(0)) : QString();
    const QString path = parts.size() > 1 ? QString::fromLatin1(parts.at(1)) : QString();
    const QString pathOnly = path.section(QLatin1Char('?'), 0, 0);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastMethod = method;
        m_lastPath = path;
    }

    QByteArray response;
    if (pathOnly == QStringLiteral("/_ping")) {
        response = httpResponse(200, "OK", "OK",
                                "Libpod-Api-Version: 5.8.9\r\nApi-Version: 1.44\r\n");
    } else if (pathOnly.contains(QStringLiteral("/containers/fail/"))) {
        response = httpResponse(500, "Internal Server Error",
                                R"({"message":"forced failure","cause":"mock"})");
    } else if (pathOnly.contains(QStringLiteral("/containers/missing/"))) {
        response = httpResponse(404, "Not Found",
                                R"({"message":"no such container","cause":"mock"})");
    } else if (pathOnly.contains(QStringLiteral("/libpod/containers/json"))) {
        const QByteArray payload = R"([{
            "Id": "abc123",
            "Names": ["/demo"],
            "Image": "docker.io/library/alpine:3.20",
            "State": "running",
            "Status": "Up 1 minute",
            "Created": "2026-01-01T00:00:00Z",
            "Ports": [{"HostIp": "0.0.0.0", "HostPort": 8080, "ContainerPort": 80, "Protocol": "tcp"}],
            "Labels": {"dev.podstack.test": "1"}
        }])";
        response = httpResponse(200, "OK", payload);
    } else if (method == QStringLiteral("POST") && pathOnly.endsWith(QStringLiteral("/exec"))) {
        response = httpResponse(201, "Created", R"({"Id":"exec-1"})");
    } else if (method == QStringLiteral("POST")
               && pathOnly.endsWith(QStringLiteral("/exec/exec-1/start"))) {
        response = QByteArrayLiteral("HTTP/1.1 101 UPGRADED\r\n"
                                     "Content-Type: application/vnd.docker.raw-stream\r\n"
                                     "Connection: Upgrade\r\n"
                                     "Upgrade: tcp\r\n\r\n"
                                     "mock-exec-output\r\n");
    } else if (pathOnly.endsWith(QStringLiteral("/exec/exec-1/json"))) {
        response = httpResponse(200, "OK", R"({"ExitCode":0})");
    } else if (pathOnly.contains(QStringLiteral("/containers/abc123/json"))) {
        response = httpResponse(200, "OK", R"({
            "Id":"abc123",
            "Name":"/demo",
            "State":{"Status":"running","Running":true,"Pid":1,"ExitCode":0},
            "Config":{"Image":"docker.io/library/alpine:3.20","Env":["A=B"]},
            "NetworkSettings":{"IPAddress":"10.0.0.2"}
        })");
    } else if (pathOnly.contains(QStringLiteral("/logs"))) {
        const auto frame = [](const QByteArray &payload) {
            QByteArray out;
            out.append(char(1));
            out.append(3, '\0');
            const quint32 length = static_cast<quint32>(payload.size());
            out.append(char((length >> 24) & 0xff));
            out.append(char((length >> 16) & 0xff));
            out.append(char((length >> 8) & 0xff));
            out.append(char(length & 0xff));
            out.append(payload);
            return out;
        };
        response = httpResponse(200, "OK", frame("line-one\n") + frame("line-two\n"));
    } else if (pathOnly.contains(QStringLiteral("/events"))) {
        response = httpResponse(
            200, "OK",
            QByteArrayLiteral(R"({"Type":"container","Action":"start","Actor":{"ID":"abc123"}})"
                              "\n"));
    } else if (method == QStringLiteral("POST")
               && (pathOnly.endsWith(QStringLiteral("/stop"))
                   || pathOnly.endsWith(QStringLiteral("/restart"))
                   || pathOnly.endsWith(QStringLiteral("/start"))
                   || pathOnly.endsWith(QStringLiteral("/pause"))
                   || pathOnly.endsWith(QStringLiteral("/unpause"))
                   || pathOnly.endsWith(QStringLiteral("/kill")))) {
        response = httpResponse(204, "No Content", {});
    } else if (method == QStringLiteral("DELETE")
               && pathOnly.contains(QStringLiteral("/containers/"))) {
        response = httpResponse(204, "No Content", {});
    } else {
        response = httpResponse(404, "Not Found",
                                R"({"message":"not found","cause":"mock"})");
    }

    [[maybe_unused]] const ssize_t written = ::write(clientFd, response.constData(), response.size());
}
