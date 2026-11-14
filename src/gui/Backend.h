#pragma once

#include "gui/StreamClient.h"
#include "core/LogReader.h"

#include <QFutureWatcher>
#include <QJsonObject>
#include <QObject>
#include <QQueue>
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
    Q_PROPERTY(QVariantMap containerDetail READ containerDetail NOTIFY containerDetailChanged)
    Q_PROPERTY(QString containerDetailError READ containerDetailError NOTIFY containerDetailChanged)
    Q_PROPERTY(bool containerDetailLoading READ containerDetailLoading
               NOTIFY containerDetailChanged)
    Q_PROPERTY(QVariantMap containerDetailStats READ containerDetailStats
               NOTIFY containerDetailStatsChanged)
    Q_PROPERTY(QVariantList containerDetailCpuHistory READ containerDetailCpuHistory
               NOTIFY containerDetailStatsChanged)
    Q_PROPERTY(QVariantList containerDetailMemHistory READ containerDetailMemHistory
               NOTIFY containerDetailStatsChanged)
    Q_PROPERTY(QVariantList containerDetailNetRxHistory READ containerDetailNetRxHistory
               NOTIFY containerDetailStatsChanged)
    Q_PROPERTY(QVariantList containerDetailNetTxHistory READ containerDetailNetTxHistory
               NOTIFY containerDetailStatsChanged)
    Q_PROPERTY(QVariantList containerDetailDiskReadHistory READ containerDetailDiskReadHistory
               NOTIFY containerDetailStatsChanged)
    Q_PROPERTY(QVariantList containerDetailDiskWriteHistory READ containerDetailDiskWriteHistory
               NOTIFY containerDetailStatsChanged)
    Q_PROPERTY(QString containerDetailStatsError READ containerDetailStatsError
               NOTIFY containerDetailStatsChanged)
    Q_PROPERTY(QStringList containerDetailLogLines READ containerDetailLogLines
               NOTIFY containerDetailLogsChanged)
    Q_PROPERTY(QString containerDetailLogsError READ containerDetailLogsError
               NOTIFY containerDetailLogsChanged)
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
    Q_PROPERTY(QString k8sState READ k8sState NOTIFY k8sChanged)
    Q_PROPERTY(bool k8sEnabled READ k8sEnabled NOTIFY k8sChanged)
    Q_PROPERTY(QVariantList k8sPods READ k8sPods NOTIFY k8sChanged)
    Q_PROPERTY(QVariantList k8sServices READ k8sServices NOTIFY k8sChanged)
    Q_PROPERTY(QString k8sError READ k8sError NOTIFY k8sChanged)
    Q_PROPERTY(bool k8sLoading READ k8sLoading NOTIFY k8sChanged)
    Q_PROPERTY(QStringList k8sLogLines READ k8sLogLines NOTIFY k8sLogsChanged)
    Q_PROPERTY(QString k8sLogError READ k8sLogError NOTIFY k8sLogsChanged)
    Q_PROPERTY(bool k8sLogActive READ k8sLogActive NOTIFY k8sLogsChanged)
    Q_PROPERTY(bool k8sPortForwardActive READ k8sPortForwardActive NOTIFY k8sForwardChanged)
    Q_PROPERTY(QString k8sPortForwardId READ k8sPortForwardId NOTIFY k8sForwardChanged)
    Q_PROPERTY(QString k8sPortForwardError READ k8sPortForwardError NOTIFY k8sForwardChanged)
    Q_PROPERTY(bool k8sJobActive READ k8sJobActive NOTIFY k8sJobChanged)
    Q_PROPERTY(QString k8sJobStage READ k8sJobStage NOTIFY k8sJobChanged)
    Q_PROPERTY(int k8sJobProgress READ k8sJobProgress NOTIFY k8sJobChanged)
    Q_PROPERTY(QString k8sJobError READ k8sJobError NOTIFY k8sJobChanged)
    Q_PROPERTY(bool pullActive READ pullActive NOTIFY pullStateChanged)
    Q_PROPERTY(QString pullStatus READ pullStatus NOTIFY pullStateChanged)
    Q_PROPERTY(QVariantMap diskUsage READ diskUsage NOTIFY diskUsageChanged)
    Q_PROPERTY(QVariantMap prunePreview READ prunePreview NOTIFY prunePreviewChanged)
    Q_PROPERTY(QVariantList composeProjects READ composeProjects NOTIFY composeProjectsChanged)
    Q_PROPERTY(QStringList composeLogLines READ composeLogLines NOTIFY composeLogsChanged)
    Q_PROPERTY(QVariantList builds READ builds NOTIFY buildsChanged)
    Q_PROPERTY(QStringList buildLog READ buildLog NOTIFY buildLogChanged)
    Q_PROPERTY(bool buildActive READ buildActive NOTIFY buildStateChanged)
    Q_PROPERTY(QString buildStatus READ buildStatus NOTIFY buildStateChanged)
    Q_PROPERTY(QString activeBuildId READ activeBuildId NOTIFY buildStateChanged)
    Q_PROPERTY(QVariantList machines READ machines NOTIFY machinesChanged)
    Q_PROPERTY(QVariantList machineDistros READ machineDistros NOTIFY machineDistrosChanged)
    Q_PROPERTY(QVariantMap machineInfo READ machineInfo NOTIFY machineInfoChanged)
    Q_PROPERTY(
        QVariantList machineSerialPorts READ machineSerialPorts NOTIFY machineSerialPortsChanged)
    Q_PROPERTY(
        QVariantList machineUsbDevices READ machineUsbDevices NOTIFY machineUsbDevicesChanged)
    Q_PROPERTY(QStringList machineLogLines READ machineLogLines NOTIFY machineLogsChanged)
    Q_PROPERTY(QString machineLogsCapturedAt READ machineLogsCapturedAt
               NOTIFY machineLogsChanged)
    Q_PROPERTY(QVariantMap machineStats READ machineStats NOTIFY machineStatsChanged)
    Q_PROPERTY(QVariantList machineCpuHistory READ machineCpuHistory
               NOTIFY machineStatsChanged)
    Q_PROPERTY(QVariantList machineMemHistory READ machineMemHistory
               NOTIFY machineStatsChanged)
    Q_PROPERTY(QVariantList machineNetRxHistory READ machineNetRxHistory
               NOTIFY machineStatsChanged)
    Q_PROPERTY(QVariantList machineNetTxHistory READ machineNetTxHistory
               NOTIFY machineStatsChanged)
    Q_PROPERTY(QString machineStatsError READ machineStatsError NOTIFY machineStatsChanged)
    Q_PROPERTY(bool machineConsoleActive READ machineConsoleActive NOTIFY machineConsoleChanged)
    Q_PROPERTY(QString machineConsoleText READ machineConsoleText NOTIFY machineConsoleChanged)
    Q_PROPERTY(QString machineConsoleError READ machineConsoleError NOTIFY machineConsoleChanged)
    Q_PROPERTY(QVariantList machineFiles READ machineFiles NOTIFY machineFilesChanged)
    Q_PROPERTY(QString machineFilesPath READ machineFilesPath NOTIFY machineFilesChanged)
    Q_PROPERTY(bool machineFilesBusy READ machineFilesBusy NOTIFY machineFilesChanged)
    Q_PROPERTY(QString machineFilesError READ machineFilesError NOTIFY machineFilesChanged)
    Q_PROPERTY(QVariantList machineSnapshots READ machineSnapshots
               NOTIFY machineSnapshotsChanged)
    Q_PROPERTY(QString machineSnapshotsError READ machineSnapshotsError
               NOTIFY machineSnapshotsChanged)
    Q_PROPERTY(bool machineCreateActive READ machineCreateActive NOTIFY machineCreateStateChanged)
    Q_PROPERTY(QString machineJobKind READ machineJobKind NOTIFY machineCreateStateChanged)
    Q_PROPERTY(QString machineCreateJobId READ machineCreateJobId NOTIFY machineCreateStateChanged)
    Q_PROPERTY(QString machineCreateStage READ machineCreateStage NOTIFY machineCreateStateChanged)
    Q_PROPERTY(int machineCreateProgress READ machineCreateProgress
               NOTIFY machineCreateStateChanged)
    Q_PROPERTY(QString machineCreateError READ machineCreateError
               NOTIFY machineCreateStateChanged)
    Q_PROPERTY(QString eventMessage READ eventMessage NOTIFY eventsChanged)
    Q_PROPERTY(QString eventKind READ eventKind NOTIFY eventsChanged)
    Q_PROPERTY(bool setupCompleted READ setupCompleted NOTIFY setupChanged)
    Q_PROPERTY(bool setupPending READ setupPending NOTIFY setupChanged)
    Q_PROPERTY(QString setupPodmanAction READ setupPodmanAction NOTIFY setupChanged)
    Q_PROPERTY(bool setupMandatoryOk READ setupMandatoryOk NOTIFY setupChanged)
    Q_PROPERTY(QVariantList setupChecks READ setupChecks NOTIFY setupChanged)
    Q_PROPERTY(QVariantMap setupSummary READ setupSummary NOTIFY setupChanged)
    Q_PROPERTY(QVariantMap setupDockerImpact READ setupDockerImpact NOTIFY setupChanged)
    Q_PROPERTY(QVariantMap updateStatus READ updateStatus NOTIFY updateStatusChanged)
    Q_PROPERTY(QVariantList systemLogs READ systemLogs NOTIFY systemLogsChanged)
    Q_PROPERTY(QString systemLogsError READ systemLogsError NOTIFY systemLogsChanged)
    Q_PROPERTY(bool systemLogsTruncated READ systemLogsTruncated NOTIFY systemLogsChanged)
    Q_PROPERTY(QVariantMap diskHealth READ diskHealth NOTIFY diskHealthChanged)
    Q_PROPERTY(QString setupDockerPolicy READ setupDockerPolicy NOTIFY setupChanged)
    Q_PROPERTY(QString setupJobId READ setupJobId NOTIFY setupJobChanged)
    Q_PROPERTY(bool setupJobActive READ setupJobActive NOTIFY setupJobChanged)
    Q_PROPERTY(int setupProgress READ setupProgress NOTIFY setupJobChanged)
    Q_PROPERTY(QStringList setupLog READ setupLog NOTIFY setupJobChanged)
    Q_PROPERTY(QString setupStatus READ setupStatus NOTIFY setupJobChanged)
    Q_PROPERTY(QString setupError READ setupError NOTIFY setupChanged)
    Q_PROPERTY(bool setupBusy READ setupBusy NOTIFY setupChanged)
    Q_PROPERTY(bool setupLoaded READ setupLoaded NOTIFY setupChanged)
    Q_PROPERTY(QVariantList settingsSchema READ settingsSchema NOTIFY settingsSchemaChanged)
    Q_PROPERTY(QVariantMap settingsValues READ settingsValues NOTIFY settingsValuesChanged)
    Q_PROPERTY(QString settingsError READ settingsError NOTIFY settingsErrorChanged)
    Q_PROPERTY(
        QStringList pendingRestartKeys READ pendingRestartKeys NOTIFY pendingRestartKeysChanged)

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
    QVariantMap containerDetail() const { return m_containerDetail; }
    QString containerDetailError() const { return m_containerDetailError; }
    bool containerDetailLoading() const { return m_containerDetailLoading; }
    QVariantMap containerDetailStats() const { return m_containerDetailStats; }
    QVariantList containerDetailCpuHistory() const { return m_containerDetailCpuHistory; }
    QVariantList containerDetailMemHistory() const { return m_containerDetailMemHistory; }
    QVariantList containerDetailNetRxHistory() const { return m_containerDetailNetRxHistory; }
    QVariantList containerDetailNetTxHistory() const { return m_containerDetailNetTxHistory; }
    QVariantList containerDetailDiskReadHistory() const
    {
        return m_containerDetailDiskReadHistory;
    }
    QVariantList containerDetailDiskWriteHistory() const
    {
        return m_containerDetailDiskWriteHistory;
    }
    QString containerDetailStatsError() const { return m_containerDetailStatsError; }
    QStringList containerDetailLogLines() const { return m_containerDetailLogLines; }
    QString containerDetailLogsError() const { return m_containerDetailLogsError; }
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
    QString k8sState() const { return m_k8sState; }
    bool k8sEnabled() const { return m_k8sEnabled; }
    QVariantList k8sPods() const { return m_k8sPods; }
    QVariantList k8sServices() const { return m_k8sServices; }
    QString k8sError() const { return m_k8sError; }
    bool k8sLoading() const { return m_k8sLoading; }
    QStringList k8sLogLines() const { return m_k8sLogLines; }
    QString k8sLogError() const { return m_k8sLogError; }
    bool k8sLogActive() const { return m_k8sLogActive; }
    bool k8sPortForwardActive() const { return m_k8sPortForwardActive; }
    QString k8sPortForwardId() const { return m_k8sPortForwardId; }
    QString k8sPortForwardError() const { return m_k8sPortForwardError; }
    bool k8sJobActive() const { return m_k8sJobActive; }
    QString k8sJobStage() const { return m_k8sJobStage; }
    int k8sJobProgress() const { return m_k8sJobProgress; }
    QString k8sJobError() const { return m_k8sJobError; }
    bool pullActive() const { return m_pullActive; }
    QString pullStatus() const { return m_pullStatus; }
    QVariantMap diskUsage() const { return m_diskUsage; }
    QVariantMap prunePreview() const { return m_prunePreview; }
    QVariantList composeProjects() const { return m_composeProjects; }
    QStringList composeLogLines() const { return m_composeLogLines; }
    QVariantList builds() const { return m_builds; }
    QStringList buildLog() const { return m_buildLog; }
    bool buildActive() const { return m_buildActive; }
    QString buildStatus() const { return m_buildStatus; }
    QString activeBuildId() const { return m_activeBuildId; }
    QVariantList machines() const { return m_machines; }
    QVariantList machineDistros() const { return m_machineDistros; }
    QVariantMap machineInfo() const { return m_machineInfo; }
    QVariantList machineSerialPorts() const { return m_machineSerialPorts; }
    QVariantList machineUsbDevices() const { return m_machineUsbDevices; }
    QStringList machineLogLines() const { return m_machineLogLines; }
    QString machineLogsCapturedAt() const { return m_machineLogsCapturedAt; }
    QVariantMap machineStats() const { return m_machineStats; }
    QVariantList machineCpuHistory() const { return m_machineCpuHistory; }
    QVariantList machineMemHistory() const { return m_machineMemHistory; }
    QVariantList machineNetRxHistory() const { return m_machineNetRxHistory; }
    QVariantList machineNetTxHistory() const { return m_machineNetTxHistory; }
    QString machineStatsError() const { return m_machineStatsError; }
    bool machineConsoleActive() const { return m_machineConsoleActive; }
    QString machineConsoleText() const { return m_machineConsoleText; }
    QString machineConsoleError() const { return m_machineConsoleError; }
    QVariantList machineFiles() const { return m_machineFiles; }
    QString machineFilesPath() const { return m_machineFilesPath; }
    bool machineFilesBusy() const { return m_machineFilesBusy; }
    QString machineFilesError() const { return m_machineFilesError; }
    QVariantList machineSnapshots() const { return m_machineSnapshots; }
    QString machineSnapshotsError() const { return m_machineSnapshotsError; }
    bool machineCreateActive() const { return m_machineCreateActive; }
    QString machineJobKind() const { return m_machineJobKind; }
    QString machineCreateJobId() const { return m_machineCreateJobId; }
    QString machineCreateStage() const { return m_machineCreateStage; }
    int machineCreateProgress() const { return m_machineCreateProgress; }
    QString machineCreateError() const { return m_machineCreateError; }
    QString eventMessage() const { return m_eventMessage; }
    QString eventKind() const { return m_eventKind; }
    bool setupCompleted() const { return m_setupCompleted; }
    bool setupPending() const { return !m_setupCompleted && m_setupShowOnboarding; }
    QString setupPodmanAction() const;
    bool setupMandatoryOk() const;
    QVariantList setupChecks() const { return m_setupChecks; }
    QVariantMap setupSummary() const { return m_setupSummary; }
    QVariantMap setupDockerImpact() const { return m_setupDockerImpact; }
    QVariantMap updateStatus() const { return m_updateStatus; }
    QVariantList systemLogs() const { return m_systemLogs; }
    QString systemLogsError() const { return m_systemLogsError; }
    bool systemLogsTruncated() const { return m_systemLogsTruncated; }
    QVariantMap diskHealth() const { return m_diskHealth; }
    QString setupDockerPolicy() const { return m_setupDockerPolicy; }
    QString setupJobId() const { return m_setupJobId; }
    bool setupJobActive() const { return m_setupJobActive; }
    int setupProgress() const { return m_setupProgress; }
    QStringList setupLog() const { return m_setupLog; }
    QString setupStatus() const { return m_setupStatus; }
    QString setupError() const { return m_setupError; }
    bool setupBusy() const { return m_setupBusy; }
    QString settingsError() const { return m_settingsError; }
    QVariantList settingsSchema() const { return m_settingsSchema; }
    QVariantMap settingsValues() const { return m_settingsValues; }
    QStringList pendingRestartKeys() const { return m_pendingRestartKeys; }
    bool setupLoaded() const { return m_setupLoaded; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshCatalog();
    Q_INVOKABLE void refreshDiskUsage();
    Q_INVOKABLE void refreshImages();
    Q_INVOKABLE void refreshVolumes();
    Q_INVOKABLE void refreshNetworks();
    Q_INVOKABLE void refreshPods();
    Q_INVOKABLE void refreshK8s();
    Q_INVOKABLE void refreshK8sStatus();
    Q_INVOKABLE void startK8sWatch();
    Q_INVOKABLE void stopK8sWatch();
    Q_INVOKABLE void startK8sLogs(const QString &namespaceName, const QString &pod,
                                  const QString &container = QString());
    Q_INVOKABLE void stopK8sLogs();
    Q_INVOKABLE void clearK8sLogs();
    Q_INVOKABLE void deleteK8sPod(const QString &namespaceName, const QString &name);
    Q_INVOKABLE void deleteK8sService(const QString &namespaceName, const QString &name);
    Q_INVOKABLE void startK8sPortForward(const QString &namespaceName, const QString &name,
                                         int localPort, int remotePort,
                                         const QString &resource = QStringLiteral("pod"));
    Q_INVOKABLE void stopK8sPortForward();
    Q_INVOKABLE void enableK8s();
    Q_INVOKABLE void disableK8s(bool force = false);
    Q_INVOKABLE void resetK8s();
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
    Q_INVOKABLE QString choosePath(const QString &mode, const QString &title,
                                   const QString &filter = QString(),
                                   const QString &initialPath = QString());
    Q_INVOKABLE void refreshBuilds();
    Q_INVOKABLE void startBuild(const QVariantMap &spec);
    Q_INVOKABLE void cancelBuild();
    Q_INVOKABLE void removeBuild(const QString &id);
    Q_INVOKABLE void refreshMachines();
    Q_INVOKABLE void refreshMachineDistros();
    Q_INVOKABLE void loadMachineInfo(const QString &id);
    Q_INVOKABLE void refreshMachineSerialPorts(const QString &id);
    Q_INVOKABLE void attachMachineSerialPort(const QString &id, const QString &path);
    Q_INVOKABLE void detachMachineSerialPort(const QString &id, const QString &path);
    Q_INVOKABLE void refreshMachineUsbDevices(const QString &id);
    Q_INVOKABLE void attachMachineUsbDevice(const QString &id, int bus, int address);
    Q_INVOKABLE void detachMachineUsbDevice(const QString &id, int bus, int address);
    Q_INVOKABLE void setMachineAudio(const QString &id, bool enabled);
    Q_INVOKABLE void loadMachineLogs(const QString &id, int tail = 200);
    Q_INVOKABLE void startMachineStats(const QString &id);
    Q_INVOKABLE void stopMachineStats();
    Q_INVOKABLE void followMachineConsole(const QString &id, int tailBytes = 64 * 1024);
    Q_INVOKABLE void stopMachineConsole();
    Q_INVOKABLE void refreshMachineFiles(const QString &id, const QString &path);
    Q_INVOKABLE void refreshMachineSnapshots(const QString &id);
    Q_INVOKABLE void createMachineSnapshot(const QString &id, const QString &name);
    Q_INVOKABLE void restoreMachineSnapshot(const QString &id, const QString &name);
    Q_INVOKABLE void removeMachineSnapshot(const QString &id, const QString &name);
    Q_INVOKABLE void downloadMachineFile(const QString &id, const QString &path,
                                         const QString &fileName);
    Q_INVOKABLE void uploadMachineFile(const QString &id, const QString &remoteDir,
                                       const QString &localPath);
    Q_INVOKABLE void copyMachineLogs();
    Q_INVOKABLE void openMachineSsh(const QString &id);
    Q_INVOKABLE void machineLifecycle(const QString &action, const QString &id,
                                      bool force = false);
    Q_INVOKABLE void removeMachine(const QString &id, bool force);
    Q_INVOKABLE void renameMachine(const QString &id, const QString &name);
    Q_INVOKABLE void cloneMachine(const QString &id, const QString &name);
    Q_INVOKABLE void updateMachine(const QVariantMap &spec);
    Q_INVOKABLE void setMachinePorts(const QString &id, const QVariantList &ports);
    Q_INVOKABLE void createMachine(const QVariantMap &spec);
    Q_INVOKABLE void cancelMachineCreate();
    Q_INVOKABLE void resetMachineCreateState();
    Q_INVOKABLE void exportMachine(const QString &id, const QString &path, bool force = false);
    Q_INVOKABLE void importMachine(const QString &path, const QString &name = QString(),
                                   bool force = false);
    Q_INVOKABLE QString machineArchivePath(const QString &id) const;
    Q_INVOKABLE void lifecycle(const QString &action, const QString &id);
    Q_INVOKABLE void removeContainer(const QString &id, bool force, bool volumes);
    Q_INVOKABLE void loadContainerDetail(const QString &id);
    Q_INVOKABLE void copyContainerInspect();
    Q_INVOKABLE void startContainerDetailStats(const QString &id);
    Q_INVOKABLE void stopContainerDetailStats();
    Q_INVOKABLE void startContainerDetailLogs(const QString &id, int tail = 200,
                                              bool follow = true);
    Q_INVOKABLE void stopContainerDetailLogs();
    Q_INVOKABLE void clearContainerDetailLogs();
    Q_INVOKABLE void containerDetailLifecycle(const QString &action, const QString &id);
    Q_INVOKABLE void removeContainerDetail(const QString &id, bool force);
    Q_INVOKABLE void createContainer(const QVariantMap &spec, bool start);
    Q_INVOKABLE void exposeContainerLan(const QString &id, bool expose);
    Q_INVOKABLE void batchAll(const QString &action);
    Q_INVOKABLE void batchIds(const QStringList &ids, const QString &action);
    Q_INVOKABLE void batchImages(const QStringList &ids, bool force = false);
    Q_INVOKABLE void batchVolumes(const QStringList &names, bool force = false);
    Q_INVOKABLE void batchNetworks(const QStringList &names, bool force = false);
    Q_INVOKABLE void batchPods(const QStringList &ids, const QString &action, bool force = false);
    Q_INVOKABLE void batchMachines(const QStringList &ids, const QString &action,
                                   bool force = false);
    Q_INVOKABLE void batchCompose(const QStringList &projectPaths, const QString &action);
    Q_INVOKABLE void batchBuilds(const QStringList &ids);
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
    Q_INVOKABLE void refreshSettings();
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE void resetSetting(const QString &key);
    Q_INVOKABLE void saveEngineMode(const QString &mode);
    Q_INVOKABLE void saveTheme(const QString &theme);
    Q_INVOKABLE void saveTraySettings(bool keepRunning, bool showInTray, bool startAtLogin,
                                      bool notifications = true);
    Q_INVOKABLE void saveProxySettings(const QString &proxy, const QString &excludeText);
    Q_INVOKABLE void openTerminal(const QString &id);
    Q_INVOKABLE void setupRefresh();
    Q_INVOKABLE void setupCheck();
    Q_INVOKABLE void setupValidate();
    Q_INVOKABLE void setupApply(const QString &action, const QVariantList &components = {});
    Q_INVOKABLE void setupLoadDockerImpact();
    Q_INVOKABLE void setupRemoveDocker();
    Q_INVOKABLE void checkForUpdates(bool interactive = false);
    Q_INVOKABLE void installUpdate();
    Q_INVOKABLE void loadSystemLogs(const QString &level = QString(),
                                    const QString &component = QString(), int sinceDays = 7);
    Q_INVOKABLE bool exportSystemLogs(const QString &path);
    Q_INVOKABLE void clearSystemLogs();
    Q_INVOKABLE void openLogsFolder();
    Q_INVOKABLE void checkDiskHealth();
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
    void containerDetailChanged();
    void containerDetailStatsChanged();
    void containerDetailLogsChanged();
    void activityChanged();
    void filesChanged();
    void imagesChanged();
    void volumesChanged();
    void networksChanged();
    void podsChanged();
    void k8sChanged();
    void k8sLogsChanged();
    void k8sForwardChanged();
    void k8sJobChanged();
    void pullStateChanged();
    void diskUsageChanged();
    void prunePreviewChanged();
    void composeProjectsChanged();
    void composeLogsChanged();
    void buildsChanged();
    void buildLogChanged();
    void buildStateChanged();
    void machinesChanged();
    void machineDistrosChanged();
    void machineInfoChanged();
    void machineSerialPortsChanged();
    void machineUsbDevicesChanged();
    void machineLogsChanged();
    void machineStatsChanged();
    void machineConsoleChanged();
    void machineFilesChanged();
    void machineSnapshotsChanged();
    void machineCreateStateChanged();
    void eventsChanged();
    void updateStatusChanged();
    void systemLogsChanged();
    void updateAvailable(const QVariantMap &release);
    void updateCheckFailed(const QString &reason);
    void diskHealthChanged();
    void diskLow(const QVariantMap &sample);
    void containerEvent(const QVariantMap &event);
    void terminalRequested(const QString &title, const QString &program,
                           const QStringList &args);
    void settingsChanged();
    void settingsSchemaChanged();
    void settingsValuesChanged();
    void settingsErrorChanged();
    void pendingRestartKeysChanged();
    void settingFinished(const QString &key, bool ok, const QString &error);
    void setupChanged();
    void setupJobChanged();

private:
    void onRefreshFinished();
    void onActionFinished();
    void onContainerDetailFinished();
    void onFilesFinished();
    void onCatalogFinished();
    void onSetupFinished();
    void onUpdateFinished();
    void loadUpdateState();
    void onDiskFinished();
    void onSettingsFinished();
    void onSettingsActionFinished();
    void setSettingInternal(const QString &key, const QVariant &value,
                            bool applyAutostart = false);
    void enqueueSettingsRequest(const QString &method, const QJsonObject &params,
                                bool applyAutostart = false);
    void startNextSettingsRequest();
    void setSettingsError(const QString &error);
    void applySettingsValues(const QVariantMap &values);
    void onMachinesFinished();
    void onMachineDistrosFinished();
    void onMachineInfoFinished();
    void onMachineSerialFinished();
    void onMachineUsbFinished();
    void onMachineLogsFinished();
    void onMachineFilesFinished();
    void onMachineSnapshotsFinished();
    void onK8sStatusFinished();
    void onK8sPodsFinished();
    void onK8sServicesFinished();
    void onK8sForwardFinished();
    void startMachineFilesRequest(const QString &operation, const QString &id,
                                  const QString &method, const QJsonObject &params,
                                  int timeoutMs);
    void applySetupStatus(const QJsonObject &status);
    void applySetupJob(const QJsonObject &job);
    void runSetupRequest(const QString &method, const QJsonObject &params);
    void refreshSection(const QString &section);
    void startNextCatalog();
    void runAction(const QString &method, const QJsonObject &params,
                   int timeoutMs = 30000);
    void batchResource(const QString &prefix, const QString &idKey, const QString &action,
                       const QStringList &ids, bool force, const QJsonObject &extra, int timeoutMs);

    struct PendingAction {
        QString method;
        QJsonObject params;
        int timeoutMs = 30000;
    };
    void startAction(const PendingAction &action);
    void enqueueAction(const PendingAction &action);

    QFutureWatcher<QJsonObject> m_watcher;
    QFutureWatcher<QJsonObject> m_actionWatcher;
    QFutureWatcher<QJsonObject> m_containerDetailWatcher;
    QFutureWatcher<QJsonObject> m_filesWatcher;
    QFutureWatcher<QJsonObject> m_catalogWatcher;
    QFutureWatcher<QJsonObject> m_setupWatcher;
    QFutureWatcher<QJsonObject> m_updateWatcher;
    QFutureWatcher<QJsonObject> m_diskWatcher;
    QFutureWatcher<QJsonObject> m_machinesWatcher;
    QFutureWatcher<QJsonObject> m_machineDistrosWatcher;
    QFutureWatcher<QJsonObject> m_machineInfoWatcher;
    QFutureWatcher<QJsonObject> m_machineSerialWatcher;
    QFutureWatcher<QJsonObject> m_machineUsbWatcher;
    QFutureWatcher<QJsonObject> m_machineLogsWatcher;
    QFutureWatcher<QJsonObject> m_machineFilesWatcher;
    QFutureWatcher<QJsonObject> m_machineSnapshotsWatcher;
    QFutureWatcher<QJsonObject> m_machineSshWatcher;
    QFutureWatcher<QJsonObject> m_k8sStatusWatcher;
    QFutureWatcher<QJsonObject> m_k8sPodsWatcher;
    QFutureWatcher<QJsonObject> m_k8sServicesWatcher;
    QFutureWatcher<QJsonObject> m_k8sForwardWatcher;
    QFutureWatcher<QJsonObject> m_settingsWatcher;
    QFutureWatcher<QJsonObject> m_settingsActionWatcher;
    struct SettingsRequest {
        QString method;
        QJsonObject params;
        bool applyAutostart = false;
    };
    QQueue<SettingsRequest> m_settingsQueue;
    SettingsRequest m_settingsCurrentRequest;
    bool m_settingsActionActive = false;
    bool m_settingsRefreshAgain = false;
    QString m_setupPendingMethod;
    QString m_catalogSection;
    QStringList m_catalogQueue;
    StreamClient m_stream;
    StreamClient m_detailStatsStream;
    StreamClient m_detailLogsStream;
    StreamClient m_activityStream;
    StreamClient m_pullStream;
    StreamClient m_composeStream;
    StreamClient m_buildStream;
    StreamClient m_machineCreateStream;
    StreamClient m_machineStatsStream;
    StreamClient m_machineConsoleStream;
    StreamClient m_setupStream;
    StreamClient m_eventsStream;
    StreamClient m_k8sLogsStream;
    StreamClient m_k8sJobStream;
    QTimer m_eventTimer;
    QTimer m_logsTimer;
    QTimer m_detailLogsTimer;
    QTimer m_refreshTimer;
    QTimer m_eventsRetryTimer;
    QTimer m_updateTimer;
    QTimer m_diskTimer;
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
    QVariantMap m_containerDetail;
    QJsonObject m_containerDetailJson;
    QString m_containerDetailError;
    bool m_containerDetailLoading = false;
    QString m_pendingContainerDetailId;
    QVariantMap m_containerDetailStats;
    QVariantList m_containerDetailCpuHistory;
    QVariantList m_containerDetailMemHistory;
    QVariantList m_containerDetailNetRxHistory;
    QVariantList m_containerDetailNetTxHistory;
    QVariantList m_containerDetailDiskReadHistory;
    QVariantList m_containerDetailDiskWriteHistory;
    QString m_containerDetailStatsError;
    QStringList m_containerDetailLogLines;
    QString m_containerDetailLogsError;
    bool m_detailLogsPending = false;
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
    QString m_k8sState;
    bool m_k8sEnabled = false;
    QVariantList m_k8sPods;
    QVariantList m_k8sServices;
    QString m_k8sError;
    bool m_k8sLoading = false;
    QStringList m_k8sLogLines;
    QString m_k8sLogError;
    bool m_k8sLogActive = false;
    bool m_k8sPortForwardActive = false;
    QString m_k8sPortForwardId;
    QString m_k8sPortForwardError;
    bool m_k8sJobActive = false;
    QString m_k8sJobStage;
    int m_k8sJobProgress = 0;
    QString m_k8sJobError;
    QTimer m_k8sTimer;
    bool m_pullActive = false;
    QString m_pullStatus;
    QVariantMap m_diskUsage;
    QVariantMap m_prunePreview;
    QVariantList m_composeProjects;
    QStringList m_composeLogLines;
    QVariantList m_builds;
    QStringList m_buildLog;
    bool m_buildActive = false;
    QString m_buildStatus;
    QString m_activeBuildId;
    QVariantList m_machines;
    QVariantList m_machineDistros;
    QVariantMap m_machineInfo;
    QVariantList m_machineSerialPorts;
    QVariantList m_machineUsbDevices;
    QStringList m_machineLogLines;
    QString m_machineLogsCapturedAt;
    QVariantMap m_machineStats;
    QVariantList m_machineCpuHistory;
    QVariantList m_machineMemHistory;
    QVariantList m_machineNetRxHistory;
    QVariantList m_machineNetTxHistory;
    QString m_machineStatsError;
    bool m_machineConsoleActive = false;
    QString m_machineConsoleText;
    QString m_machineConsoleError;
    QVariantList m_machineFiles;
    QString m_machineFilesPath = QStringLiteral("/");
    bool m_machineFilesBusy = false;
    QString m_machineFilesError;
    QVariantList m_machineSnapshots;
    QString m_machineSnapshotsError;
    QString m_pendingMachineSnapshotsId;
    QString m_machineFilesMachineId;
    QString m_machineFilesOperation;
    QString m_pendingMachineInfoId;
    QString m_pendingMachineLogsId;
    int m_pendingMachineLogsTail = 200;
    QString m_machineSshTitle;
    bool m_machineDistrosLoaded = false;
    bool m_machinesRefreshAgain = false;
    bool m_machineCreateActive = false;
    QString m_machineJobKind;
    QString m_machineCreateJobId;
    QString m_machineCreateStage;
    int m_machineCreateProgress = 0;
    QString m_machineCreateError;
    QString m_activeActionMethod;
    QJsonObject m_activeActionParams;
    QString m_eventMessage;
    QString m_eventKind;
    QSet<QString> m_recentEvents;
    bool m_setupCompleted = false;
    bool m_setupShowOnboarding = true;
    QVariantList m_setupChecks;
    QVariantMap m_setupSummary;
    QVariantMap m_setupDockerImpact;
    QVariantMap m_updateStatus;
    QVariantList m_systemLogs;
    QString m_systemLogsError;
    bool m_systemLogsTruncated = false;
    QVector<PodStack::LogLine> m_lastLogs;
    bool m_updateCheckRunning = false;
    bool m_updatesEnabled = true;
    bool m_updateInteractive = false;
    QVariantMap m_diskHealth;
    bool m_diskCheckRunning = false;
    QString m_setupDockerPolicy = QStringLiteral("keep");
    QString m_setupJobId;
    bool m_setupJobActive = false;
    int m_setupProgress = 0;
    QStringList m_setupLog;
    QString m_setupStatus;
    QString m_setupError;
    bool m_setupBusy = false;
    bool m_setupLoaded = false;
    QVariantList m_settingsSchema;
    QVariantMap m_settingsValues;
    QString m_settingsError;
    QStringList m_pendingRestartKeys;
    int m_logBufferLines = 10000;
};

} // namespace PodStack
