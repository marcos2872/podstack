// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/config/Config.h"
#include "core/config/Paths.h"
#include "gui/app/Autostart.h"

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
    void refusesToOverwriteForeignFile();
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

void AutostartTest::refusesToOverwriteForeignFile()
{
    const QString path = PodStack::Autostart::filePath();
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));

    const QByteArray foreign = "[Desktop Entry]\nExec=outra-coisa\n";
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(foreign);
    file.close();

    // Habilitar não pode sobrescrever o autostart de outro app — e o settings
    // não pode dizer "ligado" só porque o arquivo existe.
    QString error;
    QVERIFY(!PodStack::Autostart::setEnabled(true, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!PodStack::Autostart::isEnabled());

    QFile check(path);
    QVERIFY(check.open(QIODevice::ReadOnly));
    QCOMPARE(check.readAll(), foreign);
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
