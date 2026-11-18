// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QVariantList>

namespace PodStack {

/// Define, para a thread atual, o PID do processo interativo que originou a
/// chamada ao helper. O daemon roda fora da sessão gráfica e o polkit não
/// encontra agente de autenticação a partir dele; repassar o PID do cliente RPC
/// faz o prompt abrir na sessão certa.
///
/// Thread-local de propósito: os handlers RPC rodam em threads de trabalho e o
/// `HelperClient` é compartilhado, então um membro comum seria uma corrida.
void setHelperCallerPid(uint pid);
uint helperCallerPid();

/// Restaura o PID anterior ao sair do escopo (o `Daemon::handle()` tem muitos
/// `return` antecipados).
class HelperCallerScope {
public:
    explicit HelperCallerScope(uint pid) : m_previous(helperCallerPid())
    {
        setHelperCallerPid(pid);
    }
    ~HelperCallerScope() { setHelperCallerPid(m_previous); }

    HelperCallerScope(const HelperCallerScope &) = delete;
    HelperCallerScope &operator=(const HelperCallerScope &) = delete;

private:
    uint m_previous;
};

/// Cliente mínimo do helper privilegiado no barramento de sistema.
///
/// Concentra a descoberta do serviço e a chamada D-Bus usadas pelo daemon e
/// pelo setup, sem expor a interface para a GUI.
class HelperClient {
public:
    bool available() const;

    /// PID do processo interativo (a GUI) que pediu a ação, quando quem chama o
    /// helper é o daemon. Zero significa "use o remetente D-Bus" — correto para
    /// chamadas feitas de dentro da sessão. O helper revalida que o PID
    /// pertence ao mesmo UID do remetente.
    void setCallerPid(uint pid) { m_callerPid = pid; }
    uint callerPid() const { return m_callerPid != 0 ? m_callerPid : helperCallerPid(); }

    /// Chama um método do helper e devolve `true` apenas quando `{ok:true}`.
    ///
    /// `error` recebe a mensagem devolvida pelo helper ou pelo barramento.
    bool call(const QString &method, const QVariantList &arguments, QString *error = nullptr,
              int timeoutMs = 120000) const;

    /// Igual a `call`, mas devolve o mapa completo (jobId, estado, etc.).
    bool callMap(const QString &method, const QVariantList &arguments, QVariantMap *reply,
                 QString *error = nullptr, int timeoutMs = 120000) const;

private:
    uint m_callerPid = 0;
};

} // namespace PodStack
