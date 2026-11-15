// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/controllers/MachineController.h"

#include "core/config/Paths.h"
#include "core/error/Error.h"
#include "core/rpc/RpcClient.h"
#include "gui/windows/TerminalWindow.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent>

namespace PodStack {
namespace {

QVariantMap machineVariant(const QJsonObject &machine)
{
    QVariantMap item = machine.toVariantMap();
    const QString distro = machine.value(QStringLiteral("distro")).toString();
    const QString version = machine.value(QStringLiteral("distroVersion")).toString();
    item.insert(QStringLiteral("distroLabel"),
                version.isEmpty() ? distro : distro + QLatin1Char(':') + version);
    const int sshPort = machine.value(QStringLiteral("sshPort")).toInt();
    QString sshUser = machine.value(QStringLiteral("sshUser")).toString();
    if (sshUser.isEmpty()) {
        sshUser = QStringLiteral("podstack");
    }
    item.insert(QStringLiteral("ssh"),
                sshPort > 0 ? QStringLiteral("%1@127.0.0.1:%2").arg(sshUser).arg(sshPort)
                            : QStringLiteral("-"));
    return item;
}

} // namespace

MachineController::MachineController(QObject *parent) : ControllerBase(parent)
{
    connect(&m_machinesWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &MachineController::onMachinesFinished);
    connect(&m_machineDistrosWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &MachineController::onMachineDistrosFinished);
    connect(&m_machineInfoWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &MachineController::onMachineInfoFinished);
    connect(&m_machineSerialWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &MachineController::onMachineSerialFinished);
    connect(&m_machineUsbWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &MachineController::onMachineUsbFinished);
    connect(&m_machineSnapshotsWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &MachineController::onMachineSnapshotsFinished);
    connect(&m_machineSshWatcher, &QFutureWatcher<QJsonObject>::finished, this, [this] {
        const QJsonObject result = m_machineSshWatcher.result();
        if (result.contains(QStringLiteral("error"))) {
            const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
            m_lastError = errorObject.value(QStringLiteral("message")).toString();
            emit lastErrorChanged();
            return;
        }

        const QString host = result.value(QStringLiteral("host")).toString();
        const int port = result.value(QStringLiteral("port")).toInt();
        const QString user = result.value(QStringLiteral("user")).toString();
        const QString identityFile = result.value(QStringLiteral("identityFile")).toString();
        const bool forwardAgent = result.value(QStringLiteral("forwardAgent")).toBool();
        if (host.isEmpty() || port <= 0 || user.isEmpty() || identityFile.isEmpty()) {
            m_lastError = QStringLiteral("Resposta SSH inválida do daemon.");
            emit lastErrorChanged();
            return;
        }

        QString title = m_machineSshTitle;
        if (title.isEmpty()) {
            title = QStringLiteral("machine");
        }
        QStringList arguments;
        if (forwardAgent) {
            arguments << QStringLiteral("-A");
        }
        arguments << QStringLiteral("-i") << identityFile << QStringLiteral("-p")
                  << QString::number(port) << QStringLiteral("-o")
                  << QStringLiteral("IdentitiesOnly=yes") << QStringLiteral("-o")
                  << QStringLiteral("StrictHostKeyChecking=accept-new") << QStringLiteral("-o")
                  << QStringLiteral("UserKnownHostsFile=%1/known_hosts")
                         .arg(QFileInfo(identityFile).absolutePath())
                  << QStringLiteral("%1@%2").arg(user, host);
        if (isSignalConnected(QMetaMethod::fromSignal(&MachineController::terminalRequested))) {
            emit terminalRequested(title, QStringLiteral("ssh"), arguments);
        } else {
            // Fallback para uso do Backend sem a MainWindow.
            auto *window = new TerminalWindow(title, QStringLiteral("ssh"), arguments);
            window->show();
            window->raise();
            window->activateWindow();
        }
    });

    connect(&m_machineCreateStream, &StreamClient::subscriptionReady, this,
            [this](const QJsonObject &result) {
                const QString jobId = result.value(QStringLiteral("jobId")).toString();
                if (!jobId.isEmpty()) {
                    m_machineCreateJobId = jobId;
                    emit createStateChanged();
                }
            });

    connect(&m_machineCreateStream, &StreamClient::machineCreateEvent, this,
            [this](const QJsonObject &event) {
                const QString jobId = event.value(QStringLiteral("jobId")).toString();
                if (!jobId.isEmpty()) {
                    m_machineCreateJobId = jobId;
                }
                const QString stage = event.value(QStringLiteral("stage")).toString();
                if (!stage.isEmpty()) {
                    m_machineCreateStage = stage;
                }
                m_machineCreateProgress =
                    qMax(m_machineCreateProgress,
                         qBound(0, event.value(QStringLiteral("progress")).toInt(), 100));
                emit createStateChanged();
            });

    connect(&m_machineCreateStream, &StreamClient::machineCreateFinished, this,
            [this](const QJsonObject &result) {
                const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
                const QString code = errorObject.value(QStringLiteral("code")).toString();
                m_machineCreateActive = false;
                m_machineCreateProgress = 0;
                m_machineCreateError.clear();
                if (!errorObject.isEmpty() && code != QStringLiteral("PODSTACK_CANCELLED")) {
                    m_machineCreateStage = QStringLiteral("error");
                    m_machineCreateError = errorObject.value(QStringLiteral("message")).toString();
                    const QString remediation =
                        errorObject.value(QStringLiteral("remediation")).toString();
                    if (!remediation.isEmpty()) {
                        m_machineCreateError += QLatin1Char(' ') + remediation;
                    }
                } else if (code == QStringLiteral("PODSTACK_CANCELLED")) {
                    m_machineCreateStage = QStringLiteral("cancelled");
                } else {
                    m_machineCreateStage = QStringLiteral("done");
                }
                emit createStateChanged();
                refresh();
                if (!m_machineCreateError.isEmpty()) {
                    m_lastError = m_machineCreateError;
                    emit lastErrorChanged();
                    emit actionFinished(false, m_machineCreateError);
                } else {
                    emit actionFinished(true, {});
                }
            });

    connect(&m_machineCreateStream, &StreamClient::error, this, [this](const QString &message) {
        if (!m_machineCreateActive) {
            return;
        }
        m_machineCreateActive = false;
        m_machineCreateStage = QStringLiteral("error");
        m_machineCreateError = message;
        emit createStateChanged();
        m_lastError = message;
        emit lastErrorChanged();
        emit actionFinished(false, message);
    });
}

void MachineController::onActionFinished(const QString &method, const QJsonObject &params,
                                         const QJsonObject &result)
{
    ControllerBase::onActionFinished(method, params, result);
    if (isErrorResult(result)) {
        // Portas, recursos, snapshots, serial e USB usam esta conclusão para
        // sair do estado "aplicando"; antes o sinal só saía em create/export.
        emit actionFinished(false, errorMessage(result));
        return;
    }
    refresh();
    const QString id = params.value(QStringLiteral("id")).toString();
    if (!id.isEmpty() && method != QStringLiteral("machines.delete")) {
        loadInfo(id);
    }
    if (!id.isEmpty() && method.startsWith(QStringLiteral("machines.snapshots."))) {
        refreshSnapshots(id);
    }
    emit actionFinished(true, {});
}

void MachineController::batch(const QStringList &ids, const QString &action, bool force)
{
    if (ids.isEmpty()) {
        return;
    }
    runJob(
        QStringLiteral("machines.batch"), {},
        [ids, action, force] {
            return runBatchCalls(QStringLiteral("machines."), QStringLiteral("id"), action, ids,
                                 force, {});
        },
        {}, 120000);
}

void MachineController::openSsh(const QString &id)
{
    if (id.isEmpty()) {
        m_lastError = QStringLiteral("Machine inválida.");
        emit lastErrorChanged();
        return;
    }
    if (m_machineSshWatcher.isRunning()) {
        return;
    }

    m_machineSshTitle = id;
    for (const QVariant &value : m_machines) {
        const QVariantMap machine = value.toMap();
        if (machine.value(QStringLiteral("id")).toString() == id) {
            const QString name = machine.value(QStringLiteral("name")).toString();
            if (!name.isEmpty()) {
                m_machineSshTitle = name;
            }
            break;
        }
    }

    m_machineSshWatcher.setFuture(QtConcurrent::run([id] {
        RpcClient client;
        Error error;
        const QJsonObject result = client.call(QStringLiteral("machines.ssh"),
                                               QJsonObject{{QStringLiteral("id"), id}}, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("error"), error.toJson()}};
        }
        return result;
    }));
}

