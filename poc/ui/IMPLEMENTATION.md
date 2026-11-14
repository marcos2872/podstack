# Implementação em Qt 6/QML — PodStack UI Concept

> Como traduzir a POC para o app real, sem copiar HTML/CSS e sem perder os recursos existentes.
> Leia [DESIGN-SYSTEM.md](DESIGN-SYSTEM.md), [SCREENS.md](SCREENS.md) e [ONBOARDING.md](ONBOARDING.md) antes de editar.

---

## 1. Princípios

1. **A POC é visual; a spec é comportamento.** PRD/spec/tasks/RPC continuam a fonte de verdade.
2. **Não reescrever o backend.** GUI/CLI continuam clientes do `podstackd`; o `PodmanClient`/RPC já cobrem containers, imagens, volumes, redes, pods, compose e builds.
3. **QML só apresentação.** Lógica, streams e jobs no C++/daemon; QML não fala com o Podman.
4. **Tokens, não hex.** Cores, raios, espaçamentos e fontes vêm de `Theme.qml`; componentes não hardcodam.
5. **Sem shell de desktop.** Barra do GNOME, dock, wallpaper e controles de janela da POC são encenação; o app usa a janela nativa.
6. **Sem seletor de cenário.** O onboarding real detecta o estado; não há “cenário de demonstração”.
7. **Acessibilidade e i18n desde o início.** `qsTr()`, `Accessible.*`, foco e atalhos.

---

## 2. Arquitetura atual (M3)

```text
podstack-gui (QML) ──Backend/StreamClient──┐
podstack (CLI) ─────RpcClient/RpcStream─────┤
                                           ▼
                                  podstackd (JSON-RPC)
                                           │
                                  PodmanClient (libcurl)
                                           ▼
                                  Podman API (/libpod)
```

- GUI: `src/gui/qml/` (`Main.qml`, `ContainersPage.qml`, `Inspector.qml`, `ImagesView.qml`, `VolumesView.qml`, `NetworksView.qml`, `ComposeView.qml`, `BuildsView.qml`, `PodsView.qml`, `CatalogList.qml`, `Theme.qml`, `ActionButton.qml`, `FormField.qml`, `FormCheck.qml`, `LogsView.qml`, `StatsView.qml`, `FilesView.qml`, `Toast.qml`).
- Backend: `src/gui/Backend.h/.cpp` (Q_PROPERTY/Q_INVOKABLE; QVariantList; watchers; streams).
- Modelos: `ContainerListModel`; `CatalogList` genérico para listas.
- Daemon: `src/daemon/Daemon.cpp` (handlers), `BuildStore` (SQLite), `ComposeManager` (provider), `RpcServer`.
- Streams: `StreamClient` (logs/stats/pull/build/compose logs/events).
- Testes: `tests/test_gui.cpp`, `test_contract.cpp`, `test_rpc.cpp`, `test_cli.cpp`, `test_terminal.cpp`.

---

## 3. Tokens e tema

### 3.1 `Theme.qml`

O `Theme.qml` atual tem poucos tokens (`bgBase`, `bgSurface`, `bgElevated`, `border`, `textPrimary`, `textSecondary`, `indigo700`, `indigo500`, `indigo200`, `cyan`, `success`, `warning`, `danger`, `radius`, `spacing`). Expanda para a POC:

```qml
import QtQuick

QtObject {
    property bool dark: true

    readonly property color bgBase: dark ? "#080a0f" : "#f5f6fa"
    readonly property color bgSidebar: dark ? "#0a0c11" : "#ffffff"
    readonly property color bgSurface: dark ? "#0e1118" : "#ffffff"
    readonly property color bgSurface2: dark ? "#12161f" : "#f7f8fc"
    readonly property color bgElevated: dark ? "#171c27" : "#eef0f7"
    readonly property color bgHover: dark ? "#1c2230" : "#e7eaf3"
    readonly property color border: dark ? "#1d2330" : "#e4e7ef"
    readonly property color borderStrong: dark ? "#2a3242" : "#d3d8e4"
    readonly property color textPrimary: dark ? "#e7eaf0" : "#171a22"
    readonly property color textSecondary: dark ? "#9aa3b5" : "#5b6475"
    readonly property color textTertiary: dark ? "#6b7488" : "#8a92a4"
    readonly property color textDisabled: dark ? "#4d5668" : "#aab1c0"
    readonly property color indigo700: "#3730a3"
    readonly property color indigo500: dark ? "#6366f1" : "#5b5ee8"
    readonly property color indigo400: dark ? "#818cf8" : "#4f46e5"
    readonly property color accentSoft: dark ? Qt.rgba(99/255,102/255,241/255,.12)
                                            : Qt.rgba(91/255,94/255,232/255,.10)
    readonly property color cyan: dark ? "#22d3ee" : "#0891b2"
    readonly property color success: dark ? "#34d399" : "#059669"
    readonly property color successSoft: dark ? Qt.rgba(52/255,211/255,153/255,.12)
                                              : Qt.rgba(5/255,150/255,105/255,.10)
    readonly property color warning: dark ? "#fbbf24" : "#b45309"
    readonly property color warningSoft: dark ? Qt.rgba(251/255,191/255,36/255,.12)
                                              : Qt.rgba(180/255,83/255,9/255,.10)
    readonly property color danger: dark ? "#f87171" : "#dc2626"
    readonly property color dangerSoft: dark ? Qt.rgba(248/255,113/255,113/255,.12)
                                             : Qt.rgba(220/255,38/255,38/255,.10)
    readonly property color terminalBg: dark ? "#0a0d13" : "#141821"

    readonly property int radiusXs: 6
    readonly property int radiusSm: 8
    readonly property int radius: 10
    readonly property int radiusLg: 14
    readonly property int spacing: 8
    readonly property int spacingSm: 6
    readonly property int spacingLg: 14
}
```

- O `Theme` é instanciado (`Theme { id: theme }`) nos componentes; o `dark` pode ser ligado a uma setting (`Config.ui.theme`).
- Atualize **todos** os componentes para usar os novos nomes; não deixe cores antigas hardcoded.
- A mudança de tema deve ser reativa: `theme.dark = config.dark` atualiza os bindings.

### 3.2 Fonte Inter

- Baixe os pesos 400/500/600/700 (SIL OFL) para `assets/fonts/`; adicione ao `qt_add_qml_module`/resources.
- Em `Main.qml`:

```qml
FontLoader { id: interRegular; source: "qrc:/assets/fonts/Inter-Regular.ttf" }
FontLoader { id: interMedium;  source: "qrc:/assets/fonts/Inter-Medium.ttf" }
FontLoader { id: interSemiBold; source: "qrc:/assets/fonts/Inter-SemiBold.ttf" }
FontLoader { id: interBold;    source: "qrc:/assets/fonts/Inter-Bold.ttf" }
```

- Crie `AppText.qml` (ou use `font.family: interRegular.name`) com variantes `title/body/meta/label/mono`; centralize tamanhos/pesos.
- Fallback: `font.family` com `interRegular.name`; se o load falhar, o Qt usa a família padrão.
- Monoespaçado: `"JetBrains Mono"` ou `"monospace"`; empacote se quiser consistência.

### 3.3 Ícones

- Empacote os SVGs Lucide usados (24×24, stroke 1.7) em `assets/icons/`.
- Crie `AppIcon.qml`:

```qml
Item {
    id: root
    property string name
    property int size: 16
    property color color: theme.textSecondary

    implicitWidth: size
    implicitHeight: size

    Image {
        id: image
        anchors.fill: parent
        source: "qrc:/assets/icons/" + root.name + ".svg"
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        fillMode: Image.PreserveAspectFit
        visible: false
    }

    MultiEffect {
        anchors.fill: parent
        source: image
        colorization: 1.0
        colorizationColor: root.color
    }
}
```

- Alternativa: `ColorOverlay` (Qt5Compat) se o `MultiEffect` não estiver disponível; o projeto já tem `qt6-qt5compat-devel`.
- Tamanhos: 12/14/16/20; usar `Accessible.name` no botão que contém o ícone.
- Nunca usar emoji nem ícones do OrbStack.

---

## 4. Componentes QML

### 4.1 Mapa POC → QML

