# 002 — Atualização segura e logs de debug (7 dias)

> **Status:** proposta — não promovida ao núcleo SDD.
> **Marcos:** TX-15 (atualização segura) e TX-16 (logs de debug) — transversais.
> **Data:** 2026-11-12.
> **Promove para:** [`../../sdd/PRD.md`](../../sdd/PRD.md) (FR-UPD-006…016, FR-LOG-001…009,
> NFR-UPD-001…004, NFR-LOG-001…003, FR-SYS-009),
> [`../../sdd/spec.md`](../../sdd/spec.md) (nova seção de atualização segura e logs do app),
> [`../../sdd/plan.md`](../../sdd/plan.md), [`../../sdd/tasks.md`](../../sdd/tasks.md)
> (TX-15/TX-16), [`../../sdd/testing.md`](../../sdd/testing.md),
> [`../../engineering/api-rpc.md`](../../engineering/api-rpc.md) (`system.report`; `GetLogs` fica no
> contrato do helper/`security.md`),
> [`../../engineering/packaging.md`](../../engineering/packaging.md),
> [`../../engineering/settings.md`](../../engineering/settings.md),
> [`../../engineering/architecture.md`](../../engineering/architecture.md),
> [`../../engineering/security.md`](../../engineering/security.md),
> [`../../engineering/cli-parity.md`](../../engineering/cli-parity.md),
> [`../../engineering/ui-map.md`](../../engineering/ui-map.md),
> **ADR-0012** (esta spec).
> **Docs de apoio:** [`../../README.md`](../../README.md), `AGENTS.md`, `translations/`,
> `docs/user/*`, `web/*`, `CHANGELOG.md`, `CMakeLists.txt`.

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
mensagem; o daemon depende do redirecionamento do systemd, que a unit não define, não há
rotação/retenção no app, e o log do job de setup existe apenas em memória/GUI. A GUI só salva o
`UiState` em `closeEvent`, não em SIGTERM. O helper grava no journal do sistema. O
`FR-SYS-009` pede logs estruturados com rotação e redaction em `~/.local/state/podstack/logs/`; a
implementação está incompleta.

## 2. Objetivos

| # | Objetivo | Métrica |
|---|---|---|
| O1 | Nenhum caminho de update deixa processo antigo ativo | após o update, `pgrep -x podstack-gui/podstackd` não encontra os PIDs anteriores; helper, socket e daemon são os novos e o socket não reativa o binário antigo |
| O2 | Atualização assistida pela GUI/CLI com consentimento, assinatura e reinício automático | 0.1.4→0.1.5: a GUI fecha, o helper allowlisted instala em unidade transiente, e a GUI reabre na versão nova; falha restaura o app instalado |
| O3 | O caminho manual `apt`/`dnf` também é seguro | ao instalar o pacote novo com o app aberto, o `preinst` para o socket/service de todos os usuários logados, encerra GUI/daemon/helper antes do unpack e o `postinst` religa o helper; machines e containers intactos |
| O4 | Logs de debug de tudo, com retenção de 7 dias | daemon/GUI/CLI/setup/update/helper/gerenciador em arquivos `0600`/`0640`; 7 dias; redaction incondicional; `podstack system logs`/GUI/report |

## 3. Não-objetivos

- Auto-update sem consentimento; downgrade automático; rollback automático de pacote.
- Preservar o estado da GUI durante o update; o app pode ser encerrado (o `UiState` é salvo antes,
  inclusive quando o encerramento vem de SIGTERM).
- Parar containers, Podman ou o helper além do necessário para o update. Machines nunca param:
  QEMU/virtiofsd ficam fora do cgroup do `podstack.service` (ver FR-UPD-016).
- Criar uma segunda fronteira root ou executar o gerenciador de pacotes fora do helper allowlisted e
  da unidade transiente de sistema.
