# Design System — PodStack UI Concept

> Fonte visual da POC. Implemente no app **via tokens e componentes QML**, sem copiar HTML/CSS.
> O shell do desktop (barra do GNOME, dock, titlebar) é moldura da demo; **não faz parte do app**.

---

## 1. Princípios

1. **Denso, mas respirável.** Listas compactas (42–56 px), painéis com 14 px de padding e hierarquia clara.
2. **Escuro primeiro.** O tema claro é suportado por tokens; o escuro é o padrão.
3. **Pouca cor, com significado.** Índigo/ciano para marca e seleção; verde/âmbar/vermelho apenas para estado.
4. **Superfícies em camadas.** Fundo → superfície → superfície elevada; bordas de 1 px em vez de sombras fortes.
5. **Movimento discreto.** 160 ms para hover/seleção; 240 ms para janelas/modais; nada de animação decorativa longa.
6. **Nada de barras de acento laterais.** Seleção é fundo suave (`accent-soft`), não um traço vertical.
7. **Texto é interface.** Rótulos em caixa alta só para metadados (10–10.5 px); ações em sentence case.

---

## 2. Cores

### 2.1 Tema escuro (padrão)

| Token POC | Hex / valor | Uso | Token QML sugerido |
|---|---|---|---|
| `--bg` | `#080a0f` | fundo do app | `Theme.bgBase` |
| `--bg-sidebar` | `#0a0c11` | sidebar e statusbar | `Theme.bgSidebar` |
| `--surface` | `#0e1118` | painéis, cards, janela | `Theme.bgSurface` |
| `--surface-2` | `#12161f` | inputs, cabeçalhos, tabs | `Theme.bgSurface2` |
| `--surface-3` | `#171c27` | hover, segmentado ativo | `Theme.bgElevated` |
| `--surface-4` | `#1c2230` | hover forte | `Theme.bgHover` |
| `--border` | `#1d2330` | divisórias | `Theme.border` |
| `--border-strong` | `#2a3242` | foco/hover de borda | `Theme.borderStrong` |
| `--text` | `#e7eaf0` | texto principal | `Theme.textPrimary` |
| `--text-2` | `#9aa3b5` | texto secundário | `Theme.textSecondary` |
| `--text-3` | `#6b7488` | metadados, placeholders | `Theme.textTertiary` |
| `--text-4` | `#4d5668` | desabilitado | `Theme.textDisabled` |
| `--accent` | `#6366f1` | ação primária/seleção | `Theme.indigo500` |
| `--accent-2` | `#818cf8` | ícones ativos/links | `Theme.indigo400` |
| `--accent-soft` | `rgba(99,102,241,.12)` | fundo de seleção | `Theme.accentSoft` |
| `--indigo` | `#3730a3` | marca (logo) | `Theme.indigo700` |
| `--cyan` | `#22d3ee` | destaque, CPU | `Theme.cyan` |
| `--success` | `#34d399` | running, OK | `Theme.success` |
| `--success-soft` | `rgba(52,211,153,.12)` | badge/dot halo | `Theme.successSoft` |
| `--warning` | `#fbbf24` | atenção, partial | `Theme.warning` |
| `--warning-soft` | `rgba(251,191,36,.12)` | badge/aviso | `Theme.warningSoft` |
| `--danger` | `#f87171` | erro, destrutivo | `Theme.danger` |
| `--danger-soft` | `rgba(248,113,113,.12)` | badge/erro | `Theme.dangerSoft` |
| `--terminal-bg` | `#0a0d13` | terminal/log | `Theme.terminalBg` |
| `--scroll` | `#2a3344` | scrollbar | `Theme.scrollbar` |
| `--selection` | `rgba(99,102,241,.28)` | seleção de texto | `Theme.selection` |

### 2.2 Tema claro

