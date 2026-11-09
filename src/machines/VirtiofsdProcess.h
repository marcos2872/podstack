// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"

#include <QString>

namespace PodStack {

/// Processo `virtiofsd` de um mount (um shared dir por tag).
///
/// O processo é destacado (como o QEMU) para que sobreviva ao reinício do
/// daemon e não fique preso à thread de um job. A propriedade é confirmada
/// pelo `cmdline` (`/proc/<pid>/cmdline` contém o socket deste mount), então
/// stop nunca sinaliza um PID reciclado ou processo de outra instalação.
class VirtiofsdProcess {
public:
    VirtiofsdProcess() = default;
    ~VirtiofsdProcess() = default;

    VirtiofsdProcess(const VirtiofsdProcess &) = delete;
    VirtiofsdProcess &operator=(const VirtiofsdProcess &) = delete;

    /// Binário explícito, `PODSTACK_VIRTIOFSD_BINARY`, PATH ou /usr/libexec/virtiofsd.
    static QString resolveBinary(const QString &requested, Error *error);

    /// Inicia o virtiofsd e espera o socket aparecer (timeout de 5 s).
    bool start(const QString &binary, const QString &source, const QString &socketPath,
               const QString &tag, bool readOnly, Error *error);
    /// Encerra com SIGTERM, espera `timeoutMs` e usa SIGKILL se necessário.
    bool stop(Error *error = nullptr, int timeoutMs = 5000);
    bool isRunning() const;
    qint64 pid() const;

private:
    qint64 m_pid = 0;
    QString m_socketPath;
};

} // namespace PodStack
