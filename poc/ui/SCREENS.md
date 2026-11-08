# Telas — PodStack UI Concept

> Especificação tela a tela da POC. Use junto com [DESIGN-SYSTEM.md](DESIGN-SYSTEM.md).
> **Estado no app:** Containers, Images, Volumes, Networks, Compose, Builds e Pods foram entregues até o M3; Machines/Devices são o alvo do M4; o Setup ainda não existe. A POC é a referência visual para redesenhar e completar.

---

## 1. Inventário

| Rota (`state.view`) | Título | Grupo | Arquivo POC | Estado no app |
|---|---|---|---|---|
| `containers` | Containers | Engine | `app.js → containersView()` | ✅ M1 |
| `images` | Images | Engine | `imagesView()` | ✅ M2 |
| `volumes` | Volumes | Engine | `volumesView()` | ✅ M2 |
| `networks` | Networks | Engine | `networksView()` | ✅ M2 |
| `compose` | Compose | Engine | `composeView()` | ✅ M3 |
| `builds` | Builds | Engine | `buildsView()` | ✅ M3 |
| `pods` | Pods | Engine | `podsView()` | ✅ M2 |
| `machines` | Machines | Linux | `machinesView()` | ⏳ M4 |
| `devices` | Devices | Linux | `devicesView()` | ⏳ M4 |
| — | Settings | rodapé | `modalSettings()` | ✅ M1 |
| — | Setup | fora do app | `renderSetup()` | ⏳ novo |

---

## 2. Shell comum

Todas as telas usam o mesmo shell do app:

```text
Sidebar (236) │ Topbar 58 (breadcrumb + busca + tema + engine)
              │ Main: view-head + toolbar + conteúdo
              │ Statusbar 30 (conexão, contagens, rootless, licença)
```

- **view-head**: título 23/650 + subtítulo 12 `text-3`; ações à direita.
- **toolbar**: tabs de filtro, contagem, ações secundárias.
- **busca**: filtra a tela atual por nome/imagem/projeto/subnet/ID; em telas sem busca, o campo permanece no topbar (placeholder genérico).
- **Atualizar**: recarrega a seção; ícone gira 650 ms e mostra toast.
- **Tema**: botão sol/lua no header; alterna tokens sem recarregar.
- **Statusbar**: `podstackd conectado`, contagens de containers/imagens/volumes/máquinas/devices, `rootless`, licença.

---

## 3. Containers

### 3.1 Objetivo

Gerenciar o ciclo de vida e inspecionar containers, com foco em densidade e acesso rápido.

### 3.2 Layout

- Grid `minmax(0,1fr) 366px`.
- Esquerda: painel de lista com scroll.
- Direita: inspector sticky (Info/Logs/Stats/Files).
- Abaixo de 980 px: coluna única (inspector abaixo da lista).

### 3.3 Cabeçalho

- Título “Containers”; subtítulo “6 containers · 4 em execução · 2 parados”.
- Ações: **Novo container** (primário), **Start all**, **Atualizar** (ícone).
- Tabs: Todos 6 · Em execução 4 · Parados 2.
- Contagem à direita: “6 de 6”.

### 3.4 Lista

Cada linha (~56 px):

```text
●  api  [healthy] [acme]                     Up 2 hours
   ghcr.io/acme/api:1.18.2 · 8080→3000        [⏹] [↻]
```

- Ponto de estado (running verde; stopped cinza; saúde unhealthy vermelho; starting âmbar).
- Nome 13/600; badges de health/projeto.
- Imagem + portas em 11 `text-3`.
- Status à direita; ações de parar/iniciar/reiniciar aparecem no hover.
- Clique seleciona; seleção = `accent-soft`, sem barra.
- Vazio: “Nenhum resultado” + sugestão de filtro/busca.

### 3.5 Inspector — Info

- Header: nome + badge de estado; imagem; ações Parar/Iniciar, Reiniciar, Terminal, Remover.
- Grid 2 colunas: ID (mono), Projeto, IP (mono), Criado, Portas, Comando (mono).
- Seções: **Saúde** (badge healthy/sem healthcheck), **Variáveis** (bloco mono `CHAVE=valor`), **Mounts** (chips `host → container`).

### 3.6 Inspector — Logs

- Toolbar: Filtrar, Seguir, Limpar.
- Terminal 300 px; linhas com timestamp, nível (`INFO/WARN/ERR/OK`), mensagem; auto-scroll; cursor piscando.
- Fonte: `containers.logs.subscribe` (streaming); filtro local; máx. 5.000 linhas.

