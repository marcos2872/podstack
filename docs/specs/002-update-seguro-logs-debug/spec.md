# 002 — Atualização segura e logs de debug (7 dias)

> **Status:** proposta — não promovida ao núcleo SDD.
> **Marcos:** TX-12 (atualização segura) e TX-13 (logs de debug) — transversais.
> **Data:** 2026-11-12.
> **Promove para:** [`../../sdd/PRD.md`](../../sdd/PRD.md) (FR-UPD-002…, FR-SYS-010…),
> [`../../sdd/spec.md`](../../sdd/spec.md) §3.3/§7/§11, [`../../sdd/tasks.md`](../../sdd/tasks.md)
> (TX-12/TX-13), [`../../engineering/packaging.md`](../../engineering/packaging.md),
> [`../../engineering/settings.md`](../../engineering/settings.md),
> [`../../engineering/architecture.md`](../../engineering/architecture.md),
> [`../../engineering/security.md`](../../engineering/security.md),
> [`../../engineering/cli-parity.md`](../../engineering/cli-parity.md),
> [`../../engineering/ui-map.md`](../../engineering/ui-map.md).

---

## 1. Contexto

Em 2026-11-12, a atualização **0.1.2 → 0.1.3** foi instalada com a GUI, o `podstackd` e o
`podstack-helper` antigos ainda em execução. O `.deb` substituiu os binários, mas o `postinst`
apenas fez `systemctl enable --now podstack-helper.service`; um serviço já ativo **não é
reiniciado**. O resultado foi:

- a GUI continuou sendo o processo 0.1.2;
- o `podstackd` continuou servindo o setup com o código 0.1.2, cujo check de OVMF só conhecia os
  caminhos do Fedora;
- o `podstack-helper` antigo manteve a allowlist sem `ovmf`/`qemu-system-x86`;
- o usuário viu o mesmo erro de OVMF mesmo depois de instalar o pacote novo e o `ovmf` manualmente;
- só um restart manual de `podstack.service`, `podstack-helper.service` e da GUI resolveu.

Hoje o update é só **verificação**: `updates.check` (T7-06) baixa o manifesto assinado e notifica;
não há instalação orquestrada. O `system.report` (T7-08) lê
`~/.local/state/podstack/logs/podstackd.log`, mas o `Logging::init` só configura o padrão de
mensagem; o daemon depende do redirecionamento do systemd, não há rotação/retenção no app, e o log
do job de setup existe apenas em memória/GUI. O helper grava no journal do sistema. O
`FR-SYS-009` pede logs estruturados com rotação e redaction em `~/.local/state/podstack/logs/`; a
implementação está incompleta.

## 2. Objetivos

| # | Objetivo | Métrica |
|---|---|---|
| O1 | Nenhum caminho de update deixa processo antigo ativo | após o update, `pgrep -x podstack-gui/podstackd` não encontra os PIDs anteriores; helper e socket são os novos |
| O2 | Atualização assistida pela GUI/CLI com consentimento, assinatura e reinício automático | 0.1.4→0.1.5: a GUI fecha, instala e reabre com a nova versão; falha restaura o app antigo |
| O3 | O caminho manual `apt`/`dnf` também é seguro | instalar o pacote novo com o app aberto encerra GUI/daemon/helper antes do unpack e religa o helper depois |
| O4 | Logs de debug de tudo, com retenção de 7 dias | daemon/GUI/CLI/setup/update/helper/gerenciador em arquivos 0600; 7 dias; redaction; `podstack logs`/GUI/report |

## 3. Não-objetivos

- Auto-update sem consentimento; downgrade automático; rollback automático de pacote.
- Preservar o estado da GUI durante o update; o app pode ser encerrado (o `UiState` é salvo antes).
- Parar containers, Podman, machines ou o helper além do necessário para o update.
- Substituir o gerenciador de pacotes; `.deb`/RPM continuam a fonte de instalação.
- Telemetria, upload de logs ou coleta remota; os logs são locais e o report é gerado sob demanda.
- Resolver a sessão remota (SSH/Tuxee/XDG); o runner assume `systemctl --user` como no desktop. O
  caminho sem systemd continua documentado como manual (`podstackd` no mesmo `XDG_RUNTIME_DIR`).
