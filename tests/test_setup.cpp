// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Config.h"
#include "core/Paths.h"
#include "daemon/DbusUtils.h"
#include "daemon/DockerInventory.h"
#include "daemon/SetupManager.h"
#include "helper/CaTrust.h"
#include "helper/DnsZone.h"
#include "helper/Firewall.h"
#include "helper/HelperService.h"
#include "helper/Polkit.h"
#include "helper/PortBinding.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QScopeGuard>
#include <QSet>
#include <QSslCertificate>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>

#include "MockPodmanServer.h"

namespace {

bool writeExecutable(const QString &path, const QByteArray &body)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write("#!/bin/sh\n");
    file.write(body);
    file.write("\n");
    file.close();
    return file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                               | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                               | QFileDevice::ExeGroup | QFileDevice::ReadOther
                               | QFileDevice::ExeOther);
}

QString readLog(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll()).trimmed();
}

QStringList g_capturedMessages;

void captureMessage(QtMsgType, const QMessageLogContext &, const QString &message)
{
    g_capturedMessages.append(message);
}

QByteArray testCaPem()
{
    const QStringList bundles{QStringLiteral("/etc/pki/tls/certs/ca-bundle.crt"),
                              QStringLiteral("/etc/ssl/certs/ca-certificates.crt")};
    for (const QString &bundle : bundles) {
        const QList<QSslCertificate> certificates = QSslCertificate::fromPath(bundle);
        for (const QSslCertificate &certificate : certificates) {
            if (certificate.isSelfSigned()) {
                return certificate.toPem();
            }
        }
    }
    return {};
}

class EchoService : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "dev.podstack.DbusUtilsTest")

public slots:
    QVariantMap echo()
    {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("Autorização negada.")}};
    }
};

class AuthorizedHelper : public PodStack::HelperService {
public:
    using HelperService::HelperService;

protected:
    bool authorize(const QString &, QString *) const override { return true; }
};

class DnsZoneHelper : public PodStack::HelperService {
public:
    QString directory;
    mutable int reloads = 0;
    mutable bool avahi = false;

protected:
    bool authorize(const QString &, QString *) const override { return true; }
    QString resolvedDirectory() const override { return directory; }
    bool reloadResolved(QString *) const override
    {
        ++reloads;
        return true;
    }
    bool avahiActive() const override { return avahi; }
};

class CaTrustHelper : public PodStack::HelperService {
public:
    QString anchorsDirectory;
    mutable int trustUpdates = 0;

protected:
    bool authorize(const QString &, QString *) const override { return true; }
    QString caAnchorsDirectory() const override { return anchorsDirectory; }
    bool runTrustUpdate(QString *) const override
    {
        ++trustUpdates;
        return true;
    }
};

class PortBindingHelper : public PodStack::HelperService {
public:
    QString directory;
    mutable QStringList systemctlCalls;

protected:
    bool authorize(const QString &, QString *) const override { return true; }
    QString systemUnitDirectory() const override { return directory; }
    bool runSystemctl(const QStringList &arguments, QString *) const override
    {
        systemctlCalls.append(arguments.join(QLatin1Char(' ')));
        return true;
    }
};

class FakeSetupManager : public PodStack::SetupManager {
public:
    bool available = true;
    bool installOk = true;
    bool removeOk = true;
    mutable QStringList calls;

protected:
    bool helperAvailable() const override { return available; }

    bool helperCall(const QString &method, const QStringList &arguments, QString *error,
                    int) const override
    {
        calls.append(method + QLatin1Char(':') + arguments.join(QLatin1Char(',')));
        if (method == QStringLiteral("InstallPackages") && !installOk) {
            if (error) {
                *error = QStringLiteral("falha simulada ao instalar");
            }
            return false;
        }
        if (method == QStringLiteral("RemovePackages") && !removeOk) {
            if (error) {
                *error = QStringLiteral("falha simulada ao remover");
            }
            return false;
        }
        return true;
    }
};

class SetupTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void checkIncludesCoreComponents();
    void ubuntuOvmfAndPackages();
    void statusSummarizesChecks();
    void validateReportsWarnings();
    void rejectsInvalidActions();
    void jobLookupsAreSafe();
    void demarshalsHelperReply();
    void polkitRequestSignature();
    void firewallAvailablePrefersFirewalld();
    void firewallOpensAndClosesWithFirewalld();
    void firewallUsesUfw();
    void firewallRejectsInvalidInput();
    void firewallRollsBackPartialOpen();
    void firewallWithoutBackend();

    void helperRejectsPackagesOutsideAllowlist();
    void helperRequiresDBusAuthorization();
    void helperAuditsUid();
    void dockerInventoryCounts();
    void dockerInventoryNoSocket();
    void helperValidatesPorts();
    void dnsZoneInstallBackupAndRemove();
    void helperValidatesDnsZone();
    void helperInstallsDnsZone();
    void portBindingInstallAndRemove();
    void helperValidatesPortBindings();
    void helperBindsPorts();
    void caTrustInstallAndRemove();
    void helperValidatesCaTrust();
    void helperTrustsCa();
    void setupRunsSocketJob();
    void setupTracksActiveJob();
    void setupCompletesOnboarding();
    void helperInstallsPackages();
    void helperRemovesDocker();
    void setupApplyJobs();
    void setupApplyFailures();
    void setupRejectsUnavailableHelper();
};

void SetupTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QDir(PodStack::Paths::configDir()).removeRecursively();
}

void SetupTest::cleanupTestCase()
{
    // Os jobs do SetupManager rodam em QtConcurrent; espere antes de destruir
    // o manager/processo para não deixar uma thread usando o objeto.
    QThreadPool::globalInstance()->waitForDone(20000);
    QDir(PodStack::Paths::configDir()).removeRecursively();
    QStandardPaths::setTestModeEnabled(false);
}

void SetupTest::checkIncludesCoreComponents()
{
    PodStack::SetupManager manager;
    const QList<PodStack::SetupCheck> checks = manager.check(false);
    QVERIFY(checks.size() >= 9);

    QSet<QString> ids;
    for (const PodStack::SetupCheck &check : checks) {
        ids.insert(check.id);
        QVERIFY(!check.status.isEmpty());
    }
    for (const QString &id : {QStringLiteral("podman"), QStringLiteral("docker"),
                              QStringLiteral("kvm"), QStringLiteral("qemu"),
                              QStringLiteral("ovmf"), QStringLiteral("virtiofsd"),
                              QStringLiteral("sqlite"), QStringLiteral("caddy"),
                              QStringLiteral("k3s")}) {
        QVERIFY2(ids.contains(id), qPrintable(id));
    }
}

