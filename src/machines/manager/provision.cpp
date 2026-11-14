// SPDX-License-Identifier: GPL-3.0-or-later
#include "machines/manager/Common.h"

using namespace PodStack::MachineInternal;

namespace PodStack {

MachineMeta MachineManager::create(const CreateMachineRequest &request,
                                   const std::function<void(const QString &, int)> &progress,
                                   const std::function<bool()> &cancelled, Error *error)
{
    if (error) {
        *error = {};
    }

    if (!validate(request, error)) {
        return {};
    }

    QVector<MachineMount> mounts;
    if (!normalizeMounts(request.mounts, &mounts, error)) {
        return {};
    }

    if (cancelled && cancelled()) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CANCELLED"),
                                 QStringLiteral("Criação da machine cancelada.")));
        return {};
    }

    const QString arch = normalizeArch(request.arch);
    QString distroSpec = request.distro.trimmed();
    if (!request.version.trimmed().isEmpty() && !distroSpec.contains(QLatin1Char(':'))) {
        distroSpec += QLatin1Char(':') + request.version.trimmed();
    }

    Error resolveError;
    const DistroImage image = m_options.resolveDistro(distroSpec, arch, &resolveError);
    if (!resolveError.isEmpty()) {
        if (error) {
            *error = resolveError;
        }
        return {};
    }
    if (!image.url.isEmpty() && !image.arch.isEmpty() && image.arch != arch) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_UNSUPPORTED"),
                          QStringLiteral("A imagem resolvida não corresponde à arquitetura."),
                          QStringLiteral("%1 != %2").arg(image.arch, arch)));
        return {};
    }
    if (image.url.isEmpty() || image.checksum.isEmpty()) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                          QStringLiteral("A distro resolvida não tem imagem válida."), distroSpec));
        return {};
    }

    Error existingError;
    const QVector<MachineMeta> existing = existingMachines(&existingError);
    if (!existingError.isEmpty()) {
        if (error) {
            *error = existingError;
        }
        return {};
    }
    if (nameExists(existing, request.name)) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("Já existe uma machine com esse nome."),
                                 request.name, QStringLiteral("Escolha outro nome.")));
        return {};
    }

    const DownloadProgress downloadProgress =
        progress ? DownloadProgress([&progress](qint64 received, qint64 total) {
            const int percent = total > 0 ? static_cast<int>((received * 70) / total) : 0;
            progress(QStringLiteral("image"), qBound(0, percent, 70));
        })
                 : DownloadProgress();

    QString backingPath;
    Error downloadError;
    if (!m_cache.ensure(image, downloadProgress, cancelled, &backingPath, &downloadError)) {
        if (error) {
            *error = downloadError;
        }
        return {};
    }

    if (cancelled && cancelled()) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CANCELLED"),
                                 QStringLiteral("Criação da machine cancelada.")));
        return {};
    }

    const QString id = newUuidV7();
    const QString directory = machineDirFor(id);
    if (directory.isEmpty()) {
        fails(error, Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                 QStringLiteral("Falha ao gerar o identificador da machine.")));
        return {};
    }

    QDir rootDirectory;
    if (!rootDirectory.mkpath(m_options.machinesRoot) || !rootDirectory.mkpath(directory)) {
        fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível criar o diretório da machine."),
                                 directory));
        return {};
    }
    setOwnerOnly(m_options.machinesRoot, true);
    setOwnerOnly(directory, true);

    if (!resolveFirmware(arch, directory, nullptr, nullptr, error)) {
        cleanupDirectory(directory);
        return {};
    }

    QString sshKeyPath;
    QString sshPublicKey;
    if (!ensureSshKey(directory, request.name, &sshKeyPath, &sshPublicKey, error)) {
        cleanupDirectory(directory);
        return {};
    }

    if (progress) {
        progress(QStringLiteral("disk"), 75);
    }
    const QString diskPath = directory + QStringLiteral("/disk.qcow2");
    if (!createDisk(diskPath, backingPath, request.diskBytes, error)) {
        cleanupDirectory(directory);
        return {};
    }

    if (progress) {
        progress(QStringLiteral("seed"), 85);
    }
    const QString seedPath = directory + QStringLiteral("/seed.iso");
    CloudInitOptions seed;
    seed.machineId = id;
    seed.hostname = request.name;
    seed.user = normalizedUser(request.user);
    seed.password = request.password;
    seed.userData = request.userData;
    seed.sshAuthorizedKeys = {sshPublicKey};
    if (seed.userData.isEmpty()) {
        seed.environment = proxyEnvironmentFromConfig();
    }
    if (!CloudInit::writeSeedIso(seedPath, seed, error)) {
        cleanupDirectory(directory);
        return {};
    }

    if (cancelled && cancelled()) {
        cleanupDirectory(directory);
        fails(error, Error::make(QStringLiteral("PODSTACK_CANCELLED"),
                                 QStringLiteral("Criação da machine cancelada.")));
        return {};
    }

    const int sshPort = findFreePort(existing, error);
    if (sshPort == 0) {
        cleanupDirectory(directory);
        return {};
    }

    const QString timestamp = nowIso();
    MachineMeta meta;
    meta.id = id;
    meta.name = request.name;
    meta.distro = image.distro;
    meta.distroVersion = image.version;
    meta.arch = arch;
    meta.cpus = request.cpus;
    meta.memoryBytes = request.memoryBytes;
    meta.diskBytes = request.diskBytes;
    meta.diskPath = diskPath;
    meta.seedPath = seedPath;
    meta.sshKeyPath = sshKeyPath;
    meta.sshPort = sshPort;
    meta.sshUser = seed.user;
    meta.state = QStringLiteral("provisioning");
    meta.isolated = request.isolated;
    meta.isolateNetwork = request.isolateNetwork || request.isolated;
    meta.forwardSshAgent = request.isolated ? false : request.forwardSshAgent;
    meta.createdAt = timestamp;
    meta.updatedAt = timestamp;
    meta.ports = {
        MachinePort{newUuidV7(), sshPort, 22, QStringLiteral("tcp"), false, QStringLiteral("ssh")},
    };
    meta.mounts = request.isolated ? QVector<MachineMount>() : mounts;

    if (!persist(meta, error)) {
        m_store.remove(meta.id, nullptr);
        cleanupDirectory(directory);
        return {};
    }

    Error sshConfigError;
    if (!m_sshConfig.sync(meta, meta.forwardSshAgent, &sshConfigError)) {
        m_store.remove(meta.id, nullptr);
        cleanupDirectory(directory);
        if (error) {
            *error = sshConfigError;
        }
        return {};
    }

    if (progress) {
        progress(QStringLiteral("starting"), 90);
    }
    meta.state = QStringLiteral("starting");
    if (!persist(meta, error)) {
        return meta;
    }

    const QemuSpec spec = specFor(meta, true, error);
    if (spec.machineId.isEmpty()) {
        const Error specError = error ? *error : Error{};
        meta.state = QStringLiteral("error");
        meta.error = errorText(specError);
        persist(meta, nullptr);
        cleanupRuntimeFiles(directory, meta);
        return meta;
    }

    Error virtiofsError;
    if (!startVirtiofs(meta, &virtiofsError)) {
        meta.state = QStringLiteral("error");
        meta.error = errorText(virtiofsError);
        persist(meta, nullptr);
        cleanupRuntimeFiles(directory, meta);
        if (error) {
            *error = virtiofsError;
        }
        return meta;
    }

    Error startError;
    if (!m_runtime->start(spec, &startError)) {
        stopVirtiofs(meta.id);
        meta.state = QStringLiteral("error");
        meta.error = errorText(startError);
        persist(meta, nullptr);
        cleanupRuntimeFiles(directory, meta);
        if (error) {
            *error = startError;
        }
        return meta;
    }

    meta.state = QStringLiteral("running");
    meta.error.clear();
    meta.lastBootAt = nowIso();
    if (!persist(meta, error)) {
        m_runtime->stop(spec, true, nullptr);
        stopVirtiofs(meta.id);
        return meta;
    }

    if (progress) {
        progress(QStringLiteral("running"), 100);
    }
    return meta;
}

