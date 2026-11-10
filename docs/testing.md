# Estratégia de testes — PodStack

> Regra do projeto: **nenhuma tarefa é "pronta" sem evidência de teste/lint**. Este documento define o que testar, como, onde e com qual critério.

---

## 1. Pirâmide

```
        ┌─────────────────────────────┐
        │ E2E (GUI/CLI + Podman real) │  ~10%
        ├─────────────────────────────┤
        │ Integração (Podman/QEMU)    │  ~25%
        ├─────────────────────────────┤
        │ Unitários (core/podman/rpc) │  ~65%
        └─────────────────────────────┘
```

| Nível | Ferramenta | Escopo | Tempo alvo |
|---|---|---|---|
| Unitário | Qt Test (C++) | parsers, modelos, validação, state machines, RPC | < 30 s total |
| Contrato | Qt Test + `MockPodmanServer` | PodmanClient ↔ HTTP/Unix socket | < 60 s |
| Integração | Qt Test + Podman real | containers, imagens, volumes, redes, logs, exec | < 5 min |
| VM | script + pytest-shell | QEMU/cloud-init/SSH/virtiofs | < 15 min |
| E2E GUI | QML Test + acessibilidade | fluxos J1–J3 | < 10 min |
| E2E CLI | golden tests | comandos, JSON, exit codes | < 2 min |
| Performance | benchmark | startup, memória, idle CPU, listas | < 5 min |
| Segurança | testes dedicados | socket, traversal, redaction, assinatura | < 5 min |

---

## 2. Unitários

### 2.1 Core

- Modelos: serialização/desserialização, defaults, igualdade.
- Validação: nomes RFC 1123, portas, CIDR, tamanhos, paths.
- State machines: container/machine/cluster/job (todas as transições + inválidas).
- Erros: mapeamento Libpod → `PodstackError` (tabela de casos).
- Config: merge, migração, validação, campos desconhecidos preservados.

### 2.2 PodmanClient

- Parsing de JSON do Libpod (fixtures reais capturadas).
- Construção de URLs/query (filtros, `all`, `follow`, `tail`).
- Multiplexação de streams (frames 8 bytes) com dados binários/UTF-8.
- Timeouts, cancelamento, retries idempotentes.
- Erros HTTP (400/404/409/500) → códigos estáveis.

### 2.3 RPC

- Framing NDJSON (mensagens parciais, múltiplas por chunk, > 8 MiB rejeitada).
- `SO_PEERCRED` (mock de credenciais).
- Handshake/versionamento.
- Dispatch de métodos, params inválidos, método desconhecido.
- Subscriptions: subscribe/unsubscribe, backpressure, drop.
- Machines: `list` vazio, `get`/lifecycle/cancel inexistentes → `PODSTACK_NOT_FOUND`,
  `create` sem nome/distro ou com CPUs/memória inválidas → `PODSTACK_INVALID_PARAMS`;
  mounts inválidos (fonte relativa/inexistente, destino relativo/com `..`, tag inválida ou
  duplicada, mais de 8) → `PODSTACK_INVALID_PARAMS` antes do job; mounts válidos com distro
  desconhecida iniciam o job e terminam em erro **sem** `PODSTACK_UNSUPPORTED`;
  `isolated:true` (T4-14) é aceito e inicia o job, enquanto `isolated` + mounts ou +
  `forwardSshAgent` → `PODSTACK_INVALID_PARAMS` com remediação antes do job; `logs` inexistente
  → `PODSTACK_NOT_FOUND`, machine semeada devolve as últimas linhas e respeita `tail`,
  `console.log` ausente → `{lines:[]}` e symlink → `PODSTACK_IO_ERROR`; `ssh` inexistente →
  `PODSTACK_NOT_FOUND` e machine semeada com chave/config devolve
  `{host,port,user,identityFile}`; `stats` inexistente → `PODSTACK_NOT_FOUND` e machine
  semeada com SSH falso devolve CPU/memória/rede/disco/df; `stats.subscribe` entrega
  `machines.stats.delta` e aceita `machines.stats.unsubscribe`; `files.list/download/upload`
  inexistente → `PODSTACK_NOT_FOUND`, path remoto inválido na machine semeada →
  `PODSTACK_INVALID_PARAMS`, listagem com SSH falso devolve `{path, items}` (sem caminhos
  internos) e download/upload usam um `scp` falso que registra os argumentos;
  `ports.list` inexistente → `PODSTACK_NOT_FOUND`, `ports.set` sem array/hostPort string ou
  inválido → `PODSTACK_INVALID_PARAMS`, machine semeada ganha a porta e mantém a SSH ao
  enviar `[]`, e alterar a SSH é recusado; `snapshots.*` sem `id`/`name` →
  `PODSTACK_INVALID_PARAMS`, machine inexistente → `PODSTACK_NOT_FOUND`, nome de snapshot
  inválido → `PODSTACK_INVALID_PARAMS` e machine semeada sem disco → `PODSTACK_IO_ERROR`.
- `system.df`: detalhado devolve `sections.images/containers/volumes` com contagens, itens,
  flags (`dangling`, `unused`, `running`, `inUse`) e totais; `detailed:false` mantém
  contagens/totais e omite `items`; `json` bruto permanece intacto (fixture do
  `MockPodmanServer`).

### 2.4 CLI

- Parsing de flags/aliases; help.
- Formatação humana/JSON/quiet.
- Exit codes por classe de erro.
- Modo não interativo (sem TTY).
- Machines: `machine list --json` vazio, `machine info` inexistente (exit 1), `machine create`
  com nome inválido (exit 1, validação síncrona sem download), tamanho de memória/disco
  inválido e `machine --help`/usage; `machine logs` inexistente (exit 1), sem ID (usage) e
  machine semeada com `--tail`/`--json` (one-shot, sem follow). `machine ssh`/`ssh`
  inexistente (exit 1), sem nome (usage 2) e machine semeada com `ssh` falso no `PATH`
  verificando `-i`, `-p`, `IdentitiesOnly=yes`, `StrictHostKeyChecking=accept-new`,
  `user@host` e o comando após `--`. `machine files ls` (texto/`--json`/`--path`), `pull` e
  `push` usam o daemon com SSH/SCP falsos; sem ID/argumentos → usage 2 e machine inexistente
  → exit 1. `machine snapshot ls/create/restore/rm` sem argumentos → usage 2, machine/nome
  inexistente → exit 1, nome inválido → exit 1 e `restore` imprime o aviso de substituição
  do disco no stderr.