| POC | QML novo/atual | Observações |
|---|---|---|
| `.btn`, `.btn.primary`, `.btn.ghost`, `.btn.danger`, `.btn.sm`, `.btn.icon` | **`AppButton.qml`** (evoluir `ActionButton.qml`) | `variant` (primary/secondary/ghost/danger), `size` (md/sm), `iconName`; altura 32/27. |
| `.badge.*` | **`AppBadge.qml`** | `tone` (success/warning/danger/accent/muted), texto/ícone. |
| `.panel`, `.panel-head`, `.panel-body` | **`AppPanel.qml`** | raio 14, borda, shadow via `MultiEffect`. |
| `.tabs`, `.tab` | **`AppTabs.qml`** | modelo `[{id,label,count}]`, `currentIndex`. |
| `.segmented` | **`AppSegmented.qml`** | abas do inspector. |
| `.input`, `.select`, `.textarea` | **`FormField.qml`** (evoluir) | 33 px, foco accent, label 9 caixa alta. |
| `.setup-check` | **`SetupCheckRow.qml`** | status ok/warn/missing/optional. |
| `.spark` | **`Sparkline.qml`** | Canvas/Shape; 30 pontos; CPU/RAM. |
| `.terminal` | **`LogView.qml`** (ou `LogsView` atual) | mono 11; níveis; auto-scroll; 5k linhas. |
| `.toast` | **`Toast.qml`** (atual) | reposicionar/estilizar; topo do app. |
| `.modal` | **`AppDialog.qml`** | backdrop, 540/720, footer. |
| `.setup-window` | **`SetupWindow.qml`** | janela separada; ver §7. |
| `.row` (containers) | `ContainersPage.qml` + **`ContainerRow.qml`** | 56 px; dot; badges; ações no hover. |
| `.table` | **`AppTable.qml`** + delegates | ver §5. |
| `.compose-card` | `ComposeView.qml` | cards; ações. |
| `.stat-card` | **`StatCard.qml`** | label/valor/foot; accent. |
| `.notice` | **`AppNotice.qml`** | Devices. |
| `.desktop-*`, `.window-*`, `.dock` | **não implementar** | moldura da POC. |

### 4.2 Sidebar/Topbar

- Manter `Main.qml` (ApplicationWindow) e a sidebar; trocar cores/tamanhos pelos tokens.
- Itens: 34 px; ativo `accentSoft`, **sem** barra/outline; ícone 16; count pill.
- Manter os grupos **Engine** (Containers, Images, Volumes, Networks, Compose, Builds, Pods) e **Linux** (Machines, Devices); Pods do Podman não são Kubernetes.
- Adicionar Compose/Builds à sidebar (já existentes no M3) e Machines/Devices quando o backend M4 existir.
- Topbar: 58 px; breadcrumb, busca 270×34, botão de tema (sol/lua) e engine pill.
- A busca deve filtrar a **seção atual** (hoje a busca da GUI filtra containers); estender aos catálogos.

### 4.3 Containers e Inspector

- `ContainersPage.qml`: aplicar o grid `1fr 366px`; lista com dot/badges/status; selecionar primeiro (já feito); multi-seleção (já existe) com fundo `accentSoft`.
- `Inspector.qml`: header/tabs/segmentado; Info; Logs (`LogsView`); Stats; Files (`FilesView`).
- `StatsView.qml`: 4 cards + **dois** `Sparkline` (CPU/RAM); atualizar a cada 2 s; CPU histórico e RAM histórico; limpar ao trocar de container/aba.
- O `Backend` já expõe `stats` e `cpuHistory`; adicionar `memHistory`/`memHistory` parse, ou um `StatsModel` com duas séries.
- O payload de stats do daemon já é normalizado (`cpu_percent`, `mem_usage`, `mem_percent`, `net_io`, `block_io`, `pids`).
- `FilesView` atual: listar/baixar/enviar; estilizar.

### 4.4 Catálogo

- `ImagesView`, `VolumesView`, `NetworksView`, `PodsView`, `ComposeView`, `BuildsView` já existem; substituir `CatalogList`/layouts pelos componentes/tabelas da POC.
- A POC usa tabelas; no QML, prefira **`TableView`** (Qt 6.5+) para colunas redimensionáveis/ordenação, ou `ListView` + `RowLayout` para listas simples (containers/machines).
- Cabeçalho de tabela 10.5/650 caixa alta; células 11.5; hover 2.5%; seleção `accentSoft`; ações à direita (hover no catálogo; Devices sempre visíveis).
- Ações existentes que precisam continuar: tag/untag (Images), clone/import host/export/import (Volumes), conectar/desconectar (Networks), start/stop/restart (Pods), etc.