void SetupTest::statusSummarizesChecks()
{
    PodStack::SetupManager manager;
    const QJsonObject status = manager.status();
    QVERIFY(status.contains(QStringLiteral("completed")));
    QVERIFY(status.contains(QStringLiteral("showOnboarding")));
    QVERIFY(status.contains(QStringLiteral("dockerPolicy")));

    const QJsonArray checks = status.value(QStringLiteral("checks")).toArray();
    QVERIFY(checks.size() >= 9);

    const QJsonObject summary = status.value(QStringLiteral("summary")).toObject();
    QVERIFY(summary.contains(QStringLiteral("ok")));
    QVERIFY(summary.contains(QStringLiteral("warnings")));
    QVERIFY(summary.contains(QStringLiteral("missing")));
    QVERIFY(summary.contains(QStringLiteral("ready")));
}

void SetupTest::validateReportsWarnings()
{
    PodStack::SetupManager manager;
    const QJsonObject validation = manager.validate();
    QVERIFY(validation.contains(QStringLiteral("ok")));
    QVERIFY(validation.value(QStringLiteral("checks")).toArray().size() >= 9);
    QVERIFY(validation.value(QStringLiteral("warnings")).isArray());
}

void SetupTest::rejectsInvalidActions()
{
    PodStack::SetupManager manager;

    PodStack::Error error;
    QVERIFY(manager.apply(QJsonObject{{QStringLiteral("action"), QStringLiteral("bogus")}},
                          {}, &error)
                .isEmpty());
    QVERIFY(!error.isEmpty());

    error = {};
    QVERIFY(manager.removeDocker({}, {}, &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void SetupTest::jobLookupsAreSafe()
{
    PodStack::SetupManager manager;
    QVERIFY(manager.jobStatus(QStringLiteral("missing")).isEmpty());
    QVERIFY(manager.activeJob().isEmpty());

    const QJsonObject cancelled = manager.cancel(
        QJsonObject{{QStringLiteral("jobId"), QStringLiteral("missing")}});
    QCOMPARE(cancelled.value(QStringLiteral("cancelled")).toBool(), false);

    bool notified = false;
    manager.subscribe(QStringLiteral("missing"),
                      [&notified](const QString &, const QJsonObject &) { notified = true; });
    QVERIFY(!notified);
}

void SetupTest::demarshalsHelperReply()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        QSKIP("Barramento de sessão indisponível");
    }

    const QString service =
        QStringLiteral("dev.podstack.DbusUtilsTest.p%1")
            .arg(QCoreApplication::applicationPid());
    const QString path = QStringLiteral("/test");
    EchoService echo;
    QVERIFY2(bus.registerService(service), qPrintable(bus.lastError().message()));
    QVERIFY(bus.registerObject(path, &echo, QDBusConnection::ExportAllSlots));
    const auto cleanup = qScopeGuard([&bus, &path, &service] {
        bus.unregisterObject(path);
        bus.unregisterService(service);
    });

    QDBusInterface interface(service, path, QStringLiteral("dev.podstack.DbusUtilsTest"), bus);
    QVERIFY(interface.isValid());
    const QDBusMessage reply = interface.call(QStringLiteral("echo"));
    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    QVERIFY(!reply.arguments().isEmpty());

    const QVariantMap map =
        PodStack::DbusUtils::variantMapFromReply(reply.arguments().value(0));
    QCOMPARE(map.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(map.value(QStringLiteral("message")).toString(),
             QStringLiteral("Autorização negada."));
}

void SetupTest::polkitRequestSignature()
{
    const QDBusMessage request = PodStack::checkAuthorizationRequest(
        QStringLiteral("dev.podstack.helper.install-packages"),
        QCoreApplication::applicationPid());
    QCOMPARE(request.member(), QStringLiteral("CheckAuthorization"));
    QCOMPARE(request.arguments().size(), 5);
    QCOMPARE(QDBusMetaType::typeToSignature(request.arguments().at(2).metaType()),
             QStringLiteral("a{ss}"));
}

void SetupTest::firewallAvailablePrefersFirewalld()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeExecutable(dir.filePath(QStringLiteral("firewall-cmd")),
                            QByteArrayLiteral("exit 0")));
    QVERIFY(writeExecutable(dir.filePath(QStringLiteral("ufw")), QByteArrayLiteral("exit 0")));

    const QByteArray previousPath = qgetenv("PATH");
    qputenv("PATH", dir.path().toUtf8());
    const auto restore = qScopeGuard([previousPath] { qputenv("PATH", previousPath); });

    QString backend;
    QVERIFY(PodStack::Firewall::available(&backend));
    QCOMPARE(backend, QStringLiteral("firewalld"));
}

void SetupTest::firewallOpensAndClosesWithFirewalld()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log = dir.filePath(QStringLiteral("firewall.log"));
    QVERIFY(writeExecutable(dir.filePath(QStringLiteral("firewall-cmd")),
                            QByteArrayLiteral("printf '%s\\n' \"$*\" >> \"$PODSTACK_FW_LOG\"\n"
                                              "exit 0")));

    const QByteArray previousPath = qgetenv("PATH");
    qputenv("PATH", dir.path().toUtf8());
    qputenv("PODSTACK_FW_LOG", log.toUtf8());
    const auto restore = qScopeGuard([previousPath] {
        qputenv("PATH", previousPath);
        qunsetenv("PODSTACK_FW_LOG");
    });

    QString backend;
    QVERIFY(PodStack::Firewall::available(&backend));
    QCOMPARE(backend, QStringLiteral("firewalld"));

    PodStack::Error error;
    QVERIFY2(PodStack::Firewall::openPorts({QStringLiteral("8080"), QStringLiteral("8443")},
                                           QStringLiteral("tcp"), &error),
             qPrintable(error.message));
    QVERIFY(error.isEmpty());
    const QStringList opened = readLog(log).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(opened, QStringList({QStringLiteral("--add-port=8080/tcp"),
                                  QStringLiteral("--add-port=8443/tcp")}));
    QVERIFY(!readLog(log).contains(QStringLiteral("--permanent")));

    QFile::remove(log);
    QVERIFY2(PodStack::Firewall::closePorts({QStringLiteral("8080"), QStringLiteral("8443")},
                                            QStringLiteral("tcp"), &error),
             qPrintable(error.message));
    const QStringList closed = readLog(log).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(closed, QStringList({QStringLiteral("--remove-port=8080/tcp"),
                                  QStringLiteral("--remove-port=8443/tcp")}));
}

