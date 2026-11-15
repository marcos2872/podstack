// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>

namespace PodStack::DaemonBootstrap {

/// Resultado de uma tentativa de ativação do `podstack.socket`.
enum class Status {
    AlreadyRunning, ///< O socket já aceitava conexões; nada foi executado.
    Started,        ///< `systemctl --user start` rodou e o socket passou a aceitar.
    NotInstalled,   ///< Unit não encontrada (build local, Flatpak, AppImage).
    CustomSocket,   ///< PODSTACK_CONTROL_SOCKET definido: o cliente não interfere.
    Throttled,      ///< Tentativa recente (< 5 s); nada foi executado agora.
    Failed,         ///< Tentou ativar e o socket continuou indisponível.
};

struct Probe {
    bool unitInstalled = false;
    bool socketExists = false;
    bool connectable = false;
};

/// Caminhos de unit reconhecidos, na ordem de busca. Inclui
/// PODSTACK_USER_UNIT_DIR (usado pelos testes) e o diretório do usuário.
QStringList unitSearchPaths();

/// `true` quando a unit do socket está instalada em algum caminho conhecido.
bool unitInstalled();

/// Inspeciona a unit e o socket. Conectar ao socket ativo dispara a ativação
/// do `podstack.service` pelo systemd, como qualquer conexão do app.
Probe probe(const QString &socketPath = {}, int timeoutMs = 250);

/// Garante que o `podstack.socket` esteja ativo e aceitando conexões.
/// Bloqueante: use em worker threads, CLI ou no daemon. Máximo de duas
/// tentativas (com 300 ms de intervalo) e 5 s por comando do systemctl.
Status ensureRunning(const QString &socketPath = {}, QString *detail = nullptr);

/// Como ensureRunning(), mas no máximo uma tentativa síncrona a cada 5 s no
/// processo — evita vários systemctl simultâneos vindos de RpcClient/RpcStream.
Status ensureRunningThrottled(const QString &socketPath = {}, QString *detail = nullptr);

/// Dispara ensureRunning() no pool global, sem bloquear a thread chamadora.
/// Usado por StreamClient, que vive na thread da UI.
void ensureRunningAsync(const QString &socketPath = {});

/// Comando exibido ao usuário quando a ativação automática falha.
QString manualRemedy();

} // namespace PodStack::DaemonBootstrap
