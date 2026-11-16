#pragma once

#include "core/logging/LogReader.h"
#include "core/rpc/StreamClient.h"
#include "gui/controllers/BuildController.h"
#include "gui/controllers/ComposeController.h"
#include "gui/controllers/ContainerController.h"
#include "gui/controllers/ContainerDetailController.h"
#include "gui/controllers/ImageController.h"
#include "gui/controllers/K8sController.h"
#include "gui/controllers/MachineController.h"
#include "gui/controllers/MachineDetailController.h"
#include "gui/controllers/NetworkController.h"
#include "gui/controllers/PodController.h"
#include "gui/controllers/SettingsController.h"
#include "gui/controllers/SetupController.h"
#include "gui/controllers/SystemController.h"
#include "gui/controllers/UpdateController.h"
#include "gui/controllers/VolumeController.h"

#include "gui/app/Feedback.h"

#include <QFutureWatcher>
#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace PodStack {

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(PodStack::K8sController *k8s READ k8s CONSTANT)
    Q_PROPERTY(PodStack::MachineController *machines READ machines CONSTANT)
    Q_PROPERTY(PodStack::MachineDetailController *machineDetail READ machineDetail CONSTANT)
    Q_PROPERTY(PodStack::SettingsController *settings READ settings CONSTANT)
    Q_PROPERTY(PodStack::SetupController *setup READ setup CONSTANT)
    Q_PROPERTY(PodStack::SystemController *system READ system CONSTANT)
    Q_PROPERTY(PodStack::UpdateController *updates READ updates CONSTANT)
    Q_PROPERTY(PodStack::Feedback *feedback READ feedback CONSTANT)
    Q_PROPERTY(PodStack::ContainerController *containers READ containers CONSTANT)
    Q_PROPERTY(PodStack::ContainerDetailController *containerDetail READ containerDetail CONSTANT)
    Q_PROPERTY(PodStack::ImageController *images READ images CONSTANT)
    Q_PROPERTY(PodStack::ComposeController *compose READ compose CONSTANT)
    Q_PROPERTY(PodStack::BuildController *builds READ builds CONSTANT)
    Q_PROPERTY(PodStack::VolumeController *volumes READ volumes CONSTANT)
    Q_PROPERTY(PodStack::NetworkController *networks READ networks CONSTANT)
    Q_PROPERTY(PodStack::PodController *pods READ pods CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool daemonReachable READ daemonReachable NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QVariantList containerItems READ containerItems NOTIFY containersChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool actionActive READ actionActive NOTIFY actionStateChanged)
    Q_PROPERTY(QVariantMap diskUsage READ diskUsage NOTIFY diskUsageChanged)
    Q_PROPERTY(QVariantMap prunePreview READ prunePreview NOTIFY prunePreviewChanged)
    Q_PROPERTY(QString eventMessage READ eventMessage NOTIFY eventsChanged)
    Q_PROPERTY(QString eventKind READ eventKind NOTIFY eventsChanged)
    Q_PROPERTY(bool serviceActivating READ serviceActivating NOTIFY serviceActivationChanged)
    Q_PROPERTY(QString daemonVersion READ daemonVersion NOTIFY daemonInfoChanged)
    Q_PROPERTY(QString daemonMismatch READ daemonMismatch NOTIFY daemonInfoChanged)