bool MachineManager::createDisk(const QString &diskPath, const QString &backingPath,
                                qint64 diskBytes, Error *error)
{
    const QString binary = qemuImgBinary(error);
    if (binary.isEmpty()) {
        return false;
    }

    const QStringList arguments{
        QStringLiteral("create"),
        QStringLiteral("-f"),
        QStringLiteral("qcow2"),
        QStringLiteral("-F"),
        QStringLiteral("qcow2"),
        QStringLiteral("-b"),
        backingPath,
        diskPath,
        QString::number(diskBytes),
    };

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(binary, arguments);
    if (!process.waitForStarted(kQemuImgStartTimeoutMs)) {
        return fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                        QStringLiteral("Não foi possível executar o qemu-img."),
                                        process.errorString()));
    }
    if (!process.waitForFinished(kQemuImgTimeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_TIMEOUT"),
                                 QStringLiteral("O qemu-img excedeu o tempo limite."), diskPath));
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                        QStringLiteral("O qemu-img falhou ao criar o disco."),
                                        processDetail(process.readAllStandardError())));
    }

    const QFileInfo diskInfo(diskPath);
    if (!diskInfo.isFile()) {
        return fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                        QStringLiteral("O disco qcow2 não foi criado."), diskPath));
    }
    setOwnerOnly(diskPath, false);
    return true;
}

