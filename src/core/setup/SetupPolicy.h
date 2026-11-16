// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace PodStack::SetupPolicy {

/// Checks que impedem a entrada no app enquanto não estiverem `ok`.
///
/// Só bloqueiam o que o app precisa para funcionar como gerenciador Podman:
/// engine (`podman`), systemd de usuário e SQLite. QEMU/OVMF/virtiofsd são
/// necessários apenas para machines e ficam como aviso (o usuário pode abrir o
/// app e usar containers/imagens/K8s); KVM sem permissão permite TCG; Docker,
/// Caddy e k3s são opcionais.
inline bool isMandatoryCheck(const QString &id)
{
    return id == QStringLiteral("podman") || id == QStringLiteral("systemd-user") ||
           id == QStringLiteral("sqlite");
}

inline bool isCheckSatisfied(const QString &id, const QString &status)
{
    return !isMandatoryCheck(id) || status == QStringLiteral("ok");
}

} // namespace PodStack::SetupPolicy
