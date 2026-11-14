// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/Config.h"
#include "core/LogReader.h"
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
    void clearLogs();
    void rotationErrorPaths();
    void rotationHelpers();
    void logWriterLifecycle();
    void logReaderFilters();
    void logReaderEdges();
    void logReaderComponents();
    void logReaderExportErrors();
    void loggingWrappers();
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
    QVERIFY(
        file.setFileTime(QDateTime(yesterday, QTime(12, 0)), QFileDevice::FileModificationTime));
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

void LoggingTest::clearLogs()
{
    initLogging(QStringLiteral("clear"), QStringLiteral("info"));
    qCInfo(lcCore) << "antes do clear";
    PodStack::Logging::flush();
    const QString path = PodStack::Logging::currentLogFile();
    QVERIFY(QFileInfo::exists(path));
    PodStack::Logging::clear();
    PodStack::Logging::flush();
    QVERIFY(!QFileInfo::exists(path) || QFileInfo(path).size() == 0);
    qCInfo(lcCore) << "depois do clear";
    stopLogging();
    const QStringList lines = readLines(m_lastFile);
    QVERIFY(!lines.join(QString()).contains(QStringLiteral("antes do clear")));
    QVERIFY(lines.join(QString()).contains(QStringLiteral("depois do clear")));
}

void LoggingTest::rotationErrorPaths()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDate date(2026, 11, 13);

    PodStack::LogRotation::Options empty;
    empty.stem = QStringLiteral("podstackd");
    QString error;
    QVERIFY(!PodStack::LogRotation::rotate(empty, date, &error));
    QVERIFY(!error.isEmpty());

    error.clear();
    int sequence = 9;
    QVERIFY(!PodStack::LogRotation::rotateForSize(empty, date, &sequence, &error));
    QCOMPARE(sequence, 0);
    QVERIFY(!error.isEmpty());

    QFile blocker(dir.filePath(QStringLiteral("blocker")));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.write("x");
    blocker.close();
    PodStack::LogRotation::Options blocked;
    blocked.directory = blocker.fileName();
    blocked.stem = QStringLiteral("podstackd");
    error.clear();
    QVERIFY(!PodStack::LogRotation::rotate(blocked, date, &error));
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(!PodStack::LogRotation::rotateForSize(blocked, date, &sequence, &error));
    QVERIFY(!error.isEmpty());

    PodStack::LogRotation::Options missing;
    missing.directory = dir.filePath(QStringLiteral("missing"));
    missing.stem = QStringLiteral("podstackd");
    QVERIFY(PodStack::LogRotation::rotateAtStart(missing, date));
    QCOMPARE(PodStack::LogRotation::prune(missing, date), 0);
    QCOMPARE(PodStack::LogRotation::clear(missing.directory), 0);
    QVERIFY(PodStack::LogRotation::rotatedFiles(missing).isEmpty());

    PodStack::LogRotation::Options collision;
    collision.directory = dir.filePath(QStringLiteral("collision"));
    collision.stem = QStringLiteral("podstackd");
    QVERIFY(QDir().mkpath(collision.directory));
    QFile current(PodStack::LogRotation::currentPath(collision));
    QVERIFY(current.open(QIODevice::WriteOnly | QIODevice::Append));
    current.write("x");
    current.close();
    QVERIFY(QDir().mkpath(PodStack::LogRotation::rotatedPath(collision, date)));
    error.clear();
    QVERIFY(!PodStack::LogRotation::rotate(collision, date, &error));
    QVERIFY(!error.isEmpty());

    PodStack::LogRotation::Options emptyCurrent;
    emptyCurrent.directory = dir.filePath(QStringLiteral("empty-current"));
    emptyCurrent.stem = QStringLiteral("podstackd");
    QVERIFY(PodStack::LogRotation::rotateForSize(emptyCurrent, date, &sequence, nullptr));

    error.clear();
    QFile today(PodStack::LogRotation::currentPath(collision));
    QVERIFY(today.open(QIODevice::WriteOnly | QIODevice::Append));
    today.write("y");
    today.close();
    QVERIFY(PodStack::LogRotation::rotateAtStart(collision, date, &error));
    error.clear();
    QCOMPARE(PodStack::LogRotation::prune(collision, date, &error), 0);
    QVERIFY(error.isEmpty());
}

