// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace PodStack {

struct BinfmtHandler {
    QString name;
    QString interpreter;
    bool enabled = false;
};

/// Lê /proc/sys/fs/binfmt_misc (ou `root`, para testes) e devolve os handlers
/// habilitados (qemu-*, FEX-*).
QVector<BinfmtHandler> detectBinfmtHandlers(const QString &root = {});

/// Arquitetura do host no formato usado pelo Podman (amd64/arm64/...).
QString hostArchitecture();

} // namespace PodStack
