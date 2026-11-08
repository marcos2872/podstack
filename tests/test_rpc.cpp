#include "MockPodmanServer.h"

#include "core/RpcClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QTest>

namespace {

class RpcTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void lifecycle();
    void subscriptions();
    void invalidRequests();

private:
    QJsonObject readMessage(QLocalSocket &socket, QByteArray &buffer, int timeoutMs = 3000);
    void sendMessage(QLocalSocket &socket, const QJsonObject &message);

    MockPodmanServer m_mock;
    QProcess m_daemon;
    QString m_controlSocket;
};

void RpcTest::initTestCase()
{
    QVERIFY(m_mock.isListening());

    m_controlSocket = QDir::tempPath()
                      + QStringLiteral("/podstack-rpc-%1.sock")
                            .arg(QCoreApplication::applicationPid());
    QFile::remove(m_controlSocket);

    const QString daemonPath =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../podstackd"));

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PODSTACK_PODMAN_SOCKET"), m_mock.socketPath());
    environment.insert(QStringLiteral("PODSTACK_CONTROL_SOCKET"), m_controlSocket);
    m_daemon.setProcessEnvironment(environment);
    m_daemon.setProcessChannelMode(QProcess::ForwardedChannels);
    m_daemon.start(daemonPath, {QStringLiteral("--socket"), m_controlSocket});
    QVERIFY2(m_daemon.waitForStarted(5000), qPrintable(m_daemon.errorString()));

    for (int attempt = 0; attempt < 100 && !QFileInfo::exists(m_controlSocket); ++attempt) {
        QTest::qWait(20);
    }
    QVERIFY(QFileInfo::exists(m_controlSocket));

    PodStack::RpcClient client(m_controlSocket);
    PodStack::Error error;
    for (int attempt = 0; attempt < 50; ++attempt) {
        client.call(QStringLiteral("system.hello"), {}, &error, 1000);
        if (error.isEmpty()) {
            return;
        }
        QTest::qWait(100);
    }
    QFAIL(qPrintable(error.message));
}

void RpcTest::cleanupTestCase()
{
    m_daemon.terminate();
    if (!m_daemon.waitForFinished(3000)) {
        m_daemon.kill();
        m_daemon.waitForFinished(1000);
    }
    QFile::remove(m_controlSocket);
}

