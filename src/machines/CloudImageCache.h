// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"
#include "machines/DistroRegistry.h"
#include "machines/HttpDownloader.h"

#include <QString>

namespace PodStack {

/// Cache local de imagens cloud, endereçado pelo checksum da imagem.
class CloudImageCache {
public:
    explicit CloudImageCache(QString directory = {});

    QString directory() const { return m_directory; }

    QString cachedPath(const DistroImage &image) const;
    QString sidecarPath(const DistroImage &image) const;

    /// Verificação barata (arquivo + sidecar), sem recalcular o hash.
    bool isCached(const DistroImage &image) const;
    qint64 sizeBytes(const DistroImage &image) const;

    /// Garante a imagem no cache, baixando e verificando se necessário.
    bool ensure(const DistroImage &image, const DownloadProgress &progress,
                const DownloadCancelled &cancelled, QString *path, Error *error);

private:
    QString m_directory;
};

} // namespace PodStack
