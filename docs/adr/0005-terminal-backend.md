# ADR-0005 — Backend de terminal embutido

| Campo | Valor |
|---|---|
| Status | **Aceito** (2026-11-08) |
| Data | 2026-11-08 |
| Decisores | Produto, Arquitetura |
| Relacionados | `PRD.md` (Q1 resolvida), `plan.md`, `ui-map.md`, `LICENSE` |

## Contexto

O terminal é uma aba central (exec em containers, shell em machines). O OrbStack usa Ghostty (macOS/AppKit). No Linux/Qt 6, as opções viáveis:

| Opção | Licença | Estado | Esforço |
|---|---|---|---|
| **QTermWidget** 2.4/2.5 | **GPL-2.0-or-later** | ativo, Qt 6 puro | baixo (`QDockWidget`) |
| **libvterm** 0.3 | **MIT** | estável, upstream calmo | alto (renderizador, seleção, IME, mouse) |
| QMLTermWidget | GPL-2.0 | pouco ativo | médio |
| xterm.js + WebEngine | MIT | maduro | médio, +150 MB |
| KonsolePart (KF6) | GPL-2.0 | ativo | médio, puxa Konsole |

## Decisão

O PodStack é licenciado sob **GPL-3.0-or-later**. Portanto:

- Usar **QTermWidget 2.5+** embutido na janela principal (`QMainWindow`) via
  **`QDockWidget` + `QTabWidget`**. A UI QML roda no widget central (`QQuickWidget`);
  QTermWidget é um QWidget e **não** é hospedado dentro do `QQuickWidget`.
- QTermWidget é **GPL-2.0-or-later**, compatível com GPL-3.0-or-later (o código GPLv2-or-later pode ser combinado em um trabalho GPLv3).
- Manter uma abstração `TerminalBackend` para permitir troca futura, mas **não** linkar libvterm no binário principal.
- Oferecer **"Abrir no terminal externo"** como fallback e para `kubectl exec`.

## Alternativas

**libvterm (MIT):** só seria necessária se o projeto fosse permissivo/proprietário. Como a licença é GPL-3.0-or-later, não há ganho em assumir semanas de renderizador/IME/seleção/mouse.

**xterm.js + WebEngine:** rejeitado pelo peso (~150 MB) e por adicionar um runtime web ao app.

**KonsolePart:** rejeitado por puxar Konsole/KF6 e aumentar o acoplamento.

## Consequências

**Positivas:**

- Terminal completo em dias, não meses.
- OSC-7/OSC-8, cores, mouse, seleção, bracketed paste.
- Compatível com a licença do projeto; sem conflito legal.
- Abstração permite trocar o backend no futuro se necessário.

**Negativas:**

- O binário combinado permanece GPL-3.0-or-later (já era a escolha do projeto).
- Glitches de fonte conhecidos no Qt 6.9+ (mitigáveis com fonte monoespaçada explícita e política de arredondamento de escala).
- Sem SIXEL (limitação conhecida; documentar).

## Implementação

- `TerminalView` (`QWidget`) encapsula o backend concreto `QTermWidget` já configurado e
  a barra com **Abrir externo**; uma abstração `TerminalBackend` fica para uma troca
  futura. O componente é reutilizado no dock da `MainWindow`, na `TerminalWindow`
  legada e na aba Terminal do detalhe do container.
- `ExternalTerminal` descobre o terminal padrão nesta ordem: `xdg-terminal-exec`,
  `$TERMINAL`, `xdg-settings`/gsettings/KDE, `TERM_PROGRAM` e o desktop atual
  (niri/sway/Hyprland → alacritty/ghostty/kitty/foot/wezterm), com os emuladores comuns
  como fallback. Nunca usa shell.
- `MainWindow` (`QMainWindow`) hospeda a UI QML em um `QQuickWidget` e o terminal em um
  `QDockWidget` "Terminal" com abas de `TerminalView` (uma sessão por aba).
- Containers abrem `podman exec -it <id> /bin/sh`; machines abrem `ssh` após o
  `machines.ssh` devolver host/porta/usuário/chave. O `Backend` apenas emite
  `terminalRequested`; quem cria a sessão é a `MainWindow`.
- O detalhe do container é um **overlay integrado** (`ContainerDetailDialog`, um
  `QWidget` filho da `MainWindow`) cujo card hospeda `ContainerDetail.qml`
  diretamente em um `QQuickWidget`, sem `QTabWidget` nativo. O terminal é uma **aba
  interna** do detalhe (entre Arquivos e Inspect): o QML reserva o placeholder e o
  `TerminalView` (`podman exec -it <id> /bin/sh`) é filho do `QQuickWidget`,
  sobreposto à área, oculto ao trocar de aba e preservado até fechar o overlay. O
  botão **Nova sessão em janela** do QML abre uma nova sessão via `ExternalTerminal`,
  sem shell; o **Abrir externo** do `TerminalView` continua no dock e na
  `TerminalWindow`.
- `ExternalTerminal` relança a mesma sessão fora do app sem shell: prioriza
  `xdg-terminal-exec` (spec freedesktop), depois `$TERMINAL` e os emuladores comuns
  (`gnome-terminal`, `kgx`, `konsole`, `xfce4-terminal`, `alacritty`, `kitty`, `foot`,
  `wezterm`, `xterm`), com os argumentos corretos por emulador.
- Testes: TTY, resize, UTF-8, cores, Ctrl+C/D, bracketed paste; o lançamento externo é
  coberto por `ExternalTerminal::commandFor` e por um `xdg-terminal-exec` falso no `PATH`.
- Cabeçalhos SPDX `GPL-3.0-or-later` em todos os fontes.

## Referências

- QTermWidget: https://github.com/lxqt/qtermwidget
- libvterm: https://www.leonerd.org.uk/code/libvterm/
- Discussão de manutenção: https://github.com/lxqt/lxqt/discussions/2761
- GPLv3: https://www.gnu.org/licenses/gpl-3.0.html
- Compatibilidade GPLv2-or-later → GPLv3: https://www.gnu.org/licenses/gpl-faq.html#AllCompatibility
