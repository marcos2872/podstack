// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/MachineManager.h"

#include "core/Config.h"
#include "core/Ids.h"
#include "core/Logging.h"
#include "core/Paths.h"
#include "core/Proxy.h"
#include "machines/CloudInit.h"
#include "machines/MachineArchive.h"
#include "machines/SerialPorts.h"
#include "machines/UsbDevices.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTimeZone>

#include <utility>

namespace PodStack {
namespace {

constexpr qint64 kMinimumMemoryBytes = 1024LL * 1024LL * 1024LL;
constexpr qint64 kMinimumDiskBytes = 1024LL * 1024LL * 1024LL;
constexpr int kMaximumCpus = 256;
constexpr int kMaximumMounts = 8;
constexpr int kPortAttempts = 20;
constexpr int kQemuImgStartTimeoutMs = 5000;
constexpr int kQemuImgTimeoutMs = 60000;
constexpr int kSshKeygenStartTimeoutMs = 5000;
constexpr int kSshKeygenTimeoutMs = 30000;
constexpr int kMaxProcessOutput = 4096;

QTimeZone utcTimeZone()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QTimeZone::UTC;
#else
    return QTimeZone(Qt::UTC);
#endif
}
constexpr qint64 kMaxPublicKeyBytes = 64LL * 1024;

// NOLINTBEGIN(bugprone-throwing-static-initialization)
const QString kOvmfCodeAmd64 = QStringLiteral("/usr/share/edk2/ovmf/OVMF_CODE.fd");
const QString kOvmfVarsAmd64 = QStringLiteral("/usr/share/edk2/ovmf/OVMF_VARS.fd");
const QString kAavmfCodeArm64 = QStringLiteral("/usr/share/AAVMF/AAVMF_CODE.fd");
const QString kAavmfVarsArm64 = QStringLiteral("/usr/share/AAVMF/AAVMF_VARS.fd");
// NOLINTEND(bugprone-throwing-static-initialization)

Error invalidParams(const QString &message, const QString &detail = {},
                    const QString &remediation = {})
{
    return Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"), message, detail, remediation);
}

bool validMachineName(const QString &name)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
    return !name.isEmpty() && name.size() <= 63 && pattern.match(name).hasMatch();
}

Error invalidMachineName(const QString &name)
{
    return invalidParams(QStringLiteral("Nome de machine inválido."), name,
                         QStringLiteral("Use 1..63 caracteres [a-z0-9-], começando e "
                                        "terminando por letra ou número."));
}

bool validSnapshotName(const QString &name)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
    return pattern.match(name).hasMatch();
}

Error invalidSnapshotName(const QString &name)
{
    return invalidParams(QStringLiteral("Nome de snapshot inválido."), name,
                         QStringLiteral("Use 1..64 caracteres [A-Za-z0-9._-], começando por "
                                        "letra ou número."));
}

bool fails(Error *error, const Error &value)
{
    if (error) {
        *error = value;
    }
    return false;
}

QString processDetail(const QByteArray &output)
{
    QString detail = QString::fromUtf8(output).trimmed();
    if (detail.size() > kMaxProcessOutput) {
        detail = detail.left(kMaxProcessOutput);
    }
    return detail;
}

QString errorText(const Error &error)
{
    if (error.detail.isEmpty()) {
        return error.message;
    }
    return QStringLiteral("%1: %2").arg(error.message, error.detail);
}

QString normalizedPortProtocol(const QString &protocol)
{
    const QString normalized = protocol.trimmed().toLower();
    return normalized.isEmpty() ? QStringLiteral("tcp") : normalized;
}

bool isSshPort(const MachinePort &port)
{
    return port.guestPort == 22
           && normalizedPortProtocol(port.protocol) == QStringLiteral("tcp");
}

