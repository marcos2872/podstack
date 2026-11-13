// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/LogWriter.h"

#include "core/Redaction.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include <chrono>
#include <cstdio>

namespace PodStack {
namespace {

constexpr size_t kMaxQueueEntries = 8192;
constexpr qint64 kFlushIntervalMs = 1000;

LogLevel levelForType(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return LogLevel::Debug;
    case QtInfoMsg:
        return LogLevel::Info;
    case QtWarningMsg:
        return LogLevel::Warn;
    case QtCriticalMsg:
    case QtFatalMsg:
        return LogLevel::Error;
    }
    return LogLevel::Info;
}

QString contextString(const char *value)
{
    return value ? QString::fromUtf8(value) : QString();
}

} // namespace

LogWriter::LogWriter(QString processName, LogRotation::Options options, LogLevel level)
    : m_processName(std::move(processName))
    , m_pid(QCoreApplication::applicationPid())
    , m_options(std::move(options))
    , m_level(static_cast<int>(level))
{
}

LogWriter::~LogWriter()
{
    stop();
}

void LogWriter::start()
{
    if (m_thread.joinable()) {
        return;
    }

    QDir dir;
    dir.mkpath(m_options.directory);
    QFile::setPermissions(m_options.directory,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner
                              | QFileDevice::ExeOwner);
    (void) LogRotation::rotateAtStart(m_options, QDate::currentDate());
    (void) LogRotation::prune(m_options, QDate::currentDate());
    (void) openFile();

    m_lastFlushMs = QDateTime::currentMSecsSinceEpoch();
    m_thread = std::thread(&LogWriter::run, this);
}

void LogWriter::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_thread.joinable()) {
            return;
        }
        m_stop = true;
    }
    m_wake.notify_all();
    m_thread.join();
}

void LogWriter::enqueue(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const LogLevel level = levelForType(type);
    if (static_cast<int>(level) > m_level.load(std::memory_order_relaxed)) {
        return;
    }

    Entry entry;
    entry.timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    entry.level = level;
    entry.category = contextString(context.category);
    if (entry.category.isEmpty()) {
        entry.category = QStringLiteral("default");
    }
    entry.thread = QStringLiteral("0x")
                   + QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16);
    entry.message = message;
    entry.file = contextString(context.file);
    entry.line = context.line;
    entry.function = contextString(context.function);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.size() >= kMaxQueueEntries) {
            m_queue.pop_front();
            m_dropped.fetch_add(1, std::memory_order_relaxed);
        }
        m_queue.push_back(std::move(entry));
    }
    m_wake.notify_one();

    if (type == QtFatalMsg) {
        flush();
    }
}

void LogWriter::flush()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_thread.joinable() || m_stop.load(std::memory_order_relaxed)) {
        return;
    }
    m_flushRequested = true;
    m_flushDone = false;
    m_wake.notify_one();
    (void) m_flushCv.wait_for(lock, std::chrono::seconds(3), [this] { return m_flushDone; });
}

void LogWriter::requestRotation()
{
    m_rotationRequested = true;
    m_wake.notify_one();
}

void LogWriter::update(LogRotation::Options options, LogLevel level)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_options = std::move(options);
        m_level.store(static_cast<int>(level), std::memory_order_relaxed);
    }
    m_wake.notify_one();
}

QString LogWriter::currentFile() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return LogRotation::currentPath(m_options);
}

int LogWriter::dropped() const
{
    return m_dropped.load(std::memory_order_relaxed);
}

LogLevel LogWriter::levelFromName(const QString &name, LogLevel fallback)
{
    const QString normalized = name.trimmed().toLower();
    if (normalized == QStringLiteral("error")) {
        return LogLevel::Error;
    }
    if (normalized == QStringLiteral("warn") || normalized == QStringLiteral("warning")) {
        return LogLevel::Warn;
    }
    if (normalized == QStringLiteral("info")) {
        return LogLevel::Info;
    }
    if (normalized == QStringLiteral("debug")) {
        return LogLevel::Debug;
    }
    if (normalized == QStringLiteral("trace")) {
        return LogLevel::Trace;
    }
    return fallback;
}

QString LogWriter::levelName(LogLevel level)
{
    switch (level) {
    case LogLevel::Error:
        return QStringLiteral("error");
    case LogLevel::Warn:
        return QStringLiteral("warn");
    case LogLevel::Info:
        return QStringLiteral("info");
    case LogLevel::Debug:
        return QStringLiteral("debug");
    case LogLevel::Trace:
        return QStringLiteral("trace");
    }
    return QStringLiteral("info");
}

