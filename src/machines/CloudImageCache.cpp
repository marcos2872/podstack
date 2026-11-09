// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/CloudImageCache.h"

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

constexpr qint64 kHashChunkSize = 1024 * 1024;
constexpr int kLockTimeoutMs = 30000;

QString safeComponent(const QString &value)
{
    QString result;
    result.reserve(value.size());
    for (const QChar character : value) {
        if (character.isLetterOrNumber() || character == QLatin1Char('.')
            || character == QLatin1Char('-') || character == QLatin1Char('_')) {
            result.append(character);
        } else {
            result.append(QLatin1Char('_'));
        }
    }
    return result;
}

QByteArray hashFile(const QString &path, QCryptographicHash::Algorithm algorithm,
                    const DownloadProgress &progress, const DownloadCancelled &cancelled,
                    Error *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_DOWNLOAD_FAILED"),
                                 QStringLiteral("Não foi possível ler a imagem em cache."),
                                 file.errorString());
        }
        return {};
    }

    QCryptographicHash hash(algorithm);
    const qint64 total = file.size();
    qint64 received = 0;
    while (!file.atEnd()) {
        if (cancelled && cancelled()) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_CANCELLED"),
                                     QStringLiteral("Verificação do cache cancelada."));
            }
            return {};
        }
        const QByteArray chunk = file.read(kHashChunkSize);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_DOWNLOAD_FAILED"),
                                     QStringLiteral("Falha ao ler a imagem em cache."),
                                     file.errorString());
            }
            return {};
        }
        hash.addData(chunk);
        received += chunk.size();
        if (progress) {
            progress(received, total);
        }
    }
    return hash.result().toHex();
}

bool writeSidecar(const QString &path, const DistroImage &image, qint64 size, Error *error)
{
    QJsonObject object;
    object.insert(QStringLiteral("url"), image.url);
    object.insert(QStringLiteral("checksum"), image.checksum);
    object.insert(QStringLiteral("algorithm"), DistroRegistry::algorithmName(image.algorithm));
    object.insert(QStringLiteral("size"), static_cast<double>(size));
    object.insert(QStringLiteral("date"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_DOWNLOAD_FAILED"),
                                 QStringLiteral("Não foi possível gravar o sidecar do cache."),
                                 file.errorString());
        }
        return false;
    }
    const QByteArray data = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size() || !file.commit()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_DOWNLOAD_FAILED"),
                                 QStringLiteral("Não foi possível gravar o sidecar do cache."),
                                 file.errorString());
        }
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
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

bool sidecarMatches(const QJsonObject &sidecar, const DistroImage &image, qint64 fileSize)
{
    if (sidecar.isEmpty() || fileSize <= 0) {
        return false;
    }
    if (image.size > 0 && fileSize != image.size) {
        return false;
    }
    if (sidecar.value(QStringLiteral("checksum"))
            .toString()
            .compare(image.checksum, Qt::CaseInsensitive)
        != 0) {
        return false;
    }
    if (sidecar.value(QStringLiteral("algorithm")).toString()
        != DistroRegistry::algorithmName(image.algorithm)) {
        return false;
    }
    return sidecar.value(QStringLiteral("size")).toVariant().toLongLong() == fileSize;
}

} // namespace

CloudImageCache::CloudImageCache(QString directory)
    : m_directory(directory.isEmpty() ? Paths::cloudImagesDir() : std::move(directory))
{
}

QString CloudImageCache::cachedPath(const DistroImage &image) const
{
    const QString fileName =
        QStringLiteral("%1-%2-%3-%4.qcow2")
            .arg(safeComponent(image.distro), safeComponent(image.version),
                 safeComponent(image.arch), image.checksum.left(12).toLower());
    return m_directory + QLatin1Char('/') + fileName;
}

QString CloudImageCache::sidecarPath(const DistroImage &image) const
{
    return cachedPath(image) + QStringLiteral(".json");
}

