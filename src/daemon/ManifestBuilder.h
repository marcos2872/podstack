// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Error.h"

#include <QMap>
#include <QString>
#include <QStringList>

#include <functional>

namespace PodStack {

/// Constrói manifest lists multi-plataforma pelo CLI do Podman.
///
/// O Libpod REST usado no fluxo de uma plataforma não expõe a criação de
/// manifest lists; por isso este caminho executa `podman build --manifest`
/// sem shell (QProcess) e transmite stdout/stderr linha a linha.
class ManifestBuilder {
public:
    using Line = std::function<void(const QString &)>;
    using Cancelled = std::function<bool()>;

    bool build(const QString &contextPath, const QString &dockerfile, const QString &tag,
               const QMap<QString, QString> &buildArgs, const QStringList &platforms,
               const Line &onLine, const Cancelled &cancelled, Error *error);

    /// Tempo total máximo do build (default: 30 min).
    void setTimeoutMs(int timeoutMs);

    /// Valida `os/arch[/variant]`, quantidade (1..8) e caracteres seguros.
    static bool validatePlatforms(const QStringList &platforms, Error *error);

private:
    int m_timeoutMs = 30 * 60 * 1000;
};

} // namespace PodStack
