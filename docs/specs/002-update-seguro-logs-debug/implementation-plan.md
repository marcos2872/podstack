# Plano de implementação — 002 Atualização segura e logs de debug

> **Status:** aprovado para execução (2026-11-13) — **Fases 0–5 concluídas** (promoção SDD,
> T-UPD-00…03, T-LOG-01/02); T-LOG-03, T-UPD-04 e E2E real pendentes.
> **Escopo:** spec inteira (`T-UPD-00…04`, `T-LOG-01…03`, `T-E2E-01`, `T-DOC-01`).
> **Base:** [`spec.md`](spec.md), [`plan.md`](plan.md) e [`tasks.md`](tasks.md).

## 1. Decisões de execução

| Decisão | Escolha |
|---|---|
| Escopo da branch | Spec inteira, em fases verificáveis |
| Promoção ao SDD | **Antes** de codar: promover FR/NFR/tarefas para `docs/sdd/*` e criar o ADR-0012 |
| E2E real | Implementar e cobrir com unitários/integração/containers; `T-E2E-01` fica **pendente** com roteiro e evidência parcial |
| Superfícies | Backend + CLI primeiro; GUI depois, na fase de UI |
| Branch | `feat/spec-002-update-seguro-logs-debug` (base `main`) |

## 2. Fases

### Fase 0 — Promoção ao núcleo SDD (somente docs)

**Objetivo:** transformar a proposta em fonte de verdade antes de qualquer código.

- Promover `FR-UPD-006…016`, `FR-LOG-001…009`, `NFR-UPD-001…004` e `NFR-LOG-001…003` para
  `docs/sdd/PRD.md`.
- Adicionar a seção de atualização/logs em `docs/sdd/spec.md`, as decisões em `docs/sdd/plan.md`
  e as tarefas `TX-15`/`TX-16` em `docs/sdd/tasks.md`.
- Atualizar `docs/sdd/testing.md` com a matriz de testes e `docs/adr/0012-update-logging.md`.
- Gate: links/âncoras de `docs/` sem quebras; revisão de requisitos e critérios de aceite.

### Fase 1 — `T-UPD-00`: machines fora do cgroup

- Colocar QEMU/virtiofsd em scope próprio (`systemd-run --user --scope`).
- Manter `KillMode=process` apenas como defesa; não mover processos de cgroup por configuração.
- Cobrir a reconciliação de órfãos existente.
- Validação: teste unitário/integração com machine rodando; restart do daemon sem matar a VM;
  mounts virtiofs intactos.

### Fase 2 — `T-UPD-01/02`: scripts do pacote e SIGTERM

- Criar `packaging/deb/preinst` e `%pre` do RPM com parada de socket/service de todos os usuários
  logados, máscara runtime, SIGTERM + 3 s + SIGKILL, fallback `pkill -x podstackd` e trap de limpeza.
- Corrigir `postinst`/`%post`/`%posttrans`, criar `postrm` e ajustar `%preun`/`%posttrans`;
  reiniciar o helper de verdade e restaurar socket/máscara nos abortos.
- Adicionar handler de SIGTERM/SIGINT na GUI para salvar `UiState`.
- Validação: containers com processos falsos; upgrade 0.1.3→0.1.4 com app aberto; falhas
  injetadas em unpack/`postinst` e após `%pre`; PIDs antigos ausentes; `pgrep` limpo.

### Fase 3 — `T-LOG-01`: núcleo de logging

- `LogWriter`/`LogRotation` com `qInstallMessageHandler` assíncrono, fila limitada/drop-oldest,
  JSON Lines, arquivo por processo, `O_APPEND`, flush de 1 s/warn+, `0600`/`0700`.
- Rotação diária e por tamanho, retenção de 7 dias (`1–30`), `advanced.logMaxBytes` (10 MB),
  teto total de 200 MB, timer da meia-noite local e rotação no start.
- Expor `advanced.logLevel` na GUI, aplicar em runtime (`restartRequired: false`) e suportar
  `PODSTACK_LOG_LEVEL`; redaction incondicional em daemon/GUI/CLI/QML.
- Validação: rotação/retenção no 8º dia, nomes/sequência, JSON, redaction com
  `diagnostics.redact=false`, permissões, concorrência daemon+CLI, `kill -9` e UI não bloqueada.

### Fase 4 — `T-UPD-03`: runner assistido e helper

- `podstack update --install [--yes] [--json]` + flags ocultas `--runner`/`--manifest`.
- Cache do manifesto/artefato (`0600`, TTL 6 h, `.part` + rename, limite 300 MB, timeouts 15/5/30 min).
- Helper com `StageUpdate`/`InstallUpdate`/`GetUpdateStatus`, staging root-owned, chave de release
  embutida/root-owned, `O_NOFOLLOW`, UID via `GetConnectionUnixUser`, recusa de unidade ativa e
  admissão serializada por `flock`.
