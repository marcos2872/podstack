# Plano técnico — PodStack

> Derivado do [`PRD.md`](PRD.md) e da [`spec.md`](spec.md). Define stack, componentes, contratos, concorrência, persistência, build e riscos. Decisões em [`adr/`](../adr).

---

## 1. Restrições e princípios técnicos

- **Podman é o engine**; não reimplementamos runtime, rede ou storage.
- **Rootless por padrão**; rootful opt-in.
- **GUI, daemon e CLI separados**; estado no daemon, não na GUI.
- **Qt 6 + QML** para UI; **C++20** para core/daemon/CLI.
- **Streams via libcurl**; RPC via JSON-RPC sobre Unix socket.
- **QEMU/KVM direto** para machines; sem libvirt no v1.
- **RPM primeiro**; Flatpak não previsto no v1 (reavaliar sob demanda).

---

## 2. Stack

| Camada | Tecnologia | Versão mínima | Observação |
|---|---|---|---|
| Linguagem | C++ | C++20 | GCC 14+/Clang 18+ |
| UI | Qt Quick / QML + Qt Widgets (embed) | Qt 6.4 | Fedora 43: 6.10.3; F44: 6.11.2; Ubuntu 24.04: 6.4.2 |
| Build | CMake + Ninja | 3.24 / 1.11 | `CMakePresets.json` |
| HTTP/streams | libcurl | 8.x | Unix socket + hijack |
| Updates (Ed25519) | OpenSSL (libcrypto) | 3.0 | verificação do manifesto/minisign; `openssl-devel` no build |
| RPC | JSON-RPC 2.0 | — | `QLocalSocket` + `QJsonDocument` |
| Persistência | SQLite (WAL) | 3.45 | metadados de machines/builds/config |
| VM | QEMU/KVM + `qemu-img` + QMP | QEMU 9+ | `virtiofsd` 1.13+ |
| Kubernetes | k3s | 1.30+ | dentro de VM PodStack |
| Proxy/HTTPS | Caddy | 2.10+ | CA interna, reverse proxy |
| DNS | systemd-resolved + DNS userspace | systemd 257 | `*.localhost` no v1 |
| Terminal | QTermWidget | 2.0.0 vendorizado (DEB) / 2.4+ (RPM) | GPL-2.0-or-later, compatível com GPL-3.0-or-later (ADR-0005/0010) |
| Tray | QSystemTrayIcon (SNI) | Qt 6.4+ | KStatusNotifierItem opcional |
| Empacotamento | RPM + DEB + systemd + polkit | Fedora 43+ / Ubuntu 24.04+ | SELinux no RPM; DEB com qtermwidget vendorizado |

---

## 3. Arquitetura de componentes

```mermaid
flowchart LR
  subgraph UserSpace["Sessão do usuário"]
    GUI["podstack-gui<br/>(Qt Quick)"]
    CLI["podstack / podstackctl<br/>(CLI)"]
    Tray["Tray SNI"]
    D["podstackd<br/>(daemon)"]
    DB[("SQLite<br/>metadata")]
    PROXY["Proxy HTTP/HTTPS<br/>(Caddy)"]
    DNS["DNS userspace<br/>127.0.0.1:5353"]
    VM["QEMU/KVM<br/>machines"]
  end
  subgraph System["Sistema"]
    PODMAN["podman.socket<br/>libpod REST"]
    HELPER["podstack-helper<br/>(root, D-Bus/polkit)"]
    RESOLVED["systemd-resolved"]
    FW["firewalld/ufw"]
  end
  GUI -- JSON-RPC --> D
  CLI -- JSON-RPC --> D
  Tray -- D-Bus --> GUI
  D -- libcurl --> PODMAN
  D --> DB
  D --> PROXY
  D --> DNS
  D -- QMP --> VM
  D -- D-Bus --> HELPER
  HELPER --> RESOLVED
  HELPER --> FW
```

### 3.1 Processos

