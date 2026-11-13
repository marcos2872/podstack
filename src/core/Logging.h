// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/LogWriter.h"

#include <QLoggingCategory>
#include <QString>

Q_DECLARE_LOGGING_CATEGORY(lcCore)
Q_DECLARE_LOGGING_CATEGORY(lcPodman)
Q_DECLARE_LOGGING_CATEGORY(lcRpc)

namespace PodStack::Logging {

/// Configura o sink assíncrono, o nível e a rotação. `verbose` é o atalho
/// `--verbose`; `PODSTACK_LOG_LEVEL` tem precedência e a setting
/// `advanced.logLevel` é o default.
void init(bool verbose);

/// Reaplica nível/retenção/limites depois de `settings.set` (runtime).
void reload();

/// Para a writer thread, dá flush final e restaura o handler anterior.
void shutdown();

/// Solicita um flush síncrono (usado por testes e encerramento).
void flush();

QString logDirectory();
QString currentLogFile();
LogLevel level();
QString levelName(LogLevel level);

} // namespace PodStack::Logging
