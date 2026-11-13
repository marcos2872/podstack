# PRD — PodStack

| Campo | Valor |
|---|---|
| Produto | **PodStack** |
| Versão do documento | 1.0 |
| Status | Ratificado para implementação (M0) |
| Data | 2026-11-08 |
| Formato | SDD (Spec-Driven Development) |
| Baseline funcional | OrbStack 2.2.5 (build 21124), análise estática |
| Plataforma alvo | Linux desktop — Fedora 43+ (Podman ≥ 5.8, Qt ≥ 6.10) e Ubuntu 24.04+ (Podman ≥ 4.9, Qt ≥ 6.4), Wayland/X11 |
| Licença | **GPL-3.0-or-later** |
| Documentos derivados | [`spec.md`](spec.md) · [`plan.md`](plan.md) · [`tasks.md`](tasks.md) |

---

## 1. Sumário executivo

Desenvolvedores Linux que hoje usam Docker Desktop, Podman Desktop, CLI ou uma mistura de ferramentas não têm uma experiência integrada, rápida e polida para containers, VMs Linux, Compose, Kubernetes e domínios locais em um único app.

O **PodStack** entrega essa experiência: um app desktop nativo em Qt 6/QML, um daemon de usuário e um CLI scriptable, todos sobre o **Podman Engine nativo**. O produto reproduz o fluxo de trabalho e a organização visual do OrbStack (referência de mercado no macOS), com identidade e implementação próprias, e adapta o que é específico de macOS para a realidade Linux (rootless, systemd, Wayland, polkit, QEMU/KVM).

**Meta de produto:** ser a melhor experiência de desenvolvimento com containers e VMs Linux em desktop Linux, com paridade funcional com o OrbStack nos fluxos principais e desempenho superior ao Docker Desktop.

---

## 2. Contexto e problema

### 2.1 Cenário atual

- **Docker Desktop no Linux** é pesado, exige licença em empresas e tem integração irregular com o desktop.
- **Podman Desktop** é funcional, mas a experiência é fragmentada e menos polida; containers, VMs e K8s vivem em janelas/fluxos distintos.
- **CLI pura** é poderosa, mas não oferece descoberta, inspeção visual, logs/terminal integrados nem gestão de VMs.
- **VMs Linux** exigem qemu/libvirt/virt-manager, com configuração manual de cloud-init, rede, compartilhamento de arquivos e SSH.
- **Domínios locais e port forwarding** são resolvidos com `/etc/hosts`, reverse proxies e scripts frágeis.
- **Kubernetes local** (kind/minikube/k3d) exige setup e manutenção, e não conversa com o resto do ambiente.

### 2.2 Problema

Falta um produto Linux que una, em uma única experiência:

1. Containers e imagens com ciclo de vida completo, logs, terminal, arquivos e estatísticas.
2. Volumes, redes e Compose com gestão visual e agrupamento por projeto.
3. Máquinas Linux (VMs) prontas em menos de 1 minuto, com cloud-init, SSH, compartilhamento de arquivos e port forwarding.
4. Kubernetes local gerenciado, com contextos e kubectl integrados.
5. Domínios locais (`*.podstack.local`/`*.localhost`) e HTTPS sem configurar proxy manualmente.
6. CLI com paridade com a GUI, para automação e CI.
7. Tudo isso **rootless por padrão**, com segurança e sem depender de Docker Desktop.

### 2.3 Por que agora

- Podman 5.8+/6.x consolidou a API Libpod, rootless networking (pasta), Quadlet e Compose.
- Qt 6.4+ (6.10 no Fedora) e KDE Frameworks 6 estão maduros em Wayland, com tray via StatusNotifierItem.
- QEMU/KVM + virtiofs + cloud-init estão disponíveis e estáveis em todas as distros relevantes.
- O OrbStack provou que a categoria "app integrado de dev environments" tem demanda, mas segue exclusivo do macOS.

### 2.4 Posicionamento e diferenciais

No macOS, o valor do OrbStack vem da VM rápida, do file sharing e da rede. No Linux, o Podman já roda nativo; o PodStack **não precisa de uma VM para containers**. O diferencial é a experiência integrada:

- **App nativo em Qt 6/QML**, leve e sem Electron, sobre o Podman nativo.
- **Machines QEMU/KVM** com cloud-init, SSH e mounts no mesmo app.
- **Domínios locais** (`*.localhost`) com HTTPS via Caddy.
- **Densidade + paridade de CLI**: a GUI cobre o dia a dia sem esconder o Podman.
- **Rootless-first**, com rootful opt-in e helper allowlisted.

O concorrente direto é o **Podman Desktop**; a referência de polimento é o OrbStack. O PodStack aposta em um app nativo, denso e com machines integradas, em vez de uma experiência fragmentada. **Docker e Podman coexistem**: o PodStack usa Podman, mas não exige remover o Docker.

---

## 3. Objetivos e não-objetivos

### 3.1 Objetivos (v1.0)

| ID | Objetivo | Métrica |
|---|---|---|
| G1 | Substituir Docker Desktop/Podman Desktop no dia a dia | 80% das tarefas comuns executáveis só pela GUI |
| G2 | Containers nativos com paridade de CLI e GUI | 100% dos comandos P0 com equivalente visual |
| G3 | Máquinas Linux em < 60 s | Criação + SSH funcional em ≤ 60 s em hardware de referência |
| G4 | Domínios locais sem configuração | `app.podstack.local` e `app.localhost` funcionando após criar o container |
| G5 | Kubernetes local em 1 clique | Cluster operacional e contexto configurado em ≤ 90 s |
| G6 | Segurança rootless por padrão | Nenhuma operação privilegiada sem consentimento explícito |
| G7 | Desempenho percebido superior | Startup < 1 s; memória < 180 MB; idle CPU < 1% |

### 3.2 Não-objetivos (v1.0)

- Suporte a macOS ou Windows.
- Substituir o runtime de containers (não implementamos OCI/runc; usamos Podman).
- Orquestração multi-host, Swarm ou cloud Kubernetes.
- Ser um IDE ou editor de código.
- Reescrever o `docker`/`podman` CLI; integramos e expomos.
- Compatibilidade com extensões/plugins do OrbStack.
- Store de plugins de terceiros no v1.
- Telemetria obrigatória ou conta obrigatória.

---

## 4. Personas

### P1 — Dev backend (persona primária)

- Usa containers para bancos, filas e serviços; Compose em projetos.
- Quer ver logs, abrir terminal e inspecionar sem decorar comandos.
- Dor: alternar entre CLI, Docker Desktop e IDE; portas e domínios quebrados.
- Sucesso: sobe o ambiente do projeto em 1 clique e acessa por domínio.

### P2 — Dev de plataforma/infra