| Processo | Usuário | Responsabilidade | Ciclo de vida |
|---|---|---|---|
| `podstack-gui` | usuário | UI, tray, notificações, janelas | enquanto o app está aberto |
| `podstackd` | usuário | estado, RPC, streams, proxy, DNS, VMs, jobs | systemd --user, sempre ativo |
| `podstack` | usuário | CLI (cliente RPC) | por invocação |
| `podstack-helper` | root | operações privilegiadas declarativas | systemd system, sob demanda |
| `podstack update --install --runner` | usuário | runner destacado do update assistido | por update, sobrevive à saída da GUI |
| `podstack-update-<uuid>.service` | root | unidade transiente que executa o gerenciador fora do cgroup do helper/sessão | por update, `--collect` ao final |
| `podstack-update-exec` | root | wrapper do job: `flock`, redaction, log e `state.json` | filho da unidade transiente |
| `caddy` | usuário | proxy/HTTPS | filho do daemon |
| `qemu-system-*` | usuário | machines | por máquina |
| `k3s` | usuário (guest) | cluster | por cluster |

---

## 4. Cliente Podman (Libpod)

### 4.1 Descoberta

- Ordem: `PODSTACK_PODMAN_SOCKET` → `$XDG_RUNTIME_DIR/podman/podman.sock` → `/run/podman/podman.sock`.
- `GET /_ping` retorna `Libpod-Api-Version` (ex.: 5.8.9) e `Api-Version` (compat, ex.: 1.44).
- URLs: `http://d/v<libpod-version>/libpod/...`.
- Sem autenticação; autorização pelo modo/dono do socket.

### 4.2 Transporte

- **Requisições simples:** libcurl com `CURLOPT_UNIX_SOCKET_PATH`, `CURLOPT_HTTP_VERSION_1_1`.
- **Streams (logs, stats, events, pull/build NDJSON):** `CURLOPT_WRITEFUNCTION` + `CURLOPT_XFERINFOFUNCTION`; cancelamento via `CURLOPT_XFERINFOFUNCTION` retornando non-zero.
- **Hijack (exec/attach):** `CURLOPT_CONNECT_ONLY=2` + `curl_easy_send/recv`; handshake HTTP manual com `Upgrade: tcp`.
- **Multiplexação:** quando `Tty=false`, frames de 8 bytes (`stream type` + `uint32` big-endian length) em `application/vnd.docker.multiplexed-stream`; desserializar no cliente.
- **Pool:** 1 easy handle por stream; worker thread dedicada; limite de streams simultâneos (ex.: 32) com fila.

### 4.3 Camada de domínio

```
PodmanClient
├── ping() / version()
├── containers(): list/get/create/start/stop/restart/pause/unpause/kill/remove
│   ├── logs(id) -> LogStream
│   ├── stats(ids) -> StatsStream
│   ├── exec(id, cmd) -> ExecSession (hijack)
│   └── archive(id, path)
├── images(): list/pull(stream)/push/tag/untag/remove/prune/load/export
├── volumes(): list/create/remove/prune/export/import
├── networks(): list/create/remove/connect/disconnect
├── pods(): list/create/start/stop/restart/remove
├── events() -> EventStream
├── build(context, opts) -> BuildStream
└── system(): df/info
```

- Erros do Libpod são mapeados para `PodstackError` com código estável.
- Timeouts e retries: idempotentes apenas (GET); mutações sem retry automático.

### 4.4 Compat API

- Usada **apenas** como fallback para ferramentas externas/Compose.
- Nunca para operações do PodStack (evita regressões e falta de recursos).
- Se usada, fixar `Api-Version` descoberta; não hardcode 1.44.

---

## 5. Daemon

### 5.1 Responsabilidades

- Conexão e reconexão com Podman.
- Cache de estado + reconciliação.
- RPC para GUI/CLI.
- Jobs longos (pull, build, export, create machine).
- Streams (logs/stats/events) e fan-out para múltiplos clientes.
- Proxy HTTP/HTTPS e DNS userspace.
- Orquestração de QEMU/KVM e k3s.
- Setup/detecção do ambiente (Podman, Docker, QEMU/KVM, componentes) e jobs de setup.
- Settings, SQLite, logs, diagnóstico.

### 5.2 Estrutura de código

```
src/
├── core/            # modelos, erros, util, config
├── podman/          # cliente libpod + streams
├── daemon/          # main, rpc server, services, jobs, state
├── cli/             # main, comandos, formatters
├── gui/             # main, models, qml/
├── machines/        # qemu, cloud-init, qmp, ssh
├── k8s/             # k3s provider, kubeconfig
├── proxy/           # caddy/dns, domains, port forwarding
├── platform/        # systemd, polkit, xdg, tray
└── tests/
```

### 5.3 Concorrência