bool samePort(const MachinePort &left, const MachinePort &right)
{
    return left.id == right.id && left.hostPort == right.hostPort
           && left.guestPort == right.guestPort
           && normalizedPortProtocol(left.protocol) == normalizedPortProtocol(right.protocol)
           && left.exposeLan == right.exposeLan && left.label == right.label;
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

QMap<QString, QString> proxyEnvironmentFromConfig()
{
    const Config config = Config::load();
    const QString proxy =
        config.get(QStringLiteral("network.proxy")).toString(QStringLiteral("auto"));
    const QStringList excludes = proxyExcludesFromConfig(config);

    Error error;
    if (!Proxy::validate(proxy, excludes, &error)) {
        qCWarning(lcCore) << "Configuração de proxy inválida; não aplicada às machines.";
        return {};
    }
    return Proxy::environment(proxy, excludes);
}

void setOwnerOnly(const QString &path, bool directory)
{
    QFileDevice::Permissions permissions =
        QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    if (directory) {
        permissions |= QFileDevice::ExeOwner;
    }
    QFile::setPermissions(path, permissions);
}

void cleanupDirectory(const QString &directory)
{
    if (!directory.isEmpty()) {
        QDir(directory).removeRecursively();
    }
}

void cleanupRuntimeFiles(const QString &directory, const MachineMeta &meta)
{
    QFile::remove(meta.diskPath);
    if (!meta.seedPath.isEmpty()) {
        QFile::remove(meta.seedPath);
    }
    if (!directory.isEmpty()) {
        QFile::remove(directory + QStringLiteral("/OVMF_VARS.fd"));
    }
}

/// Um órfão pertence a esta instalação quando o cmdline referencia um caminho
/// dentro do machinesRoot (disco, seed, sockets). Isso permite limpar até um
/// processo cujo diretório já foi apagado, sem tocar em outra instalação.
bool processReferencesPath(qint64 pid, const QString &root)
{
    if (pid <= 0 || root.isEmpty()) {
        return false;
    }

    QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (!cmdline.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray payload = cmdline.read(1024LL * 1024 + 1);
    if (payload.isEmpty() || payload.size() > 1024LL * 1024) {
        return false;
    }

    const QByteArray prefix = QFile::encodeName(root + QLatin1Char('/'));
    const QList<QByteArray> tokens = payload.split('\0');
    for (const QByteArray &token : tokens) {
        if (token.contains(prefix)) {
            return true;
        }
    }
    return false;
}

} // namespace

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

QString MachineManager::nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
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

bool MachineManager::kvmAvailable()
{
    const QFileInfo kvm(QStringLiteral("/dev/kvm"));
    return kvm.exists() && kvm.isReadable() && kvm.isWritable();
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
        return fails(error,
                     invalidParams(QStringLiteral("Memória insuficiente (mínimo 1 GiB)."),
                                   QString::number(request.memoryBytes)));
    }
    if (request.diskBytes < kMinimumDiskBytes) {
        return fails(error,
                     invalidParams(QStringLiteral("Disco insuficiente (mínimo 1 GiB)."),
                                   QString::number(request.diskBytes)));
    }

    const QString arch = normalizeArch(request.arch);
    if (arch != QStringLiteral("amd64") && arch != QStringLiteral("arm64")) {
        return fails(error, invalidParams(QStringLiteral("Arquitetura inválida para a machine."),
                                          arch));
    }
    if (request.distro.trimmed().isEmpty()) {
        return fails(error, invalidParams(QStringLiteral("Informe a distro da machine.")));
    }
    if (request.isolated) {
        if (!request.mounts.isEmpty()) {
            return fails(error,
                         invalidParams(
                             QStringLiteral("O modo isolado não permite mounts do host."),
                             QString::number(request.mounts.size()),
                             QStringLiteral("Remova os mounts ou desative o modo isolado.")));
        }
        if (request.forwardSshAgent) {
            return fails(error,
                         invalidParams(
                             QStringLiteral("O modo isolado não permite encaminhar o SSH "
                                            "agent."),
                             {},
                             QStringLiteral("Desative o encaminhamento do agent ou o modo "
                                            "isolado.")));
        }
    }
    return normalizeMounts(request.mounts, nullptr, error);
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

std::shared_ptr<std::recursive_mutex> MachineManager::machineLock(const QString &id)
{
    std::lock_guard<std::mutex> guard(m_machineLocksMutex);
    auto &lock = m_machineLocks[id];
    if (!lock) {
        lock = std::make_shared<std::recursive_mutex>();
    }
    return lock;
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

QVector<MachineMeta> MachineManager::existingMachines(Error *error)
{
    if (!QFileInfo::exists(m_store.path())) {
        return {};
    }
    return m_store.list(error);
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

QString MachineManager::virtiofsSocketPath(const QString &machineId, const QString &tag) const
{
    static const QRegularExpression tagPattern(QStringLiteral("^[a-z0-9-]{1,32}$"));
    if (!tagPattern.match(tag).hasMatch()) {
        return {};
    }
    const QString directory = machineDirFor(machineId);
    if (directory.isEmpty()) {
        return {};
    }
    return directory + QStringLiteral("/virtiofs-") + tag + QStringLiteral(".sock");
}

bool MachineManager::normalizeMounts(const QVector<MachineMount> &mounts,
                                     QVector<MachineMount> *normalized, Error *error) const
{
    if (normalized) {
        normalized->clear();
    }
    if (mounts.size() > kMaximumMounts) {
        return fails(error,
                     invalidParams(QStringLiteral("Número de mounts acima do limite (8)."),
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
            return fails(error,
                         invalidParams(QStringLiteral("Tag de mount inválida."), tag,
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
            return fails(error,
                         invalidParams(
                             QStringLiteral("O diretório compartilhado (source) do mount deve "
                                            "ser absoluto."),
                             source));
        }
        const QFileInfo sourceInfo(source);
        if (!sourceInfo.exists() || !sourceInfo.isDir()) {
            return fails(error,
                         invalidParams(
                             QStringLiteral("O diretório compartilhado do mount não existe."),
                             source,
                             QStringLiteral("Crie o diretório no host antes de montar.")));
        }
        const QString canonicalSource = sourceInfo.canonicalFilePath();
        if (canonicalSource.isEmpty()) {
            return fails(error,
                         invalidParams(
                             QStringLiteral("Não foi possível resolver o diretório do mount."),
                             source));
        }

        const QString rawTarget = input.target.trimmed();
        if (rawTarget.isEmpty() || !QDir::isAbsolutePath(rawTarget)) {
            return fails(error,
                         invalidParams(
                             QStringLiteral("O destino (target) do mount deve ser absoluto."),
                             rawTarget));
        }
        if (rawTarget.split(QLatin1Char('/')).contains(QStringLiteral(".."))) {
            return fails(error,
                         invalidParams(
                             QStringLiteral("O destino do mount não pode conter '..'."),
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
                    usedHostPorts.insert(normalizedPortProtocol(port.protocol) + QLatin1Char(':')
                                         + QString::number(port.hostPort));
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
            return fails(error,
                         invalidParams(QStringLiteral("Protocolo de porta inválido."),
                                       input.protocol, QStringLiteral("Use tcp ou udp.")));
        }
        port.hostPort = input.hostPort;
        if (port.hostPort < 1 || port.hostPort > 65535) {
            return fails(error,
                         invalidParams(
                             QStringLiteral("Porta do host fora do intervalo 1..65535."),
                             QString::number(port.hostPort)));
        }
        port.guestPort = input.guestPort;
        if (port.guestPort < 1 || port.guestPort > 65535) {
            return fails(error,
                         invalidParams(
                             QStringLiteral("Porta do guest fora do intervalo 1..65535."),
                             QString::number(port.guestPort)));
        }
        port.label = input.label.trimmed();
        if (port.label.size() > 64) {
            return fails(error,
                         invalidParams(QStringLiteral("O rótulo da porta excede 64 caracteres."),
                                       QString::number(port.label.size())));
        }
        for (const QChar character : port.label) {
            if (character.unicode() < 0x20
                || (character.unicode() >= 0x7f && character.unicode() <= 0x9f)) {
                return fails(error,
                             invalidParams(QStringLiteral("O rótulo da porta contém "
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

        const QString hostKey =
            port.protocol + QLatin1Char(':') + QString::number(port.hostPort);
        if (usedHostPorts.contains(hostKey)) {
            return fails(error,
                         Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                     QStringLiteral("A porta do host já está em uso."),
                                     QStringLiteral("%1/%2").arg(port.hostPort).arg(port.protocol),
                                     QStringLiteral("Escolha outra porta ou protocolo.")));
        }
        if (seenHostPorts.contains(hostKey)) {
            return fails(error,
                         Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                     QStringLiteral("Porta do host duplicada na lista."),
                                     QStringLiteral("%1/%2").arg(port.hostPort).arg(port.protocol)));
        }
        seenHostPorts.insert(hostKey);

        port.id = input.id.trimmed();
        if (port.id.isEmpty()) {
            port.id = newUuidV7();
        }
        if (usedIds.contains(port.id) || seenIds.contains(port.id)) {
            return fails(error,
                         invalidParams(QStringLiteral("Identificador de porta duplicado."),
                                       port.id));
        }
        seenIds.insert(port.id);
        result.append(port);
    }

    if (normalized) {
        *normalized = result;
    }
    return true;
}

bool MachineManager::startVirtiofs(const MachineMeta &meta, Error *error)
{
    if (error) {
        *error = {};
    }
    std::lock_guard<std::recursive_mutex> lock(m_virtiofsMutex);
    stopVirtiofs(meta.id);
    if (meta.mounts.isEmpty()) {
        return true;
    }

    for (const MachineMount &mount : meta.mounts) {
        if (m_virtiofs[meta.id].find(mount.tag) != m_virtiofs[meta.id].end()) {
            stopVirtiofs(meta.id);
            return fails(error,
                         invalidParams(QStringLiteral("Tag de mount duplicada."), mount.tag));
        }
        const QString socketPath = virtiofsSocketPath(meta.id, mount.tag);
        if (socketPath.isEmpty()) {
            stopVirtiofs(meta.id);
            return fails(error,
                         invalidParams(QStringLiteral("Tag de mount inválida."), mount.tag));
        }

        auto process = std::make_unique<VirtiofsdProcess>();
        Error startError;
        if (!process->start(m_options.virtiofsdBinary, mount.source, socketPath, mount.tag,
                            mount.readOnly, &startError)) {
            qCWarning(lcCore) << "Falha ao iniciar o virtiofsd do mount" << mount.source
                              << "da machine" << meta.name << ":" << startError.code
                              << startError.detail;
            stopVirtiofs(meta.id);
            if (error) {
                *error = startError;
                if (error->detail.isEmpty()) {
                    error->detail = QStringLiteral("mount %1 (%2)").arg(mount.tag, mount.source);
                }
            }
            return false;
        }
        m_virtiofs[meta.id].emplace(mount.tag, std::move(process));
    }
    return true;
}

bool MachineManager::stopVirtiofs(const QString &machineId, Error *error)
{
    if (error) {
        *error = {};
    }
    std::lock_guard<std::recursive_mutex> lock(m_virtiofsMutex);
    const auto it = m_virtiofs.find(machineId);
    if (it == m_virtiofs.end()) {
        return true;
    }

    bool ok = true;
    for (auto processIt = it->second.begin(); processIt != it->second.end(); ++processIt) {
        Error stopError;
        if (!processIt->second->stop(&stopError)) {
            ok = false;
            qCWarning(lcCore) << "Falha ao encerrar o virtiofsd da machine" << machineId
                              << "tag" << processIt->first << ":" << stopError.code;
            if (error && error->isEmpty()) {
                *error = stopError;
            }
        }
    }
    m_virtiofs.erase(it);
    return ok;
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

bool MachineManager::resolveFirmware(const QString &arch, const QString &machineDir,
                                     QString *codePath, QString *varsPath, Error *error)
{
    QString code = m_options.ovmfCodePath;
    QString vars = m_options.ovmfVarsPath;

    if (code.isEmpty() != vars.isEmpty()) {
        return fails(error,
                     invalidParams(
                         QStringLiteral("O firmware UEFI exige OVMF code e vars juntos.")));
    }

    if (code.isEmpty()) {
        if (arch == QStringLiteral("amd64")) {
            code = kOvmfCodeAmd64;
            vars = kOvmfVarsAmd64;
        } else if (arch == QStringLiteral("arm64")) {
            code = kAavmfCodeArm64;
            vars = kAavmfVarsArm64;
        } else {
            return fails(error, invalidParams(QStringLiteral("Arquitetura inválida para firmware."),
                                              arch));
        }
    }

    const QFileInfo codeInfo(code);
    if (!codeInfo.isFile() || !codeInfo.isReadable()) {
        const QString remediation =
            arch == QStringLiteral("arm64")
                ? QStringLiteral("Instale edk2-aarch64 (Fedora) ou qemu-efi-aarch64 "
                                 "(Debian/Ubuntu).")
                : QStringLiteral("Instale edk2-ovmf (Fedora) ou ovmf (Debian/Ubuntu).");
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_UNSUPPORTED"),
                                 QStringLiteral("Firmware UEFI (OVMF) não encontrado."), code,
                                 remediation));
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
            return fails(error,
                         Error::make(QStringLiteral("PODSTACK_UNSUPPORTED"),
                                     QStringLiteral("Template OVMF_VARS não encontrado."), vars,
                                     remediation));
        }
        if (!QFile::copy(vars, target)) {
            return fails(error,
                         Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
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

QString MachineManager::qemuImgBinary(Error *error) const
{
    QString binary = m_options.qemuImgBinary;
    if (binary.isEmpty()) {
        binary = QStandardPaths::findExecutable(QStringLiteral("qemu-img"));
    }
    if (binary.isEmpty()) {
        fails(error,
              Error::make(
                  QStringLiteral("PODSTACK_UNSUPPORTED"),
                  QStringLiteral("O binário qemu-img não foi encontrado."),
                  QStringLiteral("qemu-img"),
                  QStringLiteral("Instale o QEMU/KVM (qemu-kvm no Fedora ou qemu-utils "
                                 "no Debian/Ubuntu).")));
        return {};
    }
    return binary;
}

bool MachineManager::qemuImgSnapshot(const QStringList &args, QByteArray *output, Error *error)
{
    if (output) {
        output->clear();
    }

    const QString binary = qemuImgBinary(error);
    if (binary.isEmpty()) {
        return false;
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(binary, args);
    if (!process.waitForStarted(kQemuImgStartTimeoutMs)) {
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível executar o qemu-img."),
                                 process.errorString()));
    }
    if (!process.waitForFinished(kQemuImgTimeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_TIMEOUT"),
                                 QStringLiteral("O qemu-img excedeu o tempo limite ao "
                                                "gerenciar snapshots.")));
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O qemu-img falhou ao gerenciar o snapshot."),
                                 processDetail(process.readAllStandardError())));
    }

    if (output) {
        *output = process.readAllStandardOutput();
    }
    return true;
}

QVector<MachineSnapshot> MachineManager::parseSnapshots(const MachineMeta &meta, Error *error)
{
    if (meta.diskPath.isEmpty() || !QFileInfo(meta.diskPath).isFile()) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                          QStringLiteral("O disco da machine não existe."), meta.diskPath,
                          QStringLiteral("Recrie a machine.")));
        return {};
    }

    QByteArray output;
    // `-U` (force-share) permite ler os snapshots com a machine rodando; o
    // QEMU mantém o lock de escrita no qcow2. A operação é somente leitura.
    if (!qemuImgSnapshot({QStringLiteral("info"), QStringLiteral("-U"),
                          QStringLiteral("--output=json"), meta.diskPath},
                         &output, error)) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                          QStringLiteral("Não foi possível interpretar as informações do "
                                         "disco."),
                          parseError.errorString()));
        return {};
    }

    QVector<MachineSnapshot> items;
    const QJsonArray array =
        document.object().value(QStringLiteral("snapshots")).toArray();
    items.reserve(array.size());
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) {
            continue;
        }

        MachineSnapshot snapshot;
        snapshot.id = object.value(QStringLiteral("id")).toString();
        snapshot.name = name;
        snapshot.sizeBytes = object.value(QStringLiteral("vm-state-size")).toInteger(0);

        const QJsonValue secondsValue = object.value(QStringLiteral("date-sec"));
        if (secondsValue.isDouble()) {
            qint64 milliseconds = 0;
            const QJsonValue nanosecondsValue = object.value(QStringLiteral("date-nsec"));
            if (nanosecondsValue.isDouble()) {
                milliseconds = nanosecondsValue.toInteger() / 1000000;
            }
            snapshot.date = QDateTime::fromSecsSinceEpoch(secondsValue.toInteger(), utcTimeZone())
                                .addMSecs(milliseconds)
                                .toString(Qt::ISODateWithMs);
        }
        items.append(snapshot);
    }
    return items;
}

MachineSnapshot MachineManager::findSnapshot(const QVector<MachineSnapshot> &items,
                                              const QString &name, Error *error)
{
    for (const MachineSnapshot &snapshot : items) {
        if (snapshot.name == name) {
            return snapshot;
        }
    }
    fails(error,
          Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                      QStringLiteral("Snapshot não encontrado."), name,
                      QStringLiteral("Liste os snapshots com `machine snapshot ls`.")));
    return {};
}

