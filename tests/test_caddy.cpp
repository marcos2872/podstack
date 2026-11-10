// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/CaddyProxy.h"

#include <QCoreApplication>
#include <QDir>
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

bool writeFakeCaddy(const QString &directory, const QByteArray &body)
{
    QFile file(QDir(directory).filePath(QStringLiteral("caddy")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write("#!/bin/sh\n");
    file.write(body);
    file.write("\n");
    file.close();
    return file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                               QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                               QFileDevice::ExeGroup | QFileDevice::ReadOther |
                               QFileDevice::ExeOther);
}

} // namespace

class CaddyTest : public QObject {
    Q_OBJECT

private slots:
    void configTextHasInternalCaAndProxy();
    void startFailsWithoutCaddy();
    void rejectsInvalidPorts();
    void startsAndStopsWithFakeCaddy();
    void startFailsWhenProcessExits();
    void startFailsWhenDirectoryIsFile();
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

void CaddyTest::rejectsInvalidPorts()
{
    QTemporaryDir dir;
    PodStack::CaddyProxy caddy(dir.path());
    PodStack::Error error;
    QVERIFY(!caddy.start(0, 8080, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_ARGUMENT"));
    QVERIFY(!caddy.start(8443, 8443, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_ARGUMENT"));
}

void CaddyTest::startsAndStopsWithFakeCaddy()
{
    QTemporaryDir dir;
    QTemporaryDir bin;
    QVERIFY(dir.isValid());
    QVERIFY(bin.isValid());
    QVERIFY(writeFakeCaddy(
        bin.path(), QByteArrayLiteral("port=$(grep -m1 'https_port' \"$3\" | awk '{print $2}')\n"
                                      "exec \"$TEST_CADDY_BINARY\" --fake-caddy \"$port\"")));

    const QByteArray previousPath = qgetenv("PATH");
    const QByteArray previousBinary = qgetenv("TEST_CADDY_BINARY");
    qputenv("PATH", bin.path().toUtf8() + ':' + previousPath);
    qputenv("TEST_CADDY_BINARY", QCoreApplication::applicationFilePath().toUtf8());
    const auto restore = qScopeGuard([previousPath, previousBinary] {
        qputenv("PATH", previousPath);
        qputenv("TEST_CADDY_BINARY", previousBinary);
    });

    const quint16 httpsPort = freePort();
    QVERIFY(httpsPort != 0);
    PodStack::CaddyProxy caddy(dir.path());
    PodStack::Error error;
    QVERIFY2(caddy.start(httpsPort, 8080, &error), qPrintable(error.message));
    QVERIFY(caddy.running());
    QCOMPARE(caddy.httpsPort(), httpsPort);
    QVERIFY(QFile::exists(caddy.configPath()));
    QVERIFY(caddy.configPath().startsWith(dir.path()));

    caddy.stop();
    QVERIFY(!caddy.running());
}

void CaddyTest::startFailsWhenProcessExits()
{
    QTemporaryDir dir;
    QTemporaryDir bin;
    QVERIFY(writeFakeCaddy(bin.path(), QByteArrayLiteral("exit 1")));
    const QByteArray previousPath = qgetenv("PATH");
    qputenv("PATH", bin.path().toUtf8() + ':' + previousPath);
    const auto restore = qScopeGuard([previousPath] { qputenv("PATH", previousPath); });

    PodStack::CaddyProxy caddy(dir.path());
    PodStack::Error error;
    QVERIFY(!caddy.start(freePort(), 8080, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    QVERIFY(error.message.contains(QStringLiteral("Caddy não abriu")));
    QVERIFY(!caddy.running());
}

void CaddyTest::startFailsWhenDirectoryIsFile()
{
    QTemporaryDir temp;
    QTemporaryDir bin;
    QVERIFY(writeFakeCaddy(bin.path(), QByteArrayLiteral("exit 0")));
    const QByteArray previousPath = qgetenv("PATH");
    qputenv("PATH", bin.path().toUtf8() + ':' + previousPath);
    const auto restore = qScopeGuard([previousPath] { qputenv("PATH", previousPath); });

    const QString path = temp.filePath(QStringLiteral("not-a-dir"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    PodStack::CaddyProxy caddy(path);
    PodStack::Error error;
    QVERIFY(!caddy.start(freePort(), 8080, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
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

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc == 3 && QString::fromLatin1(argv[1]) == QStringLiteral("--fake-caddy")) {
        bool ok = false;
        const quint16 port = QString::fromLatin1(argv[2]).toUShort(&ok);
        QTcpServer server;
        if (!ok || port == 0 || !server.listen(QHostAddress::LocalHost, port)) {
            return 1;
        }
        return app.exec();
    }
    CaddyTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_caddy.moc"