void LogWriter::run()
{
    for (;;) {
        std::deque<Entry> batch;
        LogRotation::Options options;
        bool stop = false;
        bool rotate = false;
        bool flushRequested = false;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait_for(lock, std::chrono::milliseconds(kFlushIntervalMs), [this] {
                return m_stop.load(std::memory_order_relaxed) || m_rotationRequested.load()
                       || m_flushRequested || !m_queue.empty();
            });
            stop = m_stop.load(std::memory_order_relaxed);
            rotate = m_rotationRequested.exchange(false);
            flushRequested = m_flushRequested;
            m_flushRequested = false;
            options = m_options;
            batch.swap(m_queue);
        }

        if (rotate) {
            rotateDaily(m_fileDate.isValid() ? m_fileDate : QDate::currentDate());
        }

        if (m_fileDate.isValid() && QDate::currentDate() != m_fileDate) {
            rotateDaily(m_fileDate);
        }

        if (!m_file && !openFile()) {
            // Sem arquivo ainda dá para manter o stderr/journal; tenta de novo
            // no próximo ciclo.
        }

        bool flushNeeded = false;
        for (const Entry &entry : batch) {
            if (entry.level == LogLevel::Warn || entry.level == LogLevel::Error) {
                flushNeeded = true;
            }
            writeEntry(m_file, entry);
        }
        writeDropped(m_file);

        if (m_file && m_file->size() >= options.maxFileBytes) {
            rotateForSize();
        }

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (flushRequested || flushNeeded || now - m_lastFlushMs >= kFlushIntervalMs) {
            flushOutput(m_file);
            m_lastFlushMs = now;
        }
        if (flushRequested) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_flushDone = true;
            m_flushCv.notify_all();
        }

        if (stop) {
            bool queueEmpty = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                queueEmpty = m_queue.empty();
            }
            if (batch.empty() && queueEmpty) {
                break;
            }
        }
    }

    closeFile();
    flushStderr();
}

void LogWriter::writeEntry(QFile *file, const Entry &entry)
{
    const QString message = Redaction::text(entry.message);
    if (file && file->isOpen()) {
        QJsonObject context;
        context.insert(QStringLiteral("file"), entry.file);
        context.insert(QStringLiteral("line"), entry.line);
        context.insert(QStringLiteral("function"), entry.function);
        QJsonObject root;
        root.insert(QStringLiteral("ts"), entry.timestamp);
        root.insert(QStringLiteral("level"), levelName(entry.level));
        root.insert(QStringLiteral("category"), entry.category);
        root.insert(QStringLiteral("process"), m_processName);
        root.insert(QStringLiteral("pid"), m_pid);
        root.insert(QStringLiteral("thread"), entry.thread);
        root.insert(QStringLiteral("message"), message);
        root.insert(QStringLiteral("context"), context);
        const QByteArray line = QJsonDocument(root).toJson(QJsonDocument::Compact) + '\n';
        (void) file->write(line);
    }

    const QByteArray human = (entry.timestamp + QLatin1Char(' ') + QLatin1Char('[')
                              + entry.category + QStringLiteral("] ") + levelName(entry.level)
                              + QStringLiteral(": ") + message + QLatin1Char('\n'))
                                 .toUtf8();
    (void) std::fwrite(human.constData(), 1, static_cast<size_t>(human.size()), stderr);
}

bool LogWriter::openFile()
{
    closeFile();
    const QString path = LogRotation::currentPath(m_options);
    auto *file = new QFile(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Append)) {
        delete file;
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    m_file = file;
    m_fileDate = QDate::currentDate();
    return true;
}

void LogWriter::closeFile()
{
    if (m_file) {
        if (m_file->isOpen()) {
            m_file->flush();
            m_file->close();
        }
        delete m_file;
        m_file = nullptr;
    }
}

void LogWriter::rotateDaily(const QDate &date)
{
    closeFile();
    (void) LogRotation::rotate(m_options, date);
    (void) LogRotation::prune(m_options, QDate::currentDate());
    (void) openFile();
}

void LogWriter::rotateForSize()
{
    closeFile();
    (void) LogRotation::rotateForSize(m_options, QDate::currentDate());
    (void) LogRotation::prune(m_options, QDate::currentDate());
    (void) openFile();
}

void LogWriter::writeDropped(QFile *file)
{
    const int count = m_dropped.exchange(0, std::memory_order_relaxed);
    if (count <= 0) {
        return;
    }
    Entry entry;
    entry.timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    entry.level = LogLevel::Warn;
    entry.category = QStringLiteral("podstack.logging");
    entry.thread = QStringLiteral("0x0");
    entry.message = QStringLiteral("fila de logs cheia; %1 entradas descartadas").arg(count);
    writeEntry(file, entry);
}

void LogWriter::flushOutput(QFile *file)
{
    if (file && file->isOpen()) {
        file->flush();
    }
    flushStderr();
}

void LogWriter::flushStderr()
{
    std::fflush(stderr);
}

} // namespace PodStack