void SetupTest::firewallUsesUfw()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log = dir.filePath(QStringLiteral("ufw.log"));
    QVERIFY(writeExecutable(dir.filePath(QStringLiteral("ufw")),
                            QByteArrayLiteral("printf '%s\\n' \"$*\" >> \"$PODSTACK_FW_LOG\"\n"
                                              "exit 0")));

    const QByteArray previousPath = qgetenv("PATH");
    qputenv("PATH", dir.path().toUtf8());
    qputenv("PODSTACK_FW_LOG", log.toUtf8());
    const auto restore = qScopeGuard([previousPath] {
        qputenv("PATH", previousPath);
        qunsetenv("PODSTACK_FW_LOG");
    });

    QString backend;
    QVERIFY(PodStack::Firewall::available(&backend));
    QCOMPARE(backend, QStringLiteral("ufw"));

    PodStack::Error error;
    QVERIFY2(PodStack::Firewall::openPorts({QStringLiteral("8080")},
                                           QStringLiteral("udp"), &error),
             qPrintable(error.message));
    QCOMPARE(readLog(log), QStringLiteral("allow 8080/udp"));

    QFile::remove(log);
    QVERIFY2(PodStack::Firewall::closePorts({QStringLiteral("8080")},
                                            QStringLiteral("udp"), &error),
             qPrintable(error.message));
    QCOMPARE(readLog(log), QStringLiteral("delete allow 8080/udp"));
}

