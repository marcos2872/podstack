# Mapa de UI — PodStack

> Especificação de telas, navegação, componentes QML, design tokens, atalhos e estados. Identidade visual própria, inspirada em densidade e organização de IDEs de containers — **sem copiar assets/layout proprietário**.

---

## 1. Princípios

1. **Três painéis**: sidebar → lista → inspector. O usuário nunca perde o contexto.
2. **Densidade com respiro**: linhas compactas (32–36 px), agrupamentos claros, detalhes sob demanda.
3. **Status sempre visível**: cor + ícone + texto (nunca só cor).
4. **Ação no contexto**: toolbar para o global, menu de contexto para o item.
5. **Feedback imediato**: estados otimistas + reconciliação; skeletons em vez de spinners infinitos.
6. **Acessível**: foco visível, contraste AA, atalhos completos.

---

## 2. Layout da janela principal

```
┌────────────────────────────────────────────────────────────────────────────┐
│ Toolbar  [☰] PodStack   [+ Criar] [Importar] [Abrir]   [🔍 Buscar] [⚙]    │
├───────────────┬──────────────────────────────────────┬─────────────────────┤
│ Sidebar (220) │ Lista (flex)                         │ Inspector (480)     │
│               │                                      │                     │
│ ENGINE        │ Tabs: Todos | Rodando | Parados      │ [Info][Stats][Logs] │
│  Containers 12│ Filtro: [todos ▾]  Ordenar: [nome ▾] │ [Terminal][Files]   │
│  Images     8 │ ┌──────────────────────────────────┐ │                     │
│  Volumes    5 │ │ ● api        running   nginx  80 │ │  api                │
│  Networks   4 │ │ ● db         running   pg     5432│ │  nginx:alpine       │
│  Compose    3 │ │ ○ worker     stopped   node       │ │  ...                │
│  Builds     3 │ └──────────────────────────────────┘ │                     │
│  Pods       2 │                                      │                     │
│ KUBERNETES    │                                      │                     │
│  Services   2 │                                      │                     │
│  Contextos    │                                      │                     │
│ LINUX         │                                      │                     │
│  Machines   2 │                                      │                     │
│  Devices    5 │                                      │                     │
│ GENERAL       │                                      │                     │
│  Activity     │                                      │                     │
│  Commands     │                                      │                     │
│               │                                      │                     │
│ [Settings]    │                                      │                     │
├───────────────┴──────────────────────────────────────┴─────────────────────┤
│ Status: engine rootless · Podman 5.8.9 · 12 containers · CPU 3% · 1.2 GB   │
└────────────────────────────────────────────────────────────────────────────┘
```

- Splitter arrastável; larguras persistem por seção.
- Colapsar sidebar (`Ctrl+B`) e inspector (`Ctrl+Shift+B`).
- Mínimo 1024×640; layout adapta inspector para drawer abaixo de 900 px.

---

## 3. Sidebar

| Grupo | Item | Contador | Ação primária |
|---|---|---|---|
| **Engine** | Containers | running/total | criar container |
| | Images | total | pull/importar |
| | Volumes | total | criar/importar |
| | Networks | total | criar rede |
| | Compose | projetos/ativos | up por diretório |
| | Builds | ativos/histórico | novo build |
| | Pods | running/total | criar pod |
| **Kubernetes** (M5) | Services | total | — |
| | Contextos | atual | trocar/gerar kubeconfig |
| **Linux** | Machines | running/total | criar máquina |
| | Devices | conectados | anexar/desanexar |
| **General** | Activity Monitor | — | abrir |
| | Commands | — | abrir referência |
| **Rodapé** | Settings | — | abrir preferências |

- **Pods do Podman ficam em Engine**; K8s Pods aparecem no contexto do cluster (M5), não como o mesmo item.
- **Seleção usa fundo `accent.soft`**, sem barra vertical ou outline de acento.
- Grupos colapsáveis; estado persistido.
- Badge de erro/atenção agregado no grupo.
- Modo rootful indicado por badge "root" no rodapé.
- Busca na sidebar filtra itens e seções.

