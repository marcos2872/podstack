// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/VirtioWinCache.h"

#include "core/Paths.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QSaveFile>

#include <utility>

namespace PodStack {
namespace {

constexpr qint64 kHashChunkSize = 1024LL * 1024LL;
constexpr int kLockTimeoutMs = 15 * 60 * 1000;

constexpr auto kManifestVersion = "0.1.302";
constexpr auto kManifestUrl = "https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/"
                              "archive-virtio/virtio-win-0.1.302-1/virtio-win-0.1.302.iso";
constexpr auto kManifestSha256 = "303f7ae40dad495d6ae474fdc571df58958a4dbc5c37a522d80f9a203867949d";
constexpr qint64 kManifestSizeBytes = 877373440;
constexpr auto kManifestFileName = "virtio-win-0.1.302.iso";

Error downloadError(const QString &message, const QString &detail, const QString &remediation,
                    bool retryable)
{
    return Error::make(QStringLiteral("PODSTACK_DOWNLOAD_FAILED"), message, detail, remediation,
                       retryable);
}

Error cancelledError()
{
    return Error::make(QStringLiteral("PODSTACK_CANCELLED"),
                       QStringLiteral("Download do ISO do virtio-win cancelado."));
}

Error dependencyError(const Error &cause)
{
    QString detail = cause.message;
    if (!cause.detail.isEmpty()) {
        detail += QStringLiteral(": ") + cause.detail;
    }
    Error error = Error::make(
        QStringLiteral("PODSTACK_DEPENDENCY_MISSING"),
        QStringLiteral("Não foi possível baixar o ISO do virtio-win."), detail,
        QStringLiteral("Verifique a conexão ou informe um ISO local (--virtio-win-iso na CLI ou "
                       "o campo ISO do virtio-win na GUI)."),
        true);
    error.reason = QStringLiteral("download_failed");
    return error;
}

Error checksumError(const QString &expected, const QString &actual)
{
    Error error =
        downloadError(QStringLiteral("O ISO do virtio-win não confere com o manifesto."),
                      QStringLiteral("esperado: %1; obtido: %2").arg(expected, actual),
                      QStringLiteral("Tente o download novamente ou informe um ISO local."), true);
    error.reason = QStringLiteral("checksum_mismatch");
    return error;
}

QJsonObject readSidecar(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }
    return document.object();
}

} // namespace

VirtioWinCache::VirtioWinCache(QString directory, VirtioWinManifest manifest)
    : m_directory(directory.isEmpty() ? Paths::virtioWinCacheDir() : std::move(directory)),
      m_manifest(std::move(manifest))
{
    if (m_manifest.url.isEmpty()) {
        m_manifest = defaultManifest();
    }
}

VirtioWinManifest VirtioWinCache::defaultManifest()
{
    VirtioWinManifest manifest;
    manifest.version = QString::fromLatin1(kManifestVersion);
    manifest.url = QString::fromLatin1(kManifestUrl);
    manifest.sha256 = QString::fromLatin1(kManifestSha256);
    manifest.sizeBytes = kManifestSizeBytes;
    manifest.fileName = QString::fromLatin1(kManifestFileName);
    return manifest;
}

QString VirtioWinCache::cachedPath() const
{
    const QString fileName = m_manifest.fileName.isEmpty()
                                 ? QStringLiteral("virtio-win-%1.iso").arg(m_manifest.version)
                                 : m_manifest.fileName;
    return m_directory + QLatin1Char('/') + fileName;
}

QString VirtioWinCache::sidecarPath() const
{
    return cachedPath() + QStringLiteral(".json");
}

bool VirtioWinCache::hashFile(const QString &path, const VirtioWinProgress &progress,
                              const DownloadCancelled &cancelled, QByteArray *actual, Error *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = downloadError(QStringLiteral("Não foi possível ler o ISO do virtio-win."),
                                   file.errorString(), {}, false);
        }
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    const qint64 total = file.size();
    qint64 received = 0;
    while (!file.atEnd()) {
        if (cancelled && cancelled()) {
            if (error) {
                *error = cancelledError();
            }
            return false;
        }
        const QByteArray chunk = file.read(kHashChunkSize);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            if (error) {
                *error = downloadError(QStringLiteral("Falha ao ler o ISO do virtio-win."),
                                       file.errorString(), {}, false);
            }
            return false;
        }
        hash.addData(chunk);
        received += chunk.size();
        if (progress) {
            progress(QStringLiteral("verify"), received, total);
        }
    }
    if (actual) {
        *actual = hash.result().toHex();
    }
    return true;
}