- Cria máquinas Linux para testes, builds cross-distro e reprodução de bugs.
- Quer cloud-init, SSH, snapshots e compartilhamento de arquivos.
- Dor: configurar QEMU/libvirt/virt-manager manualmente.
- Sucesso: `podstack create ubuntu` + SSH em menos de 1 minuto.

### P3 — Dev mobile/frontend

- Precisa de backend local + túnel/port forwarding + K8s local.
- Quer K8s sem entender de cluster; contextos trocados automaticamente.
- Sucesso: habilitar K8s e rodar `kubectl` sem editar kubeconfig.

### P4 — DevOps/SRE

- Automatiza com CLI em scripts e CI.
- Quer comportamento determinístico, exit codes e output JSON.
- Sucesso: mesmos comandos na GUI e no CI; `--format json` estável.

---

## 5. Jornadas principais

### J1 — "Rodar meu projeto"

1. Abre o PodStack; o daemon já está ativo e o engine conectado.
2. Importa/abre um `compose.yaml` ou clica em "Novo container".
3. Vê o projeto agrupado, com serviços, status e portas.
4. Abre logs por serviço, terminal e arquivos.
5. Acessa `web.projeto.podstack.local` no navegador.

**Sucesso:** do zero ao serviço acessível em < 2 min, sem editar arquivos.

### J2 — "Criar uma máquina Linux"

1. Clica em Machines → Nova.
2. Escolhe distro/versão, CPUs, memória, disco e usuário.
3. Opcionalmente adiciona cloud-init, mounts e SSH agent.
4. Aguarda criação (≤ 60 s) e abre o terminal.
5. Copia arquivos, configura VS Code Remote e expõe uma porta.

**Sucesso:** máquina pronta, SSH e compartilhamento funcionando, sem terminal externo.

### J3 — "Kubernetes local"

1. Ativa Kubernetes nas configurações.
2. O PodStack provisiona o cluster e configura o contexto `podstack-local`.
3. Vê Pods e Services na sidebar, com logs e port-forward.
4. `kubectl get pods` funciona no terminal do host.

**Sucesso:** cluster operacional e kubectl funcionando em ≤ 90 s.

### J4 — "Automatizar no CI"

1. Instala o Pacote/CLI no runner.
2. `podstack machine create ubuntu:24.04 --cpus 2 --memory 4G --disk 20G`.
3. `podstack ssh ubuntu -- command`.
4. `podstack machine delete ubuntu --force`.

**Sucesso:** comandos determinísticos, exit codes documentados, sem prompts interativos.

---

## 6. Escopo e releases

### 6.1 Definição de paridade

**Paridade v1.0** = todos os requisitos **P0** e **P1** implementados e testados. Requisitos **P2** são pós-v1.

| Release | Conteúdo | Critério |
|---|---|---|
| **M0 — Fundação** | Scaffold, daemon, cliente Libpod, CLI base, shell QML | `podstack version`, `podstack ps`, GUI abre e lista containers |
| **M1 — Containers** | Ciclo de vida, logs, exec, inspect, stats | J1 parcial; CRUD completo |
| **M2 — Catálogo** | Imagens, volumes, redes, pods, create/run | Paridade de catálogo |
| **M3 — Compose/Builds** | Projetos, builds, eventos, busca | J1 completo |
| **M4 — Machines** | QEMU/KVM, cloud-init, SSH, arquivos, port forwarding | J2 completo |
| **M5 — Kubernetes** | k3s em VM dedicada, contextos, pods/services/logs | J3 completo |
| **M6 — Domains/HTTPS** | `*.localhost`, `*.podstack.local`, proxy, HTTPS, LAN | G4 completo |
| **M7 — Desktop** | Tray, activity monitor, settings, updates, notificações | Paridade de desktop |
| **M8 — Extras** | USB/serial, X11/áudio, empacotamento RPM/DEB/Flatpak, migração, i18n, acessibilidade | P2 |
| **v1.0** | M0–M7 estáveis | Critérios da §14 |

### 6.2 Fora do escopo do v1.0

- USB/serial e X11/áudio entram no M8 (P2), salvo se houver demanda que justifique antecipar.
- Flatpak não é requisito do v1; reavaliar sob demanda.
- Tradução para idiomas além de pt-BR/en-US.
- Tema claro completo (v1 pode ser dark-first com claro funcional).

---

## 7. Requisitos funcionais

> Prioridade: **P0** = bloqueia v1.0; **P1** = v1.0 desejável, pode escorregar para v1.1; **P2** = pós-v1.
> Cada requisito tem critério de aceite verificável. Detalhes de fluxo em [`spec.md`](spec.md).

### 7.1 Sistema, daemon e ciclo de vida (`FR-SYS`)

| ID | Requisito | Prioridade | Critério de aceite |
|---|---|---|---|
| FR-SYS-001 | O daemon `podstackd` roda como serviço de usuário (systemd --user) e sobrevive ao fechamento da GUI. | P0 | Fechar a GUI não derruba containers nem o daemon; `systemctl --user status podstack` ativo. |
| FR-SYS-002 | A GUI conecta ao daemon via socket Unix `$XDG_RUNTIME_DIR/podstack/control.sock` (0600). | P0 | Socket com permissões 0600; conexão de outro UID é rejeitada. |
| FR-SYS-003 | O daemon descobre o socket do Podman e a versão da API em runtime (`Libpod-Api-Version`). | P0 | Funciona com Podman 5.8 e 6.x sem recompilar. |
| FR-SYS-004 | Se o socket do Podman não existir, o daemon orienta e pode habilitar `podman.socket` com consentimento. | P0 | Estado de erro claro na GUI e `podstack doctor` com correção. |
| FR-SYS-005 | Suporte a rootless (padrão) e rootful (opt-in explícito, com aviso). | P0 | Troca de modo exige confirmação; UI indica o modo ativo. |
| FR-SYS-006 | Inicialização automática no login, configurável. | P0 | `app.start_at_login` cria/remove autostart. |
| FR-SYS-007 | Sair da GUI pode parar containers/daemon conforme preferência do usuário. | P1 | `app.wait_for_container_stop` respeitado. |
| FR-SYS-008 | Múltiplas janelas e abas, com estado restaurado entre sessões. | P1 | Reabrir restaura janelas/abas selecionadas. |
| FR-SYS-009 | Logs estruturados com rotação e redaction. | P0 | `~/.local/state/podstack/logs/` com JSON; sem segredos. |
| FR-SYS-010 | Modo "não perturbe"/pausa de notificações. | P2 | Notificações silenciadas. |
| FR-SYS-011 | Telemetria opt-in por categoria (crash, uso, performance, erros), desligada por padrão. | P2 | Nenhum dado sai sem consentimento; cada categoria é independente. |