void LoggingTest::rotationHelpers()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PodStack::LogRotation::Options options;
    options.directory = dir.path();
    options.stem = QStringLiteral("podstackd");

    const QDate today(2026, 11, 13);
    const auto touch = [&options](const QString &path, const QDateTime &mtime) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
            return false;
        }
        file.write("x");
        file.flush();
        const bool ok = file.setFileTime(mtime, QFileDevice::FileModificationTime);
        file.close();
        return ok;
    };

    QVERIFY(touch(PodStack::LogRotation::rotatedPath(options, today.addDays(-1)),
                  QDateTime(today.addDays(-1), QTime(12, 0))));
    QVERIFY(touch(PodStack::LogRotation::rotatedPath(options, today, 2),
                  QDateTime(today, QTime(11, 0))));
    QVERIFY(touch(PodStack::LogRotation::rotatedPath(options, today, 1),
                  QDateTime(today, QTime(11, 0))));
    QVERIFY(touch(dir.filePath(QStringLiteral("podstackd-notes.jsonl")),
                  QDateTime(today, QTime(10, 0))));

    const QStringList files = PodStack::LogRotation::rotatedFiles(options);
    QCOMPARE(files.size(), 3);
    QCOMPARE(files.at(0), PodStack::LogRotation::rotatedPath(options, today.addDays(-1)));
    QCOMPARE(files.at(1), PodStack::LogRotation::rotatedPath(options, today, 1));
    QCOMPARE(files.at(2), PodStack::LogRotation::rotatedPath(options, today, 2));

    QCOMPARE(PodStack::LogRotation::clear(options.directory), 4);
    QVERIFY(PodStack::LogRotation::rotatedFiles(options).isEmpty());
}

void LoggingTest::logWriterLifecycle()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PodStack::LogRotation::Options options;
    options.directory = dir.path();
    options.stem = QStringLiteral("writer");
    options.maxFileBytes = 64;
    options.maxTotalBytes = 1024 * 1024;

    PodStack::LogWriter writer(QStringLiteral("writer"), options, PodStack::LogLevel::Info);
    writer.start();
    writer.start();
    QMessageLogContext context;
    writer.enqueue(QtInfoMsg, context, QStringLiteral("primeira"));
    writer.enqueue(QtWarningMsg, context, QStringLiteral("segunda"));
    writer.flush();
    writer.requestRotation();
    writer.flush();
    writer.stop();
    writer.flush();

    QVERIFY(!writer.currentFile().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!PodStack::LogRotation::rotatedFiles(options).isEmpty(), 3000);

    QCOMPARE(PodStack::LogWriter::levelFromName(QStringLiteral("trace"), PodStack::LogLevel::Info),
             PodStack::LogLevel::Trace);
    QCOMPARE(PodStack::LogWriter::levelFromName(QStringLiteral("desconhecido"),
                                                PodStack::LogLevel::Warn),
             PodStack::LogLevel::Warn);
    QCOMPARE(PodStack::LogWriter::levelName(PodStack::LogLevel::Debug), QStringLiteral("debug"));
    QCOMPARE(PodStack::LogWriter::levelName(PodStack::LogLevel::Trace), QStringLiteral("trace"));
    QCOMPARE(PodStack::LogWriter::levelName(static_cast<PodStack::LogLevel>(99)),
             QStringLiteral("info"));

    QTemporaryDir smallDir;
    QVERIFY(smallDir.isValid());
    PodStack::LogRotation::Options small;
    small.directory = smallDir.path();
    small.stem = QStringLiteral("small");
    small.maxFileBytes = 32;
    small.maxTotalBytes = 1024 * 1024;
    PodStack::LogWriter smallWriter(QStringLiteral("small"), small, PodStack::LogLevel::Info);
    smallWriter.start();
    for (int index = 0; index < 20; ++index) {
        smallWriter.enqueue(QtInfoMsg, context, QString(200, QLatin1Char('x')));
    }
    smallWriter.flush();
    smallWriter.stop();
    QVERIFY(!PodStack::LogRotation::rotatedFiles(small).isEmpty());

    QFile blocker(dir.filePath(QStringLiteral("blocked")));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.write("x");
    blocker.close();
    PodStack::LogRotation::Options blocked;
    blocked.directory = blocker.fileName();
    blocked.stem = QStringLiteral("blocked");
    PodStack::LogWriter blockedWriter(QStringLiteral("blocked"), blocked, PodStack::LogLevel::Info);
    blockedWriter.start();
    blockedWriter.enqueue(QtInfoMsg, context, QStringLiteral("sem arquivo"));
    blockedWriter.flush();
    blockedWriter.stop();
}

