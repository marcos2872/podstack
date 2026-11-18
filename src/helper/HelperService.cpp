// SPDX-License-Identifier: GPL-3.0-or-later

#include "helper/HelperService.h"

#include "core/error/Error.h"
#include "helper/ca/CaTrust.h"
#include "helper/dns/DnsZone.h"
#include "helper/polkit/Polkit.h"
#include "helper/ports/Firewall.h"
#include "helper/ports/PortBinding.h"
#include "helper/updates/UpdateService.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLoggingCategory>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStringList>
#include <QtGlobal>

#include <algorithm>
#include <mutex>

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
constexpr auto kStageUpdateAction = "dev.podstack.helper.stage-update";
constexpr auto kInstallUpdateAction = "dev.podstack.helper.install-update";
constexpr auto kUpdateStatusAction = "dev.podstack.helper.get-update-status";
constexpr auto kGetLogsAction = "dev.podstack.helper.get-logs";

const QStringList &installAllowlist()
{
    static const QStringList packages = {
        QStringLiteral("podman"),
        QStringLiteral("qemu-kvm"),
        QStringLiteral("qemu-system-x86_64"),
        QStringLiteral("qemu-system-aarch64"),
        QStringLiteral("virtiofsd"),
        QStringLiteral("edk2-ovmf"),
        QStringLiteral("ovmf"),
        QStringLiteral("qemu-system-x86"),
        QStringLiteral("qemu-system-arm"),
        QStringLiteral("qemu-utils"),
        QStringLiteral("qemu-efi-aarch64"),
        QStringLiteral("qemu-img"),
        QStringLiteral("qemu-full"),
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

QString runProcess(const QString &program, const QStringList &arguments, int timeoutMs,
                   const QProcessEnvironment &environment = QProcessEnvironment())
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    if (!environment.isEmpty()) {
        process.setProcessEnvironment(environment);
    }
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

/// Confirma que `pid` existe, pertence a `uid` e não foi reciclado. `/proc/<pid>`
/// expõe o UID real em `status`; o `start-time` em `stat` não é confiável para
/// detectar reciclagem quando o PID foi informado pelo chamador, então o polkit
/// também recebe um subject com `start-time` (ver `checkAuthorization`).
bool ownsProcess(uint pid, uint uid, QString *error)
{
    if (pid == 0 || uid == 0) {
        if (error) {
            *error = QStringLiteral("Processo ou UID inválido para autorização.");
        }
        return false;
    }

    QFile status(QStringLiteral("/proc/%1/status").arg(pid));
    if (!status.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("O processo %1 não existe mais.").arg(pid);
        }
        return false;
    }

    // Formato: "Uid:\t1000\t1000\t1000\t1000" (real, efetivo, salvo, fs).
    for (const QByteArray &line : status.readAll().split('\n')) {
        if (!line.startsWith("Uid:")) {
            continue;
        }
        const QList<QByteArray> fields = line.mid(4).simplified().split(' ');
        bool ok = false;
        const uint realUid = fields.isEmpty() ? 0 : fields.constFirst().toUInt(&ok);
        if (!ok || realUid != uid) {
            if (error) {
                *error = QStringLiteral("O processo %1 não pertence ao chamador.").arg(pid);
            }
            return false;
        }
        return true;
    }

    if (error) {
        *error = QStringLiteral("Não foi possível ler o dono do processo %1.").arg(pid);
    }
    return false;
}

bool updateRateLimit(uint uid, const QString &method, int limit, QString *error)
{
    static std::mutex mutex;
    static QHash<QString, QVector<qint64>> calls;

    const QString key = QString::number(uid) + QLatin1Char(':') + method;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    std::lock_guard<std::mutex> lock(mutex);
    QVector<qint64> &entries = calls[key];
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [now](qint64 timestamp) { return now - timestamp > 60000; }),
                  entries.end());
    if (entries.size() >= limit) {
        if (error) {
            *error = QStringLiteral("Muitas chamadas de update; tente novamente em instantes.");
        }
        return false;
    }
    entries.append(now);
    return true;
}

} // namespace

HelperService::HelperService(QObject *parent)
    : QObject(parent)
{
}

