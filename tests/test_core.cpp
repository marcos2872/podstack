#include "core/Config.h"
#include "core/Error.h"
#include "core/Logging.h"
#include "core/Paths.h"
#include "core/Proxy.h"
#include "core/RpcClient.h"
#include "core/SetupActions.h"
#include "core/SetupPolicy.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QScopeGuard>
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
    void configMergePreservesDefaults();
    void configSetupDefaults();
    void configInvalidFallsBack();
    void proxyValidation();
    void proxyEnvironment();
    void setupActionsMapPodmanStatus();
    void setupPolicyMandatoryChecks();
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
    QCOMPARE(config.get(QStringLiteral("app.keepRunningInTray")).toBool(), true);
    QCOMPARE(config.get(QStringLiteral("app.showInTray")).toBool(), true);
    QCOMPARE(config.get(QStringLiteral("network.proxy")).toString(), QStringLiteral("auto"));
    QVERIFY(config.get(QStringLiteral("network.proxyExclude")).toArray().isEmpty());
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

void CoreTest::configMergePreservesDefaults()
{
    QDir(PodStack::Paths::configDir()).removeRecursively();
    QDir().mkpath(PodStack::Paths::configDir());
    const QString path = PodStack::Paths::configDir() + QStringLiteral("/config.json");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"app":{"theme":"dark"}})");
    file.close();

    const PodStack::Config loaded = PodStack::Config::load();
    QCOMPARE(loaded.get(QStringLiteral("app.theme")).toString(), QStringLiteral("dark"));
    QCOMPARE(loaded.get(QStringLiteral("app.language")).toString(), QStringLiteral("pt-BR"));
    QCOMPARE(loaded.get(QStringLiteral("app.showOnboarding")).toBool(), true);
    QCOMPARE(loaded.get(QStringLiteral("app.keepRunningInTray")).toBool(), true);
    QCOMPARE(loaded.get(QStringLiteral("app.showInTray")).toBool(), true);
    QCOMPARE(loaded.get(QStringLiteral("setup.completed")).toBool(), false);
    QCOMPARE(loaded.get(QStringLiteral("engine.mode")).toString(), QStringLiteral("rootless"));

    QDir(PodStack::Paths::configDir()).removeRecursively();
}

