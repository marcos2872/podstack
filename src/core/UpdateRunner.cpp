// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/UpdateRunner.h"

#include "core/Config.h"
#include "core/Paths.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace PodStack {
namespace {

constexpr qint64 kManifestTtlMs = 6LL * 60 * 60 * 1000;
constexpr int kMaxManifestBytes = 512 * 1024;
constexpr auto kDefaultFeedUrl =
    "https://github.com/marcos2872/podstack/releases/latest/download/podstack-update.json";

QString normalizeArch(QString arch)
{
    arch = arch.trimmed().toLower();
    if (arch == QStringLiteral("amd64") || arch == QStringLiteral("x86_64")) {
        return QStringLiteral("x86_64");
    }
    if (arch == QStringLiteral("arm64") || arch == QStringLiteral("aarch64")) {
        return QStringLiteral("aarch64");
    }
    return arch;
}

QString hostArchValue()
{
    return normalizeArch(QSysInfo::buildCpuArchitecture());
}

QString hostFormatValue()
{
    return QFileInfo::exists(QStringLiteral("/etc/debian_version")) ? QStringLiteral("deb")
                                                                    : QStringLiteral("rpm");
}

Error runnerError(const QString &code, const QString &message, const QString &detail = {})
{
    return Error::make(code, message, detail);
}

QByteArray sha256(const QByteArray &payload)
{
    return QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
}

} // namespace

UpdateRunner::UpdateRunner(UpdateHelper *helper, UpdateRunnerOptions options)
    : m_helper(helper)
    , m_options(std::move(options))
{
    if (m_options.cacheDir.isEmpty()) {
        m_options.cacheDir = Paths::cacheDir() + QStringLiteral("/updates");
    }
    if (m_options.stateDir.isEmpty()) {
        m_options.stateDir = Paths::stateDir() + QStringLiteral("/updates");
    }
}

QString UpdateRunner::hostArch() const
{
    return hostArchValue();
}

QString UpdateRunner::hostFormat() const
{
    return hostFormatValue();
}

QString UpdateRunner::cachePath(const QString &name) const
{
    return m_options.cacheDir + QLatin1Char('/') + name;
}

QString UpdateRunner::statePathValue() const
{
    return m_options.stateDir + QStringLiteral("/state.json");
}

QString UpdateRunner::statePath() const
{
    return statePathValue();
}

bool UpdateRunner::download(const QString &url, int maxBytes, QByteArray *body, Error *error)
{
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer.start(m_options.downloadTimeoutMs);
    loop.exec();
    timer.stop();

    const bool timedOut = reply->error() == QNetworkReply::OperationCanceledError;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray payload = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkMessage = reply->errorString();
    reply->deleteLater();

    if (timedOut) {
        if (error) {
            *error = runnerError(QStringLiteral("PODSTACK_TIMEOUT"),
                                 QStringLiteral("O download do update excedeu o tempo limite."),
                                 url);
        }
        return false;
    }
    if (networkError != QNetworkReply::NoError) {
        if (error) {
            *error = runnerError(QStringLiteral("PODSTACK_UPDATE_DOWNLOAD_FAILED"),
                                 QStringLiteral("Falha ao baixar o update."), networkMessage);
        }
        return false;
    }
    if (status != 0 && status != 200) {
        if (error) {
            *error = runnerError(QStringLiteral("PODSTACK_UPDATE_DOWNLOAD_FAILED"),
                                 QStringLiteral("O servidor respondeu HTTP %1.").arg(status), url);
        }
        return false;
    }
    if (payload.size() > maxBytes) {
        if (error) {
            *error = runnerError(QStringLiteral("PODSTACK_UPDATE_DOWNLOAD_FAILED"),
                                 QStringLiteral("O download excedeu o limite permitido."), url);
        }
        return false;
    }
    if (body) {
        *body = payload;
    }
    return true;
}

