// SPDX-License-Identifier: GPL-3.0-or-later
#include "machines/manager/Common.h"

using namespace PodStack::MachineInternal;

namespace PodStack {

MachineMeta MachineManager::rename(const QString &idOrName, const QString &newName, Error *error)
{
    if (error) {
        *error = {};
    }

    MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
        return {};
    }
    if (meta.state == QStringLiteral("provisioning") || meta.state == QStringLiteral("deleting") ||
        meta.state == QStringLiteral("deleted")) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está em transição; tente novamente."),
                                 meta.id));
        return {};
    }

    std::shared_ptr<std::recursive_mutex> lockHolder;
    std::unique_lock<std::recursive_mutex> machineGuard;
    if (!tryLockMachine(meta.id, &lockHolder, &machineGuard)) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está em outra operação."), meta.id,
                                 QStringLiteral("Aguarde a operação atual terminar.")));
        return {};
    }

    const QString targetName = newName.trimmed();
    if (targetName == meta.name) {
        return meta;
    }
    if (!validMachineName(targetName)) {
        fails(error, invalidMachineName(targetName));
        return {};
    }

    Error listError;
    const QVector<MachineMeta> existing = existingMachines(&listError);
    if (!listError.isEmpty()) {
        if (error) {
            *error = listError;
        }
        return {};
    }
    for (const MachineMeta &machine : existing) {
        if (machine.id != meta.id && machine.name == targetName) {
            fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                     QStringLiteral("Já existe uma machine com esse nome."),
                                     targetName, QStringLiteral("Escolha outro nome.")));
            return {};
        }
    }

    const QString oldName = meta.name;
    Error sshError;
    if (!m_sshConfig.remove(oldName, &sshError)) {
        if (error) {
            *error = sshError;
        }
        return {};
    }

    meta.name = targetName;
    Error persistError;
    if (!persist(meta, &persistError)) {
        meta.name = oldName;
        persist(meta, nullptr);
        m_sshConfig.sync(meta, meta.forwardSshAgent, nullptr);
        if (error) {
            *error = persistError;
        }
        return {};
    }

    Error syncError;
    if (!m_sshConfig.sync(meta, meta.forwardSshAgent, &syncError)) {
        meta.name = oldName;
        persist(meta, nullptr);
        m_sshConfig.remove(targetName, nullptr);
        m_sshConfig.sync(meta, meta.forwardSshAgent, nullptr);
        if (error) {
            *error = syncError;
        }
        return {};
    }

    Config config = Config::load();
    if (config.get(QStringLiteral("machines.default")).toString() == oldName) {
        config.set(QStringLiteral("machines.default"), targetName);
        QString configError;
        if (!config.save(&configError)) {
            qCWarning(lcCore) << "Não foi possível atualizar a machine default após o rename:"
                              << configError;
        }
    }

    return meta;
}

QString MachineManager::defaultMachine(Error *error)
{
    if (error) {
        *error = {};
    }
    const Config config = Config::load();
    const QJsonValue value = config.get(QStringLiteral("machines.default"));
    return value.isString() ? value.toString().trimmed() : QString();
}

bool MachineManager::setDefaultMachine(const QString &idOrName, Error *error)
{
    if (error) {
        *error = {};
    }

    const MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
        return false;
    }

    Config config = Config::load();
    config.set(QStringLiteral("machines.default"), meta.name);
    QString configError;
    if (!config.save(&configError)) {
        const QString code = configError.contains(QStringLiteral("ermission"), Qt::CaseInsensitive)
                                 ? QStringLiteral("PODSTACK_PERMISSION_DENIED")
                                 : QStringLiteral("PODSTACK_IO_ERROR");
        fails(error, Error::make(code, QStringLiteral("Não foi possível salvar a machine default."),
                                 configError,
                                 QStringLiteral("Verifique as permissões de ~/.config/podstack.")));
        return false;
    }
    return true;
}