- **Thread principal:** event loop Qt (daemon e GUI).
- **RPC:** `QLocalServer` no thread principal; handlers rápidos; jobs em `QThreadPool`.
- **Streams libcurl:** thread dedicada por stream (ou pool com `curl_multi`); entrega via sinais/fila para o thread principal.
- **SQLite:** conexão por thread; WAL; escritas curtas.
- **QEMU/QMP:** thread por máquina para leitura do socket QMP; comandos serializados.
- **Backpressure:** filas limitadas por assinatura; drop de logs antigos com aviso.

### 5.4 Estado

- Fonte de verdade de containers/imagens/volumes/redes: Podman.
- Cache em memória reconciliado por eventos + poll de segurança (60 s).
- SQLite: machines, builds, settings history, jobs, clusters, domínios custom.
- Arquivos: `~/.local/state/podstack/` (runtime), `~/.config/podstack/` (config), `~/.local/share/podstack/` (dados).

---

## 6. GUI (Qt Quick)

### 6.1 Estrutura

- `QGuiApplication` + `QQmlApplicationEngine`.
- `QAbstractListModel` por entidade (containers, images, ...), com roles e `sort/filter` via `QSortFilterProxyModel`.
- `InspectorController` expõe seleção + abas.
- `TerminalView` embute QTermWidget via `QQuickWidget` (ADR-0005).
- Tema: `PodStackTheme` singleton (cores, spacing, tipografia) com dark/light.
- Ícones: conjunto próprio (SVG), nunca assets do OrbStack.

### 6.2 Modelo de dados da UI

- `AppState` (conexão, engine, modo, licença).
- `NavModel` (seções, contadores, seleção).
- `ContainerListModel`, `ImageListModel`, `VolumeListModel`, `NetworkListModel`, `MachineListModel`, `PodListModel`, `ServiceListModel`, `BuildListModel`.
- `LogModel` (ring buffer + filtro), `StatsModel` (séries temporais), `FileModel`.
- `SetupModel` (checks, etapa, progresso, logs do setup).

### 6.3 QML

- `MainWindow.qml`: `SplitView` (Sidebar | List | Inspector), toolbar, status bar.
- `Sidebar.qml`, `ListView.qml`, `Inspector.qml`, `LogsView.qml`, `TerminalView.qml`, `FilesView.qml`, `StatsView.qml`.
- `SettingsWindow.qml`, `CreateContainerDialog.qml`, `CreateMachineDialog.qml`, `Setup/SetupWindow.qml` (+ `SetupRail`, `SetupCheckRow`, `SetupProgress`, `SetupLog`).
- `Theme.qml`, `Components/` (Button, Badge, StatusDot, EmptyState, Toast).
- `qt_add_qml_module` + `qmllint`; sem lógica de negócio em QML.

---

## 7. Machines (QEMU/KVM)

### 7.1 Provisionamento

- Imagens cloud (qcow2) por distro/versão; cache em `~/.cache/podstack/images/`.
- Registry **pinado no código** (ubuntu 24.04, debian 12 e fedora 44, amd64) com URL HTTPS e
  checksum SHA-256/SHA-512 verificados; sem fetch de manifesto em runtime. O cache usa
  `<distro>-<version>-<arch>-<checksum[0..12]>.qcow2` + sidecar `.json`, lock por imagem,
  `.part` e rename atômico. Ubuntu/Debian não publicam tamanho fixo; o sidecar registra o
  tamanho real após o download.
- seed ISO **NoCloud** gerada com **libarchive** (formato ISO9660, label `cidata`, Rock Ridge
  `strict` + Joliet), contendo `meta-data`, `user-data` e `network-config` na raiz. Motivo: não
  depender do `cloud-localds`/`genisoimage` no host; libarchive já é usada pelo projeto e é a
  única dependência adicional. Os arquivos entram como `0600` (preservados pelo Rock Ridge
  `strict`), a ISO final é publicada por rename atômico e fica `0600`.
- QEMU:

```bash
qemu-system-x86_64 \
  -accel kvm -M q35 -cpu host -smp "$CPUS" -m "$MEM" \
  -drive file=disk.qcow2,if=virtio,cache=writeback,discard=unmap \
  -drive file=seed.iso,format=raw,if=virtio,readonly=on \
  -netdev user,id=n0,hostfwd=tcp:127.0.0.1:$SSHPORT-:22 \
  -device virtio-net-pci,netdev=n0 \
  -chardev socket,id=qmp,path=qmp.sock,server=on,wait=off \
  -mon chardev=qmp,mode=control \
  -display none -serial file:console.log
```

