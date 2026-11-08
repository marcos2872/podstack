#include "core/Config.h"
#include "core/Error.h"
#include "core/Logging.h"
#include "core/Paths.h"
#include "core/RpcClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTest>

namespace {

class CoreTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void errorSerializes();
    void configNestedSet();
    void configDefaults();
    void configSaveLoad();
    void configInvalidFallsBack();
    void pathOverrides();
    void rpcClientUnavailable();
    void loggingInit();
};

void CoreTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QDir(PodStack::Paths::configDir()).removeRecursively();
    QDir(PodStack::Paths::dataDir()).removeRecursively();
    QDir(PodStack::Paths::stateDir()).removeRecursively();
}

void CoreTest::cleanupTestCase()
{
    QDir(PodStack::Paths::configDir()).removeRecursively();
    QDir(PodStack::Paths::dataDir()).removeRecursively();
    QDir(PodStack::Paths::stateDir()).removeRecursively();
    QStandardPaths::setTestModeEnabled(false);
}

void CoreTest::errorSerializes()
{
    const PodStack::Error error =
        PodStack::Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                              QStringLiteral("Nome em uso."), QStringLiteral("detail"),
                              QStringLiteral("Escolha outro nome."), false);
    const QJsonObject json = error.toJson();
    QCOMPARE(json.value(QStringLiteral("code")).toString(), QStringLiteral("PODSTACK_CONFLICT"));
    QCOMPARE(json.value(QStringLiteral("message")).toString(), QStringLiteral("Nome em uso."));
    QCOMPARE(json.value(QStringLiteral("detail")).toString(), QStringLiteral("detail"));
    QCOMPARE(json.value(QStringLiteral("remediation")).toString(),
             QStringLiteral("Escolha outro nome."));
    QCOMPARE(json.value(QStringLiteral("retryable")).toBool(), false);
}

void CoreTest::configNestedSet()
{
    PodStack::Config config;
    config.set(QStringLiteral("app.theme"), QStringLiteral("dark"));
    config.set(QStringLiteral("network.subnet4"), QStringLiteral("10.0.0.0/8"));

    QCOMPARE(config.get(QStringLiteral("app.theme")).toString(), QStringLiteral("dark"));
    QCOMPARE(config.get(QStringLiteral("network.subnet4")).toString(),
             QStringLiteral("10.0.0.0/8"));
    QVERIFY(config.get(QStringLiteral("does.not.exist")).isNull());
}

void CoreTest::configDefaults()
{
    PodStack::Config config;
    QCOMPARE(config.get(QStringLiteral("schemaVersion")).toInt(), 1);
    QCOMPARE(config.get(QStringLiteral("engine.mode")).toString(), QStringLiteral("rootless"));
    QCOMPARE(config.get(QStringLiteral("telemetry.enabled")).toBool(), false);
}

void CoreTest::configSaveLoad()
{
    QDir(PodStack::Paths::configDir()).removeRecursively();

    PodStack::Config config;
    config.set(QStringLiteral("app.theme"), QStringLiteral("dark"));
    config.set(QStringLiteral("network.subnet4"), QStringLiteral("10.1.0.0/16"));

    QString error;
    QVERIFY2(config.save(&error), qPrintable(error));

    const PodStack::Config loaded = PodStack::Config::load();
    QCOMPARE(loaded.get(QStringLiteral("app.theme")).toString(), QStringLiteral("dark"));
    QCOMPARE(loaded.get(QStringLiteral("network.subnet4")).toString(),
             QStringLiteral("10.1.0.0/16"));
}

void CoreTest::configInvalidFallsBack()
{
    QDir().mkpath(PodStack::Paths::configDir());
    const QString path = PodStack::Paths::configDir() + QStringLiteral("/config.json");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not json");
    file.close();

    const PodStack::Config loaded = PodStack::Config::load();
    QCOMPARE(loaded.get(QStringLiteral("schemaVersion")).toInt(), 1);
    QCOMPARE(loaded.get(QStringLiteral("engine.mode")).toString(), QStringLiteral("rootless"));
}

void CoreTest::pathOverrides()
{
    qputenv("PODSTACK_PODMAN_SOCKET", "/tmp/podstack-test-podman.sock");
    QCOMPARE(PodStack::Paths::podmanSocket(), QStringLiteral("/tmp/podstack-test-podman.sock"));
    qunsetenv("PODSTACK_PODMAN_SOCKET");

    qputenv("PODSTACK_CONTROL_SOCKET", "/tmp/podstack-test-control.sock");
    QCOMPARE(PodStack::Paths::controlSocket(), QStringLiteral("/tmp/podstack-test-control.sock"));
    qunsetenv("PODSTACK_CONTROL_SOCKET");

    QString error;
    QVERIFY2(PodStack::Paths::ensureDirs(&error), qPrintable(error));
    QVERIFY(QFileInfo::exists(PodStack::Paths::configDir()));
    QVERIFY(QFileInfo::exists(PodStack::Paths::runtimeDir()));
}

void CoreTest::rpcClientUnavailable()
{
    const QString socketPath = QDir::tempPath()
                               + QStringLiteral("/podstack-missing-%1.sock")
                                     .arg(QCoreApplication::applicationPid());
    QFile::remove(socketPath);

    PodStack::RpcClient client(socketPath);
    PodStack::Error error;
    client.call(QStringLiteral("system.hello"), {}, &error, 200);

    QVERIFY(!error.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DAEMON_UNAVAILABLE"));
}

void CoreTest::loggingInit()
{
    PodStack::Logging::init(true);
    PodStack::Logging::init(false);
}

} // namespace

QTEST_GUILESS_MAIN(CoreTest)
#include "test_core.moc"
