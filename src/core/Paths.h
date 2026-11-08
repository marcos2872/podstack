#pragma once

#include <QString>

namespace PodStack::Paths {

/// ~/.config/podstack
QString configDir();

/// ~/.local/share/podstack
QString dataDir();

/// ~/.local/state/podstack
QString stateDir();

/// $XDG_RUNTIME_DIR/podstack (fallback /tmp/podstack-$UID)
QString runtimeDir();

/// Socket de controle do podstackd.
QString controlSocket();

/// Socket do Podman rootless (ou PODSTACK_PODMAN_SOCKET).
QString podmanSocket();

/// Cria os diretórios com permissões restritas. Retorna false e preenche `error` em falha.
bool ensureDirs(QString *error = nullptr);

} // namespace PodStack::Paths