---

## 4. Lista

### 4.1 Elementos

- **Tabs** por seção (ex.: Todos / Rodando / Parados; Compose groups).
- **Barra de filtro**: texto, chips (status, arquitetura, projeto), ordenação.
- **Cabeçalho de colunas** clicável (nome, status, imagem, portas, idade).
- **Linhas**: status dot + ícone + nome + metadados; hover revela ações rápidas.
- **Multi-seleção**: `Ctrl+click`, `Shift+click`; barra de ações em lote.
- **Menu de contexto**: iniciar/parar/remover, copiar ID/imagem/endereço, abrir no navegador, revelar no gerenciador.

### 4.2 Por seção

| Seção | Colunas principais (implementado) | Ações de toolbar |
|---|---|---|
| Containers | nome, status, imagem, portas | criar, start/stop all, lote por seleção, expandir (overlay com terminal) |
| Images | repositório:tag, digest, tamanho, uso | pull, importar, tag, untag, prune (prévia), remover |
| Volumes | nome, driver, tamanho, links | criar, clone, importar do host, prune (prévia), remover |
| Networks | nome, driver, subnet, containers | criar, conectar, desconectar, remover |
| Pods (Podman) | nome, status, containers, ID | criar, start, stop, restart, remover |
| Compose | projeto, status, serviços, diretório | up, down, restart, logs, abrir YAML/diretório |
| Builds | imagem, status, duração, origem, plataformas | novo build, cancelar, remover, log |
| Services (K8s) | planejado (M5) | — |
| Machines | nome, distro, estado, SSH | criar, start/stop/restart, excluir |
| Devices | tipo, nome, VID:PID, status | attach/detach |

**Prune com prévia (T2-18):** os diálogos de Imagens e Volumes chamam
`backend.refreshDiskUsage()` ao abrir, consultam `system.df` e listam
`sections.images.items`/`sections.volumes.items` com nome, tamanho e situação
(dangling/sem uso/em uso) e o total recuperável estimado. A lista tem altura limitada e,
quando vazia, mostra uma mensagem. Nenhum mountpoint/path é exposto na prévia.

### 4.3 Estados

- **Vazio**: ilustração leve + título + ação primária + dica de CLI.
- **Carregando**: skeleton de 6 linhas.
- **Erro**: mensagem + `Tentar novamente` + `Ver diagnóstico`.
- **Sem permissão**: explicação + como habilitar (rootless/rootful).
- **Filtro sem resultado**: "Nenhum item corresponde" + limpar filtro.

---

## 5. Inspector

Abas por tipo (estado do M4 após o redesenho do detalhe):

| Aba | Container | Machine | Volume | Image | Observação |
|---|---|---|---|---|---|
| **Info** | ✅ card lateral | ✅ | planejado | planejado | nome, imagem, portas, estado, health, ações |
| **Logs** | ✅ overlay (Detalhes) | ✅ | — | — | container: streaming/follow; machine: one-shot + Atualizar |
| **Stats** | ✅ overlay (Visão geral) | ✅ | — | — | CPU, memória, rede, disco/PIDs + gráfico; machine via SSH (2 s) |
| **Files** | ✅ overlay (Detalhes) | planejado | planejado | — | enviar, baixar e navegar via `FilesView` no detalhe; saiu do card lateral |
| **Terminal** | ✅ aba interna do detalhe | SSH no dock | — | — | `TerminalView`/QTermWidget em placeholder do QML; **Nova sessão em janela** via `ExternalTerminal` |
| **Inspect** | ✅ overlay | — | — | — | JSON completo via `containers.inspect` |

