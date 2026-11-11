#include "daemon/Daemon.h"

#include "core/Config.h"
#include "core/DiskSpace.h"
#include "core/DiskUsage.h"
#include "core/Emulation.h"
#include "core/Logging.h"
#include "core/Paths.h"
#include "core/Proxy.h"
#include "core/Redaction.h"
#include "core/Settings.h"
#include "daemon/ManifestBuilder.h"
#include "daemon/StatsCollector.h"
#include "machines/DistroRegistry.h"
#include "machines/MachineFiles.h"
#include "machines/SerialPorts.h"
#include "machines/UsbDevices.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QStringList>
#include <QSysInfo>
#include <QtConcurrent>

#include <sys/stat.h>

#include <chrono>
#include <cmath>
#include <thread>

namespace PodStack {
namespace {

QString selectedPodmanSocket()
{
    const Config config = Config::load();
    if (config.get(QStringLiteral("engine.mode")).toString() == QStringLiteral("rootful")) {
        return QStringLiteral("/run/podman/podman.sock");
    }
    return Paths::podmanSocket();
}

QString engineMode()
{
    const Config config = Config::load();
    return config.get(QStringLiteral("engine.mode")).toString(QStringLiteral("rootless"));
}

bool portNumberFromJson(const QJsonValue &value, int *number)
{
    if (!value.isDouble()) {
        return false;
    }
    const double raw = value.toDouble();
    if (raw < 1.0 || raw > 65535.0
        || raw != static_cast<double>(static_cast<int>(raw))) {
        return false;
    }
    *number = static_cast<int>(raw);
    return true;
}

QMap<QString, QString> labelsFromParams(const QJsonObject &params)
{
    QMap<QString, QString> labels;
    const QJsonObject object = params.value(QStringLiteral("labels")).toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        labels.insert(it.key(), it.value().toString());
    }
    return labels;
}

QStringList aliasesFromParams(const QJsonObject &params)
{
    QStringList aliases;
    const QJsonArray array = params.value(QStringLiteral("aliases")).toArray();
    for (const QJsonValue &value : array) {
        const QString alias = value.toString();
        if (!alias.isEmpty()) {
            aliases.append(alias);
        }
    }
    return aliases;
}

QStringList proxyExcludesFromConfig(const Config &config)
{
    QStringList excludes;
    const QJsonArray array =
        config.get(QStringLiteral("network.proxyExclude")).toArray();
    for (const QJsonValue &value : array) {
        const QString entry = value.toString();
        if (!entry.isEmpty()) {
            excludes.append(entry);
        }
    }
    return excludes;
}

/// Aplica o proxy global sem sobrescrever variáveis definidas pelo usuário.
void applyProxyEnvironment(ContainerCreateSpec &spec)
{
    const Config config = Config::load();
    const QString proxy =
        config.get(QStringLiteral("network.proxy")).toString(QStringLiteral("auto"));
    const QStringList excludes = proxyExcludesFromConfig(config);

    Error proxyError;
    if (!Proxy::validate(proxy, excludes, &proxyError)) {
        qCWarning(lcRpc) << "Configuração de proxy inválida; não aplicada aos containers.";
        return;
    }

    const QMap<QString, QString> values = Proxy::environment(proxy, excludes);
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (!spec.env.contains(it.key())) {
            spec.env.insert(it.key(), it.value());
        }
    }
}

/// Monta a CA local (quando existe) no container; opt-out por label.
void applyCaTrustMount(ContainerCreateSpec &spec)
{
    if (spec.labels.value(QStringLiteral("dev.podstack.ca"))
            .compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0) {
        return;
    }
    const QString cert =
        Paths::dataDir() + QStringLiteral("/caddy/storage/pki/authorities/local/root.crt");
    if (!QFileInfo::exists(cert)) {
        return;
    }
    const QString destination =
        QStringLiteral("/etc/pki/ca-trust/source/anchors/podstack-local-ca.crt");
    for (const MountMapping &mount : spec.mounts) {
        if (mount.destination == destination) {
            return;
        }
    }
    spec.mounts.append({cert, destination, true});
}

bool isWildcardHostIp(const QString &hostIp)
{
    return hostIp.isEmpty() || hostIp == QStringLiteral("0.0.0.0")
           || hostIp == QStringLiteral("::");
}

constexpr qint64 kMachineConsoleDefaultTail = 64LL * 1024;
constexpr qint64 kMachineConsoleMaxBytes = 512LL * 1024;

qint64 consoleTailBytes(const QJsonObject &params)
{
    qint64 tail = params.value(QStringLiteral("tailBytes")).toInteger(kMachineConsoleDefaultTail);
    if (tail <= 0) {
        tail = kMachineConsoleDefaultTail;
    }
    return qBound<qint64>(1, tail, kMachineConsoleMaxBytes);
}

/// Valida `machines/<id>/console.log` sem seguir symlink e sem sair do diretório.
/// `exists` fica falso quando o arquivo ainda não foi criado (machine sem boot).
bool checkMachineConsoleFile(const QString &directory, bool *exists, Error *error)
{
    if (exists) {
        *exists = false;
    }

    const QString path = directory + QStringLiteral("/console.log");
    const QFileInfo info(path);
    if (info.isSymLink()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O console da machine não pode ser lido."),
                                 QStringLiteral("symlink rejeitado"));
        }
        return false;
    }
    if (!info.exists()) {
        return true;
    }
    if (!info.isFile()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O console da machine não é um arquivo regular."));
        }
        return false;
    }

    const QString canonicalDir = QFileInfo(directory).canonicalFilePath();
    const QString canonicalFile = info.canonicalFilePath();
    if (canonicalDir.isEmpty() || canonicalFile.isEmpty()
        || !canonicalFile.startsWith(canonicalDir + QLatin1Char('/'))) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O console da machine está fora do diretório "
                                                "esperado."));
        }
        return false;
    }

    if (exists) {
        *exists = true;
    }
    return true;
}

bool readMachineConsoleTail(const QString &path, qint64 tailBytes, QByteArray *data,
                            qint64 *endOffset, Error *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível ler o console da machine."));
        }
        return false;
    }

    const qint64 size = file.size();
    const qint64 start = qMax<qint64>(0, size - tailBytes);
    if (start > 0 && !file.seek(start)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível posicionar no console da "
                                                "machine."));
        }
        return false;
    }

    if (data) {
        *data = file.readAll();
    }
    if (endOffset) {
        *endOffset = file.pos();
    }
    return true;
}

/// Lê até `maxBytes` a partir de `offset`. Se o arquivo encolheu, devolve
/// `endOffset < offset` e nenhum dado para o chamador reiniciar do zero.
bool readMachineConsoleChunk(const QString &path, qint64 offset, qint64 maxBytes,
                             QByteArray *data, qint64 *endOffset, Error *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível ler o console da machine."));
        }
        return false;
    }

    const qint64 size = file.size();
    if (size < offset) {
        if (data) {
            data->clear();
        }
        if (endOffset) {
            *endOffset = size;
        }
        return true;
    }
    if (size == offset) {
        if (data) {
            data->clear();
        }
        if (endOffset) {
            *endOffset = offset;
        }
        return true;
    }
    if (!file.seek(offset)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível posicionar no console da "
                                                "machine."));
        }
        return false;
    }

    if (data) {
        *data = file.read(qMin(maxBytes, size - offset));
    }
    if (endOffset) {
        *endOffset = file.pos();
    }
    return true;
}

Error helperRequiredError()
{
    return Error::make(
        QStringLiteral("PODSTACK_HELPER_REQUIRED"),
        QStringLiteral("O helper privilegiado é necessário para alterar o firewall da LAN."),
        QStringLiteral("Serviço dev.podstack.Helper1 indisponível ou sem autorização."),
        QStringLiteral("Instale o podstack-helper e autorize a operação no polkit."));
}

} // namespace

Daemon::Daemon() : m_podman(selectedPodmanSocket()), m_compose(&m_podman), m_k8s(&m_machines)
{
    Error error;
    if (!m_podman.ping(&error)) {
        qCWarning(lcPodman) << "Podman indisponível:" << error.message << error.detail;
    } else {
        qCInfo(lcPodman) << "Podman conectado; API" << m_podman.apiVersion() << "modo"
                         << engineMode();
    }

    Error storeError;
    if (!m_buildStore.open(&storeError)) {
        qCWarning(lcPodman) << "Histórico de builds indisponível:" << storeError.message
                            << storeError.detail;
    }

    Error machineError;
    m_machines.reconcile(&machineError);
    if (!machineError.isEmpty()) {
        qCWarning(lcRpc) << "Reconciliação de machines incompleta:" << machineError.code
                         << machineError.message << machineError.detail;
    }
}

QJsonObject Daemon::handle(const QString &method, const QJsonObject &params,
                           const Notify &notify, Error *error)
{
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
    if (method == QStringLiteral("pods.start") || method == QStringLiteral("pods.stop")
        || method == QStringLiteral("pods.restart")) {
        return podAction(method, params, error);
    }
    if (method == QStringLiteral("pods.remove")) {
        return podRemove(params, error);
    }
    if (method == QStringLiteral("compose.projects.list")) {
        return composeProjects(error);
    }
    if (method == QStringLiteral("compose.up") || method == QStringLiteral("compose.down")
        || method == QStringLiteral("compose.restart")
        || method == QStringLiteral("compose.stop")
        || method == QStringLiteral("compose.start")) {
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
    if (method == QStringLiteral("machines.start") || method == QStringLiteral("machines.stop")
        || method == QStringLiteral("machines.restart")) {
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

QJsonObject Daemon::machinesDistros()
{
    QJsonArray items;
    const QVector<DistroImage> images = DistroRegistry::images();
    for (const DistroImage &image : images) {
        QJsonObject item;
        item.insert(QStringLiteral("distro"), image.distro);
        item.insert(QStringLiteral("version"), image.version);
        item.insert(QStringLiteral("arch"), image.arch);
        item.insert(QStringLiteral("default"), image.isDefault);
        item.insert(QStringLiteral("url"), image.url);
        item.insert(QStringLiteral("checksum"), image.checksum);
        item.insert(QStringLiteral("algorithm"), DistroRegistry::algorithmName(image.algorithm));
        item.insert(QStringLiteral("size"), static_cast<double>(image.size));
        item.insert(QStringLiteral("cached"), m_cloudImages.isCached(image));
        items.append(item);
    }

    QJsonObject result;
    result.insert(QStringLiteral("items"), items);
    return result;
}

QJsonObject Daemon::machinesList(Error *error)
{
    const QVector<MachineMeta> machines = m_machines.list(error);
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonArray items;
    for (const MachineMeta &machine : machines) {
        items.append(machine.toJson());
    }
    return QJsonObject{{QStringLiteral("items"), items}};
}

QJsonObject Daemon::machineGet(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty()) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machineCreate(const QJsonObject &params, const Notify &notify,
                                  Error *error)
{
    CreateMachineRequest request;
    request.name = params.value(QStringLiteral("name")).toString().trimmed();
    request.distro = params.value(QStringLiteral("distro")).toString().trimmed();
    request.version = params.value(QStringLiteral("version")).toString().trimmed();
    request.arch = params.value(QStringLiteral("arch")).toString().trimmed();
    request.user = params.value(QStringLiteral("user")).toString();
    request.password = params.value(QStringLiteral("password")).toString();
    request.userData = params.value(QStringLiteral("userData")).toString();
    request.cpus = params.value(QStringLiteral("cpus")).toInt(request.cpus);
    request.memoryBytes =
        params.value(QStringLiteral("memoryBytes")).toInteger(request.memoryBytes);
    request.diskBytes = params.value(QStringLiteral("diskBytes")).toInteger(request.diskBytes);
    request.isolateNetwork =
        params.value(QStringLiteral("isolateNetwork")).toBool(request.isolateNetwork);
    request.forwardSshAgent =
        params.value(QStringLiteral("forwardSshAgent")).toBool(request.forwardSshAgent);
    request.isolated = params.value(QStringLiteral("isolated")).toBool(request.isolated);

    const QJsonValue mountsValue = params.value(QStringLiteral("mounts"));
    if (!mountsValue.isUndefined() && !mountsValue.isNull()) {
        if (!mountsValue.isArray()) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("mounts deve ser um array de objetos."));
            }
            return {};
        }
        const QJsonArray mounts = mountsValue.toArray();
        for (const QJsonValue &value : mounts) {
            if (!value.isObject()) {
                if (error) {
                    *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                         QStringLiteral("Cada mount deve ser um objeto."));
                }
                return {};
            }
            const QJsonObject object = value.toObject();
            MachineMount mount;
            mount.source = object.value(QStringLiteral("source")).toString().trimmed();
            mount.target = object.value(QStringLiteral("target")).toString().trimmed();
            mount.readOnly = object.value(QStringLiteral("readOnly")).toBool(false);
            mount.tag = object.value(QStringLiteral("tag")).toString().trimmed();
            request.mounts.append(mount);
        }
    }

    if (!m_machines.validate(request, error)) {
        return {};
    }

    QString jobId;
    {
        std::lock_guard<std::mutex> lock(m_machineJobsMutex);
        jobId = QStringLiteral("machine-%1-%2")
                    .arg(QDateTime::currentSecsSinceEpoch())
                    .arg(m_nextMachineJob++);
    }

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);
    {
        std::lock_guard<std::mutex> lock(m_machineJobsMutex);
        m_machineSubscriptions.insert(jobId, subscriptionId);
    }

    (void)QtConcurrent::run([this, jobId, subscriptionId, subscription, request, notify] {
        const auto progress = [jobId, notify](const QString &stage, int value) {
            if (!notify) {
                return;
            }
            QJsonObject payload;
            payload.insert(QStringLiteral("jobId"), jobId);
            payload.insert(QStringLiteral("stage"), stage);
            payload.insert(QStringLiteral("progress"), value);
            notify(QStringLiteral("machines.delta"), payload);
        };
        const auto cancelled = [subscription] {
            return subscription->cancel.load();
        };

        Error createError;
        MachineMeta machine = m_machines.create(request, progress, cancelled, &createError);
        if (createError.isEmpty() && machine.id.isEmpty()) {
            createError = Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                      QStringLiteral("Falha ao criar a machine."));
        }

        QJsonObject payload;
        payload.insert(QStringLiteral("jobId"), jobId);
        if (createError.isEmpty()) {
            payload.insert(QStringLiteral("machine"), machine.toJson());
        } else {
            payload.insert(QStringLiteral("error"), createError.toJson());
        }
        if (notify) {
            notify(QStringLiteral("machines.done"), payload);
        }

        {
            std::lock_guard<std::mutex> lock(m_machineJobsMutex);
            m_machineSubscriptions.remove(jobId);
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("jobId"), jobId);
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    return result;
}

