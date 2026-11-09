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
    void catalog();
    void machinesDistros();
    void composeAndBuilds();
    void setupFlow();
    void invalidRequests();

private:
    QJsonObject readMessage(QLocalSocket &socket, QByteArray &buffer, int timeoutMs = 3000);
    void sendMessage(QLocalSocket &socket, const QJsonObject &message);

    MockPodmanServer m_mock;
    QProcess m_daemon;
    QString m_controlSocket;
    QString m_testDataDir;
    QString m_fakeProvider;
};

namespace {
void writeFakeProvider(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    file.write("#!/bin/sh\ncase \"$*\" in *fail*) echo \"fake failure\" >&2; exit 1;; esac\necho \"fake compose $@\"\nexit 0\n");
    file.close();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                        | QFileDevice::ExeOwner);
}
} // namespace

void RpcTest::initTestCase()
{
    QVERIFY(m_mock.isListening());
    QDir().mkpath(QStringLiteral("/tmp/mock-project"));
    QFile mockCompose(QStringLiteral("/tmp/mock-project/compose.yaml"));
    if (mockCompose.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        mockCompose.write("services: {}\n");
        mockCompose.close();
    }

    m_testDataDir = QDir::tempPath()
                    + QStringLiteral("/podstack-rpc-data-%1")
                          .arg(QCoreApplication::applicationPid());
    QDir(m_testDataDir).removeRecursively();
    QDir().mkpath(m_testDataDir);
    m_fakeProvider = m_testDataDir + QStringLiteral("/compose-provider.sh");
    writeFakeProvider(m_fakeProvider);

    m_controlSocket = QDir::tempPath()
                      + QStringLiteral("/podstack-rpc-%1.sock")
                            .arg(QCoreApplication::applicationPid());
    QFile::remove(m_controlSocket);

    const QString daemonPath =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../podstackd"));

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PODSTACK_PODMAN_SOCKET"), m_mock.socketPath());
    environment.insert(QStringLiteral("PODSTACK_CONTROL_SOCKET"), m_controlSocket);
    environment.insert(QStringLiteral("PODSTACK_COMPOSE_PROVIDER"), m_fakeProvider);
    environment.insert(QStringLiteral("XDG_DATA_HOME"),
                       m_testDataDir + QStringLiteral("/data"));
    environment.insert(QStringLiteral("XDG_STATE_HOME"),
                       m_testDataDir + QStringLiteral("/state"));
    environment.insert(QStringLiteral("XDG_CONFIG_HOME"),
                       m_testDataDir + QStringLiteral("/config"));
    environment.insert(QStringLiteral("XDG_CACHE_HOME"),
                       m_testDataDir + QStringLiteral("/cache"));
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
    QDir(m_testDataDir).removeRecursively();
    QDir(QStringLiteral("/tmp/mock-project")).removeRecursively();
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

    const QString uploadSource = QDir::tempPath()
                                 + QStringLiteral("/podstack-rpc-upload-%1.txt")
                                       .arg(QCoreApplication::applicationPid());
    QFile uploadFile(uploadSource);
    QVERIFY(uploadFile.open(QIODevice::WriteOnly));
    uploadFile.write("rpc-upload");
    uploadFile.close();
    QJsonObject uploadParams = filesParams;
    uploadParams.insert(QStringLiteral("path"), QStringLiteral("/tmp"));
    uploadParams.insert(QStringLiteral("sourcePath"), uploadSource);
    const QJsonObject upload =
        client.call(QStringLiteral("containers.files.upload"), uploadParams, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(upload.value(QStringLiteral("ok")).toBool());
    QFile::remove(uploadSource);

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
    QString statsSubscription;
    for (int i = 0; i < 20 && stats < 1; ++i) {
        const QJsonObject message = readMessage(socket, buffer);
        if (message.isEmpty()) {
            break;
        }
        if (message.value(QStringLiteral("id")).toInt() == 4) {
            statsSubscription = message.value(QStringLiteral("result"))
                                    .toObject()
                                    .value(QStringLiteral("subscriptionId"))
                                    .toString();
        }
        if (message.value(QStringLiteral("method")).toString()
            == QStringLiteral("containers.stats.delta")) {
            ++stats;
        }
    }
    QCOMPARE(stats, 1);
    QVERIFY(!statsSubscription.isEmpty());
    sendMessage(socket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 5},
                 {QStringLiteral("method"), QStringLiteral("containers.stats.unsubscribe")},
                 {QStringLiteral("params"),
                  QJsonObject{{QStringLiteral("subscriptionId"), statsSubscription}}}});
    QVERIFY(!readMessage(socket, buffer).isEmpty());
}

