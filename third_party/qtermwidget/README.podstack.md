# qtermwidget (vendored)

Componente de terceiros incorporado ao PodStack para permitir o build no
Ubuntu 24.04, cujo archive não possui o pacote `qtermwidget6`.

- **Origem:** <https://github.com/lxqt/qtermwidget>, tag `2.0.0`.
- **Licença:** GPL-2.0-or-later (ver `LICENSE`). O PodStack é
  GPL-3.0-or-later; a combinação é compatível, pois a GPL-2.0-or-later
  permite o relicenciamento para GPL-3.0.

## Modificações do PodStack

- `CMakeLists.txt` reescrito para exigir Qt 6.4 (versão do Ubuntu 24.04) em
  vez de Qt 6.6 do upstream.
- Biblioteca compilada como **estática** e linkada em `podstack-gui`; não há
  `libqtermwidget6.so` para instalar nem conflito com a biblioteca do sistema
  no Ubuntu 26.04.
- Removida a dependência de `lxqt2-build-tools` (não empacotado no Ubuntu
  24.04), as traduções (`lib/translations`), o pkg-config, a instalação de
  headers e a instalação da biblioteca.
- Mantidos os color schemes e keytabs, instalados em
  `/usr/share/podstack/qtermwidget6` e copiados para a árvore de build. O
  `tools.cpp` ganhou fallback para
  `applicationDirPath()/../share/podstack/qtermwidget6` (build tree, `/usr/bin`
  e Flatpak), evitando colisão com `libqtermwidget6-2` no Ubuntu 26.04.
- Nenhuma alteração de API Qt 6.4 foi necessária; a única mudança em C++ é o
  fallback de localização dos dados.