### 3.7 Inspector — Stats

- 4 cards 2×2:
  - **CPU** (`3.6%`, 12 núcleos, accent ciano)
  - **Memória** (`150 MB`, “de 2 GB”)
  - **Rede (RX / TX)** (`1,2 MB / 840 KB`, eth0)
  - **Disco / PIDs** (`0 B / 12 MB`, 24 processos)
- Dois gráficos (56 px cada), com cabeçalho “CPU (%) — últimas amostras” e “RAM (MB) — últimas amostras” e valor atual à direita.
  - CPU: linha `#22d3ee`; área 10%.
  - RAM: linha `#818cf8`; escala dinâmica (`min*0.94` → `max*1.04`) para mostrar variação.
- Atualização a cada 2 s; 30 amostras; histórico local.
- Fonte: `containers.stats.subscribe` (payload com `cpu_percent`, `mem_usage`, `mem_percent`, `net_io`, `block_io`, `pids`).

### 3.8 Inspector — Files

- Breadcrumb `/`; botão **Enviar**.
- Linhas: ícone pasta/arquivo, nome, tamanho, modo (`0644`, `0755`).
- Clique em arquivo = download; pasta = navegar (na POC, toast).
- Fonte: `containers.files.list/download/upload`.

### 3.9 Ações e diálogos

| Ação | Onde | Comportamento |
|---|---|---|
| Novo container | header | Dialog: imagem, nome, portas, env, volumes, workdir, usuário, remove/init/privileged; cria e inicia. |
| Start all | header | Inicia todos os parados; toast “N containers iniciados”. |
| Start/Stop/Restart | linha/inspector | Atualiza estado; toast. |
| Pause/Unpause/Kill | inspector (app) | Ações adicionais; confirmação em kill. |
| Remover | inspector | Dialog de confirmação; remove; toast. |
| Renomear/Duplicar | inspector (app) | Diálogo/inspector; preservar no redesenho. |
| Terminal | inspector | Janela nativa QTermWidget (M1) — embutir no inspector é desejável no futuro. |
| Batch por seleção | header | `Ctrl`/`Shift` + Iniciar/Parar/Remover em lote. |

### 3.10 Estados

- Vazio: “Nenhum container encontrado” + dica `podstack run`.
- Offline: banner vermelho + “podstackd não está acessível”.
- Loading: skeleton de linhas (a definir no app).
- Erro de ação: faixa/toast com mensagem + detalhe.

---

## 4. Images

### 4.1 Layout

- Header: “Images”; “7 imagens · 6 em uso”; ações **Pull** (primário), **Importar**, **Prune**, **Atualizar**.
- Painel com tabela full-width.

### 4.2 Colunas

| Coluna | Conteúdo |
|---|---|
| Repositório:tag | nome 12/600 + tag `text-3`; badge `dangling` quando `<none>` |
| Digest | `sha256:9f2c…a1b7` mono 11 |
| Tamanho | 182 MB, 1.2 GB |
| Criada | há 3 dias |
| Uso | badge “1 container” ou “livre” |
| Ações | remover (trash), visível no hover |

### 4.3 Ações

- **Pull**: dialog com referência e plataforma opcional; progresso no header (barra/status) e toast; cancelar.
- **Importar**: arquivo tar (Path).
- **Prune**: confirmação; remove dangling/sem uso; mostra espaço liberado.
- **Remover**: confirmação; bloqueia se em uso (force explícito).
- **Tag/Untag** (app): dialog com repo:tag; preservar no redesenho.
- **Detalhes**: digest copiável; histórico (app) pode abrir painel/modal.

### 4.4 Estados

- Vazio: “Nenhuma imagem” + Pull.
- Pull em andamento: progresso + “Cancelar”.
- Erro de auth: mensagem com ação “Fazer login”.

---

## 5. Volumes

### 5.1 Layout

- Header: “Volumes”; “4 volumes · 3 em uso”; ações **Criar** (primário), **Prune**, **Atualizar**.
- Tabela: Nome, Driver, Tamanho, Links, Mountpoint, Ações.

### 5.2 Ações

| Ação | Comportamento |
|---|---|
| Criar | Dialog: nome, driver; toast. |
| Clone | Dialog origem/destino; `cp --reflink=auto` (app). Na POC, disponível no menu da linha. |
| Importar do host | Dialog diretório de origem; cópia aditiva (app). |
| Exportar/Importar `.tar.zst` | Via CLI/`Dialog` de caminho (app); não exposto na POC. |
| Prune | Confirmação; remove volumes sem containers; mostra liberado. |
| Remover | Confirmação; em uso exige force. |