void SetupTest::firewallRejectsInvalidInput()
{
    PodStack::Error error;
    QVERIFY(!PodStack::Firewall::openPorts({}, QStringLiteral("tcp"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    for (const QString &port : {QStringLiteral("0"), QStringLiteral("65536"),
                                QStringLiteral("abc"), QStringLiteral("-1")}) {
        error = {};
        QVERIFY(!PodStack::Firewall::openPorts({port}, QStringLiteral("tcp"), &error));
        QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
    }

    error = {};
    QVERIFY(!PodStack::Firewall::openPorts({QStringLiteral("8080")},
                                           QStringLiteral("sctp"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));

    QStringList many;
    for (int port = 1; port <= 33; ++port) {
        many.append(QString::number(port));
    }
    error = {};
    QVERIFY(!PodStack::Firewall::openPorts(many, QStringLiteral("tcp"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_INVALID_PARAMS"));
}

void SetupTest::firewallRollsBackPartialOpen()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log = dir.filePath(QStringLiteral("firewall.log"));
    QVERIFY(writeExecutable(
        dir.filePath(QStringLiteral("firewall-cmd")),
        QByteArrayLiteral("printf '%s\\n' \"$*\" >> \"$PODSTACK_FW_LOG\"\n"
                          "case \"$*\" in *--add-port=8443/tcp*) "
                          "echo 'porta ocupada' >&2; exit 1;; esac\n"
                          "exit 0")));

    const QByteArray previousPath = qgetenv("PATH");
    qputenv("PATH", dir.path().toUtf8());
    qputenv("PODSTACK_FW_LOG", log.toUtf8());
    const auto restore = qScopeGuard([previousPath] {
        qputenv("PATH", previousPath);
        qunsetenv("PODSTACK_FW_LOG");
    });

    PodStack::Error error;
    QVERIFY(!PodStack::Firewall::openPorts({QStringLiteral("8080"), QStringLiteral("8443")},
                                           QStringLiteral("tcp"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
    QVERIFY(error.detail.contains(QStringLiteral("porta ocupada")));

    const QStringList lines = readLog(log).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(lines, QStringList({QStringLiteral("--add-port=8080/tcp"),
                                 QStringLiteral("--add-port=8443/tcp"),
                                 QStringLiteral("--remove-port=8080/tcp")}));
}

void SetupTest::firewallWithoutBackend()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QByteArray previousPath = qgetenv("PATH");
    qputenv("PATH", dir.path().toUtf8());
    const auto restore = qScopeGuard([previousPath] { qputenv("PATH", previousPath); });

    QString backend = QStringLiteral("stale");
    QVERIFY(!PodStack::Firewall::available(&backend));
    QVERIFY(backend.isEmpty());

    PodStack::Error error;
    QVERIFY(!PodStack::Firewall::openPorts({QStringLiteral("8080")},
                                           QStringLiteral("tcp"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_HELPER_REQUIRED"));
}

void SetupTest::helperRejectsPackagesOutsideAllowlist()
{
    PodStack::HelperService helper;

    QVariantMap reply = helper.InstallPackages({});
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("message")).toString(),
             QStringLiteral("Nenhum pacote informado."));

    reply = helper.InstallPackages({QStringLiteral("nginx")});
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(reply.value(QStringLiteral("message"))
                .toString()
                .contains(QStringLiteral("fora da allowlist")));

    reply = helper.RemovePackages({});
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);

    reply = helper.RemovePackages({QStringLiteral("podman")});
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(reply.value(QStringLiteral("message"))
                .toString()
                .contains(QStringLiteral("fora da allowlist")));
}

void SetupTest::helperRequiresDBusAuthorization()
{
    PodStack::HelperService helper;

    QVariantMap reply = helper.InstallPackages({QStringLiteral("podman")});
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("message")).toString(),
             QStringLiteral("Chamada local não autorizada."));

    reply = helper.RemovePackages({QStringLiteral("docker-ce")});
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("message")).toString(),
             QStringLiteral("Chamada local não autorizada."));
}

void SetupTest::helperAuditsUid()
{
    PodStack::HelperService helper;
    g_capturedMessages.clear();
    const QtMessageHandler previous = qInstallMessageHandler(captureMessage);
    const QVariantMap reply = helper.InstallPackages({QStringLiteral("podman")});
    qInstallMessageHandler(previous);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    const QString log = g_capturedMessages.join(QLatin1Char('\n'));
    QVERIFY2(log.contains(QStringLiteral("installation requested: uid=")), qPrintable(log));
    QVERIFY2(log.contains(QStringLiteral("installation denied: uid=")), qPrintable(log));
}

void SetupTest::dockerInventoryCounts()
{
    MockPodmanServer server;
    QVERIFY(server.isListening());
    server.setResponse(QStringLiteral("/system/df"), 200, R"({
        "LayersSize": 5000000,
        "Containers": [
            {"Id":"a","State":"running","SizeRw":100},
            {"Id":"b","State":"exited","SizeRw":200}
        ],
        "Images": [{"Id":"i1"},{"Id":"i2"}],
        "Volumes": [{"Name":"v1","UsageData":{"Size":3000}}]
    })");
    server.setResponse(QStringLiteral("/networks"), 200, R"([{"Id":"n1"},{"Id":"n2"}])");

    PodStack::DockerInventory inventory(server.socketPath());
    const PodStack::DockerImpact impact = inventory.inspect();

    QVERIFY2(impact.available, qPrintable(impact.error));
    QCOMPARE(impact.containers, 2);
    QCOMPARE(impact.running, 1);
    QCOMPARE(impact.images, 2);
    QCOMPARE(impact.networks, 2);
    QCOMPARE(impact.volumes, 1);
    QCOMPARE(impact.estimatedSizeBytes, 5003300);

    const QJsonObject json = impact.toJson();
    QCOMPARE(
        json.value(QStringLiteral("containers")).toObject().value(QStringLiteral("total")).toInt(),
        2);
    QCOMPARE(json.value(QStringLiteral("available")).toBool(), true);
}

void SetupTest::dockerInventoryNoSocket()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PodStack::DockerInventory inventory(dir.filePath(QStringLiteral("missing.sock")));

    const PodStack::DockerImpact impact = inventory.inspect(200);
    QCOMPARE(impact.available, false);
    QVERIFY(!impact.error.isEmpty());
    QCOMPARE(impact.toJson().value(QStringLiteral("available")).toBool(), false);
}

void SetupTest::helperValidatesPorts()
{
    PodStack::HelperService helper;

    QVariantMap reply = helper.OpenPorts({}, QStringLiteral("tcp"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(!reply.value(QStringLiteral("message")).toString().isEmpty());

    reply = helper.OpenPorts({QStringLiteral("0")}, QStringLiteral("tcp"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);

    reply = helper.OpenPorts({QStringLiteral("8080")}, QStringLiteral("sctp"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);

    reply =
        helper.OpenPorts({QStringLiteral("8080"), QStringLiteral("8443")}, QStringLiteral("TCP"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("message")).toString(),
             QStringLiteral("Chamada local não autorizada."));

    reply = helper.ClosePorts({QStringLiteral("8081")}, QStringLiteral("udp"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("message")).toString(),
             QStringLiteral("Chamada local não autorizada."));
}

void SetupTest::dnsZoneInstallBackupAndRemove()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("podstack.conf"));
    QString error;

    QVERIFY(PodStack::DnsZone::install(dir.path(), QStringLiteral("podstack.local"),
                                       QStringLiteral("127.0.0.1:5353"), &error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray content = file.readAll();
    file.close();
    QVERIFY(content.contains("[Resolve]"));
    QVERIFY(content.contains("DNS=127.0.0.1:5353"));
    QVERIFY(content.contains("Domains=~podstack.local"));

    // Zona/upstream fora da allowlist.
    QVERIFY(!PodStack::DnsZone::validate(QStringLiteral("evil.local"),
                                         QStringLiteral("127.0.0.1:5353"), &error));
    QVERIFY(!PodStack::DnsZone::validate(QStringLiteral("podstack.local"),
                                         QStringLiteral("8.8.8.8:53"), &error));
    QVERIFY(!PodStack::DnsZone::install(dir.path(), QStringLiteral("evil.local"),
                                        QStringLiteral("127.0.0.1:5353"), &error));

    // Instalação idempotente e backup do conteúdo anterior.
    QVERIFY(PodStack::DnsZone::install(dir.path(), QStringLiteral("podstack.local"),
                                       QStringLiteral("127.0.0.1:5353"), &error));
    QFile truncate(path);
    QVERIFY(truncate.open(QIODevice::WriteOnly | QIODevice::Truncate));
    truncate.close();
    QVERIFY(PodStack::DnsZone::install(dir.path(), QStringLiteral("podstack.local"),
                                       QStringLiteral("127.0.0.1:5353"), &error));
    const QString backup = dir.filePath(QStringLiteral("podstack.conf.bak"));
    QVERIFY(QFile::exists(backup));
    QCOMPARE(QFileInfo(backup).size(), qint64(0));

    QVERIFY(PodStack::DnsZone::remove(dir.path(), QStringLiteral("podstack.local"), &error));
    QVERIFY(!QFile::exists(path));
    QVERIFY(PodStack::DnsZone::remove(dir.path(), QStringLiteral("podstack.local"), &error));
}

void SetupTest::helperValidatesDnsZone()
{
    PodStack::HelperService helper;

    QVariantMap reply =
        helper.InstallDnsZone(QStringLiteral("evil.local"), QStringLiteral("127.0.0.1:5353"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(
        reply.value(QStringLiteral("message")).toString().contains(QStringLiteral("allowlist")));

    reply = helper.InstallDnsZone(QStringLiteral("podstack.local"), QStringLiteral("8.8.8.8:53"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(
        reply.value(QStringLiteral("message")).toString().contains(QStringLiteral("allowlist")));

    reply =
        helper.InstallDnsZone(QStringLiteral("podstack.local"), QStringLiteral("127.0.0.1:5353"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("message")).toString(),
             QStringLiteral("Chamada local não autorizada."));

    reply = helper.RemoveDnsZone(QStringLiteral("park.local"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(
        reply.value(QStringLiteral("message")).toString().contains(QStringLiteral("allowlist")));
}

void SetupTest::helperInstallsDnsZone()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    DnsZoneHelper helper;
    helper.directory = dir.path();
    const QString path = dir.filePath(QStringLiteral("podstack.conf"));

    QVariantMap reply =
        helper.InstallDnsZone(QStringLiteral("podstack.local"), QStringLiteral("127.0.0.1:5353"));
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(helper.reloads, 1);
    QVERIFY(QFile::exists(path));
    QVERIFY(!reply.value(QStringLiteral("message")).toString().contains(QStringLiteral("Avahi")));

    helper.avahi = true;
    reply =
        helper.InstallDnsZone(QStringLiteral("podstack.local"), QStringLiteral("127.0.0.1:5353"));
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QVERIFY(reply.value(QStringLiteral("message")).toString().contains(QStringLiteral("Avahi")));

    reply = helper.RemoveDnsZone(QStringLiteral("podstack.local"));
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QVERIFY(!QFile::exists(path));
    QCOMPARE(helper.reloads, 3);

    reply = helper.RemoveDnsZone(QStringLiteral("podstack.local"));
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(helper.reloads, 4);
}

void SetupTest::portBindingInstallAndRemove()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;

    QVERIFY(PodStack::PortBinding::install(
        dir.path(), {QStringLiteral("80"), QStringLiteral("443")}, QStringLiteral("127.0.0.1:8080"),
        QStringLiteral("127.0.0.1:8443"), &error));
    QFile socket80(dir.filePath(QStringLiteral("podstack-http.socket")));
    QVERIFY(socket80.open(QIODevice::ReadOnly));
    QVERIFY(socket80.readAll().contains("ListenStream=80"));
    socket80.close();
    QFile service443(dir.filePath(QStringLiteral("podstack-https.service")));
    QVERIFY(service443.open(QIODevice::ReadOnly));
    const QByteArray serviceContent = service443.readAll();
    service443.close();
    QVERIFY(serviceContent.contains("systemd-socket-proxyd"));
    QVERIFY(serviceContent.contains("127.0.0.1:8443"));

    QVERIFY(!PodStack::PortBinding::validatePorts({QStringLiteral("8080")}, &error));
    QVERIFY(!PodStack::PortBinding::validateTarget(QStringLiteral("0.0.0.0:8080"), &error));
    QVERIFY(!PodStack::PortBinding::validateTarget(QStringLiteral("127.0.0.1:0"), &error));
    QVERIFY(!PodStack::PortBinding::validateTarget(QStringLiteral("127.0.0.1:70000"), &error));
    QVERIFY(!PodStack::PortBinding::install(dir.path(), {QStringLiteral("80")},
                                            QStringLiteral("192.168.1.1:8080"),
                                            QStringLiteral("127.0.0.1:8443"), &error));

    QVERIFY(PodStack::PortBinding::remove(dir.path(), {QStringLiteral("80"), QStringLiteral("443")},
                                          &error));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("podstack-http.socket"))));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("podstack-https.service"))));
    QVERIFY(PodStack::PortBinding::remove(dir.path(), {QStringLiteral("80")}, &error));
}

void SetupTest::helperValidatesPortBindings()
{
    PodStack::HelperService helper;

    QVariantMap reply = helper.BindPorts({QStringLiteral("8080")}, QStringLiteral("127.0.0.1:8080"),
                                         QStringLiteral("127.0.0.1:8443"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(
        reply.value(QStringLiteral("message")).toString().contains(QStringLiteral("allowlist")));

    reply = helper.BindPorts({QStringLiteral("80")}, QStringLiteral("0.0.0.0:8080"),
                             QStringLiteral("127.0.0.1:8443"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(
        reply.value(QStringLiteral("message")).toString().contains(QStringLiteral("allowlist")));

    reply = helper.BindPorts({QStringLiteral("80"), QStringLiteral("443")},
                             QStringLiteral("127.0.0.1:8080"), QStringLiteral("127.0.0.1:8443"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("message")).toString(),
             QStringLiteral("Chamada local não autorizada."));

    reply = helper.UnbindPorts({QStringLiteral("8443")});
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(
        reply.value(QStringLiteral("message")).toString().contains(QStringLiteral("allowlist")));
}

void SetupTest::helperBindsPorts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PortBindingHelper helper;
    helper.directory = dir.path();

    QVariantMap reply =
        helper.BindPorts({QStringLiteral("80"), QStringLiteral("443")},
                         QStringLiteral("127.0.0.1:8080"), QStringLiteral("127.0.0.1:8443"));
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("podstack-http.socket"))));
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("podstack-http.service"))));
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("podstack-https.socket"))));
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("podstack-https.service"))));
    QVERIFY(helper.systemctlCalls.contains(QStringLiteral("daemon-reload")));
    QVERIFY(helper.systemctlCalls.contains(QStringLiteral("enable --now podstack-http.socket")));
    QVERIFY(helper.systemctlCalls.contains(QStringLiteral("enable --now podstack-https.socket")));

    reply = helper.UnbindPorts({QStringLiteral("80"), QStringLiteral("443")});
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("podstack-http.socket"))));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("podstack-https.service"))));
    QVERIFY(helper.systemctlCalls.contains(QStringLiteral("disable --now podstack-http.socket")));
}

