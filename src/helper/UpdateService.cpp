// SPDX-License-Identifier: GPL-3.0-or-later

#include "helper/UpdateService.h"

#include "core/Redaction.h"
#include "core/UpdateManifest.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace PodStack {
namespace {

constexpr int kMaxManifestBytes = 512 * 1024;

Error updateError(const QString &code, const QString &message, const QString &detail = {},
                  const QString &remediation = {})
{
    return Error::make(code, message, detail, remediation);
}

QString normalizeArch(QString arch)
{
    arch = arch.trimmed().toLower();
    if (arch == QStringLiteral("amd64") || arch == QStringLiteral("x86_64")) {
        return QStringLiteral("x86_64");
    }
    if (arch == QStringLiteral("arm64") || arch == QStringLiteral("aarch64")) {
        return QStringLiteral("aarch64");
    }
    if (arch == QStringLiteral("i386") || arch == QStringLiteral("i686")) {
        return QStringLiteral("i686");
    }
    return arch;
}

QString hostArch()
{
    return normalizeArch(QSysInfo::buildCpuArchitecture());
}

QString hostFormat()
{
    return QFileInfo::exists(QStringLiteral("/etc/debian_version")) ? QStringLiteral("deb")
                                                                    : QStringLiteral("rpm");
}

QString artifactFormat(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("deb") || suffix == QStringLiteral("rpm") ? suffix : QString();
}

QString readFileLimited(const QString &path, int maxBytes, Error *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível ler %1.").arg(path));
        }
        return {};
    }
    if (file.size() > maxBytes) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"),
                                 QStringLiteral("Arquivo grande demais."), path);
        }
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

class AdmissionLock {
public:
    explicit AdmissionLock(const QString &path)
    {
        m_fd = ::open(QFile::encodeName(path).constData(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (m_fd >= 0) {
            m_locked = ::flock(m_fd, LOCK_EX | LOCK_NB) == 0;
        }
    }
    ~AdmissionLock()
    {
        if (m_fd >= 0) {
            if (m_locked) {
                (void) ::flock(m_fd, LOCK_UN);
            }
            ::close(m_fd);
        }
    }
    bool locked() const { return m_locked; }

private:
    int m_fd = -1;
    bool m_locked = false;
};

bool readStateFile(const QString &path, QVariantMap *state, Error *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Estado do update não encontrado."), path);
        }
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"),
                                 QStringLiteral("Estado do update inválido."), path);
        }
        return false;
    }
    if (state) {
        *state = document.object().toVariantMap();
    }
    return true;
}

} // namespace

UpdateService::UpdateService() : UpdateService(Options{}) {}

UpdateService::UpdateService(Options options) : m_options(std::move(options))
{
    if (m_options.embeddedKey.isEmpty()) {
        m_options.embeddedKey = QString::fromLatin1(PODSTACK_UPDATE_PUBLIC_KEY);
    }
}

bool UpdateService::readReleaseKey(QString *key, Error *error) const
{
    if (!m_options.embeddedKey.trimmed().isEmpty()) {
        if (key) {
            *key = m_options.embeddedKey;
        }
        return true;
    }

    const QFileInfo info(m_options.releaseKeyPath);
    if (!info.isFile() || info.ownerId() != 0 ||
        (info.permissions() & (QFileDevice::WriteGroup | QFileDevice::WriteOther))) {
        if (error) {
            *error = updateError(
                QStringLiteral("PODSTACK_UPDATE_UNSUPPORTED"),
                QStringLiteral("Chave de release do update indisponível."),
                m_options.releaseKeyPath,
                QStringLiteral("Instale o helper atualizado ou forneça /etc/podstack/update.pub "
                               "root-owned."));
        }
        return false;
    }
    if (key) {
        *key = readFileLimited(m_options.releaseKeyPath, 4096, error);
    }
    return key == nullptr || !key->isEmpty();
}

