// SPDX-License-Identifier: GPL-3.0-or-later

#include "helper/PortBinding.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

namespace PodStack {
namespace PortBinding {
namespace {

constexpr auto kSocketSuffix = ".socket";
constexpr auto kServiceSuffix = ".service";
constexpr auto kBackupSuffix = ".bak";

QString baseName(const QString &port)
{
    return port == QString::fromLatin1(kHttpPort) ? QStringLiteral("podstack-http")
                                                  : QStringLiteral("podstack-https");
}

QString targetFor(const QString &port, const QString &httpTarget, const QString &httpsTarget)
{
    return port == QString::fromLatin1(kHttpPort) ? httpTarget : httpsTarget;
}

bool writeFile(const QString &path, const QByteArray &content, QString *error)
{
    QFile existing(path);
    if (existing.exists()) {
        if (existing.open(QIODevice::ReadOnly) && existing.readAll() != content) {
            existing.close();
            const QString backup = path + QString::fromLatin1(kBackupSuffix);
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
    return true;
}

} // namespace

bool validatePorts(const QStringList &ports, QString *error)
{
    if (ports.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Nenhuma porta informada.");
        }
        return false;
    }
    for (const QString &port : ports) {
        if (port != QString::fromLatin1(kHttpPort) && port != QString::fromLatin1(kHttpsPort)) {
            if (error) {
                *error = QStringLiteral("Porta fora da allowlist: %1 (use 80 ou 443).").arg(port);
            }
            return false;
        }
    }
    if (error) {
        error->clear();
    }
    return true;
}

bool validateTarget(const QString &target, QString *error)
{
    static const QRegularExpression pattern(QStringLiteral("^127\\.0\\.0\\.1:(\\d{1,5})$"));
    const QRegularExpressionMatch match = pattern.match(target);
    bool ok = false;
    const int port = match.hasMatch() ? match.captured(1).toInt(&ok) : 0;
    if (!match.hasMatch() || !ok || port < 1 || port > 65535) {
        if (error) {
            *error = QStringLiteral("Alvo do socket fora da allowlist: %1.").arg(target);
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

QString socketUnitName(const QString &port)
{
    return baseName(port) + QString::fromLatin1(kSocketSuffix);
}

QString serviceUnitName(const QString &port)
{
    return baseName(port) + QString::fromLatin1(kServiceSuffix);
}

QString socketUnitText(const QString &port)
{
    return QStringLiteral("# Gerado pelo PodStack; não edite manualmente.\n"
                          "[Unit]\n"
                          "Description=PodStack ingress na porta %1\n"
                          "\n"
                          "[Socket]\n"
                          "ListenStream=%1\n"
                          "\n"
                          "[Install]\n"
                          "WantedBy=sockets.target\n")
        .arg(port);
}

QString serviceUnitText(const QString &port, const QString &target)
{
    return QStringLiteral("# Gerado pelo PodStack; não edite manualmente.\n"
                          "[Unit]\n"
                          "Description=PodStack socket proxy da porta %1\n"
                          "After=network.target\n"
                          "\n"
                          "[Service]\n"
                          "ExecStart=%2 --exit-idle-time=5min %3\n"
                          "PrivateTmp=yes\n"
                          "ProtectHome=yes\n"
                          "ProtectSystem=strict\n"
                          "NoNewPrivileges=yes\n")
        .arg(port, QString::fromLatin1(kProxyBinary), target);
}

bool install(const QString &directory, const QStringList &ports, const QString &httpTarget,
             const QString &httpsTarget, QString *error)
{
    if (!validatePorts(ports, error)) {
        return false;
    }
    for (const QString &port : ports) {
        if (!validateTarget(targetFor(port, httpTarget, httpsTarget), error)) {
            return false;
        }
    }

    QDir dir(directory);
    if (!dir.exists() && !QDir().mkpath(directory)) {
        if (error) {
            *error = QStringLiteral("Não foi possível criar %1.").arg(directory);
        }
        return false;
    }

    for (const QString &port : ports) {
        const QString target = targetFor(port, httpTarget, httpsTarget);
        if (!writeFile(dir.filePath(socketUnitName(port)), socketUnitText(port).toUtf8(), error) ||
            !writeFile(dir.filePath(serviceUnitName(port)), serviceUnitText(port, target).toUtf8(),
                       error)) {
            return false;
        }
    }
    if (error) {
        error->clear();
    }
    return true;
}

bool remove(const QString &directory, const QStringList &ports, QString *error)
{
    if (!validatePorts(ports, error)) {
        return false;
    }
    const QDir dir(directory);
    for (const QString &port : ports) {
        for (const QString &name : {socketUnitName(port), serviceUnitName(port)}) {
            const QString path = dir.filePath(name);
            if (QFileInfo::exists(path) && !QFile::remove(path)) {
                if (error) {
                    *error = QStringLiteral("Não foi possível remover %1.").arg(path);
                }
                return false;
            }
        }
    }
    if (error) {
        error->clear();
    }
    return true;
}

} // namespace PortBinding
} // namespace PodStack