### 7.2 Interface e navegação (`FR-UI`)

| ID | Requisito | Prioridade | Critério de aceite |
|---|---|---|---|
| FR-UI-001 | Janela principal com sidebar, lista e inspector redimensionáveis. | P0 | Layout persiste entre sessões; mínimo 1024×640. |
| FR-UI-002 | Sidebar organizada em **Engine** (Containers, Images, Volumes, Networks, Compose, Builds, Pods), **Linux** (Machines, Devices), **Kubernetes** (Services/contextos, quando habilitado), **General** (Activity Monitor, Commands) e Settings. | P0 | Todas as seções navegáveis; contadores corretos; Pods do Podman não se confundem com K8s. |
| FR-UI-003 | Busca global e por seção, com filtros e ordenação. | P0 | Resultados em < 200 ms para 1.000 itens. |
| FR-UI-004 | Inspector com abas Info, Stats, Logs, Terminal e Files (conforme o tipo). | P0 | Abas disponíveis conforme o tipo do item. |
| FR-UI-005 | Inspect JSON como aba adicional (diferencial PodStack; não existe no OrbStack). | P1 | JSON com busca/cópia/exportação. |
| FR-UI-006 | Ações em lote (start/stop/delete) com confirmação. | P0 | Seleção múltipla funciona; confirmação lista impacto. |
| FR-UI-007 | Estados vazios, carregando, erro e "sem permissão" com ação sugerida. | P0 | Nenhuma tela "em branco" sem explicação. |
| FR-UI-008 | Tema escuro e claro, seguindo o sistema; escala de fonte. | P1 | Troca em runtime sem reiniciar. |
| FR-UI-009 | Navegação completa por teclado e atalhos documentados. | P1 | Todas as ações acessíveis sem mouse. |
| FR-UI-010 | Notificações nativas para eventos importantes (erro, storage, update). | P1 | Respeita "não perturbe". |
| FR-UI-011 | Menu de contexto por item (abrir, copiar, revelar, remover). | P1 | Ações contextuais consistentes com a toolbar. |
| FR-UI-012 | Arrastar e soltar arquivos para importar imagens/volumes/máquinas. | P2 | Drop zone com feedback. |

### 7.3 Containers (`FR-CON`)

| ID | Requisito | Prioridade | Critério de aceite |
|---|---|---|---|
| FR-CON-001 | Listar containers (todos/rodando/parados) com nome, status, imagem, portas, projeto e idade. | P0 | Lista atualiza por eventos em < 1 s. |
| FR-CON-002 | Criar/executar container com imagem, comando, nome, portas, env, volumes, workdir, usuário, `--rm`, `--init`, `--read-only`, `--privileged`, CPU/memória. | P0 | Container criado e visível; validações impedem conflitos. |
| FR-CON-003 | Start, stop, restart, pause/unpause, kill e remove (com/sem força). | P0 | Ações refletem em < 1 s; erros exibidos com causa. |
| FR-CON-004 | Renomear e duplicar container. | P1 | Novo nome válido; conflito tratado. |
| FR-CON-005 | Logs em streaming com follow, busca, wrap, timestamps e pretty JSON. | P0 | Logs aparecem em < 500 ms; busca filtra sem travar. |
| FR-CON-006 | Terminal interativo (exec) com shell detectado e resize. | P0 | TTY funcional; Ctrl+C/Ctrl+D corretos; resize acompanha a janela. |
| FR-CON-007 | Stats ao vivo e histórico (CPU, memória, rede, disco, processos). | P0 | Atualiza a cada 1–2 s; histórico de 5 min em memória. |
| FR-CON-008 | Inspect JSON com busca, copiar e exportar. | P0 | JSON válido e completo. |
| FR-CON-009 | Navegar/baixar/enviar arquivos do container (`podman cp`). | P1 | Upload/download com progresso e conflito tratado. |
| FR-CON-010 | Portas publicadas e expostas visíveis; abrir no navegador. | P0 | Clique abre `http://localhost:<porta>` ou domínio. |
| FR-CON-011 | Agrupar containers por projeto Compose. | P1 | Grupo expansível; start/stop do grupo. |
| FR-CON-012 | Checkpoint/restore (CRIU) quando suportado. | P2 | Ação disponível apenas se o runtime suportar. |
| FR-CON-013 | Suporte a gVisor (`runsc`) quando instalado. | P2 | Runtime selecionável; erro claro se ausente. |
| FR-CON-014 | Debug shell (Pro-like) com ferramentas e instalador de pacotes no container. | P2 | Shell efêmero sem alterar a imagem. |
| FR-CON-015 | Inspecionar/limpar containers órfãos e recursos não usados. | P1 | Prune com prévia do que será removido. |

### 7.4 Imagens (`FR-IMG`)

| ID | Requisito | Prioridade | Critério de aceite |
|---|---|---|---|
| FR-IMG-001 | Listar imagens com repositório, tag, digest, tamanho, idade e uso. | P0 | Lista consistente com `podman images`. |
| FR-IMG-002 | Pull com progresso, cancelamento e autenticação de registry. | P0 | Progresso por camada; erro de auth claro. |
| FR-IMG-003 | Push/tag/untag com confirmação e seleção de registry. | P1 | Credenciais nunca aparecem em log. |
| FR-IMG-004 | Remover/prune (dangling/all) com prévia. | P0 | Não remove imagem em uso sem força explícita. |
| FR-IMG-005 | Inspecionar, ver histórico/camadas e copiar digest. | P0 | Digest copiável; histórico legível. |
| FR-IMG-006 | Importar/exportar tar e salvar/carregar. | P1 | Arquivo validado; progresso. |
| FR-IMG-007 | Seleção de plataforma/arch e indicação de multi-arch. | P2 | Pull da plataforma escolhida. |
| FR-IMG-008 | Abrir imagem em nova janela. | P2 | Janela independente. |

### 7.5 Volumes (`FR-VOL`)

| ID | Requisito | Prioridade | Critério de aceite |
|---|---|---|---|
| FR-VOL-001 | Listar volumes com driver, mountpoint, tamanho e uso. | P0 | Tamanho atualizado sob demanda. |
| FR-VOL-002 | Criar/remover volumes com validação de uso. | P0 | Volume em uso exige confirmação. |
| FR-VOL-003 | Clonar volume (cópia rápida). | P1 | Clone com conteúdo idêntico. |
| FR-VOL-004 | Exportar/importar `.tar.zst`. | P1 | Arquivo íntegro; progresso e cancelamento. |
| FR-VOL-005 | Importar diretório do host. | P1 | Validação de caminho e permissões. |
| FR-VOL-006 | Abrir no gerenciador de arquivos do desktop. | P1 | Abre o mountpoint correto. |
| FR-VOL-007 | Prune de volumes não usados com prévia. | P1 | Lista o que será removido. |

