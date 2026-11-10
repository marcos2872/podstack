#pragma once

#include "core/Error.h"
#include "daemon/BuildStore.h"
#include "daemon/ComposeManager.h"
#include "daemon/HelperClient.h"
#include "daemon/SetupManager.h"
#include "k8s/K8sManager.h"
#include "machines/CloudImageCache.h"
#include "machines/MachineManager.h"
#include "machines/MachineStats.h"
#include "podman/PodmanClient.h"

#include <QHash>
#include <QJsonObject>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

namespace PodStack {

class Daemon {
public:
    using Notify = std::function<void(const QString &method, const QJsonObject &params)>;

    Daemon();

    QJsonObject handle(const QString &method, const QJsonObject &params, const Notify &notify,
                       Error *error);

private:
    struct Subscription {
        std::atomic_bool cancel{false};
    };

    QJsonObject systemHello() const;
    QJsonObject systemStatus();
    QJsonObject systemDoctor();
    QJsonObject containersList(const QJsonObject &params, Error *error);
    QJsonObject containerAction(const QString &method, const QJsonObject &params, Error *error);
    QJsonObject containerExec(const QJsonObject &params, Error *error);
    QJsonObject containerInspect(const QJsonObject &params, Error *error);
    QJsonObject containerCreate(const QJsonObject &params, Error *error);
    QJsonObject containerExposeLan(const QJsonObject &params, Error *error);
    QJsonObject containerDuplicate(const QJsonObject &params, Error *error);
    QJsonObject containerFilesList(const QJsonObject &params, Error *error);
    QJsonObject containerFilesDownload(const QJsonObject &params, Error *error);
    QJsonObject containerFilesUpload(const QJsonObject &params, Error *error);
    QJsonObject imagesList(const QJsonObject &params, Error *error);
    QJsonObject imageInspect(const QJsonObject &params, Error *error);
    QJsonObject imageHistory(const QJsonObject &params, Error *error);
    QJsonObject imageRemove(const QJsonObject &params, Error *error);
    QJsonObject imagePrune(const QJsonObject &params, Error *error);
    QJsonObject imageTag(const QJsonObject &params, Error *error);
    QJsonObject imageUntag(const QJsonObject &params, Error *error);
    QJsonObject imagePull(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject imagePush(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject imageExport(const QJsonObject &params, Error *error);
    QJsonObject imageLoad(const QJsonObject &params, Error *error);
    QJsonObject volumesList(Error *error);
    QJsonObject volumeCreate(const QJsonObject &params, Error *error);
    QJsonObject volumeRemove(const QJsonObject &params, Error *error);
    QJsonObject volumePrune(Error *error);
    QJsonObject volumeClone(const QJsonObject &params, Error *error);
    QJsonObject volumeExport(const QJsonObject &params, Error *error);
    QJsonObject volumeImport(const QJsonObject &params, Error *error);
    QJsonObject volumeImportHostPath(const QJsonObject &params, Error *error);
    QJsonObject networksList(Error *error);
    QJsonObject networkCreate(const QJsonObject &params, Error *error);
    QJsonObject networkRemove(const QJsonObject &params, Error *error);
    QJsonObject networkConnect(const QJsonObject &params, Error *error);
    QJsonObject networkDisconnect(const QJsonObject &params, Error *error);
    QJsonObject podsList(Error *error);
    QJsonObject podCreate(const QJsonObject &params, Error *error);
    QJsonObject podAction(const QString &method, const QJsonObject &params, Error *error);
    QJsonObject podRemove(const QJsonObject &params, Error *error);
    QJsonObject systemDf(const QJsonObject &params, Error *error);
    QJsonObject composeProjects(Error *error);
    QJsonObject composeAction(const QString &method, const QJsonObject &params, Error *error);
    QJsonObject subscribeComposeLogs(const QJsonObject &params, const Notify &notify,
                                     Error *error);
    QJsonObject buildStart(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject buildCancel(const QJsonObject &params, Error *error);
    QJsonObject buildsList(const QJsonObject &params, Error *error);
    QJsonObject buildGet(const QJsonObject &params, Error *error);
    QJsonObject buildRemove(const QJsonObject &params, Error *error);
    QJsonObject machinesDistros();
    QJsonObject machinesList(Error *error);
    QJsonObject machineGet(const QJsonObject &params, Error *error);
    QJsonObject machineCreate(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject machineCancel(const QJsonObject &params, Error *error);
    QJsonObject machineExport(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject machineImport(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject machineRename(const QJsonObject &params, Error *error);
    QJsonObject machineUpdate(const QJsonObject &params, Error *error);
    QJsonObject machinePortsList(const QJsonObject &params, Error *error);
    QJsonObject machinePortsSet(const QJsonObject &params, Error *error);
    QJsonObject machineSnapshotsList(const QJsonObject &params, Error *error);
    QJsonObject machineSnapshotCreate(const QJsonObject &params, Error *error);
    QJsonObject machineSnapshotRestore(const QJsonObject &params, Error *error);
    QJsonObject machineSnapshotRemove(const QJsonObject &params, Error *error);
    QJsonObject machineClone(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject machineDefault(const QJsonObject &params, Error *error);
    QJsonObject machineAction(const QString &method, const QJsonObject &params, Error *error);
    QJsonObject machineDelete(const QJsonObject &params, Error *error);
    QJsonObject machineLogs(const QJsonObject &params, Error *error);
    QJsonObject machineConsoleRead(const QJsonObject &params, Error *error);
    QJsonObject subscribeMachineConsole(const QJsonObject &params, const Notify &notify,
                                        Error *error);
    QJsonObject machineSsh(const QJsonObject &params, Error *error);
    QJsonObject machineStats(const QJsonObject &params, Error *error);
    QJsonObject subscribeMachineStats(const QJsonObject &params, const Notify &notify,
                                      Error *error);
    QJsonObject machineFilesList(const QJsonObject &params, Error *error);
    QJsonObject machineFilesDownload(const QJsonObject &params, Error *error);
    QJsonObject machineFilesUpload(const QJsonObject &params, Error *error);
    QJsonObject k8sStatus(const QJsonObject &params, Error *error);
    QJsonObject k8sEnable(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject k8sCancel(const QJsonObject &params, Error *error);
    QJsonObject setupStatus();
    QJsonObject setupCheck(const QJsonObject &params);
    QJsonObject setupValidate();
    QJsonObject setupApply(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject setupRemoveDocker(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject setupCancel(const QJsonObject &params);
    QJsonObject setupComplete(const QJsonObject &params, Error *error);
    QJsonObject setupSubscribe(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject settingsGet(const QJsonObject &params, Error *error);
    QJsonObject settingsSet(const QJsonObject &params, Error *error);
    QJsonObject settingsReset(const QJsonObject &params, Error *error);
    QJsonObject settingsSchema() const;
    QJsonObject subscribeLogs(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject subscribeStats(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject subscribeSystemStats(const QJsonObject &params, const Notify &notify,
                                     Error *error);
    QJsonObject subscribeEvents(const Notify &notify, Error *error);
    QJsonObject unsubscribe(const QJsonObject &params);

    QString requireId(const QJsonObject &params, Error *error) const;
    QString requireMachineId(const QJsonObject &params, Error *error) const;
    QString createSubscription(const std::shared_ptr<Subscription> &subscription);
    void removeSubscription(const QString &id);

    PodmanClient m_podman;
    ComposeManager m_compose;
    BuildStore m_buildStore;
    SetupManager m_setup;
    HelperClient m_helper;
    MachineManager m_machines;
    K8sManager m_k8s;
    MachineStatsCollector m_machineStats;
    CloudImageCache m_cloudImages;
    std::mutex m_subscriptionsMutex;
    QHash<QString, std::shared_ptr<Subscription>> m_subscriptions;
    std::mutex m_buildMutex;
    QHash<QString, QString> m_buildSubscriptions;
    std::mutex m_machineJobsMutex;
    QHash<QString, QString> m_machineSubscriptions; // jobId -> subscriptionId
    std::mutex m_k8sJobsMutex;
    QHash<QString, QString> m_k8sSubscriptions; // jobId -> subscriptionId
    std::mutex m_settingsMutex;
    int m_nextSubscription = 1;
    int m_nextBuild = 1;
    int m_nextMachineJob = 1;
    int m_nextK8sJob = 1;
};

} // namespace PodStack