- Substituir o gerenciador de pacotes; `.deb`/RPM continuam a fonte de instalação.
- Telemetria, upload de logs ou coleta remota; os logs são locais e o report é gerado sob demanda.
- Resolver a sessão remota (SSH/Tuxee/XDG); o update assistido assume systemd de usuário no desktop.
  Sem systemd ou em Flatpak, o app detecta, desabilita o update assistido com mensagem e mantém o
  caminho manual documentado (`podstackd` no mesmo `XDG_RUNTIME_DIR`).
- Logs de containers/machines (já existem nos detalhes); aqui são os logs **do app**.

## 4. Requisitos funcionais

### 4.1 Ciclo de atualização

| ID | Requisito | P |
|---|---|---|
| FR-UPD-006 | Em upgrade, o `preinst` (Debian) e o `%pre` (RPM) param `podstack.socket` e `podstack.service` de **todos** os usuários logados quando há systemd (`loginctl` + `systemctl --machine user@<uid>.service --user`), aplicam máscara runtime no socket para impedir socket activation durante o unpack, encerram a GUI com SIGTERM (SIGKILL após 3 s) e o `podstackd` com parada graciosa (fallback `pkill -x podstackd` quando não há systemd **ou o user manager não responde a `systemctl --machine … --user`**), conferem `systemctl is-active` do `podstack-helper.service` antes de seguir e o param; o runner de update (`podstack update --install --runner`) **não** é encerrado. Se um processo não puder ser encerrado, o script executa um **trap de limpeza** (desfaz a máscara runtime e religa o helper dos usuários já tocados) e aborta com mensagem clara em vez de seguir com binários antigos. | P0 |
| FR-UPD-007 | O `postinst`/`%post` recarregam as units, habilitam o `podstack.socket` global, removem a máscara runtime do socket dos usuários logados, habilitam e **reinicializam** o `podstack-helper.service` (não apenas `enable --now`) e não iniciam GUI/daemon de usuário, que sobem no próximo uso. No Debian, os casos `abort-upgrade`/`abort-install` do `postrm` restauram o helper, removem a máscara runtime e avisam o usuário. No RPM, a ordem é `%pre` novo → `%preun` antigo → remoção dos arquivos antigos → `%post` novo → `%posttrans` novo; o `%posttrans` é a recuperação: repete o loop `loginctl` + `systemctl --machine user@<uid>.service --user unmask --runtime podstack.socket` e valida por usuário (o manager de sistema não desmascara unit de usuário), `systemctl enable --now podstack-helper.service`, health check (`is-active` + versão), mensagem com instrução e exit não-zero **somente** se não conseguir restaurar. O RPM não tem os casos de aborto do `postrm` Debian. Falha de `unmask --runtime` informa a remediação do socket e tenta novamente; falha do helper informa `systemctl restart podstack-helper.service`. | P0 |
| FR-UPD-008 | A GUI e o CLI oferecem `update --install` (com `--yes`/consentimento) a partir do `updates.check` (`FR-UPD-001` do PRD); o check retorna metadados do manifesto; o runner baixa o manifesto assinado de `updates.feedUrl` (mesma origem do check) e grava `~/.cache/podstack/updates/manifest.json` (`0600`, TTL 6 h) e o artefato em cache `0700`/`0600` com `.part` + rename atômico, limite de 300 MB, preflight de espaço e timeout de download de 15 min; validam canal (`updates.channel`, `FR-UPD-002` do PRD), arquitetura, formato e recusam versão igual/downgrade antes de qualquer elevação de privilégio. A assinatura do install assistido usa a **chave de release embutida** no helper (ou `/etc/podstack/update.pub` root-owned); `updates.publicKey` serve só para check/notificação. Se `updates.publicKey` customizada estiver ativa, o install assistido é recusado com `PODSTACK_UPDATE_UNSUPPORTED`/`reason=custom_key` e caminho manual. | P0 |
| FR-UPD-009 | A instalação roda em um **runner destacado** (`podstack update --install --runner`, `--manifest` interno/oculto), que para o socket/daemon do usuário (a GUI já saiu ao iniciar o runner e gravou `~/.local/state/podstack/updates/gui-was-open`), chama o helper allowlisted (`StageUpdate`/`InstallUpdate`) e faz **polling de `GetUpdateStatus` com retry/backoff** enquanto o helper estiver parado/reiniciando; não depende de sinal D-Bus contínuo. O helper inicia a unidade transiente de sistema (`systemd-run --unit=podstack-update-<uuid> --collect --property=RuntimeMaxSec=1800`), fora do cgroup do helper e da sessão, com timeout de instalação de 30 min. No sucesso, o runner inicia o socket/daemon e reabre a GUI quando o marcador existe (e o apaga); caso contrário, o usuário abre. | P0 |
| FR-UPD-010 | Em falha do pacote ou do gerenciador, o helper é mantido ou restaurado ativo, o runner relata o erro com código estável, garante que a versão instalada volte a subir (GUI/daemon antigos ou a que estiver instalada) e não deixa o app parado; o estado e o log apontam a causa. Falha do `preinst`/unpack/`postinst` é recuperável pelos casos de aborto dos scriptlets e pela recuperação do `%posttrans` no RPM. Na próxima execução de `podstack update`/GUI, o `state.json` é lido, o `jobId` é recuperado e `GetUpdateStatus(jobId)` é consultado com polling/backoff; o usuário pode **retomar ou limpar** o estado; o daemon não instala nem decide update. O rollback manual (reinstalar a versão anterior) continua possível. | P0 |
| FR-UPD-011 | O primeiro update de uma versão sem runner (0.1.3→0.1.4) é manual (`apt`/`dnf`), coberto pelos scripts do pacote; o runner entra a partir da 0.1.4. | P0 |
| FR-UPD-012 | O update é serializado por lock **global** `/run/podstack/update.lock` (flock) mantido pela **unidade transiente** (wrapper `flock`, não pelo helper), que serializa múltiplos usuários e sobrevive ao restart do helper. O helper recusa `InstallUpdate` se existir unidade `podstack-update-*` ativa e, ao subir, varre unidades ativas e `state.json` para reconstruir o estado do job (não confia em memória). O runner grava `~/.local/state/podstack/updates/state.json` com escrita atômica (`QSaveFile`/rename), fases (`queued/stopping/installing/restarting/done/failed`), `jobId`, `unit`, `stateDir`, `pid`, `boot_id` e timestamp; na próxima execução de `podstack update`/GUI, o estado é lido e o usuário pode retomar (via `GetUpdateStatus(jobId)`) ou limpar. | P1 |
| FR-UPD-013 | O manifesto baixado e o artefato ficam em `~/.cache/podstack/updates/` (dir `0700`, arquivos `0600`), com timeout de download de 15 min, `.part` + rename, limite de 300 MB, limpeza de versões antigas e TTL de 6 h do manifesto. O helper copia o artefato para staging root-owned `0700` em `/run/podstack/update/<uuid>/` com `O_NOFOLLOW`, canonicalização e checagem de dono obtida por `GetConnectionUnixUser`/polkit, com timeout de stage de 5 min; valida a assinatura Ed25519/minisign com a **chave de release embutida** ou `/etc/podstack/update.pub` root-owned e compara o **SHA-256 do artefato** ao valor contido no manifesto assinado; confere nome/versão/arch (`dpkg-deb -f`/`rpm -qp`) antes de instalar. O wrapper `podstack-update-exec` executa o gerenciador e grava `/run/podstack/update/<uuid>/state.json` (root `0640`) com fase/percentual/exit/error, além do log do job. | P0 |
| FR-UPD-014 | O helper allowlisted é a **única** fronteira root: expõe `StageUpdate`, `InstallUpdate` e `GetUpdateStatus` com as ações polkit `dev.podstack.helper.stage-update`, `dev.podstack.helper.install-update` (`auth_admin`) e `dev.podstack.helper.get-update-status` (`auth_admin_keep`, para o polling não pedir senha a cada chamada), rate limit (10/min por uid) e auditoria. A admissão é serializada por `flock` em `/run/podstack/update.admission.lock` (checagem de unidade ativa + criação do `systemd-run` atômicas), evitando duas unidades concorrentes. `GetUpdateStatus {jobId}` retorna `{jobId, phase, percent, message, exitCode, errorCode}` e usa o uid de `GetConnectionUnixUser`/polkit; `UpdateProgress`/`UpdateFinished` são best-effort. O helper recusa `InstallUpdate` se houver `podstack-update-*` ativa e, no start, varre unidades/`state.json`. O update **nunca** usa o helper como processo pai do gerenciador, nunca confia em caminho/SHA do chamador e não executa comandos arbitrários (`Exec`/shell). A instalação ocorre na unidade transiente de sistema, não no cgroup do helper. | P0 |
| FR-UPD-015 | A GUI sai antes da instalação; depois da saída, o progresso é comunicado por **notificação nativa** e por `state.json`/`GetUpdateStatus` (o runner faz polling), e ao reabrir a GUI mostra o resultado. O CLI retorna os códigos globais estáveis: `update --check` 0/1/2/3/6/8/9 e `update --install` **0/1/2/6/8/9** (sem 3; `NFR-OBS-004`), com `--json` contendo `error.code`. O app avisa para salvar/fechar, reabre via marcador `gui-was-open` quando aplicável e expõe “Ver log”. | P1 |
| FR-UPD-016 | Containers/Podman, machines e dados do usuário continuam intactos; o update para o daemon/GUI/helper, não o engine. QEMU e virtiofsd ficam em **scope próprio** (`systemd-run --user --scope`, preferencial) para sair do cgroup do `podstack.service`; `KillMode=process` **não move** os processos de cgroup, apenas evita que sejam mortos. Mounts virtiofs permanecem e a reconciliação de órfãos cobre restart. | P0 |

