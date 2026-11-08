# OrbStack 2.2.5 — análise de referência (DMG)

> Inventário funcional, de UI e de CLI extraído estaticamente do DMG oficial do **OrbStack 2.2.5 (build 21124)**.
>
> **Correções:** este documento foi produzido a partir do binário e contém inferências. Para a UI confirmada pela documentação oficial (abas reais, sidebar com Devices/Commands, settings e semântica do CLI), leia [`orbstack-2.2.5-official-notes.md`](orbstack-2.2.5-official-notes.md) — em caso de divergência, as notas oficiais prevalecem.
>
> DMG `amd64`, SHA-256 `48f66df42357ba0294ea9c625dda84ac72e10f0d23710d5afcf079bcb6ae865b`, analisado sem executar o binário. Nenhum asset, ícone, som, layout pixel-a-pixel ou código proprietário é reutilizado. "OrbStack" é marca registrada da Orbital Labs; este projeto usa identidade própria (**PodStack**).

---

## 1. Arquitetura observada no OrbStack (para entender o produto)

| Componente | Evidência | Papel |
|---|---|---|
| App macOS | `Contents/MacOS/OrbStack` (36 MB, Swift/AppKit + SwiftUI) | UI, janelas, menu bar, onboarding |
| Helper privilegiado | `OrbStack Helper` (79 MB, Go; `dev.kdrag0n.MacVirt.vmgr`) | Gerencia VMs, rede, Docker engine, K8s |
| CLI | `scli` (16 MB, Go/cobra; exposto como `orb`/`orbctl`) | Automação e scriptable parity |
| Privhelper | `dev.orbstack.OrbStack.privhelper` (SMJobBless/XPC) | Operações privilegiadas (rede, mounts, DNS) |
| VM | kernel Linux `7.2.12-orbstack`, rootfs EROFS, `vinit`, `rpack` (cpio) | "Linux machines" e o Docker engine em VM |
| Terminal | `Ghostty` / `TerminalNSView` | Terminal embutido |
| RPC | `JsonRPCClient`, `SconService`, `ApiClient`, `PHClient`, `DaemonManager`, protobuf | Comunicação UI ↔ daemon/helper |
| Rede | bridge, proxy HTTP/HTTPS/SOCKS, `*.orb.local`, porta interna `8969` | Domains, port forwarding, VPN |
| Extras | Sparkle (updates), Sentry (telemetria), SecureXPC | Atualização, diagnóstico, IPC seguro |
| Ferramentas | `xbin/docker-tools` (docker, compose, buildx), `kubectl` | Compatibilidade Docker/K8s |

**Conclusão de arquitetura:** o OrbStack é um produto **app + daemon + CLI + VM**, não um app monolítico. O PodStack deve seguir o mesmo desenho: `podstackd` (daemon de usuário) + `podstack-gui` (Qt Quick) + `podstack` (CLI), todos conversando por um socket local (JSON-RPC), com Podman nativo no Linux e QEMU/KVM apenas para "machines".

---

## 2. Mapa da UI

### 2.1 Janela principal

Estrutura de três painéis, no estilo app nativo de desktop:

```
┌──────────────────────────────────────────────────────────────────────┐
│ Toolbar: [+ Novo] [Importar] [Abrir em janela] [Pasta]  [Busca] [⚙] │
├───────────────┬──────────────────────────────────┬───────────────────┤
│ Sidebar       │ Lista (tabs + filtro + sort)     │ Inspector         │
│               │                                  │                   │
│ Containers    │  nome  status  imagem  portas    │  Overview         │
│ Images        │  ...                             │  Logs             │
│ Volumes       │                                  │  Terminal         │
│ Networks      │                                  │  Files            │
│ Machines      │                                  │  Stats            │
│ Kubernetes    │                                  │  Inspect          │
│  ├ Pods       │                                  │                   │
│  └ Services   │                                  │                   │
│ Builds        │                                  │                   │
│ Activity Mon. │                                  │                   │
│ Settings      │                                  │                   │
├───────────────┴──────────────────────────────────┴───────────────────┤
│ Status bar: engine, CPU/mem/disk, versão, licença                    │
└──────────────────────────────────────────────────────────────────────┘
```

Evidências: `SidebarView.swift`, `PrincipalView.swift`, `InspectorView.swift`, `NewMainViewController.swift`, `SplitViewController`, `SplitViewAccessor`.

