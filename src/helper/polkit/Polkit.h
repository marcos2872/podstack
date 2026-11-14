// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusMessage>
#include <QString>

namespace PodStack {

/// Monta a chamada `CheckAuthorization` com a assinatura exigida pelo polkit:
/// `((sa{sv})sa{ss}us)`. Exposto para teste de contrato.
QDBusMessage checkAuthorizationRequest(const QString &actionId, uint pid);

/// Autoriza uma ação polkit para o processo `pid` que chamou o helper.
/// Retorna false e preenche `error` quando o polkit recusa ou está indisponível.
bool checkAuthorization(const QString &actionId, uint pid, QString *error);

/// Lê o start-time (campo 22) de /proc/<pid>/stat; usado no subject do polkit.
qulonglong processStartTime(uint pid);

} // namespace PodStack