- `top --once --interval 1 --json` devolve uma amostra com `host`/`containers`/`totals` e
  `--help` funciona; o modo contínuo é interrompido com Ctrl+C.
- `system df --json` devolve as seções normalizadas; `system df` mostra o resumo e
  `--detailed` lista imagens/containers/volumes; opção desconhecida sai com 2.

### 2.5 Builds multi-plataforma

`PODSTACK_PODMAN_CLI` substitui o binário do Podman usado pelo `ManifestBuilder` (somente no
fluxo com 2+ plataformas); os testes apontam para um script falso que registra o argv, imprime
linhas e simula sucesso, falha (exit 1) ou processo lento para cancelamento. O fluxo de 0/1
plataforma continua no Libpod e não usa o override.

- `test_core::buildStorePlatforms`: round-trip de `platforms` no `BuildRecord`/JSON/SQLite e
  migração idempotente de um banco antigo sem a coluna (`ALTER TABLE ... DEFAULT '[]'`).
- `test_core::manifestBuilderValidation`: aceita `os/arch[/variant]` (inclusive variante),
  rejeita vazio/9 itens/segmentos extras e retorna `PODSTACK_UNSUPPORTED` com remediação
  quando `PODSTACK_PODMAN_CLI` aponta para um binário inexistente.
- `test_rpc::multiPlatformBuild`: `builds.start` com `platforms` cria manifest list pelo CLI
  falso (`manifest rm` + `build --platform ... --manifest ...`), entrega `builds.delta` e
  `builds.done {status:"success"}`; `builds.get` devolve `platforms`; exit 1 → `failed`;
  `builds.cancel` mata o processo lento → `cancelled`; `platform` com vírgula (compatibilidade)
  é normalizado para a lista; lista inválida/9 plataformas → `PODSTACK_INVALID_PARAMS`.
- `test_cli::composeAndBuild`: `build --platform linux/amd64,linux/arm64` envia as duas
  plataformas, o argv do CLI falso contém `--platform`/`--manifest`/tag/contexto e
  `builds ls` mostra a coluna `PLATFORMS`.
- `test_gui::backendM3`: `startBuild` com `platforms` em texto separado por vírgula cria a
  manifest list e o item do histórico expõe `platforms` como `linux/amd64, linux/arm64`.

---

## 3. Integração com Podman

### 3.1 Pré-requisitos

- Podman ≥ 5.8, `podman.socket` ativo, cgroup v2.
- Imagens de teste leves: `alpine:3.20`, `busybox:1.36`, `nginx:alpine`.
- Rede isolada de teste; limpeza por label `dev.podstack.test=1`.

### 3.2 Casos

| Área | Casos |
|---|---|
| Containers | create/start/stop/restart/pause/kill/remove; exit codes; healthcheck; `--rm` |
| Logs | follow, tail, stdout/stderr, timestamps, reconexão com `since` |
| Exec | TTY e não-TTY, resize, Ctrl+C/D, exit code, sessão órfã |
| Stats | CPU/mem/rede, stream, múltiplos containers |
| Imagens | pull, tag, remove, prune, load/save |
| Volumes | create, clone, export/import, prune |
| Prune/system df | `system.df` com fixture: contagens, itens, flags e total recuperável |
| Redes | create, subnet, DNS interno, connect/disconnect |
| Pods | create/start/stop/remove |
| Eventos | sequência de eventos, reconexão |

### 3.3 Isolamento

- Cada teste usa nomes prefixados `pstest-<uuid>`.
- `t.Cleanup`/RAII remove recursos mesmo em falha.
- Nunca tocar em recursos do usuário (filtrar por label).

### 3.4 Setup e coexistência

| Teste | Verificação |
|---|---|
| Detecção | Docker/Podman/QEMU/KVM/virtiofsd/SQLite/Caddy/k3s com fixtures de `--version`/`info` |
| Matriz de estados | Docker presente/ausente × Podman ausente/desatualizado/atualizado → ação correta |
| Coexistência | Docker presente não bloqueia; Podman instalado; Docker preservado |
| Remoção opt-in | só após `confirm: true`; cancelar não altera; sem migração automática |
| Helper | `InstallPackages`/`RemovePackages` allowlisted; sem `Exec`/shell |
| KVM ausente | aviso TCG; machines marcadas como lentas |
| Componentes | Caddy/k3s só com seleção; k3s desmarcado por padrão |
| Validação | obrigatórios vs. opcionais; “abrir mesmo assim” |
| Jobs | progresso/log/cancelamento/retry; job sobrevive ao fechar a janela |

**Cobertura atual (M3 + onboarding + bandeja):**

