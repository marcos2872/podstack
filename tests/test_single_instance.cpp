// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Paths.h"
#include "gui/SingleInstance.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTest>

#include <unistd.h>

namespace {

class SingleInstanceTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanupTestCase();

    void primaryStartsAndListens();
    void secondInstanceForwardsShow();
    void secondInstanceForwardsQuit();
    void staleSocketIsReplaced();
    void socketPermissionsArePrivate();
    void ignoresUnknownCommands();

private:
    QString m_runtimeDir;
};

void SingleInstanceTest::initTestCase()
{
    m_runtimeDir = QDir::tempPath()
        + QStringLiteral("/podstack-single-instance-%1").arg(::getpid());
    qputenv("XDG_RUNTIME_DIR", m_runtimeDir.toUtf8());
    QDir().mkpath(m_runtimeDir);
}

void SingleInstanceTest::init()
{
    QDir(m_runtimeDir).removeRecursively();
    QDir().mkpath(m_runtimeDir);
}

void SingleInstanceTest::cleanupTestCase()
{
    QDir(m_runtimeDir).removeRecursively();
}

void SingleInstanceTest::primaryStartsAndListens()
{
    PodStack::SingleInstance primary;
    QString error;
    QVERIFY2(primary.start(QString(), &error), qPrintable(error));
    QVERIFY(primary.isPrimary());
    QVERIFY(QFile::exists(PodStack::SingleInstance::socketPath()));
    QVERIFY(primary.start(QString(), &error));
}

void SingleInstanceTest::secondInstanceForwardsShow()
{
    PodStack::SingleInstance primary;
    QString error;
    QVERIFY2(primary.start(QString(), &error), qPrintable(error));
    QSignalSpy spy(&primary, &PodStack::SingleInstance::showRequested);

    PodStack::SingleInstance secondary;
    QVERIFY(!secondary.start(QStringLiteral("show"), &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QTRY_COMPARE(spy.count(), 1);
}

void SingleInstanceTest::secondInstanceForwardsQuit()
{
    PodStack::SingleInstance primary;
    QString error;
    QVERIFY2(primary.start(QString(), &error), qPrintable(error));
    QSignalSpy spy(&primary, &PodStack::SingleInstance::quitRequested);

    PodStack::SingleInstance secondary;
    QVERIFY(!secondary.start(QStringLiteral("quit"), &error));
    QTRY_COMPARE(spy.count(), 1);
}

void SingleInstanceTest::staleSocketIsReplaced()
{
    QDir().mkpath(PodStack::Paths::runtimeDir());
    QFile stale(PodStack::SingleInstance::socketPath());
    QVERIFY(stale.open(QIODevice::WriteOnly));
    stale.write("stale");
    stale.close();

    PodStack::SingleInstance primary;
    QString error;
    QVERIFY2(primary.start(QString(), &error), qPrintable(error));
    QVERIFY(primary.isPrimary());
}

void SingleInstanceTest::socketPermissionsArePrivate()
{
    PodStack::SingleInstance primary;
    QString error;
    QVERIFY2(primary.start(QString(), &error), qPrintable(error));

    const QFileDevice::Permissions permissions =
        QFileInfo(PodStack::SingleInstance::socketPath()).permissions();
    QVERIFY(permissions.testFlag(QFileDevice::ReadOwner));
    QVERIFY(permissions.testFlag(QFileDevice::WriteOwner));
    QVERIFY(!permissions.testFlag(QFileDevice::ReadGroup));
    QVERIFY(!permissions.testFlag(QFileDevice::WriteGroup));
    QVERIFY(!permissions.testFlag(QFileDevice::ReadOther));
    QVERIFY(!permissions.testFlag(QFileDevice::WriteOther));
}

void SingleInstanceTest::ignoresUnknownCommands()
{
    PodStack::SingleInstance primary;
    QString error;
    QVERIFY2(primary.start(QString(), &error), qPrintable(error));

    QLocalSocket socket;
    socket.connectToServer(PodStack::SingleInstance::socketPath());
    QVERIFY(socket.waitForConnected(2000));
    socket.write("not-json\n");
    QVERIFY(socket.waitForBytesWritten(2000));
    QTest::qWait(50);
    QVERIFY(primary.isPrimary());

    QLocalSocket unknown;
    unknown.connectToServer(PodStack::SingleInstance::socketPath());
    QVERIFY(unknown.waitForConnected(2000));
    unknown.write("{\"command\":\"bogus\"}\n");
    QVERIFY(unknown.waitForBytesWritten(2000));
    QTest::qWait(50);
    QVERIFY(primary.isPrimary());
}

} // namespace

QTEST_GUILESS_MAIN(SingleInstanceTest)
#include "test_single_instance.moc"