void LoggingTest::logReaderFilters()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile daemon(dir.filePath(QStringLiteral("podstackd.jsonl")));
    QVERIFY(daemon.open(QIODevice::WriteOnly | QIODevice::Truncate));
    daemon.write(
        QByteArrayLiteral(
            R"({"ts":"2026-11-13T00:00:00Z","level":"info","category":"podstack.core","process":"podstackd","message":"info"})") +
        '\n');
    daemon.write(
        QByteArrayLiteral(
            R"({"ts":"2026-11-13T00:00:01Z","level":"error","category":"podstack.setup","process":"podstackd","message":"Authorization: Bearer segredo"})") +
        '\n');
    daemon.close();

    PodStack::LogReadOptions options;
    options.directory = dir.path();
    options.since = QDate(2026, 11, 1);
    options.level = QStringLiteral("warn");
    options.component = QStringLiteral("setup");
    PodStack::LogReadResult result;
    PodStack::Error error;
    QVERIFY2(PodStack::readUserLogs(options, &result, &error), qPrintable(error.message));
    QCOMPARE(result.lines.size(), 1);
    QVERIFY(!result.lines.first().raw.contains(QStringLiteral("segredo")));
    QVERIFY(result.lines.first().raw.contains(QStringLiteral("[redacted]")));

    const QString exportPath = dir.filePath(QStringLiteral("export.jsonl"));
    QVERIFY(PodStack::exportLogs(result.lines, exportPath, &error));
    QFile exported(exportPath);
    QVERIFY(exported.open(QIODevice::ReadOnly));
    QVERIFY(!exported.readAll().contains("segredo"));
    QVERIFY(exported.permissions().testFlag(QFileDevice::ReadOwner));
    QVERIFY(!exported.permissions().testFlag(QFileDevice::WriteOther));
}