| Suíte | Caso | O que cobre |
|---|---|---|
| `test_core` | `configSetupDefaults`, `configMergePreservesDefaults` | defaults de `setup.*`/`app.showOnboarding` e merge recursivo |
| `test_rpc` | `setupFlow` | `setup.status/check/validate/complete`, ids dos checks, `PODSTACK_INVALID_PARAMS` e `PODSTACK_CONFIRMATION_REQUIRED` |
| `test_gui` | `backendSetup` | `setupRefresh`, `setupLoaded`, `setupChecks`, `setupSkip` e `setupComplete`; sem os checks obrigatórios no host (CI), valida a recusa e faz `QSKIP` |
| `test_cli` | `setupCommand` | `podstack setup --check`, `--validate` e a exigência de `--yes` |
| `test_cli` | `topCommand` | `podstack top --once --json` (amostra com host/containers/totais) e `--help` |
| `test_cli` | `systemDf` | `podstack system df`, `--detailed` e `--json` sobre o fixture do `system.df` |
| `test_tray` | `containersMenu*`, `status*` | menu Abrir/Containers/Activity/Sair, agrupamento por projeto, overflow, status normal/atenção/erro e defaults de config |
| `test_single_instance` | `secondInstance*` | primária/secundária, comandos `show`/`quit`, socket órfão e permissões 0600 |
| `test_autostart` | `enablesAndDisables` | `.desktop` com `--hidden`, remoção e preservação de arquivo alheio |
| `test_stats` | `parses*` | parsers de `/proc/stat`, `/proc/meminfo`, `/proc/net/dev` e `/proc/diskstats` |
| `test_rpc` | `catalog` | `system.df` detalhado/resumido, contagens, flags, itens e total recuperável (fixture) |
| `test_gui` | `backendCatalog` | `prunePreview` populado via `refreshCatalog`/`refreshDiskUsage` |
| `test_notifications` | `summaryFor*`, `cooldown*` | falha/OOM/unhealthy, eventos normais ignorados e cooldown |
| `test_setup` | `check*`, `status*`, `validate*`, `rejectsInvalidActions` | checks read-only, resumo de status e validação de ações inválidas |
| `test_machines` | `registry*`, `download*`, `cache*`, `pathsCacheDir`, `pathsMachinesDir`, `machineMeta*`, `machineStore*`, `seed*`, `idsUuidV7`, `machineManager*`, `qemuRuntimeFallback`, `sshConfigPreserves`, `virtiofsd*`, `machineFiles*` | registry pinado (default/versão inválida/arch), download feliz/404/checksum/cancelamento, reuso e rehash do cache, `cloudImagesDir`/`machinesDir` 0700, `machineDir` com id inválido, `meta.json` round-trip/0600/JSON inválido, CRUD e cascade do índice SQLite no mesmo `podstack.db` do BuildStore, seed ISO NoCloud (entradas/label `cidata`, YAML padrão, senha só como hash `$6$`, user-data verbatim, escape/validação e modo 0600), UUIDv7 (versão/variante/unicidade/ordem), manager com runtime falso (create/validação/ciclo de vida/cancelamento/portas/clone/rename/default), reconciliação por `/proc` (adoção, estado obsoleto e órfãos), fallback de pidfile no start/stop/remove e sincronização idempotente do bloco SSH (preservação, update de porta, `ForwardAgent`, remoção, 0600/0700); mounts virtiofs (fonte canônica, tags, sockets, start/stop/remove e cleanup em falha) e `virtiofsd` real opt-in por disponibilidade; files (parsing de `ls long-iso`, quoting do path, validação local/remota e argumentos de `ssh`/`scp`) |
| `test_core` | `setupActionsMapPodmanStatus` | `missing` → `install-podman`; `warn` → `enable-socket`; `ok` → sem ação |
| `test_core` | `setupPolicyMandatoryChecks` | obrigatórios (Podman, systemd-user, SQLite, QEMU/OVMF/virtiofsd) bloqueiam; KVM/Docker/Caddy/k3s não |
| `test_setup` | `firewall*` | detecção firewalld/ufw, abertura/fechamento runtime (sem `--permanent`), validação 1..65535/tcp-udp/≤32, rollback em falha parcial e ausência de backend → `PODSTACK_HELPER_REQUIRED` |
| `test_rpc` | `exposeLan` | `containers.create` sem `exposeLan` envia `host_ip=127.0.0.1`; com `exposeLan:true` sem helper → `PODSTACK_HELPER_REQUIRED` e **nenhum** create; `containers.exposeLan` em `0.0.0.0` sem helper → `PODSTACK_HELPER_REQUIRED`, loopback → `PODSTACK_UNSUPPORTED` com remediação e id inexistente → `PODSTACK_NOT_FOUND` |
| `test_gui` | `backendExposeLan` | `createContainer` com `exposeLan` e `exposeContainerLan` falham sem helper; create sem LAN usa `127.0.0.1`; configurações não mudam |
| `test_gui` | `backendContainerDetail` | `loadContainerDetail("abc123")` desembrulha `containers.inspect` (Config.Env, Mounts, Networks); id inexistente → `containerDetailError`; `copyContainerInspect` não quebra em suíte guiless; stats dedicados populam CPU/RAM/rede/disco e logs dedicados populam/limpam as linhas; ação inválida → `lastError` |
| `test_gui` | `backendTerminal` | `openTerminal` emite `terminalRequested` com título/id, `podman` e `exec -it <id> /bin/sh`; id vazio → erro; `openMachineSsh` inexistente → erro e nenhuma sessão |
| `test_terminal` | `constructsWindow`, `terminalViewConfiguration`, `externalTerminalCommands`, `externalTerminalDetection`, `containerDetailDialog`, `containerDetailDialogQml`, `machineDetailDialog`, `machineDetailDialogFromList`, `machineDetailDialogQml`, `mainWindowTerminalDock` | `TerminalView` configura QTermWidget (`DarkPastels`, keytab `linux`), barra/título (ocultável) e botão externo; `TerminalWindow` e dock da `MainWindow` reutilizam o componente; `ExternalTerminal::commandFor` cobre xdg-terminal-exec/gnome-terminal/kgx/konsole/xterm/xfce4-terminal/alacritty/kitty/foot/wezterm e a detecção usa um `xdg-terminal-exec` falso no `PATH` (o terminal real nunca é aberto); `ContainerDetailDialog` é um `QWidget` filho (`isWindow()==false`) com card e sem `QTabWidget` nativo, a sessão `TerminalView` nasce oculta no `openContainer`, aparece com a área vinda de `applyTerminalArea`, é preservada ao ocultar e recriada ao reabrir; `MachineDetailDialog` repete o padrão para machines (`QWidget` filho, card, `showTerminalTab`), monta `ssh -i <chave> -p <porta> ... user@127.0.0.1`, posiciona/oculta a sessão por `applyTerminalArea`, recria ao reabrir e não cria sessão sem `sshKeyPath`; `openMachine(id)` é exercitado com um `QLocalServer` falso respondendo `machines.list/get`; o carregamento QML real faz `QSKIP` quando o módulo `PodStack` não está no binário de teste |
| `test_core` | `buildStorePlatforms` | `platforms` no insert/get/list/JSON e migração de banco sem a coluna |
| `test_core` | `manifestBuilderValidation` | `os/arch[/variant]`, limite 1..8, rejeição de formato e `PODSTACK_UNSUPPORTED` sem CLI |
| `test_rpc` | `multiPlatformBuild` | manifest list via CLI falso: sucesso, falha, cancelamento, `platforms` no histórico e validação 1..8/`os/arch[/variant]` |
| `test_cli` | `composeAndBuild` | `build --platform p1,p2` (argv do CLI falso) e coluna `PLATFORMS` em `builds ls` |
| `test_gui` | `backendM3` | `startBuild` com `platforms` separadas por vírgula e `platforms` no item do histórico |
| `test_machines` | `machineStats` | fake de `PODSTACK_SSH_BINARY` imprime os marcadores e dados de `/proc`/`df`: CPU/memória/disco/df na primeira amostra, taxas na segunda, `reset()` zera as taxas, machine parada → `PODSTACK_CONFLICT`, chave ausente/SSH falhando → `PODSTACK_IO_ERROR` e SSH ausente → `PODSTACK_UNSUPPORTED`; valida `-i/-p`, `BatchMode`, `ConnectTimeout` e o comando remoto fixo |
| `test_machines` | `machineFiles` | fakes de `PODSTACK_SSH_BINARY` (saída `ls -la --time-style=long-iso`) e `PODSTACK_SCP_BINARY` (registra argv e cria o destino): parsing de `d`/`-`/symlink, `size`/`modified`/`path`, `.`/`..` ignorados, quoting de `it's` no comando remoto, `-i/-p`/`BatchMode`/`ConnectTimeout`, download/upload com `-r -P` e paths com espaço, rejeição de path vazio/controle, destino relativo/`..`/diretório e origem relativa/ausente, SSH/SCP ausentes → `PODSTACK_UNSUPPORTED`, falha de processo → `PODSTACK_IO_ERROR`; `machineFilesIntegration` faz `QSKIP` sem `PODSTACK_MACHINE_FILES_INTEGRATION` |
| `test_rpc` | `machineFiles` | `machines.files.list/download/upload` inexistente → `PODSTACK_NOT_FOUND`; path com controle → `PODSTACK_INVALID_PARAMS`; machine semeada + SSH falso lista `{path, items}` sem expor `machinesDir`; `scp` falso cria o destino e upload devolve `ok:true`; origem local ausente → `PODSTACK_IO_ERROR` |
| `test_machines` | `machineManagerExportImport` | roundtrip com QEMU fake: `create` → `export` (para, achata o disco, compacta, reinicia) → `import` com novo ID/porta/chave, conteúdo do disco preservado, `meta.json`/SQLite/bloco SSH, conflito de arquivo/nome sem `force`, substituição com `force` e export de machine parada sem start |
| `test_machines` | `machineManagerExportConcurrency` | `qemu-img convert` fake bloqueado: `export` para a machine e `start` concorrente retorna `PODSTACK_CONFLICT`; após liberar o fake, o export conclui e a machine volta a `running` |
| `test_machines` | `machineManagerImportChecksum` | arquivo com `disk.qcow2` reescrito e manifesto intacto → `PODSTACK_IO_ERROR`/checksum; caminho inexistente → `PODSTACK_INVALID_PARAMS`; cancelamento → `PODSTACK_CANCELLED` sem deixar machine |
| `test_machines` | `machineManagerRename` | rename troca nome/`meta.json`/SQLite e o bloco SSH (antigo ausente, novo presente), resolve pelo novo nome e `PODSTACK_NOT_FOUND` pelo antigo, atualiza `machines.default`, recusa nome existente (`PODSTACK_CONFLICT`) e nome inválido (`PODSTACK_INVALID_PARAMS`) |
| `test_machines` | `machineManagerIsolated` | `isolated=true` sem mounts: `meta.isolated`/`isolateNetwork` true, agent false, mounts vazio, só a porta SSH local, `QemuCommand` com `restrict=on` e um único `hostfwd` em `127.0.0.1`; `isolated` + mount e `isolated` + `forwardSshAgent` → `PODSTACK_INVALID_PARAMS` com remediação; `setPorts` com `exposeLan=true` em isolada → `PODSTACK_INVALID_PARAMS` sem stop/start; `clone` e export/import preservam `isolated`/`isolateNetwork` e zeram agent/mounts |
| `test_machines` | `machineManagerClone` | origem running clonada com novo ID/porta/estado `stopped`, `qemu-img convert` registrado com disco próprio e mesmo conteúdo, seed/chave/`.pub` copiados, bloco SSH do clone, origem de volta a `running`, estágios `stopping`/`disk`/`copy`/`metadata`/`starting`/`done` e `start` da cópia |
| `test_machines` | `machineManagerUpdate` | machine running: update de CPUs/memória para `stop`+`start` com metadados em `manager.get`/`meta.json`; disco 1→2 GiB registra `qemu-img resize <disco> <bytes>` e atualiza `diskBytes`; shrink 2→1 GiB retorna `PODSTACK_INVALID_PARAMS` sem parar a machine; machine parada permanece `stopped` e não chama `start`; `cpus=0` e request vazio → `PODSTACK_INVALID_PARAMS`; `forwardSshAgent=true` sem restart grava metadado e `ForwardAgent yes`, `false` remove, e falha de sync (config como diretório) faz rollback do metadado com `PODSTACK_IO_ERROR` |
| `test_machines` | `machineManagerSnapshots` | fake de qemu-img com `info --output=json` parametrizado por `PODSTACK_QEMU_IMG_INFO_JSON` e log em append: parsing de 2 snapshots (nome/tamanho/data em RFC 3339), `snapshot -c/-a/-d <nome> <disco>` com stop+start quando running e sem stop/start quando stopped, nome inválido → `PODSTACK_INVALID_PARAMS`, duplicado → `PODSTACK_CONFLICT`, restore/remove inexistente → `PODSTACK_NOT_FOUND` e disco ausente → `PODSTACK_IO_ERROR` |
| `test_machines` | `machineManagerSsh`, `sshConfigPreserves` | criação com `forwardSshAgent=true` grava `ForwardAgent yes` e `SshInfo.forwardAgent`; bloco idempotente preserva terceiros, atualiza porta, liga/desliga o agent e removal limpa o bloco |
| `test_rpc` | `machineSsh` | `machines.ssh` devolve `forwardAgent` do metadado, sem expor o conteúdo da chave; id/nome inexistente → `PODSTACK_NOT_FOUND` |
| `test_cli` | `machineCreateForwardAgent`, `machineSsh` | `machine create --forward-ssh-agent --isolated` envia `forwardSshAgent:true`, `isolateNetwork:true` e `isolated:true` (daemon fake registra o request); `machine update --forward-agent/--no-forward-agent` persiste e alterna `ForwardAgent` no config; `machine ssh` com o flag inclui `-A` (fake de `ssh`), sem o flag não inclui; flags mutuamente exclusivas e update vazio → uso |
| `test_machines` | `machineDefaultConfig` | get/set da default por nome e por ID, inexistente → `PODSTACK_NOT_FOUND`, remoção da machine limpa `machines.default` |
| `test_machines` | `qemuRuntimeFallback`, `machineManagerReconcile`, `machineManagerStartFallback`, `machineManagerRemoveOrphan` | processos fake destacados com `podstack:<id>` no argv e **sem pidfile**: `findPid`/`isRunning`/`pid`/`stop` usam `/proc/<pid>/cmdline` e o processo some; prefixo `notpodstack:` não é reconhecido; reconcile adota `starting`/`stopped` com processo como `running` (preenche `lastBootAt`), marca `running` sem processo como `stopped`, encerra órfão sem meta dentro do `machinesRoot`, ignora processo de outra instalação e é idempotente; `start` adota o processo sem lançar QEMU duplicado; `remove` sem pidfile encerra o processo, apaga diretório/store e mantém o bloco SSH fora do caminho |
| `test_rpc` | `machineStats` | `machines.stats` com machine semeada (chave + fake SSH) devolve campos, inexistente → `PODSTACK_NOT_FOUND`; `machines.stats.subscribe` entrega `machines.stats.delta` com `subscriptionId`/`id` e `unsubscribe` cancela |
| `test_rpc` | `machineConsole` | `machines.console.read` devolve o tail por bytes (default e `tailBytes`), arquivo ausente → `data:""`, inexistente → `PODSTACK_NOT_FOUND` e symlink → `PODSTACK_IO_ERROR`; `machines.console` entrega o tail inicial com `reset:true`, anexa bytes (`reset:false`), detecta truncate/reaparecimento (`reset:true`), encerra em symlink e aceita `machines.console.unsubscribe` |
| `test_cli` | `machineCommands` (console) | `machine console --no-follow` imprime o conteúdo exato, `--tail` respeita bytes, `--json` devolve `{id, data}`, `--json` sem `--no-follow` → usage 2, machine inexistente → exit 1 e o follow imprime o tail inicial + bytes anexados até o processo ser encerrado |
| `test_gui` | `backendMachineConsole` | `followMachineConsole` popula `machineConsoleText` com o tail, anexa deltas, `stopMachineConsole` limpa o buffer/estado e para o polling; machine inexistente → `machineConsoleError` sem tocar `lastError` |
| `test_gui` | `backendMachineStats` | `startMachineStats` popula `machineStats` e os quatro históricos via fake SSH do daemon; `stopMachineStats` encerra o stream e limpa o erro; erro de stream de machine não vaza para `lastError` (inspector de containers) |
| `test_gui` | `backendMachineFiles` | `refreshMachineFiles` sem machine/id inexistente, download/upload inexistente → `machineFilesError` (sem tocar `lastError`); machine semeada + SSH falso popula `machineFiles`/`machineFilesPath`, download grava em `~/Downloads` isolado pelo `user-dirs.dirs` do teste, upload envia e atualiza a listagem, nome com `..` é rejeitado antes do RPC |

