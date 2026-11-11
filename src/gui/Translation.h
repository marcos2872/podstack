// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

class QCoreApplication;

namespace PodStack {

/// Instala o catálogo da UI para `language` (pt-BR é o idioma fonte; en-US
/// carrega `:/i18n/podstack_en_US.qm`). Retorna false se o catálogo não carregar.
bool installTranslation(QCoreApplication *application, const QString &language);

} // namespace PodStack