### 7.6 Redes (`FR-NET`)

| ID | Requisito | Prioridade | Critério de aceite |
|---|---|---|---|
| FR-NET-001 | Listar redes com driver, subnet, gateway, containers conectados. | P0 | Consistente com `podman network ls`. |
| FR-NET-002 | Criar/remover redes bridge, IPv4/IPv6, labels. | P0 | Validação de CIDR e conflitos. |
| FR-NET-003 | Conectar/desconectar containers e aliases DNS. | P1 | Resolução por nome funciona dentro da rede. |
| FR-NET-004 | Expor portas na LAN por rede/container. | P1 | Toggle com aviso de segurança. |
| FR-NET-005 | Configurar subnet4 e proxy global. | P1 | Aplicado a novos containers; aviso de restart. |
| FR-NET-006 | Diagnóstico de DNS/conectividade por container. | P2 | Teste de resolução e conectividade. |

### 7.7 Machines — VMs Linux (`FR-MAC`)

| ID | Requisito | Prioridade | Critério de aceite |
|---|---|---|---|
| FR-MAC-001 | Criar máquina com distro/versão, arch, CPUs, memória, disco, usuário e senha. | P0 | Criação ≤ 60 s; SSH funcional ao final. |
| FR-MAC-002 | Distros: Ubuntu, Debian, Fedora, Alpine, Arch, Rocky, Alma, openSUSE, Oracle, Kali, Gentoo, Void, NixOS, Devuan. | P0 (top 6) / P1 (demais) | Imagem cloud correta; versão listada. |
| FR-MAC-003 | cloud-init (`user-data`) e criação de usuário/senha. | P0 | user-data aplicado; login funciona. |
| FR-MAC-004 | Start/stop/restart/delete (all/force) e estado consistente. | P0 | Sem processos QEMU órfãos. |
| FR-MAC-005 | SSH integrado com chave por máquina e `~/.ssh/config` gerenciado. | P0 | `podstack ssh <name>` funciona; bloco gerenciado é idempotente. |
| FR-MAC-006 | Compartilhar pastas do host via virtiofs. | P1 | Arquivos visíveis e com permissões corretas. |
| FR-MAC-007 | pull/push de arquivos host↔máquina. | P1 | Progresso e conflito tratados. |
| FR-MAC-008 | Exportar/importar máquina `.tar.zst`. | P1 | Integridade verificada; máquina pausada durante export. |
| FR-MAC-009 | Clonar/renomear/definir máquina padrão. | P1 | Clone independente; default usado pelo CLI. |
| FR-MAC-010 | Limites de CPU/memória/disco por máquina. | P1 | Aplicados no QEMU; disco mínimo 1 GiB. |
| FR-MAC-011 | Forward automático de portas da máquina para o host. | P1 | Porta exposta acessível em `localhost`. |
| FR-MAC-012 | SSH agent forwarding opcional. | P1 | Toggle funcional; chave não é copiada. |
| FR-MAC-013 | Modo isolado (sem rede para host/outras máquinas). | P1 | Regras aplicadas; acesso bloqueado. |
| FR-MAC-014 | Console serial/VNC para troubleshooting. | P2 | Acessível quando SSH falha. |
| FR-MAC-015 | Snapshots de disco. | P2 | Criar/restaurar snapshot. |

#### 7.7.1 Guest Windows e apps remotos (`FR-MAC-101…124`, NFR-MAC-001…008)

> Promovido da spec [`docs/specs/001-apps-remotos-vm/`](../specs/001-apps-remotos-vm/spec.md)
> (M9/M10, P1/v1.x). ADR: [`0011-apps-remotos-vm.md`](../adr/0011-apps-remotos-vm.md).

| ID | Requisito | P |
|---|---|---|
| FR-MAC-101 | Canais de guest plugáveis `ssh`/`agent`, papéis primary/fallback e `guestChannels`/`guestReady` em `machines.get`. | P1 |
| FR-MAC-102 | `agent` via qemu-guest-agent sobre virtio-serial; `guest-ping` sem rede no guest. | P1 |
| FR-MAC-103 | `agent` executa comandos e arquivos ≤ 1 MiB; transferência de usuário continua no `scp`. | P1 |
| FR-MAC-104 | `agent` informa IP/SO (`guest-network-get-interfaces`, `guest-get-osinfo`). | P1 |
| FR-MAC-105 | `provisioning` → `running` só com o primário (Linux SSH 300 s; Windows agent 10 min); campos opcionais, sem novo estado. | P1 |
| FR-MAC-106 | `auto`: Linux SSH primário/agent fallback; Windows agent primário/SSH opt-in; falha classificada. | P1 |
| FR-MAC-107 | xpra seamless com janela nativa por app em Linux X11. | P1 |
| FR-MAC-108 | Backends gráficos opcionais no host; `auto` xpra ≥ 4.0 ou x11; 3.1.5 explícito com warning; RAIL sem FreeRDP → `DEPENDENCY_MISSING`. | P1 |
| FR-MAC-109 | X11 forwarding legado preservado. | P1 |
| FR-MAC-110 | Clientes de app rodam na sessão gráfica do usuário, nunca no daemon/embutidos no QML. | P1 |
| FR-MAC-111 | Console SPICE Unix + `remote-viewer` lançado pela GUI; daemon nunca abre display. | P1 |
| FR-MAC-112 | OVMF Secure Boot por VM (`nvram.fd` 0600); Linux mantém `OVMF_VARS.fd`. | P1 |
| FR-MAC-113 | TPM 2.0 por VM com `swtpm` e estado 0600; clone regenera, export não transporta. | P1 |
| FR-MAC-114 | Criação Windows com ISO local, `answerFile?`, `windowsSsh?`, mínimos 2 vCPU/4 GiB/64 GiB, `autounattend` e senha só no XML; `virtioWinIso` opcional (download/cache T9-08). | P1 |
| FR-MAC-115 | Drivers virtio + qemu-guest-agent no provisionamento; `windowsSsh` instala OpenSSH; `guestReady` dirige o estado. | P1 |
| FR-MAC-116 | Áudio Windows `intel-hda` duplex + PipeWire por VM. | P1 |
| FR-MAC-117 | Machines Windows oferecem o backend `rail` (RDP RemoteApp), condicionado ao M9. | P1 |
| FR-MAC-118 | O host mantém uma sessão RDP por machine e lança apps adicionais nela. | P1 |
| FR-MAC-119 | Senha RDP nunca em argv/log/`meta.json`; TOFU SHA-256 em `MachineMeta` e conexão só em `127.0.0.1`; mismatch → `CERT_MISMATCH`/`trust` re-pina. | P1 |
| FR-MAC-120 | Windows Home não oferece `rail`; edição detectada por `guest-get-osinfo` e a UI explica. | P1 |
| FR-MAC-121 | Contrato `machines.apps.list/launch/stop/detach/subscribe/unsubscribe/trust`; catálogo + sessões + `cachedAt`. | P1 |
| FR-MAC-122 | Erros estáveis `PODSTACK_*` + `reason` (backend_missing, guest_unreachable, app_not_found, session_conflict, session_not_found, backend_unsupported, gui_required, cert_mismatch, consent_required, provisioning_timeout). | P1 |
| FR-MAC-123 | `machines.get/list` com `origin`/`guestReady`/`guestChannels`; `stats` com `origin`, funcionando com rede bloqueada via agent. | P1 |
| FR-MAC-124 | Stop/delete/queda do daemon/RPC encerram sessões e clientes locais sem órfãos; TTL de 60 s e reconciliação. | P1 |