### 4.2 Logs de debug e retenção

| ID | Requisito | P |
|---|---|---|
| FR-LOG-001 | O app grava logs estruturados (`NFR-OBS-001`: JSON Lines com timestamp, nível, categoria, processo, PID, thread, mensagem/contexto) de daemon, GUI, CLI, QML/Qt, setup jobs e update em `~/.local/state/podstack/logs/` (0600, dir 0700), em arquivo **corrente** por processo, sem data (`podstackd.jsonl`, `podstack-gui-<pid>.jsonl`, `podstack-cli-<pid>.jsonl`); a rotação (FR-LOG-003) deriva os nomes datados. | P0 |
| FR-LOG-002 | O nível é configurável (`advanced.logLevel`: error/warn/info/debug/trace; default info), por setting/GUI/`PODSTACK_LOG_LEVEL`; a chave é exposta na GUI (hoje `advanced.*` é oculta) e aplicada em runtime (`restartRequired: false`), permitindo desligar debug sem restart; debug não exige rebuild. | P0 |
| FR-LOG-003 | Rotação diária **e por tamanho**, com nomes derivados do corrente por inserção da data (`podstackd-YYYYMMDD.jsonl`, `podstack-gui-<pid>-YYYYMMDD.jsonl`, `podstack-cli-<pid>-YYYYMMDD.jsonl`) e sequência `…-YYYYMMDD-N.jsonl` para rotação por tamanho; retenção de **7 dias** por padrão (`advanced.logRetentionDays`, configurável 1–30 por setting/GUI/CLI), limite por arquivo (`advanced.logMaxBytes`, default 10 MB) e teto total de 200 MB; um timer diário roda à meia-noite local e a rotação também ocorre no start; ao atingir o limite no meio do dia, o arquivo é rotacionado para sequência e o mais antigo é removido primeiro. | P0 |
| FR-LOG-004 | Os logs root por operação ficam em `/var/log/podstack/jobs/<uid>/<jobId>.jsonl` (root, dir `0750`, arquivo `0640`), com `uid`/`jobId` em cada linha/índice; o wrapper exporta `PODSTACK_UPDATE_JOB_ID` e `PODSTACK_UPDATE_UID` para os scriptlets, que emitem via `logger` as chaves `JOB_ID`/`UID` no journal; `GetLogs` combina o arquivo do job com as entradas do journal filtradas por `JOB_ID`/uid do chamador (cobre `--component package`/`helper`). `jobId` é `update-<uuid>`, então o agrupamento por update é o próprio `jobId` (não há diretório separado `update-<uuid>/`). Os logs gerais do helper/update/pacote também vão para `/var/log/podstack/` com `tmpfiles.d`, logrotate/timer e política SELinux/AppArmor. O app **nunca lê esses arquivos diretamente do filesystem**: o helper expõe `GetLogs {jobId?}` (`dev.podstack.helper.get-logs`, `auth_admin`) somente para operações do uid do chamador (uid via `GetConnectionUnixUser`/polkit); sem `jobId`, retorna todas as operações do uid, com cap 1 MiB, rate limit (10/min), auditoria e retorno `{lines, truncated}` redigido. O journal continua para auditoria. | P1 |
| FR-LOG-005 | O job de setup (ação, id, linhas, erro) é persistido no log do daemon e aparece em `podstack system logs`/GUI/report, sobrevivendo ao restart do daemon. | P0 |
| FR-LOG-006 | `podstack system logs [--follow\|--since\|--level\|--component\|--json\|--export\|--clear]` e uma página/diálogo de logs na GUI (abrir pasta, filtrar, exportar) acessam os 7 dias com contrato estável: `--since` aceita `7d`/ISO 8601, `--level` usa o enum, `--component` aceita `daemon\|gui\|cli\|qml\|setup\|update\|helper\|package` (`helper`/`package` via `GetLogs`, com polkit; `package` = lotes de update do uid), `--export` canonicaliza o destino, recusa symlink/path fora do destino e grava `0600`, `--json` usa `{api:1, lines:[{ts,level,component,message}], truncated, files}` e erro `{api:1, error:{code,message,detail}}`; filtros inválidos retornam 2. O `--clear` apaga só logs do usuário (backend/limpeza em T-LOG-02; superfície CLI/GUI em T-LOG-03); logs de container continuam no subcomando `logs` existente; setup/update expõem “Ver log” no erro. | P0 |
| FR-LOG-007 | Redaction **incondicional** (`NFR-SEC-004`: tokens, Authorization, senhas, URLs com credenciais, env sensível) em todos os sinks, nos logs root e no report; `diagnostics.redact` não desliga a redaction desses caminhos (fica deprecated para eles). A unidade transiente **não usa redirecionamento `append` do systemd**: o wrapper root-owned `podstack-update-exec` filtra stdout/stderr pela `Redaction` compartilhada, grava JSONL em `/var/log/podstack/jobs/<uid>/<jobId>.jsonl` (`0640`) e espelha no journal; os scriptlets recebem `PODSTACK_UPDATE_JOB_ID`/`PODSTACK_UPDATE_UID` do wrapper e emitem mensagens estruturadas via `logger` com as chaves `JOB_ID`/`UID` (sem despejar saída crua). Nenhum segredo em log. | P0 |
| FR-LOG-008 | O `system.report --include-logs` inclui os arquivos rotacionados do usuário e os logs do helper/update obtidos via `GetLogs` escopado, respeitando redaction, 0600/0640 e os 7 dias, com caps explícitos (até 10.000 linhas por componente e 5 MB no total); `diagnostics.includeLogs` passa a default **false** (report é opt-in) e nada é enviado. | P1 |
| FR-LOG-009 | O usuário pode apagar os próprios logs (`podstack system logs --clear`/ação na GUI com confirmação) e desligar debug sem restart; `GetLogs` só expõe operações do próprio uid e os logs root **nunca são lidos diretamente do filesystem pelo app**, somente via `GetLogs` escopado. Logs de sistema/root fora das operações do uid não são expostos. | P1 |

