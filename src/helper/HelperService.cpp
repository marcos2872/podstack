// SPDX-License-Identifier: GPL-3.0-or-later

#include "helper/HelperService.h"

#include "core/Error.h"
#include "helper/Firewall.h"
#include "helper/Polkit.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QLoggingCategory>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QtGlobal>

Q_LOGGING_CATEGORY(lcHelper, "podstack.helper")

namespace PodStack {
namespace {

constexpr auto kInstallAction = "dev.podstack.helper.install-packages";
constexpr auto kRemoveAction = "dev.podstack.helper.remove-packages";
constexpr auto kOpenPortsAction = "dev.podstack.helper.open-ports";
constexpr auto kClosePortsAction = "dev.podstack.helper.close-ports";

const QStringList &installAllowlist()
{
    static const QStringList packages = {
        QStringLiteral("podman"),
        QStringLiteral("qemu-kvm"),
        QStringLiteral("qemu-system-x86_64"),
        QStringLiteral("qemu-system-aarch64"),
        QStringLiteral("virtiofsd"),
        QStringLiteral("edk2-ovmf"),
        QStringLiteral("libvirt-daemon-kvm"),
        QStringLiteral("caddy"),
    };
    return packages;
}

const QStringList &removeAllowlist()
{
    static const QStringList packages = {
        QStringLiteral("docker-ce"),
        QStringLiteral("docker-ce-cli"),
        QStringLiteral("containerd.io"),
        QStringLiteral("docker.io"),
        QStringLiteral("moby-engine"),
        QStringLiteral("podman-docker"),
    };
    return packages;
}

QString packageManager()
{
    if (!QStandardPaths::findExecutable(QStringLiteral("dnf")).isEmpty()) {
        return QStringLiteral("dnf");
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("apt-get")).isEmpty()) {
        return QStringLiteral("apt-get");
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("pacman")).isEmpty()) {
        return QStringLiteral("pacman");
    }
    return {};
}

QString runProcess(const QString &program, const QStringList &arguments, int timeoutMs)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(5000)) {
        return QStringLiteral("não foi possível iniciar %1").arg(program);
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return QStringLiteral("%1 excedeu o tempo limite").arg(program);
    }

    const QString output = QString::fromUtf8(process.readAll()).trimmed();
    const int exitCode = process.exitCode();
    if (process.exitStatus() != QProcess::NormalExit || exitCode != 0) {
        const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QString tail = lines.mid(qMax(0, lines.size() - 5)).join(QStringLiteral(" | "));
        return tail.isEmpty() ? QStringLiteral("%1 falhou (código %2)").arg(program).arg(exitCode)
                              : tail;
    }
    return {};
}

} // namespace

HelperService::HelperService(QObject *parent)
    : QObject(parent)
{
}

bool HelperService::authorize(const QString &actionId, QString *error) const
{
    if (!calledFromDBus()) {
        if (error) {
            *error = QStringLiteral("Chamada local não autorizada.");
        }
        return false;
    }

    const QString sender = message().service();
    QDBusConnectionInterface *bus = QDBusConnection::systemBus().interface();
    if (!bus) {
        if (error) {
            *error = QStringLiteral("Barramento de sistema indisponível.");
        }
        return false;
    }

    const QDBusReply<uint> pidReply = bus->servicePid(sender);
    if (!pidReply.isValid() || pidReply.value() == 0) {
        if (error) {
            *error = QStringLiteral("Não foi possível identificar o processo chamador.");
        }
        return false;
    }

    return checkAuthorization(actionId, pidReply.value(), error);
}

bool HelperService::validatePackages(const QStringList &packages, const QStringList &allowed,
                                     QString *error) const
{
    if (packages.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Nenhum pacote informado.");
        }
        return false;
    }

    for (const QString &package : packages) {
        if (!allowed.contains(package)) {
            if (error) {
                *error = QStringLiteral("Pacote fora da allowlist: %1").arg(package);
            }
            return false;
        }
    }
    return true;
}

QVariantMap HelperService::result(bool ok, const QString &message)
{
    QVariantMap map;
    map.insert(QStringLiteral("ok"), ok);
    map.insert(QStringLiteral("message"), message);
    return map;
}

QStringList HelperService::installedPackages(const QStringList &packages)
{
    QStringList installed;
    for (const QString &package : packages) {
        QProcess query;
        query.setProcessChannelMode(QProcess::MergedChannels);
        query.start(QStringLiteral("rpm"), {QStringLiteral("-q"), package});
        if (query.waitForFinished(5000) && query.exitCode() == 0) {
            installed.append(package);
        }
    }
    return installed;
}

QVariantMap HelperService::install(const QStringList &packages, const QString &actionId,
                                   const QStringList &allowed)
{
    QString error;
    if (!validatePackages(packages, allowed, &error)) {
        return result(false, error);
    }
    if (!authorize(actionId, &error)) {
        return result(false, error);
    }

    const QString manager = packageManager();
    if (manager.isEmpty()) {
        return result(false, QStringLiteral("Gerenciador de pacotes não suportado."));
    }

    qCInfo(lcHelper, "installation requested: %s", qUtf8Printable(packages.join(',')));

    QStringList arguments;
    if (manager == QStringLiteral("dnf")) {
        arguments = QStringList{QStringLiteral("install"), QStringLiteral("-y"),
                                QStringLiteral("--refresh")};
    } else if (manager == QStringLiteral("apt-get")) {
        arguments = QStringList{QStringLiteral("install"), QStringLiteral("-y")};
    } else {
        arguments = QStringList{QStringLiteral("-S"), QStringLiteral("--noconfirm")};
    }
    arguments += packages;

    const QString failure = runProcess(manager, arguments, 900000);
    if (!failure.isEmpty()) {
        return result(false, QStringLiteral("Falha ao instalar: %1").arg(failure));
    }
    return result(true, QStringLiteral("Pacotes instalados: %1")
                            .arg(packages.join(QStringLiteral(", "))));
}