void MachineController::refresh()
{
    if (m_machinesWatcher.isRunning()) {
        m_machinesRefreshAgain = true;
        return;
    }

    m_machinesWatcher.setFuture(QtConcurrent::run([] {
        RpcClient client;
        Error error;
        const QJsonObject result = client.call(QStringLiteral("machines.list"), {}, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("error"), error.toJson()}};
        }
        return result;
    }));
}

void MachineController::onMachinesFinished()
{
    const QJsonObject result = m_machinesWatcher.result();
    if (result.contains(QStringLiteral("error"))) {
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        m_lastError = errorObject.value(QStringLiteral("message")).toString();
        emit lastErrorChanged();
    } else {
        QVariantList items;
        const QJsonArray array = result.value(QStringLiteral("items")).toArray();
        items.reserve(array.size());
        for (const QJsonValue &value : array) {
            items.append(machineVariant(value.toObject()));
        }
        m_machines = items;
        emit itemsChanged();
    }

    if (m_machinesRefreshAgain) {
        m_machinesRefreshAgain = false;
        refresh();
    }
}

void MachineController::refreshDistros()
{
    if (m_machineDistrosLoaded || m_machineDistrosWatcher.isRunning()) {
        return;
    }

    m_machineDistrosWatcher.setFuture(QtConcurrent::run([] {
        RpcClient client;
        Error error;
        const QJsonObject result = client.call(QStringLiteral("machines.distros"), {}, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("error"), error.toJson()}};
        }
        return result;
    }));
}

