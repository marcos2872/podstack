#include "MockPodmanServer.h"

#include "podman/PodmanClient.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QTest>

namespace {

class ContractTest : public QObject {
    Q_OBJECT

private slots:
    void containerInfoJson();
    void pingListAndLifecycle();
    void execLogsAndEvents();
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
    QCOMPARE(containers.first().ports.size(), 1);

    QVERIFY2(client.startContainer(QStringLiteral("abc123"), &error),
             qPrintable(error.message));
    QVERIFY2(client.stopContainer(QStringLiteral("abc123"), 5, &error),
             qPrintable(error.message));
    QVERIFY2(client.restartContainer(QStringLiteral("abc123"), 5, &error),
             qPrintable(error.message));
    QVERIFY2(client.removeContainer(QStringLiteral("abc123"), true, true, &error),
             qPrintable(error.message));
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