bool MachineManager::flattenDisk(const QString &diskPath, const QString &targetPath,
                                 const std::function<bool()> &cancelled, Error *error,
                                 const QString &operation)
{
    const QString binary = qemuImgBinary(error);
    if (binary.isEmpty()) {
        return false;
    }

    const QStringList arguments{
        QStringLiteral("convert"),
        QStringLiteral("-O"),
        QStringLiteral("qcow2"),
        diskPath,
        targetPath,
    };

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(binary, arguments);
    if (!process.waitForStarted(kQemuImgStartTimeoutMs)) {
        return fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                        QStringLiteral("Não foi possível executar o qemu-img."),
                                        process.errorString()));
    }

    constexpr qint64 kExportTimeoutMs = 30LL * 60LL * 1000LL;
    QElapsedTimer timer;
    timer.start();
    while (process.state() != QProcess::NotRunning) {
        if (!process.waitForFinished(250)) {
            if (process.state() == QProcess::NotRunning) {
                break;
            }
            if (cancelled && cancelled()) {
                process.kill();
                process.waitForFinished(1000);
                QFile::remove(targetPath);
                return fails(error, Error::make(QStringLiteral("PODSTACK_CANCELLED"),
                                                QStringLiteral("%1 cancelada.").arg(operation)));
            }
            if (timer.elapsed() > kExportTimeoutMs) {
                process.kill();
                process.waitForFinished(1000);
                QFile::remove(targetPath);
                return fails(error,
                             Error::make(QStringLiteral("PODSTACK_TIMEOUT"),
                                         QStringLiteral("O qemu-img excedeu o tempo limite."),
                                         diskPath));
            }
        }
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                        QStringLiteral("O qemu-img falhou ao preparar o disco."),
                                        processDetail(process.readAllStandardError())));
    }
    if (!QFileInfo(targetPath).isFile()) {
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O disco do export não foi gerado."), targetPath));
    }
    setOwnerOnly(targetPath, false);
    return true;
}

bool MachineManager::resizeDisk(const QString &diskPath, qint64 bytes, Error *error)
{
    if (diskPath.isEmpty() || bytes <= 0) {
        return fails(error, invalidParams(QStringLiteral("Destino de resize inválido."), diskPath));
    }

    const QString binary = qemuImgBinary(error);
    if (binary.isEmpty()) {
        return false;
    }

    const QStringList arguments{
        QStringLiteral("resize"),
        diskPath,
        QString::number(bytes),
    };

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(binary, arguments);
    if (!process.waitForStarted(kQemuImgStartTimeoutMs)) {
        return fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                        QStringLiteral("Não foi possível executar o qemu-img."),
                                        process.errorString()));
    }
    if (!process.waitForFinished(kQemuImgTimeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return fails(error, Error::make(QStringLiteral("PODSTACK_TIMEOUT"),
                                        QStringLiteral("O qemu-img excedeu o tempo limite ao "
                                                       "redimensionar o disco."),
                                        diskPath));
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O qemu-img falhou ao redimensionar o disco."),
                                 processDetail(process.readAllStandardError())));
    }
    return true;
}