void MachineController::onMachineDistrosFinished()
{
    const QJsonObject result = m_machineDistrosWatcher.result();
    if (result.contains(QStringLiteral("error"))) {
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        m_lastError = errorObject.value(QStringLiteral("message")).toString();
        emit lastErrorChanged();
        return;
    }

    QVariantList items;
    const QJsonArray array = result.value(QStringLiteral("items")).toArray();
    items.reserve(array.size());
    for (const QJsonValue &value : array) {
        const QJsonObject distro = value.toObject();
        QVariantMap item = distro.toVariantMap();
        const QString name = distro.value(QStringLiteral("distro")).toString();
        const QString version = distro.value(QStringLiteral("version")).toString();
        item.insert(QStringLiteral("label"),
                    version.isEmpty() ? name : name + QLatin1Char(':') + version);
        items.append(item);
    }
    m_machineDistros = items;
    m_machineDistrosLoaded = true;
    emit distrosChanged();
}

void MachineController::refreshSerialPorts(const QString &id)
{
    if (id.isEmpty() || m_machineSerialWatcher.isRunning()) {
        return;
    }
    m_machineSerialWatcher.setFuture(QtConcurrent::run([id] {
        RpcClient client;
        Error error;
        const QJsonObject result = client.call(QStringLiteral("machines.serial.list"),
                                               QJsonObject{{QStringLiteral("id"), id}}, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("__error"), error.message}};
        }
        return result;
    }));
}

void MachineController::onMachineSerialFinished()
{
    const QJsonObject result = m_machineSerialWatcher.result();
    if (result.contains(QStringLiteral("__error"))) {
        m_lastError = result.value(QStringLiteral("__error")).toString();
        emit lastErrorChanged();
        return;
    }
    m_machineSerialPorts = result.value(QStringLiteral("ports")).toArray().toVariantList();
    emit serialPortsChanged();
}

void MachineController::attachSerialPort(const QString &id, const QString &path)
{
    if (id.isEmpty() || path.isEmpty()) {
        return;
    }
    runAction(QStringLiteral("machines.serial.attach"),
              QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("path"), path}}, {}, 180000);
}

void MachineController::detachSerialPort(const QString &id, const QString &path)
{
    if (id.isEmpty() || path.isEmpty()) {
        return;
    }
    runAction(QStringLiteral("machines.serial.detach"),
              QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("path"), path}}, {}, 180000);
}

void MachineController::refreshUsbDevices(const QString &id)
{
    if (id.isEmpty() || m_machineUsbWatcher.isRunning()) {
        return;
    }
    m_machineUsbWatcher.setFuture(QtConcurrent::run([id] {
        RpcClient client;
        Error error;
        const QJsonObject result = client.call(QStringLiteral("machines.usb.list"),
                                               QJsonObject{{QStringLiteral("id"), id}}, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("__error"), error.message}};
        }
        return result;
    }));
}

void MachineController::onMachineUsbFinished()
{
    const QJsonObject result = m_machineUsbWatcher.result();
    if (result.contains(QStringLiteral("__error"))) {
        m_lastError = result.value(QStringLiteral("__error")).toString();
        emit lastErrorChanged();
        return;
    }
    m_machineUsbDevices = result.value(QStringLiteral("devices")).toArray().toVariantList();
    emit usbDevicesChanged();
}