QJsonObject Daemon::machineExport(const QJsonObject &params, const Notify &notify,
                                  Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString path = params.value(QStringLiteral("path")).toString().trimmed();
    if (path.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o arquivo de destino do export."));
        }
        return {};
    }
    if (!QFileInfo(path).isAbsolute()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("O arquivo de export precisa ser absoluto."),
                                 path);
        }
        return {};
    }
    const bool force = params.value(QStringLiteral("force")).toBool();

    QString jobId;
    {
        std::lock_guard<std::mutex> lock(m_machineJobsMutex);
        jobId = QStringLiteral("machine-export-%1-%2")
                    .arg(QDateTime::currentSecsSinceEpoch())
                    .arg(m_nextMachineJob++);
    }

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);
    {
        std::lock_guard<std::mutex> lock(m_machineJobsMutex);
        m_machineSubscriptions.insert(jobId, subscriptionId);
    }

    (void)QtConcurrent::run([this, jobId, subscriptionId, subscription, id, path, force,
                             notify] {
        const auto progress = [jobId, notify](const QString &stage, int value) {
            if (!notify) {
                return;
            }
            QJsonObject payload;
            payload.insert(QStringLiteral("jobId"), jobId);
            payload.insert(QStringLiteral("stage"), stage);
            payload.insert(QStringLiteral("progress"), value);
            notify(QStringLiteral("machines.delta"), payload);
        };
        const auto cancelled = [subscription] {
            return subscription->cancel.load();
        };

        Error exportError;
        MachineMeta machine =
            m_machines.exportMachine(id, path, force, progress, cancelled, &exportError);
        if (exportError.isEmpty() && machine.id.isEmpty()) {
            exportError = Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                      QStringLiteral("Falha ao exportar a machine."));
        }

        QJsonObject payload;
        payload.insert(QStringLiteral("jobId"), jobId);
        payload.insert(QStringLiteral("path"), path);
        if (exportError.isEmpty()) {
            payload.insert(QStringLiteral("machine"), machine.toJson());
        } else {
            payload.insert(QStringLiteral("error"), exportError.toJson());
        }
        if (notify) {
            notify(QStringLiteral("machines.done"), payload);
        }

        {
            std::lock_guard<std::mutex> lock(m_machineJobsMutex);
            m_machineSubscriptions.remove(jobId);
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("jobId"), jobId);
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    return result;
}

QJsonObject Daemon::machineImport(const QJsonObject &params, const Notify &notify,
                                  Error *error)
{
    const QString path = params.value(QStringLiteral("path")).toString().trimmed();
    if (path.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o arquivo de machine para importar."));
        }
        return {};
    }
    if (!QFileInfo(path).isAbsolute()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("O arquivo de import precisa ser absoluto."),
                                 path);
        }
        return {};
    }
    const QString name = params.value(QStringLiteral("name")).toString().trimmed();
    const bool force = params.value(QStringLiteral("force")).toBool();

    QString jobId;
    {
        std::lock_guard<std::mutex> lock(m_machineJobsMutex);
        jobId = QStringLiteral("machine-import-%1-%2")
                    .arg(QDateTime::currentSecsSinceEpoch())
                    .arg(m_nextMachineJob++);
    }

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);
    {
        std::lock_guard<std::mutex> lock(m_machineJobsMutex);
        m_machineSubscriptions.insert(jobId, subscriptionId);
    }

    (void)QtConcurrent::run([this, jobId, subscriptionId, subscription, path, name, force,
                             notify] {
        const auto progress = [jobId, notify](const QString &stage, int value) {
            if (!notify) {
                return;
            }
            QJsonObject payload;
            payload.insert(QStringLiteral("jobId"), jobId);
            payload.insert(QStringLiteral("stage"), stage);
            payload.insert(QStringLiteral("progress"), value);
            notify(QStringLiteral("machines.delta"), payload);
        };
        const auto cancelled = [subscription] {
            return subscription->cancel.load();
        };

        Error importError;
        MachineMeta machine =
            m_machines.importMachine(path, name, force, progress, cancelled, &importError);
        if (importError.isEmpty() && machine.id.isEmpty()) {
            importError = Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                      QStringLiteral("Falha ao importar a machine."));
        }

        QJsonObject payload;
        payload.insert(QStringLiteral("jobId"), jobId);
        payload.insert(QStringLiteral("path"), path);
        if (importError.isEmpty()) {
            payload.insert(QStringLiteral("machine"), machine.toJson());
        } else {
            payload.insert(QStringLiteral("error"), importError.toJson());
        }
        if (notify) {
            notify(QStringLiteral("machines.done"), payload);
        }

        {
            std::lock_guard<std::mutex> lock(m_machineJobsMutex);
            m_machineSubscriptions.remove(jobId);
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("jobId"), jobId);
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    return result;
}

QJsonObject Daemon::machineRename(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString name = params.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o novo nome da machine."));
        }
        return {};
    }

    const MachineMeta machine = m_machines.rename(id, name, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machineUpdate(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    UpdateMachineRequest request;
    const QJsonValue cpusValue = params.value(QStringLiteral("cpus"));
    if (!cpusValue.isUndefined() && !cpusValue.isNull()) {
        request.cpus = cpusValue.toInt();
    }
    const QJsonValue memoryValue = params.value(QStringLiteral("memoryBytes"));
    if (!memoryValue.isUndefined() && !memoryValue.isNull()) {
        request.memoryBytes = memoryValue.toInteger();
    }
    const QJsonValue diskValue = params.value(QStringLiteral("diskBytes"));
    if (!diskValue.isUndefined() && !diskValue.isNull()) {
        request.diskBytes = diskValue.toInteger();
    }
    const QJsonValue forwardValue = params.value(QStringLiteral("forwardSshAgent"));
    if (!forwardValue.isUndefined() && !forwardValue.isNull()) {
        if (!forwardValue.isBool()) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("forwardSshAgent deve ser booleano."));
            }
            return {};
        }
        request.forwardSshAgent = forwardValue.toBool();
    }

    const MachineMeta machine = m_machines.update(id, request, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machinePortsList(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return QJsonObject{{QStringLiteral("items"),
                        machine.toJson().value(QStringLiteral("ports")).toArray()}};
}

QJsonObject Daemon::machinePortsSet(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QJsonValue portsValue = params.value(QStringLiteral("ports"));
    if (!portsValue.isArray()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("O campo ports deve ser um array."),
                                 QStringLiteral("Envie a lista completa de portas não-SSH."));
        }
        return {};
    }

    QVector<MachinePort> ports;
    const QJsonArray array = portsValue.toArray();
    ports.reserve(array.size());
    for (int index = 0; index < array.size(); ++index) {
        const QJsonValue entry = array.at(index);
        if (!entry.isObject()) {
            if (error) {
                *error = Error::make(
                    QStringLiteral("PODSTACK_INVALID_PARAMS"),
                    QStringLiteral("Cada item de ports deve ser um objeto."),
                    QStringLiteral("ports[%1]").arg(index));
            }
            return {};
        }
        const QJsonObject object = entry.toObject();
        const auto invalidField = [error, index](const QString &field,
                                                 const QString &expected) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("Campo inválido em ports."),
                                     QStringLiteral("ports[%1].%2 (esperado %3)")
                                         .arg(index)
                                         .arg(field, expected));
            }
            return QJsonObject{};
        };

        MachinePort port;
        const QJsonValue idValue = object.value(QStringLiteral("id"));
        if (!idValue.isUndefined() && !idValue.isNull()) {
            if (!idValue.isString()) {
                return invalidField(QStringLiteral("id"), QStringLiteral("string"));
            }
            port.id = idValue.toString();
        }
        if (!portNumberFromJson(object.value(QStringLiteral("hostPort")), &port.hostPort)) {
            return invalidField(QStringLiteral("hostPort"),
                                QStringLiteral("inteiro 1..65535"));
        }
        if (!portNumberFromJson(object.value(QStringLiteral("guestPort")), &port.guestPort)) {
            return invalidField(QStringLiteral("guestPort"),
                                QStringLiteral("inteiro 1..65535"));
        }

        const QJsonValue protocolValue = object.value(QStringLiteral("protocol"));
        if (!protocolValue.isUndefined() && !protocolValue.isNull()) {
            if (!protocolValue.isString()) {
                return invalidField(QStringLiteral("protocol"),
                                    QStringLiteral("string tcp|udp"));
            }
            port.protocol = protocolValue.toString();
        }
        const QJsonValue exposeValue = object.value(QStringLiteral("exposeLan"));
        if (!exposeValue.isUndefined() && !exposeValue.isNull()) {
            if (!exposeValue.isBool()) {
                return invalidField(QStringLiteral("exposeLan"), QStringLiteral("boolean"));
            }
            port.exposeLan = exposeValue.toBool();
        }
        const QJsonValue labelValue = object.value(QStringLiteral("label"));
        if (!labelValue.isUndefined() && !labelValue.isNull()) {
            if (!labelValue.isString()) {
                return invalidField(QStringLiteral("label"), QStringLiteral("string"));
            }
            port.label = labelValue.toString();
        }
        ports.append(port);
    }

    const MachineMeta machine = m_machines.setPorts(id, ports, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machineSerialList(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }
    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }

    QJsonArray ports;
    for (const SerialPortInfo &info : listHostSerialPorts()) {
        ports.append(info.toJson());
    }
    QJsonArray attached;
    for (const QString &path : machine.serialPorts) {
        attached.append(path);
    }
    QJsonObject machineJson;
    machineJson.insert(QStringLiteral("id"), machine.id);
    machineJson.insert(QStringLiteral("name"), machine.name);
    machineJson.insert(QStringLiteral("serialPorts"), attached);
    return QJsonObject{{QStringLiteral("ports"), ports}, {QStringLiteral("machine"), machineJson}};
}

QJsonObject Daemon::machineSerialAttach(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }
    const QString path = params.value(QStringLiteral("path")).toString();
    if (path.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o caminho da porta serial."),
                                 QStringLiteral("path"));
        }
        return {};
    }

    const MachineMeta current = m_machines.get(id, error);
    if (current.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    QStringList paths = current.serialPorts;
    if (!paths.contains(path)) {
        paths.append(path);
    }
    const MachineMeta machine = m_machines.setSerialPorts(id, paths, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machineSerialDetach(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }
    const QString path = params.value(QStringLiteral("path")).toString();
    if (path.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o caminho da porta serial."),
                                 QStringLiteral("path"));
        }
        return {};
    }

    const MachineMeta current = m_machines.get(id, error);
    if (current.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    if (!current.serialPorts.contains(path)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("A porta serial não está anexada."), path);
        }
        return {};
    }
    QStringList paths = current.serialPorts;
    paths.removeAll(path);
    const MachineMeta machine = m_machines.setSerialPorts(id, paths, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machineUsbList(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }
    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }

    QJsonArray devices;
    for (const UsbDeviceInfo &info : listHostUsbDevices()) {
        devices.append(info.toJson());
    }
    QJsonArray attached;
    for (const MachineUsbDevice &device : machine.usbDevices) {
        attached.append(device.toJson());
    }
    QJsonObject machineJson;
    machineJson.insert(QStringLiteral("id"), machine.id);
    machineJson.insert(QStringLiteral("name"), machine.name);
    machineJson.insert(QStringLiteral("usbDevices"), attached);
    return QJsonObject{{QStringLiteral("devices"), devices},
                       {QStringLiteral("machine"), machineJson}};
}

QJsonObject Daemon::machineUsbAttach(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }
    const int bus = params.value(QStringLiteral("bus")).toInt();
    const int address = params.value(QStringLiteral("address")).toInt();
    if (bus <= 0 || address <= 0) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe bus e address do dispositivo USB."),
                                 QStringLiteral("bus/address"));
        }
        return {};
    }

    const MachineMeta current = m_machines.get(id, error);
    if (current.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    QVector<MachineUsbDevice> devices = current.usbDevices;
    bool exists = false;
    for (const MachineUsbDevice &device : devices) {
        if (device.bus == bus && device.address == address) {
            exists = true;
            break;
        }
    }
    if (!exists) {
        devices.append(MachineUsbDevice{bus, address, {}, {}, {}});
    }
    const MachineMeta machine = m_machines.setUsbDevices(id, devices, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machineUsbDetach(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }
    const int bus = params.value(QStringLiteral("bus")).toInt();
    const int address = params.value(QStringLiteral("address")).toInt();
    if (bus <= 0 || address <= 0) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe bus e address do dispositivo USB."),
                                 QStringLiteral("bus/address"));
        }
        return {};
    }

    const MachineMeta current = m_machines.get(id, error);
    if (current.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    QVector<MachineUsbDevice> devices;
    bool found = false;
    for (const MachineUsbDevice &device : current.usbDevices) {
        if (device.bus == bus && device.address == address) {
            found = true;
            continue;
        }
        devices.append(device);
    }
    if (!found) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("O dispositivo USB não está anexado."),
                                 QStringLiteral("%1:%2").arg(bus).arg(address));
        }
        return {};
    }
    const MachineMeta machine = m_machines.setUsbDevices(id, devices, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machineAudioSet(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }
    const QJsonValue enabledValue = params.value(QStringLiteral("enabled"));
    if (!enabledValue.isBool()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("O campo enabled deve ser booleano."),
                                 QStringLiteral("enabled"));
        }
        return {};
    }
    const MachineMeta machine = m_machines.setAudioEnabled(id, enabledValue.toBool(), error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machineSnapshotsList(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QVector<MachineSnapshot> snapshots = m_machines.snapshots(id, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonArray items;
    for (const MachineSnapshot &snapshot : snapshots) {
        items.append(snapshot.toJson());
    }
    return QJsonObject{{QStringLiteral("items"), items}};
}

QJsonObject Daemon::machineSnapshotCreate(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString name = params.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o nome do snapshot."));
        }
        return {};
    }

    const MachineSnapshot snapshot = m_machines.createSnapshot(id, name, error);
    if (error && !error->isEmpty()) {
        return {};
    }
    return snapshot.toJson();
}

QJsonObject Daemon::machineSnapshotRestore(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString name = params.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o nome do snapshot."));
        }
        return {};
    }

    const MachineMeta machine = m_machines.restoreSnapshot(id, name, error);
    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machineSnapshotRemove(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString name = params.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o nome do snapshot."));
        }
        return {};
    }

    if (!m_machines.removeSnapshot(id, name, error)) {
        return {};
    }
    return QJsonObject{{QStringLiteral("removed"), true}};
}

