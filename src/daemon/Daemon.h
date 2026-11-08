#pragma once

#include "core/Error.h"
#include "daemon/BuildStore.h"
#include "daemon/ComposeManager.h"
#include "daemon/SetupManager.h"
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
    QJsonObject systemDf(Error *error);
    QJsonObject composeProjects(Error *error);
    QJsonObject composeAction(const QString &method, const QJsonObject &params, Error *error);
    QJsonObject subscribeComposeLogs(const QJsonObject &params, const Notify &notify,
                                     Error *error);
    QJsonObject buildStart(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject buildCancel(const QJsonObject &params, Error *error);
    QJsonObject buildsList(const QJsonObject &params, Error *error);
    QJsonObject buildGet(const QJsonObject &params, Error *error);
    QJsonObject buildRemove(const QJsonObject &params, Error *error);
    QJsonObject setupStatus();
    QJsonObject setupCheck(const QJsonObject &params);
    QJsonObject setupValidate();
    QJsonObject setupApply(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject setupRemoveDocker(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject setupCancel(const QJsonObject &params);
    QJsonObject setupComplete(const QJsonObject &params, Error *error);
    QJsonObject setupSubscribe(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject subscribeLogs(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject subscribeStats(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject subscribeEvents(const Notify &notify, Error *error);
    QJsonObject unsubscribe(const QJsonObject &params);

    QString requireId(const QJsonObject &params, Error *error) const;
    QString createSubscription(const std::shared_ptr<Subscription> &subscription);
    void removeSubscription(const QString &id);

    PodmanClient m_podman;
    ComposeManager m_compose;
    BuildStore m_buildStore;
    SetupManager m_setup;
    std::mutex m_subscriptionsMutex;
    QHash<QString, std::shared_ptr<Subscription>> m_subscriptions;
    std::mutex m_buildMutex;
    QHash<QString, QString> m_buildSubscriptions;
    int m_nextSubscription = 1;
    int m_nextBuild = 1;
};

} // namespace PodStack
