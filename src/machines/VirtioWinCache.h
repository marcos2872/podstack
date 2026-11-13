// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"
#include "machines/HttpDownloader.h"

#include <QString>

#include <functional>

namespace PodStack {

/// Manifesto embutido do ISO oficial do virtio-win.
struct VirtioWinManifest {
    QString version;
    QString url;
    QString sha256;
    qint64 sizeBytes = 0;
    QString fileName;
};

using VirtioWinProgress = std::function<void(const QString &stage, qint64 received, qint64 total)>;

/// Cache local do ISO do virtio-win, verificado por SHA-256.
class VirtioWinCache {
public:
    explicit VirtioWinCache(QString directory = {}, VirtioWinManifest manifest = {});

    static VirtioWinManifest defaultManifest();

    QString directory() const { return m_directory; }
    QString cachedPath() const;

    /// Garante o ISO no cache, baixando e verificando se necessário.
    /// `progress` recebe os estágios `download` e `verify`.
    bool ensure(const VirtioWinProgress &progress, const DownloadCancelled &cancelled,
                QString *path, Error *error);

private:
    QString sidecarPath() const;
    bool hashFile(const QString &path, const VirtioWinProgress &progress,
                  const DownloadCancelled &cancelled, QByteArray *actual, Error *error);
    bool writeSidecar(Error *error);
    void cleanupOldVersions() const;

    QString m_directory;
    VirtioWinManifest m_manifest;
};

} // namespace PodStack