| Token POC | Hex / valor | Uso |
|---|---|---|
| `--bg` | `#f5f6fa` | fundo do app |
| `--bg-sidebar` | `#ffffff` | sidebar |
| `--surface` | `#ffffff` | painéis |
| `--surface-2` | `#f7f8fc` | inputs/cabeçalhos |
| `--surface-3` | `#eef0f7` | hover/ativo |
| `--surface-4` | `#e7eaf3` | hover forte |
| `--border` | `#e4e7ef` | divisórias |
| `--border-strong` | `#d3d8e4` | borda forte |
| `--text` | `#171a22` | texto |
| `--text-2` | `#5b6475` | secundário |
| `--text-3` | `#8a92a4` | metadados |
| `--text-4` | `#aab1c0` | desabilitado |
| `--accent` | `#5b5ee8` | ação primária |
| `--accent-2` | `#4f46e5` | ícones ativos |
| `--accent-soft` | `rgba(91,94,232,.10)` | seleção |
| `--cyan` | `#0891b2` | destaque/CPU |
| `--success` | `#059669` | running/OK |
| `--warning` | `#b45309` | atenção |
| `--danger` | `#dc2626` | erro |
| `--terminal-bg` | `#141821` | terminal (mantém escuro) |

### 2.3 Regras de cor

- **Seleção**: fundo `accent-soft` + texto normal; **sem** barra vertical, **sem** outline de 1 px.
- **Status**: sempre **ponto + texto/badge**; nunca só cor.
- **Primária**: gradiente `#6d70f5 → #5b5ee8`; hover com `brightness(1.06)`; sombra `0 4px 14px rgba(99,102,241,.25)`.
- **Destrutiva**: texto/borda vermelha; fundo só no hover (`danger-soft`); confirmação obrigatória.
- **Aviso**: fundo âmbar translúcido (6–10%) + borda 24–30% + ícone âmbar.
- **Gráficos**: CPU `cyan`; RAM `indigo400` (`#818cf8`); área com 10% de opacidade.

---

## 3. Tipografia

**Família:** `Inter` (Google Fonts), pesos **400 / 450 / 500 / 600 / 700**.
**Fallback:** `system-ui, -apple-system, "Segoe UI", Roboto, sans-serif`.
**Mono:** `ui-monospace, SFMono-Regular, "SF Mono", Menlo, Consolas, monospace`.
No app: empacotar `Inter` em `assets/fonts/` (licença OFL) e carregar com `FontLoader`; não depender da rede.

| Papel | Tamanho | Peso | Altura | Uso | Classe POC |
|---|---:|---:|---:|---|---|
| Hero | 24 px | 650–700 | 1.15 | setup “Vamos preparar”, “Tudo pronto” | `.setup-hero h1`, `.setup-done h1` |
| Título de tela | 23 px | 650 | 1.15 | “Containers”, “Images”, … | `.view-title` |
| Título de seção | 19 px | 650 | 1.2 | etapas do setup | `.setup-h2` |
| Título de painel | 12.5–14 px | 600–650 | 1.3 | inspector, painéis | `.panel-title`, `.inspector-title` |
| Corpo | 13 px | 400 | 1.45 | texto geral | `body` |
| Nome de linha | 13 px | 600 | 1.3 | containers, tabelas | `.row-name`, `.repo` |
| Botão | 12 px | 550 | 1.2 | ações | `.btn` |
| Metadado | 11 px | 400 | 1.4 | imagem, portas, detalhes | `.row-meta`, `.meta-value` |
| Rótulo | 10.5 px | 650 | 1.2 | caixa alta, letter-spacing .08em | `.meta-label`, `.section-label`, `th` |
| Micro | 10.5–11 px | 400 | 1.4 | statusbar, hints, gráficos | `.statusbar`, `.hint`, `.spark-head` |
| Mono | 10.5–11 px | 400 | 1.7 | IDs, logs, comandos | `.mono`, `.terminal` |

**Detalhes:** títulos com `letter-spacing: -0.02em` a `-0.03em`; labels com `+0.08em`; números em tabelas e métricas com `font-variant-numeric: tabular-nums` no app.

**Piso de legibilidade:** em desktop, **11 px é o mínimo para metadados**; 10–10.5 px só para rótulos decorativos (não para informação necessária). A POC foi ajustada para esse piso; mantenha-o no QML e teste com escala fracionária (1,25/1,5).

---

## 4. Espaçamento, raios e sombras

**Escala:** 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 24, 28, 32 px.

| Token | Valor | Uso |
|---|---:|---|
| `--radius-xs` | 6 px | botão pequeno, tab, chip |
| `--radius-sm` | 8 px | input, botão, tab container |
| `--radius` | 10 px | card, painel menor |
| `--radius-lg` | 14 px | painel principal, modal, janela |
| pill | 999 px | badge, pill, toggle |