void MachineController::attachUsbDevice(const QString &id, int bus, int address)
{
    if (id.isEmpty() || bus <= 0 || address <= 0) {
        return;
    }
    runAction(QStringLiteral("machines.usb.attach"),
              QJsonObject{{QStringLiteral("id"), id},
                          {QStringLiteral("bus"), bus},
                          {QStringLiteral("address"), address}},
              {}, 180000);
}

void MachineController::detachUsbDevice(const QString &id, int bus, int address)
{
    if (id.isEmpty() || bus <= 0 || address <= 0) {
        return;
    }
    runAction(QStringLiteral("machines.usb.detach"),
              QJsonObject{{QStringLiteral("id"), id},
                          {QStringLiteral("bus"), bus},
                          {QStringLiteral("address"), address}},
              {}, 180000);
}

void MachineController::setAudio(const QString &id, bool enabled)
{
    if (id.isEmpty()) {
        return;
    }
    runAction(QStringLiteral("machines.audio.set"),
              QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("enabled"), enabled}}, {},
              180000);
}

void MachineController::loadInfo(const QString &id)
{
    if (id.isEmpty()) {
        m_lastError = QStringLiteral("Machine inválida.");
        emit lastErrorChanged();
        return;
    }

    if (m_machineInfoWatcher.isRunning()) {
        m_pendingMachineInfoId = id;
        return;
    }

    m_machineInfoWatcher.setFuture(QtConcurrent::run([id] {
        RpcClient client;
        Error error;
        const QJsonObject result = client.call(QStringLiteral("machines.get"),
                                               QJsonObject{{QStringLiteral("id"), id}}, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("error"), error.toJson()}};
        }
        return result;
    }));
}

void MachineController::onMachineInfoFinished()
{
    const QJsonObject result = m_machineInfoWatcher.result();
    // O objeto Machine tem um campo "error" (string) que não é um erro de RPC.
    if (result.value(QStringLiteral("error")).isObject()) {
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        m_lastError = errorObject.value(QStringLiteral("message")).toString();
        emit lastErrorChanged();
    } else if (!result.value(QStringLiteral("id")).toString().isEmpty()) {
        m_machineInfo = machineVariant(result);
        emit infoChanged();
    }

    if (!m_pendingMachineInfoId.isEmpty()) {
        const QString next = m_pendingMachineInfoId;
        m_pendingMachineInfoId.clear();
        loadInfo(next);
    }
}

void MachineController::lifecycle(const QString &action, const QString &id, bool force)
{
    if (id.isEmpty() || (action != QStringLiteral("start") && action != QStringLiteral("stop") &&
                         action != QStringLiteral("restart"))) {
        m_lastError = QStringLiteral("Ação ou machine inválida.");
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
        return;
    }

    QJsonObject params;
    params.insert(QStringLiteral("id"), id);
    if (force) {
        params.insert(QStringLiteral("force"), true);
    }
    runAction(QStringLiteral("machines.") + action, params, {}, 120000);
}

void MachineController::remove(const QString &id, bool force)
{
    if (id.isEmpty()) {
        m_lastError = QStringLiteral("Machine inválida.");
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
        return;
    }

    runAction(QStringLiteral("machines.delete"),
              QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("force"), force}}, {},
              120000);
}

void MachineController::rename(const QString &id, const QString &name)
{
    if (id.isEmpty() || name.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("Informe a machine e o novo nome.");
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
        return;
    }

    runAction(QStringLiteral("machines.rename"),
              QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("name"), name.trimmed()}});
}

void MachineController::clone(const QString &id, const QString &name)
{
    const auto fail = [this](const QString &message) {
        m_machineCreateActive = false;
        m_machineCreateStage = QStringLiteral("error");
        m_machineCreateProgress = 0;
        m_machineCreateError = message;
        emit createStateChanged();
        m_lastError = message;
        emit lastErrorChanged();
        emit actionFinished(false, message);
    };

    if (m_machineCreateActive) {
        fail(QStringLiteral("Já existe uma operação de machine em andamento."));
        return;
    }
    if (id.isEmpty() || name.trimmed().isEmpty()) {
        fail(QStringLiteral("Informe a machine de origem e o nome da cópia."));
        return;
    }

    m_machineCreateActive = true;
    m_machineJobKind = QStringLiteral("clone");
    m_machineCreateJobId.clear();
    m_machineCreateStage = QStringLiteral("validating");
    m_machineCreateProgress = 0;
    m_machineCreateError.clear();
    emit createStateChanged();

    QJsonObject params;
    params.insert(QStringLiteral("id"), id);
    params.insert(QStringLiteral("name"), name.trimmed());
    m_machineCreateStream.startMachineClone(params);
}