| `test_core` | `configSchemaCoversDocumentedKeys`, `configValidation`, `configResetPreservesSetup` | schema com 82 chaves únicas (defaults iguais a `Config::get`, `network.proxy` secret, `engine.mode` restart), validação de tipo/enum/faixa/CIDR/proxy/lista/timestamp e reset por chave + reset total preservando `schemaVersion`/`setup.*` |
| `test_rpc` | `settingsFlow` | `settings.schema` (82), `settings.get` (todas/uma), `settings.set` bool/string/lista, rejeição de chave/tipo/enum/faixa (`PODSTACK_VALIDATION`) e params ausentes (`PODSTACK_INVALID_PARAMS`), `restartRequired` em `engine.mode`, reset por chave e all preservando `setup.completed` |
| `test_cli` | `configCommands` | `config show/get/set/add/remove/reset/schema`, `--json`, listas com vírgula, erro de faixa/enum, `--yes` obrigatório sem TTY e reset total |
| `test_gui` | `backendTheme`, `backendSettings`, `backendProxy`, `backendTray` | `refreshSettings` popula schema/valores, `setSetting`/`resetSetting` via RPC, erro de validação em `settingsError`, tema/proxy/tray assíncronos e autostart aplicado após o RPC |

O detalhe do container (`ContainerDetail.qml`) é validado por `qmllint`, pelo componente
`ContainerDetailDialog` em `test_terminal` e pelo smoke `guiStartsAndQuits`; o detalhe da
machine (`MachineInspector.qml` e `MachineFilesView.qml`) é validado por `qmllint` e pelo
componente `MachineDetailDialog` em `test_terminal` (com `loadQml=false`); a navegação
visual pelas nove abas e a seleção/cópia de logs são verificadas manualmente (a suíte
`test_gui` é guiless e cobre o Backend/streams). O detalhe é um overlay filho da
`MainWindow` (sem janela flutuante) cujo card hospeda o QML diretamente; o terminal é
uma aba interna cujo placeholder recebe o `TerminalView` por `terminalAreaChanged`, e o
lançamento de uma nova sessão em janela (botão do QML/`ExternalTerminal`) é testado sem
abrir um emulador real (fake `xdg-terminal-exec`).

