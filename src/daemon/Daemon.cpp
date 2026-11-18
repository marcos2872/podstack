// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon/Daemon.h"
#include "core/config/Config.h"
#include "core/config/Paths.h"
#include "core/logging/Logging.h"
#include "core/rpc/DaemonBootstrap.h"
#include "daemon/rpc/Internal.h"

#include <QStandardPaths>

using namespace PodStack::DaemonInternal;

namespace PodStack {
namespace {

/// Conexão RPC dona das subscriptions criadas nesta thread. `Daemon::handle()`
/// roda inteiro numa thread de trabalho e as subscriptions são registradas
/// antes de qualquer `QtConcurrent::run`, então o contexto vale por toda a
/// chamada — sem isso o `createSubscription()` não sabe a quem pertence.
thread_local quint64 t_connectionId = 0;

/// Restaura o contexto da conexão ao sair do `handle()` (que tem muitos
/// `return` antecipados).
class ConnectionScope {
public:
    explicit ConnectionScope(quint64 connectionId) : m_previous(t_connectionId)
    {
        t_connectionId = connectionId;
    }
    ~ConnectionScope() { t_connectionId = m_previous; }

    ConnectionScope(const ConnectionScope &) = delete;
    ConnectionScope &operator=(const ConnectionScope &) = delete;

private:
    quint64 m_previous;
};

} // namespace

QJsonObject Daemon::handle(const QString &method, const QJsonObject &params, const Notify &notify,
                           Error *error, quint64 connectionId, uint peerPid)
{
    const ConnectionScope connectionScope(connectionId);
    // O polkit precisa autorizar a sessão do cliente interativo, não o daemon
    // (que roda em `user@uid.service`, fora da sessão gráfica). O PID vem do
    // SO_PEERCRED do socket de controle e o helper revalida o UID.
    const HelperCallerScope helperCallerScope(peerPid);

    if (error) {
        *error = {};
    }

    if (method == QStringLiteral("system.hello")) {
        return systemHello();
    }
    if (method == QStringLiteral("system.status")) {
        return systemStatus();
    }
    if (method == QStringLiteral("system.doctor")) {
        return systemDoctor();
    }
    if (method == QStringLiteral("system.report")) {
        return systemReport(params, error);
    }
    if (method == QStringLiteral("updates.check")) {
        return updatesCheck(params, error);
    }
    if (method == QStringLiteral("system.disk")) {
        return systemDisk();
    }
    if (method == QStringLiteral("system.engineStart")) {
        return systemEngineStart(error);
    }
    if (method == QStringLiteral("system.emulation")) {
        return systemEmulation();
    }
    if (method == QStringLiteral("settings.get")) {
        return settingsGet(params, error);
    }
    if (method == QStringLiteral("settings.set")) {
        return settingsSet(params, error);
    }
    if (method == QStringLiteral("settings.reset")) {
        return settingsReset(params, error);
    }
    if (method == QStringLiteral("settings.schema")) {
        return settingsSchema();
    }
    if (method == QStringLiteral("containers.list")) {
        return containersList(params, error);
    }
    if (method == QStringLiteral("containers.start") ||
        method == QStringLiteral("containers.stop") ||
        method == QStringLiteral("containers.restart") ||
        method == QStringLiteral("containers.remove") ||
        method == QStringLiteral("containers.pause") ||
        method == QStringLiteral("containers.unpause") ||
        method == QStringLiteral("containers.kill") ||
        method == QStringLiteral("containers.rename") ||
        method == QStringLiteral("containers.checkpoint") ||
        method == QStringLiteral("containers.restore")) {
        return containerAction(method, params, error);
    }
    if (method == QStringLiteral("containers.exec")) {
        return containerExec(params, error);
    }
    if (method == QStringLiteral("containers.inspect")) {
        return containerInspect(params, error);
    }
    if (method == QStringLiteral("containers.create")) {
        return containerCreate(params, error);
    }
    if (method == QStringLiteral("containers.exposeLan")) {
        return containerExposeLan(params, error);
    }
    if (method == QStringLiteral("containers.duplicate")) {
        return containerDuplicate(params, error);
    }
    if (method == QStringLiteral("containers.get")) {
        return containerGet(params, error);
    }
    if (method == QStringLiteral("containers.top")) {
        return containerTop(params, error);
    }
    if (method == QStringLiteral("containers.changes")) {
        return containerChanges(params, error);
    }
    if (method == QStringLiteral("containers.export")) {
        return containerExport(params, error);
    }
    if (method == QStringLiteral("containers.commit")) {
        return containerCommit(params, error);
    }
    if (method == QStringLiteral("containers.files.list")) {
        return containerFilesList(params, error);
    }
    if (method == QStringLiteral("containers.files.download")) {
        return containerFilesDownload(params, error);
    }
    if (method == QStringLiteral("containers.files.upload")) {
        return containerFilesUpload(params, error);
    }
    if (method == QStringLiteral("system.df")) {
        return systemDf(params, error);
    }
    if (method == QStringLiteral("images.list")) {
        return imagesList(params, error);
    }
    if (method == QStringLiteral("images.get") || method == QStringLiteral("images.inspect")) {
        return imageInspect(params, error);
    }
    if (method == QStringLiteral("images.history")) {
        return imageHistory(params, error);
    }
    if (method == QStringLiteral("images.remove")) {
        return imageRemove(params, error);
    }
    if (method == QStringLiteral("images.prune")) {
        return imagePrune(params, error);
    }
    if (method == QStringLiteral("images.tag")) {
        return imageTag(params, error);
    }
    if (method == QStringLiteral("images.untag")) {
        return imageUntag(params, error);
    }
    if (method == QStringLiteral("images.pull")) {
        return imagePull(params, notify, error);
    }
    if (method == QStringLiteral("images.push")) {
        return imagePush(params, notify, error);
    }
    if (method == QStringLiteral("images.export")) {
        return imageExport(params, error);
    }
    if (method == QStringLiteral("images.load")) {
        return imageLoad(params, error);
    }
    if (method == QStringLiteral("volumes.list")) {
        return volumesList(error);
    }
    if (method == QStringLiteral("volumes.create")) {
        return volumeCreate(params, error);
    }
    if (method == QStringLiteral("volumes.remove")) {
        return volumeRemove(params, error);
    }
    if (method == QStringLiteral("volumes.prune")) {
        return volumePrune(error);
    }
    if (method == QStringLiteral("volumes.clone")) {
        return volumeClone(params, error);
    }
    if (method == QStringLiteral("volumes.export")) {
        return volumeExport(params, error);
    }
    if (method == QStringLiteral("volumes.import")) {
        return volumeImport(params, error);
    }
    if (method == QStringLiteral("volumes.importHostPath")) {
        return volumeImportHostPath(params, error);
    }
    if (method == QStringLiteral("networks.list")) {
        return networksList(error);
    }
    if (method == QStringLiteral("networks.create")) {
        return networkCreate(params, error);
    }
    if (method == QStringLiteral("networks.remove")) {
        return networkRemove(params, error);
    }
    if (method == QStringLiteral("networks.connect")) {
        return networkConnect(params, error);
    }
    if (method == QStringLiteral("networks.disconnect")) {
        return networkDisconnect(params, error);
    }
    if (method == QStringLiteral("pods.list")) {
        return podsList(error);
    }
    if (method == QStringLiteral("pods.create")) {
        return podCreate(params, error);
    }
    if (method == QStringLiteral("pods.start") || method == QStringLiteral("pods.stop") ||
        method == QStringLiteral("pods.restart")) {
        return podAction(method, params, error);
    }
    if (method == QStringLiteral("pods.remove")) {
        return podRemove(params, error);
    }
    if (method == QStringLiteral("compose.projects.list")) {
        return composeProjects(error);
    }
    if (method == QStringLiteral("compose.up") || method == QStringLiteral("compose.down") ||
        method == QStringLiteral("compose.restart") || method == QStringLiteral("compose.stop") ||
        method == QStringLiteral("compose.start")) {
        return composeAction(method, params, error);
    }
    if (method == QStringLiteral("compose.logs.subscribe")) {
        return subscribeComposeLogs(params, notify, error);
    }
    if (method == QStringLiteral("builds.start")) {
        return buildStart(params, notify, error);
    }
    if (method == QStringLiteral("builds.cancel")) {
        return buildCancel(params, error);
    }
    if (method == QStringLiteral("builds.list")) {
        return buildsList(params, error);
    }
    if (method == QStringLiteral("builds.get")) {
        return buildGet(params, error);
    }
    if (method == QStringLiteral("builds.remove")) {
        return buildRemove(params, error);
    }
    if (method == QStringLiteral("machines.distros")) {
        return machinesDistros();
    }
    if (method == QStringLiteral("machines.list")) {
        return machinesList(error);
    }
    if (method == QStringLiteral("machines.get")) {
        return machineGet(params, error);
    }
    if (method == QStringLiteral("machines.create")) {
        return machineCreate(params, notify, error);
    }
    if (method == QStringLiteral("machines.cancel")) {
        return machineCancel(params, error);
    }
    if (method == QStringLiteral("machines.export")) {
        return machineExport(params, notify, error);
    }
    if (method == QStringLiteral("machines.import")) {
        return machineImport(params, notify, error);
    }
    if (method == QStringLiteral("machines.rename")) {
        return machineRename(params, error);
    }
    if (method == QStringLiteral("machines.update")) {
        return machineUpdate(params, error);
    }
    if (method == QStringLiteral("machines.ports.list")) {
        return machinePortsList(params, error);
    }
    if (method == QStringLiteral("machines.ports.set")) {
        return machinePortsSet(params, error);
    }
    if (method == QStringLiteral("machines.serial.list")) {
        return machineSerialList(params, error);
    }
    if (method == QStringLiteral("machines.serial.attach")) {
        return machineSerialAttach(params, error);
    }
    if (method == QStringLiteral("machines.serial.detach")) {
        return machineSerialDetach(params, error);
    }
    if (method == QStringLiteral("machines.usb.list")) {
        return machineUsbList(params, error);
    }
    if (method == QStringLiteral("machines.usb.attach")) {
        return machineUsbAttach(params, error);
    }
    if (method == QStringLiteral("machines.usb.detach")) {
        return machineUsbDetach(params, error);
    }
    if (method == QStringLiteral("machines.audio.set")) {
        return machineAudioSet(params, error);
    }
    if (method == QStringLiteral("machines.snapshots.list")) {
        return machineSnapshotsList(params, error);
    }
    if (method == QStringLiteral("machines.snapshots.create")) {
        return machineSnapshotCreate(params, error);
    }
    if (method == QStringLiteral("machines.snapshots.restore")) {
        return machineSnapshotRestore(params, error);
    }
    if (method == QStringLiteral("machines.snapshots.remove")) {
        return machineSnapshotRemove(params, error);
    }
    if (method == QStringLiteral("machines.clone")) {
        return machineClone(params, notify, error);
    }
    if (method == QStringLiteral("machines.default")) {
        return machineDefault(params, error);
    }
    if (method == QStringLiteral("machines.start") || method == QStringLiteral("machines.stop") ||
        method == QStringLiteral("machines.restart")) {
        return machineAction(method, params, error);
    }
    if (method == QStringLiteral("machines.delete")) {
        return machineDelete(params, error);
    }
    if (method == QStringLiteral("machines.logs")) {
        return machineLogs(params, error);
    }
    if (method == QStringLiteral("machines.console.read")) {
        return machineConsoleRead(params, error);
    }
    if (method == QStringLiteral("machines.console")) {
        return subscribeMachineConsole(params, notify, error);
    }
    if (method == QStringLiteral("machines.files.list")) {
        return machineFilesList(params, error);
    }
    if (method == QStringLiteral("machines.files.download")) {
        return machineFilesDownload(params, error);
    }
    if (method == QStringLiteral("machines.files.upload")) {
        return machineFilesUpload(params, error);
    }
    if (method == QStringLiteral("machines.ssh")) {
        return machineSsh(params, error);
    }
    if (method == QStringLiteral("machines.stats")) {
        return machineStats(params, error);
    }
    if (method == QStringLiteral("machines.stats.subscribe")) {
        return subscribeMachineStats(params, notify, error);
    }
    if (method == QStringLiteral("k8s.status")) {
        return k8sStatus(params, error);
    }
    if (method == QStringLiteral("k8s.enable")) {
        return k8sEnable(params, notify, error);
    }
    if (method == QStringLiteral("k8s.disable")) {
        return k8sDisable(params, notify, error);
    }
    if (method == QStringLiteral("k8s.reset")) {
        return k8sReset(params, notify, error);
    }
    if (method == QStringLiteral("k8s.cancel")) {
        return k8sCancel(params, error);
    }
    if (method == QStringLiteral("k8s.pods.list")) {
        return k8sPodsList(params, error);
    }
    if (method == QStringLiteral("k8s.services.list")) {
        return k8sServicesList(params, error);
    }
    if (method == QStringLiteral("k8s.logs.subscribe")) {
        return subscribeK8sLogs(params, notify, error);
    }
    if (method == QStringLiteral("k8s.pod.delete")) {
        return k8sPodDelete(params, error);
    }
    if (method == QStringLiteral("k8s.service.delete")) {
        return k8sServiceDelete(params, error);
    }
    if (method == QStringLiteral("k8s.portforward.start")) {
        return k8sPortForwardStart(params, notify, error);
    }
    if (method == QStringLiteral("k8s.portforward.stop")) {
        return k8sPortForwardStop(params, error);
    }
    if (method == QStringLiteral("k8s.context.use")) {
        return k8sContextUse(params, error);
    }
    if (method == QStringLiteral("setup.status")) {
        return setupStatus();
    }
    if (method == QStringLiteral("setup.check")) {
        return setupCheck(params);
    }
    if (method == QStringLiteral("setup.validate")) {
        return setupValidate();
    }
    if (method == QStringLiteral("setup.dockerImpact")) {
        return setupDockerImpact();
    }
    if (method == QStringLiteral("setup.apply")) {
        return setupApply(params, notify, error);
    }
    if (method == QStringLiteral("setup.removeDocker")) {
        return setupRemoveDocker(params, notify, error);
    }
    if (method == QStringLiteral("setup.cancel")) {
        return setupCancel(params);
    }
    if (method == QStringLiteral("setup.complete")) {
        return setupComplete(params, error);
    }
    if (method == QStringLiteral("setup.subscribe")) {
        return setupSubscribe(params, notify, error);
    }
    if (method == QStringLiteral("containers.logs.subscribe")) {
        return subscribeLogs(params, notify, error);
    }
    if (method == QStringLiteral("containers.stats.subscribe")) {
        return subscribeStats(params, notify, error);
    }
    if (method == QStringLiteral("system.stats.subscribe")) {
        return subscribeSystemStats(params, notify, error);
    }
    if (method == QStringLiteral("containers.logs.unsubscribe") ||
        method == QStringLiteral("containers.stats.unsubscribe") ||
        method == QStringLiteral("system.stats.unsubscribe") ||
        method == QStringLiteral("images.pull.unsubscribe") ||
        method == QStringLiteral("images.push.unsubscribe") ||
        method == QStringLiteral("compose.logs.unsubscribe") ||
        method == QStringLiteral("builds.unsubscribe") ||
        method == QStringLiteral("setup.unsubscribe") ||
        method == QStringLiteral("machines.stats.unsubscribe") ||
        method == QStringLiteral("machines.console.unsubscribe") ||
        method == QStringLiteral("k8s.logs.unsubscribe") ||
        method == QStringLiteral("events.unsubscribe")) {
        return unsubscribe(params);
    }
    if (method == QStringLiteral("events.subscribe")) {
        return subscribeEvents(notify, error);
    }

    if (error) {
        *error = Error::make(QStringLiteral("PODSTACK_METHOD_NOT_FOUND"),
                             QStringLiteral("Método RPC desconhecido: %1").arg(method));
    }
    return {};
}

QString Daemon::createSubscription(const std::shared_ptr<Subscription> &subscription)
{
    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    const QString id = QStringLiteral("sub-%1").arg(m_nextSubscription++);
    m_subscriptions.insert(id, subscription);
    if (subscription && subscription->cancelOnDisconnect && t_connectionId != 0) {
        m_subscriptionConnections.insert(id, t_connectionId);
    }
    return id;
}

void Daemon::removeSubscription(const QString &id)
{
    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    m_subscriptions.remove(id);
    m_subscriptionConnections.remove(id);
}

void Daemon::cancelSubscriptionsForConnection(quint64 connectionId)
{
    if (connectionId == 0) {
        return;
    }
    QVector<std::shared_ptr<Subscription>> cancelled;
    {
        std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
        const QList<QString> ids = m_subscriptionConnections.keys(connectionId);
        cancelled.reserve(ids.size());
        for (const QString &id : ids) {
            m_subscriptionConnections.remove(id);
            if (auto subscription = m_subscriptions.take(id)) {
                cancelled.append(subscription);
            }
        }
    }
    for (const auto &subscription : cancelled) {
        if (subscription) {
            subscription->cancel.store(true);
        }
    }
    if (!cancelled.isEmpty()) {
        qCInfo(lcRpc) << "subscriptions canceladas ao desconectar: conexão" << connectionId
                      << "total" << cancelled.size();
    }
}

void Daemon::cancelAllSubscriptions()
{
    QVector<std::shared_ptr<Subscription>> cancelled;
    {
        std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
        cancelled.reserve(m_subscriptions.size());
        for (auto it = m_subscriptions.constBegin(); it != m_subscriptions.constEnd(); ++it) {
            cancelled.append(it.value());
        }
        m_subscriptions.clear();
        m_subscriptionConnections.clear();
    }
    for (const auto &subscription : cancelled) {
        if (subscription) {
            subscription->cancel.store(true);
        }
    }
    if (!cancelled.isEmpty()) {
        qCInfo(lcRpc) << "subscriptions canceladas no shutdown:" << cancelled.size();
    }
}

bool Daemon::listContainersForProxy(QVector<ContainerInfo> *containers, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!containers) {
        return false;
    }
    *containers = m_podman.listContainers(true, error);
    return !error || error->isEmpty();
}