void MachineController::update(const QVariantMap &spec)
{
    const QString id = spec.value(QStringLiteral("id")).toString().trimmed();
    const bool hasCpus = spec.contains(QStringLiteral("cpus"));
    const bool hasMemory = spec.contains(QStringLiteral("memoryGiB"));
    const bool hasDisk = spec.contains(QStringLiteral("diskGiB"));
    const bool hasForwardAgent = spec.contains(QStringLiteral("forwardSshAgent"));
    if (id.isEmpty() || (!hasCpus && !hasMemory && !hasDisk && !hasForwardAgent)) {
        m_lastError = QStringLiteral("Informe a machine e ao menos um recurso ou o SSH agent.");
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
        return;
    }

    constexpr qint64 kGiB = 1024LL * 1024LL * 1024LL;
    QJsonObject params;
    params.insert(QStringLiteral("id"), id);
    if (hasCpus) {
        const int cpus = spec.value(QStringLiteral("cpus")).toInt();
        if (cpus < 1 || cpus > 256) {
            m_lastError = QStringLiteral("Número de CPUs fora do intervalo 1..256.");
            emit lastErrorChanged();
            emit actionFinished(false, m_lastError);
            return;
        }
        params.insert(QStringLiteral("cpus"), cpus);
    }
    if (hasMemory) {
        const int memoryGiB = spec.value(QStringLiteral("memoryGiB")).toInt();
        if (memoryGiB < 1) {
            m_lastError = QStringLiteral("Memória insuficiente (mínimo 1 GiB).");
            emit lastErrorChanged();
            emit actionFinished(false, m_lastError);
            return;
        }
        params.insert(QStringLiteral("memoryBytes"), static_cast<qint64>(memoryGiB) * kGiB);
    }
    if (hasDisk) {
        const int diskGiB = spec.value(QStringLiteral("diskGiB")).toInt();
        if (diskGiB < 1) {
            m_lastError = QStringLiteral("Disco insuficiente (mínimo 1 GiB).");
            emit lastErrorChanged();
            emit actionFinished(false, m_lastError);
            return;
        }
        params.insert(QStringLiteral("diskBytes"), static_cast<qint64>(diskGiB) * kGiB);
    }
    if (hasForwardAgent) {
        params.insert(QStringLiteral("forwardSshAgent"),
                      spec.value(QStringLiteral("forwardSshAgent")).toBool());
    }

    runAction(QStringLiteral("machines.update"), params, {}, 180000);
}

void MachineController::setPorts(const QString &id, const QVariantList &ports)
{
    const auto fail = [this](const QString &message) {
        m_lastError = message;
        emit lastErrorChanged();
        emit actionFinished(false, message);
    };

    if (id.isEmpty()) {
        fail(QStringLiteral("Machine inválida."));
        return;
    }

    QJsonArray array;
    for (const QVariant &value : ports) {
        const QVariantMap entry = value.toMap();
        bool hostOk = false;
        bool guestOk = false;
        const int hostPort = entry.value(QStringLiteral("hostPort")).toInt(&hostOk);
        const int guestPort = entry.value(QStringLiteral("guestPort")).toInt(&guestOk);
        if (!hostOk || hostPort < 1 || hostPort > 65535) {
            fail(QStringLiteral("Porta do host fora do intervalo 1..65535."));
            return;
        }
        if (!guestOk || guestPort < 1 || guestPort > 65535) {
            fail(QStringLiteral("Porta do guest fora do intervalo 1..65535."));
            return;
        }

        QString protocol = entry.value(QStringLiteral("protocol"), QStringLiteral("tcp"))
                               .toString()
                               .trimmed()
                               .toLower();
        if (protocol.isEmpty()) {
            protocol = QStringLiteral("tcp");
        }
        if (protocol != QStringLiteral("tcp") && protocol != QStringLiteral("udp")) {
            fail(QStringLiteral("Protocolo de porta inválido; use tcp ou udp."));
            return;
        }

        QJsonObject object;
        const QString portId = entry.value(QStringLiteral("id")).toString().trimmed();
        if (!portId.isEmpty()) {
            object.insert(QStringLiteral("id"), portId);
        }
        object.insert(QStringLiteral("hostPort"), hostPort);
        object.insert(QStringLiteral("guestPort"), guestPort);
        object.insert(QStringLiteral("protocol"), protocol);
        object.insert(QStringLiteral("exposeLan"),
                      entry.value(QStringLiteral("exposeLan")).toBool());
        object.insert(QStringLiteral("label"), entry.value(QStringLiteral("label")).toString());
        array.append(object);
    }

    runAction(QStringLiteral("machines.ports.set"),
              QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("ports"), array}}, {},
              180000);
}