### 2.2 Seções da sidebar e ações

| Seção | Conteúdo | Ações observadas |
|---|---|---|
| **Containers** | lista + abas; status; compose groups | novo, abrir, abrir em nova janela, tabs, filtro, mostrar parados, pasta, ordenar |
| **Images** | lista + abas; tamanho/digest | importar, abrir, abrir em nova janela, tabs, ordenar, copiar digest |
| **Volumes** | lista + abas; uso/tamanho | novo, importar, abrir, abrir em nova janela, tabs, ordenar |
| **Networks** | lista + abas; driver/subnet | novo, tabs, ordenar |
| **Machines** | lista + abas; distro/arch/status | novo, importar, abrir, abrir em nova janela, tabs, ordenar |
| **Kubernetes → Pods** | lista + abas; namespace/status | filtrar, abrir em nova janela, tabs, ligar/desligar |
| **Kubernetes → Services** | lista + abas; tipo/portas | filtrar, abrir em nova janela, tabs |
| **Builds** | histórico de builds; steps/logs | filtrar, apagar registro(s), mostrar step falho |
| **Activity Monitor** | CPU/mem/disco/rede por recurso | parar, frequência de atualização |
| **Commands** | referência/cheatsheet de comandos da CLI | abrir |
| **Devices** | USB/serial: forwarded, passthrough, not shared | attach/detach |
| **Settings** | janela de preferências | ver §2.6 |

Evidências: `actionDockerContainersNew/Open/OpenWindow/Tabs/Filter1`, `actionDockerImagesImport/Open/OpenWindow/Tabs`, `actionDockerVolumesImport/New/Open/OpenWindow/Tabs`, `actionDockerNetworksNew/Tabs`, `actionMachinesImport/New/Open/OpenInNewWindow/Tabs`, `actionK8sPodsFilter1/OpenWindow/Tabs`, `actionK8sServicesFilter1/OpenWindow/Tabs`, `actionK8sToggle`, `actionActivityMonitorStop`, `actionToggleInspector`.

### 2.3 Inspector (painel direito)

Abas **confirmadas** pela documentação oficial (o binário sugeria "Overview"/"Inspect", que não existem como abas):

| Aba | Conteúdo |
|---|---|
| **Info** | nome, imagem, comando, status, uptime, IP, portas, env, mounts, labels, health |
| **Stats** | CPU, memória, rede, disco, processos (histórico) |
| **Logs** | stream, busca, filtro, wrap, pretty JSON, copiar, seguir |
| **Terminal** | shell interativo no container/máquina (Ghostty no original) |
| **Files** | navegador de arquivos do volume/container; abrir, baixar, enviar |

**Inspect** (JSON bruto) não é aba no OrbStack; no PodStack é um diferencial (P1). "Overview" é o nome interno da aba Info.


Views: `LogsView.swift`, `StatsDashboardView.swift`, `FileManagerView.swift`, `Terminal.swift`, `Ghostty.swift`, `InspectorView.swift`. Modelos: `LogsViewModel`, `StatsHistory`, `ProcessStore`.

### 2.4 Janelas e diálogos

| Tela | Evidência | Conteúdo |
|---|---|---|
| Criar máquina | `CreateMachineView.swift` | distro/versão, arch, CPUs, memória, disco, usuário, senha, user-data (cloud-init), mounts, isolamento, SSH agent |
| Clonar máquina | `CloneMachineView.swift` | origem, nome, recursos |
| Importar máquina | `ImportMachineView.swift` | arquivo `.tar.zst`, nome |
| Renomear máquina | `RenameMachineView.swift` | nome novo |
| Onboarding | `OnboardingWelcomeView.swift`, `OnboardingCreateView.swift` | boas-vindas, escolha do que usar (Docker/K8s/máquina), criação inicial |
| Licença/login | `AuthView.swift`, `DrmState` | login, ativação, limites do plano |
| Diagnóstico | `DiagReporterView.swift` | gerar relatório, revisar, enviar |
| Comandos/ajuda | `CommandsRootView.swift`, `HelpButton` | referência de CLI e atalhos |
| Activity Monitor | `ActivityMonitorRootView.swift` | monitor de recursos ao vivo |