bool UpdateService::validatePackage(const QString &artifactPath, const QString &expectedVersion,
                                    Error *error) const
{
    const QString format = artifactFormat(artifactPath);
    QString tool = m_options.packageTool;
    if (tool.isEmpty()) {
        tool = format == QStringLiteral("deb") ? QStringLiteral("dpkg-deb") : QStringLiteral("rpm");
    }
    const QString executable = QStandardPaths::findExecutable(tool);
    if (executable.isEmpty()) {
        if (error) {
            *error = updateError(
                QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"),
                QStringLiteral("Ferramenta de pacote não encontrada: %1.").arg(tool), artifactPath);
        }
        return false;
    }

    QStringList arguments;
    if (format == QStringLiteral("deb")) {
        arguments = {QStringLiteral("-f"), artifactPath, QStringLiteral("Package"),
                     QStringLiteral("Version"), QStringLiteral("Architecture")};
    } else {
        arguments = {QStringLiteral("-qp"), QStringLiteral("--qf"),
                     QStringLiteral("%{NAME}\n%{VERSION}\n%{ARCH}\n"), artifactPath};
    }

    QProcess process;
    process.start(executable, arguments);
    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished(1000);
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"),
                                 QStringLiteral("A identificação do pacote excedeu o tempo."),
                                 artifactPath);
        }
        return false;
    }
    if (process.exitCode() != 0) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"),
                                 QStringLiteral("Não foi possível identificar o pacote."),
                                 QString::fromUtf8(process.readAllStandardError()));
        }
        return false;
    }

    QString output = QString::fromUtf8(process.readAllStandardOutput());
    QStringList fields;
    for (const QString &line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        QString value = line.trimmed();
        const int colon = value.indexOf(QLatin1Char(':'));
        if (colon > 0) {
            value = value.mid(colon + 1).trimmed();
        }
        fields.append(value);
    }
    if (fields.size() < 3) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"),
                                 QStringLiteral("Identidade do pacote incompleta."), output);
        }
        return false;
    }

    const QString name = fields.at(0);
    const QString version = fields.at(1);
    const QString arch = normalizeArch(fields.at(2));
    if (name != QStringLiteral("podstack")) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"),
                                 QStringLiteral("O artefato não é o pacote PodStack."), name);
        }
        return false;
    }
    if (version != expectedVersion && !version.startsWith(expectedVersion + QLatin1Char('-'))) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"),
                                 QStringLiteral("Versão do pacote não corresponde ao manifesto."),
                                 QStringLiteral("%1 != %2").arg(version, expectedVersion));
        }
        return false;
    }
    if (arch != QStringLiteral("noarch") && arch != hostArch()) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"),
                                 QStringLiteral("Arquitetura do pacote incompatível."),
                                 QStringLiteral("%1 != %2").arg(arch, hostArch()));
        }
        return false;
    }
    return true;
}

QString UpdateService::statePath(const QString &jobId) const
{
    if (!jobId.startsWith(QStringLiteral("update-")) || jobId.contains(QLatin1Char('/')) ||
        jobId.contains(QStringLiteral(".."))) {
        return {};
    }
    return m_options.stateRoot + QLatin1Char('/') + jobId + QStringLiteral("/state.json");
}

bool UpdateService::writeState(const QString &stateDir, const QVariantMap &state,
                               Error *error) const
{
    QDir dir;
    if (!dir.mkpath(stateDir)) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível criar o diretório do update."),
                                 stateDir);
        }
        return false;
    }
    QFile::setPermissions(stateDir,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QString path = stateDir + QStringLiteral("/state.json");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error =
                updateError(QStringLiteral("PODSTACK_IO_ERROR"),
                            QStringLiteral("Não foi possível gravar o estado do update."), path);
        }
        return false;
    }
    file.write(QJsonDocument(QJsonObject::fromVariantMap(state)).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (error) {
            *error =
                updateError(QStringLiteral("PODSTACK_IO_ERROR"),
                            QStringLiteral("Não foi possível concluir o estado do update."), path);
        }
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                    QFileDevice::ReadGroup);
    return true;
}

QVariantMap UpdateService::readState(const QString &stateDir, Error *error) const
{
    QVariantMap state;
    if (!readStateFile(stateDir + QStringLiteral("/state.json"), &state, error)) {
        return {};
    }
    return state;
}

bool UpdateService::activeUpdateUnit(QString *unit, Error *error) const
{
    if (unit) {
        unit->clear();
    }
    const QString executable = QStandardPaths::findExecutable(m_options.systemctl);
    if (executable.isEmpty()) {
        return false;
    }
    QProcess process;
    process.start(executable, {QStringLiteral("list-units"), QStringLiteral("--type=service"),
                               QStringLiteral("--state=active"), QStringLiteral("--no-legend"),
                               QStringLiteral("podstack-update-*")});
    if (!process.waitForFinished(10000)) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_LOCKED"),
                                 QStringLiteral("Não foi possível consultar unidades de update."));
        }
        return true;
    }
    const QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (output.isEmpty()) {
        return false;
    }
    if (unit) {
        *unit = output.section(QLatin1Char(' '), 0, 0);
    }
    return true;
}

