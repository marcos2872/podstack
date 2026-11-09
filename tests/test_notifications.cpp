// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Config.h"
#include "core/Paths.h"
#include "gui/Notifications.h"

#include <QDir>
#include <QStandardPaths>
#include <QTest>

namespace {

class NotificationsTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanupTestCase();

    void summaryForFailure();
    void summaryForUnhealthy();
    void summaryForOom();
    void ignoresNormalEvents();
    void bodyIncludesExitCode();
    void cooldownPerContainer();
    void notifiesOncePerCooldown();
    void respectsDisabledConfig();
    void bodyFallbacks();
    void prunesCooldownMap();
    void ignoresUnknownNotificationActions();
};

void NotificationsTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void NotificationsTest::init()
{
    QDir(PodStack::Paths::configDir()).removeRecursively();
}

void NotificationsTest::cleanupTestCase()
{
    QDir(PodStack::Paths::configDir()).removeRecursively();
    QStandardPaths::setTestModeEnabled(false);
}

void NotificationsTest::summaryForFailure()
{
    const QVariantMap event{{QStringLiteral("action"), QStringLiteral("die")},
                            {QStringLiteral("exitCode"), QStringLiteral("137")},
                            {QStringLiteral("name"), QStringLiteral("web")}};
    QCOMPARE(PodStack::NotificationService::summaryFor(event),
             QStringLiteral("Container falhou"));
    QVERIFY(PodStack::NotificationService::bodyFor(event).contains(QStringLiteral("137")));
    QVERIFY(PodStack::NotificationService::bodyFor(event).contains(QStringLiteral("web")));
}

void NotificationsTest::summaryForUnhealthy()
{
    const QVariantMap event{
        {QStringLiteral("action"), QStringLiteral("health_status")},
        {QStringLiteral("health"), QStringLiteral("unhealthy")},
    };
    QCOMPARE(PodStack::NotificationService::summaryFor(event),
             QStringLiteral("Container unhealthy"));
}

void NotificationsTest::summaryForOom()
{
    const QVariantMap event{{QStringLiteral("action"), QStringLiteral("oom")},
                            {QStringLiteral("name"), QStringLiteral("worker")}};
    QCOMPARE(PodStack::NotificationService::summaryFor(event),
             QStringLiteral("Container sem memória"));
    QVERIFY(PodStack::NotificationService::bodyFor(event).contains(QStringLiteral("worker")));
}

void NotificationsTest::ignoresNormalEvents()
{
    const QVariantMap start{{QStringLiteral("action"), QStringLiteral("start")}};
    QVERIFY(PodStack::NotificationService::summaryFor(start).isEmpty());

    const QVariantMap healthy{
        {QStringLiteral("action"), QStringLiteral("health_status")},
        {QStringLiteral("health"), QStringLiteral("healthy")},
    };
    QVERIFY(PodStack::NotificationService::summaryFor(healthy).isEmpty());

    const QVariantMap cleanExit{{QStringLiteral("action"), QStringLiteral("die")},
                                {QStringLiteral("exitCode"), QStringLiteral("0")}};
    QVERIFY(PodStack::NotificationService::summaryFor(cleanExit).isEmpty());
}

void NotificationsTest::bodyIncludesExitCode()
{
    const QVariantMap event{{QStringLiteral("action"), QStringLiteral("die")},
                            {QStringLiteral("exitCode"), QStringLiteral("1")},
                            {QStringLiteral("name"), QStringLiteral("db")}};
    const QString body = PodStack::NotificationService::bodyFor(event);
    QVERIFY(body.contains(QStringLiteral("db")));
    QVERIFY(body.contains(QStringLiteral("1")));
}

void NotificationsTest::cooldownPerContainer()
{
    PodStack::NotificationService service;
    const QVariantMap event{{QStringLiteral("id"), QStringLiteral("abc")},
                            {QStringLiteral("action"), QStringLiteral("die")},
                            {QStringLiteral("exitCode"), QStringLiteral("1")}};

    QVERIFY(service.shouldNotify(event, 1000));
    QVERIFY(!service.shouldNotify(event, 2000));

    QVariantMap other = event;
    other.insert(QStringLiteral("id"), QStringLiteral("def"));
    QVERIFY(service.shouldNotify(other, 2000));

    QVERIFY(service.shouldNotify(event, 32000));
}