Diálogos observados: `Delete container?`, `Delete containers?`, `Delete machines?`, `Delete networks?`, `Delete services?`, `Delete build record?`, `Reset Docker data?`, `Reset Kubernetes cluster?`, `Install Command-Line Tools?`, `Disable privileged features?`, `Allow admin privileges to improve compatibility?`, `Move to Applications?`, `Replace existing data?`, `Use containerd image store?`, `Migrate from Docker Desktop`, `Upgrade Required`.

### 2.5 Menu bar / bandeja

- Item de status com ações rápidas (containers, machines, K8s, engine, updates).
- "Show in menu bar", "Keep running when menu bar app is quit", "Activity Monitor", "Check for Updates".
- No Linux: `KStatusNotifierItem` (KF6) ou `QSystemTrayIcon`; integra com Noctalia/niri via StatusNotifierItem.

### 2.6 Settings (chaves observadas)

| Chave (GUI) | Chave CLI equivalente | Efeito |
|---|---|---|
| `launchAtLogin` | `app.start_at_login` | iniciar no login |
| `appWaitForContainerStop` | `app.wait_for_container_stop` | esperar containers pararem ao sair |
| `dockerExposePortsToLan` | `docker.expose_ports_to_lan` | expor portas de containers na LAN |
| `machinesExposePortsToLan` | `machines.expose_ports_to_lan` | expor portas de máquinas na LAN |
| `machinesForwardPorts` | `machines.forward_ports` | encaminhar portas de máquinas para o host |
| `dockerEnableContainerdImageStore` | `docker.enable_containerd_image_store` (provável) | usar image store do containerd |
| `k8sEnable` | `k8s.enable` | habilitar Kubernetes |
| `k8sExposeServices` | `k8s.expose_services` | expor Services (NodePort/LB/API) |
| `k8sKubeconfigUseDomain` | `k8s.kubeconfig_use_domain` | kubeconfig com domínio |
| `k8sTlsSan` | `k8s.tls_san` | SANs do API server |
| `k8sFilterShowSystemNs` | — | mostrar namespace de sistema |
| `networkHttps` | `network.https` | HTTPS para domínios de containers |
| `networkProxy` | `network.proxy` | proxy HTTP/HTTPS/SOCKS5/SOCKS5H |
| `networkProxyExclude` | — | hosts excluídos do proxy |
| `networkSubnet4` | `network.subnet4` | subnet IPv4 da bridge |
| `sshExposePort` | `ssh.expose_port` | expor SSH na LAN |
| `forwardSSHAgent` | — | encaminhar SSH agent do host |
| `isolateNetwork` | — | isolar rede de máquinas |
| `terminalFont`, `terminalFontBold` | — | fonte do terminal |
| `logsFontSize`, `logsWrapLines`, `logsSearchMode`, `logsInspectorPrettyJSON` | — | preferências de logs |
| `usbAttachHoldDuration` | — | tempo de retenção de USB |
| `menuBar` | — | mostrar na bandeja |
| `updateMode`, `updateFrequency` | — | política de updates |
| `dockerMigrationDismissed`, `onboardingCompleted` | — | estado de onboarding/migração |

Descrições de UI coletadas: "Expose ports to LAN", "Expose services to LAN", "Expose SSH server to LAN", "Forward ports to macOS" (→ host no Linux), "Enable HTTPS for container domains", "Allow other devices on your LAN to connect to forwarded container/machine ports", "Allow the machine to use your host SSH agent", "Pause containers while Mac is sleeping" (→ suspensão do host), "Automatically download updates", "Improve battery life. Only disable if you need to run background services", "Use domains and IPs to connect to containers and machines without port forwarding", "Block access to other machines and host IPs", "Switch Docker & Kubernetes context automatically".

---

## 3. Funcionalidades por domínio

### 3.1 Containers

- Criar/executar com: imagem, comando, nome, portas, env, volumes, workdir, usuário, `--rm`, `--init`, `--read-only`, `--privileged`, recursos (CPU/mem), restart policy, labels, arquitetura.
- Ciclo de vida: start, stop, restart, pause/unpause, kill, remove (força), rename, duplicate/clone.
- Inspeção: logs (follow, busca, wrap, pretty JSON), stats ao vivo e histórico, processes, files, inspect JSON, eventos.
- Terminal interativo (exec) com shell detectado; debug shell (Pro no original) com ferramentas e `dctl`.
- Portas: publicadas, expostas, domínio automático, abrir no navegador.
- Compose: agrupar containers por projeto, start/stop do grupo, logs por serviço, recriar.
- Avançado: checkpoint/restore (CRIU), gVisor (`runsc`), emulação x86 em ARM (Rosetta no Mac; no Linux, QEMU/user-mode ou binfmt), containers "isolated".
- Export/import de container (tar), commit de container em imagem.

