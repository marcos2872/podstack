#include "core/Config.h"
#include "core/Error.h"
#include "core/Logging.h"
#include "core/Paths.h"
#include "core/Proxy.h"
#include "core/RpcClient.h"
#include "core/RpcStream.h"
#include "core/Settings.h"
#include "core/SetupActions.h"
#include "core/SetupPolicy.h"
#include "daemon/BuildStore.h"
#include "daemon/ManifestBuilder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMap>
#include <QScopeGuard>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <thread>

#include <sqlite3.h>

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
    void configSchemaCoversDocumentedKeys();
    void configValidation();
    void configResetPreservesSetup();
    void proxyValidation();
    void proxyEnvironment();
    void setupActionsMapPodmanStatus();
    void setupPolicyMandatoryChecks();
    void pathOverrides();
    void buildStorePlatforms();
    void buildStoreErrors();
    void manifestBuilderValidation();
    void manifestBuilderBuild();
    void rpcStreamProtocol();
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

void CoreTest::configSchemaCoversDocumentedKeys()
{
    const QVector<PodStack::SettingDef> &definitions = PodStack::Settings::definitions();
    QCOMPARE(definitions.size(), 83);

    QSet<QString> keys;
    for (const PodStack::SettingDef &definition : definitions) {
        QVERIFY2(!definition.key.isEmpty(), "chave vazia no schema");
        QVERIFY2(!keys.contains(definition.key),
                 qPrintable(QStringLiteral("chave duplicada: %1").arg(definition.key)));
        keys.insert(definition.key);
        QVERIFY(!definition.category.isEmpty());
        QVERIFY(!definition.description.isEmpty());
    }

    const QStringList expected = {
        QStringLiteral("app.startAtLogin"),
        QStringLiteral("app.theme"),
        QStringLiteral("engine.mode"),
        QStringLiteral("engine.socketPath"),
        QStringLiteral("docker.enableContainerdImageStore"),
        QStringLiteral("network.proxy"),
        QStringLiteral("network.proxyExclude"),
        QStringLiteral("network.httpsPort"),
        QStringLiteral("machines.memoryBytes"),
        QStringLiteral("kubernetes.tlsSan"),
        QStringLiteral("ssh.keyType"),
        QStringLiteral("terminal.fontSize"),
        QStringLiteral("logs.searchMode"),
        QStringLiteral("updates.mode"),
        QStringLiteral("updates.lastCheck"),
        QStringLiteral("telemetry.enabled"),
        QStringLiteral("advanced.logLevel"),
        QStringLiteral("setup.completed"),
        QStringLiteral("setup.components"),
    };
    for (const QString &key : expected) {
        QVERIFY2(keys.contains(key), qPrintable(QStringLiteral("faltou %1").arg(key)));
    }

    const PodStack::Config config;
    for (const PodStack::SettingDef &definition : definitions) {
        QVERIFY2(config.get(definition.key) == definition.defaultValue,
                 qPrintable(QStringLiteral("default divergente em %1").arg(definition.key)));
    }
    QCOMPARE(config.get(QStringLiteral("schemaVersion")).toInt(), 1);

    const QJsonObject schema = PodStack::Config::schema();
    QCOMPARE(schema.value(QStringLiteral("schema")).toArray().size(), 83);
    const PodStack::SettingDef *proxy = PodStack::Settings::find(QStringLiteral("network.proxy"));
    QVERIFY(proxy != nullptr);
    QVERIFY(proxy->secret);
    const PodStack::SettingDef *engine =
        PodStack::Settings::find(QStringLiteral("engine.mode"));
    QVERIFY(engine != nullptr);
    QVERIFY(engine->restartRequired);
}