MachineMeta MachineManager::setPorts(const QString &idOrName, const QVector<MachinePort> &ports,
                                     Error *error)
{
    if (error) {
        *error = {};
    }

    MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
        return {};
    }
    if (meta.isolated) {
        for (const MachinePort &port : ports) {
            if (port.exposeLan) {
                fails(error, invalidParams(
                                 QStringLiteral("A machine está em modo isolado e não pode expor "
                                                "portas na LAN."),
                                 QStringLiteral("hostPort=%1").arg(port.hostPort),
                                 QStringLiteral("Desative exposeLan ou recrie a machine sem o modo "
                                                "isolado.")));
                return meta;
            }
        }
    }
    if (meta.state == QStringLiteral("provisioning") || meta.state == QStringLiteral("deleting") ||
        meta.state == QStringLiteral("deleted")) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está em transição; tente novamente."),
                                 meta.id));
        return meta;
    }

    std::shared_ptr<std::recursive_mutex> lockHolder;
    std::unique_lock<std::recursive_mutex> machineGuard;
    if (!tryLockMachine(meta.id, &lockHolder, &machineGuard)) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está em outra operação."), meta.id,
                                 QStringLiteral("Aguarde a operação atual terminar.")));
        return meta;
    }

    Error existingError;
    QVector<MachineMeta> existing = existingMachines(&existingError);
    if (!existingError.isEmpty()) {
        if (error) {
            *error = existingError;
        }
        return meta;
    }
    bool found = false;
    for (MachineMeta &candidate : existing) {
        if (candidate.id == meta.id) {
            candidate = meta;
            found = true;
            break;
        }
    }
    if (!found) {
        existing.append(meta);
    }

    QVector<MachinePort> normalized;
    if (!normalizePorts(meta.id, ports, existing, &normalized, error)) {
        return meta;
    }

    MachinePort sshPort;
    bool hasSshPort = false;
    for (const MachinePort &port : meta.ports) {
        if (isSshPort(port)) {
            sshPort = port;
            hasSshPort = true;
            break;
        }
    }
    if (!hasSshPort) {
        if (meta.sshPort <= 0) {
            fails(error,
                  invalidParams(QStringLiteral("A machine não tem porta SSH para preservar."),
                                meta.id));
            return meta;
        }
        sshPort.id = newUuidV7();
        sshPort.hostPort = meta.sshPort;
        sshPort.guestPort = 22;
        sshPort.protocol = QStringLiteral("tcp");
        sshPort.exposeLan = false;
        sshPort.label = QStringLiteral("ssh");
    }
    sshPort.protocol = QStringLiteral("tcp");
    sshPort.guestPort = 22;
    if (sshPort.hostPort <= 0) {
        sshPort.hostPort = meta.sshPort;
    }
    if (sshPort.hostPort < 1 || sshPort.hostPort > 65535) {
        fails(error, invalidParams(QStringLiteral("A machine não tem uma porta SSH válida."),
                                   QString::number(sshPort.hostPort)));
        return meta;
    }
    if (sshPort.id.isEmpty()) {
        sshPort.id = newUuidV7();
    }

    QVector<MachinePort> next;
    next.reserve(normalized.size() + 1);
    next.append(sshPort);
    next += normalized;

    bool changed = next.size() != meta.ports.size();
    if (!changed) {
        QVector<MachinePort> remaining = meta.ports;
        for (const MachinePort &port : next) {
            int match = -1;
            for (int index = 0; index < remaining.size(); ++index) {
                if (samePort(port, remaining.at(index))) {
                    match = index;
                    break;
                }
            }
            if (match < 0) {
                changed = true;
                break;
            }
            remaining.removeAt(match);
        }
    }
    if (!changed) {
        return meta;
    }

    const QemuSpec spec = specFor(meta, false, error);
    if (spec.machineId.isEmpty()) {
        return meta;
    }
    const bool wasRunning = meta.state == QStringLiteral("running") ||
                            meta.state == QStringLiteral("starting") ||
                            m_runtime->isRunning(spec, nullptr);

    if (wasRunning) {
        Error stopError;
        meta = stop(meta.id, false, &stopError);
        if (!stopError.isEmpty()) {
            if (error) {
                *error = stopError;
            }
            return meta;
        }
    }

    const QVector<MachinePort> previousPorts = meta.ports;
    meta.ports = next;
    meta.sshPort = sshPort.hostPort;

    Error changeError;
    Error persistError;
    if (!persist(meta, &persistError)) {
        meta.ports = previousPorts;
        changeError = persistError;
    }

    MachineMeta result = meta;
    if (wasRunning) {
        Error restartError;
        const MachineMeta restarted = start(meta.id, &restartError);
        if (!restarted.id.isEmpty()) {
            result = restarted;
        }
        if (!restartError.isEmpty()) {
            if (changeError.isEmpty()) {
                changeError = Error::make(
                    QStringLiteral("PODSTACK_CONFLICT"),
                    QStringLiteral("As portas foram atualizadas, mas a machine não reiniciou."),
                    errorText(restartError),
                    QStringLiteral("Inicie a machine manualmente; os metadados já foram "
                                   "salvos."));
            } else {
                qCWarning(lcCore) << "Falha ao reiniciar a machine" << meta.id
                                  << "após a atualização de portas:" << restartError.code;
            }
        }
    }

    if (!changeError.isEmpty() && error) {
        *error = changeError;
    }
    return result;
}

MachineMeta MachineManager::setSerialPorts(const QString &idOrName, const QStringList &paths,
                                           Error *error)
{
    if (error) {
        *error = {};
    }

    MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
        return {};
    }
    if (meta.state == QStringLiteral("provisioning") || meta.state == QStringLiteral("deleting") ||
        meta.state == QStringLiteral("deleted")) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está em transição; tente novamente."),
                                 meta.id));
        return meta;
    }

    QStringList normalized;
    for (const QString &path : paths) {
        QString pathError;
        if (!validateSerialPortPath(path, &pathError)) {
            fails(error, invalidParams(pathError, path));
            return meta;
        }
        if (!normalized.contains(path)) {
            normalized.append(path);
        }
    }

    std::shared_ptr<std::recursive_mutex> lockHolder;
    std::unique_lock<std::recursive_mutex> machineGuard;
    if (!tryLockMachine(meta.id, &lockHolder, &machineGuard)) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está em outra operação."), meta.id,
                                 QStringLiteral("Aguarde a operação atual terminar.")));
        return meta;
    }

    Error existingError;
    const QVector<MachineMeta> existing = existingMachines(&existingError);
    if (!existingError.isEmpty()) {
        if (error) {
            *error = existingError;
        }
        return meta;
    }
    for (const MachineMeta &candidate : existing) {
        if (candidate.id == meta.id) {
            continue;
        }
        for (const QString &path : normalized) {
            if (candidate.serialPorts.contains(path)) {
                fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                         QStringLiteral("A porta serial já está anexada a outra "
                                                        "machine."),
                                         path,
                                         QStringLiteral("Desanexe a porta da machine %1 primeiro.")
                                             .arg(candidate.name)));
                return meta;
            }
        }
    }

    const QemuSpec spec = specFor(meta, false, nullptr);
    const bool wasRunning =
        meta.state == QStringLiteral("running") || meta.state == QStringLiteral("starting") ||
        meta.state == QStringLiteral("stopping") || m_runtime->isRunning(spec, nullptr);

    if (wasRunning) {
        Error stopError;
        meta = stop(meta.id, true, &stopError);
        if (!stopError.isEmpty()) {
            if (error) {
                *error = stopError;
            }
            return meta;
        }
    }

    const QString machineId = meta.id;
    const QStringList previous = meta.serialPorts;
    meta.serialPorts = normalized;
    Error persistError;
    if (!persist(meta, &persistError)) {
        meta.serialPorts = previous;
        if (error) {
            *error = persistError;
        }
        return meta;
    }

    MachineMeta result = meta;
    if (wasRunning) {
        Error restartError;
        const MachineMeta restarted = start(machineId, &restartError);
        if (!restarted.id.isEmpty()) {
            result = restarted;
        }
        if (!restartError.isEmpty() && error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_CONFLICT"),
                QStringLiteral("As portas seriais foram salvas, mas a machine não reiniciou."),
                errorText(restartError),
                QStringLiteral("Inicie a machine manualmente; os metadados já foram salvos."));
        }
    }
    return result;
}