bool HelperService::authorizationPid(uint callerPid, uint *pid, QString *error) const
{
    const uint sender = senderPid();
    if (callerPid == 0 || callerPid == sender) {
        if (sender == 0) {
            if (error) {
                *error = QStringLiteral("Não foi possível identificar o processo chamador.");
            }
            return false;
        }
        *pid = sender;
        return true;
    }

    // O daemon repassa o PID da GUI porque ele mesmo roda fora da sessão e o
    // polkit não encontra agente a partir dele. Só aceitamos o PID informado se
    // ele pertencer ao mesmo UID do remetente: isso impede que qualquer processo
    // faça o polkit autorizar a sessão de outro usuário.
    //
    // Falha fechada, sem cair para o remetente: um PID alheio é tentativa de
    // autorizar outra sessão, e insistir com o subject do remetente esconderia
    // o problema atrás de uma recusa genérica do polkit.
    if (!ownsProcess(callerPid, callerUid(), error)) {
        return false;
    }
    *pid = callerPid;
    return true;
}

bool HelperService::authorize(const QString &actionId, uint callerPid, QString *error) const
{
    if (!calledFromDBus()) {
        if (error) {
            *error = QStringLiteral("Chamada local não autorizada.");
        }
        return false;
    }

    if (!QDBusConnection::systemBus().interface()) {
        if (error) {
            *error = QStringLiteral("Barramento de sistema indisponível.");
        }
        return false;
    }

    uint pid = 0;
    if (!authorizationPid(callerPid, &pid, error)) {
        if (error && error->isEmpty()) {
            *error = QStringLiteral("Não foi possível identificar o processo chamador.");
        }
        return false;
    }

    return checkAuthorization(actionId, pid, error);
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

bool HelperService::installedPackages(const QStringList &packages, QStringList *installed,
                                      QString *error)
{
    if (installed) {
        installed->clear();
    }
    if (error) {
        error->clear();
    }

    const QString manager = packageManager();
    QString tool;
    QStringList queryArguments;
    if (manager == QStringLiteral("dnf")) {
        tool = QStringLiteral("rpm");
        queryArguments = {QStringLiteral("-q")};
    } else if (manager == QStringLiteral("apt-get")) {
        tool = QStringLiteral("dpkg-query");
        queryArguments = {QStringLiteral("-W"), QStringLiteral("-f=${Status}")};
    } else if (manager == QStringLiteral("pacman")) {
        tool = QStringLiteral("pacman");
        queryArguments = {QStringLiteral("-Q")};
    } else {
        if (error) {
            *error = QStringLiteral("Gerenciador de pacotes não suportado.");
        }
        return false;
    }

    if (QStandardPaths::findExecutable(tool).isEmpty()) {
        if (error) {
            *error =
                QStringLiteral("Ferramenta de consulta de pacotes não encontrada: %1.").arg(tool);
        }
        return false;
    }

    for (const QString &package : packages) {
        QProcess query;
        query.setProcessChannelMode(QProcess::MergedChannels);
        query.start(tool, queryArguments + QStringList{package});
        if (!query.waitForFinished(5000)) {
            query.kill();
            query.waitForFinished(1000);
            if (error) {
                *error = QStringLiteral("Tempo esgotado ao consultar o pacote %1.").arg(package);
            }
            return false;
        }
        if (query.exitStatus() != QProcess::NormalExit) {
            if (error) {
                *error = QStringLiteral("Falha ao consultar o pacote %1.").arg(package);
            }
            return false;
        }

        // Exit != 0 significa "não instalado" em rpm/dpkg/pacman; no Debian a
        // saída de dpkg-query também precisa indicar instalação concluída.
        bool present = query.exitCode() == 0;
        if (present && manager == QStringLiteral("apt-get")) {
            present =
                QString::fromUtf8(query.readAll()).contains(QStringLiteral("install ok installed"));
        }
        if (present && installed) {
            installed->append(package);
        }
    }
    return true;
}

QVariantMap HelperService::install(const QStringList &packages, const QString &actionId,
                                   const QStringList &allowed, uint callerPid)
{
    QString error;
    if (!validatePackages(packages, allowed, &error)) {
        return result(false, error);
    }

    const uint uid = callerUid();
    qCInfo(lcHelper, "installation requested: uid=%u packages=%s", uid,
           qUtf8Printable(packages.join(',')));

    if (!authorize(actionId, callerPid, &error)) {
        qCInfo(lcHelper, "installation denied: uid=%u packages=%s", uid,
               qUtf8Printable(packages.join(',')));
        return result(false, error);
    }

    const QString manager = packageManager();
    if (manager.isEmpty()) {
        return result(false, QStringLiteral("Gerenciador de pacotes não suportado."));
    }

    qCInfo(lcHelper, "installation started: uid=%u packages=%s", uid,
           qUtf8Printable(packages.join(',')));

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

    QString failure;
    if (manager == QStringLiteral("apt-get")) {
        // Imagens novas podem ter índices vazios/velhos; sem o update o
        // "apt-get install podman" falha com "Unable to locate package".
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("DEBIAN_FRONTEND"), QStringLiteral("noninteractive"));
        const QString updateFailure =
            runProcess(manager, {QStringLiteral("update")}, 300000, environment);
        if (!updateFailure.isEmpty()) {
            qCInfo(lcHelper, "apt update failed: uid=%u packages=%s", uid,
                   qUtf8Printable(packages.join(',')));
            return result(
                false,
                QStringLiteral("Falha ao atualizar os índices do apt: %1").arg(updateFailure));
        }
        failure = runProcess(manager, arguments, 900000, environment);
    } else {
        failure = runProcess(manager, arguments, 900000);
    }
    if (!failure.isEmpty()) {
        qCInfo(lcHelper, "installation failed: uid=%u packages=%s", uid,
               qUtf8Printable(packages.join(',')));
        return result(false, QStringLiteral("Falha ao instalar: %1").arg(failure));
    }
    qCInfo(lcHelper, "installation finished: uid=%u packages=%s", uid,
           qUtf8Printable(packages.join(',')));
    return result(true, QStringLiteral("Pacotes instalados: %1")
                            .arg(packages.join(QStringLiteral(", "))));
}