### 3.2 Imagens

- Pull/push (registry auth), tag, inspect, history, delete, prune (dangling/all), import tar, export tar, save/load.
- Multi-arch e seleção de plataforma; digest e cópia do digest.
- Ícone/badge por imagem; tamanho; camadas; "open in new window".
- Builds: histórico, steps, logs, cache, filtro, apagar registros, build a partir de Dockerfile/contexto, buildx.

### 3.3 Volumes

- Criar, remover, inspecionar, listar uso/tamanho.
- Clone (cópia rápida), exportar `.tar.zst`, importar `.tar.zst`, importar do host.
- Abrir no gerenciador de arquivos; montar em containers; volume anônimo vs nomeado.

### 3.4 Redes

- Criar/remover networks; driver bridge; subnet IPv4/IPv6; gateway; labels.
- DNS interno; resolução por nome; aliases.
- Exposição de portas na LAN; HTTPS para domínios; proxy global e exclusões.
- Domínios `*.orb.local` (no PodStack: `*.podstack.local`), IP direto, sem port forwarding.
- VPN/proxy corporativo; isolamento de rede de máquinas.

### 3.5 Machines (VMs Linux)

- Criar com distro/versão (`ubuntu`, `debian`, `fedora`, `alpine`, `arch`, `centos`, `rocky`, `alma`, `opensuse`, `oracle`, `kali`, `gentoo`, `void`, `nixos`, `devuan`, etc.).
- Recursos: arch (x86_64/arm64), CPUs, memória, disco (limite mínimo 1 GiB), usuário, senha, cloud-init/user-data, mounts do host, rede isolada, SSH agent forwarding.
- Ciclo de vida: start/stop/restart/delete (all/force), list/info/logs/SSH/run.
- Arquivos: pull/push entre host e máquina; shared folders; file manager.
- Export/import `.tar.zst`; clone; rename; default machine.
- Integrações: SSH config, VS Code Remote, X11 forwarding, áudio, USB/serial passthrough.
- No Linux: QEMU/KVM + cloud-init + virtiofs; sem Rosetta (usar QEMU TCG ou binfmt para cross-arch).

### 3.6 Kubernetes

- Habilitar/desabilitar cluster local leve.
- kubeconfig gerenciado; troca automática de contexto Docker/K8s.
- Views de Pods e Services; logs; delete; port-forward; start/stop de pods.
- Expor Services (NodePort, LoadBalancer, API server); TLS SANs; namespace de sistema.
- Reset do cluster.
- No Linux: k3s em VM dedicada (ADR-0003); kind não é suportado no v1.

### 3.7 Domínios e port forwarding

- Domínio automático por container/serviço (`nome.orb.local` no original).
- HTTPS automático para domínios de containers.
- Forward automático de portas publicadas/expostas.
- Exposição seletiva na LAN.
- Proxy HTTP/HTTPS/SOCKS5/SOCKS5H com lista de exclusão.
- No Linux: `systemd-resolved` + proxy de userspace + helper polkit para :80/:443 e DNS.

### 3.8 SSH e integração com o host

- SSH para machines, com usuário e chave gerenciados.
- SSH agent forwarding opcional.
- Expor servidor SSH na LAN.
- Escrever/gerenciar `~/.ssh/config`.
- No Linux: integração com VS Code Remote e editores; X11/Wayland.

### 3.9 USB, serial, áudio e X11

- Listar, anexar, desanexar e inspecionar dispositivos USB; passthrough para machines.
- Portas seriais (listar/anexar).
- Áudio playback/recording em machines (v2.2.0).
- X11 forwarding para apps macOS→Linux (no original); no Linux, apps GUI via Wayland/X11 socket ou SPICE/virtio-gpu.

### 3.10 Arquivos e compartilhamento

- Bind mounts nativos (containers no Linux).
- Shared folders para machines (virtiofs/9p).
- File manager embutido; abrir no gerenciador do host; copiar caminhos.
- Pull/push via CLI.

### 3.11 Sistema, updates e diagnóstico