bool UpdateRunner::checkCustomKey(Error *error)
{
    const QString embedded = QString::fromLatin1(PODSTACK_UPDATE_PUBLIC_KEY).trimmed();
    if (!embedded.isEmpty() && !m_options.publicKey.isEmpty()
        && m_options.publicKey.trimmed() != embedded) {
        if (error) {
            *error = runnerError(
                QStringLiteral("PODSTACK_UPDATE_UNSUPPORTED"),
                QStringLiteral("O install assistido usa a chave de release do helper, não "
                               "updates.publicKey; use o caminho manual."),
                QStringLiteral("reason=custom_key"));
        }
        return false;
    }
    if (m_options.publicKey.trimmed().isEmpty()) {
        if (error) {
            *error = runnerError(QStringLiteral("PODSTACK_UPDATE_UNSUPPORTED"),
                                 QStringLiteral("Chave pública de updates não configurada."));
        }
        return false;
    }
    return true;
}

bool UpdateRunner::loadManifest(const QByteArray &manifest, const QByteArray &signature,
                                Error *error)
{
    QString signatureError;
    if (!verifyUpdateSignature(manifest, signature, m_options.publicKey, &signatureError)) {
        if (error) {
            *error = runnerError(QStringLiteral("PODSTACK_UPDATE_SIGNATURE_INVALID"),
                                 QStringLiteral("Assinatura do manifesto inválida."),
                                 signatureError);
        }
        return false;
    }

    UpdateManifest parsed;
    QString manifestError;
    if (!parseUpdateManifest(manifest, &parsed, &manifestError)) {
        if (error) {
            *error = runnerError(QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"),
                                 QStringLiteral("Manifesto de updates inválido."), manifestError);
        }
        return false;
    }
    if (!parsed.channels.contains(m_channel)) {
        if (error) {
            *error = runnerError(QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"),
                                 QStringLiteral("O manifesto não publica o canal %1.")
                                     .arg(m_channel));
        }
        return false;
    }

    const UpdateRelease release = parsed.channels.value(m_channel);
    const int order = compareVersions(QString::fromLatin1(PODSTACK_VERSION), release.version);
    if (order == 0) {
        if (error) {
            *error = runnerError(QStringLiteral("PODSTACK_UPDATE_VERSION_EQUAL"),
                                 QStringLiteral("A versão já está instalada."), release.version);
        }
        return false;
    }
    if (order > 0) {
        if (error) {
            *error = runnerError(QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"),
                                 QStringLiteral("O manifesto oferece uma versão mais antiga."),
                                 QStringLiteral("version_regression"));
        }
        return false;
    }

    bool found = false;
    for (const UpdateArtifact &artifact : release.artifacts) {
        if (artifact.format != hostFormat() || normalizeArch(artifact.arch) != hostArch()) {
            continue;
        }
        m_release = release;
        m_manifest = parsed;
        m_version = release.version;
        m_manifestBytes = manifest;
        m_signatureBytes = signature;
        found = true;
        m_artifactUrl = artifact.url;
        m_expectedSha256 = artifact.sha256;
        break;
    }
    if (!found) {
        if (error) {
            *error = runnerError(QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"),
                                 QStringLiteral("Nenhum artefato compatível com %1/%2.")
                                     .arg(hostFormat(), hostArch()));
        }
        return false;
    }
    return true;
}

