// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDate>
#include <QString>
#include <QStringList>

namespace PodStack::LogRotation {

/// Parâmetros de rotação/retenção de um processo.
struct Options {
    QString directory;
    QString stem; // podstackd, podstack-gui-123, test_logging-456
    int retentionDays = 7;
    qint64 maxFileBytes = 10 * 1024 * 1024;
    qint64 maxTotalBytes = 200 * 1024 * 1024;
};

/// `dir/<stem>.jsonl` (arquivo corrente, sem data).
QString currentPath(const Options &options);

/// `dir/<stem>-YYYYMMDD.jsonl` ou `<stem>-YYYYMMDD-N.jsonl` quando sequence > 0.
QString rotatedPath(const Options &options, const QDate &date, int sequence = 0);

/// Rotaciona o corrente para o nome diário. Se o destino já existir, anexa o
/// conteúdo e remove o corrente (restart no mesmo dia).
bool rotate(const Options &options, const QDate &date, QString *error = nullptr);

/// Rotaciona por tamanho para `<stem>-YYYYMMDD-N.jsonl`.
bool rotateForSize(const Options &options, const QDate &date, int *sequence = nullptr,
                   QString *error = nullptr);

/// Rotaciona o corrente se ele for de um dia anterior (usa o mtime).
bool rotateAtStart(const Options &options, const QDate &today, QString *error = nullptr);

/// Remove arquivos mais antigos que a retenção e aplica o teto total.
/// Retorna a quantidade removida.
int prune(const Options &options, const QDate &today, QString *error = nullptr);

/// Arquivos rotacionados (com data) do stem, do mais antigo ao mais novo.
QStringList rotatedFiles(const Options &options);

} // namespace PodStack::LogRotation