void CoreTest::configValidation()
{
    PodStack::Error error;

    QVERIFY(!PodStack::Config::validate(QStringLiteral("does.not.exist"),
                                        QStringLiteral("x"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_VALIDATION"));
    QCOMPARE(error.detail, QStringLiteral("does.not.exist"));

    QVERIFY(PodStack::Config::validate(QStringLiteral("app.theme"),
                                       QStringLiteral("dark"), &error));
    QVERIFY(error.isEmpty());
    QVERIFY(!PodStack::Config::validate(QStringLiteral("app.theme"),
                                        QStringLiteral("neon"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_VALIDATION"));

    QVERIFY(!PodStack::Config::validate(QStringLiteral("terminal.fontSize"),
                                        QStringLiteral("12"), &error));
    QVERIFY(!PodStack::Config::validate(QStringLiteral("terminal.fontSize"), 7, &error));
    QVERIFY(!PodStack::Config::validate(QStringLiteral("terminal.fontSize"), 33, &error));
    QVERIFY(!PodStack::Config::validate(QStringLiteral("terminal.fontSize"), 12.5, &error));
    QVERIFY(PodStack::Config::validate(QStringLiteral("terminal.fontSize"), 8, &error));
    QVERIFY(PodStack::Config::validate(QStringLiteral("terminal.fontSize"), 32, &error));

    QVERIFY(!PodStack::Config::validate(QStringLiteral("network.httpPort"), 0, &error));
    QVERIFY(!PodStack::Config::validate(QStringLiteral("network.httpPort"), 65536, &error));
    QVERIFY(PodStack::Config::validate(QStringLiteral("network.httpPort"), 65535, &error));

    QVERIFY(!PodStack::Config::validate(QStringLiteral("network.proxy"),
                                        QStringLiteral("ftp://127.0.0.1:21"), &error));
    QVERIFY(PodStack::Config::validate(QStringLiteral("network.proxy"),
                                       QStringLiteral("auto"), &error));
    QVERIFY(PodStack::Config::validate(QStringLiteral("network.proxy"),
                                       QStringLiteral("socks5://127.0.0.1:1080"), &error));

    QVERIFY(!PodStack::Config::validate(QStringLiteral("network.subnet4"),
                                        QStringLiteral("8.8.8.0/24"), &error));
    QVERIFY(PodStack::Config::validate(QStringLiteral("network.subnet4"),
                                       QStringLiteral("192.168.50.0/24"), &error));

    QVERIFY(!PodStack::Config::validate(
        QStringLiteral("network.proxyExclude"),
        QJsonArray{QStringLiteral("bad host")}, &error));
    QVERIFY(PodStack::Config::validate(
        QStringLiteral("network.proxyExclude"),
        QJsonArray{QStringLiteral("localhost"), QStringLiteral(".internal"),
                   QStringLiteral("10.0.0.0/8")},
        &error));

    QVERIFY(!PodStack::Config::validate(QStringLiteral("network.proxyExclude"),
                                        QJsonArray{QStringLiteral("a"),
                                                   QStringLiteral("a")},
                                        &error));
    QVERIFY(!PodStack::Config::validate(QStringLiteral("machines.memoryBytes"),
                                        1073741823, &error));
    QVERIFY(PodStack::Config::validate(QStringLiteral("machines.memoryBytes"),
                                       1073741824, &error));
    QVERIFY(!PodStack::Config::validate(QStringLiteral("machines.memoryBytes"),
                                        QStringLiteral("4GiB"), &error));

    QVERIFY(!PodStack::Config::validate(QStringLiteral("updates.lastCheck"),
                                        QStringLiteral("ontem"), &error));
    QVERIFY(PodStack::Config::validate(QStringLiteral("updates.lastCheck"),
                                       QJsonValue::Null, &error));
    QVERIFY(PodStack::Config::validate(QStringLiteral("updates.lastCheck"),
                                       QStringLiteral("2026-11-09T12:00:00Z"), &error));

    QVERIFY(!PodStack::Config::validate(QStringLiteral("engine.socketPath"),
                                        QString(), &error));
    QVERIFY(PodStack::Config::validate(QStringLiteral("engine.socketPath"),
                                       QJsonValue::Null, &error));
}

void CoreTest::configResetPreservesSetup()
{
    PodStack::Config config;
    config.set(QStringLiteral("app.theme"), QStringLiteral("dark"));
    config.set(QStringLiteral("engine.mode"), QStringLiteral("rootful"));
    config.set(QStringLiteral("network.proxy"), QStringLiteral("none"));
    config.set(QStringLiteral("setup.completed"), true);
    config.set(QStringLiteral("setup.completedAt"),
               QStringLiteral("2026-11-09T12:00:00Z"));
    config.set(QStringLiteral("setup.components"), QJsonArray{QStringLiteral("caddy")});

    PodStack::Error error;
    QVERIFY(config.reset(QStringLiteral("app.theme"), &error));
    QCOMPARE(config.get(QStringLiteral("app.theme")).toString(), QStringLiteral("system"));
    QCOMPARE(config.get(QStringLiteral("engine.mode")).toString(), QStringLiteral("rootful"));
    QVERIFY(!config.reset(QStringLiteral("does.not.exist"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_VALIDATION"));

    config.resetAll();
    QCOMPARE(config.get(QStringLiteral("schemaVersion")).toInt(), 1);
    QCOMPARE(config.get(QStringLiteral("app.theme")).toString(), QStringLiteral("system"));
    QCOMPARE(config.get(QStringLiteral("engine.mode")).toString(),
             QStringLiteral("rootless"));
    QCOMPARE(config.get(QStringLiteral("network.proxy")).toString(), QStringLiteral("auto"));
    QCOMPARE(config.get(QStringLiteral("setup.completed")).toBool(), true);
    QCOMPARE(config.get(QStringLiteral("setup.completedAt")).toString(),
             QStringLiteral("2026-11-09T12:00:00Z"));
    QCOMPARE(config.get(QStringLiteral("setup.components")).toArray().size(), 1);
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

void CoreTest::buildStorePlatforms()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString dbPath = directory.filePath(QStringLiteral("builds.db"));
    PodStack::Error error;
    {
        PodStack::BuildStore store(dbPath);
        QVERIFY2(store.open(&error), qPrintable(error.message));

        PodStack::BuildRecord record;
        record.id = QStringLiteral("build-platforms");
        record.imageRef = QStringLiteral("localhost/podstack:multi");
        record.status = QStringLiteral("success");
        record.startedAt = QStringLiteral("2026-11-09T12:00:00.000Z");
        record.durationMs = 1200;
        record.trigger = QStringLiteral("cli");
        record.contextPath = QStringLiteral("/tmp/context");
        record.logPath = QStringLiteral("/tmp/build.log");
        record.platforms = {QStringLiteral("linux/amd64"), QStringLiteral("linux/arm64")};
        QVERIFY2(store.insert(record, &error), qPrintable(error.message));

        const PodStack::BuildRecord loaded = store.get(record.id, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error.message));
        QCOMPARE(loaded.platforms, record.platforms);

        const QVector<PodStack::BuildRecord> items = store.list(10, 0, &error);
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.first().platforms, record.platforms);

        const QJsonArray platforms =
            loaded.toJson().value(QStringLiteral("platforms")).toArray();
        QCOMPARE(platforms.size(), 2);
        QCOMPARE(platforms.at(0).toString(), QStringLiteral("linux/amd64"));
        QCOMPARE(platforms.at(1).toString(), QStringLiteral("linux/arm64"));
    }

    // Banco criado antes da coluna platforms: a migração deve ser idempotente.
    const QString legacyPath = directory.filePath(QStringLiteral("legacy.db"));
    sqlite3 *legacyDb = nullptr;
    QCOMPARE(sqlite3_open(legacyPath.toUtf8().constData(), &legacyDb), SQLITE_OK);
    const char *legacySchema = R"SQL(
CREATE TABLE builds (
    id TEXT PRIMARY KEY,
    image_ref TEXT NOT NULL DEFAULT '',
    status TEXT NOT NULL,
    started_at TEXT NOT NULL,
    finished_at TEXT,
    duration_ms INTEGER NOT NULL DEFAULT 0,
    trigger TEXT NOT NULL DEFAULT 'gui',
    context_path TEXT NOT NULL DEFAULT '',
    dockerfile TEXT NOT NULL DEFAULT 'Dockerfile',
    log_path TEXT NOT NULL DEFAULT '',
    error TEXT
);
INSERT INTO builds (id, image_ref, status, started_at, log_path)
VALUES ('legacy-build', 'localhost/legacy:1', 'failed', '2026-11-08T10:00:00.000Z', '');
)SQL";
    QCOMPARE(sqlite3_exec(legacyDb, legacySchema, nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(legacyDb);

    PodStack::BuildStore legacyStore(legacyPath);
    QVERIFY2(legacyStore.open(&error), qPrintable(error.message));
    PodStack::BuildRecord legacyRecord = legacyStore.get(QStringLiteral("legacy-build"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    QCOMPARE(legacyRecord.imageRef, QStringLiteral("localhost/legacy:1"));
    QVERIFY(legacyRecord.platforms.isEmpty());

    legacyRecord.platforms = {QStringLiteral("linux/arm64")};
    QVERIFY2(legacyStore.insert(legacyRecord, &error), qPrintable(error.message));
    QCOMPARE(legacyStore.get(QStringLiteral("legacy-build"), &error).platforms,
             QStringList{QStringLiteral("linux/arm64")});
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
}

void CoreTest::buildStoreErrors()
{
    PodStack::Error error;
    PodStack::BuildStore invalid(QStringLiteral("/dev/null/builds.db"));
    QVERIFY(!invalid.open(&error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DB_ERROR"));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString corruptPath = directory.filePath(QStringLiteral("corrupt.db"));
    QFile corrupt(corruptPath);
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    corrupt.write("not a sqlite database");
    corrupt.close();
    error = {};
    PodStack::BuildStore corrupted(corruptPath);
    QVERIFY(!corrupted.open(&error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DB_ERROR"));

    const QString dbPath = directory.filePath(QStringLiteral("builds.db"));
    PodStack::BuildStore store(dbPath);
    QVERIFY2(store.open(&error), qPrintable(error.message));
    QVERIFY(store.list(10, 0, &error).isEmpty());

    error = {};
    QVERIFY(store.get(QStringLiteral("missing"), &error).id.isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_NOT_FOUND"));

    QVERIFY(store.remove(QStringLiteral("missing"), &error));
    QVERIFY(store.finish(QStringLiteral("missing"), QStringLiteral("failed"), {}, 0, &error));

    PodStack::BuildStepRecord step;
    step.id = QStringLiteral("step-1");
    step.buildId = QStringLiteral("missing-build");
    step.name = QStringLiteral("RUN");
    error = {};
    QVERIFY(!store.addStep(step, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_DB_ERROR"));
}

void CoreTest::manifestBuilderValidation()
{
    PodStack::Error error;
    QVERIFY(PodStack::ManifestBuilder::validatePlatforms(
        {QStringLiteral("linux/amd64"), QStringLiteral("linux/arm64/v8")}, &error));
    QVERIFY(error.isEmpty());

    QVERIFY(!PodStack::ManifestBuilder::validatePlatforms({}, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QVERIFY(!PodStack::ManifestBuilder::validatePlatforms(
        {QStringLiteral("linux/amd64/extra/v8")}, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QStringList tooMany;
    for (int i = 0; i < 9; ++i) {
        tooMany.append(QStringLiteral("linux/arch%1").arg(i));
    }
    QVERIFY(!PodStack::ManifestBuilder::validatePlatforms(tooMany, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile dockerfile(directory.filePath(QStringLiteral("Dockerfile")));
    QVERIFY(dockerfile.open(QIODevice::WriteOnly));
    dockerfile.write("FROM alpine:3.20\n");
    dockerfile.close();

    const QByteArray previous = qgetenv("PODSTACK_PODMAN_CLI");
    qputenv("PODSTACK_PODMAN_CLI", "/nonexistent/podstack-podman");
    PodStack::ManifestBuilder builder;
    PodStack::Error unsupported;
    const bool ok = builder.build(
        directory.path(), QStringLiteral("Dockerfile"), QStringLiteral("localhost/test:multi"),
        {}, {QStringLiteral("linux/amd64"), QStringLiteral("linux/arm64")},
        [](const QString &) {}, {}, &unsupported);
    if (previous.isNull()) {
        qunsetenv("PODSTACK_PODMAN_CLI");
    } else {
        qputenv("PODSTACK_PODMAN_CLI", previous);
    }

    QVERIFY(!ok);
    QCOMPARE(unsupported.code, QStringLiteral("PODSTACK_UNSUPPORTED"));
    QVERIFY(!unsupported.remediation.isEmpty());
}

void CoreTest::manifestBuilderBuild()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile dockerfile(directory.filePath(QStringLiteral("Dockerfile")));
    QVERIFY(dockerfile.open(QIODevice::WriteOnly));
    dockerfile.write("FROM alpine:3.20\n");
    dockerfile.close();

    const QString logPath = directory.filePath(QStringLiteral("podman.log"));
    const QString cli = directory.filePath(QStringLiteral("podman-fake.sh"));
    QFile script(cli);
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write(QStringLiteral("#!/bin/sh\n"
                                "for arg in \"$@\"; do printf '%s\\n' \"$arg\" >> \"%1\"; done\n"
                                "if [ \"$1\" = 'build' ]; then\n"
                                "  echo 'line one'\n"
                                "  echo 'line two'\n"
                                "fi\n"
                                "exit 0\n")
                     .arg(logPath)
                     .toUtf8());
    script.close();
    QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                  QFileDevice::ExeOwner));

    const QByteArray previous = qgetenv("PODSTACK_PODMAN_CLI");
    qputenv("PODSTACK_PODMAN_CLI", cli.toUtf8());
    const auto restore = qScopeGuard([previous] {
        if (previous.isEmpty()) {
            qunsetenv("PODSTACK_PODMAN_CLI");
        } else {
            qputenv("PODSTACK_PODMAN_CLI", previous);
        }
    });

    PodStack::ManifestBuilder builder;
    QStringList lines;
    PodStack::Error error;
    QVERIFY2(builder.build(
                 directory.path(), QStringLiteral("Dockerfile"),
                 QStringLiteral("localhost/test:multi"),
                 {{QStringLiteral("A"), QStringLiteral("B")}},
                 {QStringLiteral("linux/amd64"), QStringLiteral("linux/arm64")},
                 [&lines](const QString &line) { lines.append(line); }, {}, &error),
             qPrintable(error.message));
    QCOMPARE(lines, QStringList({QStringLiteral("line one"), QStringLiteral("line two")}));

    QFile log(logPath);
    QVERIFY(log.open(QIODevice::ReadOnly));
    const QStringList args =
        QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    log.close();
    QVERIFY(args.contains(QStringLiteral("build")));
    QVERIFY(args.contains(QStringLiteral("--manifest")));
    QVERIFY(args.contains(QStringLiteral("linux/amd64,linux/arm64")));
    QVERIFY(args.contains(QStringLiteral("--build-arg")));
    QVERIFY(args.contains(QStringLiteral("A=B")));

    // Cancelamento antes de iniciar.
    error = {};
    QVERIFY(!builder.build(
        directory.path(), QStringLiteral("Dockerfile"), QStringLiteral("localhost/test:multi"), {},
        {QStringLiteral("linux/amd64")}, {}, [] { return true; }, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_CANCELLED"));

    // Contexto/Dockerfile/tag inválidos.
    error = {};
    QVERIFY(!builder.build(directory.filePath(QStringLiteral("missing")),
                           QStringLiteral("Dockerfile"), QStringLiteral("tag"), {},
                           {QStringLiteral("linux/amd64")}, {}, {}, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    error = {};
    QVERIFY(!builder.build(directory.path(), QStringLiteral("Missingfile"), QStringLiteral("tag"),
                           {}, {QStringLiteral("linux/amd64")}, {}, {}, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    error = {};
    QVERIFY(!builder.build(directory.path(), QStringLiteral("Dockerfile"),
                           QStringLiteral("-bad-tag"), {}, {QStringLiteral("linux/amd64")}, {}, {},
                           &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
}

void CoreTest::rpcStreamProtocol()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const auto runServer = [](const QString &path, const QByteArray &payload) {
        std::thread serverThread([path, payload] {
            QLocalServer server;
            if (!server.listen(path)) {
                return;
            }
            if (!server.waitForNewConnection(5000)) {
                return;
            }
            QLocalSocket *socket = server.nextPendingConnection();
            if (!socket) {
                return;
            }
            socket->waitForReadyRead(5000);
            socket->write(payload);
            socket->flush();
            socket->waitForBytesWritten(5000);
            socket->waitForDisconnected(1000);
        });
        for (int attempt = 0; attempt < 100 && !QFileInfo::exists(path); ++attempt) {
            QTest::qWait(10);
        }
        return serverThread;
    };

    const QString successPath = directory.filePath(QStringLiteral("success.sock"));
    const QByteArray successPayload =
        QByteArrayLiteral("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"subscriptionId\":\"s\"}}\n"
                          "{\"jsonrpc\":\"2.0\",\"method\":\"x.delta\",\"params\":{\"v\":1}}\n"
                          "{\"jsonrpc\":\"2.0\",\"method\":\"x.done\",\"params\":{}}\n");
    std::thread successServer = runServer(successPath, successPayload);

    PodStack::RpcStream stream(successPath);
    QStringList events;
    PodStack::Error error;
    const bool ok = stream.run(
        QStringLiteral("x"), {},
        [&events](const QString &method, const QJsonObject &) { events.append(method); }, &error,
        5000);
    successServer.join();
    QVERIFY2(ok, qPrintable(error.message));
    QCOMPARE(events, QStringList({QStringLiteral("x.delta"), QStringLiteral("x.done")}));

    const QString errorPath = directory.filePath(QStringLiteral("error.sock"));
    const QByteArray errorPayload =
        QByteArrayLiteral("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"subscriptionId\":\"s\"}}\n"
                          "{\"jsonrpc\":\"2.0\",\"method\":\"x.error\",\"params\":{\"error\":"
                          "{\"code\":\"PODSTACK_IO_ERROR\",\"message\":\"falhou\"}}}\n");
    std::thread errorServer = runServer(errorPath, errorPayload);

    PodStack::RpcStream failing(errorPath);
    error = {};
    QVERIFY(!failing.run(QStringLiteral("x"), {}, {}, &error, 5000));
    errorServer.join();
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
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
