// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/CaddyProxy.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QScopeGuard>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

namespace {

class BackendServer : public QTcpServer {
public:
    explicit BackendServer(QObject *parent = nullptr) : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                QTcpSocket *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                    if (!socket->readAll().contains("\r\n\r\n")) {
                        return;
                    }
                    socket->write(
                        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
};

quint16 freePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

} // namespace

class CaddyTest : public QObject {
    Q_OBJECT

private slots:
    void configTextHasInternalCaAndProxy();
    void startFailsWithoutCaddy();
    void servesTlsOnDemand();
};

void CaddyTest::configTextHasInternalCaAndProxy()
{
    const QString text = PodStack::CaddyProxy::configText(8443, 8080, QStringLiteral("/tmp/ca"));
    QVERIFY(text.contains(QStringLiteral("https_port 8443")));
    QVERIFY(text.contains(QStringLiteral("*.localhost, *.podstack.local")));
    QVERIFY(text.contains(QStringLiteral("tls internal")));
    QVERIFY(text.contains(QStringLiteral("on_demand")));
    QVERIFY(text.contains(QStringLiteral("reverse_proxy 127.0.0.1:8080")));
    QVERIFY(text.contains(QStringLiteral("storage file_system \"/tmp/ca\"")));
    QVERIFY(text.contains(QStringLiteral("skip_install_trust")));
}

void CaddyTest::startFailsWithoutCaddy()
{
    QTemporaryDir emptyBin;
    QVERIFY(emptyBin.isValid());
    const QByteArray previousPath = qgetenv("PATH");
    qputenv("PATH", emptyBin.path().toUtf8());
    const auto restore = qScopeGuard([previousPath] { qputenv("PATH", previousPath); });

    QTemporaryDir dir;
    PodStack::CaddyProxy caddy(dir.path());
    PodStack::Error error;
    QVERIFY(!caddy.start(9443, 8080, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
    QVERIFY(!caddy.running());
}

void CaddyTest::servesTlsOnDemand()
{
    if (QStandardPaths::findExecutable(QStringLiteral("caddy")).isEmpty()) {
        QSKIP("caddy não instalado neste ambiente");
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BackendServer backend;
    QVERIFY(backend.listen(QHostAddress::LocalHost, 0));
    const quint16 httpsPort = freePort();
    QVERIFY(httpsPort != 0);

    PodStack::CaddyProxy caddy(dir.path());
    PodStack::Error error;
    QVERIFY2(caddy.start(httpsPort, backend.serverPort(), &error), qPrintable(error.message));
    QVERIFY(caddy.running());
    QCOMPARE(caddy.httpsPort(), httpsPort);

    const QString rootPath = caddy.rootCertPath();
    QVERIFY(QFile::exists(rootPath));
    const QList<QSslCertificate> roots = QSslCertificate::fromPath(rootPath);
    QVERIFY(!roots.isEmpty());

    const QString keyPath = dir.filePath(QStringLiteral("storage/pki/authorities/local/root.key"));
    QVERIFY(QFile::exists(keyPath));
    const QFile::Permissions keyPermissions = QFileInfo(keyPath).permissions();
    QVERIFY((keyPermissions & (QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                               QFileDevice::ReadOther | QFileDevice::WriteOther)) == 0);

    QSslConfiguration config = QSslConfiguration::defaultConfiguration();
    config.setCaCertificates(roots);
    QSslSocket socket;
    socket.setSslConfiguration(config);
    socket.setPeerVerifyName(QStringLiteral("web.localhost"));
    socket.connectToHostEncrypted(QStringLiteral("127.0.0.1"), httpsPort,
                                  QStringLiteral("web.localhost"));
    QVERIFY2(socket.waitForEncrypted(10000), qPrintable(socket.errorString()));
    socket.write("GET / HTTP/1.1\r\nHost: web.localhost\r\nConnection: close\r\n\r\n");

    QByteArray response;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000 && !response.endsWith("ok")) {
        QTest::qWait(20);
        response += socket.readAll();
    }
    QVERIFY(response.startsWith("HTTP/1.1 200"));
    QVERIFY(response.endsWith("ok"));

    caddy.stop();
    QVERIFY(!caddy.running());
}

QTEST_GUILESS_MAIN(CaddyTest)

#include "test_caddy.moc"