## 5. Requisitos não funcionais

| ID | Requisito |
|---|---|
| NFR-UPD-001 | Update com o app aberto não perde dados de usuário; `UiState` é salvo por handler de SIGTERM/SIGINT na GUI (hoje só `closeEvent`); containers e machines continuam rodando. |
| NFR-UPD-002 | Encerramento com SIGTERM e carência de 3 s; SIGKILL após; sem órfãos; `pgrep` limpo; socket activation não recria binário antigo. |
| NFR-UPD-003 | Instalação verificada dentro do helper com a chave de release embutida/root-owned; falha de assinatura, SHA-256 do artefato divergente do manifesto, versão ou identidade do pacote não executa o gerenciador de pacotes. |
| NFR-UPD-004 | Nenhum segredo no argv/log; o polkit autentica e o helper obtém o uid por `GetConnectionUnixUser`; o helper valida manifesto, staging, dono e identidade do pacote; unidade transiente de sistema fora do cgroup da sessão. |
| NFR-LOG-001 | Logging não bloqueia a UI (> 16 ms): `qInstallMessageHandler` apenas enfileira em writer thread com fila limitada (8192), drop-oldest com contador `dropped`; `O_APPEND`, flush a cada 1 s e em warn/error+, flush final no encerramento; perda em crash limitada a ~1 s de linhas info/debug. |
| NFR-LOG-002 | 7 dias por padrão; limites `advanced.logMaxBytes` (10 MB) e teto total 200 MB; 0600/0700 no usuário e 0640/0750 em `/var/log/podstack/jobs/<uid>/`; redaction incondicional; rotação atômica sob `flock` e arquivo por processo. |
| NFR-LOG-003 | Custo de debug mensurável; o volume de debug não derruba o app; retenção e concorrência (daemon+CLI) verificadas; `kill -9` não corrompe arquivos já descarregados. |