QJsonObject Daemon::machineClone(const QJsonObject &params, const Notify &notify,
                                 Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString name = params.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o nome da machine clonada."));
        }
        return {};
    }

    QString jobId;
    {
        std::lock_guard<std::mutex> lock(m_machineJobsMutex);
        jobId = QStringLiteral("machine-clone-%1-%2")
                    .arg(QDateTime::currentSecsSinceEpoch())
                    .arg(m_nextMachineJob++);
    }

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);
    {
        std::lock_guard<std::mutex> lock(m_machineJobsMutex);
        m_machineSubscriptions.insert(jobId, subscriptionId);
    }

    (void)QtConcurrent::run([this, jobId, subscriptionId, subscription, id, name, notify] {
        const auto progress = [jobId, notify](const QString &stage, int value) {
            if (!notify) {
                return;
            }
            QJsonObject payload;
            payload.insert(QStringLiteral("jobId"), jobId);
            payload.insert(QStringLiteral("stage"), stage);
            payload.insert(QStringLiteral("progress"), value);
            notify(QStringLiteral("machines.delta"), payload);
        };
        const auto cancelled = [subscription] {
            return subscription->cancel.load();
        };

        Error cloneError;
        MachineMeta machine = m_machines.clone(id, name, progress, cancelled, &cloneError);
        if (cloneError.isEmpty() && machine.id.isEmpty()) {
            cloneError = Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                     QStringLiteral("Falha ao clonar a machine."));
        }

        QJsonObject payload;
        payload.insert(QStringLiteral("jobId"), jobId);
        payload.insert(QStringLiteral("name"), name);
        if (cloneError.isEmpty()) {
            payload.insert(QStringLiteral("machine"), machine.toJson());
        } else {
            payload.insert(QStringLiteral("error"), cloneError.toJson());
        }
        if (notify) {
            notify(QStringLiteral("machines.done"), payload);
        }

        {
            std::lock_guard<std::mutex> lock(m_machineJobsMutex);
            m_machineSubscriptions.remove(jobId);
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("jobId"), jobId);
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    return result;
}

QJsonObject Daemon::machineDefault(const QJsonObject &params, Error *error)
{
    const QJsonValue nameValue = params.value(QStringLiteral("name"));
    if (!nameValue.isUndefined() && !nameValue.isNull()) {
        if (!nameValue.isString() || nameValue.toString().trimmed().isEmpty()) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("O nome da machine default é inválido."));
            }
            return {};
        }
        if (!m_machines.setDefaultMachine(nameValue.toString().trimmed(), error)) {
            return {};
        }
    }

    const QString name = m_machines.defaultMachine(error);
    if (error && !error->isEmpty()) {
        return {};
    }
    return QJsonObject{{QStringLiteral("name"), name}};
}

QJsonObject Daemon::machineCancel(const QJsonObject &params, Error *error)
{
    const QString jobId = params.value(QStringLiteral("jobId")).toString().trimmed();
    if (jobId.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o job da machine."));
        }
        return {};
    }

    QString subscriptionId;
    {
        std::lock_guard<std::mutex> lock(m_machineJobsMutex);
        subscriptionId = m_machineSubscriptions.value(jobId);
    }
    if (subscriptionId.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Job de machine não está em execução."), jobId);
        }
        return {};
    }

    std::shared_ptr<Subscription> subscription;
    {
        std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
        subscription = m_subscriptions.value(subscriptionId);
    }
    if (!subscription) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Job de machine não está em execução."), jobId);
        }
        return {};
    }

    subscription->cancel.store(true);
    QJsonObject result;
    result.insert(QStringLiteral("jobId"), jobId);
    result.insert(QStringLiteral("cancelled"), true);
    return result;
}

QJsonObject Daemon::machineAction(const QString &method, const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const bool force = params.value(QStringLiteral("force")).toBool();
    MachineMeta machine;
    if (method == QStringLiteral("machines.start")) {
        machine = m_machines.start(id, error);
    } else if (method == QStringLiteral("machines.stop")) {
        machine = m_machines.stop(id, force, error);
    } else if (force) {
        machine = m_machines.stop(id, true, error);
        if (error && !error->isEmpty()) {
            return {};
        }
        machine = m_machines.start(id, error);
    } else {
        machine = m_machines.restart(id, error);
    }

    if (machine.id.isEmpty() || (error && !error->isEmpty())) {
        return {};
    }
    return machine.toJson();
}

QJsonObject Daemon::machineDelete(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const bool force = params.value(QStringLiteral("force")).toBool();
    if (!m_machines.remove(id, force, error)) {
        return {};
    }
    return QJsonObject{{QStringLiteral("deleted"), true}};
}

QJsonObject Daemon::machineLogs(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty()) {
        return {};
    }

    int tail = params.value(QStringLiteral("tail")).toInt(200);
    if (tail <= 0) {
        tail = 200;
    }
    tail = qBound(1, tail, 2000);

    const QString directory = Paths::machineDir(machine.id);
    if (directory.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                 QStringLiteral("Identificador de machine inválido."));
        }
        return {};
    }

    bool consoleExists = false;
    if (!checkMachineConsoleFile(directory, &consoleExists, error)) {
        return {};
    }
    if (!consoleExists) {
        return QJsonObject{{QStringLiteral("lines"), QJsonArray{}}};
    }

    const QString path = directory + QStringLiteral("/console.log");
    QByteArray consoleData;
    qint64 start = 0;
    if (!readMachineConsoleTail(path, kMachineConsoleMaxBytes, &consoleData, &start, error)) {
        return {};
    }
    start = qMax<qint64>(0, start - consoleData.size());

    QString text = QString::fromUtf8(consoleData);
    if (start > 0) {
        const int newline = text.indexOf(QLatin1Char('\n'));
        if (newline >= 0) {
            text.remove(0, newline + 1);
        } else {
            text.clear();
        }
    }

    QStringList lines = text.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();
    }
    for (QString &line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
    }
    if (lines.size() > tail) {
        lines = lines.mid(lines.size() - tail);
    }

    QJsonArray array;
    for (const QString &line : lines) {
        array.append(line);
    }
    return QJsonObject{{QStringLiteral("lines"), array}};
}

QJsonObject Daemon::machineConsoleRead(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty()) {
        return {};
    }

    const QString directory = Paths::machineDir(machine.id);
    if (directory.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                 QStringLiteral("Identificador de machine inválido."));
        }
        return {};
    }

    bool consoleExists = false;
    if (!checkMachineConsoleFile(directory, &consoleExists, error)) {
        return {};
    }

    QByteArray data;
    if (consoleExists) {
        const QString path = directory + QStringLiteral("/console.log");
        if (!readMachineConsoleTail(path, consoleTailBytes(params), &data, nullptr, error)) {
            return {};
        }
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), machine.id);
    // O serial é binário; o stream expõe uma aproximação textual em UTF-8
    // (bytes inválidos viram U+FFFD).
    result.insert(QStringLiteral("data"), QString::fromUtf8(data));
    return result;
}

