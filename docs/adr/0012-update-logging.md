# ADR-0012 — Atualização segura e logs de debug do app

| Campo | Valor |
|---|---|
| Status | Aceito |
| Data | 2026-11-13 |
| Decisores | Arquitetura |
| Relacionados | `spec.md §15`, `plan.md §15`, `PRD.md §7.17/§7.19/§8.7/§8.8`, TX-15/TX-16, [`specs/002-update-seguro-logs-debug/`](../specs/002-update-seguro-logs-debug/) |

## Contexto

A atualização 0.1.2→0.1.3 instalou os binários novos, mas a GUI, o `podstackd` e o
`podstack-helper` antigos continuaram rodando: o `postinst` só fazia `enable --now` do helper, e um
serviço já ativo não é reiniciado. O resultado foi um daemon com o check de OVMF antigo e um helper
com allowlist velha, exigindo restart manual dos três processos.

O update atual é apenas verificação (`updates.check`); não há instalação orquestrada. Os logs também
estão incompletos: `Logging::init` só define o padrão de mensagem, o daemon depende de um
redirecionamento do systemd que a unit não define, não há rotação/retenção/redaction no caminho das
mensagens, o log do setup vive em memória e o helper grava só no journal. O `FR-SYS-009` pede logs
estruturados com rotação e redaction em `~/.local/state/podstack/logs/`.

## Opções

| Opção | Prós | Contras |
|---|---|---|
| Só scripts do pacote (`preinst`/`postinst`) | resolve o `apt`/`dnf` manual e o incidente; simples | não há update assistido; o usuário reabre |
| Scripts + runner + helper `StageUpdate`/`InstallUpdate` + unidade transiente | update assistido; valida assinatura no root; fora dos cgroups; sobrevive ao restart do helper | bootstrap da 0.1.3; polkit; mais peças |
| Segunda fronteira root dedicada (script próprio) | aparentemente simples | viola a constituição/`security.md` (helper é a única fronteira root), sem allowlist/auditoria; roda no cgroup da sessão |
| Helper como processo pai do gerenciador | reusa a allowlist | o `postinst` reinicia o helper e pode matar o `apt`/`dnf` filho |
| Lock global mantido pelo helper | mais simples | restart do helper libera o lock e dois updates podem sobrepor |
| Logs só no journal | zero código | não há arquivo de 7 dias do app, setup/helper/update; report incompleto |
| Logs JSON Lines + retenção no app | consulta/export/report; 7 dias; redaction | mais código; limite de disco |
| Logging síncrono com mutex | menos código | I/O no thread da UI; contenção entre processos; não atende `NFR-LOG-001` |

## Decisão

1. **Dois caminhos de update.** O manual (`apt`/`dnf`) é coberto pelo pacote: `preinst`/`%pre` param
   socket/service de todos os usuários logados, aplicam `mask --runtime podstack.socket`, encerram
   GUI/daemon/helper antes do unpack (SIGTERM + 3 s + SIGKILL) e abortam com trap de limpeza se algo
   não parar; `postinst`/`%post` religam o socket e **reinicializam** o helper; `postrm`/`%posttrans`
   restauram nos abortos. O runner assistido entra a partir da 0.1.4.
2. **Update assistido** na GUI/CLI: a GUI grava `gui-was-open`, inicia
   `podstack update --install --runner` com `setsid` e sai; o runner para o socket/daemon, chama
   `StageUpdate`/`InstallUpdate` e faz polling de `GetUpdateStatus` com retry/backoff.
3. **Helper é a única fronteira root.** `StageUpdate` valida assinatura com a **chave de release
   embutida** ou `/etc/podstack/update.pub` root-owned, compara o SHA-256 do artefato ao manifesto,
   confere nome/versão/arch e copia para staging root-owned com `O_NOFOLLOW`; `InstallUpdate`
   serializa a admissão por `flock`, recusa unidade ativa e inicia a unidade transiente de sistema
   (`systemd-run --unit=podstack-update-<uuid> --collect --property=RuntimeMaxSec=1800`). O lock
   global `/run/podstack/update.lock` pertence à unidade, não ao helper. `updates.publicKey`
   customizada serve só para check/notificação; o install é recusado com
   `PODSTACK_UPDATE_UNSUPPORTED`/`reason=custom_key`.