MachineMeta MachineManager::setUsbDevices(const QString &idOrName,
                                          const QVector<MachineUsbDevice> &devices, Error *error)
{
    if (error) {
        *error = {};
    }

    MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
        return {};
    }
    if (meta.state == QStringLiteral("provisioning") || meta.state == QStringLiteral("deleting") ||
        meta.state == QStringLiteral("deleted")) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está em transição; tente novamente."),
                                 meta.id));
        return meta;
    }

    QVector<MachineUsbDevice> normalized;
    for (const MachineUsbDevice &input : devices) {
        UsbDeviceInfo hostDevice;
        QString deviceError;
        if (input.bus <= 0 || input.address <= 0 ||
            !findUsbDevice(input.bus, input.address, &hostDevice, &deviceError)) {
            fails(error,
                  invalidParams(QStringLiteral("Dispositivo USB inválido ou desconectado."),
                                QStringLiteral("%1:%2").arg(input.bus).arg(input.address),
                                QStringLiteral("Reconecte o dispositivo e atualize a lista.")));
            return meta;
        }
        bool duplicate = false;
        for (const MachineUsbDevice &existing : normalized) {
            if (existing.bus == hostDevice.bus && existing.address == hostDevice.address) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            MachineUsbDevice device;
            device.bus = hostDevice.bus;
            device.address = hostDevice.address;
            device.vendorId = hostDevice.vendorId;
            device.productId = hostDevice.productId;
            device.description = hostDevice.description;
            normalized.append(device);
        }
    }

    std::shared_ptr<std::recursive_mutex> lockHolder;
    std::unique_lock<std::recursive_mutex> machineGuard;
    if (!tryLockMachine(meta.id, &lockHolder, &machineGuard)) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está em outra operação."), meta.id,
                                 QStringLiteral("Aguarde a operação atual terminar.")));
        return meta;
    }

    Error existingError;
    const QVector<MachineMeta> existing = existingMachines(&existingError);
    if (!existingError.isEmpty()) {
        if (error) {
            *error = existingError;
        }
        return meta;
    }
    for (const MachineMeta &candidate : existing) {
        if (candidate.id == meta.id) {
            continue;
        }
        for (const MachineUsbDevice &device : normalized) {
            for (const MachineUsbDevice &attached : candidate.usbDevices) {
                if (attached.bus == device.bus && attached.address == device.address) {
                    fails(error,
                          Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                      QStringLiteral("O dispositivo USB já está anexado a outra "
                                                     "machine."),
                                      device.description,
                                      QStringLiteral("Desanexe o dispositivo da machine %1 "
                                                     "primeiro.")
                                          .arg(candidate.name)));
                    return meta;
                }
            }
        }
    }

    const QemuSpec spec = specFor(meta, false, nullptr);
    const bool wasRunning =
        meta.state == QStringLiteral("running") || meta.state == QStringLiteral("starting") ||
        meta.state == QStringLiteral("stopping") || m_runtime->isRunning(spec, nullptr);

    if (wasRunning) {
        Error stopError;
        meta = stop(meta.id, true, &stopError);
        if (!stopError.isEmpty()) {
            if (error) {
                *error = stopError;
            }
            return meta;
        }
    }

    const QString machineId = meta.id;
    const QVector<MachineUsbDevice> previous = meta.usbDevices;
    meta.usbDevices = normalized;
    Error persistError;
    if (!persist(meta, &persistError)) {
        meta.usbDevices = previous;
        if (error) {
            *error = persistError;
        }
        return meta;
    }

    MachineMeta result = meta;
    if (wasRunning) {
        Error restartError;
        const MachineMeta restarted = start(machineId, &restartError);
        if (!restarted.id.isEmpty()) {
            result = restarted;
        }
        if (!restartError.isEmpty() && error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_CONFLICT"),
                QStringLiteral("Os dispositivos USB foram salvos, mas a machine não reiniciou."),
                errorText(restartError),
                QStringLiteral("Inicie a machine manualmente; os metadados já foram salvos."));
        }
    }
    return result;
}