- Logs de containers/machines (já existem nos detalhes); aqui são os logs **do app**.

## 4. Requisitos funcionais

### 4.1 Ciclo de atualização

| ID | Requisito | P |
|---|---|---|
| FR-UPD-002 | Em upgrade, o `preinst` (Debian) e o `%pre` (RPM) encerram a GUI e o `podstackd` de todos os usuários e param o `podstack-helper.service` antes do unpack; o runner de update (`podstack update --install`) **não** é encerrado. | P0 |
| FR-UPD-003 | O `postinst`/`%post` recarregam as units, habilitam o `podstack.socket` global, habilitam e **reinicializam** o `podstack-helper.service` (não apenas `enable --now`); o socket do usuário é reativado na próxima conexão. | P0 |
| FR-UPD-004 | A GUI e o CLI oferecem `update --install` (com `--yes`/consentimento); baixam o artefato do manifesto, verificam assinatura Ed25519/minisign, SHA-256, canal e versão; recusam downgrade/versão igual. | P0 |
| FR-UPD-005 | A instalação roda em um **runner destacado** (`podstack update --install --runner`), que para o daemon/socket do usuário, encerra a GUI (ela inicia o runner e sai), instala via agente privilegiado **fora do cgroup do helper** e, no sucesso, inicia o socket/daemon e a GUI nova. | P0 |
| FR-UPD-006 | Em falha do pacote, o runner relata o erro, mantém/restaura a versão instalada e reinicia a GUI/daemon antigos; não deixa o app parado. | P0 |
| FR-UPD-007 | O primeiro update de uma versão sem runner (0.1.3→0.1.4) é manual (`apt`/`dnf`), coberto pelos scripts do pacote; o runner entra a partir da 0.1.4. | P0 |
| FR-UPD-008 | O update é serializado por lock por usuário (`~/.local/state/podstack/updates/update.lock`), grava estado/fase (`state.json`) e recupera um runner interrompido no próximo start. | P1 |
| FR-UPD-009 | O manifest/artefato é baixado para `~/.cache/podstack/updates/<version>/`, 0600; o runner/root script validam caminho regular, dono, ausência de symlink e SHA antes de instalar. | P0 |
| FR-UPD-010 | O update **nunca** usa o helper como processo pai do gerenciador de pacotes (evita que o `postinst` mate o próprio `apt`/`dnf`); o privilégio vem de `pkexec`/polkit com script root dedicado. | P0 |
| FR-UPD-011 | A GUI mostra progresso, versão, notas e o erro; o CLI retorna códigos estáveis e suporta `--json`; o app avisa para salvar/fechar e reinicia ao final. | P1 |
| FR-UPD-012 | Containers/Podman, machines e dados do usuário continuam intactos; o update para o daemon, não o engine. | P0 |

### 4.2 Logs de debug e retenção

| ID | Requisito | P |
|---|---|---|
| FR-LOG-001 | O app grava logs estruturados (JSON Lines: timestamp, nível, categoria, processo, PID, thread, mensagem/contexto) de daemon, GUI, CLI, QML/Qt, setup jobs e update em `~/.local/state/podstack/logs/` (0600, dir 0700). | P0 |
| FR-LOG-002 | O nível é configurável (`advanced.logLevel`: error/warn/info/debug/trace; default info), por setting/GUI/`PODSTACK_LOG_LEVEL`; debug não exige rebuild. | P0 |
| FR-LOG-003 | Rotação diária e retenção de **7 dias** por padrão (configurável 1–30), com limite de tamanho por arquivo/total; a limpeza roda no start e diariamente; o arquivo mais antigo é removido primeiro. | P0 |
| FR-LOG-004 | Os logs do helper (`podstack-helper.service`) e dos scripts de pacote/update (`/var/log/podstack/`, root 0600, 7 dias) são coletáveis pelo suporte; o app expõe o que tem permissão e o helper expõe o restante via `GetLogs`/relatório. | P1 |
| FR-LOG-005 | O job de setup (ação, id, linhas, erro) é persistido no log do daemon e aparece em `podstack logs`/GUI/report, sobrevivendo ao restart do daemon. | P0 |
| FR-LOG-006 | `podstack logs [--follow|--since|--level|--component|--json|--export]` e uma página/diálogo de logs na GUI (abrir pasta, filtrar, exportar) acessam os 7 dias; setup/update expõem “Ver log” no erro. | P0 |
| FR-LOG-007 | Redaction obrigatória (tokens, Authorization, senhas, URLs com credenciais, env sensível) em todos os sinks e no report; nenhum segredo em log. | P0 |
| FR-LOG-008 | O `system.report --include-logs` passa a ler os arquivos rotacionados e os logs do helper/update, respeitando redaction, 0600 e os 7 dias; não envia nada. | P1 |
| FR-LOG-009 | O usuário pode apagar os próprios logs e desligar debug; os logs do helper/pacote são root e não acessíveis a outros usuários. | P1 |