- Iniciar no login; manter rodando; menu bar.
- Updates automáticos/check manual; canal/bucket.
- Relatório de diagnóstico (diag), logs unificados, doctor/fix.
- Reset de dados (Docker/K8s/tudo), migração de Docker Desktop.
- Licenciamento/login; limites de plano.
- Notificações de storage/erros.

---

## 4. CLI `orb` — árvore completa (paridade)

Extraída das completions oficiais (`Resources/completions/bash/orbctl.bash`), 51 nós.

```
orb
├── clone
├── config
│   ├── add
│   ├── docker
│   ├── get
│   ├── remove
│   ├── reset
│   ├── set
│   └── show
├── create          --arch --cpus --disk --forward-ssh-agent --isolate-network
│                   --isolated --memory --mount --set-password --user --user-data
├── debug           --context --fallback --reset --workdir
├── default
├── delete          --all --force
├── docker
│   ├── migrate     --all --containers --force --images --volumes
│   └── volume
│       ├── clone   --name
│       ├── export
│       └── import  --name
├── doctor          --fix --quiet
├── export
├── help
├── import          --name
├── info            --format
├── k8s
├── list            --format --quiet --running
├── login           --domain --force
├── logout
├── logs            --all
├── pull            --machine
├── push            --machine
├── rename
├── report
├── reset           --yes
├── restart         --all
├── run             --machine --path --shell --user --workdir
├── serial
│   └── list        --format
├── ssh
├── start           --all
├── status
├── stop            --all --force
├── top
├── update          --check
├── usb
│   ├── attach      --machine
│   ├── detach
│   ├── info
│   └── list        --format
└── version
```

Descrições observadas no binário (resumo):

| Comando | Descrição |
|---|---|
| `create` | cria máquina Linux; versão opcional (última stable); usuário默认 e senha opcionais |
| `start` | inicia máquinas por ID/nome; `--all`; sem args inicia todas que estavam rodando |
| `stop` | para máquinas; sem args para o serviço inteiro (Docker + machines) |
| `delete` | apaga máquina (para antes); `--all`/`--force`; dados são perdidos |
| `list` | lista máquinas; `--running`, `--quiet`, `--format` |
| `info` | informações da máquina; `--format` |
| `logs` | logs unificados da máquina; `--all` |
| `ssh` | conexão SSH; ajuda para VS Code/remoto |
| `run` | executa comando Linux; `--machine`, `--user`, `--workdir`, `--shell`, `--path` |
| `pull`/`push` | copia arquivos host↔máquina; `--machine` |
| `export`/`import` | exporta/importa máquina `.tar.zst`; pausa para evitar corrupção |
| `clone` | clona máquina |
| `rename` | renomeia máquina |
| `default` | get/set da máquina padrão |
| `config` | get/set/add/remove/reset/show; `config docker` abre `docker.json` |
| `docker migrate` | migra containers/volumes/imagens do Docker Desktop; `--all`, `--containers`, `--images`, `--volumes`, `--force` |
| `docker volume` | clone/export/import de volumes (`tar.zst`) |
| `doctor` | diagnostica e corrige integração Docker (`--fix`, `--quiet`) |
| `reset` | reset total (Docker + Linux data); `--yes`; configuração preservada |
| `update` | atualiza; `--check` retorna exit 3 se desatualizado |
| `top` | monitor de atividade ao vivo (máquinas, containers, compose, K8s, serviços) |
| `status` | Running=0, Stopped=1, Starting=2 |
| `login`/`logout` | licença/conta; `--domain`, `--force` |
| `report` | gera relatório de diagnóstico em `~/.orbstack/diag` |
| `serial` | portas seriais (`list --format`) |
| `usb` | `list`, `info`, `attach --machine`, `detach` |
| `k8s` | comandos/instruções de Kubernetes |
| `debug` | shell de debug em container (Pro); `--context`, `--workdir`, `--fallback`, `--reset` |
| `version` | versão |
| `help` | ajuda |

Chaves de configuração observadas: `app.start_at_login`, `app.wait_for_container_stop`, `docker.expose_ports_to_lan`, `k8s.enable`, `k8s.expose_services`, `k8s.kubeconfig_use_domain`, `k8s.tls_san`, `machines.expose_ports_to_lan`, `machines.forward_ports`, `network.https`, `network.proxy`, `network.subnet4`, `ssh.expose_port`.

No PodStack o binário será `podstack` (alias `pds`), com a mesma árvore e semântica, adaptada a Podman.

---

## 5. Adaptação para Linux (Podman)