- Wrapper `podstack-update-exec` + unidade transiente, lock global, `state.json` atômico e
  retomada por `jobId`.
- Validação: e2e 0.1.4→0.1.5 em ambiente controlado; falha simulada restaura o app; assinatura/
  chave customizada/path/TOCTOU/identidade recusados; machine rodando intacta; exit codes
  `0/1/2/6/8/9`.

### Fase 5 — `T-LOG-02`: logs root, setup e report

- Persistir setup jobs no log do daemon; logs root em `/var/log/podstack/jobs/<uid>/<jobId>.jsonl`
  (`0750`/`0640`), `tmpfiles.d`, logrotate/timer e SELinux/AppArmor.
- `GetLogs {jobId?}` via D-Bus escopado ao uid do chamador (`GetConnectionUnixUser`/polkit,
  cap 1 MiB, rate limit, auditoria).
- `system.report --include-logs` com caps (10.000 linhas/componente, 5 MB) e
  `diagnostics.includeLogs=false`; backend do `--clear`; migrar/remover `podstackd.log`.
- Validação: erro de setup/helper visível após restart; uid A não vê B; permissões root; token
  redigido; report vazio/volumetria; `--clear` backend.

### Fase 6 — `T-UPD-04` + `T-LOG-03`: CLI e GUI

- **CLI primeiro:** `podstack system logs` (`--follow`, `--since`, `--level`, `--component`,
  `--json`, `--export`, `--clear`), `update --install`, exit codes e `--json` estáveis.
- **GUI depois:** página/diálogo de logs (filtro, follow, abrir pasta, exportar, limpar,
  “Exportar diagnóstico”), “Ver log” no erro de setup/update, progresso do update por notificação
  nativa + `state.json`/`GetUpdateStatus`, marcador `gui-was-open`/`--relaunch` e retomada/limpeza.
- Validação: `--json` schema/golden; `--export` `0600` com destino canonicalizado/sem symlink;
  filtro inválido retorna 2; `qmllint`; leitura em worker/modelo virtualizado; i18n/`Accessible.name`.

### Fase 7 — `T-E2E-01`: validação real (pendente)

- Roteiro para Ubuntu 24.04/26.04 e Fedora: upgrade manual 0.1.3→0.1.4 com GUI/daemon/helper
  abertos e machine rodando; 0.1.4→0.1.5 assistido; falhas de download/assinatura/unpack/`postinst`
  e após `%pre` RPM; restart do helper no meio do `apt`; dois usuários + lock; 7 dias/rotação;
  uid A não vê B; containers/machines intactos.
- **Status inicial:** pendente de VMs/pacotes reais; evidência parcial por unitários, integração
  e containers.

### Fase 8 — `T-DOC-01`: documentação final

- Atualizar `engineering/{packaging,settings,architecture,security,cli-parity,ui-map}.md`,
  `api-rpc.md` (apenas `system.report`), `sdd/testing.md`, `docs/user/*`, `web/*`, `translations/`,
  `CHANGELOG`, ADR-0012 e índice de ADRs; bump `0.1.3→0.1.4` em `CMakeLists.txt` e
  `web/_data/site.json`.
- Gate: `scripts/web_build.py --check`, links/âncoras de `docs/`, diff de settings/schema.

## 3. Validação por fase

- `ctest --preset dev` com os testes novos e os existentes verdes.
- `clang-format --dry-run --Werror`, `clang-tidy` e `qmllint` nas áreas tocadas.
- Cobertura ≥ 80% mantida.
- Nenhum segredo em argv/log; permissões `0600`/`0640`; helper como única fronteira root;
  redaction incondicional; assinatura/SHA validados no helper.
- Containers/machines intactos; `pgrep` sem PIDs antigos; socket não reativa daemon antigo.

## 4. Riscos e rollback

| Risco | Mitigação |
|---|---|
| Scriptlet quebrar o upgrade e deixar o app parado | trap de limpeza, `postrm` Debian e `%posttrans` RPM; teste em container antes do E2E |
| Helper reiniciar durante a unidade transiente | unidade fora do cgroup do helper; lock global; polling de `GetUpdateStatus` |
| Logging bloquear a UI | writer thread, fila limitada/drop-oldest, flush assíncrono; teste de UI |
| Logs crescerem sem limite | retenção 1–30 dias, limite por arquivo e teto de 200 MB; teste do 8º dia |
| Regressão em containers/machines | scope próprio de QEMU/virtiofsd; reconciliação de órfãos; teste com machine rodando |

**Rollback:** cada fase é um commit independente; a branch pode ser revertida por fase. Não há
rollback automático de pacote no v1; a versão anterior pode ser reinstalada manualmente.

## 5. Fora de escopo

Rollback automático de versão; update delta/binário; update do Flatpak; auto-update silencioso;
notificação simultânea a múltiplos usuários; logs remotos/telemetria; suporte online; resolução do
`XDG_RUNTIME_DIR` de sessões Tuxee/SSH; E2E real nesta rodada.