void MachineController::createSnapshot(const QString &id, const QString &name)
{
    const auto fail = [this](const QString &message) {
        m_lastError = message;
        emit lastErrorChanged();
        emit actionFinished(false, message);
    };

    if (id.isEmpty()) {
        fail(QStringLiteral("Machine inválida."));
        return;
    }
    if (name.trimmed().isEmpty()) {
        fail(QStringLiteral("Informe o nome do snapshot."));
        return;
    }
    runAction(QStringLiteral("machines.snapshots.create"),
              QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("name"), name.trimmed()}}, {},
              180000);
}

void MachineController::restoreSnapshot(const QString &id, const QString &name)
{
    const auto fail = [this](const QString &message) {
        m_lastError = message;
        emit lastErrorChanged();
        emit actionFinished(false, message);
    };

    if (id.isEmpty()) {
        fail(QStringLiteral("Machine inválida."));
        return;
    }
    if (name.trimmed().isEmpty()) {
        fail(QStringLiteral("Informe o nome do snapshot."));
        return;
    }
    runAction(QStringLiteral("machines.snapshots.restore"),
              QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("name"), name.trimmed()}}, {},
              180000);
}

void MachineController::removeSnapshot(const QString &id, const QString &name)
{
    const auto fail = [this](const QString &message) {
        m_lastError = message;
        emit lastErrorChanged();
        emit actionFinished(false, message);
    };

    if (id.isEmpty()) {
        fail(QStringLiteral("Machine inválida."));
        return;
    }
    if (name.trimmed().isEmpty()) {
        fail(QStringLiteral("Informe o nome do snapshot."));
        return;
    }
    runAction(QStringLiteral("machines.snapshots.remove"),
              QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("name"), name.trimmed()}}, {},
              180000);
}

void MachineController::refreshSnapshots(const QString &id)
{
    if (id.isEmpty()) {
        m_machineSnapshotsError = QStringLiteral("Machine inválida.");
        emit snapshotsChanged();
        return;
    }

    if (m_machineSnapshotsWatcher.isRunning()) {
        m_pendingMachineSnapshotsId = id;
        return;
    }

    m_machineSnapshotsError.clear();
    m_machineSnapshotsWatcher.setFuture(QtConcurrent::run([id] {
        RpcClient client;
        Error error;
        const QJsonObject result = client.call(QStringLiteral("machines.snapshots.list"),
                                               QJsonObject{{QStringLiteral("id"), id}}, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("error"), error.toJson()}};
        }
        return result;
    }));
}

void MachineController::onMachineSnapshotsFinished()
{
    const QJsonObject result = m_machineSnapshotsWatcher.result();
    if (result.value(QStringLiteral("error")).isObject()) {
        m_machineSnapshotsError = result.value(QStringLiteral("error"))
                                      .toObject()
                                      .value(QStringLiteral("message"))
                                      .toString();
        if (m_machineSnapshotsError.isEmpty()) {
            m_machineSnapshotsError =
                QStringLiteral("Não foi possível listar os snapshots da machine.");
        }
    } else {
        m_machineSnapshotsError.clear();
        QVariantList items;
        const QJsonArray array = result.value(QStringLiteral("items")).toArray();
        items.reserve(array.size());
        for (const QJsonValue &value : array) {
            items.append(value.toObject().toVariantMap());
        }
        m_machineSnapshots = items;
    }
    emit snapshotsChanged();

    if (!m_pendingMachineSnapshotsId.isEmpty()) {
        const QString next = m_pendingMachineSnapshotsId;
        m_pendingMachineSnapshotsId.clear();
        refreshSnapshots(next);
    }
}

