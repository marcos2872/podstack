// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>

class QObject;
class QString;

namespace PodStack {

/// Instala handlers de SIGTERM/SIGINT que chamam `callback` no thread da UI.
///
/// O handler de sinal apenas escreve um byte em um pipe (async-signal-safe);
/// um QSocketNotifier lê esse byte e executa o callback no loop de eventos.
/// Isso permite salvar o `UiState` antes de o systemd encerrar a GUI durante
/// um update. Instalação é idempotente por processo.
bool installTerminationHandler(QObject *context, std::function<void()> callback,
                               QString *error = nullptr);

} // namespace PodStack
