// SPDX-License-Identifier: GPL-3.0-or-later
#include "machines/manager/Common.h"

using namespace PodStack::MachineInternal;

namespace PodStack {

MachineMeta MachineManager::exportMachine(
    const QString &idOrName, const QString &archivePath, bool force,
    const std::function<void(const QString &, int)> &progress,
    const std::function<bool()> &cancelled, Error *error)
{
    if (error) {
        *error = {};
    }

    // Export/import compartilham arquivos `.part`/temporários: serializa os
    // pacotes para dois jobs não colidirem no mesmo caminho.
    std::lock_guard<std::mutex> archiveGuard(m_archiveMutex);

    MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
        return {};
    }
    if (meta.state == QStringLiteral("provisioning") || meta.state == QStringLiteral("starting")
        || meta.state == QStringLiteral("stopping") || meta.state == QStringLiteral("deleting")) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_CONFLICT"),
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

    const QFileInfo targetInfo(archivePath);
    if (archivePath.trimmed().isEmpty() || !targetInfo.isAbsolute()) {
        fails(error, invalidParams(QStringLiteral("Informe um caminho absoluto para o export."),
                                   archivePath));
        return {};
    }
    const QString target = targetInfo.absoluteFilePath();
    if (target == meta.diskPath || target == meta.seedPath || target == meta.sshKeyPath
        || target == meta.sshKeyPath + QStringLiteral(".pub")) {
        fails(error, invalidParams(QStringLiteral("O export não pode sobrescrever um arquivo "
                                                  "interno da machine."),
                                   target));
        return {};
    }
    if (targetInfo.exists() && !targetInfo.isFile()) {
        fails(error, invalidParams(QStringLiteral("O destino do export não é um arquivo."),
                                   target));
        return {};
    }
    if (targetInfo.exists() && !force) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                          QStringLiteral("O arquivo de export já existe."), target,
                          QStringLiteral("Escolha outro caminho ou use --force para "
                                         "substituir.")));
        return {};
    }
    if (!QFileInfo(targetInfo.absolutePath()).isDir()) {
        fails(error, invalidParams(QStringLiteral("O diretório do export não existe."),
                                   targetInfo.absolutePath()));
        return {};
    }

    const QemuSpec spec = specFor(meta, false, error);
    if (spec.machineId.isEmpty()) {
        return {};
    }
    const bool wasRunning =
        meta.state == QStringLiteral("running") || meta.state == QStringLiteral("starting")
        || m_runtime->isRunning(spec, nullptr);

    if (wasRunning) {
        if (progress) {
            progress(QStringLiteral("stopping"), 2);
        }
        Error stopError;
        meta = stop(meta.id, false, &stopError);
        if (!stopError.isEmpty()) {
            if (error) {
                *error = stopError;
            }
            return {};
        }
    }

    const QString machineId = meta.id;
    const auto resume = [this, wasRunning, machineId]() {
        if (!wasRunning) {
            return;
        }
        Error resumeError;
        start(machineId, &resumeError);
        if (!resumeError.isEmpty()) {
            qCWarning(lcCore) << "Falha ao reiniciar a machine" << machineId
                              << "após o export:" << resumeError.code;
        }
    };
    const auto cancelledNow = [&cancelled]() {
        return cancelled && cancelled();
    };

    if (cancelledNow()) {
        resume();
        fails(error, Error::make(QStringLiteral("PODSTACK_CANCELLED"),
                                 QStringLiteral("Exportação cancelada.")));
        return {};
    }

    QTemporaryDir staging(m_options.machinesRoot + QStringLiteral("/.export-XXXXXX"));
    if (!staging.isValid()) {
        resume();
        fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível criar o diretório "
                                                "temporário do export."),
                                 staging.errorString()));
        return {};
    }

    if (progress) {
        progress(QStringLiteral("disk"), 5);
    }
    const QString flattened = staging.path() + QStringLiteral("/disk.qcow2");
    Error flattenError;
    if (!flattenDisk(meta.diskPath, flattened, cancelled, &flattenError)) {
        if (error) {
            *error = flattenError;
        }
        resume();
        return {};
    }
    if (cancelledNow()) {
        resume();
        fails(error, Error::make(QStringLiteral("PODSTACK_CANCELLED"),
                                 QStringLiteral("Exportação cancelada.")));
        return {};
    }

    MachineArchiveManifest manifest;
    manifest.format = MachineArchiveManifest::formatId();
    manifest.createdAt = nowIso();
    manifest.name = meta.name;
    manifest.distro = meta.distro;
    manifest.distroVersion = meta.distroVersion;
    manifest.arch = meta.arch;
    manifest.cpus = meta.cpus;
    manifest.memoryBytes = meta.memoryBytes;
    manifest.diskBytes = meta.diskBytes;
    manifest.sshUser = meta.sshUser;
    manifest.isolated = meta.isolated;
    manifest.isolateNetwork = meta.isolateNetwork;
    manifest.forwardSshAgent = meta.forwardSshAgent;

    const QString partPath = target + QStringLiteral(".part");
    QFile::remove(partPath);
    if (progress) {
        progress(QStringLiteral("archive"), 55);
    }
    const auto archiveProgress = [&progress](const QString &, int value) {
        if (progress) {
            progress(QStringLiteral("archive"), 55 + qBound(0, value, 100) * 40 / 100);
        }
    };

    Error archiveError;
    if (!MachineArchive::write(partPath, manifest, flattened, meta.seedPath, meta.sshKeyPath,
                               meta.sshKeyPath + QStringLiteral(".pub"), archiveProgress,
                               cancelled, &archiveError)) {
        QFile::remove(partPath);
        if (error) {
            *error = archiveError;
        }
        resume();
        return {};
    }
    if (cancelledNow()) {
        QFile::remove(partPath);
        resume();
        fails(error, Error::make(QStringLiteral("PODSTACK_CANCELLED"),
                                 QStringLiteral("Exportação cancelada.")));
        return {};
    }

    if (QFileInfo::exists(target) && !QFile::remove(target)) {
        QFile::remove(partPath);
        resume();
        fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível substituir o arquivo de "
                                                "export."),
                                 target));
        return {};
    }
    if (!QFile::rename(partPath, target)) {
        QFile::remove(partPath);
        resume();
        fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível finalizar o arquivo de "
                                                "export."),
                                 target));
        return {};
    }
    if (!QFile::setPermissions(target,
                               QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        QFile::remove(target);
        resume();
        fails(error, Error::make(QStringLiteral("PODSTACK_PERMISSION_DENIED"),
                                 QStringLiteral("Não foi possível restringir o arquivo de "
                                                "export a 0600."),
                                 target));
        return {};
    }

    if (wasRunning) {
        if (progress) {
            progress(QStringLiteral("starting"), 97);
        }
        Error restartError;
        const MachineMeta restarted = start(machineId, &restartError);
        if (!restartError.isEmpty()) {
            if (error) {
                *error = Error::make(
                    QStringLiteral("PODSTACK_IO_ERROR"),
                    QStringLiteral("A exportação foi concluída, mas a machine não reiniciou."),
                    errorText(restartError),
                    QStringLiteral("Inicie a machine manualmente; o arquivo de export está "
                                   "íntegro."));
            }
            return restarted.id.isEmpty() ? meta : restarted;
        }
        meta = restarted;
    }

    if (progress) {
        progress(QStringLiteral("done"), 100);
    }
    return meta;
}

