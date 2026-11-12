// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"

#include <QString>

namespace PodStack {

/// Processo `swtpm` (TPM 2.0 emulado) de uma machine Windows.
///
/// O processo é destacado (como o QEMU e o virtiofsd) para sobreviver ao
/// reinício do daemon. A propriedade é confirmada pelo `cmdline`
/// (`/proc/<pid>/cmdline` contém o socket de controle desta machine), então
/// stop nunca sinaliza um PID reciclado ou processo de outra instalação.
class SwtpmProcess {
public:
    SwtpmProcess() = default;
    ~SwtpmProcess() = default;

    SwtpmProcess(const SwtpmProcess &) = delete;
    SwtpmProcess &operator=(const SwtpmProcess &) = delete;

    /// Binário explícito, `PODSTACK_SWTPM_BINARY` ou PATH.
    static QString resolveBinary(const QString &requested, Error *error);

    /// Inicia o swtpm e espera o socket de controle aparecer (timeout de 5 s).
    bool start(const QString &binary, const QString &stateDir, const QString &socketPath,
               Error *error);
    /// Adota um swtpm já em execução (após restart do daemon) pelo socket.
    bool adopt(const QString &socketPath);
    /// Encerra com SIGTERM, espera `timeoutMs` e usa SIGKILL se necessário.
    bool stop(Error *error = nullptr, int timeoutMs = 5000);
    bool isRunning() const;
    qint64 pid() const;

    static QString socketPathFor(const QString &machineDir);
    static QString stateDirFor(const QString &machineDir);

private:
    qint64 m_pid = 0;
    QString m_socketPath;
};

} // namespace PodStack
