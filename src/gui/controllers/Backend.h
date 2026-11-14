#pragma once

#include "core/logging/LogReader.h"
#include "gui/controllers/BuildController.h"
#include "gui/controllers/ContainerController.h"
#include "gui/controllers/ContainerDetailController.h"
#include "gui/controllers/K8sController.h"
#include "gui/controllers/SettingsController.h"
#include "gui/controllers/SetupController.h"
#include "gui/controllers/SystemController.h"
#include "gui/controllers/UpdateController.h"
#include "gui/controllers/ComposeController.h"
#include "gui/controllers/ImageController.h"
#include "gui/controllers/NetworkController.h"
#include "gui/controllers/PodController.h"
#include "gui/controllers/VolumeController.h"
#include "core/rpc/StreamClient.h"

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
    Q_PROPERTY(PodStack::K8sController *k8s READ k8s CONSTANT)
    Q_PROPERTY(PodStack::SettingsController *settings READ settings CONSTANT)
    Q_PROPERTY(PodStack::SetupController *setup READ setup CONSTANT)
    Q_PROPERTY(PodStack::SystemController *system READ system CONSTANT)
    Q_PROPERTY(PodStack::UpdateController *updates READ updates CONSTANT)
    Q_PROPERTY(PodStack::ContainerController *containers READ containers CONSTANT)
    Q_PROPERTY(PodStack::ContainerDetailController *containerDetail READ containerDetail CONSTANT)
    Q_PROPERTY(PodStack::ImageController *images READ images CONSTANT)
    Q_PROPERTY(PodStack::ComposeController *compose READ compose CONSTANT)
    Q_PROPERTY(PodStack::BuildController *builds READ builds CONSTANT)
    Q_PROPERTY(PodStack::VolumeController *volumes READ volumes CONSTANT)
    Q_PROPERTY(PodStack::NetworkController *networks READ networks CONSTANT)
    Q_PROPERTY(PodStack::PodController *pods READ pods CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool actionActive READ actionActive NOTIFY actionStateChanged)
    Q_PROPERTY(QVariantMap diskUsage READ diskUsage NOTIFY diskUsageChanged)
    Q_PROPERTY(QVariantMap prunePreview READ prunePreview NOTIFY prunePreviewChanged)
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

public:
    explicit Backend(QObject *parent = nullptr);

    K8sController *k8s() { return &m_k8s; }
    SettingsController *settings() { return &m_settings; }
    SetupController *setup() { return &m_setup; }
    SystemController *system() { return &m_system; }
    UpdateController *updates() { return &m_updates; }
    ContainerController *containers() { return &m_containers; }
    ContainerDetailController *containerDetail() { return &m_containerDetail; }
    ImageController *images() { return &m_images; }
    ComposeController *compose() { return &m_compose; }
    BuildController *builds() { return &m_builds; }
    VolumeController *volumes() { return &m_volumes; }
    NetworkController *networks() { return &m_networks; }
    PodController *pods() { return &m_pods; }
    bool connected() const { return m_connected; }
    QString error() const { return m_error; }
    QString lastError() const { return m_lastError; }
    QString version() const;
    bool actionActive() const { return m_actionActive; }
    QVariantMap diskUsage() const { return m_diskUsage; }
    QVariantMap prunePreview() const { return m_prunePreview; }
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

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshCatalog();
    Q_INVOKABLE void refreshDiskUsage();
    Q_INVOKABLE void openPath(const QString &path);
    Q_INVOKABLE QString choosePath(const QString &mode, const QString &title,
                                   const QString &filter = QString(),
                                   const QString &initialPath = QString());
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
    Q_INVOKABLE void batchMachines(const QStringList &ids, const QString &action,
                                   bool force = false);

signals:
    void actionStateChanged();
    void stateChanged();
    void lastErrorChanged();
    void actionFinished(bool ok, const QString &error);
    void diskUsageChanged();
    void prunePreviewChanged();
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
    void containerEvent(const QVariantMap &event);
    void terminalRequested(const QString &title, const QString &program,
                           const QStringList &args);

private:
    void onRefreshFinished();
    void onActionFinished();
    void onContainerDetailFinished();
    void onFilesFinished();
    void onCatalogFinished();
    void onSetupFinished();
    void onUpdateFinished();
    void loadUpdateState();
    void startUpdateRunner(bool resume);
    void onSystemLogsFinished();
    void onDiskFinished();
    void onMachinesFinished();
    void onMachineDistrosFinished();
    void onMachineInfoFinished();
    void onMachineSerialFinished();
    void onMachineUsbFinished();
    void onMachineLogsFinished();
    void onMachineFilesFinished();
    void onMachineSnapshotsFinished();
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

    QFutureWatcher<QJsonObject> m_actionWatcher;
    QFutureWatcher<QJsonObject> m_catalogWatcher;
    QFutureWatcher<QJsonObject> m_machinesWatcher;
    QFutureWatcher<QJsonObject> m_machineDistrosWatcher;
    QFutureWatcher<QJsonObject> m_machineInfoWatcher;
    QFutureWatcher<QJsonObject> m_machineSerialWatcher;
    QFutureWatcher<QJsonObject> m_machineUsbWatcher;
    QFutureWatcher<QJsonObject> m_machineLogsWatcher;
    QFutureWatcher<QJsonObject> m_machineFilesWatcher;
    QFutureWatcher<QJsonObject> m_machineSnapshotsWatcher;
    QFutureWatcher<QJsonObject> m_machineSshWatcher;
    QString m_catalogSection;
    QStringList m_catalogQueue;
    StreamClient m_machineCreateStream;
    StreamClient m_machineStatsStream;
    StreamClient m_machineConsoleStream;
    StreamClient m_eventsStream;
    QTimer m_eventTimer;
    QTimer m_refreshTimer;
    QTimer m_eventsRetryTimer;
    int m_eventsRetryDelayMs = 1000;
    QList<PendingAction> m_actionQueue;
    bool m_actionActive = false;
    bool m_connected = false;
    QString m_error;
    QString m_lastError;
    QStringList m_logLines;
    K8sController m_k8s;
    SettingsController m_settings;
    SetupController m_setup;
    SystemController m_system;
    UpdateController m_updates;
    ContainerController m_containers;
    ContainerDetailController m_containerDetail{&m_containers};
    ImageController m_images;
    ComposeController m_compose;
    BuildController m_builds;
    VolumeController m_volumes;
    NetworkController m_networks;
    PodController m_pods;
    QVariantMap m_diskUsage;
    QVariantMap m_prunePreview;
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
};

} // namespace PodStack