### 4.5 Machines/Devices (M4)

- Não há views no app; criar `MachinesView.qml`, `DevicesView.qml`, `MachineDetailPanel.qml`, `MachineListModel`/`DeviceListModel` (ou QVariantList no Backend).
- Backend: novas propriedades `machines`, `devices` + refresh/ações; daemon: `machines.*`, `devices.*` (ver [`docs/engineering/api-rpc.md`](../../docs/engineering/api-rpc.md) e [`docs/adr/0003-machines-qemu-kvm.md`](../../docs/adr/0003-machines-qemu-kvm.md)).
- Machines: `MachineManager` no daemon (QEMU/QMP/cloud-init/SSH); Devices: QEMU `usb-host`/serial; o POC define a UI.
- Habilitar as seções na sidebar apenas quando o backend existir; até lá, manter desabilitadas (estado atual).

---

## 5. Tabelas e listas

- **Containers/Machines**: `ListView` com delegate de linha (56 px) e painel de detalhe; seleção `currentIndex` + `accentSoft`; ações no hover.
- **Catálogo (Images/Volumes/Networks/Builds/Pods/Devices)**: `TableView` com `columnWidthProvider`, header custom, `rowDelegate`; ou `ListView` com `RowLayout` e larguras fixas.
- **Vazio/loading/erro**: um `Item`/`Column` centralizado com título + ação; skeleton de 6 linhas (retângulos `surface-2` com `Behavior`/opacidade) no loading.
- **Ações**: `AppButton` 27 px; `ToolTip` com nome; `Accessible.name`; confirmação para destrutivas.
- **Paginação/scroll**: `ScrollBar` custom; não aninhar scrolls desnecessários.

---

## 6. Gráficos (CPU/RAM)

`Sparkline.qml` sugerido:

```qml
Canvas {
    id: chart
    property var values: []
    property color lineColor: "#22d3ee"
    property color fillColor: Qt.rgba(lineColor.r, lineColor.g, lineColor.b, 0.10)
    property real minValue: 0
    property real maxValue: 0

    onValuesChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        ctx.clearRect(0, 0, width, height)
        if (values.length < 2) return

        let lo = minValue
        let hi = maxValue
        if (hi <= lo) {
            hi = 1
            for (let i = 0; i < values.length; ++i) hi = Math.max(hi, values[i])
            lo = 0
        }
        const pad = 4
        const span = Math.max(1, hi - lo)
        ctx.beginPath()
        for (let i = 0; i < values.length; ++i) {
            const x = (i / (values.length - 1)) * width
            const y = height - pad - ((values[i] - lo) / span) * (height - pad * 2)
            if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
        }
        ctx.strokeStyle = lineColor
        ctx.lineWidth = 1.5
        ctx.lineJoin = "round"
        ctx.lineCap = "round"
        ctx.stroke()

        // área
        ctx.lineTo(width, height)
        ctx.lineTo(0, height)
        ctx.closePath()
        ctx.fillStyle = fillColor
        ctx.fill()
    }
}
```

- CPU: `lineColor: theme.cyan`; escala `0..max(5, máx)`.
- RAM: `lineColor: theme.indigo400`; escala dinâmica `min*0.94 .. max*1.04`.
- Histórico: 30 amostras; timer 2 s; limpar ao trocar de container; pausar/retomar ao entrar/sair da aba.
- Não usar `Canvas` em animação contínua; só `requestPaint()` quando chega amostra.
- Alternativa: `Shape`/`PathPolyline` para evitar Canvas; o resultado visual é o mesmo.

---

## 7. Setup / Onboarding

### 7.1 Onde fica

- Janela separada (`SetupWindow.qml`), 1020×680, modal (`modality: Qt.ApplicationModal`), titlebar próprio (38 px).
- Não colocar como aba da `Main.qml`; o app continua atrás.
- Entrada: primeiro uso; `podstack setup`; Settings → “Executar configuração novamente”.

### 7.2 Controlador