QString UpdateService::packageManagerFor(const QString &artifactPath) const
{
    if (artifactFormat(artifactPath) == QStringLiteral("deb")) {
        return QStringLiteral("apt-get");
    }
    return QStringLiteral("dnf");
}

bool UpdateService::stage(uid_t uid, const QByteArray &manifest, const QByteArray &signature,
                          const QString &artifactPath, const QString &channel, QVariantMap *result,
                          Error *error)
{
    if (error) {
        *error = {};
    }
    if (!stageArtifact(uid, manifest, signature, artifactPath, channel, result, error)) {
        return false;
    }
    return true;
}

bool UpdateService::stageArtifact(uid_t uid, const QByteArray &manifest,
                                  const QByteArray &signature, const QString &artifactPath,
                                  const QString &channel, QVariantMap *result, Error *error)
{
    if (uid == 0) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_UNSUPPORTED"),
                                 QStringLiteral("O update assistido exige um usuário."));
        }
        return false;
    }
    if (manifest.isEmpty() || manifest.size() > kMaxManifestBytes || signature.isEmpty()) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"),
                                 QStringLiteral("Manifesto ou assinatura ausente/grande demais."));
        }
        return false;
    }

    const QFileInfo artifactInfo(artifactPath);
    if (!artifactInfo.isAbsolute() || artifactInfo.isSymLink() || !artifactInfo.isFile() ||
        artifactInfo.ownerId() != uid) {
        if (error) {
            *error =
                updateError(QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"),
                            QStringLiteral("Artefato inválido ou de outro usuário."), artifactPath);
        }
        return false;
    }
    if (artifactInfo.size() <= 0 || artifactInfo.size() > m_options.maxArtifactBytes) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"),
                                 QStringLiteral("Tamanho do artefato inválido."), artifactPath);
        }
        return false;
    }

    QString releaseKey;
    if (!readReleaseKey(&releaseKey, error)) {
        return false;
    }
    QString signatureError;
    if (!verifyUpdateSignature(manifest, signature, releaseKey, &signatureError)) {
        if (error) {
            *error =
                updateError(QStringLiteral("PODSTACK_UPDATE_SIGNATURE_INVALID"),
                            QStringLiteral("Assinatura do manifesto inválida."), signatureError);
        }
        return false;
    }

    UpdateManifest parsed;
    QString manifestError;
    if (!parseUpdateManifest(manifest, &parsed, &manifestError)) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"),
                                 QStringLiteral("Manifesto inválido."), manifestError);
        }
        return false;
    }
    if (!parsed.channels.contains(channel)) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"),
                                 QStringLiteral("Canal ausente no manifesto."), channel);
        }
        return false;
    }
    const UpdateRelease release = parsed.channels.value(channel);
    const int versionOrder =
        compareVersions(QString::fromLatin1(PODSTACK_VERSION), release.version);
    if (versionOrder >= 0) {
        if (error) {
            *error = updateError(
                versionOrder == 0 ? QStringLiteral("PODSTACK_UPDATE_VERSION_EQUAL")
                                  : QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"),
                versionOrder == 0 ? QStringLiteral("A versão já está instalada.")
                                  : QStringLiteral("O manifesto oferece uma versão mais antiga."),
                versionOrder == 0 ? QString() : QStringLiteral("version_regression"));
        }
        return false;
    }

    const QString format = artifactFormat(artifactPath);
    if (format.isEmpty() || format != hostFormat()) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"),
                                 QStringLiteral("Formato do artefato incompatível."),
                                 QStringLiteral("%1 != %2").arg(format, hostFormat()));
        }
        return false;
    }
    if (channel != QStringLiteral("stable") && channel != QStringLiteral("beta")) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"),
                                 QStringLiteral("Canal de update inválido."), channel);
        }
        return false;
    }

    bool matched = false;
    QString expectedSha256;
    for (const UpdateArtifact &artifact : release.artifacts) {
        if (artifact.format != format || normalizeArch(artifact.arch) != hostArch()) {
            continue;
        }
        const QString urlName = QUrl(artifact.url).fileName();
        if (!urlName.isEmpty() && urlName != artifactInfo.fileName()) {
            continue;
        }
        expectedSha256 = artifact.sha256;
        matched = true;
        break;
    }
    if (!matched) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"),
                                 QStringLiteral("Nenhum artefato compatível no manifesto."),
                                 QStringLiteral("%1/%2").arg(hostArch(), format));
        }
        return false;
    }

    if (!validatePackage(artifactPath, release.version, error)) {
        return false;
    }

    const QString jobId =
        QStringLiteral("update-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString stateDir = m_options.stateRoot + QLatin1Char('/') + jobId;
    QDir dir;
    if (!dir.mkpath(stateDir)) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível criar o staging do update."),
                                 stateDir);
        }
        return false;
    }
    QFile::setPermissions(stateDir,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    const QString stagingArtifact =
        stateDir + QStringLiteral("/artifact.") + artifactInfo.suffix().toLower();
    const QByteArray source = QFile::encodeName(artifactPath);
    const int sourceFd = ::open(source.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (sourceFd < 0) {
        if (error) {
            *error =
                updateError(QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"),
                            QStringLiteral("Não foi possível abrir o artefato."), artifactPath);
        }
        return false;
    }
    struct stat status{};
    if (::fstat(sourceFd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_uid != uid) {
        ::close(sourceFd);
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"),
                                 QStringLiteral("Artefato inválido após validação."), artifactPath);
        }
        return false;
    }
    const QByteArray destination = QFile::encodeName(stagingArtifact);
    const int destinationFd =
        ::open(destination.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (destinationFd < 0) {
        ::close(sourceFd);
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível criar o staging do artefato."),
                                 stagingArtifact);
        }
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buffer(64LL * 1024, '\0');
    bool copyOk = true;
    for (;;) {
        const ssize_t bytes = ::read(sourceFd, buffer.data(), static_cast<size_t>(buffer.size()));
        if (bytes == 0) {
            break;
        }
        if (bytes < 0) {
            copyOk = false;
            break;
        }
        hash.addData(QByteArrayView(buffer.constData(), static_cast<qsizetype>(bytes)));
        ssize_t written = 0;
        while (written < bytes) {
            const ssize_t chunk = ::write(destinationFd, buffer.constData() + written,
                                          static_cast<size_t>(bytes - written));
            if (chunk <= 0) {
                copyOk = false;
                break;
            }
            written += chunk;
        }
        if (!copyOk) {
            break;
        }
    }
    ::close(sourceFd);
    ::close(destinationFd);
    if (!copyOk ||
        hash.result().toHex().compare(expectedSha256.toLatin1(), Qt::CaseInsensitive) != 0) {
        QFile::remove(stagingArtifact);
        if (error) {
            *error = updateError(
                QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"),
                QStringLiteral("SHA-256 do artefato não corresponde ao manifesto."), artifactPath);
        }
        return false;
    }
    QFile::setPermissions(stagingArtifact, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    QVariantMap state;
    state.insert(QStringLiteral("jobId"), jobId);
    state.insert(QStringLiteral("uid"), static_cast<uint>(uid));
    state.insert(QStringLiteral("phase"), QStringLiteral("staged"));
    state.insert(QStringLiteral("percent"), 0);
    state.insert(QStringLiteral("message"), QStringLiteral("Artefato validado."));
    state.insert(QStringLiteral("artifact"), stagingArtifact);
    state.insert(QStringLiteral("manager"), packageManagerFor(stagingArtifact));
    state.insert(QStringLiteral("version"), release.version);
    state.insert(QStringLiteral("channel"), channel);
    state.insert(QStringLiteral("unit"), QString());
    state.insert(QStringLiteral("exitCode"), 0);
    state.insert(QStringLiteral("errorCode"), QString());
    state.insert(QStringLiteral("timestamp"),
                 QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!writeState(stateDir, state, error)) {
        return false;
    }

    if (result) {
        result->insert(QStringLiteral("ok"), true);
        result->insert(QStringLiteral("message"), QStringLiteral("Artefato validado."));
        result->insert(QStringLiteral("jobId"), jobId);
        result->insert(QStringLiteral("stateDir"), stateDir);
        result->insert(QStringLiteral("version"), release.version);
    }
    return true;
}

bool UpdateService::install(uid_t uid, const QString &jobId, QVariantMap *result, Error *error)
{
    if (error) {
        *error = {};
    }
    const QString path = statePath(jobId);
    if (path.isEmpty()) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("jobId inválido."), jobId);
        }
        return false;
    }
    const QString stateDir = QFileInfo(path).absolutePath();
    QVariantMap state = readState(stateDir, error);
    if (state.isEmpty() || state.value(QStringLiteral("uid")).toUInt() != uid) {
        if (error && error->isEmpty()) {
            *error = updateError(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Update não encontrado para este usuário."), jobId);
        }
        return false;
    }
    if (state.value(QStringLiteral("phase")).toString() != QStringLiteral("staged")) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_LOCKED"),
                                 QStringLiteral("Update não está pronto para instalar."), jobId);
        }
        return false;
    }

    AdmissionLock admission(m_options.admissionLock);
    if (!admission.locked()) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_LOCKED"),
                                 QStringLiteral("Outro update está em andamento."));
        }
        return false;
    }
    QString activeUnit;
    if (activeUpdateUnit(&activeUnit, error)) {
        if (error) {
            *error =
                updateError(QStringLiteral("PODSTACK_UPDATE_LOCKED"),
                            QStringLiteral("Já existe uma unidade de update ativa."), activeUnit);
        }
        return false;
    }

    const QString artifact = state.value(QStringLiteral("artifact")).toString();
    const QString manager = state.value(QStringLiteral("manager")).toString();
    const QString unit =
        QStringLiteral("podstack-update-") + jobId.mid(QStringLiteral("update-").size());
    const QString logFile = m_options.logRoot + QLatin1Char('/') + QString::number(uid) +
                            QLatin1Char('/') + jobId + QStringLiteral(".jsonl");

    QStringList arguments;
    arguments << QStringLiteral("--unit=") + unit << QStringLiteral("--collect")
              << QStringLiteral("--property=RuntimeMaxSec=%1").arg(m_options.installTimeoutSeconds)
              << QStringLiteral("--description=PodStack update") << QStringLiteral("--")
              << QStringLiteral("flock") << m_options.updateLock << m_options.wrapperPath
              << QStringLiteral("--job-id") << jobId << QStringLiteral("--uid")
              << QString::number(uid) << QStringLiteral("--state-dir") << stateDir
              << QStringLiteral("--log-file") << logFile << QStringLiteral("--artifact") << artifact
              << QStringLiteral("--manager") << manager;

    state.insert(QStringLiteral("phase"), QStringLiteral("installing"));
    state.insert(QStringLiteral("unit"), unit);
    state.insert(QStringLiteral("message"), QStringLiteral("Instalação iniciada."));
    if (!writeState(stateDir, state, error)) {
        return false;
    }

    QProcess process;
    process.start(m_options.systemdRun, arguments);
    if (!process.waitForStarted(5000)) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_INSTALL_FAILED"),
                                 QStringLiteral("Não foi possível iniciar a unidade de update."),
                                 process.errorString());
        }
        state.insert(QStringLiteral("phase"), QStringLiteral("failed"));
        state.insert(QStringLiteral("message"),
                     QStringLiteral("Não foi possível iniciar a unidade."));
        state.insert(QStringLiteral("errorCode"), QStringLiteral("PODSTACK_UPDATE_INSTALL_FAILED"));
        (void) writeState(stateDir, state, nullptr);
        return false;
    }
    if (!process.waitForFinished(10000) || process.exitCode() != 0) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_UPDATE_INSTALL_FAILED"),
                                 QStringLiteral("systemd-run recusou a unidade de update."),
                                 QString::fromUtf8(process.readAllStandardError()).trimmed());
        }
        state.insert(QStringLiteral("phase"), QStringLiteral("failed"));
        state.insert(QStringLiteral("message"), QStringLiteral("systemd-run recusou a unidade."));
        state.insert(QStringLiteral("errorCode"), QStringLiteral("PODSTACK_UPDATE_INSTALL_FAILED"));
        (void) writeState(stateDir, state, nullptr);
        return false;
    }

    if (result) {
        result->insert(QStringLiteral("ok"), true);
        result->insert(QStringLiteral("message"), QStringLiteral("Instalação iniciada."));
        result->insert(QStringLiteral("jobId"), jobId);
        result->insert(QStringLiteral("unit"), unit);
        result->insert(QStringLiteral("stateDir"), stateDir);
    }
    return true;
}

