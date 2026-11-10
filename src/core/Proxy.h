// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"

#include <QMap>
#include <QString>
#include <QStringList>

namespace PodStack::Proxy {

/// Valida `proxy` (`none`, `auto` ou URL http/https/socks5/socks5h) e cada
/// exclusão (host, domínio, CIDR ou `*.domínio`).
bool validate(const QString &proxy, const QStringList &exclude, Error *error);

/// Variáveis de ambiente a aplicar em novos containers/máquinas.
/// - `none`: vazio.
/// - `auto`: copia do ambiente do processo as chaves de proxy já definidas.
/// - URL: define HTTP_PROXY/HTTPS_PROXY/ALL_PROXY (e minúsculas) e mescla
///   `NO_PROXY` com os defaults, as exclusões e o valor já existente.
QMap<QString, QString> environment(const QString &proxy, const QStringList &exclude);

/// Exclusões sempre aplicadas quando um proxy em URL está configurado.
QStringList defaultExcludes();

} // namespace PodStack::Proxy