- **`SetupController`** (QObject, GUI/Backend): `step`, `busy`, `progress`, `logs`, `checks`, `dockerPresent`, `dockerPolicy`, `podman`, `podmanVersion`, `podmanOutdated`, `qemu`, `kvm`, `virtiofsd`, `libvirt`, `caddy`, `k3s`, `socket`, `rootless`, `validated`; sinais `changed`; métodos `open`, `close`, `next`, `back`, `scan`, `applyEngine`, `removeDocker`, `installQemu`, `installComponents`, `validate`, `finish`.
- **`SetupManager`** (daemon): `detect()` e `apply(action, job)`; usa `PodmanClient`/`QStandardPaths`/`/dev/kvm`/helper; devolve checks e progresso.
- O QML **não** executa comandos; apenas chama o controller/RPC.

### 7.3 RPC proposto

Ver [ONBOARDING.md](ONBOARDING.md) §15:

- `setup.status`, `setup.check`, `setup.apply {actions:[...]}`, `setup.removeDocker {confirm:true}`, `setup.cancel`, `setup.validate`.
- O fluxo principal (`setup.apply`) **não remove o Docker**; `setup.removeDocker` é opt-in, exige `confirm: true` e aviso de perda.
- Jobs/subscriptions: `setup.delta {jobId, step, line, progress}`, `setup.done {jobId, status}`.
- Ações destrutivas exigem `confirm: true` no daemon; auditoria.

### 7.4 Detecção real

- Podman: binário, versão, `podman info`, socket de usuário, `systemctl --user is-active podman.socket`.
- Docker: binário/versão/socket; distinguir `docker` real de `podman-docker`.
- QEMU/KVM: `qemu-system-*`, `/dev/kvm`, grupo `kvm`, `virtiofsd`, libvirt, OVMF.
- Componentes: SQLite, Caddy, k3s, systemd de usuário.
- Tudo read-only na etapa de diagnóstico.

### 7.5 Aplicação real

- **Nunca** `sudo`/`dnf` diretamente na GUI. Helper systemd + polkit com métodos allowlisted (`InstallPackages`, `RemovePackages`, `EnableUserSocket`, …); nunca `Exec`/shell.
- Docker: **não remover por padrão**. A remoção é opt-in, com confirmação explícita, aviso de perda de containers/imagens/redes/volumes, sem migração automática e com backup/exportação opcional. O PodStack funciona com o Docker instalado; se o usuário quiser, ofereça `DOCKER_HOST` apontando para o socket do Podman.
- Podman: instalar/atualizar; `systemctl --user enable --now podman.socket`; rootless; `loginctl enable-linger` se necessário.
- QEMU/KVM: instalar pacotes; verificar `/dev/kvm`; TCG com aviso quando indisponível.
- Caddy/k3s: opcionais; k3s em VM, não no host; não instalar sem ação explícita.
- Progresso/cancelamento/retry; log com redaction; não logar tokens.
- Persistir `setup.completed`/versões; reexecutar quando o usuário quiser.
- **Sem seletor de cenário**; o estado vem da detecção.
- A matriz Podman (6 estados, com Docker presente/ausente; a ação principal não remove o Docker) está em [ONBOARDING.md](ONBOARDING.md) §7.2.

### 7.6 QML

- `SetupWindow.qml`: `RowLayout` rail 220 + `ColumnLayout` content/footer; `StackLayout`/`SwipeView` para as 7 etapas; `NumberAnimation` no slide.
- `SetupRail.qml`, `SetupStep.qml`, `SetupCheckRow.qml`, `SetupProgress.qml`, `SetupLog.qml`, `SetupCard.qml`, `SetupDone.qml`.
- Footer: Voltar/Continuar; desabilitar durante `busy`; `Esc` fecha (confirmar se `busy`).
- Foco no conteúdo ao avançar; `Accessible.name` em checks/botões.

---

## 8. Backend e modelos

### 8.1 Já existe (M3)

`Backend` expõe:

- Containers: `containersChanged`, `refresh`, `lifecycle`, `removeContainer`, `createContainer`, `batchAll/batchIds`, `rename/duplicate`, `openTerminal`.
- Logs/stats/files: `logLines`, `stats`, `cpuHistory`, `files`, `filesPath`, `subscribeLogs`, `subscribeStats`, `listFiles`, `downloadFile`, `uploadFile`.
- Catálogo: `images`, `volumes`, `networks`, `pods`, `pullImage`, `removeImage`, `pruneImages`, `tagImage`, `untagImage`, `exportImage`, `loadImage`, `createVolume`, `removeVolume`, `pruneVolumes`, `cloneVolume`, `importHostPath`, `createNetwork`, `removeNetwork`, `connectNetwork`, `disconnectNetwork`, `createPod`, `podAction`, `removePod`.
- Compose: `composeProjects`, `composeLogLines`, `refreshCompose`, `composeUp/Down/Restart/Stop/Start`, `subscribeComposeLogs`, `openPath`.
- Builds: `builds`, `buildLog`, `buildActive`, `buildStatus`, `activeBuildId`, `refreshBuilds`, `startBuild`, `cancelBuild`, `removeBuild`.
- Events: `eventMessage`, `eventKind` (toasts).
- Settings: `settings`, `saveEngineMode`.