QVector<MachineSnapshot> MachineManager::snapshots(const QString &idOrName, Error *error)
{
    if (error) {
        *error = {};
    }

    MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
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

    return parseSnapshots(meta, error);
}

MachineSnapshot MachineManager::createSnapshot(const QString &idOrName, const QString &name,
                                               Error *error)
{
    if (error) {
        *error = {};
    }
    if (!validSnapshotName(name)) {
        fails(error, invalidSnapshotName(name));
        return {};
    }

    MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
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

    const QVector<MachineSnapshot> existing = parseSnapshots(meta, error);
    if (error && !error->isEmpty()) {
        return {};
    }
    for (const MachineSnapshot &snapshot : existing) {
        if (snapshot.name == name) {
            fails(error,
                  Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                              QStringLiteral("Já existe um snapshot com esse nome."), name,
                              QStringLiteral("Escolha outro nome ou remova o snapshot "
                                             "existente.")));
            return {};
        }
    }

    const QemuSpec spec = specFor(meta, false, error);
    if (spec.machineId.isEmpty()) {
        return {};
    }
    const bool wasRunning = meta.state == QStringLiteral("running")
                            || meta.state == QStringLiteral("starting")
                            || m_runtime->isRunning(spec, nullptr);
    if (wasRunning) {
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
    const auto resume = [this, wasRunning, machineId]() -> Error {
        if (!wasRunning) {
            return {};
        }
        Error resumeError;
        start(machineId, &resumeError);
        return resumeError;
    };

    Error snapshotError;
    if (!qemuImgSnapshot({QStringLiteral("snapshot"), QStringLiteral("-c"), name,
                          meta.diskPath},
                         nullptr, &snapshotError)) {
        const Error resumeError = resume();
        if (!resumeError.isEmpty()) {
            qCWarning(lcCore) << "Falha ao reiniciar a machine" << machineId
                              << "após falha no snapshot:" << resumeError.code;
        }
        if (error) {
            *error = snapshotError;
        }
        return {};
    }

    MachineSnapshot created;
    Error listError;
    const QVector<MachineSnapshot> after = parseSnapshots(meta, &listError);
    if (listError.isEmpty()) {
        for (const MachineSnapshot &snapshot : after) {
            if (snapshot.name == name) {
                created = snapshot;
                break;
            }
        }
    } else {
        qCWarning(lcCore) << "Falha ao relistar snapshots da machine" << machineId << ":"
                          << listError.code;
    }
    if (created.name.isEmpty()) {
        // qemu-img pode não listar o snapshot em versões antigas; o comando
        // retornou sucesso, então devolvemos ao menos nome/data.
        created.name = name;
        created.date = nowIso();
    }

    const Error resumeError = resume();
    if (!resumeError.isEmpty()) {
        qCWarning(lcCore) << "Falha ao reiniciar a machine" << machineId
                          << "após criar o snapshot:" << resumeError.code;
        fails(error,
              Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                          QStringLiteral("O snapshot foi criado, mas a machine não "
                                         "reiniciou."),
                          errorText(resumeError),
                          QStringLiteral("Inicie a machine manualmente; o snapshot já foi "
                                         "gravado no disco.")));
    }
    return created;
}

