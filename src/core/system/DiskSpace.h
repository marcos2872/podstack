// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QVector>

namespace PodStack {

struct DiskSpaceSample {
    QString path;
    QString kind; // root | data | podman
    quint64 totalBytes = 0;
    quint64 freeBytes = 0;
    quint64 availableBytes = 0;
    double usedPercent = 0.0;
};

/// Amostra o espaço de um caminho com statvfs. Retorna false e preenche
/// `error` quando o caminho não existe ou não pode ser lido.
bool sampleDiskSpace(const QString &path, const QString &kind, DiskSpaceSample *out,
                     QString *error);

/// `true` quando alguma amostra atinge ou passa do limiar.
bool isDiskLow(const QVector<DiskSpaceSample> &samples, int thresholdPercent);

} // namespace PodStack