bool MachineManager::resolveFirmware(const QString &arch, const QString &machineDir,
                                     QString *codePath, QString *varsPath, Error *error)
{
    QString code = m_options.ovmfCodePath;
    QString vars = m_options.ovmfVarsPath;

    if (code.isEmpty() != vars.isEmpty()) {
        return fails(
            error, invalidParams(QStringLiteral("O firmware UEFI exige OVMF code e vars juntos.")));
    }

    if (code.isEmpty()) {
        if (arch == QStringLiteral("amd64")) {
            (void) resolveOvmfPaths(&code, &vars);
        } else if (arch == QStringLiteral("arm64")) {
            code = kAavmfCodeArm64;
            vars = kAavmfVarsArm64;
        } else {
            return fails(
                error, invalidParams(QStringLiteral("Arquitetura inválida para firmware."), arch));
        }
    }

    const QFileInfo codeInfo(code);
    if (!codeInfo.isFile() || !codeInfo.isReadable()) {
        const QString remediation =
            arch == QStringLiteral("arm64")
                ? QStringLiteral("Instale edk2-aarch64 (Fedora) ou qemu-efi-aarch64 "
                                 "(Debian/Ubuntu).")
                : QStringLiteral("Instale edk2-ovmf (Fedora) ou ovmf (Debian/Ubuntu).");
        return fails(error, Error::make(QStringLiteral("PODSTACK_UNSUPPORTED"),
                                        QStringLiteral("Firmware UEFI (OVMF) não encontrado."),
                                        code, remediation));
    }

    const QString target = machineDir + QStringLiteral("/OVMF_VARS.fd");
    if (!QFileInfo(target).isFile()) {
        const QFileInfo varsInfo(vars);
        if (!varsInfo.isFile() || !varsInfo.isReadable()) {
            const QString remediation =
                arch == QStringLiteral("arm64")
                    ? QStringLiteral("Instale edk2-aarch64 (Fedora) ou qemu-efi-aarch64 "
                                     "(Debian/Ubuntu).")
                    : QStringLiteral("Instale edk2-ovmf (Fedora) ou ovmf (Debian/Ubuntu).");
            return fails(error, Error::make(QStringLiteral("PODSTACK_UNSUPPORTED"),
                                            QStringLiteral("Template OVMF_VARS não encontrado."),
                                            vars, remediation));
        }
        if (!QFile::copy(vars, target)) {
            return fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                            QStringLiteral("Não foi possível copiar o OVMF_VARS."),
                                            target));
        }
    }
    setOwnerOnly(target, false);

    if (codePath) {
        *codePath = code;
    }
    if (varsPath) {
        *varsPath = target;
    }
    return true;
}

bool MachineManager::resolveOvmfPaths(QString *code, QString *vars,
                                      const QStringList &codeCandidates,
                                      const QStringList &varCandidates)
{
    const QStringList codes = codeCandidates.isEmpty() ? ovmfCodeCandidates() : codeCandidates;
    const QStringList varList = varCandidates.isEmpty() ? ovmfVarsCandidates() : varCandidates;
    for (int i = 0; i < codes.size() && i < varList.size(); ++i) {
        if (QFileInfo::exists(codes.at(i)) && QFileInfo::exists(varList.at(i))) {
            if (code) {
                *code = codes.at(i);
            }
            if (vars) {
                *vars = varList.at(i);
            }
            return true;
        }
    }
    if (code) {
        *code = codes.value(0);
    }
    if (vars) {
        *vars = varList.value(0);
    }
    return false;
}

QStringList MachineManager::ovmfCodeCandidates()
{
    return {
        QStringLiteral("/usr/share/edk2/ovmf/OVMF_CODE.fd"),
        QStringLiteral("/usr/share/OVMF/OVMF_CODE_4M.fd"),
        QStringLiteral("/usr/share/OVMF/OVMF_CODE.fd"),
    };
}

QStringList MachineManager::ovmfVarsCandidates()
{
    return {
        QStringLiteral("/usr/share/edk2/ovmf/OVMF_VARS.fd"),
        QStringLiteral("/usr/share/OVMF/OVMF_VARS_4M.fd"),
        QStringLiteral("/usr/share/OVMF/OVMF_VARS.fd"),
    };
}

QString MachineManager::qemuImgBinary(Error *error) const
{
    QString binary = m_options.qemuImgBinary;
    if (binary.isEmpty()) {
        binary = QStandardPaths::findExecutable(QStringLiteral("qemu-img"));
    }
    if (binary.isEmpty()) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_UNSUPPORTED"),
                          QStringLiteral("O binário qemu-img não foi encontrado."),
                          QStringLiteral("qemu-img"),
                          QStringLiteral("Instale o QEMU/KVM (qemu-kvm no Fedora ou qemu-utils "
                                         "no Debian/Ubuntu).")));
        return {};
    }
    return binary;
}

