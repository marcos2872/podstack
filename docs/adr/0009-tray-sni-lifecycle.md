# ADR-0009 — Bandeja SNI, ciclo de vida e instância única

| Campo | Valor |
|---|---|
| Status | Aceito |
| Data | 2026-11-08 |
| Decisores | Arquitetura |
| Relacionados | `plan.md`, `ui-map.md §9`, T7-03/T7-04/T7-05 |

## Contexto

O app precisa de bandeja no Wayland (niri + Noctalia), fechar-para-bandeja, menu com ações de
containers, Activity Monitor e Sair. O ambiente tem Qt 6.11 com DBus e
`org.kde.StatusNotifierWatcher`; `kf6-kstatusnotifieritem` não está instalado. Fechar a janela
hoje encerra a GUI, e não há instância única.

## Opções

| Opção | Prós | Contras |
|---|---|---|
| `QSystemTrayIcon` + `QMenu` | sem dependência nova; SNI/DBusMenu no Qt 6.11; simples | menu exportado uma vez; precisa reexportar após mudanças |
| `KStatusNotifierItem` (KF6) | API rica, status/ativação | dependência nova (`kf6-kstatusnotifieritem-devel`) |

## Decisão

- Usar **`QSystemTrayIcon` + `QMenu`** (Qt Widgets, já linkado); KSNI fica como fallback se o SNI
  do Qt falhar no host.
- O `QMenu` é reconstruído a cada mudança de estado e **reexportado** com `setContextMenu`, porque
  o SNI não emite `aboutToShow` e o DBusMenu é estático após o primeiro export.
- O `TrayController` é uma view: não faz RPC; recebe snapshot do `Backend` e emite sinais.
- **Fechar a janela esconde** para a bandeja apenas se `QSystemTrayIcon::isSystemTrayAvailable()`
  no momento do fechamento e `app.keepRunningInTray`/`app.showInTray` permitirem. Sem bandeja,
  fechar encerra a GUI.
- **Instância única** via `QLocalServer` em `$XDG_RUNTIME_DIR/podstack/gui.sock` (dir `0700`,
  socket `0600`, `SO_PEERCRED`), comandos `show`/`quit`; `--hidden` inicia na bandeja.
- **Sair** encerra a GUI; `podstackd` continua (systemd user).
- Notificações via `org.freedesktop.Notifications`, com fallback para o balão do tray; cooldown
  por container/ação.
- Activity Monitor usa um novo stream `system.stats.subscribe` (host + containers agregados).

## Consequências

**Positivas:** nenhuma dependência nova; comportamento previsível no Wayland; menu sempre
atualizado; sem segunda bandeja ao reabrir o app.

**Negativas:** o menu nativo do `QMenu` não segue o tema QML; o `setContextMenu` reexporta o menu
inteiro a cada mudança; `requestActivate()` pode ser ignorado pelo compositor.

## Implementação

- `src/gui/Tray.{h,cpp}`, `src/gui/SingleInstance.{h,cpp}`, `src/gui/Autostart.{h,cpp}`,
  `src/gui/Notifications.{h,cpp}`, `src/gui/qml/ActivityMonitorWindow.qml`.
- `Backend` expõe `containers`, `actionActive`, fila de ações e `containerEvent`; refresh com
  debounce em eventos e reconexão do stream.
- Stats: `system.stats.subscribe` no daemon (`StatsCollector`) e `ActivityMonitorWindow.qml`.
- Testes: `test_tray`, `test_single_instance`, `test_autostart`, `test_stats`,
  `test_notifications`; validação manual no niri/Noctalia.
