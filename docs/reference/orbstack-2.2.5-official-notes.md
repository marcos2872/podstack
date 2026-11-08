# OrbStack 2.2.5 — notas oficiais (complemento)

> Fonte: documentação pública (docs.orbstack.dev, orbstack.dev, release notes, FAQ, issues oficiais), consultada em nov/2026. Nenhum binário analisado neste documento. Serve para corrigir/complementar o mapa extraído do DMG em [`orbstack-2.2.5.md`](orbstack-2.2.5.md).

## 1. Correções ao mapa da UI

O mapa do DMG inferiu abas que **não existem**. A UI real do OrbStack 2.x:

### 1.1 Sidebar (confirmada)

| Grupo | Itens |
|---|---|
| Docker | **Containers**, **Volumes**, **Images**, **Builds**, **Networks** |
| Kubernetes | **Pods**, **Services** |
| Linux | **Machines** |
| General | **Activity Monitor**, **Commands**, **Devices** (USB/serial) |

Toolbar: toggle da sidebar, `+` (criar), menu de ações, ordenar/filtrar, busca.

### 1.2 Detalhe de container — abas reais

| Aba | Conteúdo |
|---|---|
| **Info** | imagem, comando, portas, env, mounts/volumes, rede/IP, domínios custom, labels, health, datas, ícone, "Show in Finder" |
| **Stats** | CPU, memória, disco, rede (aba dedicada desde v2.1.1) |
| **Logs** | streaming, busca, filtro "só linhas que casam", wrap, fonte, destaque JSON, scroll horizontal, filtro por serviço Compose, seletor de container em pods |
| **Terminal** | terminal Ghostty; sessões preservadas ao trocar de aba |
| **Files** | file manager do container (ver, editar, criar, apagar; abrir no Finder/editor) |

**Não existe aba "Inspect" na UI.** `docker inspect` é CLI. O PodStack pode oferecer "Inspect" como diferencial (P1), mas não é paridade.

### 1.3 Outras telas

- **Criar container**: imagem, nome, comando, portas, volumes, env.
- **Criar rede** (v2.0).
- **Criar máquina**: distro/versão, nome, arch, CPU/mem/disco, usuário, cloud-init, "Isolate machine", mounts.
- **Detalhe de máquina**: Info, Terminal, Files, Activity Monitor; settings de limites, portas HTTP/HTTPS, isolamento, mounts.
- **Settings**, **Onboarding**, **menu bar**.
- **Estados**: multi-seleção e ações em lote em containers/imagens/volumes/máquinas; confirmação de delete (Cmd-Delete) e skip (Cmd-Opt-Delete); atalhos ⌘N, ⌘,, ⌘Q, ⌘H, Esc.
- Notificações: OOM, serviço parado/falha, update, erros de disco, microfone, crash; diag report com confirmação.

## 2. Funcionalidades — detalhes que importam para o PodStack

### 2.1 Containers

