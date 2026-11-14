// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"

#include <QDate>
#include <QString>
#include <QStringList>
#include <QVector>

namespace PodStack {

struct LogLine {
    QString ts;
    QString level;
    QString component;
    QString message;
    QString raw;
    int levelValue = 2;
};

struct LogReadOptions {
    QString directory;
    QDate since;
    QString level;
    QString component;
    int maxLines = 10000;
    qint64 maxBytes = 5LL * 1024 * 1024;
};

struct LogReadResult {
    QVector<LogLine> lines;
    QStringList files;
    bool truncated = false;
};

/// Lê os logs JSON Lines do usuário (7 dias por padrão) e aplica filtros.
bool readUserLogs(const LogReadOptions &options, LogReadResult *result, Error *error);

/// Exporta as linhas para um arquivo 0600, canonicalizando o destino e
/// recusando symlink.
bool exportLogs(const QVector<LogLine> &lines, const QString &path, Error *error);

/// Remove os logs do usuário (backend de `system logs --clear`).
bool clearUserLogs(Error *error);

} // namespace PodStack