bool UpdateRunner::fetchArtifact(Error *error)
{
    if (m_artifactUrl.isEmpty()) {
        if (error) {
            *error = runnerError(QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"),
                                 QStringLiteral("Manifesto sem URL de artefato."));
        }
        return false;
    }
    QString fileName = QUrl(m_artifactUrl).fileName();
    if (fileName.isEmpty() || fileName.contains(QLatin1Char('/'))) {
        fileName = QStringLiteral("podstack-%1-%2.%3")
                       .arg(m_version, hostArch(), hostFormat());
    }
    const QString path = cachePath(fileName);
    QDir().mkpath(m_options.cacheDir);
    QFile::setPermissions(m_options.cacheDir,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner
                              | QFileDevice::ExeOwner);

    if (QFileInfo::exists(path)) {
        QFile cached(path);
        if (cached.open(QIODevice::ReadOnly)) {
            const QByteArray payload = cached.readAll();
            if (sha256(payload).compare(m_expectedSha256.toLatin1(), Qt::CaseInsensitive) == 0) {
                m_artifactPath = path;
                return true;
            }
        }
    }

    const QStorageInfo storage(m_options.cacheDir);
    if (storage.isValid() && storage.bytesAvailable() < 64LL * 1024 * 1024) {
        if (error) {
            *error = runnerError(QStringLiteral("PODSTACK_UPDATE_DOWNLOAD_FAILED"),
                                 QStringLiteral("Espaço insuficiente no cache de updates."));
        }
        return false;
    }

    QByteArray payload;
    if (!download(m_artifactUrl, static_cast<int>(m_options.maxArtifactBytes), &payload, error)) {
        return false;
    }
    if (m_expectedSha256.isEmpty()
        || sha256(payload).compare(m_expectedSha256.toLatin1(), Qt::CaseInsensitive) != 0) {
        if (error) {
            *error = runnerError(
                QStringLiteral("PODSTACK_UPDATE_PACKAGE_INVALID"),
                QStringLiteral("SHA-256 do artefato não corresponde ao manifesto."));
        }
        return false;
    }

    const QString part = path + QStringLiteral(".part");
    QFile file(part);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = runnerError(
                QStringLiteral("PODSTACK_UPDATE_DOWNLOAD_FAILED"),
                QStringLiteral("Não foi possível gravar o artefato no cache."), part);
        }
        return false;
    }
    if (file.write(payload) != payload.size()) {
        file.close();
        QFile::remove(part);
        if (error) {
            *error = runnerError(QStringLiteral("PODSTACK_UPDATE_DOWNLOAD_FAILED"),
                                 QStringLiteral("Falha ao gravar o artefato no cache."), part);
        }
        return false;
    }
    file.close();
    QFile::setPermissions(part, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QFile::remove(path);
    if (!QFile::rename(part, path)) {
        QFile::remove(part);
        if (error) {
            *error = runnerError(QStringLiteral("PODSTACK_UPDATE_DOWNLOAD_FAILED"),
                                 QStringLiteral("Não foi possível concluir o download."), path);
        }
        return false;
    }
    m_artifactPath = path;
    return true;
}