| ID | NFR | Critério |
|---|---|---|
| NFR-MAC-001 | Abertura de app já instalado ≤ 2 s (Fedora 43/44, host local, sem swap; Ubuntu 24.04 com xpra 3.1.5 é experimental e não entra no gate). | E2E medido. |
| NFR-MAC-002 | Nenhuma credencial em argv/log/`meta.json`; `rdpSecretPath` lido pelo `AppHost` e passado por stdin. | `/proc/<pid>/cmdline` inspecionado. |
| NFR-MAC-003 | `qga.sock`/`spice.sock`/`swtpm.sock` em `machineDir` (0700); TPM/`nvram.fd`/`rdp.secret` 0600; sessão 0700; RDP só em 127.0.0.1. | Testes de permissão. |
| NFR-MAC-004 | Ausência de backend gráfico não impede a machine; X11/SSH continuam; Linux `auto`→x11; Windows RAIL → `DEPENDENCY_MISSING`. | Testes de degradação. |
| NFR-MAC-005 | Zero regressão nos 17 binários de teste; gate de cobertura de 80% mantido. | CI. |
| NFR-MAC-006 | Setup descobre dependências por candidatos por distro, sem pacote inexistente; cobre Fedora 44, Ubuntu 24.04/26.04. | `test_setup`/clean-install. |
| NFR-MAC-007 | Instalação padrão entrega QEMU/OVMF/swtpm/virtiofsd/SPICE/áudio; xpra/FreeRDP/remote-viewer opcionais; único download de host é o virtio-win; xpra no guest sob consentimento. | Matriz por distro. |
| NFR-MAC-008 | M9/RAIL x86_64-only; host aarch64 ou guest ARM → `UNSUPPORTED`/`backend_unsupported`. | Teste de arquitetura. |


### 7.8 Kubernetes (`FR-K8S`)

| ID | Requisito | Prioridade | Critério de aceite |
|---|---|---|---|
| FR-K8S-001 | Habilitar/desabilitar cluster local com um clique. | P1 | Cluster operacional ≤ 90 s. |
| FR-K8S-002 | Gerar kubeconfig isolado e contexto `podstack-<cluster>`. | P1 | `kubectl get nodes` funciona sem editar `~/.kube/config`. |
| FR-K8S-003 | Listar Pods e Services com namespace, status e logs. | P1 | Atualização por watch; filtros. |
| FR-K8S-004 | Deletar pods/services e fazer port-forward. | P1 | Ações com confirmação e feedback. |
| FR-K8S-005 | Expor Services (NodePort/LoadBalancer/API) e TLS SANs. | P2 | Configurável; requer restart do cluster. |
| FR-K8S-006 | Trocar contexto Docker/K8s automaticamente. | P2 | Contexto correto por projeto. |
| FR-K8S-007 | Reset do cluster com confirmação. | P1 | Dados removidos; kubeconfig limpo. |

### 7.9 Compose (`FR-CMP`)

| ID | Requisito | Prioridade | Critério de aceite |
|---|---|---|---|
| FR-CMP-001 | Detectar projetos Compose por labels e agrupar containers. | P1 | Grupos corretos; status agregado. |
| FR-CMP-002 | Up/down/restart do projeto. | P1 | Equivalente a `docker compose up -d`. |
| FR-CMP-003 | Logs por serviço e agregados, com filtro. | P1 | Streaming sem misturar serviços quando filtrado. |
| FR-CMP-004 | Abrir `compose.yaml` e validar sintaxe. | P2 | Erros de schema exibidos. |
| FR-CMP-005 | Integração com Supabase CLI (start/stop de stack). | P2 | Stack reconhecida e controlada. |

### 7.10 Builds (`FR-BLD`)

| ID | Requisito | Prioridade | Critério de aceite |
|---|---|---|---|
| FR-BLD-001 | Histórico de builds com status, duração e trigger. | P1 | Retenção configurável. |
| FR-BLD-002 | Ver steps, logs e step que falhou. | P1 | Logs completos; link para o step. |
| FR-BLD-003 | Build a partir de Dockerfile/contexto e buildx. | P1 | BuildKit habilitado; cache reaproveitado. |
| FR-BLD-004 | Apagar registro(s) de build com confirmação. | P2 | Apenas metadados; não apaga imagens. |

### 7.11 Domínios e port forwarding (`FR-DOM`)

| ID | Requisito | Prioridade | Critério de aceite |
|---|---|---|---|
| FR-DOM-001 | Resolver `<container>.localhost` automaticamente, sem configuração. | P0 | `getent hosts app.localhost` → 127.0.0.1/::1; HTTP funciona. |
| FR-DOM-002 | Resolver `<container>.podstack.local` via helper opt-in. | P1 | Drop-in instalado/removido de forma idempotente. |
| FR-DOM-003 | Proxy HTTP/HTTPS por Host/SNI para o container correto. | P1 | 200 OK no domínio; 404 para desconhecido. |
| FR-DOM-004 | HTTPS automático com CA local confiável (opt-in). | P1 | Certificado válido no navegador; CA removível. |
| FR-DOM-005 | Detecção automática da porta HTTP/S do container. | P1 | Funciona com 1 porta exposta; heurística documentada. |
| FR-DOM-006 | Expor portas na LAN por toggle. | P1 | Acessível de outro dispositivo; aviso de segurança. |
| FR-DOM-007 | Proxy HTTP/HTTPS/SOCKS5/SOCKS5H com exclusões. | P1 | Config aplicada a novos containers/máquinas. |
| FR-DOM-008 | Port forwarding de portas não publicadas. | P2 | Via container roteador ou helper; documentar limite rootless. |

### 7.12 SSH e integração (`FR-SSH`)