void RpcTest::catalog()
{
    PodStack::RpcClient client(m_controlSocket);
    PodStack::Error error;

    QJsonObject result = client.call(QStringLiteral("images.list"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(result.value(QStringLiteral("items")).toArray().size(), 2);

    result = client.call(QStringLiteral("images.history"),
                         QJsonObject{{QStringLiteral("id"), QStringLiteral("img1234567890")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(result.value(QStringLiteral("items")).toArray().size(), 1);

    result = client.call(QStringLiteral("images.tag"),
                         QJsonObject{{QStringLiteral("id"), QStringLiteral("img1234567890")},
                                     {QStringLiteral("repo"), QStringLiteral("repo")},
                                     {QStringLiteral("tag"), QStringLiteral("v1")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    result = client.call(QStringLiteral("images.untag"),
                         QJsonObject{{QStringLiteral("id"), QStringLiteral("img1234567890")},
                                     {QStringLiteral("repo"), QStringLiteral("repo")},
                                     {QStringLiteral("tag"), QStringLiteral("v1")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    result = client.call(QStringLiteral("images.remove"),
                         QJsonObject{{QStringLiteral("id"), QStringLiteral("img1234567890")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(result.value(QStringLiteral("removed")).toBool());

    result = client.call(QStringLiteral("images.prune"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(static_cast<qint64>(
                 result.value(QStringLiteral("reclaimedBytes")).toDouble()),
             12345);

    result = client.call(QStringLiteral("volumes.list"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(result.value(QStringLiteral("items")).toArray().size(), 1);

    const QString sourceDir = QStringLiteral("/tmp/podstack-mock-volume");
    const QString destinationDir = QStringLiteral("/tmp/podstack-mock-volume2");
    QDir(sourceDir).removeRecursively();
    QDir(destinationDir).removeRecursively();
    QDir().mkpath(sourceDir);
    QFile sourceFile(sourceDir + QStringLiteral("/a.txt"));
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    sourceFile.write("hello");
    sourceFile.close();

    result = client.call(QStringLiteral("volumes.create"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("vol2")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(result.value(QStringLiteral("Name")).toString(), QStringLiteral("vol2"));

    result = client.call(QStringLiteral("volumes.clone"),
                         QJsonObject{{QStringLiteral("source"), QStringLiteral("vol1")},
                                     {QStringLiteral("destination"), QStringLiteral("vol2")}},
                         &error, 60000);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    const QString archivePath = QDir::tempPath()
                                + QStringLiteral("/podstack-rpc-volume-%1.tar.zst")
                                      .arg(QCoreApplication::applicationPid());
    QFile::remove(archivePath);
    result = client.call(QStringLiteral("volumes.export"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("vol1")},
                                     {QStringLiteral("path"), archivePath}},
                         &error, 60000);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(QFileInfo(archivePath).size() > 0);

    result = client.call(QStringLiteral("volumes.import"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("vol2")},
                                     {QStringLiteral("path"), archivePath},
                                     {QStringLiteral("force"), true}},
                         &error, 60000);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QFile::remove(archivePath);

    const QString hostDir = QStringLiteral("/tmp/podstack-host-src");
    QDir(hostDir).removeRecursively();
    QDir().mkpath(hostDir);
    QFile hostFile(hostDir + QStringLiteral("/b.txt"));
    QVERIFY(hostFile.open(QIODevice::WriteOnly));
    hostFile.write("host-content");
    hostFile.close();
    result = client.call(QStringLiteral("volumes.importHostPath"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("vol2")},
                                     {QStringLiteral("sourcePath"), hostDir}},
                         &error, 60000);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QDir(hostDir).removeRecursively();

    result = client.call(QStringLiteral("volumes.remove"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("vol1")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    result = client.call(QStringLiteral("volumes.prune"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(static_cast<qint64>(
                 result.value(QStringLiteral("reclaimedBytes")).toDouble()),
             456);

    result = client.call(QStringLiteral("networks.list"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(result.value(QStringLiteral("items")).toArray().size(), 1);

    result = client.call(QStringLiteral("networks.create"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("mock-net")},
                                     {QStringLiteral("subnet"),
                                      QStringLiteral("10.90.0.0/24")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(result.value(QStringLiteral("name")).toString(), QStringLiteral("mock-net"));

    result = client.call(QStringLiteral("networks.connect"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("mock-net")},
                                     {QStringLiteral("containerId"),
                                      QStringLiteral("abc123")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    result = client.call(QStringLiteral("networks.disconnect"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("mock-net")},
                                     {QStringLiteral("containerId"),
                                      QStringLiteral("abc123")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    result = client.call(QStringLiteral("networks.remove"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("mock-net")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    result = client.call(QStringLiteral("pods.list"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(result.value(QStringLiteral("items")).toArray().size(), 1);

    result = client.call(QStringLiteral("pods.create"),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("demo-pod")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(result.value(QStringLiteral("Id")).toString(), QStringLiteral("pod123"));

    result = client.call(QStringLiteral("pods.start"),
                         QJsonObject{{QStringLiteral("id"), QStringLiteral("pod123")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(result.value(QStringLiteral("ok")).toBool());

    result = client.call(QStringLiteral("pods.remove"),
                         QJsonObject{{QStringLiteral("id"), QStringLiteral("pod123")}},
                         &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    result = client.call(QStringLiteral("system.df"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(static_cast<qint64>(result.value(QStringLiteral("json"))
                                     .toObject()
                                     .value(QStringLiteral("ImagesSize"))
                                     .toDouble()),
             7001000);

    QLocalSocket socket;
    socket.connectToServer(m_controlSocket, QIODevice::ReadWrite);
    QVERIFY(socket.waitForConnected(5000));
    QByteArray buffer;
    sendMessage(socket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 1},
                 {QStringLiteral("method"), QStringLiteral("images.pull")},
                 {QStringLiteral("params"),
                  QJsonObject{{QStringLiteral("reference"), QStringLiteral("alpine")}}}});

    const QJsonObject pullResponse = readMessage(socket, buffer);
    QVERIFY(!pullResponse.value(QStringLiteral("result"))
                 .toObject()
                 .value(QStringLiteral("subscriptionId"))
                 .toString()
                 .isEmpty());

    int deltas = 0;
    bool done = false;
    for (int i = 0; i < 20 && !done; ++i) {
        const QJsonObject message = readMessage(socket, buffer);
        if (message.isEmpty()) {
            break;
        }
        const QString method = message.value(QStringLiteral("method")).toString();
        if (method == QStringLiteral("images.pull.delta")) {
            ++deltas;
        } else if (method == QStringLiteral("images.pull.done")) {
            done = true;
        }
    }
    QVERIFY(deltas >= 2);
    QVERIFY(done);
    socket.disconnectFromServer();

    QDir(sourceDir).removeRecursively();
    QDir(destinationDir).removeRecursively();
}

void RpcTest::machinesDistros()
{
    PodStack::RpcClient client(m_controlSocket);
    PodStack::Error error;

    const QJsonObject result = client.call(QStringLiteral("machines.distros"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));

    const QJsonArray items = result.value(QStringLiteral("items")).toArray();
    QVERIFY(items.size() >= 3);

    QStringList distros;
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        distros.append(item.value(QStringLiteral("distro")).toString());
        QVERIFY(!item.value(QStringLiteral("version")).toString().isEmpty());
        QCOMPARE(item.value(QStringLiteral("arch")).toString(), QStringLiteral("amd64"));
        QVERIFY(!item.value(QStringLiteral("url")).toString().isEmpty());
        QVERIFY(!item.value(QStringLiteral("checksum")).toString().isEmpty());
        QVERIFY(!item.value(QStringLiteral("algorithm")).toString().isEmpty());
        QVERIFY(item.contains(QStringLiteral("size")));
        QVERIFY(item.contains(QStringLiteral("default")));
        QVERIFY(item.contains(QStringLiteral("cached")));
    }
    QVERIFY(distros.contains(QStringLiteral("ubuntu")));
    QVERIFY(distros.contains(QStringLiteral("debian")));
    QVERIFY(distros.contains(QStringLiteral("fedora")));
}

void RpcTest::composeAndBuilds()
{
    PodStack::RpcClient client(m_controlSocket);
    PodStack::Error error;

    const QJsonObject projects =
        client.call(QStringLiteral("compose.projects.list"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    const QJsonArray projectItems = projects.value(QStringLiteral("items")).toArray();
    QCOMPARE(projectItems.size(), 1);
    QCOMPARE(projectItems.first().toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("mock-project"));

    const QString projectDir = m_testDataDir + QStringLiteral("/project");
    QDir().mkpath(projectDir);
    QFile composeFile(projectDir + QStringLiteral("/compose.yaml"));
    QVERIFY(composeFile.open(QIODevice::WriteOnly));
    composeFile.write("services:\n  web:\n    image: alpine\n");
    composeFile.close();
    QFile dockerfile(projectDir + QStringLiteral("/Dockerfile"));
    QVERIFY(dockerfile.open(QIODevice::WriteOnly));
    dockerfile.write("FROM alpine:3.20\n");
    dockerfile.close();

    const QJsonObject up =
        client.call(QStringLiteral("compose.up"),
                    QJsonObject{{QStringLiteral("projectPath"), projectDir}},
                    &error, 60000);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(up.value(QStringLiteral("ok")).toBool());

    QVERIFY(client.call(QStringLiteral("compose.restart"),
                        QJsonObject{{QStringLiteral("projectPath"), projectDir}},
                        &error, 60000)
                .value(QStringLiteral("ok"))
                .toBool());
    QVERIFY(client.call(QStringLiteral("compose.stop"),
                        QJsonObject{{QStringLiteral("projectPath"), projectDir}},
                        &error, 60000)
                .value(QStringLiteral("ok"))
                .toBool());
    QVERIFY(client.call(QStringLiteral("compose.start"),
                        QJsonObject{{QStringLiteral("projectPath"), projectDir}},
                        &error, 60000)
                .value(QStringLiteral("ok"))
                .toBool());

    PodStack::Error invalidProject;
    client.call(QStringLiteral("compose.up"),
                QJsonObject{{QStringLiteral("projectPath"),
                             QStringLiteral("/nonexistent-project")}},
                &invalidProject);
    QCOMPARE(invalidProject.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    PodStack::Error invalidBuild;
    client.call(QStringLiteral("builds.start"),
                QJsonObject{{QStringLiteral("contextPath"), projectDir}}, &invalidBuild);
    QCOMPARE(invalidBuild.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QLocalSocket logsSocket;
    logsSocket.connectToServer(m_controlSocket, QIODevice::ReadWrite);
    QVERIFY(logsSocket.waitForConnected(5000));
    QByteArray logsBuffer;
    sendMessage(logsSocket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 1},
                 {QStringLiteral("method"), QStringLiteral("compose.logs.subscribe")},
                 {QStringLiteral("params"),
                  QJsonObject{{QStringLiteral("project"), QStringLiteral("mock-project")},
                              {QStringLiteral("service"), QStringLiteral("web")}}}});
    const QJsonObject logsResponse = readMessage(logsSocket, logsBuffer);
    QVERIFY(!logsResponse.value(QStringLiteral("result"))
                 .toObject()
                 .value(QStringLiteral("subscriptionId"))
                 .toString()
                 .isEmpty());
    int logDeltas = 0;
    for (int i = 0; i < 20 && logDeltas < 2; ++i) {
        const QJsonObject message = readMessage(logsSocket, logsBuffer);
        if (message.isEmpty()) {
            break;
        }
        if (message.value(QStringLiteral("method")).toString()
            == QStringLiteral("compose.logs.delta")) {
            ++logDeltas;
        }
    }
    QVERIFY(logDeltas >= 2);
    logsSocket.disconnectFromServer();

    QLocalSocket buildSocket;
    buildSocket.connectToServer(m_controlSocket, QIODevice::ReadWrite);
    QVERIFY(buildSocket.waitForConnected(5000));
    QByteArray buildBuffer;
    sendMessage(buildSocket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 1},
                 {QStringLiteral("method"), QStringLiteral("builds.start")},
                 {QStringLiteral("params"),
                  QJsonObject{{QStringLiteral("contextPath"), projectDir},
                              {QStringLiteral("tag"), QStringLiteral("localhost/mock:1")},
                              {QStringLiteral("dockerfile"), QStringLiteral("Dockerfile")},
                              {QStringLiteral("platform"), QStringLiteral("linux/amd64")},
                              {QStringLiteral("buildArgs"),
                               QJsonObject{{QStringLiteral("A"), QStringLiteral("B")}}}}}});
    const QJsonObject buildResponse = readMessage(buildSocket, buildBuffer);
    const QString buildId = buildResponse.value(QStringLiteral("result"))
                                .toObject()
                                .value(QStringLiteral("buildId"))
                                .toString();
    QVERIFY(!buildId.isEmpty());

    int buildDeltas = 0;
    QString finalStatus;
    for (int i = 0; i < 30; ++i) {
        const QJsonObject message = readMessage(buildSocket, buildBuffer);
        if (message.isEmpty()) {
            break;
        }
        const QString method = message.value(QStringLiteral("method")).toString();
        if (method == QStringLiteral("builds.delta")) {
            ++buildDeltas;
        } else if (method == QStringLiteral("builds.done")) {
            finalStatus = message.value(QStringLiteral("params"))
                              .toObject()
                              .value(QStringLiteral("status"))
                              .toString();
            break;
        }
    }
    QVERIFY(buildDeltas >= 2);
    QCOMPARE(finalStatus, QStringLiteral("success"));
    buildSocket.disconnectFromServer();

    const QJsonObject list = client.call(QStringLiteral("builds.list"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(list.value(QStringLiteral("items")).toArray().size() >= 1);

    const QJsonObject build =
        client.call(QStringLiteral("builds.get"),
                    QJsonObject{{QStringLiteral("id"), buildId}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(build.value(QStringLiteral("status")).toString(), QStringLiteral("success"));
    QCOMPARE(build.value(QStringLiteral("imageRef")).toString(),
             QStringLiteral("localhost/mock:1"));

    const QJsonObject removed =
        client.call(QStringLiteral("builds.remove"),
                    QJsonObject{{QStringLiteral("id"), buildId}}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(removed.value(QStringLiteral("removed")).toBool());

    PodStack::Error cancelError;
    client.call(QStringLiteral("builds.cancel"),
                QJsonObject{{QStringLiteral("id"), QStringLiteral("missing")}},
                &cancelError);
    QCOMPARE(cancelError.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    PodStack::Error missingBuild;
    client.call(QStringLiteral("builds.get"),
                QJsonObject{{QStringLiteral("id"), QStringLiteral("missing")}},
                &missingBuild);
    QCOMPARE(missingBuild.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    QVERIFY(client.call(QStringLiteral("compose.down"),
                        QJsonObject{{QStringLiteral("projectPath"), projectDir}},
                        &error, 60000)
                .value(QStringLiteral("ok"))
                .toBool());

    PodStack::Error unknownService;
    client.call(QStringLiteral("compose.logs.subscribe"),
                QJsonObject{{QStringLiteral("project"), QStringLiteral("mock-project")},
                            {QStringLiteral("service"), QStringLiteral("missing")}},
                &unknownService);
    QCOMPARE(unknownService.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    QLocalSocket failBuildSocket;
    failBuildSocket.connectToServer(m_controlSocket, QIODevice::ReadWrite);
    QVERIFY(failBuildSocket.waitForConnected(5000));
    QByteArray failBuildBuffer;
    sendMessage(failBuildSocket,
                {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                 {QStringLiteral("id"), 1},
                 {QStringLiteral("method"), QStringLiteral("builds.start")},
                 {QStringLiteral("params"),
                  QJsonObject{{QStringLiteral("contextPath"),
                               QStringLiteral("/nonexistent-context")},
                              {QStringLiteral("tag"), QStringLiteral("localhost/mock:fail")}}}});
    const QJsonObject failBuildResponse = readMessage(failBuildSocket, failBuildBuffer);
    QVERIFY(!failBuildResponse.value(QStringLiteral("result"))
                 .toObject()
                 .value(QStringLiteral("buildId"))
                 .toString()
                 .isEmpty());
    QString failStatus;
    for (int i = 0; i < 30; ++i) {
        const QJsonObject message = readMessage(failBuildSocket, failBuildBuffer);
        if (message.isEmpty()) {
            break;
        }
        if (message.value(QStringLiteral("method")).toString()
            == QStringLiteral("builds.done")) {
            failStatus = message.value(QStringLiteral("params"))
                             .toObject()
                             .value(QStringLiteral("status"))
                             .toString();
            break;
        }
    }
    QCOMPARE(failStatus, QStringLiteral("failed"));
    failBuildSocket.disconnectFromServer();

    PodStack::Error emptyCompose;
    client.call(QStringLiteral("compose.up"), {}, &emptyCompose);
    QCOMPARE(emptyCompose.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    const QString failDir = m_testDataDir + QStringLiteral("/fail-project");
    QDir().mkpath(failDir);
    PodStack::Error failCompose;
    client.call(QStringLiteral("compose.up"),
                QJsonObject{{QStringLiteral("projectPath"), failDir}}, &failCompose, 60000);
    QCOMPARE(failCompose.code, QStringLiteral("PODSTACK_COMPOSE_ERROR"));

    PodStack::Error emptyLogs;
    client.call(QStringLiteral("compose.logs.subscribe"), {}, &emptyLogs);
    QCOMPARE(emptyLogs.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    PodStack::Error unknownLogs;
    client.call(QStringLiteral("compose.logs.subscribe"),
                QJsonObject{{QStringLiteral("project"), QStringLiteral("unknown")}},
                &unknownLogs);
    QCOMPARE(unknownLogs.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    PodStack::Error emptyCancel;
    client.call(QStringLiteral("builds.cancel"), {}, &emptyCancel);
    QCOMPARE(emptyCancel.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    PodStack::Error emptyGet;
    client.call(QStringLiteral("builds.get"), {}, &emptyGet);
    QCOMPARE(emptyGet.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    PodStack::Error emptyRemove;
    client.call(QStringLiteral("builds.remove"), {}, &emptyRemove);
    QCOMPARE(emptyRemove.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
}

void RpcTest::setupFlow()
{
    PodStack::RpcClient client(m_controlSocket);
    PodStack::Error error;

    const QJsonObject status = client.call(QStringLiteral("setup.status"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(status.contains(QStringLiteral("completed")));
    QVERIFY(status.contains(QStringLiteral("showOnboarding")));
    QVERIFY(status.contains(QStringLiteral("summary")));
    QVERIFY(status.value(QStringLiteral("checks")).toArray().size() >= 8);

    const QJsonObject check = client.call(QStringLiteral("setup.check"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    const QJsonArray checks = check.value(QStringLiteral("checks")).toArray();
    QVERIFY(checks.size() >= 8);
    QStringList ids;
    for (const QJsonValue &value : checks) {
        const QJsonObject object = value.toObject();
        ids.append(object.value(QStringLiteral("id")).toString());
        QVERIFY(object.contains(QStringLiteral("status")));
        QVERIFY(object.contains(QStringLiteral("detail")));
    }
    QVERIFY(ids.contains(QStringLiteral("podman")));
    QVERIFY(ids.contains(QStringLiteral("docker")));
    QVERIFY(ids.contains(QStringLiteral("kvm")));
    QVERIFY(ids.contains(QStringLiteral("qemu")));
    QVERIFY(ids.contains(QStringLiteral("sqlite")));

    const QJsonObject validate = client.call(QStringLiteral("setup.validate"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QVERIFY(validate.contains(QStringLiteral("ok")));
    QVERIFY(validate.contains(QStringLiteral("warnings")));

    QJsonObject invalidAction;
    invalidAction.insert(QStringLiteral("action"), QStringLiteral("nope"));
    (void)client.call(QStringLiteral("setup.apply"), invalidAction, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    error = {};
    (void)client.call(QStringLiteral("setup.removeDocker"), {}, &error);
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CONFIRMATION_REQUIRED"));

    error = {};
    QJsonObject skipped;
    skipped.insert(QStringLiteral("skipped"), true);
    const QJsonObject skippedResult =
        client.call(QStringLiteral("setup.complete"), skipped, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(skippedResult.value(QStringLiteral("skipped")).toBool(), true);

    const QJsonObject afterSkip = client.call(QStringLiteral("setup.status"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(afterSkip.value(QStringLiteral("completed")).toBool(), false);
    QCOMPARE(afterSkip.value(QStringLiteral("showOnboarding")).toBool(), false);

    QJsonObject completed;
    completed.insert(QStringLiteral("skipped"), false);
    const QJsonObject completedResult =
        client.call(QStringLiteral("setup.complete"), completed, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(completedResult.value(QStringLiteral("completed")).toBool(), true);

    const QJsonObject afterComplete = client.call(QStringLiteral("setup.status"), {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(afterComplete.value(QStringLiteral("completed")).toBool(), true);
    QVERIFY(!afterComplete.value(QStringLiteral("completedAt")).isNull());
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