QString Daemon::requireId(const QJsonObject &params, Error *error) const
{
    const QString id = params.value(QStringLiteral("id")).toString();
    if (id.isEmpty() && error) {
        *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                             QStringLiteral("Informe o ID ou nome do container."));
    }
    return id;
}

QString Daemon::requireMachineId(const QJsonObject &params, Error *error) const
{
    const QString id = params.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty() && error) {
        *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                             QStringLiteral("Informe o ID ou nome da machine."));
    }
    return id;
}

Daemon::Daemon() : m_podman(selectedPodmanSocket()), m_compose(&m_podman), m_k8s(&m_machines)
{
    // O store de builds é local e rápido; abrir no ctor garante que nenhum RPC
    // de build chegue sem schema. Ping/reconcile pertencem a startBackground().
    Error storeError;
    if (!m_buildStore.open(&storeError)) {
        qCWarning(lcPodman) << "Histórico de builds indisponível:" << storeError.message
                            << storeError.detail;
    }

    // Reconciliar é local e rápido, e precisa acontecer antes de qualquer RPC
    // de machine para não competir com um create concorrente.
    Error machineError;
    m_machines.reconcile(&machineError);
    if (!machineError.isEmpty()) {
        qCWarning(lcRpc) << "Reconciliação de machines incompleta:" << machineError.code
                         << machineError.message << machineError.detail;
    }
}