bool CloudImageCache::isCached(const DistroImage &image) const
{
    const QString path = cachedPath(image);
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return false;
    }
    return sidecarMatches(readSidecar(sidecarPath(image)), image, info.size());
}

qint64 CloudImageCache::sizeBytes(const DistroImage &image) const
{
    const QFileInfo info(cachedPath(image));
    return info.exists() && info.isFile() ? info.size() : 0;
}

bool CloudImageCache::ensure(const DistroImage &image, const DownloadProgress &progress,
                             const DownloadCancelled &cancelled, QString *path, Error *error)
{
    if (error) {
        *error = {};
    }
    if (path) {
        path->clear();
    }

    if (!QDir().mkpath(m_directory)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_DOWNLOAD_FAILED"),
                                 QStringLiteral("Não foi possível criar o cache de imagens."),
                                 m_directory);
        }
        return false;
    }
    QFile::setPermissions(m_directory,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner
                              | QFileDevice::ExeOwner);

    const QString target = cachedPath(image);
    QLockFile lock(target + QStringLiteral(".lock"));
    if (!lock.tryLock(kLockTimeoutMs)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_DOWNLOAD_FAILED"),
                                 QStringLiteral("Outro download da mesma imagem está em "
                                                "andamento."),
                                 target);
        }
        return false;
    }

    const QFileInfo info(target);
    if (info.exists() && info.isFile()) {
        const QJsonObject sidecar = readSidecar(sidecarPath(image));
        if (sidecarMatches(sidecar, image, info.size())) {
            if (path) {
                *path = target;
            }
            return true;
        }

        if (sidecar.isEmpty()) {
            Error hashError;
            const QByteArray actual =
                hashFile(target, image.algorithm, progress, cancelled, &hashError);
            if (actual.isEmpty()) {
                if (error) {
                    *error = hashError;
                }
                return false;
            }
            if (actual.compare(image.checksum.toLatin1(), Qt::CaseInsensitive) == 0) {
                if (!writeSidecar(sidecarPath(image), image, info.size(), error)) {
                    return false;
                }
                if (path) {
                    *path = target;
                }
                return true;
            }
        }

        QFile::remove(target);
        QFile::remove(sidecarPath(image));
    }

    if (cancelled && cancelled()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_CANCELLED"),
                                 QStringLiteral("Download cancelado."));
        }
        return false;
    }

    if (!HttpDownloader::download(image.url, target, progress, cancelled, error)) {
        return false;
    }

    const QFileInfo downloaded(target);
    if (!downloaded.exists() || downloaded.size() <= 0
        || (image.size > 0 && downloaded.size() != image.size)) {
        QFile::remove(target);
        QFile::remove(sidecarPath(image));
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_DOWNLOAD_FAILED"),
                                 QStringLiteral("A imagem baixada tem tamanho inesperado."),
                                 QStringLiteral("esperado: %1; obtido: %2")
                                     .arg(image.size)
                                     .arg(downloaded.size()));
        }
        return false;
    }

    Error hashError;
    const QByteArray actual = hashFile(target, image.algorithm, {}, cancelled, &hashError);
    if (actual.isEmpty()) {
        QFile::remove(target);
        QFile::remove(sidecarPath(image));
        if (error) {
            *error = hashError;
        }
        return false;
    }

    if (actual.compare(image.checksum.toLatin1(), Qt::CaseInsensitive) != 0) {
        QFile::remove(target);
        QFile::remove(sidecarPath(image));
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_DOWNLOAD_FAILED"),
                                 QStringLiteral("Checksum da imagem não confere."),
                                 QStringLiteral("esperado: %1; obtido: %2")
                                     .arg(image.checksum, QString::fromLatin1(actual)));
        }
        return false;
    }

    if (!writeSidecar(sidecarPath(image), image, downloaded.size(), error)) {
        QFile::remove(target);
        QFile::remove(sidecarPath(image));
        return false;
    }

    if (path) {
        *path = target;
    }
    return true;
}

} // namespace PodStack