| Área | OrbStack (macOS) | PodStack (Linux) |
|---|---|---|
| Engine | Docker em VM Linux | **Podman nativo** (rootless por padrão; rootful opcional) |
| API | Docker API via VM | **libpod REST API** em `$XDG_RUNTIME_DIR/podman/podman.sock` |
| Machines | VM própria (macvirt) | **QEMU/KVM + cloud-init + virtiofs**; distros via cloud images |
| Docker CLI | bundla docker/compose/buildx | usa `podman`, `podman compose`, `docker` (shim) do sistema; não bundlar |
| K8s | cluster embutido na VM | **k3s em VM dedicada** |
| Domínios | `*.orb.local` + resolver/proxy | `*.podstack.local` + drop-in no `systemd-resolved` + proxy de userspace |
| Port forwarding | proxy na VM | portas publicadas do Podman + proxy TCP para portas não publicadas |
| File sharing | NFS/SMB/virtiofs | bind mounts nativos; **virtiofs** para VMs |
| Terminal | Ghostty | **QTermWidget** (Qt6) embutido; fallback terminal externo |
| Tray | NSStatusItem | **KStatusNotifierItem** / `QSystemTrayIcon` |
| Privilégios | SMJobBless/XPC | **polkit + helper systemd** (`podstack-helper`) para DNS/:80/:443/USB |
| Updates | Sparkle | RPM/AppImage + update via package manager; verificação assinada |
| Telemetria | Sentry | opcional/off por padrão |
| Migração | Docker Desktop | importar de Docker/Podman; migração de volumes/imagens |

### 5.1 Domínios no Linux

1. `podstackd` sobe um proxy HTTP/HTTPS em `127.0.0.1:80/443` (ou portas altas com redirect).
2. Resolver: drop-in em `/etc/systemd/resolved.conf.d/podstack.conf` com `DNS=127.0.0.1:5353` e `Domains=~podstack.local`, ou `dnsmasq` quando disponível.
3. Proxy identifica o container pelo `Host` (`nome.podstack.local`) e encaminha ao IP/porta via Podman.
4. Fallback sem privilégio: `*.localhost` (RFC 6761) e portas altas.

### 5.2 Privilégios

- Helper `podstack-helper` (systemd system service) exposto por D-Bus/polkit; operações: instalar/remover drop-in de DNS, bind em :80/:443, regras udev para USB, montar virtiofs.
- Nunca setuid no app; nunca `sudo` interativo na GUI.

### 5.3 Empacotamento

- **RPM** (Fedora/openSUSE) como alvo principal.
- **AppImage** para teste.
- **Flatpak** não previsto no v1; se reavaliado, apenas como cliente com `--filesystem` e socket Podman, documentando limitações.
- systemd user unit `podstackd.service` + `podstack.socket`.

---

## 6. Matriz de paridade e marcos

| Marco | Escopo | Prioridade |
|---|---|---|
| **M0** | Scaffold, CMake, daemon, cliente Podman, CLI básico, shell QML | P0 |
| **M1** | Containers: lista, start/stop/restart/delete, logs, exec, inspect, stats | P0 |
| **M2** | Imagens, volumes, redes, pods; create/run dialog | P0 |
| **M3** | Compose projects, builds, eventos, busca/filtros | P1 |
| **M4** | Machines QEMU/KVM (create/start/stop/SSH/export/import), cloud-init, virtiofs | P1 |
| **M5** | Kubernetes (k3s em VM), contexts, pods/services/logs | P1 |
| **M6** | Domínios + port forwarding + HTTPS + proxy + LAN | P1 |
| **M7** | Tray/menu bar, activity monitor, notificações, updates, settings completos | P1 |
| **M8** | USB/serial, X11/áudio, migração, diagnóstico, i18n, acessibilidade | P2 |

Critério de "paridade": todo comando do CLI e toda seção da UI com comportamento equivalente; diferenças documentadas (ex.: Rosetta → QEMU TCG; Finder → gerenciador de arquivos do desktop).

---

## 7. Evidências e anexos

- `docs/orbstack-analysis.md`: hashes, inventário do bundle, arquivos Swift, tipos, seletores, chaves, strings.
- `docs/cli-tree.json`: árvore do CLI extraída das completions.
- `docs/swift-files.txt`: 29 arquivos de view/model identificados.
- `docs/settings-keys.txt`: chaves de settings observadas.