void RpcTest::lifecycle()
{
    PodStack::RpcClient client(m_controlSocket);
    PodStack::Error error;

    const QJsonObject hello = client.call(QStringLiteral("system.hello"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(hello.value(QStringLiteral("apiVersion")).toInt(), 1);

    const QJsonObject status = client.call(QStringLiteral("system.status"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(status.value(QStringLiteral("state")).toString(), QStringLiteral("ok"));

    const QJsonObject doctor = client.call(QStringLiteral("system.doctor"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(doctor.value(QStringLiteral("checks")).toArray().size(), 2);

    QJsonObject listParams;
    listParams.insert(QStringLiteral("all"), false);
    const QJsonObject list = client.call(QStringLiteral("containers.list"), listParams, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(list.value(QStringLiteral("items")).toArray().size(), 1);

    QJsonObject idParams;
    idParams.insert(QStringLiteral("id"), QStringLiteral("abc123"));

    QVERIFY2(client.call(QStringLiteral("containers.start"), idParams, &error)
                 .value(QStringLiteral("ok"))
                 .toBool(),
             qPrintable(error.message));
    QVERIFY2(client.call(QStringLiteral("containers.stop"), idParams, &error)
                 .value(QStringLiteral("ok"))
                 .toBool(),
             qPrintable(error.message));
    QVERIFY2(client.call(QStringLiteral("containers.restart"), idParams, &error)
                 .value(QStringLiteral("ok"))
                 .toBool(),
             qPrintable(error.message));
    QVERIFY2(client.call(QStringLiteral("containers.remove"), idParams, &error)
                 .value(QStringLiteral("ok"))
                 .toBool(),
             qPrintable(error.message));
    QVERIFY2(client.call(QStringLiteral("containers.pause"), idParams, &error)
                 .value(QStringLiteral("ok"))
                 .toBool(),
             qPrintable(error.message));
    QVERIFY2(client.call(QStringLiteral("containers.unpause"), idParams, &error)
                 .value(QStringLiteral("ok"))
                 .toBool(),
             qPrintable(error.message));
    QVERIFY2(client.call(QStringLiteral("containers.kill"), idParams, &error)
                 .value(QStringLiteral("ok"))
                 .toBool(),
             qPrintable(error.message));

    QJsonObject renameParams = idParams;
    renameParams.insert(QStringLiteral("name"), QStringLiteral("renamed"));
    QVERIFY2(client.call(QStringLiteral("containers.rename"), renameParams, &error)
                 .value(QStringLiteral("ok"))
                 .toBool(),
             qPrintable(error.message));

    const QJsonObject inspect = client.call(QStringLiteral("containers.inspect"), idParams, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(inspect.value(QStringLiteral("json"))
                 .toObject()
                 .value(QStringLiteral("Id"))
                 .toString(),
             QStringLiteral("abc123"));

    QJsonObject createParams;
    createParams.insert(QStringLiteral("image"), QStringLiteral("alpine:3.20"));
    createParams.insert(QStringLiteral("name"), QStringLiteral("created"));
    createParams.insert(QStringLiteral("command"), QStringLiteral("sleep 10"));
    createParams.insert(QStringLiteral("env"),
                        QJsonObject{{QStringLiteral("A"), QStringLiteral("B")}});
    createParams.insert(QStringLiteral("ports"),
                        QJsonArray{QJsonObject{{QStringLiteral("hostPort"), 18080},
                                               {QStringLiteral("containerPort"), 80},
                                               {QStringLiteral("protocol"), QStringLiteral("tcp")}}});
    createParams.insert(QStringLiteral("mounts"),
                        QJsonArray{QJsonObject{{QStringLiteral("source"), QStringLiteral("/tmp")},
                                               {QStringLiteral("destination"),
                                                QStringLiteral("/mnt")},
                                               {QStringLiteral("readOnly"), false}}});
    createParams.insert(QStringLiteral("workDir"), QStringLiteral("/tmp"));
    createParams.insert(QStringLiteral("user"), QStringLiteral("0:0"));
    createParams.insert(QStringLiteral("remove"), true);
    createParams.insert(QStringLiteral("start"), true);

    const QJsonObject created =
        client.call(QStringLiteral("containers.create"), createParams, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(created.value(QStringLiteral("id")).toString(), QStringLiteral("created123"));
    QVERIFY(created.value(QStringLiteral("started")).toBool());

    const QJsonObject duplicated =
        client.call(QStringLiteral("containers.duplicate"), idParams, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(duplicated.value(QStringLiteral("id")).toString(), QStringLiteral("created123"));
    QCOMPARE(duplicated.value(QStringLiteral("source")).toString(), QStringLiteral("abc123"));

    PodStack::Error createError;
    client.call(QStringLiteral("containers.create"), {}, &createError);
    QCOMPARE(createError.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QJsonObject filesParams;
    filesParams.insert(QStringLiteral("id"), QStringLiteral("abc123"));
    filesParams.insert(QStringLiteral("path"), QStringLiteral("/"));
    const QJsonObject files =
        client.call(QStringLiteral("containers.files.list"), filesParams, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(files.value(QStringLiteral("entries")).toArray().size(), 2);

    const QString destination = QDir::tempPath()
                                + QStringLiteral("/podstack-rpc-file-%1.txt")
                                      .arg(QCoreApplication::applicationPid());
    QFile::remove(destination);
    QJsonObject downloadParams = filesParams;
    downloadParams.insert(QStringLiteral("path"), QStringLiteral("/file.txt"));
    downloadParams.insert(QStringLiteral("destination"), destination);
    const QJsonObject download =
        client.call(QStringLiteral("containers.files.download"), downloadParams, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(download.value(QStringLiteral("destination")).toString(), destination);
    QFile::remove(destination);

    QJsonObject runningParams;
    runningParams.insert(QStringLiteral("all"), true);
    runningParams.insert(QStringLiteral("state"), QStringLiteral("running"));
    QCOMPARE(client.call(QStringLiteral("containers.list"), runningParams, &error)
                 .value(QStringLiteral("items"))
                 .toArray()
                 .size(),
             1);

    runningParams.insert(QStringLiteral("state"), QStringLiteral("stopped"));
    QCOMPARE(client.call(QStringLiteral("containers.list"), runningParams, &error)
                 .value(QStringLiteral("items"))
                 .toArray()
                 .size(),
             0);

    QJsonObject execParams;
    execParams.insert(QStringLiteral("id"), QStringLiteral("abc123"));
    execParams.insert(QStringLiteral("cmd"),
                      QJsonArray{QStringLiteral("/bin/echo"), QStringLiteral("hello")});
    const QJsonObject exec = client.call(QStringLiteral("containers.exec"), execParams, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(exec.value(QStringLiteral("stdout"))
                .toString()
                .contains(QStringLiteral("mock-exec-output")));
    QCOMPARE(exec.value(QStringLiteral("exitCode")).toInt(), 0);

    PodStack::Error missingCmdError;
    QJsonObject missingCmd;
    missingCmd.insert(QStringLiteral("id"), QStringLiteral("abc123"));
    client.call(QStringLiteral("containers.exec"), missingCmd, &missingCmdError);
    QCOMPARE(missingCmdError.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    PodStack::Error missingIdError;
    client.call(QStringLiteral("containers.start"), {}, &missingIdError);
    QCOMPARE(missingIdError.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    PodStack::Error unknownError;
    client.call(QStringLiteral("no.such.method"), {}, &unknownError);
    QCOMPARE(unknownError.code, QStringLiteral("PODSTACK_METHOD_NOT_FOUND"));
}

void RpcTest::subscriptions()
{
    QLocalSocket socket;
    socket.connectToServer(m_controlSocket, QIODevice::ReadWrite);
    QVERIFY(socket.waitForConnected(3000));

    QByteArray buffer;
    sendMessage(socket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 1},
                 {QStringLiteral("method"), QStringLiteral("containers.logs.subscribe")},
                 {QStringLiteral("params"),
                  QJsonObject{{QStringLiteral("id"), QStringLiteral("abc123")},
                              {QStringLiteral("tail"), 10},
                              {QStringLiteral("follow"), false}}}});

    QString subscriptionId;
    int deltas = 0;
    for (int i = 0; i < 20 && (subscriptionId.isEmpty() || deltas < 2); ++i) {
        const QJsonObject message = readMessage(socket, buffer);
        if (message.isEmpty()) {
            break;
        }
        if (message.value(QStringLiteral("id")).toInt() == 1) {
            subscriptionId = message.value(QStringLiteral("result"))
                                 .toObject()
                                 .value(QStringLiteral("subscriptionId"))
                                 .toString();
        }
        if (message.value(QStringLiteral("method")).toString()
            == QStringLiteral("containers.logs.delta")) {
            ++deltas;
        }
    }
    QVERIFY(!subscriptionId.isEmpty());
    QCOMPARE(deltas, 2);

    sendMessage(socket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 2},
                 {QStringLiteral("method"), QStringLiteral("containers.logs.unsubscribe")},
                 {QStringLiteral("params"),
                  QJsonObject{{QStringLiteral("subscriptionId"), subscriptionId}}}});
    QVERIFY(!readMessage(socket, buffer).isEmpty());

    sendMessage(socket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 3},
                 {QStringLiteral("method"), QStringLiteral("events.subscribe")},
                 {QStringLiteral("params"), QJsonObject{}}});

    int events = 0;
    for (int i = 0; i < 20 && events < 1; ++i) {
        const QJsonObject message = readMessage(socket, buffer);
        if (message.isEmpty()) {
            break;
        }
        if (message.value(QStringLiteral("method")).toString() == QStringLiteral("event")) {
            ++events;
        }
    }
    QCOMPARE(events, 1);

    sendMessage(socket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 4},
                 {QStringLiteral("method"), QStringLiteral("containers.stats.subscribe")},
                 {QStringLiteral("params"),
                  QJsonObject{{QStringLiteral("id"), QStringLiteral("abc123")},
                              {QStringLiteral("intervalSeconds"), 1}}}});

    int stats = 0;
    for (int i = 0; i < 20 && stats < 1; ++i) {
        const QJsonObject message = readMessage(socket, buffer);
        if (message.isEmpty()) {
            break;
        }
        if (message.value(QStringLiteral("method")).toString()
            == QStringLiteral("containers.stats.delta")) {
            ++stats;
        }
    }
    QCOMPARE(stats, 1);
}

void RpcTest::invalidRequests()
{
    QLocalSocket socket;
    socket.connectToServer(m_controlSocket, QIODevice::ReadWrite);
    QVERIFY(socket.waitForConnected(3000));

    QByteArray buffer;
    socket.write("this is not json\n");
    socket.flush();
    QJsonObject response = readMessage(socket, buffer);
    QCOMPARE(response.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(),
             -32700);

    socket.write("{\"jsonrpc\":\"2.0\",\"id\":7}\n");
    socket.flush();
    response = readMessage(socket, buffer);
    QCOMPARE(response.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(),
             -32600);
}

QJsonObject RpcTest::readMessage(QLocalSocket &socket, QByteArray &buffer, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();

    while (true) {
        const int newline = buffer.indexOf('\n');
        if (newline >= 0) {
            const QByteArray line = buffer.left(newline);
            buffer.remove(0, newline + 1);
            if (line.trimmed().isEmpty()) {
                continue;
            }
            return QJsonDocument::fromJson(line).object();
        }

        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0 || !socket.waitForReadyRead(remaining)) {
            return {};
        }
        buffer.append(socket.readAll());
    }
}

void RpcTest::sendMessage(QLocalSocket &socket, const QJsonObject &message)
{
    socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    socket.flush();
}

} // namespace

QTEST_GUILESS_MAIN(RpcTest)
#include "test_rpc.moc"
