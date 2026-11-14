// SPDX-License-Identifier: GPL-3.0-or-later
#include "machines/manager/Common.h"

using namespace PodStack::MachineInternal;

namespace PodStack {

MachineMeta MachineManager::start(const QString &idOrName, Error *error)
{
    if (error) {
        *error = {};
    }

    MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
        return {};
    }
    if (meta.state == QStringLiteral("deleting") || meta.state == QStringLiteral("deleted")) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está sendo removida."), meta.id));
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

    // Machines criadas antes da T4-04 não têm chave; não bloqueia o start delas.
    if (!meta.sshKeyPath.isEmpty()) {
        Error sshConfigError;
        if (!m_sshConfig.sync(meta, meta.forwardSshAgent, &sshConfigError)) {
            meta.state = QStringLiteral("error");
            meta.error = errorText(sshConfigError);
            persist(meta, nullptr);
            if (error) {
                *error = sshConfigError;
            }
            return meta;
        }
    }

    Error specError;
    const QemuSpec spec = specFor(meta, true, &specError);
    if (spec.machineId.isEmpty()) {
        meta.state = QStringLiteral("error");
        meta.error = errorText(specError);
        persist(meta, nullptr);
        if (error) {
            *error = specError;
        }
        return meta;
    }

    if (m_runtime->isRunning(spec, nullptr)) {
        if (meta.state != QStringLiteral("running")) {
            meta.state = QStringLiteral("running");
            meta.error.clear();
            if (meta.lastBootAt.isEmpty()) {
                meta.lastBootAt = nowIso();
            }
            persist(meta, error);
            if (error && !error->isEmpty()) {
                return meta;
            }
        }
        return meta;
    }

    meta.state = QStringLiteral("starting");
    meta.error.clear();
    if (!persist(meta, error)) {
        return meta;
    }

    Error virtiofsError;
    if (!startVirtiofs(meta, &virtiofsError)) {
        meta.state = QStringLiteral("error");
        meta.error = errorText(virtiofsError);
        persist(meta, nullptr);
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
    return meta;
}

MachineMeta MachineManager::stop(const QString &idOrName, bool force, Error *error)
{
    if (error) {
        *error = {};
    }

    MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
        return {};
    }
    if (meta.state == QStringLiteral("deleting") || meta.state == QStringLiteral("deleted")) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está sendo removida."), meta.id));
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

    const QemuSpec spec = specFor(meta, false, error);
    if (spec.machineId.isEmpty()) {
        return meta;
    }

    if (meta.state == QStringLiteral("stopped")
        && !m_runtime->isRunning(spec, nullptr)) {
        stopVirtiofs(meta.id);
        for (const MachineMount &mount : meta.mounts) {
            QFile::remove(virtiofsSocketPath(meta.id, mount.tag));
        }
        return meta;
    }

    meta.state = QStringLiteral("stopping");
    if (!persist(meta, error)) {
        return meta;
    }

    Error stopError;
    if (!m_runtime->stop(spec, force, &stopError)) {
        meta.state = QStringLiteral("error");
        meta.error = errorText(stopError);
        persist(meta, nullptr);
        if (error) {
            *error = stopError;
        }
        return meta;
    }

    Error virtiofsError;
    stopVirtiofs(meta.id, &virtiofsError);
    for (const MachineMount &mount : meta.mounts) {
        QFile::remove(virtiofsSocketPath(meta.id, mount.tag));
    }
    if (!virtiofsError.isEmpty()) {
        meta.state = QStringLiteral("error");
        meta.error = errorText(virtiofsError);
        persist(meta, nullptr);
        if (error) {
            *error = virtiofsError;
        }
        return meta;
    }

    meta.state = QStringLiteral("stopped");
    meta.error.clear();
    if (!persist(meta, error)) {
        return meta;
    }
    return meta;
}

MachineMeta MachineManager::restart(const QString &idOrName, Error *error)
{
    if (error) {
        *error = {};
    }

    const MachineMeta current = load(idOrName, error);
    if (current.id.isEmpty()) {
        return {};
    }
    std::shared_ptr<std::recursive_mutex> lockHolder;
    std::unique_lock<std::recursive_mutex> machineGuard;
    if (!tryLockMachine(current.id, &lockHolder, &machineGuard)) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está em outra operação."),
                                 current.id,
                                 QStringLiteral("Aguarde a operação atual terminar.")));
        return current;
    }

    Error stopError;
    const MachineMeta stopped = stop(current.id, false, &stopError);
    if (!stopError.isEmpty()) {
        if (error) {
            *error = stopError;
        }
        return stopped;
    }
    return start(current.id, error);
}