## 6. Casos de uso

**UC-1 — `apt` com o app aberto (incidente 0.1.3).** O usuário instala o `.deb` novo pelo terminal.
O `preinst` para o socket/service de todos os usuários logados, encerra a GUI/daemon e para o helper
antigos; o `postinst` reinicia o helper e reabilita o socket; o usuário abre a GUI nova. O OVMF passa
a ser reconhecido, sem symlinks e sem restart manual, e o socket não reativa o daemon antigo.

**UC-2 — GUI “Atualizar”.** O usuário vê a versão nova, as notas e o consentimento; o app baixa e
verifica o pacote; a GUI grava o marcador `gui-was-open` e inicia o runner, depois sai. O runner
para o daemon, chama o helper (`StageUpdate`/`InstallUpdate`, com polkit) e faz polling de
`GetUpdateStatus`; a unidade transiente instala, o `postinst` reinicia o helper sem matá-la, e o
progresso pós-saída é notificação nativa + `state.json`. Ao final, o runner reinicia o
socket/daemon e reabre a GUI porque o marcador existe; ao reabrir, a GUI mostra o resultado.

**UC-3 — CLI `podstack update --install`.** O usuário no terminal confirma (`--yes`); o polkit pede a
senha; o terminal mostra progresso e logs; no fim, `podstack version` e o daemon novos; exit 0 (ou
erro estável 1/2/6/8/9, com `--json` quando pedido).

