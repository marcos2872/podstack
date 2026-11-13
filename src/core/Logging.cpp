// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/Logging.h"

#include "core/Config.h"
#include "core/Paths.h"
#include "core/Redaction.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QTimer>
#include <QTime>

#include <algorithm>
#include <memory>

Q_LOGGING_CATEGORY(lcCore, "podstack.core")
Q_LOGGING_CATEGORY(lcPodman, "podstack.podman")
Q_LOGGING_CATEGORY(lcRpc, "podstack.rpc")

namespace PodStack::Logging {
namespace {

constexpr qint64 kDefaultMaxBytes = 10LL * 1024 * 1024;
constexpr qint64 kMinMaxBytes = 1024LL * 1024;
constexpr qint64 kMaxMaxBytes = 1024LL * 1024 * 1024;
constexpr qint64 kMaxTotalBytes = 200LL * 1024 * 1024;

std::unique_ptr<LogWriter> g_writer;
QtMessageHandler g_previousHandler = nullptr;
QTimer *g_timer = nullptr;
bool g_verbose = false;
bool g_postRoutineRegistered = false;
LogLevel g_level = LogLevel::Info;
QString g_logDirectory;

QString executableName()
{
    QString name = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    if (name.isEmpty()) {
        name = QCoreApplication::applicationName();
    }
    if (name.isEmpty()) {
        name = QStringLiteral("podstack");
    }
    if (name == QStringLiteral("podstack")) {
        name = QStringLiteral("podstack-cli");
    }
    return name;
}

QString processStem()
{
    const QString name = executableName();
    if (name == QStringLiteral("podstackd")) {
        return name;
    }
    return name + QLatin1Char('-') + QString::number(QCoreApplication::applicationPid());
}

QString configuredLogDirectory()
{
    const QString override = qEnvironmentVariable("PODSTACK_LOG_DIR").trimmed();
    if (!override.isEmpty()) {
        return override;
    }
    return Paths::stateDir() + QStringLiteral("/logs");
}

LogLevel configuredLevel()
{
    const QString environment = qEnvironmentVariable("PODSTACK_LOG_LEVEL").trimmed();
    const LogLevel fallback = g_verbose ? LogLevel::Debug : LogLevel::Info;
    if (!environment.isEmpty()) {
        return LogWriter::levelFromName(environment, fallback);
    }
    if (g_verbose) {
        return LogLevel::Debug;
    }
    const Config config = Config::load();
    return LogWriter::levelFromName(
        config.get(QStringLiteral("advanced.logLevel")).toString(), LogLevel::Info);
}

int configuredRetentionDays()
{
    const Config config = Config::load();
    return std::clamp(
        config.get(QStringLiteral("advanced.logRetentionDays")).toInt(7), 1, 30);
}

qint64 configuredMaxBytes()
{
    const Config config = Config::load();
    const qint64 value = config.get(QStringLiteral("advanced.logMaxBytes"))
                             .toVariant()
                             .toLongLong();
    return std::clamp(value > 0 ? value : kDefaultMaxBytes, kMinMaxBytes, kMaxMaxBytes);
}

LogRotation::Options currentOptions()
{
    LogRotation::Options options;
    options.directory = g_logDirectory;
    options.stem = processStem();
    options.retentionDays = configuredRetentionDays();
    options.maxFileBytes = configuredMaxBytes();
    options.maxTotalBytes = kMaxTotalBytes;
    return options;
}

void applyFilterRules(LogLevel level)
{
    QString rules;
    switch (level) {
    case LogLevel::Error:
        rules = QStringLiteral("podstack.*.debug=false\npodstack.*.info=false\n"
                               "podstack.*.warning=false");
        break;
    case LogLevel::Warn:
        rules = QStringLiteral("podstack.*.debug=false\npodstack.*.info=false");
        break;
    case LogLevel::Info:
        rules = QStringLiteral("podstack.*.debug=false");
        break;
    case LogLevel::Debug:
    case LogLevel::Trace:
        rules = QStringLiteral("podstack.*.debug=true");
        break;
    }
    QLoggingCategory::setFilterRules(rules);
}

void scheduleTimer();

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (g_writer) {
        g_writer->enqueue(type, context, message);
        return;
    }
    if (g_previousHandler) {
        g_previousHandler(type, context, message);
    }
}

void scheduleTimer()
{
    if (!QCoreApplication::instance()) {
        return;
    }
    if (!g_timer) {
        g_timer = new QTimer();
        g_timer->setSingleShot(true);
        QObject::connect(g_timer, &QTimer::timeout, []() {
            if (g_writer) {
                g_writer->requestRotation();
            }
            scheduleTimer();
        });
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime next(now.date().addDays(1), QTime(0, 0));
    qint64 milliseconds = now.msecsTo(next);
    if (milliseconds < 1000) {
        milliseconds = 1000;
    }
    g_timer->start(static_cast<int>(std::min<qint64>(milliseconds, 24LL * 60 * 60 * 1000)));
}

} // namespace

void init(bool verbose)
{
    shutdown();

    g_verbose = verbose;
    qSetMessagePattern(QStringLiteral(
        "%{time yyyy-MM-ddTHH:mm:ss.zzz} [%{category}] %{message}"));

    g_logDirectory = configuredLogDirectory();
    g_level = configuredLevel();
    applyFilterRules(g_level);

    g_writer = std::make_unique<LogWriter>(processStem(), currentOptions(), g_level);
    g_writer->start();
    g_previousHandler = qInstallMessageHandler(messageHandler);
    scheduleTimer();

    if (!g_postRoutineRegistered) {
        qAddPostRoutine(&shutdown);
        g_postRoutineRegistered = true;
    }
}

void reload()
{
    if (!g_writer) {
        return;
    }
    g_level = configuredLevel();
    applyFilterRules(g_level);
    g_writer->update(currentOptions(), g_level);
}

void shutdown()
{
    if (g_timer) {
        g_timer->stop();
        delete g_timer;
        g_timer = nullptr;
    }
    if (g_previousHandler) {
        qInstallMessageHandler(g_previousHandler);
        g_previousHandler = nullptr;
    } else if (g_writer) {
        qInstallMessageHandler(nullptr);
    }
    if (g_writer) {
        g_writer->stop();
        g_writer.reset();
    }
    g_logDirectory.clear();
    g_level = LogLevel::Info;
}

void flush()
{
    if (g_writer) {
        g_writer->flush();
    }
}

QString logDirectory()
{
    return g_logDirectory;
}

QString currentLogFile()
{
    return g_writer ? g_writer->currentFile() : QString();
}

LogLevel level()
{
    return g_level;
}

QString levelName(LogLevel level)
{
    return LogWriter::levelName(level);
}

} // namespace PodStack::Logging
