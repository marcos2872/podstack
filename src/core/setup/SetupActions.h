// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace PodStack::SetupActions {

/// Ação de setup adequada ao status do check "podman".
///
/// - `missing`: Podman não instalado → instalar (exige o helper privilegiado).
/// - `warn`: Podman instalado, mas `podman.socket` inativo → ativar o socket
///   (operação de usuário, sem helper).
/// - `ok`/vazio: nada a fazer.
inline QString podmanActionForStatus(const QString &status)
{
    if (status == QStringLiteral("missing")) {
        return QStringLiteral("install-podman");
    }
    if (status == QStringLiteral("warn")) {
        return QStringLiteral("enable-socket");
    }
    return {};
}

} // namespace PodStack::SetupActions
