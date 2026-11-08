#include "MockPodmanServer.h"

#include "podman/PodmanClient.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTest>

namespace {

class ContractTest : public QObject {
    Q_OBJECT

private slots:
    void containerInfoJson();
    void pingListAndLifecycle();
    void createContainer();
    void execLogsAndEvents();
    void files();
    void errorMapping();
    void pingFailure();
};

void ContractTest::containerInfoJson()
{
    PodStack::ContainerInfo info;
    info.id = QStringLiteral("abc123");
    info.name = QStringLiteral("demo");
    info.image = QStringLiteral("alpine:3.20");
    info.state = QStringLiteral("running");
    info.status = QStringLiteral("Up");
    info.created = QStringLiteral("2026-01-01T00:00:00Z");
    info.ports = {QStringLiteral("8080->80/tcp")};
    info.labels = {{QStringLiteral("dev.podstack.test"), QStringLiteral("1")}};

    const QJsonObject json = info.toJson();
    QCOMPARE(json.value(QStringLiteral("id")).toString(), QStringLiteral("abc123"));
    QCOMPARE(json.value(QStringLiteral("name")).toString(), QStringLiteral("demo"));
    QCOMPARE(json.value(QStringLiteral("ports")).toArray().size(), 1);
    QCOMPARE(json.value(QStringLiteral("labels")).toObject().size(), 1);
}

void ContractTest::pingListAndLifecycle()
{
    MockPodmanServer server;
    QVERIFY(server.isListening());

    PodStack::PodmanClient client(server.socketPath());
    PodStack::Error error;

    QVERIFY2(client.ping(&error), qPrintable(error.message));
    QCOMPARE(client.apiVersion(), QStringLiteral("v5.8.9"));

    const QVector<PodStack::ContainerInfo> containers = client.listContainers(true, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(containers.size(), 1);
    QCOMPARE(containers.first().name, QStringLiteral("demo"));
    QCOMPARE(containers.first().health, QStringLiteral("healthy"));
    QCOMPARE(containers.first().ports.size(), 1);

    QVERIFY2(client.startContainer(QStringLiteral("abc123"), &error),
             qPrintable(error.message));
    QVERIFY2(client.stopContainer(QStringLiteral("abc123"), 5, &error),
             qPrintable(error.message));
    QVERIFY2(client.restartContainer(QStringLiteral("abc123"), 5, &error),
             qPrintable(error.message));
    QVERIFY2(client.removeContainer(QStringLiteral("abc123"), true, true, &error),
             qPrintable(error.message));
    QVERIFY2(client.pauseContainer(QStringLiteral("abc123"), &error), qPrintable(error.message));
    QVERIFY2(client.unpauseContainer(QStringLiteral("abc123"), &error), qPrintable(error.message));
    QVERIFY2(client.killContainer(QStringLiteral("abc123"), QStringLiteral("SIGTERM"), &error),
             qPrintable(error.message));
    QVERIFY2(client.renameContainer(QStringLiteral("abc123"), QStringLiteral("renamed"), &error),
             qPrintable(error.message));
    QVERIFY(!client.renameContainer(QStringLiteral("abc123"), {}, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    const QJsonObject inspected = client.inspectContainer(QStringLiteral("abc123"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(inspected.value(QStringLiteral("Id")).toString(), QStringLiteral("abc123"));
}

void ContractTest::createContainer()
{
    MockPodmanServer server;
    PodStack::PodmanClient client(server.socketPath());
    PodStack::Error error;

    PodStack::ContainerCreateSpec spec;
    spec.image = QStringLiteral("docker.io/library/alpine:3.20");
    spec.name = QStringLiteral("created-demo");
    spec.command = QStringLiteral("sleep 10");
    spec.env.insert(QStringLiteral("A"), QStringLiteral("B"));
    spec.ports.append({18080, 80, QStringLiteral("tcp"), QStringLiteral("0.0.0.0")});
    spec.mounts.append({QStringLiteral("/tmp"), QStringLiteral("/mnt/host"), false});
    spec.workDir = QStringLiteral("/tmp");
    spec.user = QStringLiteral("0:0");
    spec.remove = true;
    spec.init = true;
    spec.privileged = true;

    const QString id = client.createContainer(spec, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(id, QStringLiteral("created123"));

    const QJsonObject body = QJsonDocument::fromJson(server.lastBody()).object();
    QCOMPARE(body.value(QStringLiteral("image")).toString(), spec.image);
    QCOMPARE(body.value(QStringLiteral("name")).toString(), spec.name);
    QCOMPARE(body.value(QStringLiteral("command")).toArray().size(), 2);
    QCOMPARE(body.value(QStringLiteral("portmappings")).toArray().size(), 1);
    QCOMPARE(body.value(QStringLiteral("mounts")).toArray().size(), 1);
    QCOMPARE(body.value(QStringLiteral("work_dir")).toString(), spec.workDir);
    QCOMPARE(body.value(QStringLiteral("user")).toString(), spec.user);
    QVERIFY(body.value(QStringLiteral("remove")).toBool());
    QVERIFY(body.value(QStringLiteral("init")).toBool());
    QVERIFY(body.value(QStringLiteral("privileged")).toBool());
}

void ContractTest::execLogsAndEvents()
{
    MockPodmanServer server;
    PodStack::PodmanClient client(server.socketPath());
    PodStack::Error error;

    PodStack::ExecResult exec;
    QVERIFY2(client.execOnce(QStringLiteral("abc123"),
                             {QStringLiteral("/bin/echo"), QStringLiteral("hello")}, &exec,
                             &error),
             qPrintable(error.message));
    QVERIFY(exec.output.contains("mock-exec-output"));
    QCOMPARE(exec.exitCode, 0);

    QStringList logLines;
    QVERIFY2(client.streamLogs(
                 QStringLiteral("abc123"), 10, false,
                 [&logLines](const QByteArray &data) {
                     const QString line = QString::fromUtf8(data).trimmed();
                     if (!line.isEmpty()) {
                         logLines.append(line);
                     }
                     return true;
                 },
                 nullptr, &error),
             qPrintable(error.message));
    QVERIFY(logLines.contains(QStringLiteral("line-one")));
    QVERIFY(logLines.contains(QStringLiteral("line-two")));

    QStringList statsLines;
    QVERIFY2(client.streamStats(
                 QStringLiteral("abc123"), 1,
                 [&statsLines](const QByteArray &line) {
                     statsLines.append(QString::fromUtf8(line));
                     return true;
                 },
                 nullptr, &error),
             qPrintable(error.message));
    QVERIFY(!statsLines.isEmpty());
    QVERIFY(statsLines.first().contains(QStringLiteral("cpu_percent")));

    QStringList events;
    QVERIFY2(client.streamEvents(
                 [&events](const QByteArray &data) {
                     events.append(QString::fromUtf8(data));
                     return true;
                 },
                 nullptr, &error),
             qPrintable(error.message));
    QVERIFY(!events.isEmpty());
    QVERIFY(events.join(QString()).contains(QStringLiteral("container")));
}

void ContractTest::files()
{
    MockPodmanServer server;
    PodStack::PodmanClient client(server.socketPath());
    PodStack::Error error;

    const QVector<PodStack::FileEntry> entries =
        client.listFiles(QStringLiteral("abc123"), QStringLiteral("/"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(entries.size(), 2);

    bool foundFile = false;
    bool foundDirectory = false;
    for (const PodStack::FileEntry &entry : entries) {
        if (entry.name == QStringLiteral("file.txt")) {
            foundFile = true;
            QCOMPARE(entry.size, 5);
            QVERIFY(!entry.directory);
        }
        if (entry.name == QStringLiteral("dir")) {
            foundDirectory = true;
            QVERIFY(entry.directory);
        }
    }
    QVERIFY(foundFile);
    QVERIFY(foundDirectory);

    const QString destination = QDir::tempPath()
                                + QStringLiteral("/podstack-file-%1.txt")
                                      .arg(QCoreApplication::applicationPid());
    QFile::remove(destination);
    QVERIFY2(client.downloadFile(QStringLiteral("abc123"), QStringLiteral("/file.txt"),
                                 destination, &error),
             qPrintable(error.message));

    QFile file(destination);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArrayLiteral("hello"));
    file.close();
    QFile::remove(destination);

    QVERIFY(!client.downloadFile(QStringLiteral("abc123"), QStringLiteral("/missing.txt"),
                                 destination, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
}

void ContractTest::errorMapping()
{
    MockPodmanServer server;
    PodStack::PodmanClient client(server.socketPath());
    PodStack::Error error;

    QVERIFY(!client.startContainer(QStringLiteral("missing"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    QVERIFY(!client.startContainer(QStringLiteral("fail"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_ENGINE_ERROR"));

    PodStack::ExecResult exec;
    QVERIFY(!client.execOnce(QStringLiteral("abc123"), {}, &exec, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY(client.inspectContainer(QStringLiteral("missing"), &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));
}

void ContractTest::pingFailure()
{
    QString socketPath;
    {
        MockPodmanServer server;
        socketPath = server.socketPath();
    }

    PodStack::PodmanClient client(socketPath);
    PodStack::Error error;
    QVERIFY(!client.ping(&error));
    QVERIFY(!error.isEmpty());
}

} // namespace

QTEST_GUILESS_MAIN(ContractTest)
#include "test_contract.moc"