bool UpdateRunner::prepare(Error *error)
{
    if (error) {
        *error = {};
    }
    if (!m_helper) {
        if (error) {
            *error = runnerError(QStringLiteral("PODSTACK_UPDATE_INSTALL_FAILED"),
                                 QStringLiteral("Helper do update indisponível."));
        }
        return false;
    }

    const Config config = Config::load();
    m_channel = m_options.channel.isEmpty()
                    ? config.get(QStringLiteral("updates.channel")).toString()
                    : m_options.channel;
    if (m_channel != QStringLiteral("stable") && m_channel != QStringLiteral("beta")) {
        if (error) {
            *error = runnerError(QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"),
                                 QStringLiteral("Canal de update inválido."), m_channel);
        }
        return false;
    }
    QString feedUrl = m_options.feedUrl;
    if (feedUrl.isEmpty()) {
        feedUrl = config.get(QStringLiteral("updates.feedUrl")).toString();
    }
    if (feedUrl.isEmpty()) {
        feedUrl = QString::fromLatin1(kDefaultFeedUrl);
    }
    m_options.feedUrl = feedUrl;
    if (m_options.publicKey.isEmpty()) {
        m_options.publicKey = config.get(QStringLiteral("updates.publicKey")).toString();
    }
    if (!checkCustomKey(error)) {
        return false;
    }

    QDir().mkpath(m_options.cacheDir);
    QFile::setPermissions(m_options.cacheDir,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner
                              | QFileDevice::ExeOwner);

    const QString manifestPath = cachePath(QStringLiteral("manifest.json"));
    const QString signaturePath = cachePath(QStringLiteral("manifest.json.minisig"));
    bool cacheValid = false;
    const qint64 manifestAge =
        QDateTime::currentMSecsSinceEpoch()
        - QFileInfo(manifestPath).lastModified().toMSecsSinceEpoch();
    if (QFileInfo::exists(manifestPath) && QFileInfo::exists(signaturePath)
        && manifestAge < kManifestTtlMs) {
        QFile manifestFile(manifestPath);
        QFile signatureFile(signaturePath);
        if (manifestFile.open(QIODevice::ReadOnly) && signatureFile.open(QIODevice::ReadOnly)) {
            m_manifestBytes = manifestFile.readAll();
            m_signatureBytes = signatureFile.readAll();
            cacheValid = !m_manifestBytes.isEmpty() && !m_signatureBytes.isEmpty();
        }
    }

    if (!cacheValid) {
        if (!download(feedUrl, kMaxManifestBytes, &m_manifestBytes, error)
            || !download(feedUrl + QStringLiteral(".minisig"), kMaxManifestBytes,
                         &m_signatureBytes, error)) {
            return false;
        }
        QFile manifestFile(manifestPath);
        QFile signatureFile(signaturePath);
        if (manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
            && signatureFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            manifestFile.write(m_manifestBytes);
            signatureFile.write(m_signatureBytes);
            manifestFile.close();
            signatureFile.close();
            QFile::setPermissions(manifestPath,
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            QFile::setPermissions(signaturePath,
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        }
    }

    if (!loadManifest(m_manifestBytes, m_signatureBytes, error)) {
        QFile::remove(manifestPath);
        QFile::remove(signaturePath);
        return false;
    }
    if (!fetchArtifact(error)) {
        return false;
    }
    return true;
}

bool UpdateRunner::prepareFromManifest(const QString &manifestPath, Error *error)
{
    if (error) {
        *error = {};
    }
    QFile manifestFile(manifestPath);
    QFile signatureFile(manifestPath + QStringLiteral(".minisig"));
    if (!manifestFile.open(QIODevice::ReadOnly) || !signatureFile.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = runnerError(QStringLiteral("PODSTACK_UPDATE_MANIFEST_INVALID"),
                                 QStringLiteral("Manifesto ou assinatura não encontrados."),
                                 manifestPath);
        }
        return false;
    }
    m_manifestBytes = manifestFile.readAll();
    m_signatureBytes = signatureFile.readAll();
    const Config config = Config::load();
    m_channel = m_options.channel.isEmpty()
                    ? config.get(QStringLiteral("updates.channel")).toString()
                    : m_options.channel;
    if (m_options.publicKey.isEmpty()) {
        m_options.publicKey = config.get(QStringLiteral("updates.publicKey")).toString();
    }
    if (!checkCustomKey(error)) {
        return false;
    }
    if (!loadManifest(m_manifestBytes, m_signatureBytes, error)) {
        return false;
    }
    return fetchArtifact(error);
}

bool UpdateRunner::runSystemctl(const QStringList &arguments, Error *error)
{
    if (arguments.isEmpty()) {
        return true;
    }
    QProcess process;
    process.start(m_options.systemctlBinary, arguments);
    if (!process.waitForStarted(5000) || !process.waitForFinished(30000)
        || process.exitCode() != 0) {
        if (error) {
            *error = runnerError(
                QStringLiteral("PODSTACK_UPDATE_INSTALL_FAILED"),
                QStringLiteral("Não foi possível controlar o serviço do PodStack."),
                QString::fromUtf8(process.readAllStandardError()).trimmed());
        }
        return false;
    }
    return true;
}

bool UpdateRunner::writeState(const QString &phase, const QString &message, Error *error)
{
    QDir().mkpath(m_options.stateDir);
    QFile::setPermissions(m_options.stateDir,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner
                              | QFileDevice::ExeOwner);
    QFile boot(QStringLiteral("/proc/sys/kernel/random/boot_id"));
    QString bootId;
    if (boot.open(QIODevice::ReadOnly)) {
        bootId = QString::fromUtf8(boot.readAll()).trimmed();
    }
    QJsonObject state;
    state.insert(QStringLiteral("phase"), phase);
    state.insert(QStringLiteral("message"), message);
    state.insert(QStringLiteral("jobId"), m_jobId);
    state.insert(QStringLiteral("unit"), m_unit);
    state.insert(QStringLiteral("stateDir"), m_stateDir);
    state.insert(QStringLiteral("version"), m_version);
    state.insert(QStringLiteral("channel"), m_channel);
    state.insert(QStringLiteral("pid"), QCoreApplication::applicationPid());
    state.insert(QStringLiteral("boot_id"), bootId);
    state.insert(QStringLiteral("timestamp"),
                 QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QSaveFile file(statePathValue());
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = runnerError(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível gravar o estado do update."),
                                 statePathValue());
        }
        return false;
    }
    file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (error) {
            *error = runnerError(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível concluir o estado do update."),
                                 statePathValue());
        }
        return false;
    }
    QFile::setPermissions(statePathValue(),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    m_phase = phase;
    return true;
}