| Sombra | Valor | Uso |
|---|---|---|
| `--shadow-sm` | `0 1px 2px rgba(0,0,0,.28), 0 6px 18px rgba(0,0,0,.18)` | painéis |
| `--shadow` | `0 18px 50px rgba(0,0,0,.38)` | toast/modal |
| `--shadow-accent` | `0 8px 30px rgba(99,102,241,.18)` | ação primária |
| janela | `0 30px 90px rgba(0,0,0,.68), 0 8px 24px rgba(0,0,0,.42)` | janela da demo |
| modal | `0 30px 90px rgba(0,0,0,.55)` | modal do app |

No QML, prefira `MultiEffect` (shadow/blur) em vez de várias camadas de `Rectangle`.

---

## 5. Layout

### 5.1 App (real)

```text
┌───────────────┬──────────────────────────────────────────────┐
│ Sidebar 236px │ Topbar 58px                                  │
│               ├──────────────────────────────────────────────┤
│               │ Main (scroll)                                │
│               │  view-head + toolbar + conteúdo              │
│               │                                              │
│               ├──────────────────────────────────────────────┤
│               │ Statusbar 30px                               │
└───────────────┴──────────────────────────────────────────────┘
```

- Sidebar: 236 px; 196 px abaixo de 980 px; padding 14/12; grupos com label de 10 px.
- Topbar: 58 px; breadcrumb + busca (270×34) + tema + engine pill.
- Main: padding 20/22/30; `view-head` + `toolbar` + grid.
- Statusbar: 30 px; 11 px; conexão + contagens + rootless + licença.
- Containers: grid `minmax(0,1fr) 366px`; abaixo de 1120 px, inspector 330; abaixo de 980 px, coluna única.
- Builds: grid `1fr 380px`; Machines: mesma lógica de containers.
- Dialogs: 440–720 px; setup 1020×680.

### 5.2 Shell da demo (não implementar)

```text
Barra do GNOME 30px
┌──────────────────────────────────────────────────────────────┐
│ Atividades · Configurar máquina        relógio        tray  │
├──────────────────────────────────────────────────────────────┤
│  wallpaper                                                   │
│   ┌──────────────────────────────────────────────────────┐   │
│   │ titlebar 36px · PodStack — Tela        – □ ×          │   │
│   ├──────────────────────────────────────────────────────┤   │
│   │ app (sidebar/topbar/main/statusbar)                  │   │
│   └──────────────────────────────────────────────────────┘   │
├──────────────────────────────────────────────────────────────┤
│ dock 66px                                                    │
└──────────────────────────────────────────────────────────────┘
```

A janela da demo tem 1240×820 (máx. 900), raio 12, borda `rgba(255,255,255,.10)`. Os controles de janela são **simulação**; o app real usa a janela nativa.

---

## 6. Componentes

### 6.1 Sidebar

| Elemento | Especificação |
|---|---|
| Brand | cápsula 32×21, título 15/700, versão 11 `text-3`; padding 4/8/18 |
| Grupo | label 10/650 caixa alta, `text-3`, padding 0/10/6 |
| Item | 34 px; gap 10; padding 0/10; raio 8; ícone 16; texto 13/500; count pill 18×… |
| Item ativo | fundo `accent-soft`; texto `text`; ícone `accent-2`; **sem** barra/outline |
| Hover | `rgba(255,255,255,.05)` |
| Rodapé | Settings; sem toggle de tema (o tema fica no header) |

**Grupos:** **Engine** (Containers, Images, Volumes, Networks, Compose, Builds, Pods) e **Linux** (Machines, Devices). Pods do Podman são grupos de containers do engine, **não** Kubernetes; clusters/k3s ficam em um grupo próprio quando existirem.

### 6.2 Topbar

- Breadcrumb 12.5 px: `PodStack / <Tela>`.
- Busca: 270×34; raio 8; `surface-2`; borda; foco com borda accent + anel `accent-soft` 3 px.
- Tema: botão ícone 32 (sol/lua), título “Alternar tema”.
- Engine pill: 28 px; dot 6; texto 11/550; `surface-2` + borda.

### 6.3 Botões