MachineMeta MachineManager::restoreSnapshot(const QString &idOrName, const QString &name,
                                            Error *error)
{
    if (error) {
        *error = {};
    }
    if (!validSnapshotName(name)) {
        fails(error, invalidSnapshotName(name));
        return {};
    }

    MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
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

    const QVector<MachineSnapshot> existing = parseSnapshots(meta, error);
    if (error && !error->isEmpty()) {
        return {};
    }
    findSnapshot(existing, name, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    const QemuSpec spec = specFor(meta, false, error);
    if (spec.machineId.isEmpty()) {
        return {};
    }
    const bool wasRunning = meta.state == QStringLiteral("running")
                            || meta.state == QStringLiteral("starting")
                            || m_runtime->isRunning(spec, nullptr);
    if (wasRunning) {
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
    const auto resume = [this, wasRunning, machineId]() -> Error {
        if (!wasRunning) {
            return {};
        }
        Error resumeError;
        start(machineId, &resumeError);
        return resumeError;
    };

    Error restoreError;
    if (!qemuImgSnapshot({QStringLiteral("snapshot"), QStringLiteral("-a"), name,
                          meta.diskPath},
                         nullptr, &restoreError)) {
        const Error resumeError = resume();
        if (!resumeError.isEmpty()) {
            qCWarning(lcCore) << "Falha ao reiniciar a machine" << machineId
                              << "após falha no restore:" << resumeError.code;
        }
        if (error) {
            *error = restoreError;
        }
        return {};
    }

    if (!wasRunning) {
        return meta;
    }

    Error resumeError;
    MachineMeta restarted = start(machineId, &resumeError);
    if (!resumeError.isEmpty()) {
        qCWarning(lcCore) << "Falha ao reiniciar a machine" << machineId
                          << "após restaurar o snapshot:" << resumeError.code;
        if (restarted.id.isEmpty()) {
            restarted = meta;
        }
        fails(error,
              Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                          QStringLiteral("O snapshot foi restaurado, mas a machine não "
                                         "reiniciou."),
                          errorText(resumeError),
                          QStringLiteral("Inicie a machine manualmente; o disco já foi "
                                         "restaurado.")));
    }
    return restarted;
}

bool MachineManager::removeSnapshot(const QString &idOrName, const QString &name, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!validSnapshotName(name)) {
        fails(error, invalidSnapshotName(name));
        return false;
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

    const QVector<MachineSnapshot> existing = parseSnapshots(meta, error);
    if (error && !error->isEmpty()) {
        return false;
    }
    findSnapshot(existing, name, error);
    if (error && !error->isEmpty()) {
        return false;
    }

    const QemuSpec spec = specFor(meta, false, error);
    if (spec.machineId.isEmpty()) {
        return false;
    }
    const bool wasRunning = meta.state == QStringLiteral("running")
                            || meta.state == QStringLiteral("starting")
                            || m_runtime->isRunning(spec, nullptr);
    if (wasRunning) {
        Error stopError;
        meta = stop(meta.id, false, &stopError);
        if (!stopError.isEmpty()) {
            if (error) {
                *error = stopError;
            }
            return false;
        }
    }

    const QString machineId = meta.id;
    const auto resume = [this, wasRunning, machineId]() -> Error {
        if (!wasRunning) {
            return {};
        }
        Error resumeError;
        start(machineId, &resumeError);
        return resumeError;
    };

    Error removeError;
    if (!qemuImgSnapshot({QStringLiteral("snapshot"), QStringLiteral("-d"), name,
                          meta.diskPath},
                         nullptr, &removeError)) {
        const Error resumeError = resume();
        if (!resumeError.isEmpty()) {
            qCWarning(lcCore) << "Falha ao reiniciar a machine" << machineId
                              << "após falha ao remover snapshot:" << resumeError.code;
        }
        if (error) {
            *error = removeError;
        }
        return false;
    }

    const Error resumeError = resume();
    if (!resumeError.isEmpty()) {
        qCWarning(lcCore) << "Falha ao reiniciar a machine" << machineId
                          << "após remover o snapshot:" << resumeError.code;
        fails(error,
              Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                          QStringLiteral("O snapshot foi removido, mas a machine não "
                                         "reiniciou."),
                          errorText(resumeError),
                          QStringLiteral("Inicie a machine manualmente; o snapshot já foi "
                                         "removido do disco.")));
        return false;
    }
    return true;
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
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível executar o qemu-img."),
                                 process.errorString()));
    }
    if (!process.waitForFinished(kQemuImgTimeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_TIMEOUT"),
                                 QStringLiteral("O qemu-img excedeu o tempo limite."),
                                 diskPath));
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O qemu-img falhou ao criar o disco."),
                                 processDetail(process.readAllStandardError())));
    }

    const QFileInfo diskInfo(diskPath);
    if (!diskInfo.isFile()) {
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O disco qcow2 não foi criado."), diskPath));
    }
    setOwnerOnly(diskPath, false);
    return true;
}

