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
    Q_PROPERTY(QVariantList memHistory READ memHistory NOTIFY statsChanged)
    Q_PROPERTY(QVariantMap activityStats READ activityStats NOTIFY activityChanged)
    Q_PROPERTY(QVariantList activityCpuHistory READ activityCpuHistory NOTIFY activityChanged)
    Q_PROPERTY(QVariantList activityMemHistory READ activityMemHistory NOTIFY activityChanged)
    Q_PROPERTY(bool activityActive READ activityActive NOTIFY activityChanged)
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    Q_PROPERTY(QString filesPath READ filesPath NOTIFY filesChanged)
    Q_PROPERTY(QVariantList containers READ containers NOTIFY containersChanged)
    Q_PROPERTY(bool actionActive READ actionActive NOTIFY actionStateChanged)
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
    Q_PROPERTY(bool setupCompleted READ setupCompleted NOTIFY setupChanged)
    Q_PROPERTY(bool setupPending READ setupPending NOTIFY setupChanged)
    Q_PROPERTY(QString setupPodmanAction READ setupPodmanAction NOTIFY setupChanged)
    Q_PROPERTY(bool setupMandatoryOk READ setupMandatoryOk NOTIFY setupChanged)
    Q_PROPERTY(QVariantList setupChecks READ setupChecks NOTIFY setupChanged)
    Q_PROPERTY(QVariantMap setupSummary READ setupSummary NOTIFY setupChanged)
    Q_PROPERTY(QString setupDockerPolicy READ setupDockerPolicy NOTIFY setupChanged)
    Q_PROPERTY(QString setupJobId READ setupJobId NOTIFY setupJobChanged)
    Q_PROPERTY(bool setupJobActive READ setupJobActive NOTIFY setupJobChanged)
    Q_PROPERTY(int setupProgress READ setupProgress NOTIFY setupJobChanged)
    Q_PROPERTY(QStringList setupLog READ setupLog NOTIFY setupJobChanged)
    Q_PROPERTY(QString setupStatus READ setupStatus NOTIFY setupJobChanged)
    Q_PROPERTY(QString setupError READ setupError NOTIFY setupChanged)
    Q_PROPERTY(bool setupBusy READ setupBusy NOTIFY setupChanged)
    Q_PROPERTY(bool setupLoaded READ setupLoaded NOTIFY setupChanged)

public:
    explicit Backend(QObject *parent = nullptr);

    bool connected() const { return m_connected; }
    QString error() const { return m_error; }
    QString lastError() const { return m_lastError; }
    QString version() const;
    const QStringList &logLines() const { return m_logLines; }
    QVariantMap stats() const { return m_stats; }
    QVariantList cpuHistory() const { return m_cpuHistory; }
    QVariantList memHistory() const { return m_memHistory; }
    QVariantMap activityStats() const { return m_activityStats; }
    QVariantList activityCpuHistory() const { return m_activityCpuHistory; }
    QVariantList activityMemHistory() const { return m_activityMemHistory; }
    bool activityActive() const { return m_activityActive; }
    QVariantList files() const { return m_files; }
    QString filesPath() const { return m_filesPath; }
    QVariantList containers() const { return m_containers; }
    bool actionActive() const { return m_actionActive; }
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
    bool setupCompleted() const { return m_setupCompleted; }
    bool setupPending() const { return !m_setupCompleted && m_setupShowOnboarding; }
    QString setupPodmanAction() const;
    bool setupMandatoryOk() const;
    QVariantList setupChecks() const { return m_setupChecks; }
    QVariantMap setupSummary() const { return m_setupSummary; }
    QString setupDockerPolicy() const { return m_setupDockerPolicy; }
    QString setupJobId() const { return m_setupJobId; }
    bool setupJobActive() const { return m_setupJobActive; }
    int setupProgress() const { return m_setupProgress; }
    QStringList setupLog() const { return m_setupLog; }
    QString setupStatus() const { return m_setupStatus; }
    QString setupError() const { return m_setupError; }
    bool setupBusy() const { return m_setupBusy; }
    bool setupLoaded() const { return m_setupLoaded; }

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
    Q_INVOKABLE void startActivityMonitor();
    Q_INVOKABLE void stopActivityMonitor();
    Q_INVOKABLE void listFiles(const QString &id, const QString &path);
    Q_INVOKABLE void downloadFile(const QString &id, const QString &path);
    Q_INVOKABLE void uploadFile(const QString &id, const QString &path, const QString &sourcePath);
    Q_INVOKABLE QVariantMap settings() const;
    Q_INVOKABLE void saveEngineMode(const QString &mode);
    Q_INVOKABLE void saveTheme(const QString &theme);
    Q_INVOKABLE void saveTraySettings(bool keepRunning, bool showInTray, bool startAtLogin,
                                      bool notifications = true);
    Q_INVOKABLE void openTerminal(const QString &id);
    Q_INVOKABLE void setupRefresh();
    Q_INVOKABLE void setupCheck();
    Q_INVOKABLE void setupValidate();
    Q_INVOKABLE void setupApply(const QString &action, const QVariantList &components = {});
    Q_INVOKABLE void setupRemoveDocker();
    Q_INVOKABLE void setupCancel();
    Q_INVOKABLE void setupComplete();
    Q_INVOKABLE void setupSkip();