**UC-4 — Falha no update.** O manifesto, o download, o helper ou o gerenciador falha (rede,
assinatura, lock, espaço, unpack, `postinst`); o helper é mantido ou restaurado ativo, o runner
religa a GUI/daemon da versão instalada, mostra o erro e aponta o log; nada fica parado. Na próxima
execução de `podstack update`/GUI, o `state.json` permite retomar ou limpar; o daemon não instala.

**UC-5 — Suporte ao OVMF.** O setup falha; o usuário abre “Ver log”/`podstack system logs` e vê o
erro do helper e o check; o `podstack report --include-logs` gera o relatório redigido com o job de
setup e os logs do daemon/helper/update dos últimos 7 dias. Logs root só entram via `GetLogs`
escopado ao uid.

**UC-6 — Debug ligado.** O usuário ativa `advanced.logLevel=debug` na GUI; as linhas de RPC, Podman,
setup e update passam a ser gravadas; após 7 dias, os arquivos mais antigos são removidos; o disco
não cresce sem limite e a UI não bloqueia.

## 7. Critérios de aceite

| ID | Dado | Quando | Então |
|---|---|---|---|
| AC-1 | Ubuntu 24.04, GUI+daemon+helper 0.1.3 abertos e socket ativo | `sudo apt install ./podstack_0.1.4_amd64.deb` | `preinst` para socket/service de todos os usuários logados e encerra GUI/daemon/helper; `postinst` reinicia o helper; PIDs antigos ausentes e socket não reativa daemon antigo; GUI nova abre; `podstack version`/daemon 0.1.4; OVMF `ok`; containers e machines intactos |
| AC-2 | 0.1.4 com app aberto | GUI “Atualizar” para 0.1.5 | download/verificação; GUI grava `gui-was-open` e fecha; helper `StageUpdate`/`InstallUpdate`; unidade transiente instala; restart do helper no meio do `apt` não mata a unidade, e o runner faz polling de `GetUpdateStatus`; GUI/daemon novos; log do update; sem processo antigo |
| AC-3 | mesmo cenário | falha simulada no download, assinatura, unpack, `postinst` ou após `%pre` no RPM | runner relata erro; helper restaurado/ativo; GUI/daemon da versão instalada voltam; `%posttrans` restaura helper/socket; log mostra a causa; nenhum processo órfão |
| AC-4 | app usado com setup/update/erro | `podstack system logs --json`/GUI/report | logs do daemon/GUI/setup/update/helper/package do próprio uid; schema `{api:1, lines, truncated, files}`; uid A não vê uid B; 7 dias; 0600/0640; redaction mesmo com `diagnostics.redact=false`; “Ver log” no erro |
| AC-5 | 0.1.4→0.1.5 | 8º dia e limite por arquivo atingido | o arquivo mais antigo foi removido; os 7 dias restantes existem; rotação `<processo>-YYYYMMDD-N.jsonl` e timer da meia-noite local respeitados |
| AC-6 | manifesto com assinatura/SHA inválidos, pacote de nome/versão/arch errado ou `updates.publicKey` customizada ativa | `update --install` | nenhum gerenciador de pacotes é executado; assinatura checada com a chave de release, não a setting; erro estável (custom key = `PODSTACK_UPDATE_UNSUPPORTED`/`reason=custom_key`); log registra a recusa |
| AC-7 | app aberto com containers e machine rodando | update assistido | containers/machines continuam; QEMU/virtiofsd vivos no scope próprio e mounts virtiofs intactos; só daemon/GUI/helper são reiniciados |

