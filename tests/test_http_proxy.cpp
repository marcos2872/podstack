// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/HttpProxy.h"

#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

#include <memory>

namespace {

struct BackendRequest {
    QByteArray head;
    QByteArray body;
};

int contentLength(const QByteArray &head)
{
    for (const QByteArray &line : head.split('\n')) {
        if (line.toLower().startsWith("content-length:")) {
            return line.mid(line.indexOf(':') + 1).trimmed().toInt();
        }
    }
    return 0;
}

class BackendServer : public QTcpServer {
public:
    explicit BackendServer(QObject *parent = nullptr) : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                QTcpSocket *socket = nextPendingConnection();
                auto buffer = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                    buffer->append(socket->readAll());
                    const int end = buffer->indexOf("\r\n\r\n");
                    if (end < 0) {
                        return;
                    }
                    const QByteArray head = buffer->left(end + 4);
                    const int length = contentLength(head);
                    if (buffer->size() - head.size() < length) {
                        return;
                    }
                    requests.append({head, buffer->mid(head.size(), length)});
                    socket->write(
                        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    QList<BackendRequest> requests;
};

QByteArray exchange(quint16 port, const QByteArray &request, int timeoutMs = 3000,
                    bool waitForBody = true)
{
    QTcpSocket socket;
    QByteArray response;
    QObject::connect(&socket, &QTcpSocket::readyRead, &socket,
                     [&response, &socket] { response += socket.readAll(); });
    socket.connectToHost(QHostAddress::LocalHost, port);
    if (!socket.waitForConnected(timeoutMs)) {
        return {};
    }
    socket.write(request);

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QTest::qWait(10);
        if (!response.contains("\r\n\r\n")) {
            continue;
        }
        if (!waitForBody) {
            QTest::qWait(30);
            break;
        }
        if (response.endsWith("ok") || response.contains("</html>")) {
            break;
        }
    }
    response += socket.readAll();
    return response;
}

PodStack::HttpProxy::ContainerProvider providerFor(const QString &name, const QStringList &ports)
{
    return [name, ports](QVector<PodStack::ContainerInfo> *containers, PodStack::Error *error) {
        if (error) {
            *error = {};
        }
        PodStack::ContainerInfo info;
        info.name = name;
        info.ports = ports;
        containers->append(info);
        return true;
    };
}

} // namespace

class HttpProxyTest : public QObject {
    Q_OBJECT

private slots:
    void hostNameParsing();
    void publishedPortParsing();
    void listenReportsPortInUse();
    void proxiesGetRequest();
    void forwardsRequestBody();
    void servesIndexForLocalhost();
    void returns404WithIndexForUnknownContainer();
    void returns404ForInvalidHost();
    void indexRequiresGetOrHead();
    void headIndexHasNoBody();
    void indexEscapesContainerContent();
    void returns404WithoutPublishedPort();
    void returns502WhenProviderFails();
    void returns502WhenBackendRefuses();
    void rejectsMalformedRequest();
    void rejectsOversizedHeaders();
    void headHasNoErrorBody();
    void abortsIdleConnection();
};

void HttpProxyTest::hostNameParsing()
{
    QString name;
    QVERIFY(PodStack::HttpProxy::hostName("demo.localhost:8080", &name));
    QCOMPARE(name, QStringLiteral("demo"));
    QVERIFY(PodStack::HttpProxy::hostName("Demo.LocalHost", &name));
    QCOMPARE(name, QStringLiteral("demo"));
    QVERIFY(PodStack::HttpProxy::hostName("web-1.localhost", &name));
    QCOMPARE(name, QStringLiteral("web-1"));

    QVERIFY(!PodStack::HttpProxy::hostName("localhost", &name));
    QVERIFY(!PodStack::HttpProxy::hostName("demo.example.com", &name));
    QVERIFY(!PodStack::HttpProxy::hostName("a.b.localhost", &name));
    QVERIFY(!PodStack::HttpProxy::hostName("-demo.localhost", &name));
    QVERIFY(!PodStack::HttpProxy::hostName("demo-.localhost", &name));
    QVERIFY(!PodStack::HttpProxy::hostName("demo.localhost:abc", &name));
    QVERIFY(!PodStack::HttpProxy::hostName("demo.localhost:0", &name));
    QVERIFY(!PodStack::HttpProxy::hostName(QByteArray(), &name));
    QVERIFY(!PodStack::HttpProxy::hostName("[::1]:8080", &name));

    QVERIFY(PodStack::HttpProxy::isIndexHost(QByteArrayLiteral("localhost")));
    QVERIFY(PodStack::HttpProxy::isIndexHost(QByteArrayLiteral("LOCALHOST:8080")));
    QVERIFY(!PodStack::HttpProxy::isIndexHost(QByteArrayLiteral("demo.localhost")));
    QVERIFY(!PodStack::HttpProxy::isIndexHost(QByteArrayLiteral("localhost:0")));
    QVERIFY(!PodStack::HttpProxy::isIndexHost(QByteArrayLiteral("localhost:abc")));
    QVERIFY(!PodStack::HttpProxy::isIndexHost(QByteArray()));
}