signals:
    void containersChanged(const QVariantList &items);
    void actionStateChanged();
    void stateChanged();
    void lastErrorChanged();
    void actionFinished(bool ok, const QString &error);
    void logsChanged();
    void statsChanged();
    void activityChanged();
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
    void containerEvent(const QVariantMap &event);
    void settingsChanged();
    void setupChanged();
    void setupJobChanged();

private:
    void onRefreshFinished();
    void onActionFinished();
    void onFilesFinished();
    void onCatalogFinished();
    void onSetupFinished();
    void applySetupStatus(const QJsonObject &status);
    void applySetupJob(const QJsonObject &job);
    void runSetupRequest(const QString &method, const QJsonObject &params);
    void refreshSection(const QString &section);
    void startNextCatalog();
    void runAction(const QString &method, const QJsonObject &params,
                   int timeoutMs = 30000);

    struct PendingAction {
        QString method;
        QJsonObject params;
        int timeoutMs = 30000;
    };
    void startAction(const PendingAction &action);
    void enqueueAction(const PendingAction &action);

    QFutureWatcher<QJsonObject> m_watcher;
    QFutureWatcher<QJsonObject> m_actionWatcher;
    QFutureWatcher<QJsonObject> m_filesWatcher;
    QFutureWatcher<QJsonObject> m_catalogWatcher;
    QFutureWatcher<QJsonObject> m_setupWatcher;
    QString m_setupPendingMethod;
    QString m_catalogSection;
    QStringList m_catalogQueue;
    StreamClient m_stream;
    StreamClient m_activityStream;
    StreamClient m_pullStream;
    StreamClient m_composeStream;
    StreamClient m_buildStream;
    StreamClient m_setupStream;
    StreamClient m_eventsStream;
    QTimer m_eventTimer;
    QTimer m_logsTimer;
    QTimer m_refreshTimer;
    QTimer m_eventsRetryTimer;
    int m_eventsRetryDelayMs = 1000;
    QList<PendingAction> m_actionQueue;
    bool m_actionActive = false;
    bool m_refreshAgain = false;
    bool m_logsPending = false;
    bool m_connected = false;
    QString m_error;
    QString m_lastError;
    QStringList m_logLines;
    QVariantMap m_stats;
    QVariantList m_cpuHistory;
    QVariantList m_memHistory;
    QVariantMap m_activityStats;
    QVariantList m_activityCpuHistory;
    QVariantList m_activityMemHistory;
    bool m_activityActive = false;
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
    bool m_setupCompleted = false;
    bool m_setupShowOnboarding = true;
    QVariantList m_setupChecks;
    QVariantMap m_setupSummary;
    QString m_setupDockerPolicy = QStringLiteral("keep");
    QString m_setupJobId;
    bool m_setupJobActive = false;
    int m_setupProgress = 0;
    QStringList m_setupLog;
    QString m_setupStatus;
    QString m_setupError;
    bool m_setupBusy = false;
    bool m_setupLoaded = false;
};

} // namespace PodStack