MachineMeta MachineManager::setAudioEnabled(const QString &idOrName, bool enabled, Error *error)
{
    if (error) {
        *error = {};
    }
    MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
        return {};
    }
    if (meta.state == QStringLiteral("provisioning") || meta.state == QStringLiteral("deleting") ||
        meta.state == QStringLiteral("deleted")) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está em transição; tente novamente."),
                                 meta.id));
        return meta;
    }

    std::shared_ptr<std::recursive_mutex> lockHolder;
    std::unique_lock<std::recursive_mutex> machineGuard;
    if (!tryLockMachine(meta.id, &lockHolder, &machineGuard)) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está em outra operação."), meta.id,
                                 QStringLiteral("Aguarde a operação atual terminar.")));
        return meta;
    }

    const QemuSpec spec = specFor(meta, false, nullptr);
    const bool wasRunning =
        meta.state == QStringLiteral("running") || meta.state == QStringLiteral("starting") ||
        meta.state == QStringLiteral("stopping") || m_runtime->isRunning(spec, nullptr);

    if (wasRunning) {
        Error stopError;
        meta = stop(meta.id, true, &stopError);
        if (!stopError.isEmpty()) {
            if (error) {
                *error = stopError;
            }
            return meta;
        }
    }

    const QString machineId = meta.id;
    const bool previous = meta.audioEnabled;
    meta.audioEnabled = enabled;
    Error persistError;
    if (!persist(meta, &persistError)) {
        meta.audioEnabled = previous;
        if (error) {
            *error = persistError;
        }
        return meta;
    }

    MachineMeta result = meta;
    if (wasRunning) {
        Error restartError;
        const MachineMeta restarted = start(machineId, &restartError);
        if (!restarted.id.isEmpty()) {
            result = restarted;
        }
        if (!restartError.isEmpty() && error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_CONFLICT"),
                QStringLiteral("O áudio foi salvo, mas a machine não reiniciou."),
                errorText(restartError),
                QStringLiteral("Inicie a machine manualmente; os metadados já foram salvos."));
        }
    }
    return result;
}

MachineMeta MachineManager::update(const QString &idOrName, const UpdateMachineRequest &request,
                                   Error *error)
{
    if (error) {
        *error = {};
    }

    MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
        return {};
    }
    if (!request.cpus && !request.memoryBytes && !request.diskBytes && !request.forwardSshAgent) {
        fails(error, invalidParams(QStringLiteral("Informe ao menos um recurso para atualizar "
                                                  "(CPUs, memória, disco ou SSH agent).")));
        return meta;
    }
    if (meta.state == QStringLiteral("provisioning") || meta.state == QStringLiteral("deleting") ||
        meta.state == QStringLiteral("deleted")) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está em transição; tente novamente."),
                                 meta.id));
        return meta;
    }

    std::shared_ptr<std::recursive_mutex> lockHolder;
    std::unique_lock<std::recursive_mutex> machineGuard;
    if (!tryLockMachine(meta.id, &lockHolder, &machineGuard)) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está em outra operação."), meta.id,
                                 QStringLiteral("Aguarde a operação atual terminar.")));
        return meta;
    }

    if (request.cpus && (*request.cpus < 1 || *request.cpus > kMaximumCpus)) {
        fails(error, invalidParams(QStringLiteral("Número de CPUs fora do intervalo 1..256."),
                                   QString::number(*request.cpus)));
        return meta;
    }
    if (request.memoryBytes && *request.memoryBytes < kMinimumMemoryBytes) {
        fails(error, invalidParams(QStringLiteral("Memória insuficiente (mínimo 1 GiB)."),
                                   QString::number(*request.memoryBytes)));
        return meta;
    }
    if (request.diskBytes && *request.diskBytes < kMinimumDiskBytes) {
        fails(error, invalidParams(QStringLiteral("Disco insuficiente (mínimo 1 GiB)."),
                                   QString::number(*request.diskBytes)));
        return meta;
    }
    if (request.diskBytes && *request.diskBytes < meta.diskBytes) {
        fails(error,
              invalidParams(
                  QStringLiteral("Não é possível encolher o disco da machine."),
                  QStringLiteral("%1 < %2 bytes").arg(*request.diskBytes).arg(meta.diskBytes),
                  QStringLiteral("Recrie a machine com um disco menor se precisar reduzir.")));
        return meta;
    }

    const bool changes = (request.cpus && *request.cpus != meta.cpus) ||
                         (request.memoryBytes && *request.memoryBytes != meta.memoryBytes) ||
                         (request.diskBytes && *request.diskBytes > meta.diskBytes);
    const bool agentChanges =
        request.forwardSshAgent && *request.forwardSshAgent != meta.forwardSshAgent;
    if (!changes && !agentChanges) {
        return meta;
    }

    // Mudança só do agent não toca no processo: persiste o metadado e
    // sincroniza o bloco do ~/.ssh/config. Se o sync falhar, desfaz o valor
    // anterior para não deixar metadado e config divergentes.
    if (!changes) {
        const bool previousAgent = meta.forwardSshAgent;
        meta.forwardSshAgent = *request.forwardSshAgent;
        Error persistError;
        if (!persist(meta, &persistError)) {
            meta.forwardSshAgent = previousAgent;
            if (error) {
                *error = persistError;
            }
            return meta;
        }

        Error syncError;
        if (!m_sshConfig.sync(meta, meta.forwardSshAgent, &syncError)) {
            meta.forwardSshAgent = previousAgent;
            Error rollbackError;
            if (!persist(meta, &rollbackError)) {
                qCWarning(lcCore) << "Falha ao restaurar o SSH agent da machine" << meta.name
                                  << "após erro no ssh config:" << rollbackError.code;
            }
            if (error) {
                *error = syncError;
            }
            return meta;
        }
        return meta;
    }

    const QemuSpec spec = specFor(meta, false, error);
    if (spec.machineId.isEmpty()) {
        return meta;
    }
    const bool wasRunning = meta.state == QStringLiteral("running") ||
                            meta.state == QStringLiteral("starting") ||
                            m_runtime->isRunning(spec, nullptr);

    if (wasRunning) {
        Error stopError;
        meta = stop(meta.id, false, &stopError);
        if (!stopError.isEmpty()) {
            if (error) {
                *error = stopError;
            }
            return meta;
        }
    }

    const QString machineId = meta.id;
    Error updateError;
    bool metadataChanged = false;
    bool resized = false;
    const bool previousAgent = meta.forwardSshAgent;

    if (request.cpus) {
        meta.cpus = *request.cpus;
        metadataChanged = true;
    }
    if (request.memoryBytes) {
        meta.memoryBytes = *request.memoryBytes;
        metadataChanged = true;
    }
    if (request.diskBytes && *request.diskBytes > meta.diskBytes) {
        // O resize é offline: o qcow2 só é tocado depois de parar a machine.
        if (!resizeDisk(meta.diskPath, *request.diskBytes, &updateError)) {
            qCWarning(lcCore) << "Falha ao redimensionar o disco da machine" << meta.name
                              << updateError.code;
        } else {
            meta.diskBytes = *request.diskBytes;
            metadataChanged = true;
            resized = true;
        }
    }
    if (agentChanges) {
        meta.forwardSshAgent = *request.forwardSshAgent;
        metadataChanged = true;
    }

    if (updateError.isEmpty() && metadataChanged) {
        Error persistError;
        if (!persist(meta, &persistError)) {
            updateError = persistError;
            if (resized) {
                updateError = Error::make(
                    QStringLiteral("PODSTACK_IO_ERROR"),
                    QStringLiteral("O disco cresceu, mas não foi possível salvar os metadados "
                                   "da machine."),
                    errorText(persistError),
                    QStringLiteral("O novo tamanho já está aplicado no disco da machine; "
                                   "repita a atualização para persistir os metadados."));
            }
        }
    }

    // O start() abaixo já reaplica o bloco SSH; sincronizar aqui permite
    // detectar a falha antes e restaurar o valor anterior do agent.
    if (updateError.isEmpty() && agentChanges) {
        Error syncError;
        if (!m_sshConfig.sync(meta, meta.forwardSshAgent, &syncError)) {
            meta.forwardSshAgent = previousAgent;
            Error rollbackError;
            if (!persist(meta, &rollbackError)) {
                qCWarning(lcCore) << "Falha ao restaurar o SSH agent da machine" << meta.name
                                  << "após erro no ssh config:" << rollbackError.code;
            }
            updateError = syncError;
        }
    }

    MachineMeta result = meta;
    if (wasRunning) {
        Error restartError;
        const MachineMeta restarted = start(machineId, &restartError);
        if (!restarted.id.isEmpty()) {
            result = restarted;
        }
        if (!restartError.isEmpty()) {
            if (updateError.isEmpty()) {
                updateError = Error::make(
                    QStringLiteral("PODSTACK_CONFLICT"),
                    QStringLiteral("Os recursos foram atualizados, mas a machine não "
                                   "reiniciou."),
                    errorText(restartError),
                    QStringLiteral("Inicie a machine manualmente; os metadados já foram "
                                   "salvos."));
            } else {
                qCWarning(lcCore) << "Falha ao reiniciar a machine" << machineId
                                  << "após a atualização de recursos:" << restartError.code;
            }
        }
    }

    if (!updateError.isEmpty() && error) {
        *error = updateError;
    }
    return result;
}

