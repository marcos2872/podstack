// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

namespace PodStack {

/// Drop-in do `systemd-resolved` que encaminha apenas `podstack.local` para o
/// DNS userspace do daemon. A zona e o upstream são fixos (allowlist): o helper
/// privilegiado nunca aceita valores arbitrários.
namespace DnsZone {

inline constexpr auto kZone = "podstack.local";
inline constexpr auto kUpstream = "127.0.0.1:5353";

/// Valida a zona e o upstream contra a allowlist.
bool validate(const QString &zone, const QString &upstream, QString *error = nullptr);

/// Conteúdo do drop-in, exposto para teste.
QString dropInText(const QString &zone, const QString &upstream);

/// Instala (ou atualiza) `<directory>/podstack.conf`, preservando o conteúdo
/// anterior em `podstack.conf.bak` quando diferente. Idempotente.
bool install(const QString &directory, const QString &zone, const QString &upstream,
             QString *error = nullptr);

/// Remove `<directory>/podstack.conf`. Idempotente.
bool remove(const QString &directory, const QString &zone, QString *error = nullptr);

} // namespace DnsZone

} // namespace PodStack