**Validação manual da bandeja (niri + Noctalia):** fechar a janela mantém o processo e o SNI;
`podstack-gui` (segunda instância) foca a janela; `--quit` encerra; o DBusMenu expõe Containers com
os containers reais; Activity Monitor abre pelo menu/clique do meio; um container com `exit 9`
gera `Notify` com “Container falhou”.

**Pendente de validação em máquina limpa:** instalação real via `podstack-helper` + polkit
(Fedora com e sem Docker), KVM/TCG e QEMU. O fluxo de UI e o contrato RPC já são cobertos.

---

## 4. Machines (QEMU)

| Teste | Verificação |
|---|---|
| Seed ISO (`test_machines`) | label `cidata` no PVD, exatamente `meta-data`/`user-data`/`network-config` na raiz, YAML padrão, `network-config` v2/DHCP, senha só como hash `$6$`, user-data verbatim, escape YAML, entradas inválidas e ISO `0600` |
| Metadados e índice (`test_machines`) | `MachineMeta`: round-trip de todos os campos + portas/mounts, `meta.json` `0600` em diretório `0700` e sem temporário residual; arquivo ausente → `PODSTACK_NOT_FOUND`, JSON inválido → `PODSTACK_IO_ERROR`. `MachineStore`: open em DB temporário, upsert/list/get/getByName/updateState/remove, cascade de portas/mounts, mesmas tabelas no `podstack.db` do `BuildStore` (WAL, conexões independentes) e `machinesDir`/`machineDir` com validação de id |
| QEMU/QMP (`test_machines`) | `QemuCommand`: x86 KVM/TCG, arm64, UEFI, `restrict=on` no isolamento, mounts virtiofs (`-chardev socket,id=vfsN` + `-device vhost-user-fs-pci,chardev=vfsN,tag=...`, limites/tags/socket), port forwarding (`hostfwd` por porta tcp/udp, bind `127.0.0.1`/`0.0.0.0`, SSH única, `restrict=on`) e caminhos/recursos inválidos. `QmpClient` com `MockQmpServer` em `QThread`: handshake, `query-status`, `system_powerdown`, erro QMP, evento intercalado, timeout, `quit` com evento `SHUTDOWN`/fechamento e socket inexistente |
| Snapshots (`test_machines`, `test_rpc`, `test_cli`) | `MachineManager` com `qemu-img` fake (`info` lê `PODSTACK_QEMU_IMG_INFO_JSON`; `snapshot` registra argv): parsing de 2 snapshots (id/nome/`vm-state-size`/data), `snapshot -c/-a/-d <nome> <disco>`, stop+start quando running e nenhum stop/start quando stopped, nome inválido/duplicado/inexistente e disco ausente. RPC/CLI cobrem dispatch, `INVALID_PARAMS`/`NOT_FOUND` e o aviso do restore |
| Runtime/manager (`test_machines`) | `MachineManager` com `FakeQemuRuntime`, `qemu-img` e `ssh-keygen` falsos: `validate` público e create baixa a imagem do mock, cria chave ed25519 (0600) + pública (0644), disco/seed/OVMF vars com 0600 em diretório 0700, injeta a pública no seed, preenche `sshKeyPath`, grava `meta.json`/SQLite e bloco SSH, escolhe porta livre, registra `-b` no qemu-img e inicia o runtime. Validação de nome/cpus/memória/disco/arch/distro e conflito de nome; start/stop/restart/remove com estados e erros refletidos no store e no `meta.json`; `sshInfo` por ID/nome e remoção do bloco; cancelamento durante o download sem diretório/DB; portas ocupadas puladas. Port forwarding: `setPorts` preserva a SSH, gera IDs, aplica tcp/udp com bind local/LAN, conflita hostPort por protocolo entre machines, recusa porta/protocolo/SSH inválidos sem parar a machine, para/reinicia quando running, não inicia quando stopped e atualiza `meta.json`/SQLite. Teste com QEMU/KVM real é opcional (`PODSTACK_QEMU_INTEGRATION=1`), monta um virtiofs real e faz `QSKIP` sem binário, `/dev/kvm`, OVMF ou virtiofsd |
| Virtiofs (`test_machines`, `test_rpc`) | `VirtiofsdProcess` com fake via `Options.virtiofsdBinary`/`PODSTACK_VIRTIOFSD_BINARY`: argv (`--shared-dir`, `--socket-path`, `--tag`, `--cache=auto`, `--xattr`, `--sandbox`, `--readonly`), socket criado e processo vivo, stop SIGTERM/remoção do socket e falha de tag simulada. `MachineManager` grava mounts canônicos/tags no meta, inicia um processo por mount e passa os sockets ao `QemuSpec`; stop/start/remove encerram e recriam; falha no segundo mount encerra o primeiro, limpa sockets e não inicia QEMU. `machines.create` valida mounts inválidos de forma síncrona. Integração com `/usr/libexec/virtiofsd` real faz `QSKIP` quando o binário não está disponível |
| RPC/CLI de machines (`test_rpc`, `test_cli`) | `machines.list/get/create/cancel/start/stop/restart/delete/logs/console/console.read/console.unsubscribe/ssh/stats/ports.list/ports.set` com erros estáveis; `machine list/info/logs/console/create/start/stop/restart/delete/ssh/port ls/add/rm/--help`. `logs` cobre `tail`, arquivo ausente, symlink e machine inexistente; `console` cobre tail por bytes, `--no-follow`, `--json`, symlink e follow com reset; `ssh` cobre machine inexistente, campos retornados e argumentos montados pelo CLI com um `ssh` falso; `stats`/`stats.subscribe` usam um SSH falso no daemon e validam os campos, o delta e o unsubscribe; `files`/`pull`/`push` usam SSH/SCP falsos (listagem JSON/texto, download com destino absoluto e upload local); `ports` cobre tipo inválido, porta inválida, preservação da SSH, adição/remoção por hostPort e recusa da SSH. A criação real (download + QEMU + cloud-init) **não** roda no CI: é manual ou integração opt-in, pois exige rede, QEMU/KVM e OVMF |
| Arquivos de machines (`test_machines`, `test_rpc`, `test_cli`, `test_gui`) | `MachineFiles` lista via `ssh "ls -la --time-style=long-iso -- '<path>'"` (path escapado, sem shell local), baixa/envia com `scp -r -P/-i ...` e valida paths locais/remotos; `machines.files.*` resolve ID/nome e não expõe caminhos internos; o CLI imprime tabela/JSON e `pull`/`push`; a GUI usa `refreshMachineFiles`/`downloadMachineFile`/`uploadMachineFile` em worker com `machineFilesBusy`/`machineFilesError` próprios e destino `~/Downloads`. SSH/SCP reais são integração opt-in (`QSKIP`) |
| Métricas de machines (`test_machines`, `test_gui`) | `MachineStatsCollector` com `PODSTACK_SSH_BINARY` falso: parsing de `/proc/stat`, `/proc/meminfo`, `/proc/net/dev`, `/proc/diskstats` e `df -B1 -P /`; CPU 33,3% e taxas de rede/disco na segunda amostra; `reset()` zera o histórico; validação de machine `running`/chave; erros de SSH/timeout/SSH ausente. Na GUI, `startMachineStats` popula os cards/históricos e `stopMachineStats` cancela a subscription. O SSH real é manual |
| GUI de machines (`test_gui`) | `machines` vazio + sinal, `loadMachineInfo`/`loadMachineLogs` inexistente → erro, `followMachineConsole`/`stopMachineConsole` populam/limpam o buffer serial e param o polling, `machineLifecycle`/`removeMachine`/`openMachineSsh` inexistente → `lastError`/`actionFinished(false)`, `createMachine({})` falha na validação síncrona sem QEMU, e machine semeada no `MachineStore` lista/info/logs com `tail`. `openMachineSsh` da machine semeada emite `terminalRequested` com `ssh -i <chave> -p <porta> user@host` (dock/abas cobertos sem QML em `test_terminal`); o SSH real é manual |
| Criar Ubuntu 24.04 | SSH responde ≤ 60 s; hostname/user corretos |
| cloud-init | arquivo criado, pacote instalado, comando executado |
| Stop/start | estado consistente; sem QEMU órfão |
| Delete | disco/seed/chave removidos; SSH config limpo |
| Export/import | checksum; máquina restaurada funcional |
| virtiofs | arquivo do host visível/gravável no guest |
| Port forwarding | porta do guest acessível em `localhost` |
| Isolado | sem acesso ao host/rede (quando aplicável) |
| KVM ausente | modo TCG ou erro claro (configurável) |