QJsonObject Daemon::subscribeMachineConsole(const QJsonObject &params, const Notify &notify,
                                            Error *error)
{
    if (error) {
        *error = {};
    }

    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty()) {
        return {};
    }

    const QString directory = Paths::machineDir(machine.id);
    if (directory.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                 QStringLiteral("Identificador de machine inválido."));
        }
        return {};
    }

    bool consoleExists = false;
    if (!checkMachineConsoleFile(directory, &consoleExists, error)) {
        return {};
    }

    const QString path = directory + QStringLiteral("/console.log");
    QByteArray initial;
    qint64 offset = 0;
    if (consoleExists
        && !readMachineConsoleTail(path, consoleTailBytes(params), &initial, &offset, error)) {
        return {};
    }
    bool pendingReset = !consoleExists;

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);

    (void) QtConcurrent::run([this, machineId = machine.id, path, offset, pendingReset, initial,
                              subscriptionId, subscription, notify]() mutable {
        // O serial entrega bytes crus; um caractere UTF-8 pode ser dividido
        // entre dois chunks. O decoder mantém o estado entre deltas para não
        // produzir U+FFFD no meio de uma sequência.
        QStringDecoder decoder(QStringDecoder::Utf8);
        const auto sendDelta = [&](const QByteArray &bytes, bool reset) {
            if (reset) {
                decoder = QStringDecoder(QStringDecoder::Utf8);
            }
            QJsonObject payload;
            payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
            payload.insert(QStringLiteral("id"), machineId);
            payload.insert(QStringLiteral("data"), QString(decoder(bytes)));
            payload.insert(QStringLiteral("reset"), reset);
            notify(QStringLiteral("machines.console.delta"), payload);
        };
        const auto sendError = [&](const Error &streamError) {
            QJsonObject payload;
            payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
            payload.insert(QStringLiteral("id"), machineId);
            payload.insert(QStringLiteral("error"), streamError.toJson());
            notify(QStringLiteral("machines.console.error"), payload);
        };

        sendDelta(initial, true);

        while (!subscription->cancel.load()) {
            for (int tick = 0; tick < 3; ++tick) {
                if (subscription->cancel.load()) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (subscription->cancel.load()) {
                break;
            }

            bool exists = false;
            Error checkError;
            if (!checkMachineConsoleFile(QFileInfo(path).absolutePath(), &exists, &checkError)) {
                sendError(checkError);
                break;
            }
            if (!exists) {
                offset = 0;
                pendingReset = true;
                continue;
            }

            QByteArray chunk;
            qint64 nextOffset = offset;
            Error readError;
            if (!readMachineConsoleChunk(path, offset, kMachineConsoleMaxBytes, &chunk,
                                         &nextOffset, &readError)) {
                sendError(readError);
                break;
            }
            if (nextOffset < offset) {
                offset = 0;
                pendingReset = true;
                continue;
            }
            if (chunk.isEmpty()) {
                continue;
            }

            const bool reset = pendingReset;
            pendingReset = false;
            offset = nextOffset;
            sendDelta(chunk, reset);
        }

        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    result.insert(QStringLiteral("id"), machine.id);
    return result;
}

QJsonObject Daemon::machineSsh(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    SshInfo info;
    if (!m_machines.sshInfo(id, &info, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("host"), info.host);
    result.insert(QStringLiteral("port"), info.port);
    result.insert(QStringLiteral("user"), info.user);
    result.insert(QStringLiteral("identityFile"), info.identityFile);
    result.insert(QStringLiteral("forwardAgent"), info.forwardAgent);
    return result;
}

QJsonObject Daemon::machineFilesList(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty()) {
        return {};
    }

    SshInfo info;
    if (!m_machines.sshInfo(machine.id, &info, error)) {
        return {};
    }

    const QString path = params.value(QStringLiteral("path")).toString(QStringLiteral("/"));
    QJsonArray items;
    if (!MachineFiles::list(info, path, &items, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("path"), path);
    result.insert(QStringLiteral("items"), items);
    return result;
}

QJsonObject Daemon::machineFilesDownload(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty()) {
        return {};
    }

    SshInfo info;
    if (!m_machines.sshInfo(machine.id, &info, error)) {
        return {};
    }

    const QString path = params.value(QStringLiteral("path")).toString();
    const QString destination = params.value(QStringLiteral("destination")).toString();
    if (!MachineFiles::download(info, path, destination, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("path"), path);
    result.insert(QStringLiteral("destination"), destination);
    return result;
}

QJsonObject Daemon::machineFilesUpload(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty()) {
        return {};
    }

    SshInfo info;
    if (!m_machines.sshInfo(machine.id, &info, error)) {
        return {};
    }

    const QString path = params.value(QStringLiteral("path")).toString();
    const QString sourcePath = params.value(QStringLiteral("sourcePath")).toString();
    if (!MachineFiles::upload(info, sourcePath, path, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("path"), path);
    result.insert(QStringLiteral("sourcePath"), sourcePath);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::machineStats(const QJsonObject &params, Error *error)
{
    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty()) {
        return {};
    }

    SshInfo info;
    if (!m_machines.sshInfo(machine.id, &info, error)) {
        return {};
    }
    return m_machineStats.sample(machine, info, error);
}

QJsonObject Daemon::subscribeMachineStats(const QJsonObject &params, const Notify &notify,
                                          Error *error)
{
    if (error) {
        *error = {};
    }

    const QString id = requireMachineId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const MachineMeta machine = m_machines.get(id, error);
    if (machine.id.isEmpty()) {
        return {};
    }

    SshInfo info;
    if (!m_machines.sshInfo(machine.id, &info, error)) {
        return {};
    }

    const int interval = qBound(1, params.value(QStringLiteral("intervalSeconds")).toInt(2), 10);
    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);

    (void)QtConcurrent::run([this, machine, info, interval, subscriptionId, subscription,
                             notify] {
        MachineStatsCollector collector;
        while (!subscription->cancel.load()) {
            Error sampleError;
            QJsonObject sample = collector.sample(machine, info, &sampleError);
            if (!sample.isEmpty()) {
                sample.insert(QStringLiteral("subscriptionId"), subscriptionId);
                sample.insert(QStringLiteral("id"), machine.id);
                notify(QStringLiteral("machines.stats.delta"), sample);
            } else if (!sampleError.isEmpty()) {
                QJsonObject payload;
                payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
                payload.insert(QStringLiteral("id"), machine.id);
                payload.insert(QStringLiteral("error"), sampleError.toJson());
                notify(QStringLiteral("machines.stats.error"), payload);
                break;
            }

            for (int tick = 0; tick < interval * 10; ++tick) {
                if (subscription->cancel.load()) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    result.insert(QStringLiteral("id"), machine.id);
    return result;
}

QJsonObject Daemon::systemHello() const
{
    QJsonObject podman;
    podman.insert(QStringLiteral("apiVersion"), m_podman.apiVersion());
    podman.insert(QStringLiteral("socket"), m_podman.socketPath());
    podman.insert(QStringLiteral("mode"), engineMode());

    QJsonObject result;
    result.insert(QStringLiteral("apiVersion"), 1);
    result.insert(QStringLiteral("daemonVersion"), QStringLiteral(PODSTACK_VERSION));
    result.insert(QStringLiteral("podman"), podman);
    return result;
}

QJsonObject Daemon::systemStatus()
{
    Error error;
    const bool ok = m_podman.ping(&error);

    QJsonObject podman;
    podman.insert(QStringLiteral("apiVersion"), m_podman.apiVersion());
    podman.insert(QStringLiteral("socket"), m_podman.socketPath());
    podman.insert(QStringLiteral("mode"), engineMode());

    QJsonObject result;
    result.insert(QStringLiteral("state"), ok ? QStringLiteral("ok") : QStringLiteral("error"));
    result.insert(QStringLiteral("engine"),
                  ok ? QStringLiteral("running") : QStringLiteral("unavailable"));
    result.insert(QStringLiteral("daemonVersion"), QStringLiteral(PODSTACK_VERSION));
    result.insert(QStringLiteral("podman"), podman);
    if (!ok) {
        result.insert(QStringLiteral("error"), error.toJson());
    }
    return result;
}

QJsonObject Daemon::systemDoctor()
{
    QJsonArray checks;

    const bool socketExists = QFile::exists(Paths::controlSocket());
    QJsonObject socketCheck;
    socketCheck.insert(QStringLiteral("id"), QStringLiteral("daemon.socket"));
    socketCheck.insert(QStringLiteral("status"),
                       socketExists ? QStringLiteral("ok") : QStringLiteral("fail"));
    socketCheck.insert(QStringLiteral("detail"), Paths::controlSocket());
    checks.append(socketCheck);

    Error error;
    const bool podmanOk = m_podman.ping(&error);
    QJsonObject podmanCheck;
    podmanCheck.insert(QStringLiteral("id"), QStringLiteral("podman.socket"));
    podmanCheck.insert(QStringLiteral("status"),
                       podmanOk ? QStringLiteral("ok") : QStringLiteral("fail"));
    podmanCheck.insert(QStringLiteral("detail"),
                       podmanOk ? m_podman.socketPath() : error.message);
    if (!podmanOk && !error.remediation.isEmpty()) {
        podmanCheck.insert(QStringLiteral("remediation"), error.remediation);
    }
    checks.append(podmanCheck);

    QJsonObject result;
    result.insert(QStringLiteral("checks"), checks);
    return result;
}

QJsonObject Daemon::systemReport(const QJsonObject &params, Error *error)
{
    const Config config = Config::load();
    const bool redact = config.get(QStringLiteral("diagnostics.redact")).toBool(true);
    const bool includeLogs =
        params.value(QStringLiteral("includeLogs"))
            .toBool(config.get(QStringLiteral("diagnostics.includeLogs")).toBool(true));

    QJsonObject report;
    report.insert(QStringLiteral("generatedAt"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    report.insert(QStringLiteral("version"), QString::fromLatin1(PODSTACK_VERSION));
    report.insert(QStringLiteral("platform"), QSysInfo::prettyProductName());
    report.insert(QStringLiteral("kernel"), QSysInfo::kernelVersion());
    report.insert(QStringLiteral("qt"), QString::fromLatin1(qVersion()));
    report.insert(QStringLiteral("controlSocket"), Paths::controlSocket());

    const QJsonObject settings = config.flatValues();
    report.insert(QStringLiteral("settings"), redact ? Redaction::json(settings) : settings);
    report.insert(QStringLiteral("doctor"), systemDoctor());

    if (includeLogs) {
        QJsonArray logs;
        QFile file(Paths::stateDir() + QStringLiteral("/logs/podstackd.log"));
        if (file.open(QIODevice::ReadOnly)) {
            const QStringList lines =
                QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            const int start = qMax(0, lines.size() - 200);
            for (int index = start; index < lines.size(); ++index) {
                logs.append(redact ? Redaction::text(lines.at(index)) : lines.at(index));
            }
        }
        report.insert(QStringLiteral("logs"), logs);
    }

    const QByteArray payload = QJsonDocument(report).toJson(QJsonDocument::Indented);
    const QString reportsDir = Paths::stateDir() + QStringLiteral("/reports");
    if (!QDir().mkpath(reportsDir)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível criar %1.").arg(reportsDir));
        }
        return {};
    }
    QFile::setPermissions(reportsDir,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QString path =
        reportsDir +
        QStringLiteral("/podstack-report-%1.json")
            .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível escrever o report."));
        }
        return {};
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (!file.commit()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível concluir o report."));
        }
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("path"), path);
    result.insert(QStringLiteral("bytes"), payload.size());
    result.insert(
        QStringLiteral("sha256"),
        QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex()));
    return result;
}

QJsonObject Daemon::updatesCheck(const QJsonObject &params, Error *error)
{
    if (params.contains(QStringLiteral("force")) &&
        !params.value(QStringLiteral("force")).isBool()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("force deve ser booleano."));
        }
        return {};
    }
    const bool force = params.value(QStringLiteral("force")).toBool(false);
    const QString channel = params.value(QStringLiteral("channel")).toString();

    QJsonObject result = m_updates.check(channel, force, error);
    if (!error || error->isEmpty()) {
        if (result.value(QStringLiteral("status")).toString() == QStringLiteral("ok")) {
            std::lock_guard<std::mutex> lock(m_settingsMutex);
            Config config = Config::load();
            config.set(QStringLiteral("updates.lastCheck"),
                       result.value(QStringLiteral("checkedAt")).toString());
            QString saveError;
            (void) config.save(&saveError);
        }
    }
    return result;
}

QJsonObject Daemon::systemDisk()
{
    const QJsonValue thresholdValue =
        Config::load().get(QStringLiteral("notifications.diskThresholdPercent"));
    const int threshold = thresholdValue.isDouble() ? thresholdValue.toInt() : 90;

    QVector<DiskSpaceSample> samples;
    QJsonArray errors;
    QSet<quint64> devices;
    const auto add = [&](const QString &path, const QString &kind) {
        struct stat info{};
        if (::stat(path.toUtf8().constData(), &info) == 0) {
            const quint64 device = static_cast<quint64>(info.st_dev);
            if (devices.contains(device)) {
                return;
            }
            devices.insert(device);
        }
        DiskSpaceSample sample;
        QString error;
        if (sampleDiskSpace(path, kind, &sample, &error)) {
            samples.append(sample);
        } else {
            errors.append(error);
        }
    };

    add(QStringLiteral("/"), QStringLiteral("root"));
    add(Paths::dataDir(), QStringLiteral("data"));
    add(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
            QStringLiteral("/containers"),
        QStringLiteral("podman"));

    QJsonArray filesystems;
    for (const DiskSpaceSample &sample : samples) {
        QJsonObject item;
        item.insert(QStringLiteral("path"), sample.path);
        item.insert(QStringLiteral("kind"), sample.kind);
        item.insert(QStringLiteral("totalBytes"), static_cast<double>(sample.totalBytes));
        item.insert(QStringLiteral("freeBytes"), static_cast<double>(sample.freeBytes));
        item.insert(QStringLiteral("availableBytes"), static_cast<double>(sample.availableBytes));
        item.insert(QStringLiteral("usedPercent"), sample.usedPercent);
        filesystems.append(item);
    }

    QJsonObject result;
    result.insert(QStringLiteral("checkedAt"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    result.insert(QStringLiteral("thresholdPercent"), threshold);
    result.insert(QStringLiteral("low"), isDiskLow(samples, threshold));
    result.insert(QStringLiteral("filesystems"), filesystems);
    result.insert(QStringLiteral("errors"), errors);
    return result;
}

QJsonObject Daemon::systemEmulation()
{
    QJsonArray handlers;
    bool foreign = false;
    for (const BinfmtHandler &handler : detectBinfmtHandlers()) {
        QJsonObject item;
        item.insert(QStringLiteral("name"), handler.name);
        item.insert(QStringLiteral("interpreter"), handler.interpreter);
        item.insert(QStringLiteral("enabled"), handler.enabled);
        handlers.append(item);
        if (handler.name.startsWith(QStringLiteral("qemu-")) ||
            handler.name.startsWith(QStringLiteral("FEX-"))) {
            foreign = true;
        }
    }

    QJsonObject result;
    result.insert(QStringLiteral("hostArch"), hostArchitecture());
    result.insert(QStringLiteral("handlers"), handlers);
    result.insert(QStringLiteral("foreignEmulation"), foreign);
    return result;
}

QJsonObject Daemon::containersList(const QJsonObject &params, Error *error)
{
    const bool all = params.value(QStringLiteral("all")).toBool(false);
    const QString state = params.value(QStringLiteral("state")).toString();
    const QVector<ContainerInfo> containers = m_podman.listContainers(all, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonArray items;
    for (const ContainerInfo &container : containers) {
        if (!state.isEmpty() && state != QStringLiteral("all")) {
            if (state == QStringLiteral("running") && container.state != QStringLiteral("running")) {
                continue;
            }
            if (state == QStringLiteral("paused") && container.state != QStringLiteral("paused")) {
                continue;
            }
            if (state == QStringLiteral("stopped")
                && container.state != QStringLiteral("exited")
                && container.state != QStringLiteral("created")
                && container.state != QStringLiteral("dead")
                && container.state != QStringLiteral("stopped")) {
                continue;
            }
        }
        items.append(container.toJson());
    }

    QJsonObject result;
    result.insert(QStringLiteral("items"), items);
    result.insert(QStringLiteral("total"), items.size());
    return result;
}

QJsonObject Daemon::containerAction(const QString &method, const QJsonObject &params,
                                    Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    bool ok = false;
    if (method == QStringLiteral("containers.start")) {
        ok = m_podman.startContainer(id, error);
    } else if (method == QStringLiteral("containers.stop")) {
        ok = m_podman.stopContainer(
            id, params.value(QStringLiteral("timeoutSeconds")).toInt(10), error);
    } else if (method == QStringLiteral("containers.restart")) {
        ok = m_podman.restartContainer(
            id, params.value(QStringLiteral("timeoutSeconds")).toInt(10), error);
    } else if (method == QStringLiteral("containers.remove")) {
        ok = m_podman.removeContainer(id, params.value(QStringLiteral("force")).toBool(false),
                                      params.value(QStringLiteral("volumes")).toBool(false), error);
    } else if (method == QStringLiteral("containers.pause")) {
        ok = m_podman.pauseContainer(id, error);
    } else if (method == QStringLiteral("containers.unpause")) {
        ok = m_podman.unpauseContainer(id, error);
    } else if (method == QStringLiteral("containers.kill")) {
        ok = m_podman.killContainer(id, params.value(QStringLiteral("signal")).toString(), error);
    } else if (method == QStringLiteral("containers.rename")) {
        ok = m_podman.renameContainer(id, params.value(QStringLiteral("name")).toString(), error);
    } else if (method == QStringLiteral("containers.checkpoint")) {
        ok = m_podman.checkpointContainer(
            id, params.value(QStringLiteral("leaveRunning")).toBool(false),
            params.value(QStringLiteral("tcpEstablished")).toBool(false), error);
    } else if (method == QStringLiteral("containers.restore")) {
        ok = m_podman.restoreContainer(
            id, params.value(QStringLiteral("tcpEstablished")).toBool(false), error);
    }

    if (!ok) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::containerExec(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QJsonArray commandArray = params.value(QStringLiteral("cmd")).toArray();
    if (commandArray.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o comando a executar."));
        }
        return {};
    }

    QStringList command;
    for (const QJsonValue &value : commandArray) {
        command.append(value.toString());
    }

    ExecResult execResult;
    if (!m_podman.execOnce(id, command, &execResult, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("exitCode"), execResult.exitCode);
    result.insert(QStringLiteral("stdout"), QString::fromUtf8(execResult.output));
    return result;
}

QJsonObject Daemon::containerInspect(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QJsonObject json = m_podman.inspectContainer(id, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("json"), json);
    return result;
}

QJsonObject Daemon::containerCreate(const QJsonObject &params, Error *error)
{
    ContainerCreateSpec spec;
    spec.image = params.value(QStringLiteral("image")).toString();
    spec.name = params.value(QStringLiteral("name")).toString();
    spec.command = params.value(QStringLiteral("command")).toString();
    spec.workDir = params.value(QStringLiteral("workDir")).toString();
    spec.user = params.value(QStringLiteral("user")).toString();
    spec.remove = params.value(QStringLiteral("remove")).toBool(false);
    spec.init = params.value(QStringLiteral("init")).toBool(false);
    spec.privileged = params.value(QStringLiteral("privileged")).toBool(false);
    const bool exposeLan = params.value(QStringLiteral("exposeLan")).toBool(false);

    if (spec.image.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe a imagem do container."));
        }
        return {};
    }

    const QJsonObject env = params.value(QStringLiteral("env")).toObject();
    for (auto it = env.constBegin(); it != env.constEnd(); ++it) {
        spec.env.insert(it.key(), it.value().toString());
    }

    QMap<QString, QStringList> publishedByProtocol;
    const QJsonArray ports = params.value(QStringLiteral("ports")).toArray();
    for (const QJsonValue &value : ports) {
        const QJsonObject port = value.toObject();
        PortMapping mapping;
        mapping.hostPort = port.value(QStringLiteral("hostPort")).toInt();
        mapping.containerPort = port.value(QStringLiteral("containerPort")).toInt();
        mapping.protocol = port.value(QStringLiteral("protocol"))
                               .toString(QStringLiteral("tcp"))
                               .trimmed()
                               .toLower();
        mapping.hostIp = port.value(QStringLiteral("hostIp"))
                             .toString(exposeLan ? QStringLiteral("0.0.0.0")
                                                 : QStringLiteral("127.0.0.1"));
        if (exposeLan) {
            mapping.hostIp = QStringLiteral("0.0.0.0");
        }
        if (mapping.hostPort > 0 && mapping.containerPort > 0) {
            if (exposeLan) {
                if (mapping.protocol != QStringLiteral("tcp")
                    && mapping.protocol != QStringLiteral("udp")) {
                    if (error) {
                        *error = Error::make(
                            QStringLiteral("PODSTACK_INVALID_PARAMS"),
                            QStringLiteral("Expor na LAN aceita apenas portas tcp ou udp."));
                    }
                    return {};
                }
                if (mapping.hostPort > 65535) {
                    if (error) {
                        *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                             QStringLiteral("Porta do host inválida: %1.")
                                                 .arg(mapping.hostPort));
                    }
                    return {};
                }
                publishedByProtocol[mapping.protocol].append(
                    QString::number(mapping.hostPort));
            }
            spec.ports.append(mapping);
        }
    }

    if (exposeLan) {
        if (publishedByProtocol.isEmpty()) {
            if (error) {
                *error = Error::make(
                    QStringLiteral("PODSTACK_INVALID_PARAMS"),
                    QStringLiteral("Informe ao menos uma porta publicada para expor na LAN."));
            }
            return {};
        }
        int totalPorts = 0;
        for (const QStringList &hostPorts : publishedByProtocol) {
            totalPorts += hostPorts.size();
        }
        if (totalPorts > 32) {
            if (error) {
                *error = Error::make(
                    QStringLiteral("PODSTACK_INVALID_PARAMS"),
                    QStringLiteral("No máximo 32 portas podem ser expostas por container."));
            }
            return {};
        }
        if (!m_helper.available()) {
            if (error) {
                *error = helperRequiredError();
            }
            return {};
        }
    }

    const QJsonArray mounts = params.value(QStringLiteral("mounts")).toArray();
    for (const QJsonValue &value : mounts) {
        const QJsonObject mount = value.toObject();
        MountMapping mapping;
        mapping.source = mount.value(QStringLiteral("source")).toString();
        mapping.destination = mount.value(QStringLiteral("destination")).toString();
        mapping.readOnly = mount.value(QStringLiteral("readOnly")).toBool(false);
        if (!mapping.source.isEmpty() && !mapping.destination.isEmpty()) {
            spec.mounts.append(mapping);
        }
    }

    const QJsonObject labels = params.value(QStringLiteral("labels")).toObject();
    for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
        spec.labels.insert(it.key(), it.value().toString());
    }

    applyProxyEnvironment(spec);
    applyCaTrustMount(spec);

    const QString id = m_podman.createContainer(spec, error);
    if (id.isEmpty()) {
        return {};
    }

    const bool start = params.value(QStringLiteral("start")).toBool(true);
    if (start && !m_podman.startContainer(id, error)) {
        return {};
    }

    if (exposeLan) {
        QStringList openedProtocols;
        for (auto it = publishedByProtocol.constBegin(); it != publishedByProtocol.constEnd();
             ++it) {
            QString helperError;
            const bool ok = m_helper.call(
                QStringLiteral("OpenPorts"),
                QVariantList{QVariant::fromValue(it.value()), it.key()}, &helperError);
            if (!ok) {
                for (const QString &protocol : openedProtocols) {
                    QString ignored;
                    (void)m_helper.call(
                        QStringLiteral("ClosePorts"),
                        QVariantList{QVariant::fromValue(publishedByProtocol.value(protocol)),
                                     protocol},
                        &ignored);
                }
                Error cleanupError;
                (void)m_podman.removeContainer(id, true, false, &cleanupError);
                if (error) {
                    *error = Error::make(
                        QStringLiteral("PODSTACK_IO_ERROR"),
                        QStringLiteral("O container foi criado, mas o firewall não abriu as "
                                       "portas; a criação foi desfeita."),
                        helperError,
                        QStringLiteral("Verifique o firewalld/ufw e tente novamente."));
                }
                return {};
            }
            openedProtocols.append(it.key());
        }
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("started"), start);
    result.insert(QStringLiteral("exposeLan"), exposeLan);
    return result;
}

QJsonObject Daemon::containerExposeLan(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }
    const bool expose = params.value(QStringLiteral("expose")).toBool(false);

    const QJsonObject json = m_podman.inspectContainer(id, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    const QJsonObject bindings =
        json.value(QStringLiteral("HostConfig"))
            .toObject()
            .value(QStringLiteral("PortBindings"))
            .toObject();

    QMap<QString, QStringList> portsByProtocol;
    QJsonArray portSpecs;
    bool anyPort = false;
    bool wildcardOnly = true;
    for (auto it = bindings.constBegin(); it != bindings.constEnd(); ++it) {
        const QStringList keyParts = it.key().split(QLatin1Char('/'));
        if (keyParts.size() != 2) {
            continue;
        }
        const QString protocol = keyParts.at(1).trimmed().toLower();
        const QJsonArray hosts = it.value().toArray();
        for (const QJsonValue &hostValue : hosts) {
            const QJsonObject host = hostValue.toObject();
            const int hostPort = host.value(QStringLiteral("HostPort")).toString().toInt();
            if (hostPort <= 0) {
                continue;
            }
            anyPort = true;
            const QString hostIp = host.value(QStringLiteral("HostIp")).toString();
            if (!isWildcardHostIp(hostIp)) {
                wildcardOnly = false;
            }
            if (protocol != QStringLiteral("tcp") && protocol != QStringLiteral("udp")) {
                if (error) {
                    *error = Error::make(
                        QStringLiteral("PODSTACK_UNSUPPORTED"),
                        QStringLiteral("O firewall da LAN só gerencia portas tcp e udp."));
                }
                return {};
            }
            portsByProtocol[protocol].append(QString::number(hostPort));
            portSpecs.append(QStringLiteral("%1/%2").arg(hostPort).arg(protocol));
        }
    }

    if (!anyPort) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_INVALID_PARAMS"),
                QStringLiteral("O container não publica portas."),
                QString(), QStringLiteral("Publique portas ao criar o container."));
        }
        return {};
    }

    if (!wildcardOnly) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_UNSUPPORTED"),
                QStringLiteral("As portas publicadas não estão em 0.0.0.0. Recrie o container "
                               "com a opção 'Expor na LAN'."),
                QStringLiteral("O bind atual aponta para o loopback ou um IP específico."),
                QStringLiteral("Remova o container e crie-o novamente com 'Expor na LAN'."));
        }
        return {};
    }

    if (!m_helper.available()) {
        if (error) {
            *error = helperRequiredError();
        }
        return {};
    }

    const QString method = expose ? QStringLiteral("OpenPorts") : QStringLiteral("ClosePorts");
    QStringList changedProtocols;
    for (auto it = portsByProtocol.constBegin(); it != portsByProtocol.constEnd(); ++it) {
        QString helperError;
        const bool ok = m_helper.call(
            method, QVariantList{QVariant::fromValue(it.value()), it.key()}, &helperError);
        if (!ok) {
            if (expose) {
                for (const QString &protocol : changedProtocols) {
                    QString ignored;
                    (void)m_helper.call(
                        QStringLiteral("ClosePorts"),
                        QVariantList{QVariant::fromValue(portsByProtocol.value(protocol)),
                                     protocol},
                        &ignored);
                }
            }
            if (error) {
                *error = Error::make(
                    QStringLiteral("PODSTACK_IO_ERROR"),
                    QStringLiteral("Não foi possível %1 as portas no firewall.")
                        .arg(expose ? QStringLiteral("abrir") : QStringLiteral("fechar")),
                    helperError,
                    QStringLiteral("Verifique o firewalld/ufw e tente novamente."));
            }
            return {};
        }
        changedProtocols.append(it.key());
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("expose"), expose);
    result.insert(QStringLiteral("ports"), portSpecs);
    return result;
}

