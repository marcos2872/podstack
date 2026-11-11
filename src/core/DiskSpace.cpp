// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/DiskSpace.h"

#include <QFileInfo>

#include <sys/statvfs.h>

namespace PodStack {

bool sampleDiskSpace(const QString &path, const QString &kind, DiskSpaceSample *out, QString *error)
{
    if (out) {
        *out = DiskSpaceSample();
    }
    if (error) {
        error->clear();
    }
    if (!QFileInfo::exists(path)) {
        if (error) {
            *error = QStringLiteral("Caminho inexistente: %1").arg(path);
        }
        return false;
    }

    struct statvfs stats{};
    if (::statvfs(path.toUtf8().constData(), &stats) != 0) {
        if (error) {
            *error = QStringLiteral("Não foi possível ler o espaço de %1.").arg(path);
        }
        return false;
    }

    const quint64 fragmentSize = stats.f_frsize > 0 ? stats.f_frsize : stats.f_bsize;
    if (fragmentSize == 0 || stats.f_blocks == 0) {
        if (error) {
            *error = QStringLiteral("Filesystem sem estatísticas úteis: %1").arg(path);
        }
        return false;
    }

    DiskSpaceSample sample;
    sample.path = path;
    sample.kind = kind;
    sample.totalBytes = static_cast<quint64>(stats.f_blocks) * fragmentSize;
    sample.freeBytes = static_cast<quint64>(stats.f_bfree) * fragmentSize;
    sample.availableBytes = static_cast<quint64>(stats.f_bavail) * fragmentSize;
    const quint64 usedBytes =
        sample.totalBytes > sample.availableBytes ? sample.totalBytes - sample.availableBytes : 0;
    sample.usedPercent = sample.totalBytes > 0 ? 100.0 * static_cast<double>(usedBytes) /
                                                     static_cast<double>(sample.totalBytes)
                                               : 0.0;

    if (out) {
        *out = sample;
    }
    return true;
}

bool isDiskLow(const QVector<DiskSpaceSample> &samples, int thresholdPercent)
{
    for (const DiskSpaceSample &sample : samples) {
        if (sample.usedPercent >= static_cast<double>(thresholdPercent)) {
            return true;
        }
    }
    return false;
}

} // namespace PodStack
