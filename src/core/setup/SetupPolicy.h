// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace PodStack::SetupPolicy {

/// Checks que impedem a entrada no app enquanto não estiverem `ok`.
///
/// KVM sem permissão (/dev/kvm indisponível) é aviso e permite TCG; Docker,
/// Caddy e k3s são opcionais.
inline bool isMandatoryCheck(const QString &id)
{
    return id == QStringLiteral("podman") || id == QStringLiteral("systemd-user")
        || id == QStringLiteral("sqlite") || id == QStringLiteral("qemu")
        || id == QStringLiteral("ovmf") || id == QStringLiteral("virtiofsd");
}

inline bool isCheckSatisfied(const QString &id, const QString &status)
{
    return !isMandatoryCheck(id) || status == QStringLiteral("ok");
}

} // namespace PodStack::SetupPolicy