## 8. Fora de escopo

Rollback automático de versão; update delta/binário; update do Flatpak (o app detecta e oferece o
caminho manual); auto-update silencioso; notificação simultânea a múltiplos usuários (o lock global
serializa); logs remotos/telemetria; rotina de suporte online; resolver o `XDG_RUNTIME_DIR` de
sessões Tuxee/SSH (o runner documenta o caminho manual).

## 9. Decisões abertas

| ID | Decisão | Recomendação |
|---|---|---|
| D1 | O update assistido (GUI/CLI) entra no escopo ou só os scripts do pacote? | Ambos: scripts resolvem o `apt` manual (incidente 0.1.3); o runner entrega o update automático a partir da 0.1.4. |
| D2 | Mecanismo privilegiado: helper allowlisted (`StageUpdate`/`InstallUpdate`) + unidade transiente de sistema vs segunda fronteira root dedicada? | Helper + `systemd-run` de sistema: uma única fronteira root; assinatura validada com chave de release embutida/root-owned; staging root-owned; lock global mantido pela unidade transiente. Segunda fronteira root dedicada é rejeitada pela constituição/`security.md`. |
| D3 | Política do `preinst`: SIGTERM + carência + SIGKILL em todos os usuários? | Sim; parar socket/service de todos os logados, máscara runtime para impedir socket activation, handler de SIGTERM na GUI salva `UiState`; fallback `pkill -x podstackd` sem systemd e checagem `is-active` do helper antes de seguir; machines em scope próprio não param. |
| D4 | Formato/local dos logs: JSON Lines em `~/.local/state/podstack/logs` + `/var/log/podstack`? | Sim; 0600/0700 no usuário e 0640/0750 em `/var/log/podstack/jobs/<uid>/`; 7 dias; limite por arquivo e teto total; helper/update/package em `/var/log/podstack`. |
| D5 | Debug default e ajuste: `advanced.logLevel` (info) + GUI/`PODSTACK_LOG_LEVEL`? | Sim; a chave passa a ser exposta na GUI; aplicação em runtime (`restartRequired: false`); debug opt-in; 7 dias. |
| D6 | Auto-reabrir a GUI no `apt` manual? | Não é possível de forma confiável pelo `postinst` (root/sessão); o usuário abre. No update assistido, a GUI grava `gui-was-open` antes de iniciar o runner e o runner reabre por esse marcador, apagando-o depois. |
| D7 | Rollback de pacote? | Não automático no v1; a versão anterior pode ser reinstalada manualmente; o runner religa a versão que estiver instalada. |
| D8 | Acesso aos logs do helper: journal + `GetLogs` ou só journal? | `GetLogs {jobId?}` via D-Bus, escopado ao uid do chamador (`GetConnectionUnixUser`/polkit, `auth_admin`, 1 MiB, rate limit, auditoria); logs de sistema/root fora das operações do uid não são expostos; o journal continua para auditoria. |
| D9 | Como machines sobrevivem ao update? | QEMU/virtiofsd em **scope próprio** (`systemd-run --user --scope`, preferencial); `KillMode=process` só evita que sejam mortos, não os move de cgroup; teste com machine rodando; reconciliação de órfãos no restart. |
| D10 | Logging síncrono ou assíncrono? | Assíncrono com writer thread, fila limitada, arquivo por processo, `flock` e rotação por rename; nunca I/O no thread da UI. |
| D11 | Onde ficam o lock e o estado do job? | Lock na unidade transiente (`flock /run/podstack/update.lock`), estado do job em `/run/podstack/update/<uuid>/state.json` escrito pelo wrapper; o helper só recusa unidade ativa e reconstrói o estado no start. |
| D12 | Qual chave verifica o install assistido? | Chave de release embutida no helper ou `/etc/podstack/update.pub` root-owned; `updates.publicKey` serve só para check/notificação e, se customizada, o install é recusado (`PODSTACK_UPDATE_UNSUPPORTED`, `reason=custom_key`). |

