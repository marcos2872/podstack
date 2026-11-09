// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"

#include <QString>
#include <QStringList>

namespace PodStack {

/// Dados do seed cloud-init NoCloud entregue à máquina como ISO.
struct CloudInitOptions {
    QString machineId;  // instance-id
    QString hostname;   // local-hostname (RFC1123)
    QString user = QStringLiteral("podstack");
    QString password;   // opcional, texto claro; hash SHA-512 crypt no seed
    QStringList sshAuthorizedKeys;
    QString userData;      // se não vazio, usar verbatim
    QString networkConfig; // se vazio, gerar DHCP v2
};

class CloudInit {
public:
    /// Grava uma ISO9660 com label `cidata` contendo `meta-data`, `user-data` e
    /// `network-config` na raiz. O arquivo final fica 0600 e é publicado por rename
    /// atômico de `<path>.tmp`.
    static bool writeSeedIso(const QString &path, const CloudInitOptions &options, Error *error);
};

} // namespace PodStack
