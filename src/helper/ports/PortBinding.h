// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

namespace PodStack {

/// Socket activation de :80/:443 para o proxy do daemon.
///
/// O helper grava unidades systemd de sistema (`podstack-http.socket`,
/// `podstack-https.socket`) e serviços `systemd-socket-proxyd` apontando para
/// `127.0.0.1:<porta do proxy>`. Portas e alvos são allowlisted: o helper
/// privilegiado nunca aceita bind em endereço não-loopback.
namespace PortBinding {

inline constexpr auto kHttpPort = "80";
inline constexpr auto kHttpsPort = "443";
inline constexpr auto kProxyBinary = "/usr/lib/systemd/systemd-socket-proxyd";

/// Valida a lista de portas (apenas 80 e 443).
bool validatePorts(const QStringList &ports, QString *error = nullptr);

/// Valida um alvo `127.0.0.1:<1..65535>`.
bool validateTarget(const QString &target, QString *error = nullptr);

QString socketUnitName(const QString &port);
QString serviceUnitName(const QString &port);

QString socketUnitText(const QString &port);
QString serviceUnitText(const QString &port, const QString &target);

/// Escreve as unidades das portas pedidas em `directory`. Idempotente e com
/// backup `.bak` quando o conteúdo existente difere.
bool install(const QString &directory, const QStringList &ports, const QString &httpTarget,
             const QString &httpsTarget, QString *error = nullptr);

/// Remove as unidades das portas pedidas. Idempotente.
bool remove(const QString &directory, const QStringList &ports, QString *error = nullptr);

} // namespace PortBinding

} // namespace PodStack