## 5. Requisitos não funcionais

| ID | Requisito |
|---|---|
| NFR-UPD-001 | Update com o app aberto não perde dados de usuário; `UiState` salvo; containers/machines continuam rodando. |
| NFR-UPD-002 | Encerramento com SIGTERM e carência de 3 s; SIGKILL após; sem órfãos; `pgrep` limpo. |
| NFR-UPD-003 | Instalação verificada; falha de assinatura/SHA/versão não executa o gerenciador de pacotes. |
| NFR-UPD-004 | Nenhum segredo no argv/log; o polkit/pkexec autentica; o script root valida o caminho. |
| NFR-LOG-001 | Logging não bloqueia a UI (> 16 ms) e não perde linhas em crash; escrita append com mutex/rotação atômica. |
| NFR-LOG-002 | 7 dias por padrão; limite total (ex. 100 MB) e por arquivo (ex. 25 MB); 0600/0700; redaction. |
| NFR-LOG-003 | Custo de debug mensurável; o volume de debug não derruba o app; a retenção é verificada. |

## 6. Casos de uso

**UC-1 — `apt` com o app aberto (incidente 0.1.3).** O usuário instala o `.deb` novo pelo terminal.
O `preinst` encerra a GUI/daemon/helper antigos; o `postinst` reinicia o helper e reabilita o socket;
o usuário abre a GUI nova. O OVMF passa a ser reconhecido, sem symlinks e sem restart manual.

**UC-2 — GUI “Atualizar”.** O usuário vê a versão nova, as notas e o consentimento; o app baixa e
verifica o pacote; a GUI inicia o runner e sai; o runner para o daemon, instala via `pkexec`, espera
o `postinst`, reinicia o socket/daemon e a GUI nova; o progresso aparece no runner/notificação.

**UC-3 — CLI `podstack update --install`.** O usuário no terminal confirma; o runner faz o mesmo;
o polkit pede a senha; o terminal mostra progresso e logs; no fim, `podstack version` e o daemon
novos; exit 0 (ou erro estável).

**UC-4 — Falha no update.** O `pkexec`/gerenciador falha (rede, assinatura, lock, espaço); o runner
religa a GUI/daemon antigos, mostra o erro e aponta o log; nada fica parado.

**UC-5 — Suporte ao OVMF.** O setup falha; o usuário abre “Ver log”/`podstack logs` e vê o erro do
helper e o check; o `podstack report --include-logs` gera o relatório redigido com o job de setup e
os logs do daemon/helper/update dos últimos 7 dias.

**UC-6 — Debug ligado.** O usuário ativa `advanced.logLevel=debug`; as linhas de RPC, Podman, setup
e update passam a ser gravadas; após 7 dias, os arquivos mais antigos são removidos; o disco não
cresce sem limite.

## 7. Critérios de aceite