- Engine Docker real; contexto `orbstack`; socket `~/.orbstack/run/docker.sock`.
- Ciclo de vida completo: create/run/start/stop/restart/pause/unpause/rm; `--privileged`, `--net=host`, `--platform`, `--device`, healthchecks, labels.
- Portas: `-p`, bind em IP específico, TCP/UDP/SCTP, portas <1024; "Expose ports to LAN" (`docker.expose_ports_to_lan`, default on); números copiáveis.
- Exec: `docker exec`, aba Terminal, **Debug Shell** (`orb debug`) para distroless/read-only.
- Logs/Stats: abas próprias + CLI.
- CRIU: checkpoint/restore experimental habilitado por padrão.
- gVisor/runsc: suportado.
- x86 em ARM: Rosetta (default), `--platform linux/amd64`, `DOCKER_DEFAULT_PLATFORM`; outras archs citadas em release notes.
- Rede: bridge custom, IP direto do Mac, `host.docker.internal`, IPv6 opcional, proxy do sistema, DNS do macOS/VPN.
- Storage: disco virtual esparso de 8 TB (`data.img`), crescimento/encolhimento dinâmico.
- **Podman não é suportado** (issue #88, "not planned"); workaround é rodar Podman dentro de uma máquina Linux. Isso valida a oportunidade do PodStack.

### 2.2 Imagens

- pull/push/tag/inspect/prune; login em registries privados; credenciais no osxkeychain.
- Certificados por registry em `~/.docker/certs.d/<registry>/`.
- Multi-arch: build para amd64/i386/arm64/arm/v7; buildx multi-plataforma; imagens locais disponíveis no K8s.
- Arquivos em `~/OrbStack/docker/images/<tag>` (somente leitura); import/export `.tar`; badge de arch emulada.
- Fora do escopo: Extensions, Docker Scout, restrição de registry.

### 2.3 Volumes e redes

- Volumes: create/ls/inspect/prune; tamanho/data na UI; arquivos em `~/OrbStack/docker/volumes/<name>`; clone copy-on-write; export/import `.tar.zst`; ícone por label.
- Bind mounts: VirtioFS com cache dinâmico; caminhos do Mac sem tradução; file watching.
- Redes: bridge e redes Compose isoladas; IPAM (`bip`, `default-address-pools`); faixa padrão `192.168.x.x`; máquinas em `198.19.x.x`.
- Conectividade: `host.docker.internal` (container→Mac), `host.orb.internal` (máquina→Mac), `docker.orb.internal` (máquina→portas publicadas), `docker.for.mac.localhost` legado.
- VPN/DNS: segue resolvers/VPN do macOS; proxy HTTP/HTTPS/SOCKS; SOCKS tem preferência; IPv6 desabilitado por padrão; ICMP funciona.

### 2.4 Builds

- BuildKit; estágios paralelos; cache com limpeza automática; `docker builder prune -a`.
- `docker buildx` incluso; builder multi-plataforma; driver Kubernetes (v2.2.0).
- Builds tab (v2.2.3): histórico, etapas, logs; recuperação de cache corrupto.

### 2.5 Compose

- `docker compose` incluso; projetos viram grupos; nome do projeto = pasta do `docker-compose.yml`.
- Domínios `service.project.orb.local`; funciona entre projetos; `docker compose run` não sequestra domínios.
- UI: start/stop/restart do projeto, logs agregados com filtro por serviço, acesso ao YAML, grupos expandidos, ícone via label.
- Supabase CLI: stacks reconhecidas e start/stop com um clique (v2.2.3).

### 2.6 Machines

- 16 distros: Alma, Alpine, Arch, CentOS, Debian, Devuan, Fedora, Gentoo, Kali, NixOS, openEuler, openSUSE, Oracle, Rocky, Ubuntu, Void.
- Criação < 1 min; init real (systemd/OpenRC/runit); todas compartilham o kernel do OrbStack (modelo WSL2).
- x86 em ARM via Rosetta; sem attach de GDB/LLDB em processo emulado.
- Limites por máquina: `--memory`, `--cpus`, `--disk`; config `machine.<nome>.{memory_mib,cpu,disk_bytes}`.
- Clone CoW, export/import `.tar.zst`, rename, delete; machine settings com portas HTTP/HTTPS.
- Cloud-init: `-c/--user-data` com cloud-config YAML ou shell script.
- File sharing: Mac em `/mnt/mac` e nos mesmos caminhos (`/Users/...`); Linux em `~/OrbStack/<machine>`; `orb push/pull`.
- Rede: `machine.orb.local`; portas em `localhost`; `machines.expose_ports_to_lan`; IPs `198.19.x.x`; comunicação entre máquinas; `http_port`/`https_port`.
- SSH: servidor embutido multiplexando todas as máquinas; host `orb` no `~/.ssh/config`; porta **32222**; chave `~/.orbstack/ssh/id_ed25519`; `ssh user@machine@orb`; só chave, só localhost; agent forward automático; `-L/-R`.
- Isoladas: sem `/mnt/mac`, sem host/rede de máquinas (opcional), sem comandos `mac`, sem USB/serial/som; SSH agent opt-in; mounts seletivos `--mount`; não é fronteira de segurança contra malware (kernel compartilhado).
- Integração: comandos `mac`/`macctl`, `open`/`pbcopy`/`osascript`/`code` linkados, binfmt para Mach-O, `caffeinate`, `ORBENV`/`WSLENV`, notificações; headers do kernel em `/sys/kernel/kheaders.tar.xz`; sem KVM aninhado.
- Gráficos: sem X server/Wayland embutido; XQuartz (`DISPLAY=host.docker.internal`) ou xrdp; X11 forwarding citado.

### 2.7 Kubernetes

- Cluster single-node leve (k3s) com GUI e integração de rede; `kubectl` incluso; `orb start|stop|restart|delete k8s`.
- Imagens locais disponíveis sem registry (evitar `:latest` ou `imagePullPolicy: IfNotPresent`).
- Acesso direto: ClusterIP, Pod IP, NodePort em `localhost`, LoadBalancer/Ingress em `*.k8s.orb.local`, `cluster.local`, API em `k8s.orb.local`; port-forward desnecessário para Services.
- UI: Pods e Services, labels, seções colapsáveis, init containers, logs por container, abrir serviço no browser, troca de contexto pelo menu bar.
- Config: `k8s.kubeconfig_use_domain`, `k8s.tls_san`, KubeletConfiguration em `~/.orbstack/config/kubelet.conf`; Flannel CNI; network policies.
- Sem Ingress controller por padrão; multi-node via kind/k3d/k3s/minikube em máquina.

### 2.8 Domínios e HTTPS

- `container.orb.local`, `service.project.orb.local`, `machine.orb.local`; zero config; subdomínios wildcard.
- Custom domains via label `dev.orbstack.domains` (somente `.local`); mirror de produção.
- Detecção automática de porta: probe HTTP/HTTPS na primeira conexão (user-agent `OrbStack-Server-Detection`), cache; override `dev.orbstack.http-port`/`https-port`; porta por domínio `app.local@8080` (v2.2.4).
- Index page `orb.local`; `k8s.orb.local`; depende de "Allow access to container domains & IPs" (`network_bridge`).
- HTTPS: CA local + certificados por domínio + reverse proxy; CA confiada no host e nos containers (opt-out label); chaves no keychain; Firefox ≤119 exige `security.enterprise_roots.enabled`.
- Porta 443/8443 pode ser usada por servidor próprio; label `dev.orbstack.https-port`.

### 2.9 Menu bar, integrações e ciclo de vida

- Menu bar: Open OrbStack (⌘N), Containers (submenu por projeto/container), Machines (submenu), Kubernetes Context, Help, Settings… (⌘,), Quit (⌘Q); abrir web service, port forwards/bind mounts, copiar ID/imagem/endereço/domínio, iniciar parados, terminal, Finder.
- Editores: VS Code Remote-SSH (`host orb`), Dev Containers, JetBrains, Fleet, Ansible (`machine@orb`).
- USB/serial: passthrough; serial/UART e FIDO são *forwarded* (usáveis nos dois lados); demais dedicados; `orb usb list|info|attach|detach`, `orb serial list`; indisponível em isoladas.
- Som: placa ALSA virtual com entrada/saída padrão do Mac; playback/recording; permissão de microfone; indisponível em isoladas.
- Notificações via `mac notify`; `orb top` TUI.
- Backup/restore: export/import de máquinas e volumes; dados em `~/Library/Group Containers/.../data`.
- Updates: auto-update; canais Stable/Canary; `orb update --check` (exit codes); sem auto-update headless.
- Licença: pessoal grátis; comercial US$ 8/usuário/mês; trial Pro 30 dias; educação/OSS sob aprovação; Enterprise com SSO/SAML, auto-assign, MDM.
- Admin opcional: instalar CLIs, symlink `/var/run/docker.sock`; `setup.use_admin=false`.

## 3. Settings — categorias oficiais

| Categoria | Opções |
|---|---|
| **System** | `rosetta`, `memory_mib`, `cpu` (7 = 700%), `mount_hide_shared` |
| **Docker** | IPv6, daemon.json (`orb config docker`), containerd image store, `docker.expose_ports_to_lan`, restart do engine, logs |
| **Network** | `network_bridge`, `network_proxy` (auto/none/URL), `network.proxy.exclude`, aviso de conflito de IP com VPN |
| **Storage** | barras de capacidade; limpar dados Docker; reset total; uso por volume |
| **Kubernetes** | expor serviços à LAN, kubeconfig por domínio, SANs do API server, kubelet config, start/stop |
| **Machines** | `machines.expose_ports_to_lan`, limites por máquina, portas HTTP/HTTPS, isolamento, mounts |
| **Advanced (CLI-only)** | `ssh.expose_port` (default false), `docker.node_name` (Swarm), `setup.use_admin` |
| **App/Conta** | iniciar no login, ícone no menu bar/Dock, canal de update, fonte de terminal/logs, app de terminal preferido, "Hold ⌘Q", firewall, conta/organização, diagnósticos |

## 4. CLI — correções e semântica

- `orb` sem subcomando = shell na máquina default; `orb <cmd>` = executa na default; `orbctl` = forma explícita (exige `shell`/`run`).
- Flags globais: `-m/--machine`, `-u/--user`, `-w` (workdir), `-c` (cloud-init no create; contexto no debug).
- `orb start|stop|restart [machine|docker|k8s]`; `orb delete [machine|docker|k8s]`; `orb status` (0/1/2).
- `orb create` flags: `--arch`, `--memory`, `--cpus`, `--disk`, `--isolated`, `--isolate-network`, `--forward-ssh-agent`, `--mount`, `--set-password`, `-c user-data`.
- `orb config set K V`, `orb config docker`; `orb docker migrate`; `orb docker volume clone|export|import`.
- `orb usb list|info|attach|detach`; `orb serial list`; `orb top`; `orb update [--check]`; `orb login|logout`; `orb report`; `orb reset`; `orb completion`; `orb version`; `orb help`.
- `orb migrate` (migração de dados) e `orb domain` não aparecem na doc oficial; não fazem parte da paridade obrigatória.
- `mac`/`macctl` são macOS-only; **não** entram no PodStack.
- Não existem `orb machine` nem `orb kube`; máquinas usam verbos diretos e K8s usa `start/stop/restart/delete k8s`.

## 5. O que o PodStack deliberadamente não copia

- Rosetta e binfmt Mach-O (macOS-only).
- Keychain/osxkeychain (Linux usa p11-kit/Secret Service).
- Finder (Linux usa `xdg-open`/portal).
- `mac`/`macctl`, XQuartz, `pbcopy`/`osascript`.
- mDNS `.local` como mecanismo primário (conflita com Avahi na LAN; PodStack usa `*.localhost`/`*.podstack.local`).
- Modelo de licença/SSO/MDM do OrbStack.

## 6. Lacunas oficiais (ND)

- Textos de empty state, campos exatos dos diálogos, estrutura exata de Settings, conteúdo da tela Commands.
- Atalhos além dos listados; busca global; detalhes do histórico de builds.
- `orb domain`/`orb exec` não documentados.
- gVisor/CRIU/emulação de outras archs só em release notes.
- Limites por container e restart policy na UI não documentados.