Ações da GUI no M3: criar container (diálogo), start/stop/restart/pause/unpause/kill/remove, renomear, duplicar, abrir porta, abrir terminal, `Start all`/`Stop all`, multi-seleção (`Ctrl`/`Shift` + lote), upload para containers, e Settings (rootless/rootful). O catálogo tem seções navegáveis para Images (pull/importar/tag/untag/prune/remover), Volumes (criar/clone/importar do host/prune/remover), Networks (criar/conectar/desconectar/remover) e Pods (criar/start/stop/restart/remover). **Compose** agrupa projetos por labels, sobe/derruba via provider, abre YAML/diretório e assina logs por serviço ou agregados. **Builds** inicia builds via Libpod (uma ou nenhuma plataforma) ou uma manifest list via CLI do Podman (duas ou mais), exibe as plataformas no diálogo e na coluna `PLATAFORMAS` do catálogo quando há dados, mostra progresso/log (sem steps no fluxo multi-plataforma) e mantém histórico em SQLite. **Machines** (M4) lista NOME/DISTRO/ESTADO/SSH, mostra Info, Logs do `console.log` (com seleção de texto, copiar e horário da captura) e Stats (cards e sparklines de CPU, RAM, disco, rede e I/O) e permite criar (com senha opcional só em hash), start/stop/restart, abrir SSH em terminal e excluir com confirmação. **Eventos globais** aparecem como toasts no topo. A barra de status mostra a conexão com o daemon, contagens de containers/imagens/volumes/machines e a licença. Services e Devices permanecem desabilitados até os marcos seguintes.

### 5.1 Info

- Cabeçalho: ícone, nome, status, ações (start/stop/ssh/restart/delete).
- Largura do painel: ~480 px no modo normal e ~320 px no compacto (janela < 1100 px);
  a lista encolhe com `Layout.fillWidth`. As grades de Info/Ações usam uma coluna quando
  não há espaço (limiar de 460 px) para não cortar ID, portas, estado ou botões.
- Botão **Expandir** (ícone expand, quatro setas para fora) abre o detalhe do container
  em overlay integrado (seção 5.7). O card lateral mostra somente **Info** e **Ações**.
- Grid de metadados: ID, portas publicadas, estado, criado e saúde.
- Ações: abrir no navegador, copiar ID, revelar no gerenciador.
- **Exposição LAN (T2-15):** na célula de portas, o botão alterna **“Expor na LAN”** /
  **“Fechar LAN”** para containers com portas publicadas (`backend.exposeContainerLan`).
  O aviso lembra que só as portas publicadas são afetadas e que containers antigos em
  `127.0.0.1` exigem recriação; erros (`PODSTACK_UNSUPPORTED`/`PODSTACK_HELPER_REQUIRED`)
  aparecem junto ao botão e no rodapé de ações. O diálogo de criação tem o checkbox
  **“Expor portas na LAN”** com o mesmo aviso; sem ele, o bind é `127.0.0.1`.

### 5.2 Stats

- Cards: CPU %, memória (usado/limite), rede RX/TX, disco R/W, PIDs.
- Gráfico de linha (60 s) com tooltip; atualização 2 s.
- Botão pausar/retomar.
- **Machines (M4):** a aba **Stats** coleta de dentro da VM por SSH one-shot a cada 2 s
  (`machines.stats.subscribe`) e mostra cards de CPU, RAM (usado/total), disco (usado/total),
  rede ↓/↑ e I/O de disco (leitura/escrita), com sparklines de CPU/RAM (0–100) e rede
  (autoescala). Ao abrir a aba a subscription começa; ao sair, é cancelada. Estados vazios
  (“aguardando amostras”, machine parada) e erros de SSH aparecem no próprio painel.

### 5.3 Logs

- Toolbar: follow (toggle), busca, filtro (só matching), wrap, timestamps, pretty JSON, limpar, exportar.
- Lista virtualizada com cores por stream (stdout neutro, stderr vermelho suave).
- Auto-scroll pausa ao subir; badge "novas linhas".
- Machines (M4): `console.log` one-shot com as últimas 200 linhas, seleção de texto, botão
  Copiar e indicador "Atualizado em" (horário da captura); follow/console serial ficam para
  a T4-15.