### 8.2 Adicionar

- `QVariantList machines`, `devices`; signals; refresh/ações; `MachineListModel`/`DeviceListModel` (ou QVariantList).
- `memHistory` (ou `StatsModel` com CPU/RAM).
- `SetupController`/`setup` property.
- `theme`/`dark` setting e persistência (`Config`).
- Busca global por seção (hoje a busca da GUI é de containers).

### 8.3 RPC

- Não alterar contratos existentes sem versionar; atualizar [`docs/engineering/api-rpc.md`](../../docs/engineering/api-rpc.md).
- Machines/Devices: seguir o contrato planejado (M4).
- Setup: propor `setup.*` + `jobs`/subscriptions; nenhum comando arbitrário.

---

## 9. Eventos, toasts e streams

- `events.subscribe` já existe; o `Backend` mantém `eventMessage/eventKind` e o `Toast.qml` no Main.
- O toast deve ficar **8 px abaixo do topbar** no app; no POC ele está no desktop, porque a janela é uma simulação.
- Deduplicar por `(Actor.ID, Action, time)`; ignorar tipos não relevantes.
- Logs/stats/build/compose logs: um `StreamClient` por tipo; cancelar ao sair da aba (já feito em parte; revisar).

---

## 10. Janela e chrome

- **Não** implementar a barra do GNOME, o wallpaper, o dock, o titlebar da POC nem os botões minimizar/maximizar/fechar.
- O app usa a janela nativa (Wayland/X11) e o `ApplicationWindow`; se um header customizado for desejado, usar `Qt.FramelessWindowHint` com muito cuidado (arrastar, resize, sombras, atalhos) — **não é necessário** para o redesign.
- A sidebar/topbar/statusbar **são** o chrome do app.

---

## 11. Acessibilidade e i18n

- `Accessible.name`, `Accessible.description`, `Accessible.role` em botões, linhas, tabs, inputs.
- Foco visível (3 px accentSoft + borda accent); ordem de tab lógica.
- Atalhos: `/` busca, `Esc` fecha, `Ctrl+N` novo, `Ctrl+R` atualizar; documentar em [`docs/engineering/ui-map.md`](../../docs/engineering/ui-map.md).
- Strings em `qsTr()`; PT-BR base e en-US; evitar concatenar frases.
- Contraste: não usar `textTertiary` para informação essencial; testar ambos os temas.
- `prefers-reduced-motion` → reduzir animações (verificar `Settings`/`QStyleHints`).

---

## 12. Ordem de implementação

### Fase 1 — Tokens e base

1. Atualizar `Theme.qml` (tokens acima) e o tema claro/escuro.
2. Empacotar Inter + ícones Lucide; `AppIcon`, `AppText`.
3. `AppButton` (variant/size/icon), `AppBadge`, `AppPanel`, `AppTabs`, `AppSegmented`, `AppNotice`, `AppDialog`.
4. Restilizar `Main.qml` (sidebar/topbar/statusbar), `Toast.qml`, `ActionButton`, `FormField`, `FormCheck`.
5. Verificar visualmente as telas existentes; nenhum comportamento alterado.

### Fase 2 — Containers

1. `ContainersPage` (linha 56, dot/badges, hover actions, seleção suave).
2. `Inspector` (header/tabs/segmentado; Info/Logs/Stats/Files).
3. `Sparkline` CPU/RAM; `StatsView` 4 cards + 2 gráficos; 2 s; históricos.
4. Testes GUI; regressão de logs/stats/files; performance.

### Fase 3 — Catálogo

1. Tabelas de Images/Volumes/Networks/Pods/Builds/Compose com `TableView`/delegates.
2. Diálogos de pull/create/clone/prune/up/build; confirmações.
3. Preservar tag/untag, import/export, connect/disconnect, lifecycle de pods.
4. Testes de contrato/RPC/CLI/GUI; cobertura ≥80%.