## 10. Rastreabilidade

| Requisito | Tarefa |
|---|---|
| FR-UPD-006…007 (pacote/scriptlets) | T-UPD-01, T-UPD-02 |
| FR-UPD-008…010, 012…015 (runner/helper/UI/CLI) | T-UPD-03, T-UPD-04 |
| FR-UPD-011 (bootstrap) | T-UPD-02, T-UPD-04 |
| FR-UPD-016 (dados/containers/machines) | T-UPD-00, T-UPD-02, T-UPD-03 |
| FR-LOG-001…003, 007 (core/retenção) | T-LOG-01 |
| FR-LOG-004…005, 008…009 (helper/setup/report/limpeza) | T-LOG-02 |
| FR-LOG-006 (CLI/GUI) | T-LOG-03 |
| NFR-UPD-001…004 | T-UPD-00…04, T-E2E-01 |
| NFR-LOG-001…003 | T-LOG-01, T-LOG-02, T-E2E-01 |
| Todos os AC | T-E2E-01 |
| Docs/web/ADR | T-DOC-01 |

Referências existentes: `FR-UPD-001` (check assinado), `FR-UPD-002` (canal stable/beta), `T7-06`;
`FR-SYS-009` (logs estruturados + redaction), `T7-08`/`system.report`; `T7-01`/`advanced.logLevel`;
`packaging/deb/postinst`; `%post` do RPM; `src/core/Logging.cpp`; `src/daemon/UpdateChecker.*`;
`src/daemon/SetupManager.cpp`.