**Infra:** runner com `/dev/kvm`; imagens cloud em cache; timeout 10 min.

---

## 5. Kubernetes

| Teste | Verificação |
|---|---|
| Enable | cluster Ready ≤ 90 s |
| kubeconfig | `kubectl get nodes` funciona |
| Pods/Services | watch reflete criação/remoção |
| Logs | logs por container |
| Port-forward | porta local acessa o pod |
| Disable | cluster e kubeconfig removidos |

---

## 6. Domínios e proxy

| Teste | Verificação |
|---|---|
| `*.localhost` | resolve; proxy retorna 200 |
| Detecção de porta | escolhe porta correta; cache |
| 404 | host desconhecido |
| LAN toggle | acesso externo sim/não |
| HTTPS | certificado válido; CA confiável |
| DNS `podstack.local` | install/remove idempotente; resolved recarrega |
| Path traversal | rejeitado no proxy/files |
| Proxy `none`/`auto`/URL | validação de esquema/host; `auto` copia só chaves existentes; URL define HTTP/HTTPS/ALL |
| `NO_PROXY` | defaults + exclusões + existente, sem duplicatas; minúsculas iguais |
| Container novo | `containers.create`/`duplicate` aplicam proxy sem sobrescrever env explícito (`test_rpc/proxyEnvironment`) |
| Machine nova | `write_files` em `/etc/environment`; `user-data` próprio não é mesclado (`test_machines/seedEnvironment`) |
| GUI de proxy | salvar persiste `network.proxy`/`proxyExclude`; valor inválido não altera (`test_gui/backendProxy`) |

