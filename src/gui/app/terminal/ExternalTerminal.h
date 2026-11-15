// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>

namespace PodStack {

/// Lança uma sessão do PodStack em um emulador de terminal externo.
///
/// A descoberta segue a spec freedesktop (`xdg-terminal-exec`), depois a
/// preferência do usuário (`$TERMINAL`) e, por fim, os emuladores comuns.
/// Nenhum comando passa por shell: o programa e os argumentos são entregues
/// diretamente ao `QProcess`.
class ExternalTerminal {
public:
    /// Inicia `program` com `args` no primeiro terminal disponível.
    /// Retorna false e preenche `error` com uma mensagem legível em caso de falha.
    static bool launch(const QString &program, const QStringList &args,
                       QString *error = nullptr);

    /// Argumentos que `terminal` precisa receber para executar `program` com `args`.
    static QStringList commandFor(const QString &terminal, const QString &program,
                                  const QStringList &args);

    /// Caminho do terminal externo preferido, ou vazio se nenhum for encontrado.
    static QString findTerminal();
};

} // namespace PodStack