bool UpdateRunner::pollStatus(Error *error)
{
    QElapsedTimer timer;
    timer.start();
    int delayMs = std::max(100, m_options.pollIntervalMs);
    while (timer.elapsed() < m_options.installTimeoutMs) {
        QVariantMap reply;
        if (!m_helper->call(QStringLiteral("GetUpdateStatus"),
                            {m_jobId}, &reply, error)) {
            if (error && (error->code.contains(QStringLiteral("NOT_FOUND"))
                          || error->code.contains(QStringLiteral("LOCKED")))) {
                QThread::msleep(static_cast<unsigned long>(delayMs));
                delayMs = std::min(delayMs * 2, 5000);
                continue;
            }
            return false;
        }
        const QString phase = reply.value(QStringLiteral("phase")).toString();
        if (!phase.isEmpty()) {
            (void) writeState(phase, reply.value(QStringLiteral("message")).toString(), nullptr);
        }
        if (phase == QStringLiteral("done")) {
            return true;
        }
        if (phase == QStringLiteral("failed")) {
            if (error) {
                *error = runnerError(
                    reply.value(QStringLiteral("errorCode")).toString().isEmpty()
                        ? QStringLiteral("PODSTACK_UPDATE_INSTALL_FAILED")
                        : reply.value(QStringLiteral("errorCode")).toString(),
                    reply.value(QStringLiteral("message")).toString(),
                    QStringLiteral("exitCode=%1")
                        .arg(reply.value(QStringLiteral("exitCode")).toInt()));
            }
            return false;
        }
        QThread::msleep(static_cast<unsigned long>(delayMs));
        delayMs = std::min(delayMs * 2, 5000);
    }
    if (error) {
        *error = runnerError(QStringLiteral("PODSTACK_TIMEOUT"),
                             QStringLiteral("A instalação do update excedeu o tempo limite."));
    }
    return false;
}