| ID | Requisito | Prioridade | Critério de aceite |
|---|---|---|---|
| FR-SSH-001 | Gerenciar chaves e blocos do `~/.ssh/config` de forma idempotente. | P0 | Sem duplicar; backup antes de alterar. |
| FR-SSH-002 | Expor servidor SSH da máquina na LAN (toggle). | P1 | Porta e firewall documentados. |
| FR-SSH-003 | Integração com VS Code Remote e editores. | P1 | Instruções e comando copiáveis. |
| FR-SSH-004 | Encaminhar SSH agent do host. | P1 | Toggle por máquina. |

### 7.13 USB, serial, áudio e X11 (`FR-USB`)

| ID | Requisito | Prioridade | Critério de aceite |
|---|---|---|---|
| FR-USB-001 | Listar dispositivos USB e anexar/desanexar de máquinas. | P2 | Device visível no guest; desconexão segura. |
| FR-USB-002 | Listar portas seriais e anexar. | P2 | Console serial funcional. |
| FR-USB-003 | Áudio playback/recording na máquina. | P2 | PipeWire/Pulse documentado. |
| FR-USB-004 | X11 forwarding para apps da máquina. | P2 | `DISPLAY` funcional via SSH. |

### 7.14 Arquivos (`FR-FILE`)

| ID | Requisito | Prioridade | Critério de aceite |
|---|---|---|---|
| FR-FILE-001 | Navegador de arquivos de volumes e containers. | P1 | Listar, abrir, baixar, enviar. |
| FR-FILE-002 | Abrir caminho no gerenciador do desktop. | P1 | `xdg-open` correto. |
| FR-FILE-003 | Copiar caminho/arquivo para o clipboard. | P1 | Formatos texto e arquivo. |
| FR-FILE-004 | Drag-and-drop de arquivos para container/máquina. | P2 | Feedback e validação. |

### 7.15 CLI (`FR-CLI`)

| ID | Requisito | Prioridade | Critério de aceite |
|---|---|---|---|
| FR-CLI-001 | Binário `podstack` com árvore equivalente ao `orb` (33 comandos). | P0 | Todos os comandos P0 documentados e funcionais. |
| FR-CLI-002 | Alias `pds` e completions bash/zsh/fish. | P1 | Completions geradas e testadas. |
| FR-CLI-003 | Saída humana e `--format json` estável. | P0 | JSON versionado e documentado. |
| FR-CLI-004 | Exit codes documentados (0/1/2/3...). | P0 | Testes golden de exit code. |
| FR-CLI-005 | Modo não interativo para CI (`--yes`, `--force`, sem TTY). | P0 | Nenhum prompt em CI. |
| FR-CLI-006 | `podstack doctor` e `podstack report`. | P1 | Diagnóstico sem segredos. |
| FR-CLI-007 | `podstack top` (TUI) com monitor de atividade. | P2 | Funciona em terminal 80×24. |

### 7.16 Settings (`FR-SET`)

| ID | Requisito | Prioridade | Critério de aceite |
|---|---|---|---|
| FR-SET-001 | Settings em GUI e CLI com as mesmas chaves. | P0 | `podstack config set` reflete na GUI em < 1 s. |
| FR-SET-002 | Validação e mensagens de erro por chave. | P0 | Valor inválido não é salvo. |
| FR-SET-003 | Config em `~/.config/podstack/config.json` (0600). | P0 | Arquivo versionado, migrável. |
| FR-SET-004 | Reset por chave e reset total. | P1 | Confirmação e backup. |
| FR-SET-005 | Settings que exigem restart indicados claramente. | P0 | Badge "requer reinício". |

### 7.17 Updates, diagnóstico e licença (`FR-UPD`)

| ID | Requisito | Prioridade | Critério de aceite |
|---|---|---|---|
| FR-UPD-001 | Verificar updates assinados; notificar sem instalar sem consentimento. | P1 | Assinatura verificada; rollback possível. |
| FR-UPD-002 | Canal stable/beta configurável. | P2 | Troca de canal com aviso. |
| FR-UPD-003 | Relatório de diagnóstico com prévia e redaction. | P1 | Usuário revisa antes de enviar. |
| FR-UPD-004 | Licença/conta opcional (sem bloquear recursos básicos). | P2 | App funciona offline sem conta. |
| FR-UPD-005 | Migração **opcional** de dados de Docker/Podman existente (imagens, volumes). | P2 | Prévia e rollback; não é pré-requisito para usar o PodStack. |

> **Decisão (2026-11-11):** FR-UPD-004 (licença/conta) e FR-UPD-005 (migração de dados) ficam
> no pós-v1 (M8). O PodStack é offline por padrão e não exige conta; a migração permanece
> opcional, com o Docker preservado por padrão.

### 7.18 Primeira execução e setup (`FR-SETUP`)

| ID | Requisito | Prioridade | Critério de aceite |
|---|---|---|---|
| FR-SETUP-001 | Primeira execução com diagnóstico **read-only** de Docker, Podman, QEMU/KVM, `/dev/kvm`, virtiofsd, systemd de usuário, SQLite, Caddy e k3s. | P0 | Nenhuma alteração sem confirmação; cada check tem status, versão, detalhe e remediação. |
| FR-SETUP-002 | Instalar/atualizar Podman e ativar `podman.socket` rootless com consentimento. | P0 | Podman atual e socket ativo ao final; nenhuma remoção de Docker. |
| FR-SETUP-003 | Docker e Podman **coexistem**; a remoção do Docker é opt-in, com confirmação explícita e aviso de perda de containers/imagens/redes/volumes. | P0 | Docker presente não bloqueia o setup; remoção só com `confirm: true`; sem migração automática. |
| FR-SETUP-004 | Preparar QEMU/KVM, OVMF, virtiofsd e libvirt opcional; avisar e permitir TCG quando `/dev/kvm` não estiver disponível. | P0 | Machines funcionam com KVM; sem KVM, o modo TCG é explícito e marcado como lento. |
| FR-SETUP-005 | Componentes obrigatórios (socket/rootless/SQLite) e opcional Caddy com escolha explícita; o k3s roda em VM dedicada e não é instalado no host pelo setup. | P1 | Caddy não é instalado sem seleção; o resumo lista o que foi instalado e indica o Kubernetes como VM gerenciada no app. |
| FR-SETUP-006 | Validação final com obrigatórios vs. opcionais e opção de abrir o app com avisos. | P0 | Obrigatórios ausentes são destacados; o usuário pode corrigir ou continuar. |
| FR-SETUP-007 | Setup reexecutável por Settings e `podstack setup`; jobs com progresso, log, cancelamento e retry. | P1 | Fechar a janela não perde o job; reexecutar não repete ações já aplicadas. |
| FR-SETUP-008 | Nenhuma operação privilegiada pela GUI: instalar/remover pacotes e remover Docker passam pelo helper allowlisted; o socket de usuário é habilitado pelo daemon. | P0 | Nenhum `sudo`/shell na GUI; cada operação tem consentimento e auditoria. |

