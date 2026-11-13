// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>

class QThread;

/// Servidor local mínimo do qemu-guest-agent para testes, sem QEMU real.
class MockGuestAgent {
public:
    MockGuestAgent();
    explicit MockGuestAgent(QString socketPath);
    ~MockGuestAgent();

    MockGuestAgent(const MockGuestAgent &) = delete;
    MockGuestAgent &operator=(const MockGuestAgent &) = delete;

    QString socketPath() const { return m_path; }
    bool isListening() const { return m_listening; }

    void setOsInfo(const QJsonObject &info);
    void setFileContent(const QByteArray &data);
    void setFailCommand(const QString &command);
    void setFailCommandOnce(const QString &command);
    void setSilentCommand(const QString &command);
    void setExecSignal(bool enabled);
    void setExecPid(int pid);
    void setExecOutput(const QByteArray &output);
    void setFileOpenError(bool enabled);
    void setFileWriteCount(int count);
    QByteArray writtenData() const;

    int requestCount(const QString &command) const;
    QStringList commands() const;

private:
    QJsonObject osInfo() const;
    QByteArray fileContent() const;
    void handleLine(class QLocalSocket *socket, const QByteArray &line);

    QString m_path;
    bool m_listening = false;

    mutable QMutex m_mutex;
    QJsonObject m_osInfo;
    QByteArray m_fileContent;
    QByteArray m_writtenData;
    bool m_fileRead = false;
    QString m_failCommand;
    QString m_failOnce;
    QString m_silentCommand;
    bool m_execSignal = false;
    int m_execPid = 1;
    QByteArray m_execOutput = QByteArrayLiteral("ok");
    bool m_fileOpenError = false;
    int m_fileWriteCount = -1;
    QHash<QString, int> m_counts;
    QStringList m_commands;

    std::atomic_bool m_stop{false};
    QThread *m_thread = nullptr;
};