4. **Estado e logs do update** ficam em `state.json` (`/run/podstack/update/<uuid>/` escrito pelo
   wrapper e `~/.local/state/podstack/updates/` escrito pelo runner) e em
   `/var/log/podstack/jobs/<uid>/<jobId>.jsonl` (`0640`), com redaction no wrapper. O helper
   reconstrói o estado no start varrendo unidades e `state.json`; falha restaura helper/socket e
   religa a versão instalada, sem rollback automático.
5. **Logs do app** usam `qInstallMessageHandler` assíncrono (writer thread, fila 8192,
   drop-oldest), JSON Lines por processo em `~/.local/state/podstack/logs/` (`0600`/`0700`),
   rotação diária e por tamanho (`<processo>-YYYYMMDD[-N].jsonl`), retenção de **7 dias**
   (1–30), `advanced.logMaxBytes` 10 MB, teto 200 MB, timer da meia-noite local e rotação no start.
   `advanced.logLevel` é exposto na GUI e aplicado em runtime (`restartRequired: false`);
   `PODSTACK_LOG_LEVEL` é o override.
6. **Redaction incondicional** antes de gravar, em todos os sinks, logs root e report;
   `diagnostics.redact` não a desliga nesses caminhos. O app nunca lê `/var/log/podstack`
   diretamente: `GetLogs {jobId?}` (`auth_admin`, cap 1 MiB, rate limit, auditoria) filtra pelo uid
   do chamador via `GetConnectionUnixUser`/polkit.
7. **Superfícies:** `podstack system logs` (filtros/`--json`/`--export`/`--clear`), página/diálogo
   de logs na GUI, “Ver log” no erro de setup/update e `system.report --include-logs` com caps
   (10.000 linhas/componente, 5 MB). `diagnostics.includeLogs` passa a default **false**.
8. **Machines sobrevivem:** QEMU/virtiofsd ficam em scope próprio (`systemd-run --user --scope`);
   `KillMode=process` é apenas defesa em profundidade e não move processos de cgroup.

## Consequências

**Positivas:** nenhum caminho de update deixa processos antigos; o `apt` manual resolve o incidente
sem restart manual; o update assistido reabre o app na versão nova; uma única fronteira root
auditável; 7 dias de logs locais com redaction e retenção; suporte consegue pedir um report
completo e redigido.

**Negativas:** mais peças (runner, helper, unidade transiente, wrapper) e mais estados de falha;
scriptlets root exigem teste em container; o primeiro update (0.1.3→0.1.4) continua manual; logs
ocupam até 200 MB; o formato JSONL e os nomes de arquivo passam a ser contrato observável.

## Implementação

- `src/core/Logging.{h,cpp}`, `src/core/LogWriter.*`, `src/core/LogRotation.*`;
  `src/core/Settings`/schema; `src/daemon/{main,Daemon,SetupManager,UpdateChecker}.*`;
  `src/gui/{main,MainWindow,Backend}.*` e QML; `src/cli/main.cpp`;
  `src/helper/{main,HelperService}.*`; `src/machines/QemuRuntime.cpp`.
- `packaging/deb/{preinst,postinst,postrm,prerm}`, `packaging/rpm/podstack.spec`,
  `packaging/podstack-update-exec`, `packaging/tmpfiles.d/podstack.conf`,
  `packaging/logrotate/podstack` e ações polkit.
- `packaging/systemd/podstack.service`, `packaging/systemd-system/podstack-helper.service`,
  `packaging/selinux/*`, `scripts/build-{deb,rpm}.sh` e `scripts/clean-install-check.sh`.
- Testes: `test_logging`, `test_update_runner`, `test_helper_update`, `test_cli` (golden exit
  codes) e scriptlets em container; matriz E2E em [`testing.md`](../sdd/testing.md) §16.