| Variante | Altura | Padding | Raio | Fonte | Cores |
|---|---:|---:|---:|---|---|
| Primário | 32 | 0 12 | 8 | 12/550 | gradiente indigo; texto branco; sombra accent |
| Secundário | 32 | 0 12 | 8 | 12/550 | `surface-2`; borda; hover `surface-3` |
| Ghost | 32 | 0 12 | 8 | 12/550 | transparente; hover `surface-2` |
| Perigo | 32 | 0 12 | 8 | 12/550 | texto `danger`; hover `danger-soft` |
| Ícone | 32×32 | 0 | 8 | — | como secundário |
| Pequeno | 27 | 0 9 | 6 | 11/550 | como secundário |
| Pequeno ícone | 27×27 | 0 | 6 | — | — |

Estados: hover (fundo + borda), active (`translateY(1px)`), disabled (opacidade .5, sem hover), foco (anel accent). Ícones 14 px.

### 6.4 Campos

- Input/select: 33 px; raio 8; `surface-2`; borda; texto 12; placeholder `text-3`; foco accent + anel.
- Label: 9/650 caixa alta, `text-3`, gap 5.
- Textarea: 72 px; mono 11.
- Checkbox: `accent-color: accent`; label 11; opção com borda e padding 9/11.

### 6.5 Badges e status

- Badge: 19 px; padding 0 6; raio 999; 11/600; borda ou fundo suave.
  - `success` (verde), `warning` (âmbar), `danger` (vermelho), `accent` (índigo), `muted` (cinza).
- Dot: 8 px; running/success com halo 3 px; stopped `text-3`; warning/danger com halo.
- Sempre acompanhar o dot de um texto/badge.

### 6.6 Tabs e segmentado

- Tabs (filtros): container `surface-2` + borda, padding 3, raio 8; botão 26 px, padding 0 10, raio 6, 11/550; ativo `surface-3` + texto; contagem 11.
- Segmentado (inspector): 3 px; botão 24 px; 10/550; ativo `surface-3`.

### 6.7 Painéis

- `surface`, borda 1, raio 14, `shadow-sm`; `panel-head` 10/14 + borda; título 12.5/600; sub 11/400 `text-3`.
- `panel-body` 14; `panel-foot` 10/14, 11 px `text-3`.
- Listas: `max-height: calc(100vh - 260px)` e scroll interno na demo; no app, o `ListView` ocupa o espaço disponível.

### 6.8 Linhas de lista (Containers)

- Grid: `10px minmax(0,1fr) auto`; gap 12; padding 10/14; altura ~56; borda inferior.
- Nome 13/600 + badge de saúde + projeto; meta 11 `text-3` (imagem · portas).
- Status à direita 11; ações (parar/iniciar/reiniciar) aparecem no hover (32 px; ícones 14).
- Hover `rgba(255,255,255,.03)`; selecionado `accent-soft`; **sem** barra lateral.
- Ícone de status à esquerda; clique seleciona; duplo clique pode abrir terminal (ideia).

### 6.9 Tabelas

- Header 10.5/650 caixa alta `text-3`; padding 9/14; borda inferior.
- Células 11; padding 10/14; linha com borda inferior; hover 2.5% do texto.
- Repositório 12/600; digest mono 11 `text-3`.
- Ações à direita, ocultas até hover (catálogo); **Devices** mantém ações sempre visíveis.
- Seleção (Machines): fundo `accent-soft`, **sem** barra.
- Colunas: ver [SCREENS.md](SCREENS.md).

### 6.10 Inspector (Containers)

- Largura 366 (330 @1120); sticky no topo; `panel`.
- Header 13/14: título 14/650 + badge de estado; sub 11 `text-3`; ações (Parar/Iniciar, Reiniciar, Terminal, Remover) em `btn sm`.
- Segmentado Info/Logs/Stats/Files.
- Corpo 13/14.
- **Info:** grid 2 colunas; labels 10.5 caixa alta; valores 11; seções (Saúde, Variáveis, Mounts) com 10.5 caixa alta; código em bloco `terminal-bg` mono 11.
- **Logs:** toolbar (filtrar, seguir, limpar) + terminal 300 px.
- **Stats:** 4 cards 2×2 (CPU, Memória, Rede RX/TX, Disco/PIDs); gráfico CPU + gráfico RAM (56 px cada); atualização 2 s; 30 amostras; detalhes em §6.11.
- **Files:** breadcrumb `/`, botão Enviar, linhas com ícone/nome/tamanho/modo; 22 px ícone; hover.

