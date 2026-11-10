#pragma once

#include <QString>

namespace PodStack::Paths {

/// ~/.config/podstack
QString configDir();

/// ~/.local/share/podstack
QString dataDir();

/// ~/.local/state/podstack
QString stateDir();

/// ~/.cache/podstack (respeita XDG_CACHE_HOME)
QString cacheDir();

/// ~/.cache/podstack/images
QString cloudImagesDir();

/// ~/.local/share/podstack/machines
QString machinesDir();

/// ~/.local/share/podstack/machines/<id>; vazio se o id não for seguro para path.
QString machineDir(const QString &id);

/// $XDG_RUNTIME_DIR/podstack (fallback /tmp/podstack-$UID)
QString runtimeDir();

/// Socket de controle do podstackd.
QString controlSocket();

/// Socket do Podman rootless (ou PODSTACK_PODMAN_SOCKET).
QString podmanSocket();

/// Cria os diretórios com permissões restritas. Retorna false e preenche `error` em falha.
bool ensureDirs(QString *error = nullptr);

} // namespace PodStack::Paths