- virtiofs (se mounts): um `virtiofsd` por mount com
  `--shared-dir <source> --socket-path <machineDir>/virtiofs-<tag>.sock --tag <tag>
  --cache=auto --xattr` (+ `--readonly`), usando `--sandbox=chroot` como root e
  `--sandbox=namespace` no rootless; socket absoluto dentro do diretório da machine.
  QEMU recebe `-chardev socket,id=vfs<N>,path=...` + `-device
  vhost-user-fs-pci,chardev=vfs<N>,tag=<tag>`. Limite de 8 mounts e tags
  `[a-z0-9-]{1,32}` únicas (default `host<N>`).
- Controle por QMP: `system_powerdown`, `quit`, `query-status`, `human-monitor-command`.

### 7.2 Estado e metadados

- `~/.local/share/podstack/machines/<id>/`: `disk.qcow2`, `seed.iso`, `meta.json`, `console.log`, `qmp.sock`, `id_ed25519`.
- `meta.json`: distro, versão, arch, recursos, portas, mounts, isolamento, criação, último boot.
- SQLite indexa para listagem rápida; `meta.json` é a fonte de verdade recuperável.

### 7.3 Limitações

- Sem `/dev/kvm`: TCG lento (avisar; permitir desabilitar).
- Sem snapshot no v1 (P2).
- Sem console gráfico no v1 (serial/VNC P2).

---

## 8. Kubernetes

- **Decisão:** k3s em VM PodStack (PRD Q2; ADR-0003 para a VM), não kind rootless.
- VM dedicada com 2 CPU/4 GB/20 GB (configurável).
- k3s server sem Traefik? Manter Traefik desabilitado no v1 para evitar conflito de portas; usar Caddy do PodStack para Services? (Decisão pendente; documentar.)
- kubeconfig isolado; contexto `podstack-<cluster>`; `KUBECONFIG` via `podstack kube env`.
- Watch de pods/services via API server (`/api/v1/...?watch=1`) com token do kubeconfig.
- Exposição: NodePort em localhost; LoadBalancer/Ingress via `*.k8s.podstack.local` (P2).

---

## 9. Domínios, proxy e portas

### 9.1 v1 sem privilégio

- `*.localhost` (RFC 6761) → 127.0.0.1/::1.
- Proxy do daemon em `127.0.0.1:8080` (HTTP) e `8443` (HTTPS), mapeando `Host` → container/porta.
- Página de índice em `http://localhost:8080` listando serviços.
- Sem alterar `/etc/hosts`, sem root.

### 9.2 v1.1 com helper

- `*.podstack.local` via DNS userspace 5353 + drop-in no resolved.
- Portas 80/443 via socket activation do helper → socket do daemon.
- Caddy para HTTPS/CA.
- Tudo opt-in e reversível.

### 9.3 Detecção de porta

- Probe concorrente com timeout 300 ms nas portas expostas + comuns.
- Cache por container/config digest; invalidar em eventos.
- Override por label.

---

## 10. Persistência e caminhos

| Caminho | Conteúdo | Permissão |
|---|---|---|
| `~/.config/podstack/config.json` | settings | 0600 |
| `~/.config/podstack/polkit/` | políticas (se aplicável) | 0644 |
| `~/.local/share/podstack/podstack.db` | SQLite (machines, builds, jobs) | 0600 |
| `~/.local/share/podstack/machines/<id>/` | discos, seed, chaves, logs | 0700 |
| `~/.local/share/podstack/kubeconfigs/` | kubeconfigs | 0600 |
| `~/.local/share/podstack/ca/` | CA/chaves | 0600 |
| `~/.cache/podstack/images/` | imagens cloud | 0700 |
| `~/.local/state/podstack/logs/` | logs rotacionados (JSON Lines) | 0700 (arquivos 0600) |
| `~/.local/state/podstack/updates/` | `state.json`/`gui-was-open` do runner | 0700 |
| `~/.cache/podstack/updates/` | manifesto e artefato do update | 0700 (arquivos 0600) |
| `/run/podstack/update/<uuid>/` | staging root-owned e `state.json` do job | 0700/0640 |
| `/var/log/podstack/jobs/<uid>/` | logs root por operação do update | 0750/0640 |
| `~/.local/state/podstack/ui-state.json` | geometria, seção e abas da UI | 0600 |
| `$XDG_RUNTIME_DIR/podstack/` | sockets, PID, locks | 0700 |