QJsonObject Daemon::containerDuplicate(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QJsonObject json = m_podman.inspectContainer(id, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    const QJsonObject config = json.value(QStringLiteral("Config")).toObject();
    const QString image = config.value(QStringLiteral("Image")).toString();
    if (image.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_RESPONSE"),
                                 QStringLiteral("Inspect sem imagem para duplicar."));
        }
        return {};
    }

    ContainerCreateSpec spec;
    spec.image = image;
    spec.name = params.value(QStringLiteral("name")).toString();
    if (spec.name.isEmpty()) {
        spec.name = id.left(12) + QStringLiteral("-copy");
    }
    spec.workDir = config.value(QStringLiteral("WorkingDir")).toString();
    spec.user = config.value(QStringLiteral("User")).toString();

    QStringList command;
    const QJsonArray commandArray = config.value(QStringLiteral("Cmd")).toArray();
    for (const QJsonValue &value : commandArray) {
        command.append(value.toString());
    }
    spec.command = command.join(QLatin1Char(' '));

    const QJsonArray env = config.value(QStringLiteral("Env")).toArray();
    for (const QJsonValue &value : env) {
        const QString entry = value.toString();
        const int equals = entry.indexOf(QLatin1Char('='));
        if (equals > 0) {
            spec.env.insert(entry.left(equals), entry.mid(equals + 1));
        }
    }

    applyProxyEnvironment(spec);

    const QJsonObject bindings = json.value(QStringLiteral("HostConfig"))
                                     .toObject()
                                     .value(QStringLiteral("PortBindings"))
                                     .toObject();
    for (auto it = bindings.constBegin(); it != bindings.constEnd(); ++it) {
        const QStringList keyParts = it.key().split(QLatin1Char('/'));
        if (keyParts.size() != 2) {
            continue;
        }
        const int containerPort = keyParts.at(0).toInt();
        const QJsonArray hosts = it.value().toArray();
        for (const QJsonValue &hostValue : hosts) {
            const QJsonObject host = hostValue.toObject();
            const int hostPort = host.value(QStringLiteral("HostPort")).toString().toInt();
            if (containerPort > 0) {
                spec.ports.append({hostPort, containerPort, keyParts.at(1),
                                   host.value(QStringLiteral("HostIp"))
                                       .toString(QStringLiteral("0.0.0.0"))});
            }
        }
    }

    const QJsonArray mounts = json.value(QStringLiteral("Mounts")).toArray();
    for (const QJsonValue &value : mounts) {
        const QJsonObject mount = value.toObject();
        const QString source = mount.value(QStringLiteral("Source")).toString();
        const QString destination = mount.value(QStringLiteral("Destination")).toString();
        if (!source.isEmpty() && !destination.isEmpty()) {
            spec.mounts.append({source, destination, !mount.value(QStringLiteral("RW")).toBool()});
        }
    }

    const QJsonObject labels = config.value(QStringLiteral("Labels")).toObject();
    for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
        spec.labels.insert(it.key(), it.value().toString());
    }
    applyCaTrustMount(spec);

    const QString newId = m_podman.createContainer(spec, error);
    if (newId.isEmpty()) {
        return {};
    }
    if (!m_podman.startContainer(newId, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), newId);
    result.insert(QStringLiteral("source"), id);
    result.insert(QStringLiteral("started"), true);
    return result;
}