bool MachineManager::remove(const QString &idOrName, bool force, Error *error)
{
    if (error) {
        *error = {};
    }

    MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
        return false;
    }

    std::shared_ptr<std::recursive_mutex> lockHolder;
    std::unique_lock<std::recursive_mutex> machineGuard;
    if (!tryLockMachine(meta.id, &lockHolder, &machineGuard)) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine está em outra operação."), meta.id,
                                 QStringLiteral("Aguarde a operação atual terminar.")));
        return false;
    }

    Error stopError;
    stop(meta.id, force, &stopError);
    if (!stopError.isEmpty() && !force) {
        if (error) {
            *error = stopError;
        }
        return false;
    }

    if (!stopError.isEmpty()) {
        // Remove forçado: mesmo com o QEMU resistindo, não deixe o virtiofsd
        // (e o socket) para trás ao apagar o diretório da machine.
        stopVirtiofs(meta.id);
        for (const MachineMount &mount : meta.mounts) {
            QFile::remove(virtiofsSocketPath(meta.id, mount.tag));
        }
    }

    meta = load(meta.id, error);
    if (meta.id.isEmpty()) {
        return false;
    }

    const QString directory = machineDirFor(meta.id);
    if (directory.isEmpty()) {
        fails(error, invalidParams(QStringLiteral("Identificador de machine inválido."), meta.id));
        return false;
    }

    const QFileInfo directoryInfo(directory);
    if (directoryInfo.isSymLink()) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_PERMISSION_DENIED"),
                          QStringLiteral("O diretório da machine é um symlink."), directory));
        return false;
    }

    const QString rootCanonical = QFileInfo(m_options.machinesRoot).canonicalFilePath();
    if (rootCanonical.isEmpty()) {
        fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Diretório de machines inválido."),
                                 m_options.machinesRoot));
        return false;
    }
    if (directoryInfo.exists()) {
        const QString canonical = directoryInfo.canonicalFilePath();
        if (canonical.isEmpty() || !canonical.startsWith(rootCanonical + QLatin1Char('/'))) {
            fails(error,
                  Error::make(QStringLiteral("PODSTACK_PERMISSION_DENIED"),
                              QStringLiteral("O caminho da machine escapa do diretório "
                                             "permitido."),
                              directory));
            return false;
        }
    }

    meta.state = QStringLiteral("deleting");
    meta.error.clear();
    if (!persist(meta, error)) {
        return false;
    }

    if (directoryInfo.exists() && !QDir(directory).removeRecursively()) {
        meta.state = QStringLiteral("error");
        meta.error = QStringLiteral("Não foi possível remover o diretório da machine.");
        persist(meta, nullptr);
        fails(error, Error::make(QStringLiteral("PODSTACK_IO_ERROR"), meta.error, directory));
        return false;
    }

    if (!m_store.remove(meta.id, error)) {
        return false;
    }

    Error sshConfigError;
    if (!m_sshConfig.remove(meta.name, &sshConfigError)) {
        qCWarning(lcCore) << "Não foi possível remover o bloco SSH da machine" << meta.name
                          << sshConfigError.code;
    }

    Config config = Config::load();
    if (config.get(QStringLiteral("machines.default")).toString() == meta.name) {
        config.set(QStringLiteral("machines.default"), QJsonValue::Null);
        QString configError;
        if (!config.save(&configError)) {
            qCWarning(lcCore) << "Não foi possível limpar a machine default ao remover"
                              << meta.name << configError;
        }
    }
    return true;
}

