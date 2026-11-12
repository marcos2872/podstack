# ADR-0010 — DEB do Ubuntu 24.04 com QTermWidget vendorizado

| Campo | Valor |
|---|---|
| Status | **Aceito** (2026-11-12) |
| Data | 2026-11-12 |
| Decisores | Produto, Arquitetura |
| Relacionados | ADR-0005, `packaging.md`, `PRD.md` (NFR-COMP-001) |

## Contexto

A release 0.1.1 publicou um `.deb` construído no Fedora 44 com Qt 6.10 e
`libqtermwidget6.so.2`. O Ubuntu 24.04 LTS tem Qt 6.4.2 e **não empacota
`qtermwidget6`** (só o `libqtermwidget5-1`, de Qt 5); o Ubuntu 25.10 também não o
tem e o 26.04 passou a oferecer `libqtermwidget6-2`. Como o `podstack-gui` linka a
biblioteca, o app morria no loader antes de abrir a janela; o `control` ainda usava
nomes de pacotes `t64` do Ubuntu 24.04 e não cobria os módulos QML.

## Decisão

1. Reduzir o requisito de Qt para **6.4** e adicionar fallbacks para APIs de 6.5+
   (`QQuickWidget::loadFromModule`, `Qt.styleHints.colorScheme`/`Qt.ColorScheme`,
   `QTimeZone::UTC`, `MultiEffect` e cantos individuais de `Rectangle`).
2. Vendorizar o **QTermWidget 2.0.0** em `third_party/qtermwidget`, com CMake próprio
   (sem `lxqt2-build-tools`), biblioteca **estática** e dados em
   `/usr/share/podstack/qtermwidget6`; o binário não depende de
   `libqtermwidget6.so.2` e não conflita com o pacote do 26.04.
3. Construir o `.deb` no **Ubuntu 24.04** (glibc 2.39, Qt 6.4) e validar a execução
   em 24.04, 25.10 e 26.04; o RPM continua no Fedora 44 com o QTermWidget do sistema.
4. Declarar no `Depends` as alternativas `t64`/sem `t64` do Qt, os módulos QML,
   `libqt6sql6-sqlite`, `qt6-qpa-plugins`/`qt6-wayland` e as duas variantes de
   `libcurl`, e strip dos binários no staging.

## Alternativas

- **PPA/backport do qtermwidget6:** adiciona um repositório externo e não é
  reproduzível para o usuário final.
- **Remover o terminal integrado no 24.04:** regressão funcional; o botão “Abrir
  externo” já existe, mas o terminal é uma feature central.
- **Flatpak para o 24.04:** não resolve o `.deb` que o usuário instalou; pode ser
  oferecido como canal alternativo.
- **Construir o DEB no Fedora:** o binário exige Qt 6.10 e não é compatível com o
  Qt 6.4 do 24.04.

## Consequências

- O `.deb` cresce um pouco (QTermWidget estático), mas cai de ~30 MB para ~2,6 MB
  com o `strip` e não instala uma biblioteca compartilhada concorrente.
- Atualizações de segurança do QTermWidget exigem subir a versão vendorizada e
  recompilar o DEB; o source e as modificações ficam documentados em
  `third_party/qtermwidget/README.podstack.md`.
- O RPM e o DEB passam a ter pipelines separados; a publicação só ocorre depois
  dos dois jobs.
- Debian 12 não é suportado pelo binário (glibc 2.39); o alvo é Ubuntu 24.04+.