QJsonObject Daemon::containerFilesList(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString path = params.value(QStringLiteral("path")).toString(QStringLiteral("/"));
    const QVector<FileEntry> entries = m_podman.listFiles(id, path, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonArray items;
    for (const FileEntry &entry : entries) {
        items.append(entry.toJson());
    }

    QJsonObject result;
    result.insert(QStringLiteral("path"), path);
    result.insert(QStringLiteral("entries"), items);
    return result;
}

QJsonObject Daemon::containerFilesDownload(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString path = params.value(QStringLiteral("path")).toString();
    const QString destination = params.value(QStringLiteral("destination")).toString();
    if (path.isEmpty() || destination.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o caminho e o destino."));
        }
        return {};
    }

    if (!m_podman.downloadFile(id, path, destination, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("path"), path);
    result.insert(QStringLiteral("destination"), destination);
    return result;
}

QJsonObject Daemon::containerFilesUpload(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString path = params.value(QStringLiteral("path")).toString();
    const QString sourcePath = params.value(QStringLiteral("sourcePath")).toString();
    if (path.isEmpty() || sourcePath.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o destino e o arquivo de origem."));
        }
        return {};
    }

    if (!m_podman.uploadFile(id, path, sourcePath, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("path"), path);
    result.insert(QStringLiteral("sourcePath"), sourcePath);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::subscribeLogs(const QJsonObject &params, const Notify &notify, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const int tail = params.value(QStringLiteral("tail")).toInt(200);
    const bool follow = params.value(QStringLiteral("follow")).toBool(true);

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);

    (void)QtConcurrent::run([this, id, tail, follow, subscriptionId, subscription, notify] {
        QByteArray buffer;
        Error streamError;

        const auto onData = [&](const QByteArray &data) {
            buffer.append(data);
            int newline = -1;
            while ((newline = buffer.indexOf('\n')) >= 0) {
                QByteArray line = buffer.left(newline);
                buffer.remove(0, newline + 1);
                if (line.endsWith('\r')) {
                    line.chop(1);
                }
                QJsonObject payload;
                payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
                payload.insert(QStringLiteral("line"), QString::fromUtf8(line));
                notify(QStringLiteral("containers.logs.delta"), payload);
                if (subscription->cancel.load()) {
                    return false;
                }
            }
            return !subscription->cancel.load();
        };

        m_podman.streamLogs(id, tail, follow, onData, &subscription->cancel, &streamError);
        if (!streamError.isEmpty() && !subscription->cancel.load()) {
            QJsonObject payload;
            payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
            payload.insert(QStringLiteral("error"), streamError.toJson());
            notify(QStringLiteral("containers.logs.error"), payload);
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    return result;
}

QJsonObject Daemon::subscribeStats(const QJsonObject &params, const Notify &notify, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const int interval = params.value(QStringLiteral("intervalSeconds")).toInt(1);
    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);

    (void)QtConcurrent::run([this, id, interval, subscriptionId, subscription, notify] {
        Error streamError;
        const auto onLine = [&](const QByteArray &line) {
            const QJsonDocument document = QJsonDocument::fromJson(line);
            if (document.isObject()) {
                QJsonObject payload = document.object();
                payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
                notify(QStringLiteral("containers.stats.delta"), payload);
            }
            return !subscription->cancel.load();
        };

        m_podman.streamStats(id, interval, onLine, &subscription->cancel, &streamError);
        if (!streamError.isEmpty() && !subscription->cancel.load()) {
            QJsonObject payload;
            payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
            payload.insert(QStringLiteral("error"), streamError.toJson());
            notify(QStringLiteral("containers.stats.error"), payload);
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    return result;
}

QJsonObject Daemon::subscribeSystemStats(const QJsonObject &params, const Notify &notify,
                                         Error *error)
{
    if (error) {
        *error = {};
    }

    const int interval = qBound(1, params.value(QStringLiteral("intervalSeconds")).toInt(2), 10);
    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);

    (void)QtConcurrent::run([this, interval, subscriptionId, subscription, notify] {
        StatsCollector collector;
        while (!subscription->cancel.load()) {
            Error sampleError;
            QJsonObject sample = collector.sample(m_podman, &sampleError);
            if (!sample.isEmpty()) {
                sample.insert(QStringLiteral("subscriptionId"), subscriptionId);
                notify(QStringLiteral("system.stats.delta"), sample);
            } else if (!sampleError.isEmpty()) {
                QJsonObject payload;
                payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
                payload.insert(QStringLiteral("error"), sampleError.toJson());
                notify(QStringLiteral("system.stats.error"), payload);
                break;
            }

            for (int tick = 0; tick < interval * 10; ++tick) {
                if (subscription->cancel.load()) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    return result;
}

QJsonObject Daemon::subscribeEvents(const Notify &notify, Error *error)
{
    if (error) {
        *error = {};
    }

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);

    (void)QtConcurrent::run([this, subscriptionId, subscription, notify] {
        QByteArray buffer;
        Error streamError;

        const auto onData = [&](const QByteArray &data) {
            buffer.append(data);
            int newline = -1;
            while ((newline = buffer.indexOf('\n')) >= 0) {
                const QByteArray line = buffer.left(newline);
                buffer.remove(0, newline + 1);
                const QJsonDocument document = QJsonDocument::fromJson(line);
                if (document.isObject()) {
                    notify(QStringLiteral("event"), document.object());
                }
                if (subscription->cancel.load()) {
                    return false;
                }
            }
            return !subscription->cancel.load();
        };

        m_podman.streamEvents(onData, &subscription->cancel, &streamError);
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    return result;
}

QJsonObject Daemon::imagesList(const QJsonObject &params, Error *error)
{
    const bool all = params.value(QStringLiteral("all")).toBool(false);
    QJsonArray items;
    for (const ImageInfo &image : m_podman.listImages(all, error)) {
        items.append(image.toJson());
    }
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("items"), items);
    result.insert(QStringLiteral("total"), items.size());
    return result;
}

QJsonObject Daemon::imageInspect(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QJsonObject json = m_podman.inspectImage(id, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("json"), json);
    return result;
}

QJsonObject Daemon::imageHistory(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    QJsonArray items;
    for (const ImageHistoryEntry &entry : m_podman.imageHistory(id, error)) {
        items.append(entry.toJson());
    }
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("items"), items);
    return result;
}

QJsonObject Daemon::imageRemove(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const bool force = params.value(QStringLiteral("force")).toBool(false);
    if (!m_podman.removeImage(id, force, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("removed"), true);
    return result;
}

QJsonObject Daemon::imagePrune(const QJsonObject &params, Error *error)
{
    const bool all = params.value(QStringLiteral("all")).toBool(false);
    qint64 reclaimed = 0;
    if (!m_podman.pruneImages(all, &reclaimed, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("reclaimedBytes"), static_cast<double>(reclaimed));
    return result;
}

QJsonObject Daemon::imageTag(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString repo = params.value(QStringLiteral("repo")).toString();
    if (repo.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o repositório da tag."));
        }
        return {};
    }

    if (!m_podman.tagImage(id, repo, params.value(QStringLiteral("tag")).toString(), error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("repo"), repo);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::imageUntag(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString repo = params.value(QStringLiteral("repo")).toString();
    if (repo.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o repositório da tag."));
        }
        return {};
    }

    if (!m_podman.untagImage(id, repo, params.value(QStringLiteral("tag")).toString(), error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("repo"), repo);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::imagePull(const QJsonObject &params, const Notify &notify, Error *error)
{
    const QString reference = params.value(QStringLiteral("reference")).toString();
    if (reference.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe a imagem para o pull."));
        }
        return {};
    }
    const QString platform = params.value(QStringLiteral("platform")).toString();

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);

    (void)QtConcurrent::run([this, reference, platform, subscriptionId, subscription, notify] {
        Error streamError;
        const auto onLine = [&](const QByteArray &line) {
            const QJsonDocument document = QJsonDocument::fromJson(line);
            if (!document.isObject()) {
                return !subscription->cancel.load();
            }
            QJsonObject payload = document.object();
            payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
            notify(QStringLiteral("images.pull.delta"), payload);
            return !subscription->cancel.load();
        };

        m_podman.pullImage(reference, platform, onLine, &subscription->cancel, &streamError);

        QJsonObject payload;
        payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
        payload.insert(QStringLiteral("reference"), reference);
        if (!streamError.isEmpty() && !subscription->cancel.load()) {
            payload.insert(QStringLiteral("error"), streamError.toJson());
            notify(QStringLiteral("images.pull.error"), payload);
        } else {
            payload.insert(QStringLiteral("cancelled"), subscription->cancel.load());
            notify(QStringLiteral("images.pull.done"), payload);
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    result.insert(QStringLiteral("reference"), reference);
    return result;
}

QJsonObject Daemon::imagePush(const QJsonObject &params, const Notify &notify, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }
    const QString destination = params.value(QStringLiteral("destination")).toString();
    if (destination.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o destino do push."));
        }
        return {};
    }

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);

    (void)QtConcurrent::run([this, id, destination, subscriptionId, subscription, notify] {
        Error streamError;
        const auto onLine = [&](const QByteArray &line) {
            const QJsonDocument document = QJsonDocument::fromJson(line);
            if (!document.isObject()) {
                return !subscription->cancel.load();
            }
            QJsonObject payload = document.object();
            payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
            notify(QStringLiteral("images.push.delta"), payload);
            return !subscription->cancel.load();
        };

        m_podman.pushImage(id, destination, onLine, &subscription->cancel, &streamError);

        QJsonObject payload;
        payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
        payload.insert(QStringLiteral("destination"), destination);
        if (!streamError.isEmpty() && !subscription->cancel.load()) {
            payload.insert(QStringLiteral("error"), streamError.toJson());
            notify(QStringLiteral("images.push.error"), payload);
        } else {
            payload.insert(QStringLiteral("cancelled"), subscription->cancel.load());
            notify(QStringLiteral("images.push.done"), payload);
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    result.insert(QStringLiteral("destination"), destination);
    return result;
}

QJsonObject Daemon::imageExport(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }
    const QString path = params.value(QStringLiteral("path")).toString();
    if (path.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o arquivo de destino."));
        }
        return {};
    }

    if (!m_podman.exportImage(id, path, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("path"), path);
    return result;
}

QJsonObject Daemon::imageLoad(const QJsonObject &params, Error *error)
{
    const QString path = params.value(QStringLiteral("path")).toString();
    if (path.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o arquivo da imagem."));
        }
        return {};
    }

    if (!m_podman.loadImage(path, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("path"), path);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::volumesList(Error *error)
{
    QJsonArray items;
    for (const VolumeInfo &volume : m_podman.listVolumes(error)) {
        items.append(volume.toJson());
    }
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("items"), items);
    result.insert(QStringLiteral("total"), items.size());
    return result;
}

QJsonObject Daemon::volumeCreate(const QJsonObject &params, Error *error)
{
    QMap<QString, QString> options;
    const QJsonObject optionsObject = params.value(QStringLiteral("options")).toObject();
    for (auto it = optionsObject.constBegin(); it != optionsObject.constEnd(); ++it) {
        options.insert(it.key(), it.value().toString());
    }

    const QJsonObject json =
        m_podman.createVolume(params.value(QStringLiteral("name")).toString(),
                              params.value(QStringLiteral("driver")).toString(),
                              labelsFromParams(params), options, error);
    if (error && !error->isEmpty()) {
        return {};
    }
    return json;
}

QJsonObject Daemon::volumeRemove(const QJsonObject &params, Error *error)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o nome do volume."));
        }
        return {};
    }

    const bool force = params.value(QStringLiteral("force")).toBool(false);
    if (!m_podman.removeVolume(name, force, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("removed"), true);
    return result;
}

QJsonObject Daemon::volumePrune(Error *error)
{
    qint64 reclaimed = 0;
    if (!m_podman.pruneVolumes(&reclaimed, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("reclaimedBytes"), static_cast<double>(reclaimed));
    return result;
}

QJsonObject Daemon::volumeClone(const QJsonObject &params, Error *error)
{
    const QString source = params.value(QStringLiteral("source")).toString();
    const QString destination = params.value(QStringLiteral("destination")).toString();
    if (!m_podman.cloneVolume(source, destination, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("source"), source);
    result.insert(QStringLiteral("destination"), destination);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::volumeExport(const QJsonObject &params, Error *error)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    const QString path = params.value(QStringLiteral("path")).toString();
    if (!m_podman.exportVolume(name, path, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("path"), path);
    return result;
}

QJsonObject Daemon::volumeImport(const QJsonObject &params, Error *error)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    const QString path = params.value(QStringLiteral("path")).toString();
    const bool force = params.value(QStringLiteral("force")).toBool(false);
    if (!m_podman.importVolume(name, path, force, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("path"), path);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::volumeImportHostPath(const QJsonObject &params, Error *error)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    const QString sourcePath = params.value(QStringLiteral("sourcePath")).toString();
    if (!m_podman.importHostPath(name, sourcePath, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("sourcePath"), sourcePath);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::networksList(Error *error)
{
    QJsonArray items;
    for (const NetworkInfo &network : m_podman.listNetworks(error)) {
        items.append(network.toJson());
    }
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("items"), items);
    result.insert(QStringLiteral("total"), items.size());
    return result;
}

QJsonObject Daemon::networkCreate(const QJsonObject &params, Error *error)
{
    const QJsonObject json =
        m_podman.createNetwork(params.value(QStringLiteral("name")).toString(),
                               params.value(QStringLiteral("driver")).toString(),
                               params.value(QStringLiteral("subnet")).toString(),
                               params.value(QStringLiteral("gateway")).toString(),
                               params.value(QStringLiteral("ipv6")).toBool(false),
                               labelsFromParams(params), error);
    if (error && !error->isEmpty()) {
        return {};
    }
    return json;
}

QJsonObject Daemon::networkRemove(const QJsonObject &params, Error *error)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o nome da rede."));
        }
        return {};
    }

    const bool force = params.value(QStringLiteral("force")).toBool(false);
    if (!m_podman.removeNetwork(name, force, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("removed"), true);
    return result;
}

QJsonObject Daemon::networkConnect(const QJsonObject &params, Error *error)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    const QString containerId = params.value(QStringLiteral("containerId")).toString();
    if (name.isEmpty() || containerId.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe a rede e o container."));
        }
        return {};
    }

    if (!m_podman.connectNetwork(name, containerId, aliasesFromParams(params), error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("containerId"), containerId);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::networkDisconnect(const QJsonObject &params, Error *error)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    const QString containerId = params.value(QStringLiteral("containerId")).toString();
    if (name.isEmpty() || containerId.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe a rede e o container."));
        }
        return {};
    }

    if (!m_podman.disconnectNetwork(name, containerId, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("containerId"), containerId);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::podsList(Error *error)
{
    QJsonArray items;
    for (const PodInfo &pod : m_podman.listPods(error)) {
        items.append(pod.toJson());
    }
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("items"), items);
    result.insert(QStringLiteral("total"), items.size());
    return result;
}

QJsonObject Daemon::podCreate(const QJsonObject &params, Error *error)
{
    const QJsonObject json = m_podman.createPod(params.value(QStringLiteral("name")).toString(),
                                                labelsFromParams(params), error);
    if (error && !error->isEmpty()) {
        return {};
    }
    return json;
}

QJsonObject Daemon::podAction(const QString &method, const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString action = method.section(QLatin1Char('.'), 1);
    if (!m_podman.podAction(id, action, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("action"), action);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::podRemove(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const bool force = params.value(QStringLiteral("force")).toBool(false);
    if (!m_podman.removePod(id, force, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("removed"), true);
    return result;
}

QJsonObject Daemon::systemDf(const QJsonObject &params, Error *error)
{
    const QJsonObject json = m_podman.systemDf(error);
    if (error && !error->isEmpty()) {
        return {};
    }

    return normalizeDiskUsage(json, params.value(QStringLiteral("detailed")).toBool(true));
}

QJsonObject Daemon::composeProjects(Error *error)
{
    QJsonArray items;
    for (const ComposeProjectInfo &project : m_compose.listProjects(error)) {
        items.append(project.toJson());
    }
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("items"), items);
    result.insert(QStringLiteral("total"), items.size());
    return result;
}

QJsonObject Daemon::composeAction(const QString &method, const QJsonObject &params,
                                  Error *error)
{
    const QString projectPath = params.value(QStringLiteral("projectPath")).toString();
    if (projectPath.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o diretório do projeto Compose."));
        }
        return {};
    }

    const QString action = method.section(QLatin1Char('.'), 1);
    const QString service = params.value(QStringLiteral("service")).toString();
    bool ok = false;
    if (action == QStringLiteral("up")) {
        ok = m_compose.up(projectPath, params.value(QStringLiteral("detach")).toBool(true),
                          error);
    } else if (action == QStringLiteral("down")) {
        ok = m_compose.down(projectPath, params.value(QStringLiteral("volumes")).toBool(false),
                            error);
    } else if (action == QStringLiteral("restart")) {
        ok = m_compose.restart(projectPath, service, error);
    } else if (action == QStringLiteral("stop")) {
        ok = m_compose.stop(projectPath, service, error);
    } else if (action == QStringLiteral("start")) {
        ok = m_compose.start(projectPath, service, error);
    } else if (error) {
        *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                             QStringLiteral("Ação Compose inválida."), action);
    }
    if (!ok) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("projectPath"), projectPath);
    result.insert(QStringLiteral("service"), service);
    result.insert(QStringLiteral("action"), action);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::subscribeComposeLogs(const QJsonObject &params, const Notify &notify,
                                         Error *error)
{
    const QString project = params.value(QStringLiteral("project")).toString();
    if (project.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o projeto Compose."));
        }
        return {};
    }
    const QString serviceFilter = params.value(QStringLiteral("service")).toString();

    const QVector<ContainerInfo> containers = m_podman.listContainers(true, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    QVector<QPair<QString, QString>> targets;
    for (const ContainerInfo &container : containers) {
        if (container.labels.value(QStringLiteral("com.docker.compose.project")) != project) {
            continue;
        }
        const QString service = container.labels.value(
            QStringLiteral("com.docker.compose.service"));
        if (!serviceFilter.isEmpty() && service != serviceFilter) {
            continue;
        }
        targets.append({container.id, service});
    }
    if (targets.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Nenhum container do projeto encontrado."),
                                 project);
        }
        return {};
    }

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);
    auto remaining = std::make_shared<std::atomic_int>(targets.size());

    for (const auto &target : targets) {
        const QString containerId = target.first;
        const QString service = target.second;
        (void)QtConcurrent::run([this, project, service, containerId, subscriptionId,
                                 subscription, remaining, notify] {
            QByteArray buffer;
            Error streamError;
            const auto onData = [&](const QByteArray &data) {
                buffer.append(data);
                int newline = -1;
                while ((newline = buffer.indexOf('\n')) >= 0) {
                    QByteArray line = buffer.left(newline);
                    buffer.remove(0, newline + 1);
                    if (line.endsWith('\r')) {
                        line.chop(1);
                    }
                    QJsonObject payload;
                    payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
                    payload.insert(QStringLiteral("project"), project);
                    payload.insert(QStringLiteral("service"), service);
                    payload.insert(QStringLiteral("containerId"), containerId);
                    payload.insert(QStringLiteral("line"), QString::fromUtf8(line));
                    notify(QStringLiteral("compose.logs.delta"), payload);
                    if (subscription->cancel.load()) {
                        return false;
                    }
                }
                return !subscription->cancel.load();
            };

            m_podman.streamLogs(containerId, 200, true, onData, &subscription->cancel,
                                &streamError);
            if (--(*remaining) == 0) {
                removeSubscription(subscriptionId);
            }
        });
    }

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    result.insert(QStringLiteral("project"), project);
    return result;
}

QJsonObject Daemon::buildStart(const QJsonObject &params, const Notify &notify, Error *error)
{
    const QString contextPath = params.value(QStringLiteral("contextPath")).toString();
    const QString tag = params.value(QStringLiteral("tag")).toString();
    const QString dockerfile =
        params.value(QStringLiteral("dockerfile")).toString(QStringLiteral("Dockerfile"));
    const QString trigger =
        params.value(QStringLiteral("trigger")).toString(QStringLiteral("gui"));

    if (contextPath.isEmpty() || tag.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o contexto e a tag do build."));
        }
        return {};
    }

    QStringList platforms;
    const QJsonValue platformsValue = params.value(QStringLiteral("platforms"));
    if (platformsValue.isArray()) {
        for (const QJsonValue &value : platformsValue.toArray()) {
            const QString platform = value.toString().trimmed();
            if (!platform.isEmpty() && !platforms.contains(platform)) {
                platforms.append(platform);
            }
        }
    } else if (platformsValue.isString()) {
        for (const QString &part :
             platformsValue.toString().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString platform = part.trimmed();
            if (!platform.isEmpty() && !platforms.contains(platform)) {
                platforms.append(platform);
            }
        }
    }
    // Compatibilidade com clientes antigos: `platform` single ou separado por vírgula.
    if (platforms.isEmpty()) {
        for (const QString &part : params.value(QStringLiteral("platform"))
                                       .toString()
                                       .split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString platform = part.trimmed();
            if (!platform.isEmpty() && !platforms.contains(platform)) {
                platforms.append(platform);
            }
        }
    }
    if (!platforms.isEmpty()) {
        Error platformError;
        if (!ManifestBuilder::validatePlatforms(platforms, &platformError)) {
            if (error) {
                *error = platformError;
            }
            return {};
        }
    }

    QMap<QString, QString> buildArgs;
    const QJsonObject argsObject = params.value(QStringLiteral("buildArgs")).toObject();
    for (auto it = argsObject.constBegin(); it != argsObject.constEnd(); ++it) {
        buildArgs.insert(it.key(), it.value().toString());
    }

    const QString buildId = QStringLiteral("build-%1-%2")
                                .arg(QDateTime::currentSecsSinceEpoch())
                                .arg(m_nextBuild++);
    const QString logDir = Paths::stateDir() + QStringLiteral("/builds");
    QDir().mkpath(logDir);
    const QString logPath = logDir + QLatin1Char('/') + buildId + QStringLiteral(".log");

    BuildRecord record;
    record.id = buildId;
    record.imageRef = tag;
    record.status = QStringLiteral("running");
    record.startedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    record.trigger = trigger;
    record.contextPath = contextPath;
    record.dockerfile = dockerfile;
    record.logPath = logPath;
    record.platforms = platforms;
    if (!m_buildStore.insert(record, error)) {
        return {};
    }

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);
    {
        std::lock_guard<std::mutex> lock(m_buildMutex);
        m_buildSubscriptions.insert(buildId, subscriptionId);
    }

    if (platforms.size() > 1) {
        // Manifest list: o Libpod REST não cria manifest lists, então usamos o CLI.
        (void)QtConcurrent::run([this, buildId, contextPath, dockerfile, tag, buildArgs,
                                 platforms, logPath, subscriptionId, subscription, notify] {
            QElapsedTimer timer;
            timer.start();

            QFile logFile(logPath);
            if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
                qCWarning(lcPodman) << "Não foi possível abrir o log do build:"
                                    << logFile.errorString();
            }

            const auto onLine = [&](const QString &line) {
                if (logFile.isOpen()) {
                    logFile.write(line.toUtf8());
                    logFile.write("\n");
                    logFile.flush();
                }
                QJsonObject payload;
                payload.insert(QStringLiteral("buildId"), buildId);
                payload.insert(QStringLiteral("line"), line);
                notify(QStringLiteral("builds.delta"), payload);
            };
            const auto cancelled = [subscription] {
                return subscription->cancel.load();
            };

            ManifestBuilder builder;
            Error buildError;
            const bool ok = builder.build(contextPath, dockerfile, tag, buildArgs, platforms,
                                          onLine, cancelled, &buildError);
            if (logFile.isOpen()) {
                logFile.close();
            }

            const bool wasCancelled = subscription->cancel.load();
            const QString status = wasCancelled
                                       ? QStringLiteral("cancelled")
                                       : (ok ? QStringLiteral("success")
                                             : QStringLiteral("failed"));
            const int durationMs = static_cast<int>(timer.elapsed());
            const QString errorText = ok ? QString() : buildError.message;

            m_buildStore.finish(buildId, status, errorText, durationMs);

            QJsonObject payload;
            payload.insert(QStringLiteral("buildId"), buildId);
            payload.insert(QStringLiteral("status"), status);
            payload.insert(QStringLiteral("imageId"), QString());
            payload.insert(QStringLiteral("error"), errorText);
            payload.insert(QStringLiteral("durationMs"), durationMs);
            payload.insert(QStringLiteral("steps"), 0);
            notify(QStringLiteral("builds.done"), payload);

            {
                std::lock_guard<std::mutex> lock(m_buildMutex);
                m_buildSubscriptions.remove(buildId);
            }
            removeSubscription(subscriptionId);
        });

        QJsonObject result;
        result.insert(QStringLiteral("buildId"), buildId);
        result.insert(QStringLiteral("subscriptionId"), subscriptionId);
        result.insert(QStringLiteral("logPath"), logPath);
        return result;
    }

    const QString platform = platforms.isEmpty() ? QString() : platforms.first();
    (void)QtConcurrent::run([this, buildId, contextPath, dockerfile, tag, buildArgs,
                             platform, logPath, subscriptionId, subscription, notify] {
        QElapsedTimer timer;
        timer.start();

        QFile logFile(logPath);
        if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
            qCWarning(lcPodman) << "Não foi possível abrir o log do build:"
                                << logFile.errorString();
        }
        int stepIndex = 0;
        bool sawStep = false;

        const auto onLine = [&](const QByteArray &line) {
            QString text;
            const QJsonDocument document = QJsonDocument::fromJson(line);
            if (document.isObject()) {
                const QJsonObject object = document.object();
                text = object.value(QStringLiteral("stream")).toString();
                const QString errorText = object.value(QStringLiteral("error")).toString();
                if (!errorText.isEmpty()) {
                    text = errorText;
                }
                if (text.isEmpty()) {
                    text = object.value(QStringLiteral("status")).toString();
                }
            }
            if (text.isEmpty()) {
                text = QString::fromUtf8(line);
            }
            if (!text.endsWith(QLatin1Char('\n'))) {
                text += QLatin1Char('\n');
            }
            if (logFile.isOpen()) {
                logFile.write(text.toUtf8());
                logFile.flush();
            }

            QJsonObject payload;
            payload.insert(QStringLiteral("buildId"), buildId);
            payload.insert(QStringLiteral("line"), text.trimmed());
            notify(QStringLiteral("builds.delta"), payload);

            if (text.startsWith(QStringLiteral("STEP "))) {
                const QRegularExpression expression(
                    QStringLiteral("^STEP\\s+(\\d+)/(\\d+):\\s*(.*)$"));
                const QRegularExpressionMatch match = expression.match(text.trimmed());
                if (match.hasMatch()) {
                    BuildStepRecord step;
                    step.id = buildId + QStringLiteral("-step-")
                              + match.captured(1);
                    step.buildId = buildId;
                    step.index = match.captured(1).toInt();
                    step.name = match.captured(3);
                    step.status = QStringLiteral("running");
                    step.startedAt =
                        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
                    step.logPath = logPath;
                    m_buildStore.addStep(step);
                    stepIndex = step.index;
                    sawStep = true;
                }
            }
            return !subscription->cancel.load();
        };

        QString imageId;
        Error buildError;
        const bool ok = m_podman.buildImage(contextPath, dockerfile, tag, buildArgs, platform,
                                            onLine, &subscription->cancel, &imageId,
                                            &buildError);
        if (logFile.isOpen()) {
            logFile.close();
        }

        const bool cancelled = subscription->cancel.load();
        const QString status = cancelled ? QStringLiteral("cancelled")
                                         : (ok ? QStringLiteral("success")
                                               : QStringLiteral("failed"));
        const int durationMs = static_cast<int>(timer.elapsed());
        const QString errorText = ok ? QString() : buildError.message;

        m_buildStore.finish(buildId, status, errorText, durationMs);
        m_buildStore.finishSteps(buildId,
                                 status == QStringLiteral("success")
                                     ? QStringLiteral("success")
                                     : QStringLiteral("failed"));

        QJsonObject payload;
        payload.insert(QStringLiteral("buildId"), buildId);
        payload.insert(QStringLiteral("status"), status);
        payload.insert(QStringLiteral("imageId"), imageId);
        payload.insert(QStringLiteral("error"), errorText);
        payload.insert(QStringLiteral("durationMs"), durationMs);
        payload.insert(QStringLiteral("steps"), sawStep ? stepIndex : 0);
        notify(QStringLiteral("builds.done"), payload);

        {
            std::lock_guard<std::mutex> lock(m_buildMutex);
            m_buildSubscriptions.remove(buildId);
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("buildId"), buildId);
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    result.insert(QStringLiteral("logPath"), logPath);
    return result;
}

QJsonObject Daemon::buildCancel(const QJsonObject &params, Error *error)
{
    const QString id = params.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o build."));
        }
        return {};
    }

    QString subscriptionId;
    {
        std::lock_guard<std::mutex> lock(m_buildMutex);
        subscriptionId = m_buildSubscriptions.value(id);
    }
    if (subscriptionId.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Build não está em execução."), id);
        }
        return {};
    }

    QJsonObject result = unsubscribe(QJsonObject{{QStringLiteral("subscriptionId"),
                                                  subscriptionId}});
    result.insert(QStringLiteral("id"), id);
    return result;
}

QJsonObject Daemon::buildsList(const QJsonObject &params, Error *error)
{
    const int limit = params.value(QStringLiteral("limit")).toInt(50);
    const int offset = params.value(QStringLiteral("offset")).toInt(0);

    QJsonArray items;
    for (const BuildRecord &record : m_buildStore.list(limit, offset, error)) {
        items.append(record.toJson());
    }
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("items"), items);
    result.insert(QStringLiteral("total"), items.size());
    return result;
}

QJsonObject Daemon::buildGet(const QJsonObject &params, Error *error)
{
    const QString id = params.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o build."));
        }
        return {};
    }

    const BuildRecord record = m_buildStore.get(id, error);
    if (error && !error->isEmpty()) {
        return {};
    }
    return record.toJson();
}

