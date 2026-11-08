#include "MockPodmanServer.h"

#include <QByteArray>
#include <QDir>
#include <QString>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>

#include <system_error>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

QByteArray tarEntry(const QByteArray &name, const QByteArray &data, char type = '0')
{
    char header[512] = {};
    std::memcpy(header, name.constData(), std::min<int>(name.size(), 100));
    std::snprintf(header + 100, 8, "%07o", 0644);
    std::snprintf(header + 108, 8, "%07o", 0);
    std::snprintf(header + 116, 8, "%07o", 0);
    std::snprintf(header + 124, 12, "%011o", static_cast<unsigned int>(data.size()));
    std::snprintf(header + 136, 12, "%011o", 0);
    std::memset(header + 148, ' ', 8);
    header[156] = type;
    std::memcpy(header + 257, "ustar", 6);
    std::memcpy(header + 263, "00", 2);

    unsigned int checksum = 0;
    for (unsigned char byte : header) {
        checksum += byte;
    }
    std::snprintf(header + 148, 8, "%06o", checksum);
    header[154] = '\0';
    header[155] = ' ';

    QByteArray entry(header, 512);
    entry.append(data);
    const int padding = (512 - (data.size() % 512)) % 512;
    entry.append(QByteArray(padding, '\0'));
    return entry;
}

QByteArray mockTar()
{
    QByteArray tar;
    tar += tarEntry("file.txt", "hello", '0');
    tar += tarEntry("dir/", {}, '5');
    tar.append(QByteArray(1024, '\0'));
    return tar;
}

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