bool MachineManager::validate(const CreateMachineRequest &request, Error *error) const
{
    if (!validMachineName(request.name)) {
        return fails(error, invalidMachineName(request.name));
    }
    if (request.cpus < 1 || request.cpus > kMaximumCpus) {
        return fails(error,
                     invalidParams(QStringLiteral("Número de CPUs fora do intervalo 1..256."),
                                   QString::number(request.cpus)));
    }
    if (request.memoryBytes < kMinimumMemoryBytes) {
        return fails(error, invalidParams(QStringLiteral("Memória insuficiente (mínimo 1 GiB)."),
                                          QString::number(request.memoryBytes)));
    }
    if (request.diskBytes < kMinimumDiskBytes) {
        return fails(error, invalidParams(QStringLiteral("Disco insuficiente (mínimo 1 GiB)."),
                                          QString::number(request.diskBytes)));
    }

    const QString arch = normalizeArch(request.arch);
    if (arch != QStringLiteral("amd64") && arch != QStringLiteral("arm64")) {
        return fails(error,
                     invalidParams(QStringLiteral("Arquitetura inválida para a machine."), arch));
    }
    if (request.distro.trimmed().isEmpty()) {
        return fails(error, invalidParams(QStringLiteral("Informe a distro da machine.")));
    }
    if (request.isolated) {
        if (!request.mounts.isEmpty()) {
            return fails(
                error,
                invalidParams(QStringLiteral("O modo isolado não permite mounts do host."),
                              QString::number(request.mounts.size()),
                              QStringLiteral("Remova os mounts ou desative o modo isolado.")));
        }
        if (request.forwardSshAgent) {
            return fails(
                error, invalidParams(QStringLiteral("O modo isolado não permite encaminhar o SSH "
                                                    "agent."),
                                     {},
                                     QStringLiteral("Desative o encaminhamento do agent ou o modo "
                                                    "isolado.")));
        }
    }
    return normalizeMounts(request.mounts, nullptr, error);
}