void Daemon::startBackground()
{
    if (m_backgroundStarted.exchange(true)) {
        return;
    }

    // `engine.autoStart` (default) garante o podman.socket rootless ao abrir o
    // app. É uma operação de systemd do usuário, sem helper/polkit. No modo
    // rootful o socket é do sistema e exige configuração administrativa.
    const Config config = Config::load();
    if (engineMode() == QStringLiteral("rootless") &&
        config.get(QStringLiteral("engine.autoStart")).toBool(true) &&
        !QStandardPaths::findExecutable(QStringLiteral("podman")).isEmpty()) {
        QString engineDetail;
        const DaemonBootstrap::EngineStatus engineStatus =
            DaemonBootstrap::ensureEngineRunning(Paths::podmanSocket(), true, &engineDetail);
        if (engineStatus == DaemonBootstrap::EngineStatus::Started) {
            qCInfo(lcPodman) << "podman.socket ativado por engine.autoStart";
        } else if (engineStatus == DaemonBootstrap::EngineStatus::Failed) {
            qCWarning(lcPodman) << "Não foi possível ativar o podman.socket:" << engineDetail;
        }
    }

    Error error;
    if (!m_podman.ping(&error)) {
        qCWarning(lcPodman) << "Podman indisponível:" << error.message << error.detail;
    } else {
        qCInfo(lcPodman) << "Podman conectado; API" << m_podman.apiVersion() << "modo"
                         << engineMode();
    }
}

QJsonObject Daemon::unsubscribe(const QJsonObject &params)
{
    const QString subscriptionId = params.value(QStringLiteral("subscriptionId")).toString();

    std::shared_ptr<Subscription> subscription;
    {
        std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
        subscription = m_subscriptions.take(subscriptionId);
        m_subscriptionConnections.remove(subscriptionId);
    }
    if (subscription) {
        subscription->cancel.store(true);
    }

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    result.insert(QStringLiteral("cancelled"), static_cast<bool>(subscription));
    return result;
}

} // namespace PodStack