void SetupTest::caTrustInstallAndRemove()
{
    const QByteArray pem = testCaPem();
    if (pem.isEmpty()) {
        QSKIP("nenhuma CA autoassinada no bundle do sistema");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    const QString fingerprint = PodStack::CaTrust::fingerprintOf(pem);
    QVERIFY(!fingerprint.isEmpty());
    QVERIFY(PodStack::CaTrust::validateFingerprint(fingerprint, &error));
    QVERIFY(PodStack::CaTrust::isSelfSigned(pem, &error));

    QVERIFY(!PodStack::CaTrust::trust(dir.path(), pem, QString(64, QLatin1Char('a')), &error));
    QVERIFY(PodStack::CaTrust::trust(dir.path(), pem, fingerprint, &error));
    const QString anchor = dir.filePath(QStringLiteral("podstack-local-ca.crt"));
    QVERIFY(QFile::exists(anchor));

    QVERIFY(!PodStack::CaTrust::untrust(dir.path(), QString(64, QLatin1Char('a')), &error));
    QVERIFY(QFile::exists(anchor));
    QVERIFY(PodStack::CaTrust::untrust(dir.path(), fingerprint, &error));
    QVERIFY(!QFile::exists(anchor));
    QVERIFY(PodStack::CaTrust::untrust(dir.path(), fingerprint, &error));
}

void SetupTest::helperValidatesCaTrust()
{
    const QByteArray pem = testCaPem();
    if (pem.isEmpty()) {
        QSKIP("nenhuma CA autoassinada no bundle do sistema");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString certPath = dir.filePath(QStringLiteral("root.crt"));
    QFile file(certPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(pem), qint64(pem.size()));
    file.close();
    const QString fingerprint = PodStack::CaTrust::fingerprintOf(pem);

    PodStack::HelperService helper;
    QVariantMap reply = helper.TrustCa(certPath, QStringLiteral("abc"));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(
        reply.value(QStringLiteral("message")).toString().contains(QStringLiteral("Fingerprint")));

    reply = helper.TrustCa(certPath, QString(64, QLatin1Char('a')));
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(
        reply.value(QStringLiteral("message")).toString().contains(QStringLiteral("fingerprint")));

    reply = helper.TrustCa(QStringLiteral("/arquivo/inexistente.crt"), fingerprint);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);

    reply = helper.TrustCa(certPath, fingerprint);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("message")).toString(),
             QStringLiteral("Chamada local não autorizada."));

    reply = helper.UntrustCa(fingerprint);
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("message")).toString(),
             QStringLiteral("Chamada local não autorizada."));
}