---

## 11. Build e CI

- CMake presets: `dev`, `release`, `asan`, `tsan`.
- `-Wall -Wextra -Werror` no CI; `clang-tidy` e `clang-format` check.
- Testes: `ctest` com Qt Test; integração com Podman real; QML Test; golden tests de CLI.
- CI em Fedora 43/44 e Ubuntu 24.04 (containers), rootless; job rootful opcional.
- Cache de ccache; artefatos RPM + AppImage.

---

## 12. Empacotamento

- RPM: `podstack`, `podstack-helper`, `podstack-selinux`.
- systemd user: `podstack.socket`, `podstack.service`; system: `podstack-helper.service`.
- polkit: ações `dev.podstack.helper.*`, incluindo `stage-update`/`install-update` (`auth_admin`) e
  `get-update-status`/`get-logs` (`auth_admin_keep`/`auth_admin`).
- Scriptlets: `preinst`/`postinst`/`postrm`/`prerm` (Debian) e `%pre`/`%preun`/`%post`/`%posttrans`
  (RPM) com parada/religamento de socket/daemon/helper, máscara runtime e trap de limpeza.
- Wrapper `podstack-update-exec` em `/usr/libexec/podstack/`; `tmpfiles.d` e logrotate/timer para
  `/var/log/podstack`; `procps`/`procps-ng` declarados.
- AppStream + `.desktop` + ícones.
- Flatpak: não previsto no v1; reavaliar sob demanda (ADR-0006).

---

## 13. Riscos técnicos e mitigação

| Risco | Mitigação |
|---|---|
| Hijack/exec | PoC no M0 com libcurl; testes com TTY/resize; fallback "abrir terminal externo" |
| virtiofs/SELinux | PoC no M4; fallback 9p; testes em enforcing |
| Rootless sem IP direto | `*.localhost` + portas publicadas; helper opt-in |
| QTermWidget GPL | Resolvido: projeto GPL-3.0-or-later; QTermWidget é compatível (ADR-0005) |
| Compose provider ausente | Detectar e empacotar dependência; fallback com aviso |
| Podman 6.x | Camada de adaptação + testes 5.8/6.x |
| Escopo | Marcos com critérios de saída; P2 fora do v1 |
| SELinux | Política desde M0; testes |
| Update deixar processo antigo ativo | `preinst`/`%pre` com máscara runtime, SIGTERM e trap; `postinst` reinicia o helper |
| Update matar a instalação ao reiniciar o helper | unidade transiente de sistema; lock na unidade; polling de `GetUpdateStatus` |
| Logging bloquear a UI ou crescer sem limite | writer assíncrono com fila limitada, retenção 7 dias, limites por arquivo e teto 200 MB |
| Logs root expostos a outro uid | `GetLogs` escopado por `GetConnectionUnixUser`/polkit; app não lê root diretamente |

---

## 14. Marcos

| Marco | Entrega técnica | Critério de saída |
|---|---|---|
| M0 | Scaffold, CMake, daemon, cliente Podman, CLI `version/ps`, shell QML | build + `podstack ps` + GUI lista containers |
| M1 | Containers completos, logs, exec, stats | E2E de ciclo de vida + terminal |
| M2 | Imagens, volumes, redes, pods | CRUD + integração |
| M3 | Compose, builds, eventos | Projeto sobe/desce pela GUI |
| M4 | Machines QEMU/KVM | create/ssh/export/import |
| M5 | Kubernetes | cluster + kubectl + UI |
| M6 | Domains/HTTPS | `*.localhost` + proxy + CA |
| M7 | Tray, settings, updates, notificações | paridade de desktop |
| M8 | USB/serial/X11/áudio, migração, i18n | P2 |

Detalhamento em [`tasks.md`](tasks.md) e [`roadmap.md`](roadmap.md).

---

## 15. Atualização segura e logs do app

> Decisão: **ADR-0012**. Requisitos: `FR-UPD-006…016`, `FR-LOG-001…009` e `NFR-UPD`/`NFR-LOG`
> do [`PRD.md`](PRD.md). Fluxos em [`spec.md`](spec.md) §15.

### 15.1 Caminhos do update

