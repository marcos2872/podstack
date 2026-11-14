// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

namespace PodStack {

/// Inicia processos de machines em um scope systemd de usuário.
///
/// O `podstack.service` roda no cgroup do daemon; sem um scope próprio, parar
/// ou reiniciar o serviço mataria QEMU/virtiofsd. `systemd-run --user --scope`
/// coloca o processo em um scope independente, fora do cgroup do daemon.
/// Quando o systemd de usuário não está disponível, o processo é iniciado
/// diretamente (comportamento anterior).
class ProcessScope {
public:
    ProcessScope() = delete;

    /// `true` quando `systemd-run --user --scope` pode ser usado.
    /// `PODSTACK_DISABLE_SCOPE=1` desliga; `PODSTACK_SYSTEMD_RUN_BINARY`
    /// permite usar um runner alternativo (testes).
    static bool available();

    /// argv do `systemd-run` para executar `program` com `arguments`.
    static QStringList arguments(const QString &unit, const QString &program,
                                 const QStringList &programArguments);

    /// Nome de unit válido, com sufixo `.scope`.
    static QString unitName(const QString &prefix, const QString &id);

    /// Inicia destacado. Usa o scope quando disponível e cai para a execução
    /// direta se o runner não puder ser iniciado.
    static bool startDetached(const QString &unit, const QString &program,
                              const QStringList &programArguments, qint64 *pid,
                              QString *failure = nullptr, bool *scoped = nullptr);

private:
    static QString systemdRunBinary();
};

} // namespace PodStack
