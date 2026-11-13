// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/Config.h"
#include "core/LogRotation.h"
#include "core/LogWriter.h"
#include "core/Logging.h"
#include "core/Paths.h"
#include "core/Settings.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QTime>

#include <memory>

namespace {

QJsonObject lastJsonLine(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QList<QByteArray> lines = file.readAll().split('\n');
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        if (it->trimmed().isEmpty()) {
            continue;
        }
        const QJsonDocument document = QJsonDocument::fromJson(*it);
        return document.isObject() ? document.object() : QJsonObject{};
    }
    return {};
}

QStringList readLines(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QStringList lines;
    for (const QByteArray &line : file.readAll().split('\n')) {
        if (!line.trimmed().isEmpty()) {
            lines.append(QString::fromUtf8(line));
        }
    }
    return lines;
}

QString uniqueLogDir(QTemporaryDir *root, const QString &name)
{
    const QString path = root->filePath(name);
    QDir().mkpath(path);
    return path;
}

} // namespace

class LoggingTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void jsonLineFields();
    void levelFiltering();
    void redaction();
    void runtimeReload();
    void rotationDaily();
    void rotationBySize();
    void retentionAndCap();
    void rotateAtStart();
    void queueDrop();
    void settingsSchema();

private:
    void initLogging(const QString &name, const QString &level);
    void stopLogging();

    std::unique_ptr<QTemporaryDir> m_root;
    QString m_lastFile;
};

