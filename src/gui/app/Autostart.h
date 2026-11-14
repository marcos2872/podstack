// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace PodStack::Autostart {

/// Caminho do .desktop em ~/.config/autostart.
QString filePath();

/// O arquivo de autostart do PodStack existe?
bool isEnabled();

/// Cria/remove o .desktop de autostart. Retorna false e preenche `error` em falha.
bool setEnabled(bool enabled, QString *error = nullptr);

/// Garante que o autostart reflita `app.startAtLogin` da configuração.
void syncFromConfig();

} // namespace PodStack::Autostart