public:
    explicit Backend(QObject *parent = nullptr);

    K8sController *k8s() { return &m_k8s; }
    MachineController *machines() { return &m_machines; }
    MachineDetailController *machineDetail() { return &m_machineDetail; }
    SettingsController *settings() { return &m_settings; }
    SetupController *setup() { return &m_setup; }
    SystemController *system() { return &m_system; }
    UpdateController *updates() { return &m_updates; }
    Feedback *feedback() { return &m_feedback; }
    ContainerController *containers() { return &m_containers; }
    ContainerDetailController *containerDetail() { return &m_containerDetail; }
    ImageController *images() { return &m_images; }
    ComposeController *compose() { return &m_compose; }
    BuildController *builds() { return &m_builds; }
    VolumeController *volumes() { return &m_volumes; }
    NetworkController *networks() { return &m_networks; }
    PodController *pods() { return &m_pods; }
    bool connected() const { return m_connected; }
    bool daemonReachable() const { return m_daemonReachable; }
    QString error() const { return m_error; }
    QVariantList containerItems() const { return m_containers.items(); }
    QString lastError() const { return m_lastError; }
    QString version() const;
    bool actionActive() const { return m_actionActive; }
    QVariantMap diskUsage() const { return m_diskUsage; }
    QVariantMap prunePreview() const { return m_prunePreview; }
    QString eventMessage() const { return m_eventMessage; }
    QString eventKind() const { return m_eventKind; }
    bool serviceActivating() const { return m_serviceActivating; }
    QString daemonVersion() const { return m_daemonVersion; }
    QString daemonMismatch() const { return m_daemonMismatch; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshCatalog();
    Q_INVOKABLE void refreshDiskUsage();
    /// Reinicia o podstack.service em background (botão "Reiniciar daemon").
    /// Um daemon travado aceita conexões mas não responde; `start` não resolve.
    Q_INVOKABLE void restartDaemonService();
    /// Ativa o podman.socket rootless sem reiniciar o daemon (engine parado).
    Q_INVOKABLE void startEngine();
    Q_INVOKABLE void openPath(const QString &path);
    Q_INVOKABLE QString choosePath(const QString &mode, const QString &title,
                                   const QString &filter = QString(),
                                   const QString &initialPath = QString());

signals:
    void actionStateChanged();
    void stateChanged();
    /// Lista de containers para a página. Existia antes do refactor dos
    /// controllers e o `Main.qml` continuava escutando: sem o sinal, a página
    /// nunca recebia os itens e mostrava "0 containers".
    void containersChanged(const QVariantList &items);
    void lastErrorChanged();
    void serviceActivationChanged();
    void daemonInfoChanged();
    void actionFinished(bool ok, const QString &error);
    void diskUsageChanged();
    void prunePreviewChanged();
    void eventsChanged();
    void containerEvent(const QVariantMap &event);
    void containerCreated(const QString &id);
    void terminalRequested(const QString &title, const QString &program, const QStringList &args);

private:
    void onRefreshFinished();
    void onActionFinished();
    void onContainerDetailFinished();
    void onFilesFinished();
    void onCatalogFinished();
    /// Texto de confirmação para um método concluído com sucesso ("" = sem toast).
    static QString successText(const QString &method);
    void onSetupFinished();
    void onUpdateFinished();
    void loadUpdateState();
    void startUpdateRunner(bool resume);
    void onSystemLogsFinished();
    void onDiskFinished();
    void onServiceActivationFinished();
    void refreshDaemonInfo();
    void onHelloFinished();
    void applySetupStatus(const QJsonObject &status);
    void applySetupJob(const QJsonObject &job);
    void runSetupRequest(const QString &method, const QJsonObject &params);
    void refreshSection(const QString &section);
    void startNextCatalog();
    void runAction(const QString &method, const QJsonObject &params, int timeoutMs = 30000);
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
    QFutureWatcher<int> m_serviceWatcher;
    QFutureWatcher<QJsonObject> m_helloWatcher;
    bool m_serviceActivating = false;
    QString m_daemonVersion;
    QString m_daemonMismatch;
    QString m_catalogSection;
    QStringList m_catalogQueue;
    StreamClient m_eventsStream;
    QTimer m_eventTimer;
    QTimer m_refreshTimer;
    QTimer m_eventsRetryTimer;
    int m_eventsRetryDelayMs = 1000;
    QList<PendingAction> m_actionQueue;
    bool m_actionActive = false;
    bool m_connected = false;
    bool m_daemonReachable = true;
    QString m_error;
    QString m_lastError;
    QStringList m_logLines;
    QString m_lastDownloadDir;
    K8sController m_k8s;
    MachineController m_machines;
    MachineDetailController m_machineDetail;
    SettingsController m_settings;
    SetupController m_setup;
    SystemController m_system;
    UpdateController m_updates;
    Feedback m_feedback;
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
    QString m_pendingMachineSnapshotsId;
    QString m_pendingMachineInfoId;
    QString m_pendingMachineLogsId;
    int m_pendingMachineLogsTail = 200;
    QString m_activeActionMethod;
    QJsonObject m_activeActionParams;
    QString m_eventMessage;
    QString m_eventKind;
    QSet<QString> m_recentEvents;
};

} // namespace PodStack