void LoggingTest::logReaderEdges()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile daemon(dir.filePath(QStringLiteral("podstackd.jsonl")));
    QVERIFY(daemon.open(QIODevice::WriteOnly | QIODevice::Truncate));
    daemon.write(
        QByteArrayLiteral(
            R"({"ts":"2026-11-13T00:00:00Z","level":"debug","category":"podstack.core","process":"podstackd","message":"debug"})") +
        '\n');
    daemon.write(QByteArrayLiteral("not-json\n"));
    daemon.close();
    QFile gui(dir.filePath(QStringLiteral("podstack-gui-1.jsonl")));
    QVERIFY(gui.open(QIODevice::WriteOnly | QIODevice::Truncate));
    gui.write(
        QByteArrayLiteral(
            R"({"ts":"2026-11-13T00:00:01Z","level":"error","category":"qml","process":"podstack-gui-1","message":"erro qml"})") +
        '\n');
    gui.close();

    PodStack::LogReadOptions options;
    options.directory = dir.path();
    options.since = QDate::currentDate().addDays(1);
    PodStack::LogReadResult result;
    PodStack::Error error;
    QVERIFY(PodStack::readUserLogs(options, &result, &error));
    QCOMPARE(result.lines.size(), 0);

    options.since = QDate::currentDate().addDays(-6);
    options.level = QStringLiteral("trace");
    options.maxLines = 1;
    QVERIFY(PodStack::readUserLogs(options, &result, &error));
    QCOMPARE(result.lines.size(), 1);
    QVERIFY(result.truncated);

    options.maxLines = 100;
    options.component = QStringLiteral("qml");
    QVERIFY(PodStack::readUserLogs(options, &result, &error));
    QCOMPARE(result.lines.size(), 1);
    QCOMPARE(result.lines.first().component, QStringLiteral("qml"));

    options.component.clear();
    options.maxBytes = 1;
    QVERIFY(PodStack::readUserLogs(options, &result, &error));
    QVERIFY(result.truncated);
    options.maxBytes = 5 * 1024 * 1024;

    const QString target = dir.filePath(QStringLiteral("target.jsonl"));
    QVERIFY(PodStack::exportLogs({}, target, &error));
    const QString link = dir.filePath(QStringLiteral("link.jsonl"));
    QVERIFY(QFile::link(target, link));
    error = {};
    QVERIFY(!PodStack::exportLogs({}, link, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    error = {};
    QVERIFY(!PodStack::exportLogs({}, QStringLiteral("/nao-existe-podstack/x.jsonl"), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    const QString stateLogs = PodStack::Paths::stateDir() + QStringLiteral("/logs");
    QVERIFY(QDir().mkpath(stateLogs));
    QFile stateFile(stateLogs + QStringLiteral("/podstackd.jsonl"));
    QVERIFY(stateFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    stateFile.write("{}\n");
    stateFile.close();
    QVERIFY(PodStack::clearUserLogs(&error));
    QVERIFY(!QFileInfo::exists(stateFile.fileName()));
}

void LoggingTest::logReaderExportErrors()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PodStack::Error error;

    QVERIFY(!PodStack::exportLogs({}, dir.path(), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    error = {};
    QVERIFY(
        !PodStack::exportLogs({}, dir.filePath(QStringLiteral("missing/sub/file.jsonl")), &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));

    const QString target = dir.filePath(QStringLiteral("target.jsonl"));
    QFile real(target);
    QVERIFY(real.open(QIODevice::WriteOnly));
    real.close();
    const QString link = dir.filePath(QStringLiteral("link.jsonl"));
    QVERIFY(QFile::link(target, link));
    error = {};
    QVERIFY(!PodStack::exportLogs({}, link, &error));
    QCOMPARE(error.code, QStringLiteral("PODSTACK_IO_ERROR"));
}

void LoggingTest::logReaderComponents()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.filePath(QStringLiteral("podstackd.jsonl")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QByteArrayLiteral(
        R"({"ts":"2026-11-13T00:00:00Z","level":"info","category":"podstack.update","process":"podstackd","message":"update"})"
        "\n\n"
        R"({"ts":"2026-11-13T00:00:01Z","level":"info","category":"qml","process":"podstack-gui-1","message":"qml"})"
        "\n"
        R"({"ts":"2026-11-13T00:00:02Z","level":"info","category":"podstack.core","process":"podstack-gui-1","message":"gui"})"
        "\n"
        R"({"ts":"2026-11-13T00:00:03Z","level":"info","category":"podstack.core","process":"podstack-cli-1","message":"cli"})"
        "\n"
        R"({"ts":"2026-11-13T00:00:04Z","level":"info","category":"podstack.core","process":"podstackd","message":"daemon"})"
        "\n"));
    file.close();

    PodStack::LogReadOptions options;
    options.directory = dir.path();
    options.since = QDate::currentDate().addDays(-6);
    options.level = QStringLiteral("trace");
    PodStack::LogReadResult result;
    PodStack::Error error;
    QVERIFY(PodStack::readUserLogs(options, &result, &error));
    QCOMPARE(result.lines.size(), 5);
    QCOMPARE(result.lines.at(0).component, QStringLiteral("update"));
    QCOMPARE(result.lines.at(1).component, QStringLiteral("qml"));
    QCOMPARE(result.lines.at(2).component, QStringLiteral("gui"));
    QCOMPARE(result.lines.at(3).component, QStringLiteral("cli"));
    QCOMPARE(result.lines.at(4).component, QStringLiteral("daemon"));
}

void LoggingTest::loggingWrappers()
{
    PodStack::Logging::shutdown();
    PodStack::Logging::reload();
    QVERIFY(PodStack::Logging::currentLogFile().isEmpty());
    QCOMPARE(PodStack::Logging::level(), PodStack::LogLevel::Info);
    QCOMPARE(PodStack::Logging::levelName(PodStack::LogLevel::Warn), QStringLiteral("warn"));

    initLogging(QStringLiteral("wrappers"), QStringLiteral("debug"));
    QCOMPARE(PodStack::Logging::level(), PodStack::LogLevel::Debug);
    QVERIFY(!PodStack::Logging::logDirectory().isEmpty());
    QVERIFY(!PodStack::Logging::currentLogFile().isEmpty());
    stopLogging();
}

void LoggingTest::settingsSchema()
{
    const PodStack::SettingDef *level =
        PodStack::Settings::find(QStringLiteral("advanced.logLevel"));
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
