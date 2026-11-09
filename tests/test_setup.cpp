// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Paths.h"
#include "daemon/SetupManager.h"

#include <QDir>
#include <QJsonArray>
#include <QSet>
#include <QStandardPaths>
#include <QTest>

namespace {

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

} // namespace

QTEST_GUILESS_MAIN(SetupTest)
#include "test_setup.moc"