bool VirtioWinCache::writeSidecar(Error *error)
{
    QJsonObject object;
    object.insert(QStringLiteral("version"), m_manifest.version);
    object.insert(QStringLiteral("url"), m_manifest.url);
    object.insert(QStringLiteral("sha256"), m_manifest.sha256);
    object.insert(QStringLiteral("size"), static_cast<double>(m_manifest.sizeBytes));
    object.insert(QStringLiteral("date"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QSaveFile file(sidecarPath());
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = downloadError(QStringLiteral("Não foi possível gravar o sidecar do cache."),
                                   file.errorString(), {}, false);
        }
        return false;
    }
    const QByteArray data = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size() || !file.commit()) {
        if (error) {
            *error = downloadError(QStringLiteral("Não foi possível gravar o sidecar do cache."),
                                   file.errorString(), {}, false);
        }
        return false;
    }
    QFile::setPermissions(sidecarPath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

void VirtioWinCache::cleanupOldVersions() const
{
    const QDir dir(m_directory);
    const QString currentName = QFileInfo(cachedPath()).fileName();
    const QString currentSidecar = QFileInfo(sidecarPath()).fileName();
    const QStringList entries =
        dir.entryList({QStringLiteral("virtio-win-*.iso"), QStringLiteral("virtio-win-*.iso.json"),
                       QStringLiteral("virtio-win-*.iso.part")},
                      QDir::Files | QDir::NoSymLinks);
    for (const QString &entry : entries) {
        if (entry == currentName || entry == currentSidecar) {
            continue;
        }
        QFile::remove(dir.filePath(entry));
    }
}

bool VirtioWinCache::ensure(const VirtioWinProgress &progress, const DownloadCancelled &cancelled,
                            QString *path, Error *error)
{
    if (error) {
        *error = {};
    }
    if (path) {
        path->clear();
    }

    if (!QDir().mkpath(m_directory)) {
        if (error) {
            *error = downloadError(QStringLiteral("Não foi possível criar o cache do virtio-win."),
                                   m_directory, {}, false);
        }
        return false;
    }
    QFile::setPermissions(m_directory,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    const QString target = cachedPath();
    QLockFile lock(target + QStringLiteral(".lock"));
    if (!lock.tryLock(kLockTimeoutMs)) {
        Error busy = downloadError(
            QStringLiteral("Outro download do ISO do virtio-win está em andamento."), target,
            QStringLiteral("Aguarde o download em andamento e tente novamente."), true);
        busy.reason = QStringLiteral("download_in_progress");
        if (error) {
            *error = busy;
        }
        return false;
    }

    const QFileInfo info(target);
    if (info.exists() && info.isFile()) {
        const QJsonObject sidecar = readSidecar(sidecarPath());
        const bool sidecarMatches =
            !sidecar.isEmpty() && info.size() == m_manifest.sizeBytes &&
            sidecar.value(QStringLiteral("size")).toVariant().toLongLong() == info.size() &&
            sidecar.value(QStringLiteral("version")).toString() == m_manifest.version &&
            sidecar.value(QStringLiteral("sha256"))
                    .toString()
                    .compare(m_manifest.sha256, Qt::CaseInsensitive) == 0;
        if (sidecarMatches) {
            if (progress) {
                progress(QStringLiteral("download"), info.size(), info.size());
            }
            cleanupOldVersions();
            if (path) {
                *path = target;
            }
            return true;
        }

        bool rehash = info.size() == m_manifest.sizeBytes;
        if (rehash) {
            Error hashError;
            QByteArray actual;
            if (!hashFile(target, progress, cancelled, &actual, &hashError)) {
                if (error) {
                    *error = hashError;
                }
                return false;
            }
            if (actual.compare(m_manifest.sha256.toLatin1(), Qt::CaseInsensitive) == 0) {
                if (!writeSidecar(error)) {
                    return false;
                }
                cleanupOldVersions();
                if (path) {
                    *path = target;
                }
                return true;
            }
        }

        QFile::remove(target);
        QFile::remove(sidecarPath());
    }

    if (cancelled && cancelled()) {
        if (error) {
            *error = cancelledError();
        }
        return false;
    }

    if (progress) {
        progress(QStringLiteral("download"), 0, m_manifest.sizeBytes);
    }
    Error downloadFailure;
    if (!HttpDownloader::download(
            m_manifest.url, target,
            [&progress](qint64 received, qint64 total) {
                if (progress) {
                    progress(QStringLiteral("download"), received, total);
                }
            },
            cancelled, &downloadFailure)) {
        if (downloadFailure.code == QStringLiteral("PODSTACK_CANCELLED")) {
            if (error) {
                *error = downloadFailure;
            }
            return false;
        }
        if (error) {
            *error = dependencyError(downloadFailure);
        }
        return false;
    }

    const QFileInfo downloaded(target);
    if (!downloaded.exists() || downloaded.size() <= 0 ||
        (m_manifest.sizeBytes > 0 && downloaded.size() != m_manifest.sizeBytes)) {
        QFile::remove(target);
        QFile::remove(sidecarPath());
        if (error) {
            *error = checksumError(QString::number(m_manifest.sizeBytes),
                                   QString::number(downloaded.size()));
        }
        return false;
    }

    Error hashError;
    QByteArray actual;
    if (!hashFile(target, progress, cancelled, &actual, &hashError)) {
        QFile::remove(target);
        QFile::remove(sidecarPath());
        if (error) {
            *error = hashError;
        }
        return false;
    }
    if (actual.compare(m_manifest.sha256.toLatin1(), Qt::CaseInsensitive) != 0) {
        QFile::remove(target);
        QFile::remove(sidecarPath());
        if (error) {
            *error = checksumError(m_manifest.sha256, QString::fromLatin1(actual));
        }
        return false;
    }

    if (!writeSidecar(error)) {
        QFile::remove(target);
        QFile::remove(sidecarPath());
        return false;
    }

    cleanupOldVersions();
    if (path) {
        *path = target;
    }
    return true;
}

} // namespace PodStack