bool MachineManager::nameExists(const QVector<MachineMeta> &existing, const QString &name) const
{
    for (const MachineMeta &meta : existing) {
        if (meta.name == name) {
            return true;
        }
    }
    return false;
}

QString MachineManager::normalizedUser(const QString &user)
{
    const QString trimmed = user.trimmed();
    if (!trimmed.isEmpty()) {
        return trimmed;
    }

    QString fallback = qEnvironmentVariable("USER").trimmed();
    if (fallback.isEmpty()) {
        fallback = qEnvironmentVariable("LOGNAME").trimmed();
    }
    return fallback.isEmpty() ? QStringLiteral("podstack") : fallback;
}

bool MachineManager::normalizeMounts(const QVector<MachineMount> &mounts,
                                     QVector<MachineMount> *normalized, Error *error) const
{
    if (normalized) {
        normalized->clear();
    }
    if (mounts.size() > kMaximumMounts) {
        return fails(error, invalidParams(QStringLiteral("Número de mounts acima do limite (8)."),
                                          QString::number(mounts.size())));
    }

    static const QRegularExpression tagPattern(QStringLiteral("^[a-z0-9-]{1,32}$"));
    QSet<QString> tags;
    for (const MachineMount &mount : mounts) {
        const QString tag = mount.tag.trimmed();
        if (tag.isEmpty()) {
            continue;
        }
        if (!tagPattern.match(tag).hasMatch()) {
            return fails(error, invalidParams(QStringLiteral("Tag de mount inválida."), tag,
                                              QStringLiteral("Use 1..32 caracteres [a-z0-9-].")));
        }
        if (tags.contains(tag)) {
            return fails(error,
                         invalidParams(QStringLiteral("Tag de mount duplicada."), tag,
                                       QStringLiteral("Cada mount precisa de uma tag única.")));
        }
        tags.insert(tag);
    }

    int nextDefault = 0;
    const auto nextDefaultTag = [&tags, &nextDefault]() {
        QString tag;
        do {
            tag = QStringLiteral("host%1").arg(nextDefault++);
        } while (tags.contains(tag));
        tags.insert(tag);
        return tag;
    };

    QVector<MachineMount> result;
    result.reserve(mounts.size());
    for (const MachineMount &input : mounts) {
        const QString source = input.source.trimmed();
        if (source.isEmpty() || !QDir::isAbsolutePath(source)) {
            return fails(
                error,
                invalidParams(QStringLiteral("O diretório compartilhado (source) do mount deve "
                                             "ser absoluto."),
                              source));
        }
        const QFileInfo sourceInfo(source);
        if (!sourceInfo.exists() || !sourceInfo.isDir()) {
            return fails(
                error,
                invalidParams(QStringLiteral("O diretório compartilhado do mount não existe."),
                              source, QStringLiteral("Crie o diretório no host antes de montar.")));
        }
        const QString canonicalSource = sourceInfo.canonicalFilePath();
        if (canonicalSource.isEmpty()) {
            return fails(
                error,
                invalidParams(QStringLiteral("Não foi possível resolver o diretório do mount."),
                              source));
        }

        const QString rawTarget = input.target.trimmed();
        if (rawTarget.isEmpty() || !QDir::isAbsolutePath(rawTarget)) {
            return fails(error, invalidParams(QStringLiteral(
                                                  "O destino (target) do mount deve ser absoluto."),
                                              rawTarget));
        }
        if (rawTarget.split(QLatin1Char('/')).contains(QStringLiteral(".."))) {
            return fails(error,
                         invalidParams(QStringLiteral("O destino do mount não pode conter '..'."),
                                       rawTarget));
        }
        const QString target = QDir::cleanPath(rawTarget);

        MachineMount mount;
        mount.id = newUuidV7();
        mount.source = canonicalSource;
        mount.target = target;
        mount.readOnly = input.readOnly;
        mount.tag = input.tag.trimmed().isEmpty() ? nextDefaultTag() : input.tag.trimmed();
        result.append(mount);
    }

    if (normalized) {
        *normalized = result;
    }
    return true;
}