### 5.3 Estados

- Vazio: “Nenhum volume” + Criar.
- Em uso: links > 0 com badge accent; remoção bloqueada até force.

---

## 6. Networks

### 6.1 Layout

- Header: “Networks”; “3 redes · bridge”; **Criar**, **Atualizar**.
- Tabela: Nome, Driver, Subnet, Containers, Ações.

### 6.2 Ações

- Criar: dialog nome/driver/subnet/gateway/IPv6; validação de CIDR (delegada ao Podman).
- Remover: confirmação; containers conectados bloqueiam.
- **Conectar/Desconectar** (app): dialog com container e aliases; preservar no redesenho (ação na linha ou menu).
- DNS interno: label de serviço/alias resolve pelo engine.

---

## 7. Compose

### 7.1 Layout

- Header: “Compose”; “3 projetos · 1 ativo”; **Up por diretório** (primário), **Atualizar**.
- Grid de cards `minmax(320px,1fr)`.

### 7.2 Card

- Nome + dot de status (running/partial/stopped); diretório mono; badge de status.
- Serviços em chips.
- Ações: **Up**, **Restart**, **Logs**, **Down** (danger).
- Foot: “N serviços · compose.yaml”.

### 7.3 Ações

| Ação | Comportamento |
|---|---|
| Up por diretório | Dialog com diretório; `up -d` no provider; toast; recarrega a lista. |
| Up/Restart/Down | Atualizam o status; confirmação no Down; volumes preservados por padrão. |
| Logs | Modal/painel com log agregado por serviço (`compose.logs.subscribe`). |
| Abrir YAML/diretório | `xdg-open` no config file ou working dir. |
| Start/Stop | Por projeto/serviço (app); adicionar ao menu do card. |

### 7.4 Estados

- Sem projetos: “Nenhum projeto” + Up por diretório.
- Provider ausente: erro com remediação “Instale docker-compose”.

---

## 8. Builds

### 8.1 Layout

- Header: “Builds”; “N registros · histórico em SQLite”; **Novo build**, **Atualizar**.
- Grid `1fr 380px`: tabela + painel de detalhe.

### 8.2 Tabela

Colunas: Imagem (+ ID), Status, Duração, Origem (`CLI/Compose/GUI`), Início, Ação “Log”.

Status: `success` verde, `failed` vermelho, `running` ciano, `cancelled` cinza.

### 8.3 Detalhe

- Header: ID + badge de status + remover.
- Meta: imagem (mono), duração, contexto (mono), Dockerfile.
- Barra de progresso quando running.
- Log 300 px com steps (`STEP 1/5`), COMMIT, erros; auto-scroll.
- Ações: cancelar build ativo; remover registro (não remove a imagem); abrir log.

### 8.4 Novo build

- Dialog: contexto, Dockerfile, tag, plataforma, build args.
- Cria registro running; progresso/log em tempo real; toast ao concluir.
- Fonte: `builds.start` (subscription `builds.delta/done`), `builds.list/get/remove`.

---

## 9. Pods

### 9.1 Layout

- Header: “Pods”; “N pods · grupos de containers do Podman”; **Atualizar**. (No app: criar/start/stop/restart/remover.)
- Tabela: Nome (+ ID), Status, Containers, IP, Idade; status Running verde, Degraded âmbar.
- **Pods do Podman não são Kubernetes**: são grupos de containers no mesmo namespace. k3s/clusters ficam para o grupo Kubernetes (M4+), quando existirem.

### 9.2 Ações (app)

- Criar pod; start/stop/restart; remover com force; logs agregados dos containers do pod.
- A POC mostra a tabela; os botões de lifecycle devem entrar no header/menu da linha no redesenho.

---

## 10. Machines (alvo M4)

### 10.1 Layout

- Header: “Machines”; “3 máquinas · 1 em execução · QEMU/KVM”; **Nova máquina** (primário), **Importar**, **Atualizar**.
- Grid `minmax(0,1fr) 366px`: tabela + detalhe.

### 10.2 Tabela

Colunas: Nome (+ QEMU), Distro, Status (Running/Stopped/Provisioning/Error), Arch (badge), Recursos (`6 CPU · 8 GB`), IP.