void LoggingTest::initTestCase()
{
    m_root = std::make_unique<QTemporaryDir>();
    QVERIFY(m_root->isValid());
    qputenv("XDG_CONFIG_HOME", m_root->filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_STATE_HOME", m_root->filePath(QStringLiteral("state")).toUtf8());
}

void LoggingTest::cleanupTestCase()
{
    PodStack::Logging::shutdown();
    qunsetenv("XDG_CONFIG_HOME");
    qunsetenv("XDG_STATE_HOME");
    qunsetenv("PODSTACK_LOG_DIR");
    qunsetenv("PODSTACK_LOG_LEVEL");
}

void LoggingTest::initLogging(const QString &name, const QString &level)
{
    PodStack::Logging::shutdown();
    qputenv("PODSTACK_LOG_DIR", uniqueLogDir(m_root.get(), name).toUtf8());
    if (level.isEmpty()) {
        qunsetenv("PODSTACK_LOG_LEVEL");
    } else {
        qputenv("PODSTACK_LOG_LEVEL", level.toUtf8());
    }
    PodStack::Logging::init(false);
}

void LoggingTest::stopLogging()
{
    m_lastFile = PodStack::Logging::currentLogFile();
    PodStack::Logging::flush();
    PodStack::Logging::shutdown();
}

void LoggingTest::jsonLineFields()
{
    initLogging(QStringLiteral("fields"), QStringLiteral("debug"));
    qCInfo(lcCore) << "mensagem de teste";
    stopLogging();

    const QString path = m_lastFile;
    QVERIFY2(QFileInfo::exists(path), qPrintable(path));
    const QJsonObject line = lastJsonLine(path);
    QVERIFY(!line.isEmpty());
    QVERIFY(!line.value(QStringLiteral("ts")).toString().isEmpty());
    QCOMPARE(line.value(QStringLiteral("level")).toString(), QStringLiteral("info"));
    QCOMPARE(line.value(QStringLiteral("category")).toString(), QStringLiteral("podstack.core"));
    QVERIFY(!line.value(QStringLiteral("process")).toString().isEmpty());
    QVERIFY(line.value(QStringLiteral("pid")).toInt() > 0);
    QVERIFY(line.value(QStringLiteral("thread")).toString().startsWith(QStringLiteral("0x")));
    QCOMPARE(line.value(QStringLiteral("message")).toString(), QStringLiteral("mensagem de teste"));
    const QJsonObject context = line.value(QStringLiteral("context")).toObject();
    QVERIFY(!context.value(QStringLiteral("file")).toString().isEmpty());
    QVERIFY(context.value(QStringLiteral("line")).toInt() > 0);
}

void LoggingTest::levelFiltering()
{
    initLogging(QStringLiteral("levels"), QStringLiteral("warn"));
    qCDebug(lcCore) << "debug-nao-deve";
    qCInfo(lcCore) << "info-nao-deve";
    qCWarning(lcCore) << "warn-deve";
    stopLogging();

    QStringList lines = readLines(m_lastFile);
    QCOMPARE(lines.size(), 1);
    QVERIFY(lines.first().contains(QStringLiteral("warn-deve")));

    initLogging(QStringLiteral("levels-error"), QStringLiteral("error"));
    qCWarning(lcCore) << "warn-nao-deve";
    qCCritical(lcCore) << "error-deve";
    stopLogging();

    lines = readLines(m_lastFile);
    QCOMPARE(lines.size(), 1);
    QVERIFY(lines.first().contains(QStringLiteral("error-deve")));
}

void LoggingTest::redaction()
{
    initLogging(QStringLiteral("redaction"), QStringLiteral("info"));
    qCInfo(lcCore) << "Authorization: Bearer super-secret" << "password=hunter2";
    stopLogging();

    const QStringList lines = readLines(m_lastFile);
    QCOMPARE(lines.size(), 1);
    QVERIFY(!lines.first().contains(QStringLiteral("super-secret")));
    QVERIFY(!lines.first().contains(QStringLiteral("hunter2")));
    QVERIFY(lines.first().contains(QStringLiteral("[redacted]")));
}

void LoggingTest::runtimeReload()
{
    PodStack::Logging::shutdown();
    qputenv("PODSTACK_LOG_DIR", uniqueLogDir(m_root.get(), QStringLiteral("reload")).toUtf8());
    qunsetenv("PODSTACK_LOG_LEVEL");
    QFile::remove(PodStack::Paths::configDir() + QStringLiteral("/config.json"));
    PodStack::Config config = PodStack::Config::load();
    config.set(QStringLiteral("advanced.logLevel"), QStringLiteral("error"));
    QString error;
    QVERIFY2(config.save(&error), qPrintable(error));

    PodStack::Logging::init(false);
    qCDebug(lcCore) << "debug-antes";
    config = PodStack::Config::load();
    config.set(QStringLiteral("advanced.logLevel"), QStringLiteral("debug"));
    QVERIFY2(config.save(&error), qPrintable(error));
    PodStack::Logging::reload();
    qCDebug(lcCore) << "debug-depois";
    stopLogging();

    const QStringList lines = readLines(m_lastFile);
    bool foundAfter = false;
    for (const QString &line : lines) {
        QVERIFY(!line.contains(QStringLiteral("debug-antes")));
        foundAfter = foundAfter || line.contains(QStringLiteral("debug-depois"));
    }
    QVERIFY(foundAfter);
}

void LoggingTest::rotationDaily()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PodStack::LogRotation::Options options;
    options.directory = dir.path();
    options.stem = QStringLiteral("podstackd");

    QFile current(PodStack::LogRotation::currentPath(options));
    QVERIFY(current.open(QIODevice::WriteOnly | QIODevice::Append));
    current.write("linha1\n");
    current.close();

    const QDate date(2026, 11, 13);
    QVERIFY(PodStack::LogRotation::rotate(options, date));
    const QString rotated = PodStack::LogRotation::rotatedPath(options, date);
    QVERIFY(QFileInfo::exists(rotated));
    QVERIFY(!QFileInfo::exists(PodStack::LogRotation::currentPath(options)));
    QCOMPARE(readLines(rotated).size(), 1);

    // Restart no mesmo dia: anexa ao arquivo diário em vez de sobrescrever.
    QVERIFY(current.open(QIODevice::WriteOnly | QIODevice::Append));
    current.write("linha2\n");
    current.close();
    QVERIFY(PodStack::LogRotation::rotate(options, date));
    QCOMPARE(readLines(rotated).size(), 2);
}

void LoggingTest::rotationBySize()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PodStack::LogRotation::Options options;
    options.directory = dir.path();
    options.stem = QStringLiteral("podstack-cli-42");

    const QDate date(2026, 11, 13);
    QFile current(PodStack::LogRotation::currentPath(options));
    QVERIFY(current.open(QIODevice::WriteOnly | QIODevice::Append));
    current.write("a\n");
    current.close();

    int sequence = 0;
    QVERIFY(PodStack::LogRotation::rotateForSize(options, date, &sequence));
    QCOMPARE(sequence, 1);
    QVERIFY(QFileInfo::exists(PodStack::LogRotation::rotatedPath(options, date, 1)));

    QVERIFY(current.open(QIODevice::WriteOnly | QIODevice::Append));
    current.write("b\n");
    current.close();
    QVERIFY(PodStack::LogRotation::rotateForSize(options, date, &sequence));
    QCOMPARE(sequence, 2);
    QVERIFY(QFileInfo::exists(PodStack::LogRotation::rotatedPath(options, date, 2)));
}