### 5.4 Terminal

- Emulação completa (cores, cursor, seleção, copy/paste, mouse).
- Componente reutilizável `TerminalView` (QTermWidget + barra com título e botão
  **Abrir externo**) usado no dock, na `TerminalWindow` legada e no overlay de detalhe.
- Terminal embutido em painel dockável (`QDockWidget` "Terminal") da janela principal,
  com abas (`QTabWidget`) por sessão; título = container/machine.
- Containers abrem `podman exec -it <id> /bin/sh`; machines abrem `ssh` após
  `machines.ssh`. Fechar a aba encerra a sessão; fechar o dock apenas o esconde
  (as sessões continuam e uma nova sessão reabre o painel).
- **Abrir externo** relança o mesmo programa/argumentos via `xdg-terminal-exec`,
  `$TERMINAL` ou os emuladores comuns, sem passar por shell.
- Menu: copiar tela/seleção (texto/ANSI/HTML), reset, fonte, fechar.

### 5.5 Files

- Breadcrumb + listagem (nome, tamanho, modo, mtime).
- Ações: abrir, baixar, enviar, criar pasta, renomear, apagar, copiar caminho.
- Editor externo com save-back opcional.

### 5.6 Inspect

- JSON viewer com árvore, busca, copiar, exportar.
- **Diferencial PodStack** (não existe como aba no OrbStack).

### 5.7 Detalhe do container (overlay integrado)

- Botão **Expandir** no cabeçalho do inspector abre o `ContainerDetailDialog`, um
  `QWidget` **filho da `MainWindow`** que cobre a janela inteira (sem frame nem janela
  separada) com fundo escurecido e um card central de margens ~24 px e cantos
  arredondados. O botão **x** do cabeçalho (ou `Esc`) encerra o overlay, os streams e a
  sessão de terminal. Ao redimensionar a janela, um event filter reajusta o overlay.
- O card lateral mantém apenas **Info** e **Ações**; logs, stats, files, ambiente,
  redes, volumes e inspect vivem no overlay.
- O card do overlay hospeda `ContainerDetail.qml` diretamente em um `QQuickWidget`
  (sem `QTabWidget` nativo). O terminal é uma **aba interna** do detalhe, entre
  **Arquivos** e **Inspect**: o QML reserva a área e o C++ sobrepõe a sessão
  `TerminalView` (`podman exec -it <id> /bin/sh`) sobre o placeholder, ocultando-a
  ao trocar de aba e preservando a sessão até fechar o overlay. O botão **Nova
  sessão em janela** abre uma nova sessão via `ExternalTerminal`; o **Abrir
  externo** do `TerminalView` continua no dock e na `TerminalWindow`.
- Cabeçalho do detalhe: nome, status, imagem, start/stop/restart, remover com
  confirmação e **x** para fechar. O terminal não fica mais no cabeçalho.
- Abas de Detalhes (9):
  - **Visão geral** — cards de CPU, RAM (usado/total), rede ↓/↑ e disco R/W com sparklines
    de 60 amostras; stats em stream próprio (`containerDetailStats`), ~1 s.
  - **Logs** — `LogsView` em `TextArea` único (seleção multi-linha, Ctrl+C, Copiar, busca,
    wrap, timestamps, seguir e limpar) alimentado por `containerDetailLogLines`.
  - **Info** — estado, PID/exit, health, criado/iniciado, restart policy, imagem/digest,
    entrypoint/comando, workdir, usuário, portas publicadas e labels.
  - **Ambiente** — todas as `Config.Env` (`KEY=VALUE`); chaves com token/password/secret/
    authorization/key ficam mascaradas até **Mostrar valores**; Copiar respeita o mascaramento.
  - **Redes** — `NetworkSettings.Networks` (IP, gateway, MAC, aliases, NetworkID) e portas publicadas.
  - **Volumes** — `Mounts` (tipo, nome, source, destination, driver, RW e propagação).
  - **Arquivos** — `FilesView` para navegar, baixar e enviar arquivos do container.
  - **Terminal** — sessão embutida `podman exec -it <id> /bin/sh` sobre o placeholder,
    com o botão **Nova sessão em janela**.
  - **Inspect** — JSON completo e indentado de `containers.inspect`, com Copiar.