void MachineManager::reconcile(Error *error)
{
    if (error) {
        *error = {};
    }

    Error listError;
    const QVector<MachineMeta> machines = m_store.list(&listError);
    if (!listError.isEmpty()) {
        if (error) {
            *error = listError;
        }
        qCWarning(lcCore) << "Reconciliação de machines: falha ao listar" << listError.code
                          << listError.detail;
        return;
    }

    const QHash<QString, qint64> processes = m_runtime->runningMachines();
    QSet<QString> knownIds;
    knownIds.reserve(machines.size());
    for (const MachineMeta &meta : machines) {
        knownIds.insert(meta.id);
    }

    for (MachineMeta meta : machines) {
        const qint64 pid = processes.value(meta.id, 0);
        if (pid > 0) {
            if (meta.state == QStringLiteral("deleting")
                || meta.state == QStringLiteral("deleted")) {
                continue;
            }
            if (meta.state == QStringLiteral("running") && !meta.lastBootAt.isEmpty()) {
                continue;
            }

            meta.state = QStringLiteral("running");
            meta.error.clear();
            if (meta.lastBootAt.isEmpty()) {
                meta.lastBootAt = nowIso();
            }
            Error persistError;
            if (!persist(meta, &persistError)) {
                if (error && error->isEmpty()) {
                    *error = persistError;
                }
                qCWarning(lcCore) << "Reconciliação: falha ao adotar a machine" << meta.name
                                  << persistError.code;
            } else {
                qCInfo(lcCore) << QStringLiteral("Machine %1 adotada como running (pid %2)")
                                      .arg(meta.name)
                                      .arg(pid);
            }
            continue;
        }

        if (meta.state != QStringLiteral("running")
            && meta.state != QStringLiteral("starting")
            && meta.state != QStringLiteral("stopping")) {
            continue;
        }

        meta.state = QStringLiteral("stopped");
        meta.error.clear();
        Error persistError;
        if (!persist(meta, &persistError)) {
            if (error && error->isEmpty()) {
                *error = persistError;
            }
            qCWarning(lcCore) << "Reconciliação: falha ao marcar a machine" << meta.name
                              << "como stopped:" << persistError.code;
        } else {
            qCInfo(lcCore) << "Machine" << meta.name
                           << "sem processo; estado corrigido para stopped";
        }
    }

    for (auto it = processes.constBegin(); it != processes.constEnd(); ++it) {
        if (knownIds.contains(it.key())) {
            continue;
        }

        // Só limpa órfãos que pertencem a este diretório de machines. Isso
        // impede que um daemon de teste (store temporário) encerre QEMUs de
        // outra instalação do mesmo usuário que usem o mesmo prefixo.
        const QString orphanDirectory = machineDirFor(it.key());
        const bool ownsDirectory = !orphanDirectory.isEmpty()
                                   && QFileInfo(orphanDirectory).isDir();
        if (!ownsDirectory
            && !processReferencesPath(it.value(), m_options.machinesRoot)) {
            qCDebug(lcCore) << "Processo com marcador podstack:" << it.key()
                            << "ignorado: fora de" << m_options.machinesRoot;
            continue;
        }

        Error terminateError;
        if (m_runtime->terminate(it.key(), it.value(), &terminateError)) {
            qCInfo(lcCore) << QStringLiteral("Processo órfão da machine %1 encerrado (pid %2)")
                                  .arg(it.key())
                                  .arg(it.value());
        } else {
            if (error && error->isEmpty()) {
                *error = terminateError;
            }
            qCWarning(lcCore) << QStringLiteral(
                                     "Falha ao encerrar processo órfão da machine %1 "
                                     "(pid %2): %3")
                                     .arg(it.key())
                                     .arg(it.value())
                                     .arg(terminateError.code);
        }
    }

    // Só processos iniciados por este MachineManager. Um virtiofsd de outra
    // instalação (ou adotado após reinício) não é tocado.
    std::lock_guard<std::recursive_mutex> virtiofsLock(m_virtiofsMutex);
    const QStringList managedMachines = [this] {
        QStringList ids;
        ids.reserve(static_cast<int>(m_virtiofs.size()));
        for (const auto &entry : m_virtiofs) {
            ids.append(entry.first);
        }
        return ids;
    }();
    for (const QString &machineId : managedMachines) {
        Error lookupError;
        const MachineMeta meta = m_store.get(machineId, &lookupError);
        const bool active = meta.id == machineId
                            && (meta.state == QStringLiteral("running")
                                || meta.state == QStringLiteral("starting"));
        if (active) {
            continue;
        }
        Error stopError;
        if (stopVirtiofs(machineId, &stopError)) {
            qCInfo(lcCore) << "virtiofsd remanescente encerrado para a machine" << machineId;
        } else {
            qCWarning(lcCore) << "Falha ao encerrar virtiofsd remanescente da machine"
                              << machineId << stopError.code;
        }
    }
}

MachineMeta MachineManager::load(const QString &idOrName, Error *error)
{
    Error lookup;
    MachineMeta meta = m_store.get(idOrName, &lookup);
    if (!lookup.isEmpty()) {
        if (lookup.code != QStringLiteral("PODSTACK_NOT_FOUND")) {
            if (error) {
                *error = lookup;
            }
            return {};
        }

        meta = m_store.getByName(idOrName, &lookup);
    }

    if (!lookup.isEmpty()) {
        if (error) {
            *error = lookup;
        }
        return {};
    }
    return meta;
}

bool MachineManager::persist(MachineMeta &meta, Error *error)
{
    meta.updatedAt = nowIso();
    const QString directory = machineDirFor(meta.id);
    if (directory.isEmpty()) {
        return fails(error, invalidParams(QStringLiteral("Identificador de machine inválido."),
                                          meta.id));
    }

    const QString metaPath = directory + QStringLiteral("/meta.json");
    if (!MachineMetaFile::write(metaPath, meta, error)) {
        return false;
    }
    return m_store.upsert(meta, error);
}