bool MachineManager::normalizePorts(const QString &machineId, const QVector<MachinePort> &ports,
                                    const QVector<MachineMeta> &existing,
                                    QVector<MachinePort> *normalized, Error *error) const
{
    if (normalized) {
        normalized->clear();
    }

    QSet<QString> usedHostPorts;
    QSet<QString> usedIds;
    int managedSshHostPort = 0;
    for (const MachineMeta &meta : existing) {
        const bool current = meta.id == machineId;
        for (const MachinePort &port : meta.ports) {
            if (port.hostPort > 0) {
                if (!current || isSshPort(port)) {
                    usedHostPorts.insert(normalizedPortProtocol(port.protocol) + QLatin1Char(':') +
                                         QString::number(port.hostPort));
                }
            }
            if (!port.id.isEmpty() && (!current || isSshPort(port))) {
                usedIds.insert(port.id);
            }
        }
        if (meta.sshPort > 0) {
            usedHostPorts.insert(QStringLiteral("tcp:") + QString::number(meta.sshPort));
            if (current && managedSshHostPort == 0) {
                managedSshHostPort = meta.sshPort;
            }
        }
    }
    for (const MachineMeta &meta : existing) {
        if (meta.id != machineId) {
            continue;
        }
        for (const MachinePort &port : meta.ports) {
            if (isSshPort(port) && port.hostPort > 0) {
                managedSshHostPort = port.hostPort;
                break;
            }
        }
    }

    QSet<QString> seenHostPorts;
    QSet<QString> seenIds;
    QVector<MachinePort> result;
    result.reserve(ports.size());
    for (const MachinePort &input : ports) {
        MachinePort port;
        port.protocol = normalizedPortProtocol(input.protocol);
        if (port.protocol != QStringLiteral("tcp") && port.protocol != QStringLiteral("udp")) {
            return fails(error, invalidParams(QStringLiteral("Protocolo de porta inválido."),
                                              input.protocol, QStringLiteral("Use tcp ou udp.")));
        }
        port.hostPort = input.hostPort;
        if (port.hostPort < 1 || port.hostPort > 65535) {
            return fails(error,
                         invalidParams(QStringLiteral("Porta do host fora do intervalo 1..65535."),
                                       QString::number(port.hostPort)));
        }
        port.guestPort = input.guestPort;
        if (port.guestPort < 1 || port.guestPort > 65535) {
            return fails(error,
                         invalidParams(QStringLiteral("Porta do guest fora do intervalo 1..65535."),
                                       QString::number(port.guestPort)));
        }
        port.label = input.label.trimmed();
        if (port.label.size() > 64) {
            return fails(error,
                         invalidParams(QStringLiteral("O rótulo da porta excede 64 caracteres."),
                                       QString::number(port.label.size())));
        }
        for (const QChar character : port.label) {
            if (character.unicode() < 0x20 ||
                (character.unicode() >= 0x7f && character.unicode() <= 0x9f)) {
                return fails(error, invalidParams(QStringLiteral("O rótulo da porta contém "
                                                                 "caracteres de controle.")));
            }
        }
        port.exposeLan = input.exposeLan;

        if (isSshPort(port)) {
            if (managedSshHostPort <= 0 || port.hostPort != managedSshHostPort) {
                return fails(
                    error,
                    invalidParams(QStringLiteral("A porta SSH é gerenciada pela machine e não "
                                                 "pode ser alterada."),
                                  QStringLiteral("%1/tcp -> 22").arg(port.hostPort),
                                  QStringLiteral("Remova a porta da lista; a SSH é preservada "
                                                 "automaticamente.")));
            }
            continue;
        }

        const QString hostKey = port.protocol + QLatin1Char(':') + QString::number(port.hostPort);
        if (usedHostPorts.contains(hostKey)) {
            return fails(error,
                         Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                     QStringLiteral("A porta do host já está em uso."),
                                     QStringLiteral("%1/%2").arg(port.hostPort).arg(port.protocol),
                                     QStringLiteral("Escolha outra porta ou protocolo.")));
        }
        if (seenHostPorts.contains(hostKey)) {
            return fails(
                error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                   QStringLiteral("Porta do host duplicada na lista."),
                                   QStringLiteral("%1/%2").arg(port.hostPort).arg(port.protocol)));
        }
        seenHostPorts.insert(hostKey);

        port.id = input.id.trimmed();
        if (port.id.isEmpty()) {
            port.id = newUuidV7();
        }
        if (usedIds.contains(port.id) || seenIds.contains(port.id)) {
            return fails(
                error, invalidParams(QStringLiteral("Identificador de porta duplicado."), port.id));
        }
        seenIds.insert(port.id);
        result.append(port);
    }

    if (normalized) {
        *normalized = result;
    }
    return true;
}