- Ao abrir: `loadContainerDetail(id)` + streams próprios de stats/logs. Ao fechar: ambos os
  streams são cancelados e os logs do detalhe são limpos. Erros de inspect aparecem no
  banner do overlay, sem poluir `backend.lastError` do inspector de containers.

---

## 6. Diálogos

| Diálogo | Campos | Validação |
|---|---|---|
| Criar container | imagem, nome, comando, portas, env, volumes, workdir, user, flags, recursos, expor na LAN | imagem existente/pull; nome único; portas válidas; helper para LAN |
| Criar rede | nome, driver, subnet4, gateway, IPv6, labels | CIDR válido; sem sobreposição |
| Criar máquina | distro/versão, nome, CPU/mem/disco, user, user-data, isolado, SSH agent | recursos mínimos; nome único; YAML válido. M4 não expõe senha nem mounts (T4-04/T4-07) |
| Clonar máquina | origem, novo nome | nome único |
| Importar máquina | arquivo, nome | checksum/manifest |
| Renomear | novo nome | RFC 1123 |
| Confirmar remoção | lista + impacto | requer confirmação ou `force` |
| Reset Docker/K8s | escopo + aviso | digitar nome/`--yes` |
| Instalar helper/CLI | explicação + polkit | consentimento |
| Migração | origem, itens, prévia | espaço/rollback |

Todos com: foco inicial no campo primário, `Esc` cancela, `Enter` confirma quando válido, erros inline.

---

## 7. Settings

Janela separada (⌘, / `Ctrl+,`), sidebar de categorias:

| Categoria | Conteúdo |
|---|---|
| **Geral** | iniciar no login, tema, idioma, tray, manter rodando |
| **Engine** | rootless/rootful, socket, versão, reiniciar engine |
| **Setup** | executar novamente, status do Podman/Docker, componentes |
| **Docker** | expor portas na LAN, containerd image store, daemon.json |
| **Rede** | HTTPS, proxy, exclusões, subnet, bridge, LAN |
| **Máquinas** | portas na LAN, forward, limites default, SSH agent |
| **Kubernetes** | habilitar, expor serviços, kubeconfig por domínio, SANs |
| **Terminal/Logs** | fonte, tamanho, wrap, pretty JSON, busca |
| **Updates** | modo, canal, check agora |
| **Avançado** | SSH expose, diretórios, reset, diagnóstico, telemetria |

Cada setting: label, descrição curta, controle, badge "requer reinício" quando aplicável, link "saiba mais".

---

## 8. Onboarding / setup

Janela separada, aberta no primeiro uso, por `podstack setup` ou Settings → “Executar configuração novamente”.

1. **Boas-vindas**: o que é o PodStack, o que será feito; nada é alterado.
2. **Diagnóstico**: checks read-only de Docker, Podman, QEMU/KVM, `/dev/kvm`, virtiofsd, systemd de usuário, SQLite, Caddy e k3s.
3. **Podman**: instalar/atualizar e ativar `podman.socket` rootless; **o Docker não é removido por padrão**. A remoção é uma ação separada, opt-in, com confirmação e aviso de perda.
4. **QEMU/KVM**: instalar QEMU, OVMF, virtiofsd e libvirt opcional; sem `/dev/kvm`, avisar e permitir TCG.
5. **Componentes**: socket/rootless/SQLite obrigatórios; Caddy e k3s opcionais (k3s desmarcado).
6. **Validação**: checklist final; obrigatórios vs. opcionais; permite abrir com avisos.
7. **Pronto**: resumo e abertura do app.