bool MachineManager::resizeDisk(const QString &diskPath, qint64 bytes, Error *error)
{
    if (diskPath.isEmpty() || bytes <= 0) {
        return fails(error, invalidParams(QStringLiteral("Destino de resize inválido."),
                                          diskPath));
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
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível executar o qemu-img."),
                                 process.errorString()));
    }
    if (!process.waitForFinished(kQemuImgTimeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_TIMEOUT"),
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
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
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
                return fails(error,
                             Error::make(QStringLiteral("PODSTACK_CANCELLED"),
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
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O qemu-img falhou ao preparar o disco."),
                                 processDetail(process.readAllStandardError())));
    }
    if (!QFileInfo(targetPath).isFile()) {
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O disco do export não foi gerado."),
                                 targetPath));
    }
    setOwnerOnly(targetPath, false);
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
        if (privateInfo.isSymLink() || !privateInfo.isFile() || publicInfo.isSymLink()
            || !publicInfo.isFile()) {
            return fails(error,
                         Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                     QStringLiteral("A chave SSH existente é inválida."),
                                     keyPath));
        }
        if (!QFile::setPermissions(keyPath, QFileDevice::ReadOwner
                                                | QFileDevice::WriteOwner)) {
            return fails(error,
                         Error::make(QStringLiteral("PODSTACK_PERMISSION_DENIED"),
                                     QStringLiteral("Não foi possível restringir a chave SSH "
                                                    "a 0600."),
                                     keyPath));
        }
        if (!QFile::setPermissions(publicPath, QFileDevice::ReadOwner
                                                   | QFileDevice::WriteOwner
                                                   | QFileDevice::ReadGroup
                                                   | QFileDevice::ReadOther)) {
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
            return fails(error,
                         Error::make(QStringLiteral("PODSTACK_TIMEOUT"),
                                     QStringLiteral("O ssh-keygen excedeu o tempo limite."),
                                     keyPath));
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            return fails(error,
                         Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                     QStringLiteral("O ssh-keygen falhou ao gerar a chave."),
                                     processDetail(process.readAllStandardError())));
        }
        if (!QFileInfo(keyPath).isFile() || !QFileInfo(publicPath).isFile()) {
            return fails(error,
                         Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                     QStringLiteral("O ssh-keygen não criou o par de chaves."),
                                     keyPath));
        }
        if (!QFile::setPermissions(keyPath, QFileDevice::ReadOwner
                                                | QFileDevice::WriteOwner)) {
            return fails(error,
                         Error::make(QStringLiteral("PODSTACK_PERMISSION_DENIED"),
                                     QStringLiteral("Não foi possível restringir a chave SSH "
                                                    "a 0600."),
                                     keyPath));
        }
        if (!QFile::setPermissions(publicPath, QFileDevice::ReadOwner
                                                   | QFileDevice::WriteOwner
                                                   | QFileDevice::ReadGroup
                                                   | QFileDevice::ReadOther)) {
            return fails(error,
                         Error::make(QStringLiteral("PODSTACK_PERMISSION_DENIED"),
                                     QStringLiteral("Não foi possível ajustar a chave pública "
                                                    "para 0644."),
                                     publicPath));
        }
    }

    QFile file(publicPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível ler a chave pública SSH."),
                                 file.errorString()));
    }
    const QByteArray payload = file.read(kMaxPublicKeyBytes);
    const QString key = QString::fromUtf8(payload).trimmed();
    if (key.isEmpty() || key.contains(QLatin1Char('\n'))
        || key.contains(QLatin1Char('\r'))) {
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
        spec.ports.append(QemuPort{QStringLiteral("tcp"), meta.sshPort, 22,
                                   QStringLiteral("127.0.0.1")});
    }

    if (!forStart) {
        return spec;
    }

    if (!QFileInfo(meta.diskPath).isFile()) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                          QStringLiteral("O disco da machine não existe."), meta.diskPath,
                          QStringLiteral("Recrie a machine.")));
        return {};
    }
    if (meta.seedPath.isEmpty() || !QFileInfo(meta.seedPath).isFile()) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                          QStringLiteral("O seed cloud-init da machine não existe."),
                          meta.seedPath, QStringLiteral("Recrie a machine.")));
        return {};
    }

    if (!resolveFirmware(meta.arch, directory, &spec.ovmfCodePath, &spec.ovmfVarsPath, error)) {
        return {};
    }
    return spec;
}

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
        fails(error, Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                 QStringLiteral("A distro resolvida não tem imagem válida."),
                                 distroSpec));
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
        fails(error,
              Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                          QStringLiteral("Já existe uma machine com esse nome."), request.name,
                          QStringLiteral("Escolha outro nome.")));
        return {};
    }

    const DownloadProgress downloadProgress =
        progress ? DownloadProgress([&progress](qint64 received, qint64 total) {
            const int percent =
                total > 0 ? static_cast<int>((received * 70) / total) : 0;
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
        MachinePort{newUuidV7(), sshPort, 22, QStringLiteral("tcp"), false,
                    QStringLiteral("ssh")},
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

MachineMeta MachineManager::update(const QString &idOrName,
                                   const UpdateMachineRequest &request, Error *error)
{
    if (error) {
        *error = {};
    }

    MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
        return {};
    }
    if (!request.cpus && !request.memoryBytes && !request.diskBytes
        && !request.forwardSshAgent) {
        fails(error, invalidParams(QStringLiteral("Informe ao menos um recurso para atualizar "
                                                  "(CPUs, memória, disco ou SSH agent).")));
        return meta;
    }
    if (meta.state == QStringLiteral("provisioning")
        || meta.state == QStringLiteral("deleting")
        || meta.state == QStringLiteral("deleted")) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_CONFLICT"),
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
        fails(error,
              invalidParams(QStringLiteral("Número de CPUs fora do intervalo 1..256."),
                            QString::number(*request.cpus)));
        return meta;
    }
    if (request.memoryBytes && *request.memoryBytes < kMinimumMemoryBytes) {
        fails(error,
              invalidParams(QStringLiteral("Memória insuficiente (mínimo 1 GiB)."),
                            QString::number(*request.memoryBytes)));
        return meta;
    }
    if (request.diskBytes && *request.diskBytes < kMinimumDiskBytes) {
        fails(error,
              invalidParams(QStringLiteral("Disco insuficiente (mínimo 1 GiB)."),
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

    const bool changes =
        (request.cpus && *request.cpus != meta.cpus)
        || (request.memoryBytes && *request.memoryBytes != meta.memoryBytes)
        || (request.diskBytes && *request.diskBytes > meta.diskBytes);
    const bool agentChanges = request.forwardSshAgent
                              && *request.forwardSshAgent != meta.forwardSshAgent;
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
    const bool wasRunning = meta.state == QStringLiteral("running")
                            || meta.state == QStringLiteral("starting")
                            || m_runtime->isRunning(spec, nullptr);

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
                fails(error,
                      invalidParams(
                          QStringLiteral("A machine está em modo isolado e não pode expor "
                                         "portas na LAN."),
                          QStringLiteral("hostPort=%1").arg(port.hostPort),
                          QStringLiteral("Desative exposeLan ou recrie a machine sem o modo "
                                         "isolado.")));
                return meta;
            }
        }
    }
    if (meta.state == QStringLiteral("provisioning")
        || meta.state == QStringLiteral("deleting")
        || meta.state == QStringLiteral("deleted")) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                          QStringLiteral("A machine está em transição; tente novamente."),
                          meta.id));
        return meta;
    }

    std::shared_ptr<std::recursive_mutex> lockHolder;
    std::unique_lock<std::recursive_mutex> machineGuard;
    if (!tryLockMachine(meta.id, &lockHolder, &machineGuard)) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_CONFLICT"),
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
        fails(error,
              invalidParams(QStringLiteral("A machine não tem uma porta SSH válida."),
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
    const bool wasRunning = meta.state == QStringLiteral("running")
                            || meta.state == QStringLiteral("starting")
                            || m_runtime->isRunning(spec, nullptr);

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

MachineMeta MachineManager::rename(const QString &idOrName, const QString &newName,
                                   Error *error)
{
    if (error) {
        *error = {};
    }

    MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
        return {};
    }
    if (meta.state == QStringLiteral("provisioning") || meta.state == QStringLiteral("deleting")
        || meta.state == QStringLiteral("deleted")) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                          QStringLiteral("A machine está em transição; tente novamente."),
                          meta.id));
        return {};
    }

    std::shared_ptr<std::recursive_mutex> lockHolder;
    std::unique_lock<std::recursive_mutex> machineGuard;
    if (!tryLockMachine(meta.id, &lockHolder, &machineGuard)) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_CONFLICT"),
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
            fails(error,
                  Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                              QStringLiteral("Já existe uma machine com esse nome."), targetName,
                              QStringLiteral("Escolha outro nome.")));
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