| Caminho | Mecanismo | Resultado |
|---|---|---|
| Manual `apt`/`dnf` | `preinst`/`%pre` param socket/service/GUI/daemon/helper; `postinst`/`%posttrans` religam | Resolve o incidente 0.1.3; usuário reabre o app |
| Assistido GUI/CLI (0.1.4+) | GUI → runner `setsid` → `StageUpdate`/`InstallUpdate` → unidade transiente | App reabre na versão nova via `gui-was-open` |

- O `preinst` aplica `mask --runtime podstack.socket` por usuário logado, para socket/service, encerra
  GUI (SIGTERM + 3 s + SIGKILL) e daemon (systemd ou `pkill -x podstackd`), confere o helper e aborta
  com trap de limpeza se algo não parar. Não toca no runner nem em containers/machines.
- O `postinst`/`%post` faz `daemon-reload`, habilita o socket, remove a máscara por usuário e
  **reinicia** o helper; não abre GUI/daemon de usuário. `postrm`/`%posttrans` restauram nos abortos.
- O runner grava `state.json` atômico (`QSaveFile`), para o socket/daemon e baixa/valida o manifesto
  e o artefato em cache (`0600`, TTL 6 h, `.part` + rename, 300 MB, timeouts 15/5/30 min).
- O helper é a única fronteira root: `StageUpdate` (staging `/run/podstack/update/<uuid>/` `0700`,
  `O_NOFOLLOW`, dono via `GetConnectionUnixUser`, assinatura com chave de release embutida/root-owned,
  SHA-256 contra o manifesto, identidade `dpkg-deb -f`/`rpm -qp`) e `InstallUpdate` (admissão com
  `flock /run/podstack/update.admission.lock`, recusa de unidade ativa, `systemd-run
  --unit=podstack-update-<uuid> --collect --property=RuntimeMaxSec=1800`).
- O lock global `/run/podstack/update.lock` pertence à unidade transiente, não ao helper; o wrapper
  `podstack-update-exec` executa o gerenciador, filtra stdout/stderr pela `Redaction`, grava
  `/var/log/podstack/jobs/<uid>/<jobId>.jsonl` e `state.json` (`0640`). Não há redirecionamento
  `append` do systemd.
- `GetUpdateStatus` (`auth_admin_keep`) alimenta o polling com backoff durante o restart do helper;
  `UpdateProgress`/`UpdateFinished` são best-effort. O helper reconstrói o estado no start varrendo
  unidades e `state.json`.
- Falha: helper mantido/restaurado, GUI/daemon da versão instalada religados, erro em `state.json` e
  no log; próxima execução oferece retomar/limpar. Sem rollback automático.
- QEMU/virtiofsd ficam em `systemd-run --user --scope`; `KillMode=process` é defesa em profundidade,
  não move processos de cgroup. `updates.mode=auto` só verifica/baixa; Flatpak/sem systemd de usuário
  desabilita o assistido com caminho manual.

### 15.2 Logs

- `src/core/Logging` instala `qInstallMessageHandler` que só enfileira (writer thread, fila 8192,
  drop-oldest + `dropped`), sem I/O no thread da UI; stderr/journal permanece.
- `LogWriter`/`LogRotation`: JSON Lines por processo (`podstackd.jsonl`,
  `podstack-gui-<pid>.jsonl`, `podstack-cli-<pid>.jsonl`), `O_APPEND`, flush 1 s/warn+, rotação
  diária e por tamanho (`<processo>-YYYYMMDD[-N].jsonl`), retenção 7 dias (1–30),
  `advanced.logMaxBytes` 10 MB, teto 200 MB, `flock` e rename atômico; timer da meia-noite local.
- Redaction **antes** de gravar, em todos os sinks e logs root; `advanced.logLevel` em runtime
  (`restartRequired: false`) e `PODSTACK_LOG_LEVEL`; setup jobs persistidos no log do daemon.
- Logs root em `/var/log/podstack/jobs/<uid>/<jobId>.jsonl` (`0750`/`0640`), com `tmpfiles.d`,
  logrotate/timer e SELinux/AppArmor; o app só acessa via `GetLogs {jobId?}` (`auth_admin`, cap
  1 MiB, rate limit, auditoria), escopado ao uid. O legado `podstackd.log` é migrado/removido.
- `podstack system logs`/GUI leem/filtram/exportam/limpam os 7 dias (`--json` `{api:1,...}`,
  `--export` `0600`, filtro inválido 2); `system.report --include-logs` inclui os 7 dias com caps
  de 10.000 linhas/componente e 5 MB, e `diagnostics.includeLogs` default `false`.