QByteArray MockPodmanServer::lastBody() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastBody;
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
        m_lastBody = body;
    }

    QByteArray response;
    if (pathOnly == QStringLiteral("/_ping")) {
        response = httpResponse(200, "OK", "OK",
                                "Libpod-Api-Version: 5.8.9\r\nApi-Version: 1.44\r\n");
    } else if (method == QStringLiteral("POST")
               && pathOnly.endsWith(QStringLiteral("/containers/create"))) {
        response = httpResponse(201, "Created", R"({"Id":"created123"})");
    } else if (pathOnly.contains(QStringLiteral("/containers/fail/"))) {
        response = httpResponse(500, "Internal Server Error",
                                R"({"message":"forced failure","cause":"mock"})");
    } else if (pathOnly.contains(QStringLiteral("/containers/missing/"))) {
        response = httpResponse(404, "Not Found",
                                R"({"message":"no such container","cause":"mock"})");
    } else if (pathOnly.contains(QStringLiteral("/libpod/containers/json"))) {
        const QByteArray payload = R"JSON([{
            "Id": "abc123",
            "Names": ["/demo"],
            "Image": "docker.io/library/alpine:3.20",
            "State": "running",
            "Status": "Up 1 minute (healthy)",
            "Created": "2026-01-01T00:00:00Z",
            "Ports": [{"HostIp": "0.0.0.0", "HostPort": 8080, "ContainerPort": 80, "Protocol": "tcp"}],
            "Labels": {
                "dev.podstack.test": "1",
                "com.docker.compose.project": "mock-project",
                "com.docker.compose.service": "web",
                "com.docker.compose.project.working_dir": "/tmp/mock-project"
            }
        }])JSON";
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
            "Config":{"Image":"docker.io/library/alpine:3.20","Env":["A=B"],"Cmd":["sleep","10"],"WorkingDir":"/tmp","User":"0:0"},
            "HostConfig":{"PortBindings":{"80/tcp":[{"HostIp":"0.0.0.0","HostPort":"8080"}]}},
            "Mounts":[{"Source":"/tmp","Destination":"/mnt","RW":true}],
            "NetworkSettings":{"IPAddress":"10.0.0.2"}
        })");
    } else if (method == QStringLiteral("PUT")
               && pathOnly.contains(QStringLiteral("/archive"))) {
        response = httpResponse(200, "OK", {});
    } else if (pathOnly.contains(QStringLiteral("/archive"))) {
        response = httpResponse(200, "OK", mockTar());
    } else if (pathOnly.contains(QStringLiteral("/containers/stats"))
               && path.contains(QStringLiteral("containers=rawcgroup"))) {
        response = httpResponse(
            200, "OK",
            R"({"Id":"rawcgroup","name":"raw","cpu_stats":{"cpu_usage":{"total_usage":2000},"system_cpu_usage":4000,"online_cpus":2},"precpu_stats":{"cpu_usage":{"total_usage":1000},"system_cpu_usage":2000},"memory_stats":{"usage":1024,"limit":2048},"networks":{"eth0":{"rx_bytes":10,"tx_bytes":20}},"blkio_stats":{"io_service_bytes_recursive":[{"op":"read","value":30},{"op":"write","value":40}]},"pids_stats":{"current":7}})");
    } else if (pathOnly.contains(QStringLiteral("/containers/stats"))
               && path.contains(QStringLiteral("containers=rawstats"))) {
        response = httpResponse(
            200, "OK",
            R"({"Error":null,"Stats":[{"AvgCPU":0.5,"ContainerID":"rawstats","Name":"raw","MemUsage":1024,"MemLimit":2048,"MemPerc":50.0,"Network":{"eth0":{"RxBytes":10,"TxBytes":20}},"BlockInput":30,"BlockOutput":40,"PIDs":7}]})");
    } else if (pathOnly.contains(QStringLiteral("/stats"))) {
        const QByteArray statsPayload = QByteArrayLiteral(
            "{\"cpu_percent\":\"1.5%\",\"mem_usage\":\"1MB / 1GB\","
            "\"net_io\":\"0B / 0B\",\"block_io\":\"0B / 0B\",\"pids\":\"1\"}\n");
        response = httpResponse(200, "OK", statsPayload);
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
    } else if (pathOnly.contains(QStringLiteral("/libpod/build"))) {
        response = httpResponse(
            200, "OK",
            QByteArrayLiteral("{\"stream\":\"STEP 1/1: FROM alpine:3.20\\n\"}\n"
                              "{\"stream\":\"COMMIT mock-image\\n\"}\n"
                              "{\"aux\":{\"ID\":\"sha256:built123\"}}\n"));
    } else if (pathOnly.contains(QStringLiteral("/events"))) {
        response = httpResponse(
            200, "OK",
            QByteArrayLiteral(R"({"Type":"container","Action":"start","Actor":{"ID":"abc123"}})"
                              "\n"));
    } else if (pathOnly.contains(QStringLiteral("/missing"))) {
        response = httpResponse(404, "Not Found",
                                R"({"message":"not found","cause":"mock"})");
    } else if (pathOnly.contains(QStringLiteral("/libpod/images/json"))) {
        response = httpResponse(
            200, "OK",
            QByteArrayLiteral(R"JSON([{
                "Id":"img1234567890","RepoTags":["docker.io/library/alpine:3.20"],
                "RepoDigests":[],"Created":1700000000,"Size":7000000,
                "Containers":1,"Digest":"sha256:deadbeef","Labels":{"dev.podstack.test":"1"}
            },{
                "Id":"dangling123456","RepoTags":[],"RepoDigests":[],
                "Created":1700000001,"Size":1000,"Containers":0,"Labels":{}
            }])JSON"));
    } else if (pathOnly.contains(QStringLiteral("/libpod/images/img1234567890/json"))
               || pathOnly.contains(QStringLiteral("/libpod/images/img123/json"))) {
        response = httpResponse(200, "OK", R"({
            "Id":"img1234567890","RepoTags":["docker.io/library/alpine:3.20"],
            "Digest":"sha256:deadbeef","Size":7000000,"Architecture":"amd64","Os":"linux"
        })");
    } else if (pathOnly.contains(QStringLiteral("/libpod/images/img1234567890/history"))) {
        response = httpResponse(
            200, "OK",
            R"([{"Id":"img1234567890","Created":1700000000,"CreatedBy":"COPY /app","Comment":"","Size":123}])");
    } else if (method == QStringLiteral("POST")
               && pathOnly.contains(QStringLiteral("/libpod/images/pull"))) {
        if (path.contains(QStringLiteral("fail"))) {
            response = httpResponse(200, "OK",
                                    QByteArrayLiteral("{\"error\":\"pull failed\"}\n"));
        } else {
            response = httpResponse(
                200, "OK",
                QByteArrayLiteral(
                    "{\"stream\":\"Trying to pull...\\n\"}\n"
                    "{\"status\":\"Downloading\",\"progress\":\"50%\",\"id\":\"layer1\"}\n"
                    "{\"images\":[\"img1234567890\"],\"id\":\"img1234567890\"}\n"));
        }
    } else if (method == QStringLiteral("POST")
               && pathOnly.contains(QStringLiteral("/libpod/images/img1234567890/push"))) {
        response = httpResponse(
            200, "OK",
            QByteArrayLiteral("{\"status\":\"Pushing\"}\n"
                              "{\"status\":\"Done\"}\n"));
    } else if (method == QStringLiteral("POST")
               && pathOnly.endsWith(QStringLiteral("/untag"))) {
        response = httpResponse(201, "Created", {});
    } else if (method == QStringLiteral("POST")
               && pathOnly.endsWith(QStringLiteral("/tag"))) {
        response = httpResponse(201, "Created", {});
    } else if (method == QStringLiteral("POST")
               && pathOnly.contains(QStringLiteral("/libpod/images/prune"))) {
        response = httpResponse(200, "OK",
                                R"({"ImagesDeleted":[],"SpaceReclaimed":12345})");
    } else if (pathOnly.endsWith(QStringLiteral("/get"))
               && pathOnly.contains(QStringLiteral("/libpod/images/"))) {
        response = httpResponse(200, "OK", mockTar());
    } else if (method == QStringLiteral("POST")
               && pathOnly.contains(QStringLiteral("/libpod/images/load"))) {
        response = httpResponse(200, "OK", R"({"stream":"Loaded image"})");
    } else if (method == QStringLiteral("DELETE")
               && pathOnly.contains(QStringLiteral("/libpod/images/"))) {
        response = httpResponse(200, "OK", R"([{"Deleted":"img1234567890"}])");
    } else if (pathOnly.contains(QStringLiteral("/libpod/volumes/json"))) {
        response = httpResponse(
            200, "OK",
            R"([{"Name":"vol1","Driver":"local","Mountpoint":"/tmp/podstack-mock-volume","CreatedAt":"2026-01-01T00:00:00Z","Labels":{}}])");
    } else if (pathOnly.contains(QStringLiteral("/libpod/volumes/vol2/json"))) {
        response = httpResponse(
            200, "OK",
            R"({"Name":"vol2","Driver":"local","Mountpoint":"/tmp/podstack-mock-volume2","Labels":{}})");
    } else if (pathOnly.contains(QStringLiteral("/libpod/volumes/vol1/json"))) {
        response = httpResponse(
            200, "OK",
            R"({"Name":"vol1","Driver":"local","Mountpoint":"/tmp/podstack-mock-volume","Labels":{}})");
    } else if (method == QStringLiteral("POST")
               && pathOnly.contains(QStringLiteral("/libpod/volumes/create"))) {
        response = httpResponse(201, "Created",
                                R"({"Name":"vol2","Driver":"local","Mountpoint":"/tmp/podstack-mock-volume2"})");
    } else if (method == QStringLiteral("POST")
               && pathOnly.contains(QStringLiteral("/libpod/volumes/prune"))) {
        response = httpResponse(200, "OK", R"([{"Id":"vol1","SpaceReclaimed":456}])");
    } else if (method == QStringLiteral("DELETE")
               && pathOnly.contains(QStringLiteral("/libpod/volumes/"))) {
        response = httpResponse(204, "No Content", {});
    } else if (pathOnly.contains(QStringLiteral("/libpod/networks/json"))) {
        response = httpResponse(
            200, "OK",
            R"([{"name":"mock-net","id":"net123","driver":"bridge","network_interface":"podman9","created":"2026-01-01T00:00:00Z","subnets":[{"subnet":"10.89.0.0/24","gateway":"10.89.0.1"}],"ipv6_enabled":false,"dns_enabled":true,"internal":false,"labels":{}}])");
    } else if (pathOnly.contains(QStringLiteral("/libpod/networks/mock-net/json"))) {
        response = httpResponse(
            200, "OK",
            R"({"name":"mock-net","id":"net123","driver":"bridge","subnets":[{"subnet":"10.89.0.0/24","gateway":"10.89.0.1"}],"containers":{"abc123":{}}})");
    } else if (method == QStringLiteral("POST")
               && pathOnly.contains(QStringLiteral("/libpod/networks/create"))) {
        response = httpResponse(
            200, "OK",
            R"({"name":"mock-net","id":"net123","driver":"bridge","subnets":[{"subnet":"10.90.0.0/24","gateway":"10.90.0.1"}]})");
    } else if (method == QStringLiteral("POST")
               && (pathOnly.endsWith(QStringLiteral("/connect"))
                   || pathOnly.endsWith(QStringLiteral("/disconnect")))) {
        response = httpResponse(200, "OK", {});
    } else if (method == QStringLiteral("DELETE")
               && pathOnly.contains(QStringLiteral("/libpod/networks/"))) {
        response = httpResponse(200, "OK", {});
    } else if (pathOnly.contains(QStringLiteral("/libpod/pods/json"))) {
        response = httpResponse(
            200, "OK",
            R"([{"Id":"pod123","Name":"demo-pod","Status":"Running","Created":"2026-01-01T00:00:00Z","InfraId":"infra123","Containers":[{"Id":"infra123","Names":"demo-pod-infra","Status":"running"},{"Id":"c1","Names":"demo-pod-c1","Status":"running"}],"Labels":{}}])");
    } else if (method == QStringLiteral("POST")
               && pathOnly.contains(QStringLiteral("/libpod/pods/create"))) {
        response = httpResponse(201, "Created", R"({"Id":"pod123"})");
    } else if (method == QStringLiteral("POST")
               && pathOnly.startsWith(QStringLiteral("/libpod/pods/"))
               && (pathOnly.endsWith(QStringLiteral("/start"))
                   || pathOnly.endsWith(QStringLiteral("/stop"))
                   || pathOnly.endsWith(QStringLiteral("/restart")))) {
        response = httpResponse(200, "OK", {});
    } else if (method == QStringLiteral("DELETE")
               && pathOnly.contains(QStringLiteral("/libpod/pods/"))) {
        response = httpResponse(200, "OK", {});
    } else if (pathOnly.contains(QStringLiteral("/libpod/system/df"))) {
        response = httpResponse(
            200, "OK",
            R"({"ImagesSize":7001000,"Containers":[],"Images":[{"Repository":"docker.io/library/alpine","Tag":"3.20","ImageID":"img1234567890","Size":7000000}],"Volumes":[{"VolumeName":"vol1","Links":1,"Size":2048,"ReclaimableSize":2048}]})");
    } else if (method == QStringLiteral("POST")
               && (pathOnly.endsWith(QStringLiteral("/stop"))
                   || pathOnly.endsWith(QStringLiteral("/restart"))
                   || pathOnly.endsWith(QStringLiteral("/start"))
                   || pathOnly.endsWith(QStringLiteral("/pause"))
                   || pathOnly.endsWith(QStringLiteral("/unpause"))
                   || pathOnly.endsWith(QStringLiteral("/kill"))
                   || pathOnly.endsWith(QStringLiteral("/rename")))) {
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
