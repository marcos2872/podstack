// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <functional>

namespace PodStack {

struct MachineArchiveArtifact {
    QString path;
    qint64 size = 0;
    QString sha256;
};

/// Manifesto do arquivo `.tar.zst` de uma machine (`podstack.machine/1`).
struct MachineArchiveManifest {
    QString format;
    QString createdAt;
    QString name;
    QString distro;
    QString distroVersion;
    QString arch;
    int cpus = 2;
    qint64 memoryBytes = 0;
    qint64 diskBytes = 0;
    QString sshUser;
    bool isolateNetwork = false;
    bool forwardSshAgent = false;
    QVector<MachineArchiveArtifact> artifacts;

    static QString formatId();

    QJsonObject toJson() const;
    static MachineArchiveManifest fromJson(const QJsonObject &object, Error *error);
};

/// Empacota e extrai o `.tar.zst` de uma machine com manifesto e checksums
/// SHA-256 por artefato. O arquivo contém o disco (qcow2 autocontido), o seed
/// cloud-init e o par de chaves SSH da machine.
class MachineArchive {
public:
    using Progress = std::function<void(const QString &stage, int progress)>;
    using Cancelled = std::function<bool()>;

    struct Paths {
        QString directory;
        QString diskPath;
        QString seedPath;
        QString sshKeyPath;
        QString sshPublicKeyPath;
    };

    /// Grava o arquivo em `archivePath`. Em falha/cancelamento o chamador deve
    /// remover o arquivo parcial (o writer não cria diretórios).
    static bool write(const QString &archivePath, const MachineArchiveManifest &manifest,
                      const QString &diskPath, const QString &seedPath,
                      const QString &sshKeyPath, const QString &sshPublicKeyPath,
                      const Progress &progress, const Cancelled &cancelled, Error *error);

    /// Extrai para `directory` (que deve existir e estar vazia), validando
    /// nomes de entrada, formato do manifesto, tamanhos e checksums.
    static bool extract(const QString &archivePath, const QString &directory,
                        MachineArchiveManifest *manifest, Paths *paths,
                        const Progress &progress, const Cancelled &cancelled, Error *error);
};

} // namespace PodStack
