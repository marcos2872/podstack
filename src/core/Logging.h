#pragma once

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcCore)
Q_DECLARE_LOGGING_CATEGORY(lcPodman)
Q_DECLARE_LOGGING_CATEGORY(lcRpc)

namespace PodStack::Logging {

/// Configura o formato e o nível de log. `verbose` habilita debug.
void init(bool verbose);

} // namespace PodStack::Logging
