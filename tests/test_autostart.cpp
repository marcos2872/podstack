// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Config.h"
#include "core/Paths.h"
#include "gui/Autostart.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTest>

namespace {

class AutostartTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanupTestCase();

    void enablesAndDisables();
    void preservesForeignFile();
    void syncFromConfig();
};

void AutostartTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void AutostartTest::init()
{
    QDir(PodStack::Paths::configDir()).removeRecursively();
    QFile::remove(PodStack::Autostart::filePath());
}

void AutostartTest::cleanupTestCase()
{
    QFile::remove(PodStack::Autostart::filePath());
    QDir(PodStack::Paths::configDir()).removeRecursively();
    QStandardPaths::setTestModeEnabled(false);
}

void AutostartTest::enablesAndDisables()
{
    QString error;
    QVERIFY2(PodStack::Autostart::setEnabled(true, &error), qPrintable(error));
    QVERIFY(PodStack::Autostart::isEnabled());

    QFile file(PodStack::Autostart::filePath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray content = file.readAll();
    QVERIFY(content.contains("--hidden"));
    QVERIFY(content.contains("X-PodStack-Autostart=true"));
    QVERIFY(content.contains("Exec="));

    QVERIFY2(PodStack::Autostart::setEnabled(false, &error), qPrintable(error));
    QVERIFY(!PodStack::Autostart::isEnabled());
}

void AutostartTest::syncFromConfig()
{
    PodStack::Config config;
    config.set(QStringLiteral("app.startAtLogin"), true);
    QString error;
    QVERIFY2(config.save(&error), qPrintable(error));

    PodStack::Autostart::syncFromConfig();
    QVERIFY(PodStack::Autostart::isEnabled());

    config.set(QStringLiteral("app.startAtLogin"), false);
    QVERIFY2(config.save(&error), qPrintable(error));
    PodStack::Autostart::syncFromConfig();
    QVERIFY(!PodStack::Autostart::isEnabled());
}

void AutostartTest::preservesForeignFile()
{
    const QString path = PodStack::Autostart::filePath();
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("[Desktop Entry]\nExec=outra-coisa\n");
    file.close();

    QString error;
    QVERIFY2(PodStack::Autostart::setEnabled(false, &error), qPrintable(error));
    QVERIFY(QFile::exists(path));
}

} // namespace

QTEST_GUILESS_MAIN(AutostartTest)
#include "test_autostart.moc"
