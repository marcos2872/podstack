// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"
#include "machines/MachineManager.h"

#include <QJsonArray>
#include <QString>

namespace PodStack {

/// Lista, baixa e envia arquivos de uma machine via SSH/SCP.
///
/// Funciona com qualquer distro Linux que tenha `ls` e o servidor SSH do
/// cloud-init; não depende de mounts virtiofs. Os binários `ssh`/`scp` são
/// sobrescrevíveis por `PODSTACK_SSH_BINARY`/`PODSTACK_SCP_BINARY` em dev/teste.
class MachineFiles {
public:
    /// Lista `path` (arquivo ou diretório) e devolve itens
    /// `{name, path, directory, size, modified}`. `.` e `..` são omitidos.
    static bool list(const SshInfo &ssh, const QString &path, QJsonArray *items,
                     Error *error = nullptr);

    /// Baixa `remotePath` para `destination` com `scp -r`.
    static bool download(const SshInfo &ssh, const QString &remotePath,
                         const QString &destination, Error *error = nullptr);

    /// Envia `localPath` para `remotePath` com `scp -r`.
    static bool upload(const SshInfo &ssh, const QString &localPath,
                       const QString &remotePath, Error *error = nullptr);
};

} // namespace PodStack