void CoreTest::configSetupDefaults()
{
    PodStack::Config config;
    QCOMPARE(config.get(QStringLiteral("app.showOnboarding")).toBool(), true);
    QCOMPARE(config.get(QStringLiteral("setup.completed")).toBool(), false);
    QCOMPARE(config.get(QStringLiteral("setup.dockerPolicy")).toString(), QStringLiteral("keep"));
    QVERIFY(config.get(QStringLiteral("setup.completedAt")).isNull());
    QVERIFY(config.get(QStringLiteral("setup.lastValidation")).isNull());
    QCOMPARE(config.get(QStringLiteral("setup.components")).toArray().size(), 0);

    config.set(QStringLiteral("setup.completed"), true);
    config.set(QStringLiteral("setup.dockerPolicy"), QStringLiteral("removed"));
    QString error;
    QVERIFY2(config.save(&error), qPrintable(error));

    const PodStack::Config loaded = PodStack::Config::load();
    QCOMPARE(loaded.get(QStringLiteral("setup.completed")).toBool(), true);
    QCOMPARE(loaded.get(QStringLiteral("setup.dockerPolicy")).toString(),
             QStringLiteral("removed"));
    QCOMPARE(loaded.get(QStringLiteral("app.theme")).toString(), QStringLiteral("system"));

    QDir(PodStack::Paths::configDir()).removeRecursively();
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

void CoreTest::proxyValidation()
{
    PodStack::Error error;
    QVERIFY(PodStack::Proxy::validate(QStringLiteral("none"), {}, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(PodStack::Proxy::validate(QStringLiteral("auto"), {}, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(PodStack::Proxy::validate(QStringLiteral("http://127.0.0.1:3128"), {}, &error));
    QVERIFY(PodStack::Proxy::validate(QStringLiteral("https://proxy.example:8443"), {}, &error));
    QVERIFY(PodStack::Proxy::validate(QStringLiteral("http://127.0.0.1:3128/"), {}, &error));
    QVERIFY(PodStack::Proxy::validate(QStringLiteral("socks5://127.0.0.1:1080"), {}, &error));
    QVERIFY(PodStack::Proxy::validate(
        QStringLiteral("socks5h://user:pass@127.0.0.1:1080"), {}, &error));

    QVERIFY(PodStack::Proxy::validate(
        QStringLiteral("http://127.0.0.1:3128"),
        {QStringLiteral("localhost"), QStringLiteral(".internal"),
         QStringLiteral("*.example.com"), QStringLiteral("10.0.0.0/8"),
         QStringLiteral("192.168.1.10"), QStringLiteral("::1"),
         QStringLiteral("2001:db8::/32")},
        &error));
    QVERIFY(error.isEmpty());

    const QStringList invalidProxies = {
        QString(),
        QStringLiteral("ftp://127.0.0.1:21"),
        QStringLiteral("socks4://127.0.0.1:1080"),
        QStringLiteral("http://"),
        QStringLiteral("http://bad host:3128"),
        QStringLiteral("auto "),
    };
    for (const QString &proxy : invalidProxies) {
        QVERIFY2(!PodStack::Proxy::validate(proxy, {}, &error), qPrintable(proxy));
        QCOMPARE(error.code, QStringLiteral("PODSTACK_VALIDATION"));
    }

    const QStringList invalidExcludes = {
        QString(),
        QStringLiteral("bad host"),
        QStringLiteral("a,b"),
        QStringLiteral("http://example.com"),
        QStringLiteral("10.0.0.0/33"),
        QStringLiteral("2001:db8::/129"),
        QStringLiteral("10.0.0.0/"),
        QStringLiteral("*.example.com."),
        QStringLiteral("*.*.example.com"),
        QString::fromUtf8("example.com\n"),
    };
    for (const QString &entry : invalidExcludes) {
        QVERIFY2(!PodStack::Proxy::validate(QStringLiteral("auto"), {entry}, &error),
                 qPrintable(entry));
        QCOMPARE(error.code, QStringLiteral("PODSTACK_VALIDATION"));
    }

    QCOMPARE(PodStack::Proxy::defaultExcludes(),
             QStringList({QStringLiteral("localhost"), QStringLiteral("127.0.0.1"),
                          QStringLiteral("::1"), QStringLiteral("*.localhost")}));
}

void CoreTest::proxyEnvironment()
{
    const QStringList keys = {
        QStringLiteral("HTTP_PROXY"),  QStringLiteral("HTTPS_PROXY"),
        QStringLiteral("ALL_PROXY"),   QStringLiteral("NO_PROXY"),
        QStringLiteral("http_proxy"),  QStringLiteral("https_proxy"),
        QStringLiteral("all_proxy"),   QStringLiteral("no_proxy"),
    };
    QMap<QString, QByteArray> saved;
    for (const QString &key : keys) {
        if (qEnvironmentVariableIsSet(key.toUtf8().constData())) {
            saved.insert(key, qgetenv(key.toUtf8().constData()));
            qunsetenv(key.toUtf8().constData());
        }
    }
    const auto restore = qScopeGuard([&saved, &keys] {
        for (const QString &key : keys) {
            qunsetenv(key.toUtf8().constData());
        }
        for (auto it = saved.constBegin(); it != saved.constEnd(); ++it) {
            qputenv(it.key().toUtf8().constData(), it.value());
        }
    });

    QVERIFY(PodStack::Proxy::environment(QStringLiteral("none"), {}).isEmpty());

    qputenv("HTTP_PROXY", "http://daemon:8080");
    const QMap<QString, QString> automatic =
        PodStack::Proxy::environment(QStringLiteral("auto"), {});
    QCOMPARE(automatic.size(), 1);
    QCOMPARE(automatic.value(QStringLiteral("HTTP_PROXY")),
             QStringLiteral("http://daemon:8080"));
    QVERIFY(!automatic.contains(QStringLiteral("HTTPS_PROXY")));

    const QString proxy = QStringLiteral("http://127.0.0.1:3128");
    QMap<QString, QString> values =
        PodStack::Proxy::environment(proxy, {QStringLiteral("localhost"),
                                             QStringLiteral(".internal"),
                                             QStringLiteral("10.0.0.0/8")});
    QCOMPARE(values.value(QStringLiteral("HTTP_PROXY")), proxy);
    QCOMPARE(values.value(QStringLiteral("HTTPS_PROXY")), proxy);
    QCOMPARE(values.value(QStringLiteral("ALL_PROXY")), proxy);
    QCOMPARE(values.value(QStringLiteral("http_proxy")), proxy);
    QCOMPARE(values.value(QStringLiteral("https_proxy")), proxy);
    QCOMPARE(values.value(QStringLiteral("all_proxy")), proxy);
    QCOMPARE(values.value(QStringLiteral("NO_PROXY")),
             QStringLiteral("localhost,127.0.0.1,::1,*.localhost,.internal,10.0.0.0/8"));
    QCOMPARE(values.value(QStringLiteral("no_proxy")),
             values.value(QStringLiteral("NO_PROXY")));

    qputenv("NO_PROXY", "existing.local, localhost, 10.0.0.0/8");
    qputenv("no_proxy", "other.local");
    values = PodStack::Proxy::environment(proxy, {QStringLiteral(".internal"),
                                                  QStringLiteral("10.0.0.0/8")});
    QCOMPARE(values.value(QStringLiteral("NO_PROXY")),
             QStringLiteral("localhost,127.0.0.1,::1,*.localhost,.internal,10.0.0.0/8,"
                            "existing.local,other.local"));
    QCOMPARE(values.value(QStringLiteral("no_proxy")),
             values.value(QStringLiteral("NO_PROXY")));

    const QMap<QString, QString> automaticWithNoProxy =
        PodStack::Proxy::environment(QStringLiteral("auto"), {});
    QCOMPARE(automaticWithNoProxy.value(QStringLiteral("NO_PROXY")),
             QStringLiteral("existing.local, localhost, 10.0.0.0/8"));
    QCOMPARE(automaticWithNoProxy.value(QStringLiteral("no_proxy")),
             QStringLiteral("other.local"));
}

void CoreTest::setupActionsMapPodmanStatus()
{
    QCOMPARE(PodStack::SetupActions::podmanActionForStatus(QStringLiteral("missing")),
             QStringLiteral("install-podman"));
    QCOMPARE(PodStack::SetupActions::podmanActionForStatus(QStringLiteral("warn")),
             QStringLiteral("enable-socket"));
    QVERIFY(PodStack::SetupActions::podmanActionForStatus(QStringLiteral("ok")).isEmpty());
    QVERIFY(PodStack::SetupActions::podmanActionForStatus(QString()).isEmpty());
}

void CoreTest::setupPolicyMandatoryChecks()
{
    for (const QString &id : {QStringLiteral("podman"), QStringLiteral("systemd-user"),
                              QStringLiteral("sqlite"), QStringLiteral("qemu"),
                              QStringLiteral("ovmf"), QStringLiteral("virtiofsd")}) {
        QVERIFY2(PodStack::SetupPolicy::isMandatoryCheck(id), qPrintable(id));
        QVERIFY2(PodStack::SetupPolicy::isCheckSatisfied(id, QStringLiteral("ok")),
                 qPrintable(id));
        QVERIFY2(!PodStack::SetupPolicy::isCheckSatisfied(id, QStringLiteral("missing")),
                 qPrintable(id));
    }

    for (const QString &id : {QStringLiteral("kvm"), QStringLiteral("docker"),
                              QStringLiteral("caddy"), QStringLiteral("k3s")}) {
        QVERIFY2(!PodStack::SetupPolicy::isMandatoryCheck(id), qPrintable(id));
        QVERIFY2(PodStack::SetupPolicy::isCheckSatisfied(id, QStringLiteral("warn")),
                 qPrintable(id));
    }
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