void SetupTest::helperTrustsCa()
{
    const QByteArray pem = testCaPem();
    if (pem.isEmpty()) {
        QSKIP("nenhuma CA autoassinada no bundle do sistema");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString certPath = dir.filePath(QStringLiteral("root.crt"));
    QFile file(certPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(pem), qint64(pem.size()));
    file.close();

    CaTrustHelper helper;
    helper.anchorsDirectory = dir.path();
    const QString fingerprint = PodStack::CaTrust::fingerprintOf(pem);

    QVariantMap reply = helper.TrustCa(certPath, fingerprint);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("podstack-local-ca.crt"))));
    QCOMPARE(helper.trustUpdates, 1);

    reply = helper.UntrustCa(fingerprint);
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("podstack-local-ca.crt"))));
    QCOMPARE(helper.trustUpdates, 2);
}

void SetupTest::setupRunsSocketJob()
{
    QTemporaryDir bin;
    QVERIFY(bin.isValid());
    QVERIFY(writeExecutable(bin.filePath(QStringLiteral("systemctl")),
                            QByteArrayLiteral("sleep 0.3\nexit 0")));

    const QByteArray previousPath = qgetenv("PATH");
    qputenv("PATH", bin.path().toUtf8() + ':' + previousPath);
    const auto restore = qScopeGuard([previousPath] { qputenv("PATH", previousPath); });

    PodStack::SetupManager manager;
    QStringList methods;
    QJsonObject lastPayload;
    const auto notify = [&methods, &lastPayload](const QString &method,
                                                 const QJsonObject &payload) {
        methods.append(method);
        lastPayload = payload;
    };

    PodStack::Error error;
    const QJsonObject started = manager.apply(
        QJsonObject{{QStringLiteral("action"), QStringLiteral("enable-socket")}}, notify, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    const QString jobId = started.value(QStringLiteral("jobId")).toString();
    QVERIFY(!jobId.isEmpty());
    QVERIFY(!manager.activeJob().isEmpty());

    QTRY_VERIFY_WITH_TIMEOUT(methods.contains(QStringLiteral("setup.done")), 20000);
    QVERIFY(methods.contains(QStringLiteral("setup.delta")));
    QCOMPARE(lastPayload.value(QStringLiteral("state")).toString(), QStringLiteral("success"));
    QCOMPARE(lastPayload.value(QStringLiteral("progress")).toInt(), 100);
    QVERIFY(lastPayload.value(QStringLiteral("checks")).toArray().size() >= 9);
    QVERIFY(manager.activeJob().isEmpty());
    QVERIFY(QThreadPool::globalInstance()->waitForDone(20000));
}

void SetupTest::setupTracksActiveJob()
{
    QTemporaryDir bin;
    QVERIFY(bin.isValid());
    QVERIFY(writeExecutable(bin.filePath(QStringLiteral("systemctl")),
                            QByteArrayLiteral("sleep 0.5\nexit 0")));

    const QByteArray previousPath = qgetenv("PATH");
    qputenv("PATH", bin.path().toUtf8() + ':' + previousPath);
    const auto restore = qScopeGuard([previousPath] { qputenv("PATH", previousPath); });

    PodStack::SetupManager manager;
    PodStack::Error error;
    const QJsonObject started = manager.apply(
        QJsonObject{{QStringLiteral("action"), QStringLiteral("enable-socket")}}, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error.message));
    const QString jobId = started.value(QStringLiteral("jobId")).toString();

    QCOMPARE(manager.activeJob().value(QStringLiteral("jobId")).toString(), jobId);
    QCOMPARE(manager.jobStatus(jobId).value(QStringLiteral("jobId")).toString(), jobId);

    bool subscribed = false;
    const QJsonObject snapshot = manager.jobStatus(jobId);
    manager.subscribe(jobId,
                      [&subscribed](const QString &, const QJsonObject &) { subscribed = true; });
    QVERIFY(subscribed);
    QCOMPARE(snapshot.value(QStringLiteral("action")).toString(), QStringLiteral("enable-socket"));

    const QJsonObject cancelled = manager.cancel(QJsonObject{{QStringLiteral("jobId"), jobId}});
    QCOMPARE(cancelled.value(QStringLiteral("cancelled")).toBool(), true);

    QTRY_VERIFY_WITH_TIMEOUT(
        manager.jobStatus(jobId).value(QStringLiteral("state")).toString() ==
                QStringLiteral("success") ||
            manager.jobStatus(jobId).value(QStringLiteral("state")).toString() ==
                QStringLiteral("failed"),
        20000);
    QVERIFY(QThreadPool::globalInstance()->waitForDone(20000));
}

