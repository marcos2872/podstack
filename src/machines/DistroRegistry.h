// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"

#include <QCryptographicHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace PodStack {

/// Imagem cloud pinada no binário (sem manifesto remoto em runtime).
struct DistroImage {
    QString distro;
    QString version;
    QString arch;
    QString url;
    QString checksum;
    QCryptographicHash::Algorithm algorithm = QCryptographicHash::Sha256;
    qint64 size = 0;
    bool isDefault = false;
};

class DistroRegistry {
public:
    /// Todas as imagens pinadas, na ordem do registry.
    static QVector<DistroImage> images();

    /// Distros disponíveis, sem duplicatas, na ordem do registry.
    static QStringList distros();

    /// Resolve `distro[:versão]` para a imagem pinada.
    /// `arch` vazio usa a arquitetura do host; só há imagens amd64.
    static DistroImage resolve(const QString &spec, const QString &arch, Error *error);

    /// Nome estável do algoritmo ("sha256"/"sha512").
    static QString algorithmName(QCryptographicHash::Algorithm algorithm);

    /// Arquitetura do host normalizada (amd64/arm64/...).
    static QString hostArch();
};

} // namespace PodStack