### Fase 4 — Machines/Devices

1. Backend/daemon M4 (`machines.*`, `devices.*`); models.
2. `MachinesView`/`DevicesView` + detalhe; criar/start/stop/restart/terminal/SSH/logs; attach/detach/scan.
3. Testes com QEMU real (QSKIP quando indisponível) e mocks.

### Fase 5 — Setup

1. `SetupManager` + `setup.*` + helper allowlisted.
2. `SetupWindow`/componentes; 7 etapas; matriz Podman com coexistência de Docker; validação.
3. Primeiro uso/CLI/Settings; persistência; testes de detecção/consentimento/erro.

### Fase 6 — Polimento

- Screenshots de regressão (ambos os temas), `qmllint`, acessibilidade, i18n, atalhos, performance, cobertura.

---

## 13. Checklist de aceite do redesign

- [ ] `Theme.qml` com todos os tokens; nenhuma cor hardcoded nos componentes.
- [ ] Inter e ícones empacotados; fallback; licenças.
- [ ] Sidebar/topbar/statusbar no novo visual; sem barra de acento em seleção.
- [ ] Containers: lista, inspector, Info/Logs/Stats/Files; CPU/RAM em gráficos; 2 s.
- [ ] Catálogo: tabelas e ações equivalentes às atuais; nenhuma regressão de RPC.
- [ ] Machines/Devices quando M4; seções desabilitadas até lá.
- [ ] Onboarding separado, 7 etapas, matriz Podman (Docker pode coexistir), validação.
- [ ] Tema claro/escuro; contraste; foco; atalhos; `qsTr()`.
- [ ] `ctest --preset dev` 7/7; cobertura ≥80%; `qmllint`; GUI offscreen.
- [ ] Docs atualizados (`docs/engineering/ui-map.md`, `docs/engineering/settings.md`, `docs/engineering/api-rpc.md` se RPC mudar).

---

## 14. Riscos e decisões

| Risco / decisão | Mitigação |
|---|---|
| Trocar o tema afeta todas as telas | Fase 1 isolada, screenshots antes/depois, revisão visual. |
| Inter não está no sistema | Empacotar OFL; fallback; testar sem rede. |
| Ícones coloridos no QML | `MultiEffect`/`ColorOverlay`; testar em ambos os temas. |
| Remoção opt-in do Docker é destrutiva | Confirmação explícita, aviso de perda, helper allowlisted, logs, retry; o fluxo principal nunca remove. |
| Setup instalar pacotes | Helper/polkit; nunca shell; distro suportada; remediação manual. |
| Machines/Devices não existem ainda | Implementar backend M4 antes das telas; manter desabilitado. |
| Performance de tabelas/gráficos | `TableView`/`ListView` com delegates leves; `Canvas` só sob demanda; limite de históricos. |
| Regressão de RPC/CLI | Não alterar contratos sem versionar; testes existentes. |
| Escopo do onboarding | Entregar detecção + validação primeiro; ações privilegiadas depois, com helper. |
| **Não migrar para Tauri** | A camada visual é QML; Tauri não traz o design e exigiria reescrever terminal/GUI. |

---

## 15. Referências

- [`docs/engineering/ui-map.md`](../../docs/engineering/ui-map.md) — mapa de telas e navegação.
- [`docs/sdd/PRD.md`](../../docs/sdd/PRD.md) — requisitos e critérios.
- [`docs/sdd/spec.md`](../../docs/sdd/spec.md) — fluxos detalhados.
- [`docs/engineering/api-rpc.md`](../../docs/engineering/api-rpc.md) — contrato do daemon.
- [`docs/engineering/settings.md`](../../docs/engineering/settings.md) — settings e defaults.
- [`docs/engineering/security.md`](../../docs/engineering/security.md) — helper, socket, segredos.
- [`docs/sdd/testing.md`](../../docs/sdd/testing.md) — testes e cobertura.
- [`docs/adr/0003-machines-qemu-kvm.md`](../../docs/adr/0003-machines-qemu-kvm.md) — machines.
- [`poc/ui/`](.) — POC e demais docs.

> Em caso de divergência entre a POC e a spec, **a spec vence**. Atualize a spec antes de implementar.