### 6.11 Cards de estatística e gráficos

- Card: `surface-2`, borda, raio 8, padding 9/10; label 8 caixa alta; valor 13.5/600; foot 9 `text-3`; CPU em `cyan`.
- `.charts`: grid gap 8; `.spark-wrap` com borda/raio 8, `surface-2`, padding 7/10/3.
- Cabeçalho do gráfico: 10.5 caixa alta `text-3`, valor à direita.
- Sparkline: SVG 300×56 (viewBox 300×64); CPU `#22d3ee`, RAM `#818cf8`; área 10%; 30 pontos; `preserveAspectRatio="none"`.
- RAM usa escala dinâmica (`min*0.94`, `max*1.04`) para mostrar variação; CPU usa 0–max (mín. 5).
- No app: `Sparkline.qml` com `Canvas` ou `Shape`; histórico no Backend/`StatsModel`; timer de 2 s.

### 6.12 Terminal / logs

- `terminal-bg`, borda, raio 8; mono 11; altura 300 (inspector), 360 (modal), 140 (setup).
- Linha: timestamp `#596377`, nível 34 px (`INFO` ciano, `WARN` âmbar, `ERR` vermelho, `OK` verde), mensagem `#a7b0c2`.
- Cursor 6×11 piscando; auto-scroll no fim.
- Logs de build: mesmas cores; steps `STEP n/m`.
- No app: reutilizar `LogsView`/`StreamClient`; `ListView` com `Text` mono; limite de 5.000 linhas.

### 6.13 Files

- Linha 22/1fr/auto/auto; padding 7/8; hover `surface-2`; borda inferior.
- Ícone 16 `text-3`; nome 11; tamanho/modo mono 10.5.
- Clique em arquivo baixa; pasta navega (na POC, apenas toast).

### 6.14 Cards de Compose

- Grid `repeat(auto-fill, minmax(320px,1fr))` gap 12; padding 14.
- Nome 13.5/650 + dot de status; diretório mono 11 `text-3`.
- Serviços em chips 21 px mono 10.5.
- Ações Up/Restart/Logs + Down (danger) alinhadas; foot 9 `text-3`.

### 6.15 Builds

- Grid `1fr 380px`; tabela + painel de detalhe.
- Status: success verde, failed vermelho, running ciano, cancelled cinza.
- Barra de progresso 4 px (6 px no setup) com gradiente accent→cyan.
- Log 300 px; meta (imagem, duração, contexto, Dockerfile).

### 6.16 Toasts

- Stack no topo central (44 px do desktop; no app, 8 px abaixo do topbar).
- Largura 260–520; padding 10/13; raio 8; `surface-3` 92% + blur; borda forte; sombra.
- Ícone 15; kinds: success/warning/danger/info; animação 240 ms; duração 3,2 s; empilhável.

### 6.17 Modais / diálogos

- Backdrop `rgba(4,6,10,.62)` + blur 8; centralizado; largura 540 (padrão) ou 720 (wide); raio 14; borda forte; sombra.
- Head 16/18; título 15/650; sub 11 `text-3`; body 16/18; foot 12/18 com fundo `surface-2`.
- Formulário: grid 2 colunas; labels 11 caixa alta; inputs 33; hint 11.
- Ações: Ghost “Cancelar” + primária; confirmação destrutiva em vermelho.
- Fechar por Esc, clique no backdrop e botão X.

### 6.18 Setup (resumo)

Detalhes completos em [ONBOARDING.md](ONBOARDING.md). Componentes:

- Janela 1020×680, titlebar 38, rail 220, content com padding 24/28, footer 12/20.
- Rail: passo 10/11, dot 24, label 12/600, hint 11; ativo `accent-soft`; concluído verde.
- Checks: linha 9/12; ícone 22; label 11.5/600; detalhe 11; badge.
- Card: padding 14; borda; raio 10; warn/success.
- Log: terminal 140 px; progresso 6 px.
- Options: checkboxes; linha 9/11; badge.
- Done: ícone 56; resumo 2×3.

### 6.19 Shell do desktop (demo)