QJsonObject Daemon::buildRemove(const QJsonObject &params, Error *error)
{
    const QString id = params.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o build."));
        }
        return {};
    }

    {
        std::lock_guard<std::mutex> lock(m_buildMutex);
        const QString subscriptionId = m_buildSubscriptions.value(id);
        if (!subscriptionId.isEmpty()) {
            unsubscribe(QJsonObject{{QStringLiteral("subscriptionId"), subscriptionId}});
        }
    }

    const BuildRecord record = m_buildStore.get(id, error);
    if (error && !error->isEmpty()) {
        return {};
    }
    if (!record.logPath.isEmpty()) {
        QFile::remove(record.logPath);
    }
    if (!m_buildStore.remove(id, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("removed"), true);
    return result;
}

QJsonObject Daemon::settingsGet(const QJsonObject &params, Error *error)
{
    QString key;
    const QJsonValue keyValue = params.value(QStringLiteral("key"));
    if (!keyValue.isUndefined() && !keyValue.isNull()) {
        if (!keyValue.isString() || keyValue.toString().trimmed().isEmpty()) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("O campo key deve ser uma string."),
                                     QStringLiteral("key"));
            }
            return {};
        }
        key = keyValue.toString().trimmed();
        if (!Settings::find(key)) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_VALIDATION"),
                                     QStringLiteral("Chave de configuração desconhecida: %1.")
                                         .arg(key),
                                     key,
                                     QStringLiteral("Consulte `podstack config schema`."));
            }
            return {};
        }
    }

    std::lock_guard<std::mutex> lock(m_settingsMutex);
    const Config config = Config::load();
    return QJsonObject{{QStringLiteral("values"), config.flatValues(key)}};
}

QJsonObject Daemon::settingsSet(const QJsonObject &params, Error *error)
{
    const QJsonValue keyValue = params.value(QStringLiteral("key"));
    if (!keyValue.isString() || keyValue.toString().trimmed().isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("O campo key é obrigatório."),
                                 QStringLiteral("key"));
        }
        return {};
    }
    if (!params.contains(QStringLiteral("value"))) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("O campo value é obrigatório."),
                                 QStringLiteral("value"));
        }
        return {};
    }

    const QString key = keyValue.toString().trimmed();
    const QJsonValue value = params.value(QStringLiteral("value"));

    std::lock_guard<std::mutex> lock(m_settingsMutex);
    Config config = Config::load();

    Error validation;
    if (!Config::validate(key, value, &validation)) {
        if (error) {
            *error = validation;
        }
        return {};
    }

    const QJsonValue previous = config.get(key);
    config.set(key, value);

    QString saveError;
    if (!config.save(&saveError)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível salvar a configuração."),
                                 saveError,
                                 QStringLiteral("Verifique as permissões de ~/.config/podstack."));
        }
        return {};
    }

    const SettingDef *definition = Settings::find(key);
    const bool restartRequired = definition && definition->restartRequired
                                 && !(previous == value);
    QJsonObject result;
    result.insert(QStringLiteral("values"), config.flatValues());
    result.insert(QStringLiteral("restartRequired"), restartRequired);
    result.insert(QStringLiteral("key"), key);
    return result;
}

QJsonObject Daemon::settingsReset(const QJsonObject &params, Error *error)
{
    QString key;
    const QJsonValue keyValue = params.value(QStringLiteral("key"));
    if (!keyValue.isUndefined() && !keyValue.isNull()) {
        if (!keyValue.isString() || keyValue.toString().trimmed().isEmpty()) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("O campo key deve ser uma string."),
                                     QStringLiteral("key"));
            }
            return {};
        }
        key = keyValue.toString().trimmed();
    }

    std::lock_guard<std::mutex> lock(m_settingsMutex);
    Config config = Config::load();
    bool restartRequired = false;

    if (!key.isEmpty()) {
        const SettingDef *definition = Settings::find(key);
        if (!definition) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_VALIDATION"),
                                     QStringLiteral("Chave de configuração desconhecida: %1.")
                                         .arg(key),
                                     key,
                                     QStringLiteral("Consulte `podstack config schema`."));
            }
            return {};
        }
        const QJsonValue previous = config.get(key);
        config.reset(key);
        restartRequired = definition->restartRequired
                          && !(previous == definition->defaultValue);
    } else {
        const QJsonObject previous = config.flatValues();
        config.resetAll();
        for (const SettingDef &definition : Settings::definitions()) {
            if (definition.restartRequired
                && !(previous.value(definition.key) == definition.defaultValue)) {
                restartRequired = true;
                break;
            }
        }
    }

    QString saveError;
    if (!config.save(&saveError)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível salvar a configuração."),
                                 saveError,
                                 QStringLiteral("Verifique as permissões de ~/.config/podstack."));
        }
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("values"), config.flatValues());
    result.insert(QStringLiteral("restartRequired"), restartRequired);
    return result;
}

QJsonObject Daemon::settingsSchema() const
{
    return Settings::schemaJson();
}