---

## 8. Requisitos não funcionais

### 8.1 Desempenho (`NFR-PERF`)

| ID | Requisito | Meta |
|---|---|---|
| NFR-PERF-001 | Startup da GUI (cold) | ≤ 1,0 s |
| NFR-PERF-002 | Tempo até listar containers | ≤ 700 ms após conectar |
| NFR-PERF-003 | Memória em repouso (GUI + daemon) | ≤ 180 MB RSS |
| NFR-PERF-004 | CPU em repouso | ≤ 1% de um core |
| NFR-PERF-005 | Logs: latência de primeira linha | ≤ 500 ms |
| NFR-PERF-006 | Listas com 1.000 itens | scroll 60 fps |
| NFR-PERF-007 | Ações de ciclo de vida | feedback ≤ 100 ms; conclusão ≤ 1 s (local) |

### 8.2 Segurança (`NFR-SEC`)

| ID | Requisito |
|---|---|
| NFR-SEC-001 | Rootless por padrão; rootful exige confirmação explícita. |
| NFR-SEC-002 | Socket do daemon `0600` em dir `0700`; validação de UID via `SO_PEERCRED`. |
| NFR-SEC-003 | Helper privilegiado com API fechada, polkit e auditoria. |
| NFR-SEC-004 | Nenhum segredo em logs; redaction de env, tokens e chaves. |
| NFR-SEC-005 | Verificação de assinatura em updates; TLS obrigatório para registries. |
| NFR-SEC-006 | Proteção contra path traversal em import/export e file manager. |
| NFR-SEC-007 | SELinux/AppArmor documentados; política para o helper. |

### 8.3 Confiabilidade (`NFR-REL`)

| ID | Requisito |
|---|---|
| NFR-REL-001 | Daemon reinicia automaticamente e reconcilia estado ao subir. |
| NFR-REL-002 | Nenhum processo QEMU órfão após crash; limpeza no start. |
| NFR-REL-003 | Migrações de config/dados idempotentes e com backup. |
| NFR-REL-004 | Operações destrutivas sempre com confirmação e prévia. |
| NFR-REL-005 | Reconexão automática ao Podman com backoff. |

### 8.4 Compatibilidade (`NFR-COMP`)

| ID | Requisito |
|---|---|
| NFR-COMP-001 | Fedora 43+ (Podman ≥ 5.8, Qt ≥ 6.10, systemd ≥ 257); Ubuntu 24.04+ (Podman ≥ 4.9, Qt ≥ 6.4, systemd ≥ 255). |
| NFR-COMP-002 | Wayland e X11; GNOME, KDE, niri/Noctalia, wlroots. |
| NFR-COMP-003 | Rootless e rootful; cgroup v2. |
| NFR-COMP-004 | Podman 4.9–6.x no DEB/Ubuntu e 5.8–6.x no RPM/Fedora (API descoberta em runtime). |
| NFR-COMP-005 | Arquiteturas x86_64 e aarch64. |

### 8.5 Acessibilidade e i18n (`NFR-A11Y`)

| ID | Requisito |
|---|---|
| NFR-A11Y-001 | WCAG 2.1 AA: contraste, foco, labels, teclado. |
| NFR-A11Y-002 | Escala de fonte do sistema e zoom sem quebrar layout. |
| NFR-A11Y-003 | pt-BR e en-US no v1; strings externalizadas. |
| NFR-A11Y-004 | Respeitar `prefers-reduced-motion`. |

### 8.6 Observabilidade (`NFR-OBS`)

| ID | Requisito |
|---|---|
| NFR-OBS-001 | Logs JSON com timestamp, nível, componente, correlação. |
| NFR-OBS-002 | `podstack doctor` cobre socket, Podman, DNS, portas, QEMU, kubeconfig. |
| NFR-OBS-003 | Telemetria opt-in por categoria, anônima, desligada por padrão. |
| NFR-OBS-004 | Códigos de erro estáveis documentados. |

---

## 9. UX e princípios de design

1. **Zero configuração para o caminho feliz**: criar container/máquina e acessar deve funcionar sem editar arquivos.
2. **Densidade controlada**: informação técnica visível sem poluição; progressive disclosure no inspector.
3. **Feedback imediato**: toda ação tem estado otimista + reconciliação; nada "congela".
4. **Segurança visível**: operações privilegiadas explicam o porquê e pedem consentimento.
5. **Consistência GUI/CLI**: mesmos nomes, mesmas chaves, mesmos exit codes.
6. **Acessível por padrão**: teclado, contraste, leitores de tela.
7. **Identidade própria**: paleta, tipografia e ícones originais; inspiração em densidade e organização, não cópia. Marca aprovada: **cápsula (principal) + camadas (secundária)**; monograma P não aprovado. Assets em [`../assets/`](../../assets) e regras em [`branding.md`](../engineering/branding.md).

Design tokens e componentes em [`ui-map.md`](../engineering/ui-map.md).

---

## 10. Métricas de sucesso

| Métrica | Meta v1.0 | Como medir |
|---|---|---|
| Tarefas comunes concluídas só pela GUI | ≥ 80% | teste de usabilidade com 12 devs |
| Tempo "zero → container acessível" | ≤ 2 min | cronometrado em J1 |
| Tempo "zero → máquina com SSH" | ≤ 60 s | cronometrado em J2 |
| Tempo "zero → kubectl funcional" | ≤ 90 s | cronometrado em J3 |
| Crash-free sessions | ≥ 99,5% | telemetria opt-in/local |
| NPS de devs Linux | ≥ 40 | pesquisa pós-beta |
| Issues P0 abertas no release | 0 | tracker |
| Cobertura de testes do core | ≥ 70% | CI |

---

## 11. Riscos e mitigações