- Barra 30 px, fundo `rgba(6,8,14,.58)` + blur; “Atividades”, “Configurar máquina”, relógio, tray.
- Janela: 1240×820, raio 12, borda branca 10%, sombra forte; titlebar 36; controles 24 circulares; fechar vermelho no hover.
- Dock 66; ícones 44×44, raio 12, `rgba(20,24,33,.74)` + blur; app com borda accent e dot.
- **Não implementar**: é apenas encenação de desktop.

---

## 7. Iconografia

- Estilo **Lucide** (stroke 1.7, round, 24×24 viewBox), monocromático `currentColor`.
- Tamanhos: 12 (badges), 14 (botões), 16 (nav/linha), 20 (dock).
- Conjunto usado: containers (box), images (layers), volumes (database), networks (share), compose (grid), builds (hammer), pods (hexagon), machines (monitor), devices (plug), settings, search, plus, refresh, play, stop, rotate, trash, terminal, folder, file, cpu, memory, activity, drive, chevron, x, sun, moon, check, alert, copy, external, more, upload, download, filter, shield, wifi, volume, battery.
- No QML: empacotar SVGs em `assets/icons/` e usar `IconImage` com `color` (ou `MultiEffect`/`ColorOverlay`); não usar emoji.
- Ícones de ação sempre com `Accessible.name`/`title`.

---

## 8. Movimento

| Situação | Duração | Curva |
|---|---:|---|
| Hover/seleção | 160 ms | `cubic-bezier(.2,.8,.2,1)` |
| Toast | 240 ms in / 200 ms out | ease |
| Modal | 200–240 ms | popIn |
| Janela maximizar/minimizar | 220 ms | ease |
| Setup step (carrossel) | 260 ms | slide 16 px + fade |
| Gráficos | 400 ms no width | ease |

- `prefers-reduced-motion` remove animações não essenciais.
- No QML: `Behavior on color`, `NumberAnimation`, `MultiEffect` com `blur`/`shadow`; evitar animações contínuas.

---

## 9. Acessibilidade

- **Contraste**: corpo em `text`/`text-2`; `text-3` só para metadados não essenciais; não usar 10 px para informação crítica.
- **Foco**: anel de 3 px `accent-soft` + borda accent; navegação por teclado; `Esc` fecha modal/setup; `/` foca a busca.
- **Alvos**: mínimo 32×32; linhas inteiras clicáveis; ações de hover também acessíveis por menu/teclado.
- **Semântica**: botões reais, labels associadas, `aria-label` na POC; no QML `Accessible.name`/`description`/`role`.
- **Status**: nunca só cor; dot + texto/badge.
- **Texto**: pt-BR; sentence case; verbos de ação; erros com mensagem + detalhe + ação recomendada.

---

## 10. Conteúdo e tom

- Títulos: “Containers”, “Images”, “Volumes”, “Networks”, “Compose”, “Builds”, “Pods”, “Machines”, “Devices”.
- Labels: caixa alta, curtos (`CPU`, `MEMÓRIA`, `REDE (RX / TX)`, `DISCO / PIDS`, `STATUS`, `SAÚDE`, `MOUNTS`, `PORTAS`).
- Ações: “Novo container”, “Start all”, “Pull”, “Prune”, “Remover”, “Conectar”, “Desconectar”, “Validar instalação”.
- Mensagens de erro: “Não foi possível conectar ao podstackd.” + detalhe + “Inicie com: …”.
- Evite: “Ops!”, “Sucesso!”, emojis, texto em caixa alta em parágrafos, jargão sem explicação.

---

## 11. Do / Don't

| Faça | Não faça |
|---|---|
| Usar tokens (`Theme.qml`) | Hardcode de hex/px nos componentes |
| Seleção com fundo suave | Barra/acento vertical azul |
| Gradiente só na ação primária e logo | Gradientes em cards/fundos |
| Ícones Lucide 1.7 | Misturar famílias/emoji |
| Tabelas densas com hover | Cards gigantes para listas longas |
| Um único botão primário por contexto | Vários primários competindo |
| Texto curto e específico | “Lorem ipsum” ou labels vagos |
| Shadow/blur discretos (`MultiEffect`) | Sombras fortes em tudo |
| Foco visível | Remover outline sem substituto |
