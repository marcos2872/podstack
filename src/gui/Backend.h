#pragma once

#include "gui/StreamClient.h"

#include <QFutureWatcher>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace PodStack {

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QStringList logLines READ logLines NOTIFY logsChanged)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)
    Q_PROPERTY(QVariantList cpuHistory READ cpuHistory NOTIFY statsChanged)
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    Q_PROPERTY(QString filesPath READ filesPath NOTIFY filesChanged)

public:
    explicit Backend(QObject *parent = nullptr);

    bool connected() const { return m_connected; }
    QString error() const { return m_error; }
    QString lastError() const { return m_lastError; }
    QString version() const;
    const QStringList &logLines() const { return m_logLines; }
    QVariantMap stats() const { return m_stats; }
    QVariantList cpuHistory() const { return m_cpuHistory; }
    QVariantList files() const { return m_files; }
    QString filesPath() const { return m_filesPath; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void lifecycle(const QString &action, const QString &id);
    Q_INVOKABLE void removeContainer(const QString &id, bool force, bool volumes);
    Q_INVOKABLE void createContainer(const QVariantMap &spec, bool start);
    Q_INVOKABLE void batchAll(const QString &action);
    Q_INVOKABLE void renameContainer(const QString &id, const QString &name);
    Q_INVOKABLE void duplicateContainer(const QString &id);
    Q_INVOKABLE void subscribeLogs(const QString &id, int tail, bool follow);
    Q_INVOKABLE void unsubscribeLogs();
    Q_INVOKABLE void clearLogs();
    Q_INVOKABLE void subscribeStats(const QString &id);
    Q_INVOKABLE void unsubscribeStats();
    Q_INVOKABLE void listFiles(const QString &id, const QString &path);
    Q_INVOKABLE void downloadFile(const QString &id, const QString &path);
    Q_INVOKABLE QVariantMap settings() const;
    Q_INVOKABLE void saveEngineMode(const QString &mode);
    Q_INVOKABLE void openTerminal(const QString &id);

signals:
    void containersChanged(const QVariantList &items);
    void stateChanged();
    void lastErrorChanged();
    void actionFinished(bool ok, const QString &error);
    void logsChanged();
    void statsChanged();
    void filesChanged();

private:
    void onRefreshFinished();
    void onActionFinished();
    void onFilesFinished();
    void runAction(const QString &method, const QJsonObject &params);

    QFutureWatcher<QJsonObject> m_watcher;
    QFutureWatcher<QJsonObject> m_actionWatcher;
    QFutureWatcher<QJsonObject> m_filesWatcher;
    StreamClient m_stream;
    QTimer m_logsTimer;
    bool m_logsPending = false;
    bool m_connected = false;
    QString m_error;
    QString m_lastError;
    QStringList m_logLines;
    QVariantMap m_stats;
    QVariantList m_cpuHistory;
    QVariantList m_files;
    QString m_filesPath = QStringLiteral("/");
    QVariantList m_containers;
};

} // namespace PodStack