---

## 7. E2E (GUI)

- QML Test para componentes (estados, validação, foco).
- Diálogos de prune de Imagens/Volumes: `refreshDiskUsage()` ao abrir popula
  `Backend.prunePreview` a partir de `system.df`; a lista mostra nome, tamanho, situação e
  total recuperável, e fica vazia com mensagem quando não há itens (`test_gui/backendCatalog`).
- Testes de fluxo com `qmltestrunner` + daemon real:
  1. abrir app → listar containers;
  2. criar container → start → logs → terminal → stop → remove;
  3. criar máquina → SSH → push/pull → delete;
  4. abrir setup → detectar → instalar Podman mantendo Docker → validar → abrir app;
  5. habilitar K8s → ver pods/services.
- Screenshots de referência (golden) para regressão visual leve (não pixel-perfect).
- Acessibilidade: navegação por teclado e labels.

---

## 8. Performance

| Métrica | Método | Meta |
|---|---|---|
| Startup cold | `hyperfine podstack-gui` (com daemon quente) | ≤ 1,0 s |
| Memória idle | RSS após 60 s | ≤ 180 MB |
| CPU idle | `pidstat` 60 s | ≤ 1% |
| Lista 1.000 containers | tempo de scroll/frame | 60 fps |
| Logs 100k linhas | busca | ≤ 200 ms |
| Latência de evento | criação externa → UI | ≤ 1 s |

Benchmarks rodam no CI noturno; regressão > 20% falha o job.

---

## 9. Segurança

| Teste | Verificação |
|---|---|
| Socket | permissões, UID errado rejeitado |
| Helper | métodos allowlisted; sem exec; polkit |
| Firewall LAN | `OpenPorts`/`ClosePorts` só tcp/udp, 1..65535, ≤32; runtime sem `--permanent`; rollback parcial; sem helper → `PODSTACK_HELPER_REQUIRED` |
| Setup | detecção read-only; remoção do Docker exige consentimento; sem shell |
| Redaction | fixtures com tokens/senhas/chaves |
| Path traversal | `..`, symlink, archive malicioso |
| Update | assinatura inválida rejeitada |
| CA/DNS | install/remove idempotente; sem resíduo |
| Rootful | aviso e badge; sem escalada automática |
| SELinux | enforcing sem AVC inesperado |

---

## 10. Matriz de compatibilidade

| Dimensão | Valores |
|---|---|
| Fedora | 43, 44 |
| Sessão | Wayland, X11 |
| Desktop | GNOME, KDE Plasma, niri/Noctalia |
| Podman | 5.8, 6.x |
| Modo | rootless, rootful |
| Arch | x86_64, aarch64 (quando disponível) |
| KVM | presente, ausente (TCG) |

CI cobre o subconjunto P0; matriz completa no release candidate.

---

## 11. Test doubles

### 11.1 `MockPodmanServer`

- Servidor HTTP sobre Unix socket em C++/Qt Test.
- Rotas configuráveis com fixtures JSON.
- Simula streams (logs/events) e hijack (exec).
- Injeção de erros (timeout, 500, conexão fechada).
- Usado em unit/contract tests, sem Podman.

### 11.2 Fixtures

- JSON reais capturados de Podman 5.8/6.x, versionados em `tests/fixtures/`.
- Imagens de teste fixadas por digest.
- Cloud images fixadas por checksum.

**Registry e cache de machines:** `test_machines` usa `MockHttpServer` (QTcpServer em
loopback) e não acessa a rede externa. Os testes de metadados/índice usam banco e arquivos
temporários, sem QEMU. O SSH usa `ssh-keygen` falso via `MachineManager::Options.sshKeygenBinary`
e escreve em um config temporário via `Options.sshConfigPath`. No daemon e no CLI, o teste
define `PODSTACK_SSH_CONFIG` para isolar o `~/.ssh/config` real; o mesmo override pode ser
usado no desenvolvimento. As métricas de machines usam `PODSTACK_SSH_BINARY` apontando para
um script falso que imprime os marcadores `__PODSTACK_*__` e dados sintéticos de `/proc`/`df`,
sem abrir conexão real; o script também registra o argv para validar `-i`, `-p`,
`BatchMode=yes`, `ConnectTimeout=3` e o comando remoto fixo. Os arquivos de machines usam
`PODSTACK_SSH_BINARY`/`PODSTACK_SCP_BINARY` falsos (listagem `ls -la --time-style=long-iso`
e registro do argv de `scp`); `MachineFiles::list` real é opt-in por
`PODSTACK_MACHINE_FILES_INTEGRATION=1` + `PODSTACK_MACHINE_FILES_HOST/USER/KEY/PORT`, com
`QSKIP` quando ausente. Os mounts virtiofs usam
`Options.virtiofsdBinary`/`PODSTACK_VIRTIOFSD_BINARY` apontando para um script falso que
registra o argv, cria o socket Unix e dorme; o QEMU continua no `FakeQemuRuntime`. Rodar
isolado:

```bash
ctest --preset dev -R machines --output-on-failure
```

---

## 12. CI

Workflow: [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) — roda em **toda pull request** e em push para `main`.