MachineMeta MachineManager::clone(
    const QString &idOrName, const QString &newName,
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
    if (source.state == QStringLiteral("provisioning")
        || source.state == QStringLiteral("deleting")
        || source.state == QStringLiteral("deleted")) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                          QStringLiteral("A machine está em transição; tente novamente."),
                          source.id));
        return {};
    }

    std::shared_ptr<std::recursive_mutex> lockHolder;
    std::unique_lock<std::recursive_mutex> machineGuard;
    if (!tryLockMachine(source.id, &lockHolder, &machineGuard)) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_CONFLICT"),
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
        fails(error,
              Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                          QStringLiteral("Já existe uma machine com esse nome."), targetName,
                          QStringLiteral("Escolha outro nome.")));
        return {};
    }

    const auto cancelledNow = [&cancelled]() {
        return cancelled && cancelled();
    };
    if (cancelledNow()) {
        fails(error, Error::make(QStringLiteral("PODSTACK_CANCELLED"),
                                 QStringLiteral("Clonagem cancelada.")));
        return {};
    }

    const QemuSpec spec = specFor(source, false, error);
    if (spec.machineId.isEmpty()) {
        return {};
    }
    const bool wasRunning = source.state == QStringLiteral("running")
                            || source.state == QStringLiteral("starting")
                            || m_runtime->isRunning(spec, nullptr);
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
    if (!copyPrivate(source.seedPath, seedPath, &copyError)
        || !copyPrivate(source.sshKeyPath, sshKeyPath, &copyError)
        || !copyPrivate(source.sshKeyPath + QStringLiteral(".pub"), publicKeyPath, &copyError)) {
        cleanup();
        resume();
        if (error) {
            *error = copyError;
        }
        return {};
    }
    if (!QFile::setPermissions(publicKeyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                  | QFileDevice::ReadGroup
                                                  | QFileDevice::ReadOther)) {
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
        MachinePort{newUuidV7(), sshPort, 22, QStringLiteral("tcp"), false,
                    QStringLiteral("ssh")},
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
        fails(error,
              Error::make(code,
                          QStringLiteral("Não foi possível salvar a machine default."),
                          configError,
                          QStringLiteral("Verifique as permissões de ~/.config/podstack.")));
        return false;
    }
    return true;
}

