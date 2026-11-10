// SPDX-License-Identifier: GPL-3.0-or-later

#include "helper/DnsZone.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace PodStack {
namespace DnsZone {
namespace {

constexpr auto kFileName = "podstack.conf";
constexpr auto kBackupName = "podstack.conf.bak";

QString filePath(const QString &directory)
{
    return QDir(directory).filePath(QString::fromLatin1(kFileName));
}

} // namespace

bool validate(const QString &zone, const QString &upstream, QString *error)
{
    if (zone.compare(QString::fromLatin1(kZone), Qt::CaseInsensitive) != 0) {
        if (error) {
            *error = QStringLiteral("Zona DNS fora da allowlist: %1.").arg(zone);
        }
        return false;
    }
    if (upstream != QString::fromLatin1(kUpstream)) {
        if (error) {
            *error = QStringLiteral("Upstream DNS fora da allowlist: %1.").arg(upstream);
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

QString dropInText(const QString &zone, const QString &upstream)
{
    return QStringLiteral("# Gerado pelo PodStack; não edite manualmente.\n"
                          "[Resolve]\n"
                          "DNS=%1\n"
                          "Domains=~%2\n")
        .arg(upstream, zone);
}

bool install(const QString &directory, const QString &zone, const QString &upstream, QString *error)
{
    if (!validate(zone, upstream, error)) {
        return false;
    }

    QDir dir(directory);
    if (!dir.exists() && !QDir().mkpath(directory)) {
        if (error) {
            *error = QStringLiteral("Não foi possível criar %1.").arg(directory);
        }
        return false;
    }

    const QString path = filePath(directory);
    const QByteArray content = dropInText(zone, upstream).toUtf8();

    QFile existing(path);
    if (existing.exists()) {
        if (existing.open(QIODevice::ReadOnly) && existing.readAll() != content) {
            existing.close();
            const QString backup = dir.filePath(QString::fromLatin1(kBackupName));
            QFile::remove(backup);
            if (!QFile::copy(path, backup)) {
                if (error) {
                    *error = QStringLiteral("Não foi possível fazer backup de %1.").arg(path);
                }
                return false;
            }
        }
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error =
                QStringLiteral("Não foi possível escrever %1: %2").arg(path, file.errorString());
        }
        return false;
    }
    if (file.write(content) != content.size()) {
        if (error) {
            *error = QStringLiteral("Falha ao escrever %1.").arg(path);
        }
        file.cancelWriting();
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup |
                        QFileDevice::ReadOther);
    if (!file.commit()) {
        if (error) {
            *error =
                QStringLiteral("Não foi possível concluir %1: %2").arg(path, file.errorString());
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

bool remove(const QString &directory, const QString &zone, QString *error)
{
    if (zone.compare(QString::fromLatin1(kZone), Qt::CaseInsensitive) != 0) {
        if (error) {
            *error = QStringLiteral("Zona DNS fora da allowlist: %1.").arg(zone);
        }
        return false;
    }

    const QString path = QDir(directory).filePath(QString::fromLatin1(kFileName));
    if (QFileInfo::exists(path) && !QFile::remove(path)) {
        if (error) {
            *error = QStringLiteral("Não foi possível remover %1.").arg(path);
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

} // namespace DnsZone
} // namespace PodStack