void HttpProxyTest::publishedPortParsing()
{
    PodStack::ContainerInfo info;
    QString host;
    quint16 port = 0;

    info.ports = {QStringLiteral("8080->80/tcp")};
    QVERIFY(PodStack::HttpProxy::publishedPort(info, &host, &port));
    QCOMPARE(host, QStringLiteral("127.0.0.1"));
    QCOMPARE(port, quint16(8080));

    info.ports = {QStringLiteral("127.0.0.1:8081->81/tcp")};
    QVERIFY(PodStack::HttpProxy::publishedPort(info, &host, &port));
    QCOMPARE(host, QStringLiteral("127.0.0.1"));
    QCOMPARE(port, quint16(8081));

    info.ports = {QStringLiteral("192.0.2.10:8082->82/tcp")};
    QVERIFY(PodStack::HttpProxy::publishedPort(info, &host, &port));
    QCOMPARE(host, QStringLiteral("192.0.2.10"));
    QCOMPARE(port, quint16(8082));

    info.ports = {QStringLiteral("[::1]:8083->83/tcp")};
    QVERIFY(PodStack::HttpProxy::publishedPort(info, &host, &port));
    QCOMPARE(host, QStringLiteral("::1"));
    QCOMPARE(port, quint16(8083));

    info.ports = {QStringLiteral("8084->84/udp"), QStringLiteral("8085->85/tcp")};
    QVERIFY(PodStack::HttpProxy::publishedPort(info, &host, &port));
    QCOMPARE(port, quint16(8085));

    info.ports = {QStringLiteral("80/tcp")};
    QVERIFY(!PodStack::HttpProxy::publishedPort(info, &host, &port));
    info.ports = {QStringLiteral("garbage")};
    QVERIFY(!PodStack::HttpProxy::publishedPort(info, &host, &port));
    info.ports = {QStringLiteral("70000->80/tcp")};
    QVERIFY(!PodStack::HttpProxy::publishedPort(info, &host, &port));
    info.ports.clear();
    QVERIFY(!PodStack::HttpProxy::publishedPort(info, &host, &port));
}

