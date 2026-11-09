// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace PodStack {

/// Um shared dir exposto à máquina por virtiofs.
struct QemuMount {
    QString tag;
    QString socketPath;
    bool readOnly = false;
};

/// Parâmetros imutáveis de uma máquina QEMU/KVM.
struct QemuSpec {
    QString machineId;  // UUIDv7
    QString name;       // nome RFC1123
    QString arch = QStringLiteral("amd64"); // amd64|arm64
    int cpus = 2;
    qint64 memoryBytes = 4294967296LL;
    QString diskPath;
    QString seedPath;
    QString qmpSocketPath;
    QString consoleLogPath;
    QString pidFile;
    quint16 sshPort = 32222;
    bool kvm = true;
    bool isolateNetwork = false;
    QString ovmfCodePath;
    QString ovmfVarsPath;
    QVector<QemuMount> mounts;
};

class QemuCommand {
public:
    /// Binário do QEMU para a arquitetura (`qemu-system-x86_64`/`qemu-system-aarch64`).
    static QString binary(const QString &arch, Error *error);

    /// Monta a linha de comando do QEMU (sem tocar no filesystem).
    static QStringList arguments(const QemuSpec &spec, Error *error);
};

} // namespace PodStack