| Etapa | Conteúdo | Bloqueia merge |
|---|---|---|
| `ci` (job único) | checkout + deps Fedora 44, build com `coverage`, testes, gcovr | ✅ |
| Cobertura | relatório `src/` (XML/HTML) bloqueante; gate bloqueante em **80%** | ✅ |
| Lint | `clang-format` nas linhas alteradas e `clang-tidy` nos fontes alterados (`scripts/check-format.sh`, `scripts/check-tidy.sh`) | ✅ |
| Artefato | `coverage.xml`, `coverage/`, resumos | — |
| Futuro | E2E GUI/CLI, segurança, performance, RPM | planejado |

- Container: `fedora:44` com Qt 6.11, CMake/Ninja, libcurl e gcovr.
- O teste de integração com Podman roda quando há socket disponível; caso contrário, é `QSKIP` (o contrato e o RPC cobrem o caminho sem engine).
- Cache: ccache e imagens Podman (quando o runner permitir).
- Flaky tests: quarentena com issue e prazo; não podem ficar > 2 semanas.

---

## 13. Cobertura e qualidade

- **M3 (antes da bandeja):** **82,5%** de linhas no total (`src/`); gate do CI em **80%** sobre `src/`. O gcovr roda com `--gcov-ignore-parse-errors` e `--merge-mode-functions=separate` para contornar bugs conhecidos do gcov (negative hits e merge de `main.cpp` de targets diferentes). O CI repete `ctest -R rpc` antes do relatório porque o `.gcda` do `podstackd` é sobrescrito pelo último teste que inicia o daemon; o rpc cobre o maior conjunto de handlers.
- **Bandeja (2026-11-08):** **79,7%** de linhas (`src/`), medido com o mesmo comando do CI (13 suítes + `rpc`). O gate de 80% **não** foi atingido por 0,3 p.p.; os maiores vazios são `src/helper` (0%, exige polkit/root), `SetupManager` jobs privilegiados e caminhos de erro de `SingleInstance`/`Autostart`. O código novo da bandeja está coberto: `Tray.cpp` 96%, `StatsCollector.cpp` 93,5%, `Backend.cpp` 79%, `Notifications.cpp` 76%. Follow-up recomendado: teste de integração do helper e dos jobs de setup para recuperar o gate.
- **Machines/M4 + settings (2026-11-09):** **74,2%** de linhas (`src/`) com as 14 suítes + `rpc`. O M4 adicionou ~3,2 mil linhas e o T7-01 mais ~700; a cobertura global caiu e o gate de 80% ficou temporariamente informativo.
- **M5/T5-01 (2026-11-10):** `test_machines` cobre o provisionamento do cluster com `MachineManager` fake (QEMU/qemu-img/SSH-keygen), `K8sManager::RemoteExec` injetado e um `ssh` fake para o runner real: criação da VM dedicada, reuso idempotente, conflito de nome, `K8sStore`, falha de instalação com VM parada para retry, cancelamento, timeout e `status` refletindo `stopped`/`ready`/erros. `test_rpc/k8sStatusAndValidation` cobre `k8s.status` desabilitado, validação de tipos/faixas de `k8s.enable` e erros de `k8s.cancel`. O fluxo completo foi validado em KVM real (Fedora, 2026-11-10): `k8s.enable` levou **90 s** até `Cluster Ready`, com `sudo k3s kubectl get nodes` retornando `podstack-k8s Ready control-plane v1.37.2+k3s1` e `cloud-init status: done`. Cobertura após a T5-01: **19.148/23.918 (80,1%)** no CI, com o gate de 80%.
- **Pré-M5 (2026-11-10):** **80,1%** de linhas (`src/`, 18.561/23.175), medido com o mesmo comando do CI. Os testes novos cobrem `HelperService`/`SetupManager` (jobs, instalação/remoção e erros), `QemuProcessRuntime` com QEMU fake (start/stop/QMP/órfãos), `MachineArchive` (manifesto, escrita e extração), estados/erros de `MachineManager`, `MachineStore`/`MachineMeta`, `CloudInit`, `VirtiofsdProcess`, `SshConfig`, `MachineStats`, `HttpDownloader`, `MachineFiles`, `BuildStore`, `PodmanClient` (catálogo completo), `UnixHttpClient`, `StreamClient`, `ExternalTerminal`, `Backend` e caminhos inválidos de RPC/CLI. O console serial usa `QStringDecoder` incremental e há teste de caractere UTF-8 dividido entre chunks. Testes QML reais foram habilitados (`test_terminal` carrega `Main.qml`, `ContainerDetail.qml` e `MachineDetail.qml`; antes eram `QSKIP`). O CI tem **gate bloqueante em 80%** (`--fail-under-line 80`) e `clang-format`/`clang-tidy` nos arquivos alterados. A meta de 80% foi atingida; os maiores vazios restantes são `MachineManager`, `cli/main.cpp`, `Daemon.cpp`, `Backend.cpp` e `PodmanClient` (caminhos de erro e operações raras).
- **Meta v1.0:** manter ≥ 80% e buscar ≥ 85% em caminhos críticos; testes QML/terminal dedicados entram no M2.
- Nenhum merge com teste vermelho.
- Bug corrigido → teste de regressão obrigatório.
- Código novo sem teste exige justificativa no PR.

Comandos locais:

```bash
cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage --output-on-failure
mkdir -p coverage
gcovr -r . --filter 'src/' --xml coverage.xml \
  --html-details coverage/index.html --txt-metric line --print-summary
gcovr -r . --filter 'src/' --txt-metric line --print-summary --fail-under-line 80
```

---

## 14. Roteiro manual (release)

### J1 — Projeto Compose

1. `podstack compose up -d` em projeto de exemplo.
2. GUI mostra grupo, serviços e portas.
3. `curl http://web.localhost:8080` responde.
4. Logs por serviço; terminal; stop/down.

### J2 — Máquina

1. `podstack machine create ubuntu:24.04 dev --cpus 2 --memory 4G`.
2. `podstack ssh dev -- uname -a`.
3. `podstack machine push ./file dev:~/`.
4. Reiniciar host; máquina sobe; SSH funciona.
5. Export/import.

### J3 — Kubernetes

1. `podstack kube start`.
2. `podstack kube env`; `kubectl get pods -A`.
3. Criar deployment de teste; ver na GUI; logs; port-forward.
4. `podstack kube disable`.

### J4 — CI

1. Runner sem TTY.
2. `podstack machine create ... --yes`.
3. `podstack machine run ...`.
4. `podstack machine delete ... --force`.
5. Exit codes corretos.

---

## 15. Evidência

Todo PR/tarefa anexa:

- comando executado (`ctest`, `pytest`, etc.);
- resumo (passou/falhou, tempo);
- cobertura quando aplicável;
- screenshot/log para E2E;
- benchmark antes/depois para performance.