MachineMeta MachineManager::importMachine(
    const QString &archivePath, const QString &requestedName, bool force,
    const std::function<void(const QString &, int)> &progress,
    const std::function<bool()> &cancelled, Error *error)
{
    if (error) {
        *error = {};
    }

    std::lock_guard<std::mutex> archiveGuard(m_archiveMutex);

    const QFileInfo archiveInfo(archivePath);
    if (archivePath.trimmed().isEmpty() || !archiveInfo.isAbsolute() || !archiveInfo.isFile()
        || archiveInfo.isSymLink()) {
        fails(error, invalidParams(QStringLiteral("Informe um arquivo de machine existente."),
                                   archivePath));
        return {};
    }

    const QString machinesRoot = m_options.machinesRoot;
    if (!QDir().mkpath(machinesRoot)) {
        fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível acessar o diretório das "
                                                "machines."),
                                 machinesRoot));
        return {};
    }
    setOwnerOnly(machinesRoot, true);

    QTemporaryDir staging(machinesRoot + QStringLiteral("/.import-XXXXXX"));
    if (!staging.isValid()) {
        fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível criar o diretório "
                                                "temporário do import."),
                                 staging.errorString()));
        return {};
    }

    if (progress) {
        progress(QStringLiteral("verify"), 2);
    }
    MachineArchiveManifest manifest;
    MachineArchive::Paths paths;
    const auto extractProgress = [&progress](const QString &, int value) {
        if (progress) {
            progress(QStringLiteral("verify"), 2 + qBound(0, value, 100) * 58 / 100);
        }
    };
    Error extractError;
    if (!MachineArchive::extract(archivePath, staging.path(), &manifest, &paths,
                                 extractProgress, cancelled, &extractError)) {
        if (error) {
            *error = extractError;
        }
        return {};
    }
    if (cancelled && cancelled()) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CANCELLED"),
                                 QStringLiteral("Importação cancelada.")));
        return {};
    }

    const QString name =
        requestedName.trimmed().isEmpty() ? manifest.name : requestedName.trimmed();
    static const QRegularExpression namePattern(
        QStringLiteral("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
    if (name.isEmpty() || name.size() > 63 || !namePattern.match(name).hasMatch()) {
        fails(error, invalidParams(QStringLiteral("Nome de machine inválido."), name,
                                   QStringLiteral("Use 1..63 caracteres [a-z0-9-], começando "
                                                  "e terminando por letra ou número.")));
        return {};
    }

    Error listError;
    QVector<MachineMeta> existing = existingMachines(&listError);
    if (!listError.isEmpty()) {
        if (error) {
            *error = listError;
        }
        return {};
    }

    // Reserva a porta antes de qualquer remoção (force): se não houver porta
    // livre, a machine existente permanece intacta.
    const int sshPort = findFreePort(existing, error);
    if (sshPort == 0) {
        return {};
    }

    std::shared_ptr<std::recursive_mutex> existingLock;
    std::unique_lock<std::recursive_mutex> existingGuard;
    for (const MachineMeta &machine : existing) {
        if (machine.name != name) {
            continue;
        }
        if (!force) {
            fails(error,
                  Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                              QStringLiteral("Já existe uma machine com esse nome."), name,
                              QStringLiteral("Use --name para escolher outro nome ou --force "
                                             "para substituir a machine existente.")));
            return {};
        }
        if (!tryLockMachine(machine.id, &existingLock, &existingGuard)) {
            fails(error,
                  Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                              QStringLiteral("A machine existente está em outra operação."),
                              machine.id,
                              QStringLiteral("Aguarde a operação atual terminar.")));
            return {};
        }
        if (!remove(machine.id, true, error)) {
            return {};
        }
        existing = existingMachines(&listError);
        if (!listError.isEmpty()) {
            if (error) {
                *error = listError;
            }
            return {};
        }
        break;
    }

    const QString id = newUuidV7();
    const QString directory = machineDirFor(id);
    if (directory.isEmpty() || QFileInfo::exists(directory)) {
        fails(error, Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                 QStringLiteral("Falha ao gerar o identificador da machine.")));
        return {};
    }

    staging.setAutoRemove(false);
    if (!QDir().rename(staging.path(), directory)) {
        staging.setAutoRemove(true);
        fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível mover a machine importada."),
                                 directory));
        return {};
    }
    setOwnerOnly(directory, true);

    const auto cleanup = [this, &directory, &id]() {
        QDir(directory).removeRecursively();
        m_store.remove(id, nullptr);
    };

    const auto artifactPath = [&directory](const QString &name) {
        return directory + QLatin1Char('/') + name;
    };

    const QString timestamp = nowIso();
    MachineMeta meta;
    meta.id = id;
    meta.name = name;
    meta.distro = manifest.distro;
    meta.distroVersion = manifest.distroVersion;
    meta.arch = manifest.arch;
    meta.cpus = manifest.cpus;
    meta.memoryBytes = manifest.memoryBytes;
    meta.diskBytes = manifest.diskBytes;
    meta.diskPath = artifactPath(QStringLiteral("disk.qcow2"));
    meta.seedPath = artifactPath(QStringLiteral("seed.iso"));
    meta.sshKeyPath = artifactPath(QStringLiteral("id_ed25519"));
    meta.sshPort = sshPort;
    meta.sshUser = manifest.sshUser;
    meta.state = QStringLiteral("stopped");
    meta.isolated = manifest.isolated;
    meta.isolateNetwork = manifest.isolated || manifest.isolateNetwork;
    meta.forwardSshAgent = manifest.isolated ? false : manifest.forwardSshAgent;
    meta.createdAt = timestamp;
    meta.updatedAt = timestamp;
    meta.ports = {
        MachinePort{newUuidV7(), sshPort, 22, QStringLiteral("tcp"), false,
                    QStringLiteral("ssh")},
    };

    for (const QString &path : {meta.diskPath, meta.seedPath, meta.sshKeyPath}) {
        if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
            cleanup();
            fails(error, Error::make(QStringLiteral("PODSTACK_PERMISSION_DENIED"),
                                     QStringLiteral("Não foi possível ajustar as permissões "
                                                    "de um arquivo importado."),
                                     path));
            return {};
        }
    }

    if (progress) {
        progress(QStringLiteral("metadata"), 85);
    }
    Error persistError;
    if (!persist(meta, &persistError)) {
        cleanup();
        if (error) {
            *error = persistError;
        }
        return {};
    }

    Error sshError;
    if (!m_sshConfig.sync(meta, meta.forwardSshAgent, &sshError)) {
        cleanup();
        if (error) {
            *error = sshError;
        }
        return {};
    }

    if (progress) {
        progress(QStringLiteral("done"), 100);
    }
    return meta;
}

} // namespace PodStack