bool UpdateService::logs(uid_t uid, const QString &jobId, QVariantList *lines, bool *truncated,
                         Error *error) const
{
    if (error) {
        *error = {};
    }
    if (lines) {
        lines->clear();
    }
    if (truncated) {
        *truncated = false;
    }

    const QString directory = m_options.logRoot + QLatin1Char('/') + QString::number(uid);
    QStringList files;
    if (!jobId.isEmpty()) {
        if (!jobId.startsWith(QStringLiteral("update-")) || jobId.contains(QLatin1Char('/')) ||
            jobId.contains(QStringLiteral(".."))) {
            if (error) {
                *error = updateError(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("jobId inválido."), jobId);
            }
            return false;
        }
        const QString path = directory + QLatin1Char('/') + jobId + QStringLiteral(".jsonl");
        if (!QFileInfo::exists(path)) {
            if (error) {
                *error = updateError(QStringLiteral("PODSTACK_NOT_FOUND"),
                                     QStringLiteral("Log do update não encontrado."), jobId);
            }
            return false;
        }
        files.append(path);
    } else {
        const QDir dir(directory);
        files = dir.entryList({QStringLiteral("*.jsonl")}, QDir::Files | QDir::Readable);
        files.sort();
        for (QString &file : files) {
            file = dir.filePath(file);
        }
    }

    constexpr qint64 kMaxBytes = 1024LL * 1024;
    constexpr int kMaxLines = 10000;
    qint64 bytes = 0;
    const auto appendLine = [&](const QString &line) {
        bytes += line.toUtf8().size();
        if (bytes > kMaxBytes || (lines && lines->size() >= kMaxLines)) {
            if (truncated) {
                *truncated = true;
            }
            return false;
        }
        if (lines && !line.trimmed().isEmpty()) {
            lines->append(Redaction::text(line));
        }
        return true;
    };

    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-7);
    for (const QString &path : files) {
        if (QFileInfo(path).lastModified() < cutoff) {
            continue;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        while (!file.atEnd()) {
            if (!appendLine(QString::fromUtf8(file.readLine()).trimmed())) {
                return true;
            }
        }
    }

    // O wrapper espelha as linhas dos scriptlets no journal com JOB_ID/UID.
    const QString journalctl = m_options.includeJournal
                                   ? QStandardPaths::findExecutable(QStringLiteral("journalctl"))
                                   : QString();
    if (!journalctl.isEmpty()) {
        QProcess journal;
        journal.start(journalctl,
                      {QStringLiteral("-t"), QStringLiteral("podstack-update"),
                       QStringLiteral("--since"), QStringLiteral("7 days ago"),
                       QStringLiteral("--no-pager"), QStringLiteral("-o"), QStringLiteral("cat")});
        if (journal.waitForFinished(10000) && journal.exitCode() == 0) {
            const QString jobNeedle =
                jobId.isEmpty() ? QString() : QStringLiteral("JOB_ID=%1").arg(jobId);
            const QString uidNeedle = QStringLiteral("UID=%1").arg(uid);
            for (const QByteArray &raw : journal.readAllStandardOutput().split('\n')) {
                const QString line = QString::fromUtf8(raw).trimmed();
                if (line.isEmpty()) {
                    continue;
                }
                if (!jobNeedle.isEmpty() && !line.contains(jobNeedle)) {
                    continue;
                }
                if (jobNeedle.isEmpty() && !line.contains(uidNeedle)) {
                    continue;
                }
                if (!appendLine(line)) {
                    return true;
                }
            }
        }
    }
    return true;
}

bool UpdateService::status(uid_t uid, const QString &jobId, QVariantMap *result, Error *error)
{
    if (error) {
        *error = {};
    }
    const QString path = statePath(jobId);
    if (path.isEmpty()) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("jobId inválido."), jobId);
        }
        return false;
    }
    const QString stateDir = QFileInfo(path).absolutePath();
    const QVariantMap state = readState(stateDir, error);
    if (state.isEmpty()) {
        return false;
    }
    if (state.value(QStringLiteral("uid")).toUInt() != uid) {
        if (error) {
            *error = updateError(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Update não encontrado para este usuário."), jobId);
        }
        return false;
    }
    if (result) {
        *result = state;
        result->insert(QStringLiteral("ok"), true);
    }
    return true;
}

} // namespace PodStack