QVariantMap HelperService::InstallPackages(const QStringList &packages, uint callerPid)
{
    return install(packages, QString::fromLatin1(kInstallAction), installAllowlist(), callerPid);
}

QVariantMap HelperService::RemovePackages(const QStringList &packages, uint callerPid)
{
    QString error;
    if (!validatePackages(packages, removeAllowlist(), &error)) {
        return result(false, error);
    }

    const uint uid = callerUid();
    qCInfo(lcHelper, "removal requested: uid=%u packages=%s", uid,
           qUtf8Printable(packages.join(',')));

    if (!authorize(QString::fromLatin1(kRemoveAction), callerPid, &error)) {
        qCInfo(lcHelper, "removal denied: uid=%u packages=%s", uid,
               qUtf8Printable(packages.join(',')));
        return result(false, error);
    }

    QStringList installed;
    QString queryError;
    if (!installedPackages(packages, &installed, &queryError)) {
        qCWarning(lcHelper, "removal query failed: uid=%u packages=%s error=%s", uid,
                  qUtf8Printable(packages.join(',')), qUtf8Printable(queryError));
        return result(false, queryError);
    }
    if (installed.isEmpty()) {
        qCInfo(lcHelper, "removal skipped: uid=%u packages=%s reason=not-installed", uid,
               qUtf8Printable(packages.join(',')));
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

    qCInfo(lcHelper, "removal started: uid=%u packages=%s", uid,
           qUtf8Printable(installed.join(',')));

    QStringList arguments;
    if (manager == QStringLiteral("dnf") || manager == QStringLiteral("apt-get")) {
        arguments = QStringList{QStringLiteral("remove"), QStringLiteral("-y")};
    } else {
        arguments = QStringList{QStringLiteral("-R"), QStringLiteral("--noconfirm")};
    }
    arguments += installed;

    const QString failure = runProcess(manager, arguments, 900000);
    if (!failure.isEmpty()) {
        qCInfo(lcHelper, "removal failed: uid=%u packages=%s", uid,
               qUtf8Printable(installed.join(',')));
        return result(false, QStringLiteral("Falha ao remover o Docker: %1").arg(failure));
    }
    qCInfo(lcHelper, "removal finished: uid=%u packages=%s", uid,
           qUtf8Printable(installed.join(',')));
    return result(true, QStringLiteral("Pacotes removidos: %1")
                            .arg(installed.join(QStringLiteral(", "))));
}

uint HelperService::senderPid() const
{
    if (!calledFromDBus()) {
        return 0;
    }
    QDBusConnectionInterface *bus = QDBusConnection::systemBus().interface();
    if (!bus) {
        return 0;
    }
    const QDBusReply<uint> reply = bus->servicePid(message().service());
    return reply.isValid() ? reply.value() : 0;
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
                                       const QString &actionId, bool open, uint callerPid)
{
    QStringList normalized;
    Error validationError;
    if (!Firewall::validatePorts(ports, protocol, &normalized, &validationError)) {
        return result(false, validationError.message);
    }
    const QString normalizedProtocol = protocol.trimmed().toLower();

    QString error;
    if (!authorize(actionId, callerPid, &error)) {
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

QVariantMap HelperService::OpenPorts(const QStringList &ports, const QString &protocol,
                                     uint callerPid)
{
    return changePorts(ports, protocol, QString::fromLatin1(kOpenPortsAction), true, callerPid);
}

QVariantMap HelperService::ClosePorts(const QStringList &ports, const QString &protocol,
                                      uint callerPid)
{
    return changePorts(ports, protocol, QString::fromLatin1(kClosePortsAction), false, callerPid);
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
                                         bool trustCa, uint callerPid)
{
    QString error;
    if (!CaTrust::validateFingerprint(fingerprint, &error)) {
        return result(false, error);
    }

    QByteArray pem;
    if (trustCa) {
        const QFileInfo info(QFileInfo(certPath).canonicalFilePath());
        if (!info.isFile() || !info.isReadable() || info.size() > 1024LL * 1024) {
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

    if (!authorize(QString::fromLatin1(kTrustCaAction), callerPid, &error)) {
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
                                              const QString &httpsTarget, bool bind, uint callerPid)
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
    if (!authorize(QString::fromLatin1(kBindPortsAction), callerPid, &error)) {
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
                                         bool installZone, uint callerPid)
{
    QString validationError;
    const QString checkedUpstream =
        installZone ? upstream : QString::fromLatin1(DnsZone::kUpstream);
    if (!DnsZone::validate(zone, checkedUpstream, &validationError)) {
        return result(false, validationError);
    }

    QString error;
    if (!authorize(QString::fromLatin1(kDnsZoneAction), callerPid, &error)) {
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

QVariantMap HelperService::InstallDnsZone(const QString &zone, const QString &upstream,
                                          uint callerPid)
{
    return changeDnsZone(zone, upstream, true, callerPid);
}

QVariantMap HelperService::RemoveDnsZone(const QString &zone, uint callerPid)
{
    return changeDnsZone(zone, QString(), false, callerPid);
}

QVariantMap HelperService::BindPorts(const QStringList &ports, const QString &httpTarget,
                                     const QString &httpsTarget, uint callerPid)
{
    return changePortBindings(ports, httpTarget, httpsTarget, true, callerPid);
}

QVariantMap HelperService::UnbindPorts(const QStringList &ports, uint callerPid)
{
    return changePortBindings(ports, QString(), QString(), false, callerPid);
}

QVariantMap HelperService::TrustCa(const QString &certPath, const QString &fingerprint,
                                   uint callerPid)
{
    return changeCaTrust(certPath, fingerprint, true, callerPid);
}

QVariantMap HelperService::UntrustCa(const QString &fingerprint, uint callerPid)
{
    return changeCaTrust(QString(), fingerprint, false, callerPid);
}

UpdateService *HelperService::updateService()
{
    if (!m_updateService) {
        m_updateService = std::make_unique<UpdateService>();
    }
    return m_updateService.get();
}

QVariantMap HelperService::updateFailure(const Error &error)
{
    QVariantMap map = result(false, error.message);
    map.insert(QStringLiteral("code"), error.code);
    map.insert(QStringLiteral("detail"), error.detail);
    return map;
}

QVariantMap HelperService::StageUpdate(const QByteArray &manifest, const QByteArray &signature,
                                       const QString &artifactPath, const QString &channel,
                                       uint callerPid)
{
    const uint uid = callerUid();
    QString error;
    if (!authorize(QString::fromLatin1(kStageUpdateAction), callerPid, &error)) {
        qCWarning(lcHelper, "StageUpdate uid=%u result=denied", uid);
        return result(false, error);
    }
    if (!updateRateLimit(uid, QStringLiteral("stage"), 10, &error)) {
        return result(false, error);
    }

    QVariantMap reply;
    Error updateError;
    if (!updateService()->stage(uid, manifest, signature, artifactPath, channel, &reply,
                                &updateError)) {
        qCWarning(lcHelper, "StageUpdate uid=%u result=error code=%s", uid,
                  qUtf8Printable(updateError.code));
        return updateFailure(updateError);
    }
    qCInfo(lcHelper, "StageUpdate uid=%u job=%s result=ok", uid,
           qUtf8Printable(reply.value(QStringLiteral("jobId")).toString()));
    return reply;
}

QVariantMap HelperService::InstallUpdate(const QString &jobId, uint callerPid)
{
    const uint uid = callerUid();
    QString error;
    if (!authorize(QString::fromLatin1(kInstallUpdateAction), callerPid, &error)) {
        qCWarning(lcHelper, "InstallUpdate uid=%u job=%s result=denied", uid,
                  qUtf8Printable(jobId));
        return result(false, error);
    }
    if (!updateRateLimit(uid, QStringLiteral("install"), 10, &error)) {
        return result(false, error);
    }

    QVariantMap reply;
    Error updateError;
    if (!updateService()->install(uid, jobId, &reply, &updateError)) {
        qCWarning(lcHelper, "InstallUpdate uid=%u job=%s result=error code=%s", uid,
                  qUtf8Printable(jobId), qUtf8Printable(updateError.code));
        return updateFailure(updateError);
    }
    qCInfo(lcHelper, "InstallUpdate uid=%u job=%s unit=%s result=ok", uid, qUtf8Printable(jobId),
           qUtf8Printable(reply.value(QStringLiteral("unit")).toString()));
    return reply;
}

QVariantMap HelperService::GetUpdateStatus(const QString &jobId, uint callerPid)
{
    const uint uid = callerUid();
    QString error;
    if (!authorize(QString::fromLatin1(kUpdateStatusAction), callerPid, &error)) {
        qCWarning(lcHelper, "GetUpdateStatus uid=%u job=%s result=denied", uid,
                  qUtf8Printable(jobId));
        return result(false, error);
    }
    if (!updateRateLimit(uid, QStringLiteral("status"), 120, &error)) {
        return result(false, error);
    }

    QVariantMap reply;
    Error updateError;
    if (!updateService()->status(uid, jobId, &reply, &updateError)) {
        return updateFailure(updateError);
    }
    return reply;
}

QVariantMap HelperService::GetLogs(const QString &jobId, uint callerPid)
{
    const uint uid = callerUid();
    QString error;
    if (!authorize(QString::fromLatin1(kGetLogsAction), callerPid, &error)) {
        qCWarning(lcHelper, "GetLogs uid=%u job=%s result=denied", uid, qUtf8Printable(jobId));
        return result(false, error);
    }
    if (!updateRateLimit(uid, QStringLiteral("logs"), 10, &error)) {
        return result(false, error);
    }

    QVariantList lines;
    bool truncated = false;
    Error logsError;
    if (!updateService()->logs(uid, jobId, &lines, &truncated, &logsError)) {
        return updateFailure(logsError);
    }
    qCInfo(lcHelper, "GetLogs uid=%u job=%s lines=%d truncated=%d result=ok", uid,
           qUtf8Printable(jobId), static_cast<int>(lines.size()), truncated ? 1 : 0);
    QVariantMap reply;
    reply.insert(QStringLiteral("ok"), true);
    reply.insert(QStringLiteral("message"), QString());
    reply.insert(QStringLiteral("lines"), lines);
    reply.insert(QStringLiteral("truncated"), truncated);
    return reply;
}

} // namespace PodStack