bool UpdateRunner::install(Error *error)
{
    if (error) {
        *error = {};
    }
    if (m_manifestBytes.isEmpty() || m_artifactPath.isEmpty()) {
        if (error) {
            *error = runnerError(QStringLiteral("PODSTACK_UPDATE_INSTALL_FAILED"),
                                 QStringLiteral("O update ainda não foi preparado."));
        }
        return false;
    }

    if (!writeState(QStringLiteral("queued"), QStringLiteral("Update na fila."), error)) {
        return false;
    }

    bool servicesStopped = false;
    if (m_options.restartServices) {
        if (!runSystemctl({QStringLiteral("--user"), QStringLiteral("stop"),
                           QStringLiteral("podstack.socket"), QStringLiteral("podstack.service")},
                          error)) {
            (void) writeState(QStringLiteral("failed"),
                              error ? error->message : QStringLiteral("Falha ao parar serviços."),
                              nullptr);
            return false;
        }
        servicesStopped = true;
    }

    if (!writeState(QStringLiteral("stopping"),
                    QStringLiteral("Serviços parados; preparando o pacote."), error)) {
        if (servicesStopped) {
            (void) runSystemctl({QStringLiteral("--user"), QStringLiteral("start"),
                                 QStringLiteral("podstack.socket")},
                                nullptr);
        }
        return false;
    }

    QVariantMap stageReply;
    if (!m_helper->call(QStringLiteral("StageUpdate"),
                        {m_manifestBytes, m_signatureBytes, m_artifactPath, m_channel},
                        &stageReply, error)) {
        (void) writeState(QStringLiteral("failed"), error ? error->message : QString(), nullptr);
        if (servicesStopped) {
            (void) runSystemctl({QStringLiteral("--user"), QStringLiteral("start"),
                                 QStringLiteral("podstack.socket")},
                                nullptr);
        }
        return false;
    }
    m_jobId = stageReply.value(QStringLiteral("jobId")).toString();
    m_stateDir = stageReply.value(QStringLiteral("stateDir")).toString();
    if (!writeState(QStringLiteral("staged"), QStringLiteral("Pacote validado."), error)) {
        if (servicesStopped) {
            (void) runSystemctl({QStringLiteral("--user"), QStringLiteral("start"),
                                 QStringLiteral("podstack.socket")},
                                nullptr);
        }
        return false;
    }

    QVariantMap installReply;
    if (!m_helper->call(QStringLiteral("InstallUpdate"), {m_jobId}, &installReply, error)) {
        (void) writeState(QStringLiteral("failed"), error ? error->message : QString(), nullptr);
        if (servicesStopped) {
            (void) runSystemctl({QStringLiteral("--user"), QStringLiteral("start"),
                                 QStringLiteral("podstack.socket")},
                                nullptr);
        }
        return false;
    }
    m_unit = installReply.value(QStringLiteral("unit")).toString();
    if (!writeState(QStringLiteral("installing"),
                    QStringLiteral("Instalação iniciada."), error)) {
        return false;
    }

    const bool done = pollStatus(error);
    if (done) {
        if (m_options.restartServices) {
            if (!runSystemctl({QStringLiteral("--user"), QStringLiteral("start"),
                               QStringLiteral("podstack.socket")},
                              error)) {
                return false;
            }
        }
        (void) writeState(QStringLiteral("done"), QStringLiteral("Update instalado."), nullptr);
        return true;
    }

    (void) writeState(QStringLiteral("failed"),
                      error ? error->message : QStringLiteral("Falha no update."), nullptr);
    if (m_options.restartServices) {
        (void) runSystemctl({QStringLiteral("--user"), QStringLiteral("start"),
                             QStringLiteral("podstack.socket")},
                            nullptr);
    }
    return false;
}

int UpdateRunner::exitCodeFor(const Error &error)
{
    if (error.code == QStringLiteral("PODSTACK_UPDATE_CONSENT_DENIED")
        || error.code == QStringLiteral("PODSTACK_PERMISSION")
        || error.code == QStringLiteral("PODSTACK_HELPER_REQUIRED")) {
        return 6;
    }
    if (error.code.contains(QStringLiteral("TIMEOUT"))) {
        return 8;
    }
    if (error.code == QStringLiteral("PODSTACK_CANCELLED")) {
        return 9;
    }
    return 1;
}

} // namespace PodStack