bool MachineManager::kvmAvailable()
{
    const QFileInfo kvm(QStringLiteral("/dev/kvm"));
    return kvm.exists() && kvm.isReadable() && kvm.isWritable();
}

QString MachineManager::normalizeArch(const QString &arch)
{
    const QString normalized = arch.trimmed().toLower();
    if (normalized.isEmpty()) {
        return DistroRegistry::hostArch();
    }
    if (normalized == QStringLiteral("x86_64") || normalized == QStringLiteral("x64")) {
        return QStringLiteral("amd64");
    }
    if (normalized == QStringLiteral("aarch64")) {
        return QStringLiteral("arm64");
    }
    return normalized;
}

MachineMeta MachineManager::clone(const QString &idOrName, const QString &newName,
                                  const std::function<void(const QString &, int)> &progress,
                                  const std::function<bool()> &cancelled, Error *error)
{
    if (error) {
        *error = {};
    }

    MachineMeta source = load(idOrName, error);
    if (source.id.isEmpty()) {
        return {};
    }
    if (source.state == QStringLiteral("provisioning") ||
        source.state == QStringLiteral("deleting") || source.state == QStringLiteral("deleted")) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está em transição; tente novamente."),
                                 source.id));
        return {};
    }

    std::shared_ptr<std::recursive_mutex> lockHolder;
    std::unique_lock<std::recursive_mutex> machineGuard;
    if (!tryLockMachine(source.id, &lockHolder, &machineGuard)) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está em outra operação."), source.id,
                                 QStringLiteral("Aguarde a operação atual terminar.")));
        return {};
    }

    const QString targetName = newName.trimmed();
    if (!validMachineName(targetName)) {
        fails(error, invalidMachineName(targetName));
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
    if (nameExists(existing, targetName)) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("Já existe uma machine com esse nome."), targetName,
                                 QStringLiteral("Escolha outro nome.")));
        return {};
    }

    const auto cancelledNow = [&cancelled]() { return cancelled && cancelled(); };
    if (cancelledNow()) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CANCELLED"),
                                 QStringLiteral("Clonagem cancelada.")));
        return {};
    }

    const QemuSpec spec = specFor(source, false, error);
    if (spec.machineId.isEmpty()) {
        return {};
    }
    const bool wasRunning = source.state == QStringLiteral("running") ||
                            source.state == QStringLiteral("starting") ||
                            m_runtime->isRunning(spec, nullptr);
    if (wasRunning) {
        if (progress) {
            progress(QStringLiteral("stopping"), 5);
        }
        Error stopError;
        source = stop(source.id, false, &stopError);
        if (!stopError.isEmpty()) {
            if (error) {
                *error = stopError;
            }
            return {};
        }
    }

    const QString sourceId = source.id;
    const auto resume = [this, wasRunning, sourceId]() {
        if (!wasRunning) {
            return;
        }
        Error resumeError;
        start(sourceId, &resumeError);
        if (!resumeError.isEmpty()) {
            qCWarning(lcCore) << "Falha ao reiniciar a machine" << sourceId
                              << "após a clonagem:" << resumeError.code;
        }
    };

    if (cancelledNow()) {
        resume();
        fails(error, Error::make(QStringLiteral("PODSTACK_CANCELLED"),
                                 QStringLiteral("Clonagem cancelada.")));
        return {};
    }

    const int sshPort = findFreePort(existing, error);
    if (sshPort == 0) {
        resume();
        return {};
    }

    const QString id = newUuidV7();
    const QString directory = machineDirFor(id);
    if (directory.isEmpty() || QFileInfo::exists(directory)) {
        resume();
        fails(error, Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                 QStringLiteral("Falha ao gerar o identificador da machine.")));
        return {};
    }

    QDir rootDirectory;
    if (!rootDirectory.mkpath(m_options.machinesRoot) || !rootDirectory.mkpath(directory)) {
        resume();
        fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível criar o diretório da machine."),
                                 directory));
        return {};
    }
    setOwnerOnly(m_options.machinesRoot, true);
    setOwnerOnly(directory, true);

    const auto cleanup = [this, directory, id, targetName]() {
        QDir(directory).removeRecursively();
        m_store.remove(id, nullptr);
        m_sshConfig.remove(targetName, nullptr);
    };

    const QString diskPath = directory + QStringLiteral("/disk.qcow2");
    if (progress) {
        progress(QStringLiteral("disk"), 10);
    }
    Error flattenError;
    if (!flattenDisk(source.diskPath, diskPath, cancelled, &flattenError,
                     QStringLiteral("Clonagem"))) {
        cleanup();
        resume();
        if (error) {
            *error = flattenError;
        }
        return {};
    }
    if (cancelledNow()) {
        cleanup();
        resume();
        fails(error, Error::make(QStringLiteral("PODSTACK_CANCELLED"),
                                 QStringLiteral("Clonagem cancelada.")));
        return {};
    }
    if (progress) {
        progress(QStringLiteral("disk"), 80);
        progress(QStringLiteral("copy"), 85);
    }

    const QString seedPath = directory + QStringLiteral("/seed.iso");
    const QString sshKeyPath = directory + QStringLiteral("/id_ed25519");
    const QString publicKeyPath = sshKeyPath + QStringLiteral(".pub");
    const auto copyPrivate = [](const QString &from, const QString &to, Error *copyError) {
        if (from.isEmpty() || !QFileInfo(from).isFile()) {
            return fails(copyError,
                         Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                     QStringLiteral("Um arquivo da machine de origem não existe."),
                                     from));
        }
        if (!QFile::copy(from, to)) {
            return fails(copyError,
                         Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                     QStringLiteral("Não foi possível copiar um arquivo da "
                                                    "machine de origem."),
                                     to));
        }
        if (!QFile::setPermissions(to, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
            return fails(copyError,
                         Error::make(QStringLiteral("PODSTACK_PERMISSION_DENIED"),
                                     QStringLiteral("Não foi possível ajustar as permissões do "
                                                    "arquivo clonado."),
                                     to));
        }
        return true;
    };

    Error copyError;
    if (!copyPrivate(source.seedPath, seedPath, &copyError) ||
        !copyPrivate(source.sshKeyPath, sshKeyPath, &copyError) ||
        !copyPrivate(source.sshKeyPath + QStringLiteral(".pub"), publicKeyPath, &copyError)) {
        cleanup();
        resume();
        if (error) {
            *error = copyError;
        }
        return {};
    }
    if (!QFile::setPermissions(publicKeyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                  QFileDevice::ReadGroup |
                                                  QFileDevice::ReadOther)) {
        cleanup();
        resume();
        fails(error, Error::make(QStringLiteral("PODSTACK_PERMISSION_DENIED"),
                                 QStringLiteral("Não foi possível ajustar as permissões da "
                                                "chave pública clonada."),
                                 publicKeyPath));
        return {};
    }

    if (cancelledNow()) {
        cleanup();
        resume();
        fails(error, Error::make(QStringLiteral("PODSTACK_CANCELLED"),
                                 QStringLiteral("Clonagem cancelada.")));
        return {};
    }
    if (progress) {
        progress(QStringLiteral("metadata"), 95);
    }

    const QString timestamp = nowIso();
    MachineMeta meta;
    meta.id = id;
    meta.name = targetName;
    meta.distro = source.distro;
    meta.distroVersion = source.distroVersion;
    meta.arch = source.arch;
    meta.cpus = source.cpus;
    meta.memoryBytes = source.memoryBytes;
    meta.diskBytes = source.diskBytes;
    meta.diskPath = diskPath;
    meta.seedPath = seedPath;
    meta.sshKeyPath = sshKeyPath;
    meta.sshPort = sshPort;
    meta.sshUser = source.sshUser;
    meta.state = QStringLiteral("stopped");
    meta.isolated = source.isolated;
    meta.isolateNetwork = source.isolateNetwork || source.isolated;
    meta.forwardSshAgent = source.isolated ? false : source.forwardSshAgent;
    meta.createdAt = timestamp;
    meta.updatedAt = timestamp;
    meta.ports = {
        MachinePort{newUuidV7(), sshPort, 22, QStringLiteral("tcp"), false, QStringLiteral("ssh")},
    };

    Error persistError;
    if (!persist(meta, &persistError)) {
        cleanup();
        resume();
        if (error) {
            *error = persistError;
        }
        return {};
    }

    Error sshError;
    if (!m_sshConfig.sync(meta, meta.forwardSshAgent, &sshError)) {
        cleanup();
        resume();
        if (error) {
            *error = sshError;
        }
        return {};
    }

    if (wasRunning) {
        if (progress) {
            progress(QStringLiteral("starting"), 98);
        }
        resume();
    }
    if (progress) {
        progress(QStringLiteral("done"), 100);
    }
    return meta;
}

} // namespace PodStack
