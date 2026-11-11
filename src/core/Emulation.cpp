// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/Emulation.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSysInfo>

namespace PodStack {
namespace {

QString readTrimmed(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll()).trimmed();
}

} // namespace

QVector<BinfmtHandler> detectBinfmtHandlers(const QString &root)
{
    QString directory = root;
    if (directory.isEmpty()) {
        directory = QProcessEnvironment::systemEnvironment().value(
            QStringLiteral("PODSTACK_BINFMT_DIR"), "/proc/sys/fs/binfmt_misc");
    }

    QVector<BinfmtHandler> handlers;
    const QDir dir(directory);
    for (const QFileInfo &entry :
         dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
        if (entry.fileName() == QStringLiteral("register") ||
            entry.fileName() == QStringLiteral("status")) {
            continue;
        }
        const QString content = readTrimmed(entry.absoluteFilePath());
        if (content.isEmpty()) {
            continue;
        }
        BinfmtHandler handler;
        handler.name = entry.fileName();
        handler.enabled = content.contains(QStringLiteral("enabled"));
        for (const QString &line : content.split(QLatin1Char('\n'))) {
            if (line.startsWith(QStringLiteral("interpreter "))) {
                handler.interpreter = line.mid(QStringLiteral("interpreter ").size()).trimmed();
                break;
            }
        }
        if (handler.enabled) {
            handlers.append(handler);
        }
    }
    return handlers;
}

QString hostArchitecture()
{
    const QString arch = QSysInfo::currentCpuArchitecture();
    if (arch == QStringLiteral("x86_64")) {
        return QStringLiteral("amd64");
    }
    if (arch == QStringLiteral("arm64") || arch == QStringLiteral("aarch64")) {
        return QStringLiteral("arm64");
    }
    return arch;
}

} // namespace PodStack
