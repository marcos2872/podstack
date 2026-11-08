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
│ Sidebar (220) │ Lista (flex)                         │ Inspector (360)     │
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
| Containers | nome, status, imagem, portas | criar, start/stop all, lote por seleção, terminal |
| Images | repositório:tag, digest, tamanho, uso | pull, importar, tag, untag, prune, remover |
| Volumes | nome, driver, tamanho, links | criar, clone, importar do host, prune, remover |
| Networks | nome, driver, subnet, containers | criar, conectar, desconectar, remover |
| Pods (Podman) | nome, status, containers, ID | criar, start, stop, restart, remover |
| Compose | projeto, status, serviços, diretório | up, down, restart, logs, abrir YAML/diretório |
| Builds | imagem, status, duração, origem | novo build, cancelar, remover, log |
| Services (K8s) | planejado (M5) | — |
| Machines | planejado (M4) | — |
| Devices | tipo, nome, VID:PID, status | attach/detach |

### 4.3 Estados

- **Vazio**: ilustração leve + título + ação primária + dica de CLI.
- **Carregando**: skeleton de 6 linhas.
- **Erro**: mensagem + `Tentar novamente` + `Ver diagnóstico`.
- **Sem permissão**: explicação + como habilitar (rootless/rootful).
- **Filtro sem resultado**: "Nenhum item corresponde" + limpar filtro.

---

## 5. Inspector

Abas por tipo (estado do M2):

| Aba | Container | Machine | Volume | Image | Observação |
|---|---|---|---|---|---|
| **Info** | ✅ | planejado | planejado | planejado | nome, imagem, portas, estado, health, ações |
| **Logs** | ✅ | planejado | — | — | streaming/follow, busca, wrap, timestamps |
| **Stats** | ✅ | planejado | — | — | CPU, memória, rede, disco/PIDs + gráfico |
| **Files** | ✅ | planejado | planejado | — | listar e baixar para `~/Downloads` |
| **Terminal** | janela nativa | planejado | — | — | QTermWidget; embutir no inspector no M3 |
| **Inspect** | CLI `podstack inspect` | — | — | — | aba GUI no M2 |

Ações da GUI no M3: criar container (diálogo), start/stop/restart/pause/unpause/kill/remove, renomear, duplicar, abrir porta, abrir terminal, `Start all`/`Stop all`, multi-seleção (`Ctrl`/`Shift` + lote), upload para containers, e Settings (rootless/rootful). O catálogo tem seções navegáveis para Images (pull/importar/tag/untag/prune/remover), Volumes (criar/clone/importar do host/prune/remover), Networks (criar/conectar/desconectar/remover) e Pods (criar/start/stop/restart/remover). **Compose** agrupa projetos por labels, sobe/derruba via provider, abre YAML/diretório e assina logs por serviço ou agregados. **Builds** inicia builds via Libpod, mostra progresso/steps e mantém histórico em SQLite. **Eventos globais** aparecem como toasts no topo. A barra de status mostra a conexão com o daemon, contagens de containers/imagens/volumes e a licença. Machines, Services e Devices permanecem desabilitados até os marcos seguintes.

### 5.1 Info

- Cabeçalho: ícone, nome, status, ações (start/stop/restart/delete).
- Grid de metadados copiáveis: imagem, comando, portas, env, mounts, rede/IP, labels, health, criado/uptime.
- Ações: abrir no navegador, copiar ID, revelar no gerenciador.

### 5.2 Stats

- Cards: CPU %, memória (usado/limite), rede RX/TX, disco R/W, PIDs.
- Gráfico de linha (60 s) com tooltip; atualização 2 s.
- Botão pausar/retomar.

### 5.3 Logs

- Toolbar: follow (toggle), busca, filtro (só matching), wrap, timestamps, pretty JSON, limpar, exportar.
- Lista virtualizada com cores por stream (stdout neutro, stderr vermelho suave).
- Auto-scroll pausa ao subir; badge "novas linhas".

### 5.4 Terminal

- Emulação completa (cores, cursor, seleção, copy/paste, mouse).
- Tabs de sessão; título = comando/shell; indicador de conexão.
- Menu: copiar tela/seleção (texto/ANSI/HTML), reset, fonte, fechar.
- Fallback: "Abrir no terminal externo".

### 5.5 Files

- Breadcrumb + listagem (nome, tamanho, modo, mtime).
- Ações: abrir, baixar, enviar, criar pasta, renomear, apagar, copiar caminho.
- Editor externo com save-back opcional.

### 5.6 Inspect

- JSON viewer com árvore, busca, copiar, exportar.
- **Diferencial PodStack** (não existe como aba no OrbStack).

---

## 6. Diálogos

| Diálogo | Campos | Validação |
|---|---|---|
| Criar container | imagem, nome, comando, portas, env, volumes, workdir, user, flags, recursos | imagem existente/pull; nome único; portas válidas |
| Criar rede | nome, driver, subnet4, gateway, IPv6, labels | CIDR válido; sem sobreposição |
| Criar máquina | distro/versão, nome, arch, CPU/mem/disco, user/senha, user-data, mounts, isolado, SSH agent | recursos mínimos; nome único; YAML válido |
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
└── Setup/               # planejado (T7-07)
    ├── SetupWindow.qml
    ├── SetupRail.qml
    ├── SetupStep.qml
    ├── SetupCheckRow.qml
    ├── SetupProgress.qml
    ├── SetupLog.qml
    └── SetupDone.qml
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
| `Esc` | desselecionar/fechar |
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