void SetupTest::setupCompletesOnboarding()
{
    PodStack::SetupManager manager;
    PodStack::Error error;

    QVERIFY(manager.complete(true, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(!PodStack::Config::load().get(QStringLiteral("setup.completed")).toBool());
    QVERIFY(!PodStack::Config::load().get(QStringLiteral("app.showOnboarding")).toBool(true));

    error = {};
    QVERIFY(manager.complete(false, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(PodStack::Config::load().get(QStringLiteral("setup.completed")).toBool());
    QVERIFY(
        !PodStack::Config::load().get(QStringLiteral("setup.completedAt")).toString().isEmpty());
}

void SetupTest::helperInstallsPackages()
{
    QTemporaryDir bin;
    QTemporaryDir logs;
    QVERIFY(bin.isValid());
    QVERIFY(logs.isValid());
    const QString log = logs.filePath(QStringLiteral("packages.log"));
    QVERIFY(
        writeExecutable(bin.filePath(QStringLiteral("dnf")),
                        QStringLiteral("echo \"$@\" >> \"$PODSTACK_PKG_LOG\"\nexit 0").toUtf8()));

    const QByteArray previousPath = qgetenv("PATH");
    qputenv("PATH", bin.path().toUtf8());
    qputenv("PODSTACK_PKG_LOG", log.toUtf8());
    const auto restore = qScopeGuard([previousPath] {
        qputenv("PATH", previousPath);
        qunsetenv("PODSTACK_PKG_LOG");
    });

    AuthorizedHelper helper;
    QVariantMap reply =
        helper.InstallPackages({QStringLiteral("podman"), QStringLiteral("virtiofsd")});
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(readLog(log).contains(QStringLiteral("install -y --refresh podman virtiofsd")));

    // Falha do gerenciador de pacotes vira mensagem com o tail da saída.
    QVERIFY(writeExecutable(bin.filePath(QStringLiteral("dnf")),
                            QStringLiteral("echo boom >&2\nexit 1").toUtf8()));
    reply = helper.InstallPackages({QStringLiteral("podman")});
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(reply.value(QStringLiteral("message"))
                .toString()
                .contains(QStringLiteral("Falha ao instalar")));
    QVERIFY(reply.value(QStringLiteral("message")).toString().contains(QStringLiteral("boom")));

    // apt-get e pacman têm linhas de argumentos próprias.
    QTemporaryDir aptBin;
    QVERIFY(aptBin.isValid());
    QVERIFY(
        writeExecutable(aptBin.filePath(QStringLiteral("apt-get")), QByteArrayLiteral("exit 0")));
    qputenv("PATH", aptBin.path().toUtf8());
    reply = helper.InstallPackages({QStringLiteral("podman")});
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);

    QTemporaryDir pacBin;
    QVERIFY(pacBin.isValid());
    QVERIFY(
        writeExecutable(pacBin.filePath(QStringLiteral("pacman")), QByteArrayLiteral("exit 0")));
    qputenv("PATH", pacBin.path().toUtf8());
    reply = helper.InstallPackages({QStringLiteral("podman")});
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);

    // Sem gerenciador suportado.
    QTemporaryDir emptyBin;
    QVERIFY(emptyBin.isValid());
    qputenv("PATH", emptyBin.path().toUtf8());
    reply = helper.InstallPackages({QStringLiteral("podman")});
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(reply.value(QStringLiteral("message"))
                .toString()
                .contains(QStringLiteral("não suportado")));
}

void SetupTest::helperRemovesDocker()
{
    QTemporaryDir bin;
    QVERIFY(bin.isValid());
    QVERIFY(
        writeExecutable(bin.filePath(QStringLiteral("rpm")), QStringLiteral("exit 0").toUtf8()));
    QVERIFY(writeExecutable(bin.filePath(QStringLiteral("systemctl")),
                            QStringLiteral("exit 0").toUtf8()));
    QVERIFY(
        writeExecutable(bin.filePath(QStringLiteral("dnf")), QStringLiteral("exit 0").toUtf8()));

    const QByteArray previousPath = qgetenv("PATH");
    qputenv("PATH", bin.path().toUtf8());
    const auto restore = qScopeGuard([previousPath] { qputenv("PATH", previousPath); });

    AuthorizedHelper helper;
    QVariantMap reply = helper.RemovePackages({QStringLiteral("docker-ce")});
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(reply.value(QStringLiteral("message"))
                .toString()
                .contains(QStringLiteral("Pacotes removidos")));

    // Sem pacotes instalados não há remoção, mas a operação é idempotente.
    QVERIFY(
        writeExecutable(bin.filePath(QStringLiteral("rpm")), QStringLiteral("exit 1").toUtf8()));
    reply = helper.RemovePackages({QStringLiteral("docker-ce")});
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(reply.value(QStringLiteral("message"))
                .toString()
                .contains(QStringLiteral("Nenhum pacote")));

    // Falha do gerenciador durante a remoção.
    QVERIFY(
        writeExecutable(bin.filePath(QStringLiteral("rpm")), QStringLiteral("exit 0").toUtf8()));
    QVERIFY(writeExecutable(bin.filePath(QStringLiteral("dnf")),
                            QStringLiteral("echo rm-fail >&2\nexit 1").toUtf8()));
    reply = helper.RemovePackages({QStringLiteral("docker-ce")});
    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(reply.value(QStringLiteral("message"))
                .toString()
                .contains(QStringLiteral("Falha ao remover")));
}

void SetupTest::setupApplyJobs()
{
    QTemporaryDir bin;
    QVERIFY(bin.isValid());
    QVERIFY(writeExecutable(bin.filePath(QStringLiteral("systemctl")),
                            QStringLiteral("exit 0").toUtf8()));
    QVERIFY(writeExecutable(bin.filePath(QStringLiteral("podman")),
                            QStringLiteral("echo 'podman version 5.0.0'\nexit 0").toUtf8()));

    const QByteArray previousPath = qgetenv("PATH");
    qputenv("PATH", bin.path().toUtf8());
    const auto restore = qScopeGuard([previousPath] { qputenv("PATH", previousPath); });

    {
        FakeSetupManager manager;
        bool done = false;
        PodStack::Error error;
        const QJsonObject started = manager.apply(
            QJsonObject{{QStringLiteral("action"), QStringLiteral("install-podman")}},
            [&done](const QString &method, const QJsonObject &) {
                if (method == QStringLiteral("setup.done")) {
                    done = true;
                }
            },
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error.message));
        QVERIFY(!started.value(QStringLiteral("jobId")).toString().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(done, 20000);
        QVERIFY(QThreadPool::globalInstance()->waitForDone(20000));
        const QString state = manager.jobStatus(started.value(QStringLiteral("jobId")).toString())
                                  .value(QStringLiteral("state"))
                                  .toString();
        QCOMPARE(state, QStringLiteral("success"));
        QVERIFY(manager.calls.contains(QStringLiteral("InstallPackages:podman")));
    }

    {
        FakeSetupManager manager;
        manager.installOk = false;
        bool done = false;
        PodStack::Error error;
        const QJsonObject started = manager.apply(
            QJsonObject{{QStringLiteral("action"), QStringLiteral("install-qemu")}},
            [&done](const QString &method, const QJsonObject &) {
                if (method == QStringLiteral("setup.done")) {
                    done = true;
                }
            },
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error.message));
        QTRY_VERIFY_WITH_TIMEOUT(done, 20000);
        QVERIFY(QThreadPool::globalInstance()->waitForDone(20000));
        const QJsonObject status =
            manager.jobStatus(started.value(QStringLiteral("jobId")).toString());
        QCOMPARE(status.value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
        QVERIFY(status.value(QStringLiteral("error"))
                    .toString()
                    .contains(QStringLiteral("falha simulada")));
    }

    {
        FakeSetupManager manager;
        bool done = false;
        PodStack::Error error;
        QJsonArray actions;
        actions.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("install-components")}});
        QJsonArray components;
        components.append(QStringLiteral("caddy"));
        components.append(QStringLiteral("ignorado"));
        const QJsonObject started = manager.apply(
            QJsonObject{{QStringLiteral("actions"), actions},
                        {QStringLiteral("components"), components}},
            [&done](const QString &method, const QJsonObject &) {
                if (method == QStringLiteral("setup.done")) {
                    done = true;
                }
            },
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error.message));
        QTRY_VERIFY_WITH_TIMEOUT(done, 20000);
        QVERIFY(QThreadPool::globalInstance()->waitForDone(20000));
        QVERIFY(manager.calls.contains(QStringLiteral("InstallPackages:caddy")));
    }

    {
        FakeSetupManager manager;
        bool done = false;
        PodStack::Error error;
        const QJsonObject started = manager.removeDocker(
            QJsonObject{{QStringLiteral("confirm"), true}},
            [&done](const QString &method, const QJsonObject &) {
                if (method == QStringLiteral("setup.done")) {
                    done = true;
                }
            },
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error.message));
        QTRY_VERIFY_WITH_TIMEOUT(done, 20000);
        QVERIFY(QThreadPool::globalInstance()->waitForDone(20000));
        const QString state = manager.jobStatus(started.value(QStringLiteral("jobId")).toString())
                                  .value(QStringLiteral("state"))
                                  .toString();
        QCOMPARE(state, QStringLiteral("success"));
        QCOMPARE(PodStack::Config::load().get(QStringLiteral("setup.dockerPolicy")).toString(),
                 QStringLiteral("removed"));
    }
}