void MachineController::create(const QVariantMap &spec)
{
    const auto fail = [this](const QString &message) {
        m_machineCreateActive = false;
        m_machineCreateStage = QStringLiteral("error");
        m_machineCreateProgress = 0;
        m_machineCreateError = message;
        emit createStateChanged();
        m_lastError = message;
        emit lastErrorChanged();
        emit actionFinished(false, message);
    };

    const QString name = spec.value(QStringLiteral("name")).toString().trimmed();
    static const QRegularExpression namePattern(
        QStringLiteral("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
    if (name.isEmpty() || !namePattern.match(name).hasMatch()) {
        fail(QStringLiteral("Nome de machine inválido."));
        return;
    }

    const QString distro = spec.value(QStringLiteral("distro")).toString().trimmed();
    if (distro.isEmpty()) {
        fail(QStringLiteral("Informe a distro da machine."));
        return;
    }
    const bool isolated = spec.value(QStringLiteral("isolated")).toBool();

    const int cpus = spec.value(QStringLiteral("cpus"), 2).toInt();
    if (cpus < 1 || cpus > 256) {
        fail(QStringLiteral("Número de CPUs fora do intervalo 1..256."));
        return;
    }

    const int memoryGiB = spec.value(QStringLiteral("memoryGiB"), 4).toInt();
    const int diskGiB = spec.value(QStringLiteral("diskGiB"), 20).toInt();
    if (memoryGiB < 1) {
        fail(QStringLiteral("Memória insuficiente (mínimo 1 GiB)."));
        return;
    }
    if (diskGiB < 1) {
        fail(QStringLiteral("Disco insuficiente (mínimo 1 GiB)."));
        return;
    }

    constexpr qint64 kGiB = 1024LL * 1024LL * 1024LL;
    QJsonObject params;
    params.insert(QStringLiteral("name"), name);
    params.insert(QStringLiteral("distro"), distro);
    const QString version = spec.value(QStringLiteral("version")).toString().trimmed();
    if (!version.isEmpty()) {
        params.insert(QStringLiteral("version"), version);
    }
    params.insert(QStringLiteral("cpus"), cpus);
    params.insert(QStringLiteral("memoryBytes"), static_cast<qint64>(memoryGiB) * kGiB);
    params.insert(QStringLiteral("diskBytes"), static_cast<qint64>(diskGiB) * kGiB);
    const QString user = spec.value(QStringLiteral("user")).toString();
    if (!user.isEmpty()) {
        params.insert(QStringLiteral("user"), user);
    }
    const QString userData = spec.value(QStringLiteral("userData")).toString();
    if (!userData.isEmpty()) {
        params.insert(QStringLiteral("userData"), userData);
    }
    const QString password = spec.value(QStringLiteral("password")).toString();
    if (!password.isEmpty()) {
        params.insert(QStringLiteral("password"), password);
    }
    params.insert(QStringLiteral("isolated"), isolated);
    // O modo isolado implica rede restrita e sem SSH agent; o daemon reforça.
    params.insert(QStringLiteral("isolateNetwork"),
                  isolated || spec.value(QStringLiteral("isolateNetwork")).toBool());
    params.insert(QStringLiteral("forwardSshAgent"),
                  isolated ? false : spec.value(QStringLiteral("forwardSshAgent")).toBool());

    QJsonArray mounts;
    const QString mountsText = spec.value(QStringLiteral("mountsText")).toString();
    const QStringList mountLines = mountsText.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &rawLine : mountLines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const QStringList parts = line.split(QLatin1Char(':'));
        if (parts.size() < 2 || parts.size() > 3) {
            fail(QStringLiteral("Mount inválido: use origem:destino[:ro]."));
            return;
        }
        const QString source = parts.at(0).trimmed();
        const QString target = parts.at(1).trimmed();
        const QString mode = parts.size() == 3 ? parts.at(2).trimmed() : QString();
        if (source.isEmpty() || target.isEmpty() || !source.startsWith(QLatin1Char('/')) ||
            !target.startsWith(QLatin1Char('/'))) {
            fail(QStringLiteral("Mount inválido: origem e destino precisam ser absolutos."));
            return;
        }
        if (!mode.isEmpty() && mode != QStringLiteral("ro")) {
            fail(QStringLiteral("Mount inválido: o modo deve ser 'ro'."));
            return;
        }
        QJsonObject mount;
        mount.insert(QStringLiteral("source"), source);
        mount.insert(QStringLiteral("target"), target);
        mount.insert(QStringLiteral("readOnly"), mode == QStringLiteral("ro"));
        mounts.append(mount);
    }
    if (!mounts.isEmpty()) {
        if (isolated) {
            fail(QStringLiteral("O modo isolado não permite mounts do host."));
            return;
        }
        params.insert(QStringLiteral("mounts"), mounts);
    }

    m_machineCreateActive = true;
    m_machineJobKind = QStringLiteral("create");
    m_machineCreateJobId.clear();
    m_machineCreateStage = QStringLiteral("validating");
    m_machineCreateProgress = 0;
    m_machineCreateError.clear();
    emit createStateChanged();
    m_machineCreateStream.startMachineCreate(params);
}

void MachineController::cancelCreate()
{
    if (m_machineCreateJobId.isEmpty()) {
        return;
    }

    const QString jobId = m_machineCreateJobId;
    m_machineCreateStage = QStringLiteral("cancelling");
    emit createStateChanged();
    (void) QtConcurrent::run([this, jobId] {
        RpcClient client;
        Error error;
        client.call(QStringLiteral("machines.cancel"),
                    QJsonObject{{QStringLiteral("jobId"), jobId}}, &error, 30000);
        if (error.isEmpty()) {
            return;
        }
        // Sem isso o painel ficava em "cancelling" para sempre, sem explicar.
        const QString message = error.message;
        QMetaObject::invokeMethod(
            this,
            [this, message] {
                m_machineCreateStage = QStringLiteral("error");
                m_machineCreateError = message;
                m_lastError = message;
                emit createStateChanged();
                emit lastErrorChanged();
            },
            Qt::QueuedConnection);
    });
}

void MachineController::resetCreateState()
{
    if (m_machineCreateActive) {
        return;
    }
    m_machineCreateStage.clear();
    m_machineCreateProgress = 0;
    m_machineCreateError.clear();
    m_machineCreateJobId.clear();
    m_machineJobKind.clear();
    emit createStateChanged();
}

void MachineController::exportTo(const QString &id, const QString &path, bool force)
{
    const auto fail = [this](const QString &message) {
        m_lastError = message;
        emit lastErrorChanged();
        emit actionFinished(false, message);
    };

    if (m_machineCreateActive) {
        fail(QStringLiteral("Já existe uma operação de machine em andamento."));
        return;
    }
    if (id.isEmpty() || path.trimmed().isEmpty()) {
        fail(QStringLiteral("Informe a machine e o arquivo de destino do export."));
        return;
    }

    m_machineCreateActive = true;
    m_machineJobKind = QStringLiteral("export");
    m_machineCreateJobId.clear();
    m_machineCreateStage = QStringLiteral("validating");
    m_machineCreateProgress = 0;
    m_machineCreateError.clear();
    emit createStateChanged();

    QJsonObject params;
    params.insert(QStringLiteral("id"), id);
    params.insert(QStringLiteral("path"), path.trimmed());
    if (force) {
        params.insert(QStringLiteral("force"), true);
    }
    m_machineCreateStream.startMachineExport(params);
}

void MachineController::importFrom(const QString &path, const QString &name, bool force)
{
    const auto fail = [this](const QString &message) {
        m_lastError = message;
        emit lastErrorChanged();
        emit actionFinished(false, message);
    };

    if (m_machineCreateActive) {
        fail(QStringLiteral("Já existe uma operação de machine em andamento."));
        return;
    }
    if (path.trimmed().isEmpty()) {
        fail(QStringLiteral("Informe o arquivo de machine para importar."));
        return;
    }

    m_machineCreateActive = true;
    m_machineJobKind = QStringLiteral("import");
    m_machineCreateJobId.clear();
    m_machineCreateStage = QStringLiteral("validating");
    m_machineCreateProgress = 0;
    m_machineCreateError.clear();
    emit createStateChanged();

    QJsonObject params;
    params.insert(QStringLiteral("path"), path.trimmed());
    if (!name.trimmed().isEmpty()) {
        params.insert(QStringLiteral("name"), name.trimmed());
    }
    if (force) {
        params.insert(QStringLiteral("force"), true);
    }
    m_machineCreateStream.startMachineImport(params);
}

QString MachineController::archivePath(const QString &id) const
{
    QString name;
    for (const QVariant &value : m_machines) {
        const QVariantMap machine = value.toMap();
        if (machine.value(QStringLiteral("id")).toString() == id) {
            name = machine.value(QStringLiteral("name")).toString();
            break;
        }
    }
    QString directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (directory.isEmpty()) {
        directory = QDir::homePath();
    }
    if (name.isEmpty()) {
        name = QStringLiteral("machine");
    }
    return directory + QLatin1Char('/') + name + QStringLiteral(".tar.zst");
}

} // namespace PodStack