QVector<MachineMeta> MachineManager::list(Error *error)
{
    if (error) {
        *error = {};
    }
    return m_store.list(error);
}

MachineMeta MachineManager::get(const QString &idOrName, Error *error)
{
    if (error) {
        *error = {};
    }
    return load(idOrName, error);
}

QVector<MachineMeta> MachineManager::existingMachines(Error *error)
{
    if (!QFileInfo::exists(m_store.path())) {
        return {};
    }
    return m_store.list(error);
}

int MachineManager::findFreePort(const QVector<MachineMeta> &existing, Error *error)
{
    if (m_options.sshPortStart < 1 || m_options.sshPortStart > 65535) {
        fails(error, invalidParams(QStringLiteral("Porta SSH inicial inválida."),
                                   QString::number(m_options.sshPortStart)));
        return 0;
    }

    QSet<int> usedPorts;
    for (const MachineMeta &meta : existing) {
        // A porta SSH é TCP; portas UDP no mesmo número não bloqueiam o hostfwd TCP.
        if (meta.sshPort > 0) {
            usedPorts.insert(meta.sshPort);
        }
        for (const MachinePort &port : meta.ports) {
            if (port.hostPort > 0
                && (port.protocol.isEmpty()
                    || port.protocol.compare(QStringLiteral("tcp"), Qt::CaseInsensitive) == 0)) {
                usedPorts.insert(port.hostPort);
            }
        }
    }

    for (int offset = 0; offset < kPortAttempts; ++offset) {
        const int candidate = m_options.sshPortStart + offset;
        if (candidate > 65535) {
            break;
        }
        if (usedPorts.contains(candidate)) {
            continue;
        }

        QTcpServer probe;
        if (!probe.listen(QHostAddress::LocalHost, static_cast<quint16>(candidate))) {
            continue;
        }
        probe.close();
        return candidate;
    }

    fails(error,
          Error::make(
              QStringLiteral("PODSTACK_CONFLICT"),
              QStringLiteral("Nenhuma porta SSH livre na faixa configurada."),
              QStringLiteral("%1..%2")
                  .arg(m_options.sshPortStart)
                  .arg(m_options.sshPortStart + kPortAttempts - 1),
              QStringLiteral("Libere uma porta ou ajuste machine.sshPortStart.")));
    return 0;
}

QString MachineManager::machineDirFor(const QString &id) const
{
    static const QRegularExpression validId(QStringLiteral("^[A-Za-z0-9._:-]+$"));
    if (id.isEmpty() || id == QStringLiteral(".") || id.contains(QStringLiteral(".."))
        || !validId.match(id).hasMatch()) {
        return {};
    }
    return m_options.machinesRoot + QLatin1Char('/') + id;
}

bool MachineManager::tryLockMachine(const QString &id,
                                    std::shared_ptr<std::recursive_mutex> *holder,
                                    std::unique_lock<std::recursive_mutex> *lock)
{
    *holder = machineLock(id);
    std::unique_lock<std::recursive_mutex> candidate(**holder, std::try_to_lock);
    if (!candidate.owns_lock()) {
        holder->reset();
        return false;
    }
    lock->swap(candidate);
    return true;
}

QString MachineManager::nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

MachineManager::MachineManager()
    : MachineManager(Options{})
{
}

MachineManager::MachineManager(Options options)
    : m_options(std::move(options))
    , m_runtime(m_options.runtime ? std::move(m_options.runtime)
                                  : std::make_unique<QemuProcessRuntime>())
    , m_store(m_options.storePath.isEmpty()
                  ? QString()
                  : QFileInfo(m_options.storePath).absoluteFilePath())
    , m_cache(m_options.imageCacheDir.isEmpty()
                  ? QString()
                  : QDir(m_options.imageCacheDir).absolutePath())
    , m_sshConfig(m_options.sshConfigPath)
{
    if (m_options.machinesRoot.isEmpty()) {
        m_options.machinesRoot = Paths::machinesDir();
    }
    m_options.machinesRoot = QDir(m_options.machinesRoot).absolutePath();

    if (!m_options.resolveDistro) {
        m_options.resolveDistro = DistroRegistry::resolve;
    }
}

MachineManager::~MachineManager() = default;

std::shared_ptr<std::recursive_mutex> MachineManager::machineLock(const QString &id)
{
    std::lock_guard<std::mutex> guard(m_machineLocksMutex);
    auto &lock = m_machineLocks[id];
    if (!lock) {
        lock = std::make_shared<std::recursive_mutex>();
    }
    return lock;
}

} // namespace PodStack