- Pode ser pulado; reexecutável em Settings → Setup.
- O setup nunca executa `sudo`/shell na GUI; ações privilegiadas passam pelo helper allowlisted.
- Detecção é read-only; toda alteração exige consentimento.
- Docker e Podman coexistem (sockets diferentes); o PodStack usa Podman.

**Implementação:** `SetupWindow.qml` é um **overlay modal dentro da MainWindow** (painel
1020×680 limitado à janela), com `SetupRail.qml` (rail das 7 etapas) e `SetupCheckRow.qml`.
Enquanto `setup.completed=false`, o overlay cobre o app e o conteúdo fica desabilitado; não há
“abrir mesmo assim”. O X só aparece ao reconfigurar um setup já concluído, e o botão
**Executar configuração novamente** em Settings reabre o overlay. A conclusão só é liberada
quando os checks obrigatórios estão `ok`. O painel de progresso/log desaparece no sucesso;
em falha, permanece para diagnóstico com um X no canto superior direito, e o X não aparece
enquanto o job está rodando.

---

## 9. Tray / menu bar

- Ícone com status (normal/atenção/erro).
- Menu:
  - Abrir PodStack
  - Containers → submenu por projeto (start/stop/restart/logs)
  - Machines → submenu (start/stop/ssh)
  - Kubernetes → contexto/start/stop
  - Activity Monitor
  - Settings
  - Sair
- Clique simples abre a janela; clique do meio abre Activity Monitor.
- "Manter rodando ao fechar" configurável.

**Implementação (2026-11-08):** `TrayController` usa `QSystemTrayIcon` + `QMenu` (SNI via
DBusMenu no Wayland/Noctalia). O menu entrega **Abrir PodStack**, **Containers** (agrupado por
projeto Compose, com iniciar/parar/reiniciar/logs — abre o modal na aba Logs — e abrir),
**Activity Monitor**, **Configurações**
e **Sair**. O ícone reflete normal/atenção (setup pendente ou container unhealthy)/erro (daemon
indisponível); fechar a janela esconde para a bandeja; a segunda instância foca a primeira; e o
`SetupWindow` nunca é escondido para o tray. Pendentes: submenus de Machines/Kubernetes (M4/M5) e
“Verificar atualizações” (T7-06).

---

## 10. Design tokens

### 10.1 Cores (dark)

| Token | Valor | Uso |
|---|---|---|
| `bg.base` | `#0F1115` | fundo da janela |
| `bg.surface` | `#161A21` | cards/painéis |
| `bg.elevated` | `#1D232C` | diálogos/hover |
| `border.subtle` | `#2A313C` | divisórias |
| `text.primary` | `#E6EAF0` | texto |
| `text.secondary` | `#9AA4B2` | metadados |
| `accent` | `#6366F1` | ações primárias, links, foco |
| `accent.strong` | `#3730A3` | marca, botões preenchidos, seleção |
| `accent.soft` | `#A5B4FC` | hover suave, detalhes |
| `accent.cyan` | `#22D3EE` | "ativo/rodando", destaque de marca |
| `success` | `#3FB950` | running/ok |
| `warning` | `#D29922` | atenção |
| `danger` | `#F85149` | erro/parado |
| `info` | `#58A6FF` | links |

> Paleta de marca, logo e regras de uso: [`branding.md`](branding.md). O ciano não deve ser usado como texto pequeno sobre fundo claro.

- **Seleção:** fundo `accent.soft` + texto normal; sem barra vertical ou outline de acento.

### 10.2 Cores (light)

`bg.base #F6F8FA`, `bg.surface #FFFFFF`, `border #D0D7DE`, `text.primary #1F2328`, `text.secondary #57606A`. Acento `#3730A3` (texto/botão) e `#6366F1` (hover); sucesso/atenção/erro com contraste AA. Cyan `#22D3EE` apenas como indicador gráfico, nunca texto.

### 10.3 Tipografia