bool MachineManager::ensureSshKey(const QString &directory, const QString &name,
                                  QString *privatePath, QString *publicKey, Error *error)
{
    if (privatePath) {
        *privatePath = directory + QStringLiteral("/id_ed25519");
    }
    if (publicKey) {
        publicKey->clear();
    }

    QString binary = m_options.sshKeygenBinary;
    if (binary.isEmpty()) {
        binary = QStandardPaths::findExecutable(QStringLiteral("ssh-keygen"));
    }
    if (binary.isEmpty()) {
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_UNSUPPORTED"),
                                 QStringLiteral("O binário ssh-keygen não foi encontrado."),
                                 QStringLiteral("ssh-keygen"),
                                 QStringLiteral("Instale o OpenSSH client (openssh-clients no "
                                                "Fedora ou openssh-client no Debian/Ubuntu).")));
    }

    const QString keyPath = directory + QStringLiteral("/id_ed25519");
    const QString publicPath = keyPath + QStringLiteral(".pub");
    const QFileInfo privateInfo(keyPath);
    const QFileInfo publicInfo(publicPath);

    if (privateInfo.exists() && publicInfo.exists()) {
        if (privateInfo.isSymLink() || !privateInfo.isFile() || publicInfo.isSymLink() ||
            !publicInfo.isFile()) {
            return fails(error,
                         Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                     QStringLiteral("A chave SSH existente é inválida."), keyPath));
        }
        if (!QFile::setPermissions(keyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
            return fails(error,
                         Error::make(QStringLiteral("PODSTACK_PERMISSION_DENIED"),
                                     QStringLiteral("Não foi possível restringir a chave SSH "
                                                    "a 0600."),
                                     keyPath));
        }
        if (!QFile::setPermissions(publicPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                   QFileDevice::ReadGroup |
                                                   QFileDevice::ReadOther)) {
            return fails(error,
                         Error::make(QStringLiteral("PODSTACK_PERMISSION_DENIED"),
                                     QStringLiteral("Não foi possível ajustar a chave pública "
                                                    "para 0644."),
                                     publicPath));
        }
    } else if (privateInfo.exists() != publicInfo.exists()) {
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O par de chaves SSH da machine está incompleto."),
                                 keyPath,
                                 QStringLiteral("Remova a chave restante e recrie a machine.")));
    } else {
        const QStringList arguments{
            QStringLiteral("-q"),
            QStringLiteral("-t"),
            QStringLiteral("ed25519"),
            QStringLiteral("-N"),
            QString(),
            QStringLiteral("-C"),
            QStringLiteral("podstack:%1").arg(name),
            QStringLiteral("-f"),
            keyPath,
        };

        QProcess process;
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.start(binary, arguments);
        if (!process.waitForStarted(kSshKeygenStartTimeoutMs)) {
            return fails(error,
                         Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                     QStringLiteral("Não foi possível executar o ssh-keygen."),
                                     process.errorString()));
        }
        if (!process.waitForFinished(kSshKeygenTimeoutMs)) {
            process.kill();
            process.waitForFinished(1000);
            return fails(error, Error::make(QStringLiteral("PODSTACK_TIMEOUT"),
                                            QStringLiteral("O ssh-keygen excedeu o tempo limite."),
                                            keyPath));
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            return fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                            QStringLiteral("O ssh-keygen falhou ao gerar a chave."),
                                            processDetail(process.readAllStandardError())));
        }
        if (!QFileInfo(keyPath).isFile() || !QFileInfo(publicPath).isFile()) {
            return fails(error,
                         Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                     QStringLiteral("O ssh-keygen não criou o par de chaves."),
                                     keyPath));
        }
        if (!QFile::setPermissions(keyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
            return fails(error,
                         Error::make(QStringLiteral("PODSTACK_PERMISSION_DENIED"),
                                     QStringLiteral("Não foi possível restringir a chave SSH "
                                                    "a 0600."),
                                     keyPath));
        }
        if (!QFile::setPermissions(publicPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                   QFileDevice::ReadGroup |
                                                   QFileDevice::ReadOther)) {
            return fails(error,
                         Error::make(QStringLiteral("PODSTACK_PERMISSION_DENIED"),
                                     QStringLiteral("Não foi possível ajustar a chave pública "
                                                    "para 0644."),
                                     publicPath));
        }
    }

    QFile file(publicPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                        QStringLiteral("Não foi possível ler a chave pública SSH."),
                                        file.errorString()));
    }
    const QByteArray payload = file.read(kMaxPublicKeyBytes);
    const QString key = QString::fromUtf8(payload).trimmed();
    if (key.isEmpty() || key.contains(QLatin1Char('\n')) || key.contains(QLatin1Char('\r'))) {
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("A chave pública SSH está vazia ou inválida."),
                                 publicPath));
    }
    if (publicKey) {
        *publicKey = key;
    }
    return true;
}

QemuSpec MachineManager::specFor(const MachineMeta &meta, bool forStart, Error *error)
{
    QemuSpec spec;
    spec.machineId = meta.id;
    spec.name = meta.name;
    spec.arch = meta.arch;
    spec.cpus = meta.cpus;
    spec.memoryBytes = meta.memoryBytes;
    spec.diskPath = meta.diskPath;
    spec.seedPath = meta.seedPath;
    spec.sshPort = static_cast<quint16>(meta.sshPort);
    spec.kvm = kvmAvailable();
    spec.isolateNetwork = meta.isolateNetwork;
    spec.serialPorts = meta.serialPorts;
    spec.audioEnabled = meta.audioEnabled;
    for (const MachineUsbDevice &device : meta.usbDevices) {
        spec.usbDevices.append(QemuUsbDevice{device.bus, device.address});
    }

    const QString directory = machineDirFor(meta.id);
    if (directory.isEmpty()) {
        fails(error, invalidParams(QStringLiteral("Identificador de machine inválido."), meta.id));
        return {};
    }

    spec.qmpSocketPath = directory + QStringLiteral("/qmp.sock");
    spec.consoleLogPath = directory + QStringLiteral("/console.log");
    spec.pidFile = directory + QStringLiteral("/qemu.pid");

    for (const MachineMount &mount : meta.mounts) {
        const QString socketPath = virtiofsSocketPath(meta.id, mount.tag);
        if (!socketPath.isEmpty()) {
            spec.mounts.append(QemuMount{mount.tag, socketPath, mount.readOnly});
        }
    }

    for (const MachinePort &port : meta.ports) {
        QemuPort qemuPort;
        qemuPort.protocol = normalizedPortProtocol(port.protocol);
        qemuPort.hostPort = port.hostPort;
        qemuPort.guestPort = port.guestPort;
        qemuPort.hostAddress =
            port.exposeLan ? QStringLiteral("0.0.0.0") : QStringLiteral("127.0.0.1");
        spec.ports.append(qemuPort);
    }
    if (spec.ports.isEmpty() && meta.sshPort > 0) {
        // Metadados antigos (anteriores à T4-12) guardavam só a porta SSH.
        spec.ports.append(
            QemuPort{QStringLiteral("tcp"), meta.sshPort, 22, QStringLiteral("127.0.0.1")});
    }

    if (!forStart) {
        return spec;
    }

    if (!QFileInfo(meta.diskPath).isFile()) {
        fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O disco da machine não existe."), meta.diskPath,
                                 QStringLiteral("Recrie a machine.")));
        return {};
    }
    if (meta.seedPath.isEmpty() || !QFileInfo(meta.seedPath).isFile()) {
        fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O seed cloud-init da machine não existe."),
                                 meta.seedPath, QStringLiteral("Recrie a machine.")));
        return {};
    }

    if (!resolveFirmware(meta.arch, directory, &spec.ovmfCodePath, &spec.ovmfVarsPath, error)) {
        return {};
    }
    return spec;
}

} // namespace PodStack
