// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/MachineManager.h"

#include "core/Config.h"
#include "core/Ids.h"
#include "core/Logging.h"
#include "core/Paths.h"
#include "core/Proxy.h"
#include "machines/CloudInit.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTcpServer>

#include <utility>

namespace PodStack {
namespace {

constexpr qint64 kMinimumMemoryBytes = 1024LL * 1024LL * 1024LL;
constexpr qint64 kMinimumDiskBytes = 1024LL * 1024LL * 1024LL;
constexpr int kMaximumCpus = 256;
constexpr int kPortAttempts = 20;
constexpr int kQemuImgStartTimeoutMs = 5000;
constexpr int kQemuImgTimeoutMs = 60000;
constexpr int kSshKeygenStartTimeoutMs = 5000;
constexpr int kSshKeygenTimeoutMs = 30000;
constexpr int kMaxProcessOutput = 4096;
constexpr qint64 kMaxPublicKeyBytes = 64 * 1024;

const QString kOvmfCodeAmd64 = QStringLiteral("/usr/share/edk2/ovmf/OVMF_CODE.fd");
const QString kOvmfVarsAmd64 = QStringLiteral("/usr/share/edk2/ovmf/OVMF_VARS.fd");
const QString kAavmfCodeArm64 = QStringLiteral("/usr/share/AAVMF/AAVMF_CODE.fd");
const QString kAavmfVarsArm64 = QStringLiteral("/usr/share/AAVMF/AAVMF_VARS.fd");

Error invalidParams(const QString &message, const QString &detail = {},
                    const QString &remediation = {})
{
    return Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"), message, detail, remediation);
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
    static const QRegularExpression namePattern(
        QStringLiteral("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$"));

    if (request.name.isEmpty() || request.name.size() > 63
        || !namePattern.match(request.name).hasMatch()) {
        return fails(error,
                     invalidParams(QStringLiteral("Nome de machine inválido."), request.name,
                                   QStringLiteral("Use 1..63 caracteres [a-z0-9-], começando e "
                                                  "terminando por letra ou número.")));
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
    return true;
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
        if (meta.sshPort > 0) {
            usedPorts.insert(meta.sshPort);
        }
        for (const MachinePort &port : meta.ports) {
            if (port.hostPort > 0) {
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

bool MachineManager::createDisk(const QString &diskPath, const QString &backingPath,
                                qint64 diskBytes, Error *error)
{
    QString binary = m_options.qemuImgBinary;
    if (binary.isEmpty()) {
        binary = QStandardPaths::findExecutable(QStringLiteral("qemu-img"));
    }
    if (binary.isEmpty()) {
        return fails(error,
                     Error::make(
                         QStringLiteral("PODSTACK_UNSUPPORTED"),
                         QStringLiteral("O binário qemu-img não foi encontrado."),
                         QStringLiteral("qemu-img"),
                         QStringLiteral("Instale o QEMU/KVM (qemu-kvm no Fedora ou qemu-utils "
                                        "no Debian/Ubuntu).")));
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

    const QString directory = machineDirFor(meta.id);
    if (directory.isEmpty()) {
        fails(error, invalidParams(QStringLiteral("Identificador de machine inválido."), meta.id));
        return {};
    }

    spec.qmpSocketPath = directory + QStringLiteral("/qmp.sock");
    spec.consoleLogPath = directory + QStringLiteral("/console.log");
    spec.pidFile = directory + QStringLiteral("/qemu.pid");

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
    meta.isolated = false;
    meta.isolateNetwork = request.isolateNetwork;
    meta.forwardSshAgent = request.forwardSshAgent;
    meta.createdAt = timestamp;
    meta.updatedAt = timestamp;
    meta.ports = {
        MachinePort{newUuidV7(), sshPort, 22, QStringLiteral("tcp"), false,
                    QStringLiteral("ssh")},
    };

    if (!persist(meta, error)) {
        m_store.remove(meta.id, nullptr);
        cleanupDirectory(directory);
        return {};
    }

    Error sshConfigError;
    if (!m_sshConfig.sync(meta, request.forwardSshAgent, &sshConfigError)) {
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

    Error startError;
    if (!m_runtime->start(spec, &startError)) {
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

    Error startError;
    if (!m_runtime->start(spec, &startError)) {
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

    const QemuSpec spec = specFor(meta, false, error);
    if (spec.machineId.isEmpty()) {
        return meta;
    }

    if (meta.state == QStringLiteral("stopped")
        && !m_runtime->isRunning(spec, nullptr)) {
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

    const MachineMeta stopped = stop(idOrName, false, error);
    if (error && !error->isEmpty()) {
        return stopped;
    }
    return start(idOrName, error);
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

    Error stopError;
    stop(meta.id, force, &stopError);
    if (!stopError.isEmpty() && !force) {
        if (error) {
            *error = stopError;
        }
        return false;
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
    return true;
}

} // namespace PodStack