QVariantMap HelperService::InstallPackages(const QStringList &packages)
{
    return install(packages, QString::fromLatin1(kInstallAction), installAllowlist());
}

QVariantMap HelperService::RemovePackages(const QStringList &packages)
{
    QString error;
    if (!validatePackages(packages, removeAllowlist(), &error)) {
        return result(false, error);
    }
    if (!authorize(QString::fromLatin1(kRemoveAction), &error)) {
        return result(false, error);
    }

    const QStringList installed = installedPackages(packages);
    if (installed.isEmpty()) {
        return result(true, QStringLiteral("Nenhum pacote do Docker estava instalado."));
    }

    QProcess stop;
    stop.start(QStringLiteral("systemctl"),
               {QStringLiteral("disable"), QStringLiteral("--now"),
                QStringLiteral("docker.socket"), QStringLiteral("docker.service")});
    (void)stop.waitForFinished(30000);

    const QString manager = packageManager();
    if (manager.isEmpty()) {
        return result(false, QStringLiteral("Gerenciador de pacotes não suportado."));
    }

    qCInfo(lcHelper, "removal requested: %s", qUtf8Printable(installed.join(',')));

    QStringList arguments;
    if (manager == QStringLiteral("dnf")) {
        arguments = QStringList{QStringLiteral("remove"), QStringLiteral("-y")};
    } else if (manager == QStringLiteral("apt-get")) {
        arguments = QStringList{QStringLiteral("remove"), QStringLiteral("-y")};
    } else {
        arguments = QStringList{QStringLiteral("-R"), QStringLiteral("--noconfirm")};
    }
    arguments += installed;

    const QString failure = runProcess(manager, arguments, 900000);
    if (!failure.isEmpty()) {
        return result(false, QStringLiteral("Falha ao remover o Docker: %1").arg(failure));
    }
    return result(true, QStringLiteral("Pacotes removidos: %1")
                            .arg(installed.join(QStringLiteral(", "))));
}

uint HelperService::callerUid() const
{
    if (!calledFromDBus()) {
        return 0;
    }
    QDBusConnectionInterface *bus = QDBusConnection::systemBus().interface();
    if (!bus) {
        return 0;
    }
    const QDBusReply<uint> reply = bus->serviceUid(message().service());
    return reply.isValid() ? reply.value() : 0;
}

QVariantMap HelperService::changePorts(const QStringList &ports, const QString &protocol,
                                       const QString &actionId, bool open)
{
    QStringList normalized;
    Error validationError;
    if (!Firewall::validatePorts(ports, protocol, &normalized, &validationError)) {
        return result(false, validationError.message);
    }
    const QString normalizedProtocol = protocol.trimmed().toLower();

    QString error;
    if (!authorize(actionId, &error)) {
        qCWarning(lcHelper, "%s uid=%u ports=%s protocol=%s result=denied",
                  open ? "OpenPorts" : "ClosePorts", callerUid(),
                  qUtf8Printable(normalized.join(QLatin1Char(','))),
                  qUtf8Printable(normalizedProtocol));
        return result(false, error);
    }

    PodStack::Error firewallError;
    const bool changed =
        open ? Firewall::openPorts(normalized, normalizedProtocol, &firewallError)
             : Firewall::closePorts(normalized, normalizedProtocol, &firewallError);
    if (!changed) {
        QString message = firewallError.message;
        if (!firewallError.detail.isEmpty()) {
            message += QLatin1Char(' ') + firewallError.detail;
        }
        qCWarning(lcHelper, "%s uid=%u ports=%s protocol=%s result=error code=%s",
                  open ? "OpenPorts" : "ClosePorts", callerUid(),
                  qUtf8Printable(normalized.join(QLatin1Char(','))),
                  qUtf8Printable(normalizedProtocol), qUtf8Printable(firewallError.code));
        return result(false, message);
    }

    qCInfo(lcHelper, "%s uid=%u ports=%s protocol=%s result=ok",
           open ? "OpenPorts" : "ClosePorts", callerUid(),
           qUtf8Printable(normalized.join(QLatin1Char(','))),
           qUtf8Printable(normalizedProtocol));
    return result(true, open ? QStringLiteral("Portas abertas no firewall: %1/%2")
                                   .arg(normalized.join(QStringLiteral(", ")), normalizedProtocol)
                             : QStringLiteral("Portas fechadas no firewall: %1/%2")
                                   .arg(normalized.join(QStringLiteral(", ")), normalizedProtocol));
}

QVariantMap HelperService::OpenPorts(const QStringList &ports, const QString &protocol)
{
    return changePorts(ports, protocol, QString::fromLatin1(kOpenPortsAction), true);
}

QVariantMap HelperService::ClosePorts(const QStringList &ports, const QString &protocol)
{
    return changePorts(ports, protocol, QString::fromLatin1(kClosePortsAction), false);
}

} // namespace PodStack