void HttpProxyTest::listenReportsPortInUse()
{
    QTcpServer busy;
    QVERIFY(busy.listen(QHostAddress::LocalHost, 0));

    PodStack::HttpProxy proxy(providerFor(QStringLiteral("demo"), {}));
    QCOMPARE(proxy.port(), quint16(0));
    PodStack::Error error;
    QVERIFY(!proxy.listen(busy.serverPort(), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    QCOMPARE(proxy.port(), quint16(0));

    QVERIFY(proxy.listen(0, &error));
    QVERIFY(proxy.port() != 0);
    proxy.close();
    QCOMPARE(proxy.port(), quint16(0));
}

void HttpProxyTest::proxiesGetRequest()
{
    BackendServer backend;
    QVERIFY(backend.listen(QHostAddress::LocalHost, 0));
    const quint16 backendPort = backend.serverPort();

    PodStack::HttpProxy proxy(
        providerFor(QStringLiteral("demo"), {QStringLiteral("%1->80/tcp").arg(backendPort)}));
    QVERIFY(proxy.listen(0));

    const QByteArray response =
        exchange(proxy.port(),
                 "GET /hello?x=1 HTTP/1.1\r\nHost: demo.localhost:8080\r\nAccept: */*\r\n\r\n");
    QVERIFY(response.startsWith("HTTP/1.1 200"));
    QVERIFY(response.endsWith("ok"));

    QTRY_COMPARE_WITH_TIMEOUT(backend.requests.size(), 1, 3000);
    const QByteArray head = backend.requests.first().head;
    QVERIFY(head.startsWith("GET /hello?x=1 HTTP/1.1\r\n"));
    QVERIFY(head.contains("Host: demo.localhost:8080"));
    QVERIFY(head.contains("X-Forwarded-For:"));
    QVERIFY(head.contains("X-Forwarded-Proto: http"));
    QVERIFY(head.contains("Connection: close"));
}

void HttpProxyTest::forwardsRequestBody()
{
    BackendServer backend;
    QVERIFY(backend.listen(QHostAddress::LocalHost, 0));
    const quint16 backendPort = backend.serverPort();

    PodStack::HttpProxy proxy(
        providerFor(QStringLiteral("demo"), {QStringLiteral("%1->80/tcp").arg(backendPort)}));
    QVERIFY(proxy.listen(0));

    const QByteArray body = "name=podstack&mode=proxy";
    const QByteArray response =
        exchange(proxy.port(), "POST /submit HTTP/1.1\r\nHost: demo.localhost\r\nContent-Type: "
                               "application/x-www-form-urlencoded\r\nContent-Length: " +
                                   QByteArray::number(body.size()) + "\r\n\r\n" + body);
    QVERIFY(response.startsWith("HTTP/1.1 200"));

    QTRY_COMPARE_WITH_TIMEOUT(backend.requests.size(), 1, 3000);
    QCOMPARE(backend.requests.first().body, body);
    QVERIFY(backend.requests.first().head.contains("Content-Length: " +
                                                   QByteArray::number(body.size())));
}

void HttpProxyTest::servesIndexForLocalhost()
{
    PodStack::HttpProxy proxy(
        [](QVector<PodStack::ContainerInfo> *containers, PodStack::Error *error) {
            if (error) {
                *error = {};
            }
            PodStack::ContainerInfo web;
            web.name = QStringLiteral("web");
            web.state = QStringLiteral("running");
            web.ports = {QStringLiteral("9001->80/tcp")};
            PodStack::ContainerInfo api;
            api.name = QStringLiteral("api");
            api.state = QStringLiteral("exited");
            api.ports = {QStringLiteral("9002->8080/tcp")};
            containers->append(web);
            containers->append(api);
            return true;
        });
    QVERIFY(proxy.listen(0));

    const QByteArray response = exchange(proxy.port(), "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    QVERIFY(response.startsWith("HTTP/1.1 200"));
    QVERIFY(response.contains("Serviços locais"));
    const QByteArray port = QByteArray::number(proxy.port());
    QVERIFY(response.contains("http://web.localhost:" + port + "/"));
    QVERIFY(response.contains("http://api.localhost:" + port + "/"));
    QVERIFY(response.contains(">9001<"));
    QVERIFY(response.contains(">9002<"));
    QVERIFY(response.contains("exited"));
    QVERIFY(response.indexOf("http://api.localhost") < response.indexOf("http://web.localhost"));
}

void HttpProxyTest::returns404WithIndexForUnknownContainer()
{
    PodStack::HttpProxy proxy(
        providerFor(QStringLiteral("demo"), {QStringLiteral("9001->80/tcp")}));
    QVERIFY(proxy.listen(0));

    const QByteArray response =
        exchange(proxy.port(), "GET / HTTP/1.1\r\nHost: missing.localhost\r\n\r\n");
    QVERIFY(response.startsWith("HTTP/1.1 404"));
    QVERIFY(response.contains("Container não encontrado"));
    QVERIFY(response.contains("http://demo.localhost:" + QByteArray::number(proxy.port()) + "/"));
}

void HttpProxyTest::indexRequiresGetOrHead()
{
    PodStack::HttpProxy proxy(providerFor(QStringLiteral("demo"), {}));
    QVERIFY(proxy.listen(0));

    const QByteArray response =
        exchange(proxy.port(), "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n");
    QVERIFY(response.startsWith("HTTP/1.1 405"));
    QVERIFY(response.contains("Allow: GET, HEAD"));
}

void HttpProxyTest::headIndexHasNoBody()
{
    PodStack::HttpProxy proxy(providerFor(QStringLiteral("demo"), {}));
    QVERIFY(proxy.listen(0));

    const QByteArray response =
        exchange(proxy.port(), "HEAD / HTTP/1.1\r\nHost: localhost\r\n\r\n", 3000, false);
    QVERIFY(response.startsWith("HTTP/1.1 200"));
    QVERIFY(response.contains("Content-Length:"));
    QVERIFY(!response.contains("<html"));
}

void HttpProxyTest::indexEscapesContainerContent()
{
    PodStack::HttpProxy proxy(
        providerFor(QStringLiteral("bad<script>"), {QStringLiteral("9001->80/tcp")}));
    QVERIFY(proxy.listen(0));

    const QByteArray response = exchange(proxy.port(), "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    QVERIFY(response.startsWith("HTTP/1.1 200"));
    QVERIFY(response.contains("bad&lt;script&gt;"));
    QVERIFY(!response.contains("bad<script>"));
    QVERIFY(response.contains("nome incompatível com Host"));
}

void HttpProxyTest::returns404ForInvalidHost()
{
    PodStack::HttpProxy proxy(providerFor(QStringLiteral("demo"), {}));
    QVERIFY(proxy.listen(0));

    const QByteArray response =
        exchange(proxy.port(), "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
    QVERIFY(response.startsWith("HTTP/1.1 404"));
    QVERIFY(response.contains("Domínio não encontrado"));
    QVERIFY(!response.contains("Serviços locais"));
}

void HttpProxyTest::returns404WithoutPublishedPort()
{
    PodStack::HttpProxy proxy(providerFor(QStringLiteral("demo"), {QStringLiteral("80/tcp")}));
    QVERIFY(proxy.listen(0));

    const QByteArray response =
        exchange(proxy.port(), "GET / HTTP/1.1\r\nHost: demo.localhost\r\n\r\n");
    QVERIFY(response.startsWith("HTTP/1.1 404"));
    QVERIFY(response.contains("Sem porta publicada"));
}

void HttpProxyTest::returns502WhenProviderFails()
{
    PodStack::HttpProxy proxy([](QVector<PodStack::ContainerInfo> *, PodStack::Error *error) {
        if (error) {
            *error = PodStack::Error::make(QStringLiteral("PODSTACK_ENGINE_UNAVAILABLE"),
                                           QStringLiteral("Podman fora do ar."));
        }
        return false;
    });
    QVERIFY(proxy.listen(0));

    const QByteArray response =
        exchange(proxy.port(), "GET / HTTP/1.1\r\nHost: demo.localhost\r\n\r\n");
    QVERIFY(response.startsWith("HTTP/1.1 502"));
    QVERIFY(response.contains("Podman fora do ar."));
}

void HttpProxyTest::returns502WhenBackendRefuses()
{
    quint16 closedPort = 0;
    PodStack::HttpProxy proxy(
        [&closedPort](QVector<PodStack::ContainerInfo> *containers, PodStack::Error *error) {
            if (error) {
                *error = {};
            }
            PodStack::ContainerInfo info;
            info.name = QStringLiteral("demo");
            info.ports = {QStringLiteral("%1->80/tcp").arg(closedPort)};
            containers->append(info);
            return true;
        });
    QVERIFY(proxy.listen(0));

    QTcpServer closed;
    QVERIFY(closed.listen(QHostAddress::LocalHost, 0));
    closedPort = closed.serverPort();
    closed.close();

    const QByteArray response =
        exchange(proxy.port(), "GET / HTTP/1.1\r\nHost: demo.localhost\r\n\r\n");
    QVERIFY(response.startsWith("HTTP/1.1 502"));
    QVERIFY(response.contains("Container indisponível"));
}

void HttpProxyTest::rejectsMalformedRequest()
{
    PodStack::HttpProxy proxy(providerFor(QStringLiteral("demo"), {}));
    QVERIFY(proxy.listen(0));

    const QByteArray response = exchange(proxy.port(), "BOGUS\r\n\r\n");
    QVERIFY(response.startsWith("HTTP/1.1 400"));
    QVERIFY(response.contains("Requisição inválida"));
}

void HttpProxyTest::rejectsOversizedHeaders()
{
    PodStack::HttpProxy proxy(providerFor(QStringLiteral("demo"), {}));
    QVERIFY(proxy.listen(0));

    QByteArray request = "GET / HTTP/1.1\r\nHost: demo.localhost\r\nX-Fill: ";
    request.append(QByteArray(33 * 1024, 'a'));
    const QByteArray response = exchange(proxy.port(), request);
    QVERIFY(response.startsWith("HTTP/1.1 431"));
}

void HttpProxyTest::headHasNoErrorBody()
{
    PodStack::HttpProxy proxy(providerFor(QStringLiteral("demo"), {}));
    QVERIFY(proxy.listen(0));

    const QByteArray response =
        exchange(proxy.port(), "HEAD / HTTP/1.1\r\nHost: missing.localhost\r\n\r\n", 3000, false);
    QVERIFY(response.startsWith("HTTP/1.1 404"));
    QVERIFY(response.contains("Content-Length:"));
    QVERIFY(!response.contains("<html"));
}

void HttpProxyTest::abortsIdleConnection()
{
    QTcpServer silent;
    QVERIFY(silent.listen(QHostAddress::LocalHost, 0));
    QList<QTcpSocket *> sockets;
    connect(&silent, &QTcpServer::newConnection, &silent, [&silent, &sockets] {
        while (silent.hasPendingConnections()) {
            sockets.append(silent.nextPendingConnection());
        }
    });

    PodStack::HttpProxy proxy(providerFor(QStringLiteral("demo"),
                                          {QStringLiteral("%1->80/tcp").arg(silent.serverPort())}));
    proxy.setTimeouts(1000, 150);
    QVERIFY(proxy.listen(0));

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, proxy.port());
    QVERIFY(client.waitForConnected(1000));
    client.write("GET / HTTP/1.1\r\nHost: demo.localhost\r\n\r\n");
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QAbstractSocket::UnconnectedState, 3000);
}

QTEST_GUILESS_MAIN(HttpProxyTest)

#include "test_http_proxy.moc"