- UI: `Inter`/`Noto Sans` (fallback system-ui), 13 px base, 1.45 line-height.
- Monoespaçada: `JetBrains Mono`/`Noto Sans Mono`, 11 px (terminal/logs).
- Escala: 10.5, 11, 12, 13, 15, 19, 23, 24 px.
- **Piso de legibilidade:** 11 px para metadados; 10.5 px só para rótulos decorativos; nunca usar 9–10 px para informação necessária.
- Títulos: 650–700 com letter-spacing -0.02 a -0.03em; labels em caixa alta com +0.08em.
- Respeitar escala do sistema (não fixar px absolutos no QML; usar `pointSize`/`fontMetrics`).

### 10.4 Espaçamento e raio

- Grid 4 px: 4, 8, 12, 16, 24, 32.
- Raio: 6 px (controles), 10 px (cards), 14 px (diálogos).
- Sombras discretas; sem glassmorphism.

### 10.5 Ícones

- Conjunto próprio (SVG 16/20/24 px), traço 1.5.
- Ícones de distro originais/simples (círculo + letra) para evitar marcas.
- Status sempre com forma + cor.
- Marca: **cápsula** (principal) e **camadas** (secundária); em 16–24 px usar a cápsula compacta; **não** usar o monograma P. Ver [`branding.md`](branding.md).

---

## 11. Componentes QML

```
qml/
├── Main.qml
├── Theme.qml            # singleton de tokens
├── NavItem.qml
├── ViewHeader.qml
├── CatalogList.qml
├── AppIcon.qml
├── AppBadge.qml
├── ActionButton.qml
├── FormField.qml
├── FormCheck.qml
├── ContainersPage.qml
├── Inspector.qml
├── ContainerDetail.qml   # conteúdo do overlay de detalhe (9 abas, inclui Terminal)
├── LogsView.qml
├── StatsView.qml
├── Sparkline.qml
├── FilesView.qml
├── ImagesView.qml
├── VolumesView.qml
├── NetworksView.qml
├── PodsView.qml
├── ComposeView.qml
├── BuildsView.qml
├── Toast.qml
└── Setup/               # implementado em src/gui/qml (sem subpasta)
    ├── SetupWindow.qml    # 7 etapas + progresso/log + confirmações
    ├── SetupRail.qml      # rail de etapas
    └── SetupCheckRow.qml  # check com status/versão/remediação
```

- `qt_add_qml_module`; `qmllint` no CI.
- Modelos C++ expostos via `Q_PROPERTY`/`Q_INVOKABLE`; QML só apresentação.
- `QQmlApplicationEngine` com `setDesktopFileName("dev.podstack.PodStack")`.

---

## 12. Atalhos

| Atalho | Ação |
|---|---|
| `Ctrl+N` | novo (contexto da seção) |
| `Ctrl+Shift+N` | nova janela |
| `Ctrl+T` | nova aba |
| `Ctrl+W` | fechar aba |
| `Ctrl+,` | Settings |
| `Ctrl+B` | alternar sidebar |
| `Ctrl+Shift+B` | alternar inspector |
| `Ctrl+F` | buscar na seção |
| `Ctrl+Shift+F` | busca global |
| `F5` | atualizar |
| `Delete` | remover com confirmação |
| `Ctrl+Delete` | remover sem confirmação (se permitido) |
| `Ctrl+Enter` | iniciar/parar selecionado |
| `Ctrl+Shift+L` | abrir logs |
| `Ctrl+Shift+T` | abrir terminal |
| `Esc` | desselecionar/fechar (inclui o overlay de detalhe do container) |
| `Ctrl+Q` | sair |

Atalhos configuráveis (P2); conflitos com o desktop são resolvidos pelo usuário.

---

## 13. Acessibilidade

- Todos os controles com `Accessible.name`/`description`.
- Ordem de tab lógica; foco visível (outline 2 px accent).
- Contraste AA em ambos os temas.
- Suporte a leitor de tela (AT-SPI); status anunciado.
- `prefers-reduced-motion` respeitado (sem animações longas).
- Zoom 100–200% sem quebra; layout reflui.