QJsonObject Daemon::k8sStatus(const QJsonObject &params, Error *error)
{
    const QString name = params.value(QStringLiteral("cluster")).toString().trimmed();
    K8sCluster cluster = name.isEmpty() ? m_k8s.status(error) : m_k8s.status(name, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonObject result = cluster.toJson();
    result.insert(QStringLiteral("enabled"),
                  !cluster.state.isEmpty() && cluster.state != QStringLiteral("disabled"));
    result.insert(QStringLiteral("version"), QString());
    return result;
}

QJsonObject Daemon::k8sEnable(const QJsonObject &params, const Notify &notify, Error *error)
{
    K8sManager::EnableRequest request;

    const auto readInt = [&](const QString &key, std::optional<int> *target) {
        const QJsonValue value = params.value(key);
        if (value.isUndefined() || value.isNull()) {
            return true;
        }
        if (!value.isDouble()) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("%1 deve ser um número inteiro.").arg(key));
            }
            return false;
        }
        const double raw = value.toDouble();
        if (std::trunc(raw) != raw || raw < 1 || raw > 256) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("%1 fora do intervalo 1..256.").arg(key),
                                     QString::number(raw));
            }
            return false;
        }
        *target = static_cast<int>(raw);
        return true;
    };

    const auto readBytes = [&](const QString &key, std::optional<qint64> *target) {
        const QJsonValue value = params.value(key);
        if (value.isUndefined() || value.isNull()) {
            return true;
        }
        if (!value.isDouble()) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("%1 deve ser um número inteiro.").arg(key));
            }
            return false;
        }
        const double raw = value.toDouble();
        if (std::trunc(raw) != raw || raw < 1) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("%1 inválido.").arg(key), QString::number(raw));
            }
            return false;
        }
        *target = static_cast<qint64>(raw);
        return true;
    };

    if (!readInt(QStringLiteral("cpus"), &request.cpus) ||
        !readBytes(QStringLiteral("memoryBytes"), &request.memoryBytes) ||
        !readBytes(QStringLiteral("diskBytes"), &request.diskBytes)) {
        return {};
    }

    QString jobId;
    {
        std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
        jobId =
            QStringLiteral("k8s-%1-%2").arg(QDateTime::currentSecsSinceEpoch()).arg(m_nextK8sJob++);
    }

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);
    {
        std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
        m_k8sSubscriptions.insert(jobId, subscriptionId);
    }

    (void) QtConcurrent::run([this, jobId, subscriptionId, subscription, request, notify] {
        const auto progress = [jobId, notify](const QString &stage, int value) {
            if (!notify) {
                return;
            }
            QJsonObject payload;
            payload.insert(QStringLiteral("jobId"), jobId);
            payload.insert(QStringLiteral("stage"), stage);
            payload.insert(QStringLiteral("progress"), value);
            notify(QStringLiteral("k8s.delta"), payload);
        };
        const auto cancelled = [subscription] { return subscription->cancel.load(); };

        Error enableError;
        const K8sCluster cluster = m_k8s.enable(request, progress, cancelled, &enableError);
        if (enableError.isEmpty() && cluster.id.isEmpty()) {
            enableError = Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                      QStringLiteral("Falha ao habilitar o cluster."));
        }

        QJsonObject payload;
        payload.insert(QStringLiteral("jobId"), jobId);
        if (enableError.isEmpty()) {
            payload.insert(QStringLiteral("cluster"), cluster.toJson());
        } else {
            payload.insert(QStringLiteral("error"), enableError.toJson());
        }
        if (notify) {
            notify(QStringLiteral("k8s.done"), payload);
        }

        {
            std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
            m_k8sSubscriptions.remove(jobId);
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("jobId"), jobId);
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    return result;
}

QJsonObject Daemon::k8sDisable(const QJsonObject &params, const Notify &notify, Error *error)
{
    const bool force = params.value(QStringLiteral("force")).toBool(false);
    QString jobId;
    {
        std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
        jobId =
            QStringLiteral("k8s-%1-%2").arg(QDateTime::currentSecsSinceEpoch()).arg(m_nextK8sJob++);
    }
    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);
    {
        std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
        m_k8sSubscriptions.insert(jobId, subscriptionId);
    }

    (void) QtConcurrent::run([this, jobId, subscriptionId, subscription, force, notify] {
        const auto progress = [jobId, notify](const QString &stage, int value) {
            if (!notify) {
                return;
            }
            QJsonObject payload;
            payload.insert(QStringLiteral("jobId"), jobId);
            payload.insert(QStringLiteral("stage"), stage);
            payload.insert(QStringLiteral("progress"), value);
            notify(QStringLiteral("k8s.delta"), payload);
        };
        const auto cancelled = [subscription] { return subscription->cancel.load(); };
        Error disableError;
        const K8sCluster cluster = m_k8s.disable(force, progress, cancelled, &disableError);

        QJsonObject payload;
        payload.insert(QStringLiteral("jobId"), jobId);
        if (disableError.isEmpty()) {
            payload.insert(QStringLiteral("cluster"), cluster.toJson());
        } else {
            payload.insert(QStringLiteral("error"), disableError.toJson());
        }
        if (notify) {
            notify(QStringLiteral("k8s.done"), payload);
        }
        {
            std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
            m_k8sSubscriptions.remove(jobId);
        }
        removeSubscription(subscriptionId);
    });

    return QJsonObject{{QStringLiteral("jobId"), jobId},
                       {QStringLiteral("subscriptionId"), subscriptionId}};
}

QJsonObject Daemon::k8sReset(const QJsonObject &params, const Notify &notify, Error *error)
{
    Q_UNUSED(params);
    QJsonObject forceParams{{QStringLiteral("force"), true}};
    return k8sDisable(forceParams, notify, error);
}

QJsonObject Daemon::k8sCancel(const QJsonObject &params, Error *error)
{
    const QString jobId = params.value(QStringLiteral("jobId")).toString().trimmed();
    if (jobId.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o job do cluster."));
        }
        return {};
    }

    QString subscriptionId;
    {
        std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
        subscriptionId = m_k8sSubscriptions.value(jobId);
    }
    if (subscriptionId.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Job de cluster não está em execução."), jobId);
        }
        return {};
    }

    std::shared_ptr<Subscription> subscription;
    {
        std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
        subscription = m_subscriptions.value(subscriptionId);
    }
    if (!subscription) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Job de cluster não está em execução."), jobId);
        }
        return {};
    }

    subscription->cancel.store(true);
    QJsonObject result;
    result.insert(QStringLiteral("jobId"), jobId);
    result.insert(QStringLiteral("cancelled"), true);
    return result;
}

QJsonObject Daemon::k8sPodsList(const QJsonObject &params, Error *error)
{
    const QJsonValue namespaceValue = params.value(QStringLiteral("namespace"));
    if (!namespaceValue.isUndefined() && !namespaceValue.isNull() && !namespaceValue.isString()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("namespace deve ser uma string."));
        }
        return {};
    }
    const QJsonArray items = m_k8s.pods(namespaceValue.toString().trimmed(), error);
    if (error && !error->isEmpty()) {
        return {};
    }
    return QJsonObject{{QStringLiteral("items"), items}};
}

QJsonObject Daemon::k8sServicesList(const QJsonObject &params, Error *error)
{
    const QJsonValue namespaceValue = params.value(QStringLiteral("namespace"));
    if (!namespaceValue.isUndefined() && !namespaceValue.isNull() && !namespaceValue.isString()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("namespace deve ser uma string."));
        }
        return {};
    }
    const QJsonArray items = m_k8s.services(namespaceValue.toString().trimmed(), error);
    if (error && !error->isEmpty()) {
        return {};
    }
    return QJsonObject{{QStringLiteral("items"), items}};
}

QJsonObject Daemon::subscribeK8sLogs(const QJsonObject &params, const Notify &notify, Error *error)
{
    const QString ns = params.value(QStringLiteral("namespace")).toString().trimmed();
    const QString pod = params.value(QStringLiteral("pod")).toString().trimmed();
    const QString container = params.value(QStringLiteral("container")).toString().trimmed();
    const int tail = params.value(QStringLiteral("tail")).toInt(500);
    if (!K8sManager::validateLogsRequest(ns, pod, container, tail, error)) {
        return {};
    }

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);
    (void) QtConcurrent::run(
        [this, ns, pod, container, tail, subscriptionId, subscription, notify] {
            const auto onLine = [&notify, &ns, &pod, &container](const QString &line) {
                QJsonObject payload;
                payload.insert(QStringLiteral("namespace"), ns);
                payload.insert(QStringLiteral("pod"), pod);
                payload.insert(QStringLiteral("container"), container);
                payload.insert(QStringLiteral("line"), line);
                notify(QStringLiteral("k8s.logs.delta"), payload);
            };
            const auto cancelled = [subscription] { return subscription->cancel.load(); };

            Error streamError;
            m_k8s.streamLogs(ns, pod, container, tail, onLine, cancelled, &streamError);

            QJsonObject payload;
            payload.insert(QStringLiteral("namespace"), ns);
            payload.insert(QStringLiteral("pod"), pod);
            payload.insert(QStringLiteral("container"), container);
            if (!streamError.isEmpty()) {
                payload.insert(QStringLiteral("error"), streamError.toJson());
            }
            notify(QStringLiteral("k8s.logs.done"), payload);
            removeSubscription(subscriptionId);
        });

    return QJsonObject{{QStringLiteral("subscriptionId"), subscriptionId}};
}

QJsonObject Daemon::k8sPodDelete(const QJsonObject &params, Error *error)
{
    const QString ns = params.value(QStringLiteral("namespace")).toString().trimmed();
    const QString name = params.value(QStringLiteral("name")).toString().trimmed();
    if (!m_k8s.deleteResource(QStringLiteral("pod"), ns, name, error)) {
        return {};
    }
    return QJsonObject{{QStringLiteral("deleted"), true}};
}

QJsonObject Daemon::k8sServiceDelete(const QJsonObject &params, Error *error)
{
    const QString ns = params.value(QStringLiteral("namespace")).toString().trimmed();
    const QString name = params.value(QStringLiteral("name")).toString().trimmed();
    if (!m_k8s.deleteResource(QStringLiteral("service"), ns, name, error)) {
        return {};
    }
    return QJsonObject{{QStringLiteral("deleted"), true}};
}

QJsonObject Daemon::k8sPortForwardStart(const QJsonObject &params, const Notify &notify,
                                        Error *error)
{
    const QString resource =
        params.value(QStringLiteral("resource")).toString(QStringLiteral("pod")).trimmed();
    const QString ns = params.value(QStringLiteral("namespace")).toString().trimmed();
    const QString pod = params.value(QStringLiteral("pod")).toString().trimmed();
    const int localPort = params.value(QStringLiteral("localPort")).toInt();
    const int remotePort = params.value(QStringLiteral("remotePort")).toInt();
    if (!K8sManager::validatePortForward(resource, ns, pod, localPort, remotePort, error) ||
        !m_k8s.checkClusterReady(error)) {
        return {};
    }
    const bool exposeLan =
        Config::load().get(QStringLiteral("kubernetes.exposeServices")).toBool(false);

    QString id;
    {
        std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
        id = QStringLiteral("fwd-%1").arg(m_nextK8sForward++);
    }
    auto subscription = std::make_shared<Subscription>();
    {
        std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
        m_k8sForwards.insert(id, subscription);
    }

    (void) QtConcurrent::run([this, id, resource, ns, pod, localPort, remotePort, exposeLan,
                              subscription, notify] {
        const auto onLine = [&notify, &id](const QString &line) {
            QJsonObject payload;
            payload.insert(QStringLiteral("id"), id);
            payload.insert(QStringLiteral("line"), line);
            notify(QStringLiteral("k8s.portforward.delta"), payload);
        };
        const auto cancelled = [subscription] { return subscription->cancel.load(); };
        Error forwardError;
        m_k8s.portForward(resource, ns, pod, localPort, remotePort, exposeLan, onLine, cancelled,
                          &forwardError);

        QJsonObject payload;
        payload.insert(QStringLiteral("id"), id);
        if (!forwardError.isEmpty()) {
            payload.insert(QStringLiteral("error"), forwardError.toJson());
        }
        notify(QStringLiteral("k8s.portforward.done"), payload);
        {
            std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
            m_k8sForwards.remove(id);
        }
    });

    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("resource"), resource},
                       {QStringLiteral("namespace"), ns},
                       {QStringLiteral("pod"), pod},
                       {QStringLiteral("localPort"), localPort},
                       {QStringLiteral("remotePort"), remotePort}};
}

QJsonObject Daemon::k8sPortForwardStop(const QJsonObject &params, Error *error)
{
    const QString id = params.value(QStringLiteral("id")).toString().trimmed();
    std::shared_ptr<Subscription> subscription;
    {
        std::lock_guard<std::mutex> lock(m_k8sJobsMutex);
        subscription = m_k8sForwards.value(id);
    }
    if (!subscription) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Port-forward não está ativo."), id);
        }
        return {};
    }
    subscription->cancel.store(true);
    return QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("stopped"), true}};
}

QJsonObject Daemon::k8sContextUse(const QJsonObject &params, Error *error)
{
    const QString context = params.value(QStringLiteral("context")).toString().trimmed();
    if (!m_k8s.useContext(context, error)) {
        return {};
    }
    return QJsonObject{{QStringLiteral("context"), context}};
}

QJsonObject Daemon::setupStatus()
{
    return m_setup.status();
}

QJsonObject Daemon::setupCheck(const QJsonObject &params)
{
    const bool deep = params.value(QStringLiteral("deep")).toBool(false);
    QJsonArray checks;
    for (const SetupCheck &check : m_setup.check(deep)) {
        checks.append(check.toJson());
    }

    QJsonObject result;
    result.insert(QStringLiteral("checks"), checks);
    return result;
}

QJsonObject Daemon::setupValidate()
{
    return m_setup.validate();
}

QJsonObject Daemon::setupApply(const QJsonObject &params, const Notify &notify, Error *error)
{
    return m_setup.apply(params, notify, error);
}

QJsonObject Daemon::setupRemoveDocker(const QJsonObject &params, const Notify &notify,
                                      Error *error)
{
    return m_setup.removeDocker(params, notify, error);
}

QJsonObject Daemon::setupDockerImpact()
{
    return m_setup.dockerImpact();
}

QJsonObject Daemon::setupCancel(const QJsonObject &params)
{
    return m_setup.cancel(params);
}

QJsonObject Daemon::setupComplete(const QJsonObject &params, Error *error)
{
    const bool skipped = params.value(QStringLiteral("skipped")).toBool(false);
    if (!m_setup.complete(skipped, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("completed"), !skipped);
    result.insert(QStringLiteral("skipped"), skipped);
    return result;
}

QJsonObject Daemon::setupSubscribe(const QJsonObject &params, const Notify &notify, Error *error)
{
    const QString jobId = params.value(QStringLiteral("jobId")).toString();
    if (jobId.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o job de setup."));
        }
        return {};
    }

    const QJsonObject job = m_setup.jobStatus(jobId);
    if (job.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Job de setup não encontrado."));
        }
        return {};
    }

    m_setup.subscribe(jobId, notify);
    return QJsonObject{{QStringLiteral("jobId"), jobId}};
}

QJsonObject Daemon::unsubscribe(const QJsonObject &params)
{
    const QString subscriptionId = params.value(QStringLiteral("subscriptionId")).toString();

    std::shared_ptr<Subscription> subscription;
    {
        std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
        subscription = m_subscriptions.take(subscriptionId);
    }
    if (subscription) {
        subscription->cancel.store(true);
    }

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    result.insert(QStringLiteral("cancelled"), static_cast<bool>(subscription));
    return result;
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

QString Daemon::createSubscription(const std::shared_ptr<Subscription> &subscription)
{
    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    const QString id = QStringLiteral("sub-%1").arg(m_nextSubscription++);
    m_subscriptions.insert(id, subscription);
    return id;
}

void Daemon::removeSubscription(const QString &id)
{
    std::lock_guard<std::mutex> lock(m_subscriptionsMutex);
    m_subscriptions.remove(id);
}

} // namespace PodStack
