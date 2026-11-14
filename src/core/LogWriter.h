// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/LogRotation.h"

#include <QDate>
#include <QString>
#include <QtCore/qlogging.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

class QFile;

namespace PodStack {

enum class LogLevel : int {
    Error = 0,
    Warn = 1,
    Info = 2,
    Debug = 3,
    Trace = 4,
};

/// Writer assíncrono de logs JSON Lines.
///
/// O handler de mensagens só enfileira; o I/O (arquivo e stderr) acontece
/// nesta thread. A fila é limitada e descarta a entrada mais antiga quando
/// cheia, mantendo a UI responsiva.
class LogWriter {
public:
    LogWriter(QString processName, LogRotation::Options options, LogLevel level);
    ~LogWriter();

    LogWriter(const LogWriter &) = delete;
    LogWriter &operator=(const LogWriter &) = delete;

    void start();
    void stop();
    void enqueue(QtMsgType type, const QMessageLogContext &context, const QString &message);
    void flush();
    void clear();
    void requestRotation();
    void update(LogRotation::Options options, LogLevel level);

    QString currentFile() const;
    int dropped() const;

    static LogLevel levelFromName(const QString &name, LogLevel fallback);
    static QString levelName(LogLevel level);

private:
    struct Entry {
        QString timestamp;
        LogLevel level = LogLevel::Info;
        QString category;
        QString thread;
        QString message;
        QString file;
        int line = 0;
        QString function;
    };

    void run();
    void writeEntry(QFile *file, const Entry &entry);
    bool openFile();
    void closeFile();
    void rotateDaily(const QDate &date);
    void rotateForSize();
    void writeDropped(QFile *file);
    void flushOutput(QFile *file);
    void flushStderr();
    void scheduleStop();

    QString m_processName;
    qint64 m_pid = 0;
    LogRotation::Options m_options;
    std::atomic<int> m_level;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_rotationRequested{false};
    std::atomic<bool> m_clearRequested{false};
    std::atomic<int> m_dropped{0};

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_flushCv;
    std::deque<Entry> m_queue;
    bool m_flushRequested = false;
    bool m_flushDone = false;
    std::thread m_thread;

    // Apenas a writer thread usa estes membros.
    QFile *m_file = nullptr;
    QDate m_fileDate;
    qint64 m_lastFlushMs = 0;
};

} // namespace PodStack