| ID | Dado | Quando | Então |
|---|---|---|---|
| AC-1 | Ubuntu 24.04, GUI+daemon+helper 0.1.3 abertos | `sudo apt install ./podstack_0.1.4_amd64.deb` | `preinst` encerra os antigos; `postinst` reinicia o helper; PIDs antigos ausentes; GUI nova abre; `podstack version`/daemon 0.1.4; OVMF `ok`; containers intactos |
| AC-2 | 0.1.4 com app aberto | GUI “Atualizar” para 0.1.5 | download/verificação; GUI fecha; runner instala; GUI/daemon novos; log do update; sem processo antigo |
| AC-3 | mesmo cenário | falha simulada do gerenciador | runner relata erro; GUI/daemon antigos voltam; log mostra a causa; nenhum processo órfão |
| AC-4 | app usado com setup/update/erro | `podstack logs`/GUI/report | logs do daemon/GUI/setup/update/helper/package; 7 dias; 0600; redaction; “Ver log” no erro |
| AC-5 | 0.1.4→0.1.5 | 8º dia | o arquivo mais antigo foi removido; os 7 dias restantes existem; limite total respeitado |
| AC-6 | manifesto com assinatura/SHA inválidos | `update --install` | nenhum gerenciador de pacotes é executado; erro estável; log registra a recusa |
| AC-7 | app aberto com containers/machines rodando | update | containers/machines continuam; só o daemon/GUI/helper são reiniciados |

## 8. Fora de escopo

Rollback automático de versão; update delta/binário; update do Flatpak; auto-update silencioso;
múltiplos usuários simultâneos com notificação; logs remotos/telemetria; rotina de suporte online;
resolver o `XDG_RUNTIME_DIR` de sessões Tuxee/SSH (o runner documenta o caminho manual).

## 9. Decisões abertas

| ID | Decisão | Recomendação |
|---|---|---|
| D1 | O update assistido (GUI/CLI) entra no escopo ou só os scripts do pacote? | Ambos: scripts resolvem o `apt` manual (incidente 0.1.3); o runner entrega o update automático a partir da 0.1.4. |
| D2 | Mecanismo privilegiado: `pkexec` + script root dedicado vs helper `InstallUpdate` + unit transiente? | `pkexec`/script root para a instalação; o helper é parado/religado pelo pacote. Evita o cgroup do helper e o bootstrap do método novo. |
| D3 | Política do `preinst`: SIGTERM + carência + SIGKILL em todos os usuários? | Sim, best effort; o app salva `UiState`; containers/machines não param. O app updater é gracioso; o `apt` manual é “frio”. |
| D4 | Formato/local dos logs: JSON Lines em `~/.local/state/podstack/logs` + `/var/log/podstack`? | Sim; 0600/0700; 7 dias; limite total; helper/update/package em `/var/log/podstack` root. |
| D5 | Debug default e ajuste: `advanced.logLevel` (info) + GUI/`PODSTACK_LOG_LEVEL`? | Sim; debug opt-in; reaplica no próximo start; 7 dias. |
| D6 | Auto-reabrir a GUI no `apt` manual? | Não é possível de forma confiável pelo `postinst` (root/sessão); o usuário abre. O runner reabre no update assistido. |
| D7 | Rollback de pacote? | Não automático no v1; a versão anterior pode ser reinstalada manualmente; o runner religa a versão que estiver instalada. |
| D8 | Acesso aos logs do helper: journal + `GetLogs` ou só `pkexec`/journal? | `GetLogs` allowlisted no helper para o app/report, com redaction; o journal continua para auditoria. |

## 10. Rastreabilidade

| Requisito | Tarefa |
|---|---|
| FR-UPD-002…003 (pacote) | T-UPD-01, T-UPD-02 |
| FR-UPD-004…006, 008…011 (runner/UI) | T-UPD-03, T-UPD-04 |
| FR-UPD-007 (bootstrap) | T-UPD-02, T-UPD-04 |
| FR-UPD-012 (dados/containers) | T-UPD-02, T-UPD-03 |
| FR-LOG-001…003, 007 (core/retenção) | T-LOG-01 |
| FR-LOG-004…005, 008…009 (helper/setup/report) | T-LOG-02 |
| FR-LOG-006 (CLI/GUI) | T-LOG-03 |
| Todos os AC | T-E2E-01 |
| Docs/web/ADR | T-DOC-01 |

Referências existentes: `FR-UPD-001` (check assinado), `T7-06`; `FR-SYS-009` (logs com rotação),
`T7-08`/`system.report`; `T7-01`/`advanced.logLevel`; `packaging/deb/postinst`; `%post` do RPM;
`src/core/Logging.cpp`; `src/daemon/UpdateChecker.*`; `src/daemon/SetupManager.cpp`.