bool MachineManager::sshInfo(const QString &idOrName, SshInfo *info, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!info) {
        return fails(error, invalidParams(QStringLiteral("Destino SSH inválido.")));
    }
    *info = {};

    const MachineMeta meta = load(idOrName, error);
    if (meta.id.isEmpty()) {
        return false;
    }

    const QFileInfo keyInfo(meta.sshKeyPath);
    if (meta.sshKeyPath.isEmpty() || keyInfo.isSymLink() || !keyInfo.isFile()) {
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("A chave SSH da machine não existe."),
                                 meta.id,
                                 QStringLiteral("Recrie a machine para gerar uma nova chave.")));
    }
    if (!QFile::setPermissions(meta.sshKeyPath, QFileDevice::ReadOwner
                                                   | QFileDevice::WriteOwner)) {
        return fails(error,
                     Error::make(QStringLiteral("PODSTACK_PERMISSION_DENIED"),
                                 QStringLiteral("Não foi possível restringir a chave SSH "
                                                "a 0600."),
                                 meta.id));
    }

    Error syncError;
    if (!m_sshConfig.sync(meta, meta.forwardSshAgent, &syncError)) {
        if (error) {
            *error = syncError;
        }
        return false;
    }

    info->host = QStringLiteral("127.0.0.1");
    info->port = meta.sshPort;
    info->user = meta.sshUser;
    info->identityFile = meta.sshKeyPath;
    info->forwardAgent = meta.forwardSshAgent;
    return true;
}

} // namespace PodStack