void NotificationsTest::notifiesOncePerCooldown()
{
    PodStack::NotificationService service;
    int count = 0;
    QString lastContainer;
    service.setSenderForTesting(
        [&count, &lastContainer](const QString &, const QString &, const QString &id) {
            ++count;
            lastContainer = id;
        });

    QVariantMap event{{QStringLiteral("id"), QStringLiteral("abc")},
                      {QStringLiteral("action"), QStringLiteral("die")},
                      {QStringLiteral("exitCode"), QStringLiteral("1")},
                      {QStringLiteral("name"), QStringLiteral("web")}};
    service.notifyContainerEvent(event);
    QCOMPARE(count, 1);
    QCOMPARE(lastContainer, QStringLiteral("abc"));

    service.notifyContainerEvent(event);
    QCOMPARE(count, 1);

    event.insert(QStringLiteral("id"), QStringLiteral("def"));
    service.notifyContainerEvent(event);
    QCOMPARE(count, 2);
}

void NotificationsTest::bodyFallbacks()
{
    const QVariantMap unnamed{{QStringLiteral("action"), QStringLiteral("die")},
                              {QStringLiteral("id"), QStringLiteral("abcdef123456789")},
                              {QStringLiteral("exitCode"), QStringLiteral("2")}};
    QVERIFY(PodStack::NotificationService::bodyFor(unnamed).contains(
        QStringLiteral("abcdef123456")));

    const QVariantMap unhealthy{
        {QStringLiteral("action"), QStringLiteral("health_status")},
        {QStringLiteral("health"), QStringLiteral("unhealthy")},
        {QStringLiteral("name"), QStringLiteral("web")},
    };
    QVERIFY(PodStack::NotificationService::bodyFor(unhealthy).contains(
        QStringLiteral("unhealthy")));

    const QVariantMap unknown{{QStringLiteral("action"), QStringLiteral("start")}};
    QVERIFY(PodStack::NotificationService::bodyFor(unknown).isEmpty());
}

void NotificationsTest::prunesCooldownMap()
{
    PodStack::NotificationService service;
    for (int index = 0; index < 260; ++index) {
        const QVariantMap event{{QStringLiteral("id"), QString::number(index)},
                                {QStringLiteral("action"), QStringLiteral("die")},
                                {QStringLiteral("exitCode"), QStringLiteral("1")}};
        QVERIFY(service.shouldNotify(event, 1000 + index));
    }
}

void NotificationsTest::ignoresUnknownNotificationActions()
{
    PodStack::NotificationService service;
    QVERIFY(QMetaObject::invokeMethod(&service, "onActionInvoked", Q_ARG(uint, 999),
                                      Q_ARG(QString, QStringLiteral("default"))));
    QVERIFY(QMetaObject::invokeMethod(&service, "onNotificationClosed", Q_ARG(uint, 999),
                                      Q_ARG(uint, 1)));
}

void NotificationsTest::respectsDisabledConfig()
{
    PodStack::Config config;
    config.set(QStringLiteral("app.notifications"), false);
    QString error;
    QVERIFY2(config.save(&error), qPrintable(error));

    PodStack::NotificationService service;
    int count = 0;
    service.setSenderForTesting(
        [&count](const QString &, const QString &, const QString &) { ++count; });

    const QVariantMap event{{QStringLiteral("id"), QStringLiteral("abc")},
                            {QStringLiteral("action"), QStringLiteral("die")},
                            {QStringLiteral("exitCode"), QStringLiteral("1")}};
    service.notifyContainerEvent(event);
    QCOMPARE(count, 0);
}

} // namespace

QTEST_GUILESS_MAIN(NotificationsTest)
#include "test_notifications.moc"