| Risco | Prob. | Impacto | Mitigação |
|---|---|---|---|
| Hijack HTTP (exec/attach) mal implementado | Alta | Alto | libcurl `CONNECT_ONLY`, PoC no M0, testes de TTY/resize |
| Rootless não permite IP direto/portas baixas | Alta | Médio | `*.localhost` no v1; helper opt-in para :80/:443; documentar limites |
| virtiofs rootless/SELinux instável | Média | Alto | PoC no M4; fallback 9p; matriz de testes |
| QEMU/KVM indisponível (sem `/dev/kvm`) | Média | Médio | Detecção + modo TCG lento avisado; requisito documentado |
| QTermWidget GPL | Resolvido | — | Projeto GPL-3.0-or-later; QTermWidget compatível (ADR-0005) |
| SELinux bloquear helper/DNS/portas | Média | Alto | Política desde o M0; testes em enforcing |
| Compose provider ausente | Média | Médio | Empacotar/declarar dependência; fallback `podman-compose` com aviso |
| Coexistência Docker/Podman confunde o usuário | Média | Médio | Badge "Podman", status do engine, diagnóstico e docs; Docker é opcional |
| Setup instalar/remover pacotes | Média | Alto | Helper allowlisted, polkit, confirmação e auditoria; v1 focado em Fedora |
| API Podman 6.x mudar comportamento | Média | Médio | Testar 5.8 e 6.x; camada de adaptação |
| Escopo "paridade total" estourar | Alta | Alto | Marcos M0–M7 com critérios de saída; P2 explicitamente pós-v1 |
| Flatpak não suportar o produto | Alta | Baixo | Não é alvo v1; RPM primeiro |
| Concorrência entre GUI/CLI/daemon | Média | Médio | Estado no daemon, eventos, reconciliação |
| Vazamento de segredos em diagnóstico | Baixa | Alto | Redaction + revisão antes de enviar |

---

## 12. Dependências

### 12.1 Runtime

- Podman ≥ 5.8 no Fedora e ≥ 4.9 no Ubuntu 24.04 (`podman.socket`; 5.8 recomendado), crun/runc, pasta/slirp4netns, aardvark-dns.
- systemd ≥ 257 no Fedora e ≥ 255 no Ubuntu 24.04, polkit, dbus.
- QEMU/KVM, `qemu-img`, `cloud-init`/`cloud-localds`, `virtiofsd` (Machines).
- k3s (Kubernetes).
- Caddy 2.10 (proxy/HTTPS) — decisão ADR-0004.
- Qt 6.4+ (Core, Gui, Widgets, Quick, QuickControls2, Network, DBus); 6.10 no Fedora 43/44.
- QTermWidget 2.0.0 vendorizado no DEB (estático) / 2.4+ no RPM (GPL-2.0-or-later).
- libcurl (hijack/streams), SQLite (metadados), OpenSSL/GnuTLS.

### 12.2 Build

- CMake ≥ 3.24, Ninja, GCC 14+/Clang 18+, `clang-format`, `clang-tidy`.
- Qt Test, QML Test, `ctest`.
- RPM tooling (`rpmbuild`, `fedpkg`), AppStream.

### 12.3 Desenvolvimento

- Podman para testes de integração; Fedora 43/44 em CI.
- `shellcheck` para scripts; `reuse`/SPDX para licenças.

---

## 13. Questões e decisões

| ID | Questão | Impacto | Dono | Status |
|---|---|---|---|---|
| Q1 | **Resolvida:** licença **GPL-3.0-or-later**; terminal **QTermWidget** (GPL-2.0-or-later, compatível) | — | Produto | resolvido em 2026-11-08 |
| Q2 | **Resolvida:** Kubernetes com **k3s em VM dedicada**; kind fora do v1 | — | Arquitetura | resolvido em 2026-11-08 |
| Q3 | **Resolvida/atualizada (2026-11-11):** RPM e DEB como pacotes nativos; **Flatpak suportado como cliente desktop**, com daemon/helper instalados no host (limitações documentadas) | — | Produto | atualizado em 2026-11-11 |
| Q4 | **Resolvida:** **rootful suportado** como opt-in explícito, com aviso | — | Arquitetura | resolvido em 2026-11-08 |
| Q5 | **Resolvida:** `*.podstack.local` via helper opt-in; `*.localhost` é o padrão sem privilégio | — | Produto | resolvido em 2026-11-08 |
| Q6 | **Resolvida:** telemetria **opt-in por categoria** (crash, uso, performance, erros), desligada por padrão | — | Produto | resolvido em 2026-11-08 |
| Q7 | **Resolvida:** pt-BR e en-US, com estrutura extensível para novos idiomas | — | Produto | resolvido em 2026-11-08 |
| Q8 | **Resolvida:** Docker e Podman coexistem; o Docker é opcional e a remoção é opt-in, com confirmação e helper allowlisted | — | Arquitetura | resolvido em 2026-11-08 |
| Q9 | **Resolvida:** onboarding/setup em janela separada, 7 etapas, reexecutável; detecção read-only e ações privilegiadas via helper | — | Produto | resolvido em 2026-11-08 |
| Q10 | **Resolvida:** Pods do Podman ficam no grupo **Engine**; o grupo **Kubernetes** é reservado para k3s/clusters (M5) | — | Produto | resolvido em 2026-11-08 |
| Q11 | **Resolvida:** migração opcional de dados Docker/Podman fica para o **pós-v1 (M8)**; o Docker não é removido nem migrado automaticamente | — | Produto | resolvido em 2026-11-11 |
| Q12 | **Resolvida:** **sem conta obrigatória**; o app funciona 100% offline e nenhum recurso básico depende de rede | — | Produto | resolvido em 2026-11-11 |

---

## 14. Critérios de release v1.0

1. Todos os requisitos **P0** implementados e com teste automatizado.
2. ≥ 90% dos **P1** implementados; os restantes documentados como known gaps.
3. Nenhum bug P0/P1 aberto; crash-free ≥ 99,5% no beta.
4. Metas de desempenho da §8.1 atendidas em hardware de referência.
5. Auditoria de segurança do helper, socket e proxy concluída.
6. Testes em Fedora 43 e 44, Wayland e X11, GNOME/KDE/niri, rootless e rootful.
7. Documentação de usuário (instalação, primeiros passos, CLI, troubleshooting) publicada.
8. Pacote RPM assinado, com systemd units, polkit e política SELinux.
9. `podstack doctor` verde em instalação limpa.
10. Critérios de aceite das jornadas J1–J4 atendidos.
11. Licença GPL-3.0-or-later aplicada: `LICENSE`, cabeçalhos SPDX nos fontes e metadados do RPM.
12. Setup completo em instalação limpa, com e sem Docker; o Docker é preservado por padrão e a remoção é opt-in.

---

## 15. Anexos

- [`reference/orbstack-2.2.5.md`](../reference/orbstack-2.2.5.md) — baseline funcional e mapa de UI/CLI.
- [`spec.md`](spec.md) — especificação detalhada e critérios de aceite por fluxo.
- [`plan.md`](plan.md) — plano técnico.
- [`tasks.md`](tasks.md) — tarefas por marco.
- [`../poc/ui/README.md`](../../poc/ui/README.md) — UI Concept e guia de implementação QML (referência visual; não é o app).
- [`architecture.md`](../engineering/architecture.md) — diagramas e componentes.
- [`api-rpc.md`](../engineering/api-rpc.md) — contrato do daemon.