Seleção = `accent-soft`; clique preenche o detalhe.

### 10.3 Detalhe

- Header: nome + status; distro/versão · arch; ações Parar/Iniciar, Reiniciar, Terminal, SSH, Remover (na zona de risco).
- Meta: IP, SSH, usuário, criada, uptime, kernel.
- Recursos: 3 barras (CPU/RAM/disco) com valor e percentual.
- SSH: bloco mono `ssh dev` + botão copiar.
- Mounts e portas em chips.
- Console: “Ver boot log” (modal).
- Zona de risco: “Remover máquina”.

### 10.4 Nova máquina

Dialog: nome, distro (Ubuntu/Debian/Fedora), versão, arch (arm64/amd64), CPUs, memória (GB), disco (GB), usuário. Cria como `Provisioning` e vira `Running` após o cloud-init (simulado; no app, progresso real).

### 10.5 Ações

Start/Stop/Restart; Terminal (QTermWidget/console serial); SSH (copia comando/abre); Logs (boot); Remover (confirmação; disco e chave); Importar/Clonar/Exportar (app, M4).

### 10.6 Estados

- Provisioning: âmbar + disable actions.
- Error: vermelho + “recriar”.
- Sem KVM: badge “TCG (lento)” e aviso.

---

## 11. Devices (alvo M4)

### 11.1 Layout

- Header: “Devices”; “5 dispositivos · 2 conectados”; **Escanear** (primário), **Atualizar**.
- Notice informativo: “Passthrough USB/serial — o dispositivo é removido do host enquanto conectado; desconecte antes de removê-lo fisicamente.”
- Tabela: Tipo (USB/Serial + ícone), Nome (+ erro), VID:PID (mono), Status (Attached/Available/Error), Máquina, Barramento, Ação.

### 11.2 Ações

- **Conectar**: dialog com select de máquina; status vira Attached; a máquina recebe o dispositivo; toast.
- **Desconectar**: confirmação; volta a Available; máquina “—”.
- **Tentar novamente**: para Error (ex.: `device busy`); libera.
- **Escanear**: procura novos dispositivos; toast.

### 11.3 Estados

- Available: badge muted; ação Conectar.
- Attached: badge success; ação Desconectar.
- Error: badge danger + motivo; ação Tentar.
- Sem dispositivos: empty state.

---

## 12. Settings

Dialog 440 px:

- **Tema**: select Claro/Escuro (o header também alterna).
- **Modo do engine**: rootless (padrão) / rootful; aviso de restart do `podstackd`.
- Erros inline (`text-danger` 10 px); ações Cancelar/Salvar.

No app, adicionar as seções previstas em [`docs/settings.md`](../../docs/settings.md) (geral, recursos, rede, updates, privacidade).

---

## 13. Setup / Onboarding

Fora da janela do app; detalhes completos em [ONBOARDING.md](ONBOARDING.md).

- Entrada na POC: botão **Configurar máquina** na barra do desktop + ícone na dock.
- No app real: primeiro uso, `podstack setup` e Settings → “Executar configuração novamente”.
- 7 etapas: boas-vindas, diagnóstico, **Podman** (coexistência com o Docker), QEMU/KVM, componentes, validação, pronto.
- **Docker e Podman coexistem** (sockets diferentes). Remover o Docker é uma ação opcional, destrutiva e com confirmação explícita; não é requisito para usar o PodStack.

---

## 14. Padrões de estado

| Estado | Regra |
|---|---|
| **Vazio** | Título curto + uma frase + ação primária + dica de CLI quando existir. |
| **Loading** | Skeleton de 6 linhas (lista) ou barra indeterminada (ação); nunca spinner eterno. |
| **Erro** | Mensagem, detalhe técnico expansível, ação recomendada (ex.: “Instale docker-compose”). |
| **Sem permissão** | Explicação rootless/rootful e como habilitar; não mostrar stacktrace. |
| **Filtro sem resultado** | “Nenhum item corresponde” + limpar filtro. |
| **Ação destrutiva** | Confirmação explícita; nome do recurso no título; consequência no corpo. |
| **Ação longa** | Progresso + log + cancelar; nunca bloquear a UI. |

---

## 15. Referências de dados

Os campos, exemplos e mapeamento RPC estão em [DATA-MODEL.md](DATA-MODEL.md). Os fluxos estão em [FLOWS.md](FLOWS.md). A tradução para QML está em [IMPLEMENTATION.md](IMPLEMENTATION.md).
