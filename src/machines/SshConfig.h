// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"
#include "machines/MachineMeta.h"

#include <QString>

namespace PodStack {

/// Gerencia o bloco `podstack` em `~/.ssh/config` de uma machine.
///
/// O caminho padrão é `$PODSTACK_SSH_CONFIG` (override de dev/teste) ou
/// `~/.ssh/config`. O bloco é delimitado por marcadores estáveis e substituído
/// de forma idempotente; o restante do arquivo é preservado.
class SshConfig {
public:
    explicit SshConfig(const QString &path = {});

    QString path() const { return m_path; }

    /// Insere/atualiza o bloco da machine. O arquivo final fica 0600 e o
    /// diretório pai 0700.
    bool sync(const MachineMeta &machine, bool forwardAgent, Error *error);

    /// Remove o bloco da machine (arquivo ausente é sucesso).
    bool remove(const QString &name, Error *error);

private:
    QString m_path;
};

} // namespace PodStack
