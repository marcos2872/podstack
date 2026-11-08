#pragma once

#include "gui/StreamClient.h"

#include <QFutureWatcher>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QSet>
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
    Q_PROPERTY(QVariantList images READ images NOTIFY imagesChanged)
    Q_PROPERTY(QVariantList volumes READ volumes NOTIFY volumesChanged)
    Q_PROPERTY(QVariantList networks READ networks NOTIFY networksChanged)
    Q_PROPERTY(QVariantList pods READ pods NOTIFY podsChanged)
    Q_PROPERTY(bool pullActive READ pullActive NOTIFY pullStateChanged)
    Q_PROPERTY(QString pullStatus READ pullStatus NOTIFY pullStateChanged)
    Q_PROPERTY(QVariantMap diskUsage READ diskUsage NOTIFY diskUsageChanged)
    Q_PROPERTY(QVariantList composeProjects READ composeProjects NOTIFY composeProjectsChanged)
    Q_PROPERTY(QStringList composeLogLines READ composeLogLines NOTIFY composeLogsChanged)
    Q_PROPERTY(QVariantList builds READ builds NOTIFY buildsChanged)
    Q_PROPERTY(QStringList buildLog READ buildLog NOTIFY buildLogChanged)
    Q_PROPERTY(bool buildActive READ buildActive NOTIFY buildStateChanged)
    Q_PROPERTY(QString buildStatus READ buildStatus NOTIFY buildStateChanged)
    Q_PROPERTY(QString activeBuildId READ activeBuildId NOTIFY buildStateChanged)
    Q_PROPERTY(QString eventMessage READ eventMessage NOTIFY eventsChanged)
    Q_PROPERTY(QString eventKind READ eventKind NOTIFY eventsChanged)

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
    QVariantList images() const { return m_images; }
    QVariantList volumes() const { return m_volumes; }
    QVariantList networks() const { return m_networks; }
    QVariantList pods() const { return m_pods; }
    bool pullActive() const { return m_pullActive; }
    QString pullStatus() const { return m_pullStatus; }
    QVariantMap diskUsage() const { return m_diskUsage; }
    QVariantList composeProjects() const { return m_composeProjects; }
    QStringList composeLogLines() const { return m_composeLogLines; }
    QVariantList builds() const { return m_builds; }
    QStringList buildLog() const { return m_buildLog; }
    bool buildActive() const { return m_buildActive; }
    QString buildStatus() const { return m_buildStatus; }
    QString activeBuildId() const { return m_activeBuildId; }
    QString eventMessage() const { return m_eventMessage; }
    QString eventKind() const { return m_eventKind; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshCatalog();
    Q_INVOKABLE void refreshImages();
    Q_INVOKABLE void refreshVolumes();
    Q_INVOKABLE void refreshNetworks();
    Q_INVOKABLE void refreshPods();
    Q_INVOKABLE void pullImage(const QString &reference, const QString &platform = {});
    Q_INVOKABLE void cancelPull();
    Q_INVOKABLE void removeImage(const QString &id, bool force);
    Q_INVOKABLE void pruneImages(bool all);
    Q_INVOKABLE void tagImage(const QString &id, const QString &repoTag);
    Q_INVOKABLE void untagImage(const QString &id, const QString &repoTag);
    Q_INVOKABLE void exportImage(const QString &id, const QString &path);
    Q_INVOKABLE void loadImage(const QString &path);
    Q_INVOKABLE void createVolume(const QString &name, const QString &driver);
    Q_INVOKABLE void removeVolume(const QString &name, bool force);
    Q_INVOKABLE void pruneVolumes();
    Q_INVOKABLE void cloneVolume(const QString &source, const QString &destination);
    Q_INVOKABLE void importHostPath(const QString &name, const QString &sourcePath);
    Q_INVOKABLE void createNetwork(const QString &name, const QString &subnet,
                                   const QString &gateway);
    Q_INVOKABLE void removeNetwork(const QString &name, bool force);
    Q_INVOKABLE void connectNetwork(const QString &name, const QString &containerId);
    Q_INVOKABLE void disconnectNetwork(const QString &name, const QString &containerId);
    Q_INVOKABLE void createPod(const QString &name);
    Q_INVOKABLE void podAction(const QString &action, const QString &id);
    Q_INVOKABLE void removePod(const QString &id, bool force);
    Q_INVOKABLE void refreshCompose();
    Q_INVOKABLE void composeUp(const QString &projectPath);
    Q_INVOKABLE void composeDown(const QString &projectPath, bool volumes);
    Q_INVOKABLE void composeRestart(const QString &projectPath, const QString &service);
    Q_INVOKABLE void composeStop(const QString &projectPath, const QString &service);
    Q_INVOKABLE void composeStart(const QString &projectPath, const QString &service);
    Q_INVOKABLE void subscribeComposeLogs(const QString &project, const QString &service);
    Q_INVOKABLE void unsubscribeComposeLogs();
    Q_INVOKABLE void openPath(const QString &path);
    Q_INVOKABLE void refreshBuilds();
    Q_INVOKABLE void startBuild(const QVariantMap &spec);
    Q_INVOKABLE void cancelBuild();
    Q_INVOKABLE void removeBuild(const QString &id);
    Q_INVOKABLE void lifecycle(const QString &action, const QString &id);
    Q_INVOKABLE void removeContainer(const QString &id, bool force, bool volumes);
    Q_INVOKABLE void createContainer(const QVariantMap &spec, bool start);
    Q_INVOKABLE void batchAll(const QString &action);
    Q_INVOKABLE void batchIds(const QStringList &ids, const QString &action);
    Q_INVOKABLE void renameContainer(const QString &id, const QString &name);
    Q_INVOKABLE void duplicateContainer(const QString &id);
    Q_INVOKABLE void subscribeLogs(const QString &id, int tail, bool follow);
    Q_INVOKABLE void unsubscribeLogs();
    Q_INVOKABLE void clearLogs();
    Q_INVOKABLE void subscribeStats(const QString &id);
    Q_INVOKABLE void unsubscribeStats();
    Q_INVOKABLE void listFiles(const QString &id, const QString &path);
    Q_INVOKABLE void downloadFile(const QString &id, const QString &path);
    Q_INVOKABLE void uploadFile(const QString &id, const QString &path, const QString &sourcePath);
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
    void imagesChanged();
    void volumesChanged();
    void networksChanged();
    void podsChanged();
    void pullStateChanged();
    void diskUsageChanged();
    void composeProjectsChanged();
    void composeLogsChanged();
    void buildsChanged();
    void buildLogChanged();
    void buildStateChanged();
    void eventsChanged();

private:
    void onRefreshFinished();
    void onActionFinished();
    void onFilesFinished();
    void onCatalogFinished();
    void refreshSection(const QString &section);
    void startNextCatalog();
    void runAction(const QString &method, const QJsonObject &params,
                   int timeoutMs = 30000);

    QFutureWatcher<QJsonObject> m_watcher;
    QFutureWatcher<QJsonObject> m_actionWatcher;
    QFutureWatcher<QJsonObject> m_filesWatcher;
    QFutureWatcher<QJsonObject> m_catalogWatcher;
    QString m_catalogSection;
    QStringList m_catalogQueue;
    StreamClient m_stream;
    StreamClient m_pullStream;
    StreamClient m_composeStream;
    StreamClient m_buildStream;
    StreamClient m_eventsStream;
    QTimer m_eventTimer;
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
    QVariantList m_images;
    QVariantList m_volumes;
    QVariantList m_networks;
    QVariantList m_pods;
    bool m_pullActive = false;
    QString m_pullStatus;
    QVariantMap m_diskUsage;
    QVariantList m_composeProjects;
    QStringList m_composeLogLines;
    QVariantList m_builds;
    QStringList m_buildLog;
    bool m_buildActive = false;
    QString m_buildStatus;
    QString m_activeBuildId;
    QString m_eventMessage;
    QString m_eventKind;
    QSet<QString> m_recentEvents;
};

} // namespace PodStack
