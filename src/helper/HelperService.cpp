// SPDX-License-Identifier: GPL-3.0-or-later

#include "helper/HelperService.h"

#include "core/Error.h"
#include "helper/CaTrust.h"
#include "helper/DnsZone.h"
#include "helper/Firewall.h"
#include "helper/Polkit.h"
#include "helper/PortBinding.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QFile>
#include <QFileInfo>
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
constexpr auto kDnsZoneAction = "dev.podstack.helper.dns-zone";
constexpr auto kBindPortsAction = "dev.podstack.helper.bind-ports";
constexpr auto kTrustCaAction = "dev.podstack.helper.trust-ca";

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
    if (manager == QStringLiteral("dnf") || manager == QStringLiteral("apt-get")) {
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

QString HelperService::resolvedDirectory() const
{
    return QStringLiteral("/etc/systemd/resolved.conf.d");
}

bool HelperService::reloadResolved(QString *error) const
{
    const QString reloadError =
        runProcess(QStringLiteral("systemctl"),
                   {QStringLiteral("reload"), QStringLiteral("systemd-resolved")}, 10000);
    if (reloadError.isEmpty()) {
        return true;
    }
    const QString flushError =
        runProcess(QStringLiteral("resolvectl"), {QStringLiteral("flush-caches")}, 5000);
    if (flushError.isEmpty()) {
        return true;
    }
    if (error) {
        *error =
            QStringLiteral("Drop-in gravado, mas o resolved não recarregou: %1").arg(reloadError);
    }
    return false;
}

bool HelperService::avahiActive() const
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(
        QStringLiteral("systemctl"),
        {QStringLiteral("is-active"), QStringLiteral("--quiet"), QStringLiteral("avahi-daemon")});
    if (!process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

QString HelperService::systemUnitDirectory() const
{
    return QStringLiteral("/etc/systemd/system");
}

bool HelperService::runSystemctl(const QStringList &arguments, QString *error) const
{
    const QString failure = runProcess(QStringLiteral("systemctl"), arguments, 15000);
    if (!failure.isEmpty()) {
        if (error) {
            *error = failure;
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

QString HelperService::caAnchorsDirectory() const
{
    if (!QStandardPaths::findExecutable(QStringLiteral("update-ca-trust")).isEmpty()) {
        return QStringLiteral("/etc/pki/ca-trust/source/anchors");
    }
    return QStringLiteral("/usr/local/share/ca-certificates");
}

bool HelperService::runTrustUpdate(QString *error) const
{
    QString program = QStandardPaths::findExecutable(QStringLiteral("update-ca-trust"));
    if (program.isEmpty()) {
        program = QStandardPaths::findExecutable(QStringLiteral("update-ca-certificates"));
    }
    if (program.isEmpty()) {
        if (error) {
            *error = QStringLiteral("update-ca-trust/update-ca-certificates não encontrado.");
        }
        return false;
    }
    const QString failure = runProcess(program, {}, 60000);
    if (!failure.isEmpty()) {
        if (error) {
            *error = failure;
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

QVariantMap HelperService::changeCaTrust(const QString &certPath, const QString &fingerprint,
                                         bool trustCa)
{
    QString error;
    if (!CaTrust::validateFingerprint(fingerprint, &error)) {
        return result(false, error);
    }

    QByteArray pem;
    if (trustCa) {
        const QFileInfo info(QFileInfo(certPath).canonicalFilePath());
        if (!info.isFile() || !info.isReadable() || info.size() > 1024 * 1024) {
            return result(false, QStringLiteral("Certificado inválido ou inacessível."));
        }
        const uint uid = callerUid();
        if (uid != 0 && info.ownerId() != uid) {
            return result(false, QStringLiteral("O certificado não pertence ao chamador."));
        }
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            return result(false, QStringLiteral("Não foi possível ler o certificado."));
        }
        pem = file.readAll();
        if (CaTrust::fingerprintOf(pem) != CaTrust::normalizeFingerprint(fingerprint)) {
            return result(false, QStringLiteral("O fingerprint não corresponde ao certificado."));
        }
        if (!CaTrust::isSelfSigned(pem, &error)) {
            return result(false, error);
        }
    }

    if (!authorize(QString::fromLatin1(kTrustCaAction), &error)) {
        qCWarning(lcHelper, "%s uid=%u result=denied", trustCa ? "TrustCa" : "UntrustCa",
                  callerUid());
        return result(false, error);
    }

    if (trustCa) {
        if (!CaTrust::trust(caAnchorsDirectory(), pem, fingerprint, &error)) {
            qCWarning(lcHelper, "TrustCa uid=%u result=error", callerUid());
            return result(false, error);
        }
        if (!runTrustUpdate(&error)) {
            return result(false, error);
        }
        qCInfo(lcHelper, "TrustCa uid=%u fingerprint=%s result=ok", callerUid(),
               qUtf8Printable(CaTrust::normalizeFingerprint(fingerprint)));
        return result(true, QStringLiteral("CA local confiada no trust store do host."));
    }

    if (!CaTrust::untrust(caAnchorsDirectory(), fingerprint, &error)) {
        qCWarning(lcHelper, "UntrustCa uid=%u result=error", callerUid());
        return result(false, error);
    }
    if (!runTrustUpdate(&error)) {
        return result(false, error);
    }
    qCInfo(lcHelper, "UntrustCa uid=%u fingerprint=%s result=ok", callerUid(),
           qUtf8Printable(CaTrust::normalizeFingerprint(fingerprint)));
    return result(true, QStringLiteral("CA local removida do trust store do host."));
}

QVariantMap HelperService::changePortBindings(const QStringList &ports, const QString &httpTarget,
                                              const QString &httpsTarget, bool bind)
{
    QString validationError;
    if (!PortBinding::validatePorts(ports, &validationError)) {
        return result(false, validationError);
    }
    if (bind) {
        for (const QString &port : ports) {
            const QString target =
                port == QString::fromLatin1(PortBinding::kHttpPort) ? httpTarget : httpsTarget;
            if (!PortBinding::validateTarget(target, &validationError)) {
                return result(false, validationError);
            }
        }
    }

    QString error;
    if (!authorize(QString::fromLatin1(kBindPortsAction), &error)) {
        qCWarning(lcHelper, "%s uid=%u ports=%s result=denied", bind ? "BindPorts" : "UnbindPorts",
                  callerUid(), qUtf8Printable(ports.join(QLatin1Char(','))));
        return result(false, error);
    }

    const QString directory = systemUnitDirectory();
    if (bind) {
        QString writeError;
        if (!PortBinding::install(directory, ports, httpTarget, httpsTarget, &writeError)) {
            qCWarning(lcHelper, "BindPorts uid=%u ports=%s result=error", callerUid(),
                      qUtf8Printable(ports.join(QLatin1Char(','))));
            return result(false, writeError);
        }
        if (!runSystemctl({QStringLiteral("daemon-reload")}, &error)) {
            return result(false, error);
        }
        for (const QString &port : ports) {
            if (!runSystemctl({QStringLiteral("enable"), QStringLiteral("--now"),
                               PortBinding::socketUnitName(port)},
                              &error)) {
                qCWarning(lcHelper, "BindPorts uid=%u port=%s result=enable-error", callerUid(),
                          qUtf8Printable(port));
                return result(false, error);
            }
        }
        qCInfo(lcHelper, "BindPorts uid=%u ports=%s result=ok", callerUid(),
               qUtf8Printable(ports.join(QLatin1Char(','))));
        return result(true, QStringLiteral("Socket activation ativa nas portas: %1.")
                                .arg(ports.join(QStringLiteral(", "))));
    }

    for (const QString &port : ports) {
        QString ignored;
        if (!runSystemctl({QStringLiteral("disable"), QStringLiteral("--now"),
                           PortBinding::socketUnitName(port)},
                          &ignored)) {
            qCDebug(lcHelper, "UnbindPorts: %s não estava ativa: %s", qUtf8Printable(port),
                    qUtf8Printable(ignored));
        }
    }
    QString removeError;
    if (!PortBinding::remove(directory, ports, &removeError)) {
        qCWarning(lcHelper, "UnbindPorts uid=%u ports=%s result=error", callerUid(),
                  qUtf8Printable(ports.join(QLatin1Char(','))));
        return result(false, removeError);
    }
    if (!runSystemctl({QStringLiteral("daemon-reload")}, &error)) {
        return result(false, error);
    }
    qCInfo(lcHelper, "UnbindPorts uid=%u ports=%s result=ok", callerUid(),
           qUtf8Printable(ports.join(QLatin1Char(','))));
    return result(true, QStringLiteral("Socket activation removida das portas: %1.")
                            .arg(ports.join(QStringLiteral(", "))));
}

QVariantMap HelperService::changeDnsZone(const QString &zone, const QString &upstream,
                                         bool installZone)
{
    QString validationError;
    const QString checkedUpstream =
        installZone ? upstream : QString::fromLatin1(DnsZone::kUpstream);
    if (!DnsZone::validate(zone, checkedUpstream, &validationError)) {
        return result(false, validationError);
    }

    QString error;
    if (!authorize(QString::fromLatin1(kDnsZoneAction), &error)) {
        qCWarning(lcHelper, "%s uid=%u zone=%s result=denied",
                  installZone ? "InstallDnsZone" : "RemoveDnsZone", callerUid(),
                  qUtf8Printable(zone));
        return result(false, error);
    }

    if (installZone) {
        QString writeError;
        if (!DnsZone::install(resolvedDirectory(), zone, upstream, &writeError)) {
            qCWarning(lcHelper, "InstallDnsZone uid=%u zone=%s result=error", callerUid(),
                      qUtf8Printable(zone));
            return result(false, writeError);
        }
        QString reloadError;
        if (!reloadResolved(&reloadError)) {
            qCWarning(lcHelper, "InstallDnsZone uid=%u zone=%s result=reload-error", callerUid(),
                      qUtf8Printable(zone));
            return result(false, reloadError);
        }
        QString message =
            QStringLiteral("Drop-in do resolved instalado para %1 (DNS %2).").arg(zone, upstream);
        if (avahiActive()) {
            message += QStringLiteral(" Avahi está ativo; se houver conflito de nomes, "
                                      "desative um dos serviços.");
        }
        qCInfo(lcHelper, "InstallDnsZone uid=%u zone=%s upstream=%s result=ok", callerUid(),
               qUtf8Printable(zone), qUtf8Printable(upstream));
        return result(true, message);
    }

    QString removeError;
    if (!DnsZone::remove(resolvedDirectory(), zone, &removeError)) {
        qCWarning(lcHelper, "RemoveDnsZone uid=%u zone=%s result=error", callerUid(),
                  qUtf8Printable(zone));
        return result(false, removeError);
    }
    QString reloadError;
    if (!reloadResolved(&reloadError)) {
        qCWarning(lcHelper, "RemoveDnsZone uid=%u zone=%s result=reload-error", callerUid(),
                  qUtf8Printable(zone));
        return result(false, reloadError);
    }
    qCInfo(lcHelper, "RemoveDnsZone uid=%u zone=%s result=ok", callerUid(), qUtf8Printable(zone));
    return result(true, QStringLiteral("Drop-in do resolved removido para %1.").arg(zone));
}

QVariantMap HelperService::InstallDnsZone(const QString &zone, const QString &upstream)
{
    return changeDnsZone(zone, upstream, true);
}

QVariantMap HelperService::RemoveDnsZone(const QString &zone)
{
    return changeDnsZone(zone, QString(), false);
}

QVariantMap HelperService::BindPorts(const QStringList &ports, const QString &httpTarget,
                                     const QString &httpsTarget)
{
    return changePortBindings(ports, httpTarget, httpsTarget, true);
}

QVariantMap HelperService::UnbindPorts(const QStringList &ports)
{
    return changePortBindings(ports, QString(), QString(), false);
}

QVariantMap HelperService::TrustCa(const QString &certPath, const QString &fingerprint)
{
    return changeCaTrust(certPath, fingerprint, true);
}

QVariantMap HelperService::UntrustCa(const QString &fingerprint)
{
    return changeCaTrust(QString(), fingerprint, false);
}

} // namespace PodStack