void SetupTest::setupApplyFailures()
{
    QTemporaryDir bin;
    QVERIFY(bin.isValid());
    QVERIFY(
        writeExecutable(bin.filePath(QStringLiteral("systemctl")), QByteArrayLiteral("exit 0")));
    QVERIFY(writeExecutable(bin.filePath(QStringLiteral("podman")),
                            QByteArrayLiteral("echo 'podman version 5.0.0'\nexit 0")));

    const QByteArray previousPath = qgetenv("PATH");
    qputenv("PATH", bin.path().toUtf8());
    const auto restore = qScopeGuard([previousPath] { qputenv("PATH", previousPath); });

    {
        FakeSetupManager manager;
        manager.removeOk = false;
        bool done = false;
        PodStack::Error error;
        const QJsonObject started = manager.removeDocker(
            QJsonObject{{QStringLiteral("confirm"), true}},
            [&done](const QString &method, const QJsonObject &) {
                if (method == QStringLiteral("setup.done")) {
                    done = true;
                }
            },
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error.message));
        QTRY_VERIFY_WITH_TIMEOUT(done, 20000);
        QVERIFY(QThreadPool::globalInstance()->waitForDone(20000));
        QCOMPARE(manager.jobStatus(started.value(QStringLiteral("jobId")).toString())
                     .value(QStringLiteral("state"))
                     .toString(),
                 QStringLiteral("failed"));
    }

    {
        FakeSetupManager manager;
        bool done = false;
        PodStack::Error error;
        const QJsonObject started = manager.apply(
            QJsonObject{{QStringLiteral("action"), QStringLiteral("upgrade-podman")}},
            [&done](const QString &method, const QJsonObject &) {
                if (method == QStringLiteral("setup.done")) {
                    done = true;
                }
            },
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error.message));
        QTRY_VERIFY_WITH_TIMEOUT(done, 20000);
        QVERIFY(QThreadPool::globalInstance()->waitForDone(20000));
        QVERIFY(manager.calls.contains(QStringLiteral("InstallPackages:podman")));
    }

    {
        // install-components sem componentes não instala nada, mas ativa o socket.
        FakeSetupManager manager;
        bool done = false;
        PodStack::Error error;
        const QJsonObject started = manager.apply(
            QJsonObject{{QStringLiteral("action"), QStringLiteral("install-components")}},
            [&done](const QString &method, const QJsonObject &) {
                if (method == QStringLiteral("setup.done")) {
                    done = true;
                }
            },
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error.message));
        QTRY_VERIFY_WITH_TIMEOUT(done, 20000);
        QVERIFY(QThreadPool::globalInstance()->waitForDone(20000));
        QVERIFY(manager.calls.isEmpty());
    }

    {
        // Instalação termina, mas o Podman não aparece no PATH.
        const QByteArray savedPath = qgetenv("PATH");
        qputenv("PATH", bin.path().toUtf8() + ":/nonexistent");
        QTemporaryDir noPodman;
        QVERIFY(noPodman.isValid());
        QVERIFY(writeExecutable(noPodman.filePath(QStringLiteral("systemctl")),
                                QByteArrayLiteral("exit 0")));
        qputenv("PATH", noPodman.path().toUtf8());
        const auto restoreNoPodman = qScopeGuard([savedPath] { qputenv("PATH", savedPath); });

        FakeSetupManager manager;
        bool done = false;
        PodStack::Error error;
        const QJsonObject started = manager.apply(
            QJsonObject{{QStringLiteral("action"), QStringLiteral("install-qemu")}},
            [&done](const QString &method, const QJsonObject &) {
                if (method == QStringLiteral("setup.done")) {
                    done = true;
                }
            },
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error.message));
        QTRY_VERIFY_WITH_TIMEOUT(done, 20000);
        QVERIFY(QThreadPool::globalInstance()->waitForDone(20000));
        const QJsonObject status =
            manager.jobStatus(started.value(QStringLiteral("jobId")).toString());
        QCOMPARE(status.value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
        QVERIFY(status.value(QStringLiteral("error"))
                    .toString()
                    .contains(QStringLiteral("Podman não foi encontrado")));
    }
}

void SetupTest::setupRejectsUnavailableHelper()
{
    FakeSetupManager manager;
    manager.available = false;

    PodStack::Error error;
    QVERIFY(manager
                .apply(QJsonObject{{QStringLiteral("action"), QStringLiteral("install-podman")}},
                       {}, &error)
                .isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_HELPER_UNAVAILABLE"));

    error = {};
    QVERIFY(
        manager.removeDocker(QJsonObject{{QStringLiteral("confirm"), true}}, {}, &error).isEmpty());
    QCOMPARE(error.code, QStringLiteral("PODSTACK_HELPER_UNAVAILABLE"));
}

} // namespace

void SetupTest::ubuntuOvmfAndPackages()
{
    const QStringList codes = PodStack::SetupManager::ovmfCodeCandidates();
    QVERIFY(codes.contains(QStringLiteral("/usr/share/OVMF/OVMF_CODE_4M.fd")));

    const QStringList apt =
        PodStack::SetupManager::qemuPackagesForManager(QStringLiteral("apt-get"));
    QVERIFY(apt.contains(QStringLiteral("ovmf")));
    QVERIFY(apt.contains(QStringLiteral("qemu-system-x86")));
    QVERIFY(apt.contains(QStringLiteral("qemu-utils")));
    QVERIFY(!apt.contains(QStringLiteral("edk2-ovmf")));

    const QStringList dnf =
        PodStack::SetupManager::qemuPackagesForManager(QStringLiteral("dnf"));
    QVERIFY(dnf.contains(QStringLiteral("edk2-ovmf")));
    QVERIFY(dnf.contains(QStringLiteral("qemu-system-x86_64")));
}

QTEST_GUILESS_MAIN(SetupTest)
#include "test_setup.moc"