void LoggingTest::retentionAndCap()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PodStack::LogRotation::Options options;
    options.directory = dir.path();
    options.stem = QStringLiteral("podstackd");
    options.retentionDays = 7;
    options.maxTotalBytes = 20;

    const QDate today(2026, 11, 13);
    const auto write = [&options](const QDate &date, int size) {
        QFile file(PodStack::LogRotation::rotatedPath(options, date));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        file.write(QByteArray(size, 'x'));
        return true;
    };

    QVERIFY(write(today, 5));
    QVERIFY(write(today.addDays(-6), 5));
    QVERIFY(write(today.addDays(-7), 5));
    QVERIFY(PodStack::LogRotation::prune(options, today) >= 1);
    QVERIFY(QFileInfo::exists(PodStack::LogRotation::rotatedPath(options, today)));
    QVERIFY(QFileInfo::exists(PodStack::LogRotation::rotatedPath(options, today.addDays(-6))));
    QVERIFY(!QFileInfo::exists(PodStack::LogRotation::rotatedPath(options, today.addDays(-7))));

    // Teto total: remove o mais antigo primeiro.
    QVERIFY(write(today, 8));
    QVERIFY(write(today.addDays(-1), 8));
    QVERIFY(write(today.addDays(-2), 8));
    options.maxTotalBytes = 16;
    PodStack::LogRotation::prune(options, today);
    QVERIFY(!QFileInfo::exists(PodStack::LogRotation::rotatedPath(options, today.addDays(-2))));
}

void LoggingTest::rotateAtStart()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PodStack::LogRotation::Options options;
    options.directory = dir.path();
    options.stem = QStringLiteral("podstackd");

    const QDate today(2026, 11, 13);
    const QDate yesterday = today.addDays(-1);
    const QString current = PodStack::LogRotation::currentPath(options);
    QFile file(current);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append));
    file.write("ontem\n");
    QVERIFY(file.flush());
    QVERIFY(file.setFileTime(QDateTime(yesterday, QTime(12, 0)),
                             QFileDevice::FileModificationTime));
    file.close();
    QCOMPARE(QFileInfo(current).lastModified().date(), yesterday);

    QVERIFY(PodStack::LogRotation::rotateAtStart(options, today));
    QVERIFY(QFileInfo::exists(PodStack::LogRotation::rotatedPath(options, yesterday)));
    QVERIFY(!QFileInfo::exists(current));
}

void LoggingTest::queueDrop()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PodStack::LogRotation::Options options;
    options.directory = dir.path();
    options.stem = QStringLiteral("queue");

    PodStack::LogWriter writer(QStringLiteral("queue"), options, PodStack::LogLevel::Info);
    QMessageLogContext context;
    for (int index = 0; index < 9000; ++index) {
        writer.enqueue(QtInfoMsg, context, QStringLiteral("entrada %1").arg(index));
    }
    QCOMPARE(writer.dropped(), 808);

    writer.start();
    writer.flush();
    writer.stop();
    QCOMPARE(writer.dropped(), 0);

    const QStringList lines = readLines(writer.currentFile());
    bool foundDropped = false;
    for (const QString &line : lines) {
        foundDropped = foundDropped || line.contains(QStringLiteral("descartadas"));
    }
    QVERIFY(foundDropped);
}

void LoggingTest::settingsSchema()
{
    const PodStack::SettingDef *level = PodStack::Settings::find(QStringLiteral("advanced.logLevel"));
    QVERIFY(level != nullptr);
    QVERIFY(!level->hidden);
    QVERIFY(!level->restartRequired);
    QVERIFY(level->enumValues.contains(QStringLiteral("debug")));

    const PodStack::SettingDef *retention =
        PodStack::Settings::find(QStringLiteral("advanced.logRetentionDays"));
    QVERIFY(retention != nullptr);
    QCOMPARE(retention->defaultValue.toInt(), 7);

    const PodStack::SettingDef *maxBytes =
        PodStack::Settings::find(QStringLiteral("advanced.logMaxBytes"));
    QVERIFY(maxBytes != nullptr);
    QCOMPARE(maxBytes->defaultValue.toVariant().toLongLong(), 10485760LL);
}

QTEST_GUILESS_MAIN(LoggingTest)
#include "test_logging.moc"
