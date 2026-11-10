// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Paths.h"
#include "daemon/DbusUtils.h"
#include "daemon/SetupManager.h"
#include "helper/Firewall.h"
#include "helper/Polkit.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QScopeGuard>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

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

class SetupTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void checkIncludesCoreComponents();
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
};

void SetupTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QDir(PodStack::Paths::configDir()).removeRecursively();
}

void SetupTest::cleanupTestCase()
{
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

} // namespace

QTEST_GUILESS_MAIN(SetupTest)
#include "test_setup.moc"
