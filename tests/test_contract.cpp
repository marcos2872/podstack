#include "MockPodmanServer.h"

#include "podman/PodmanClient.h"

#include <QTest>

namespace {

class ContractTest : public QObject {
    Q_OBJECT

private slots:
    void pingListAndStart();
};

void ContractTest::pingListAndStart()
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
    QCOMPARE(server.lastMethod(), QStringLiteral("POST"));
    QVERIFY(server.lastPath().endsWith(QStringLiteral("/containers/abc123/start")));
}

} // namespace

QTEST_GUILESS_MAIN(ContractTest)
#include "test_contract.moc"
