// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/QemuRuntime.h"

#include "core/Logging.h"
#include "machines/ProcessScope.h"
#include "machines/QmpClient.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <chrono>
#include <csignal>
#include <thread>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace PodStack {
namespace {

constexpr int kPidFileMaxBytes = 64;
constexpr int kCmdlineMaxBytes = 64 * 1024;
constexpr int kPollIntervalMs = 50;

enum class PidRead {
    Missing,
    Invalid,
    Ok,
};

void sleepFor(int milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

PidRead readPidFile(const QString &path, qint64 *pid, QString *detail)
{
    if (pid) {
        *pid = 0;
    }

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        if (detail) {
            *detail = QStringLiteral("pidfile ausente: %1").arg(path);
        }
        return PidRead::Missing;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (detail) {
            *detail = QStringLiteral("não foi possível ler o pidfile: %1").arg(file.errorString());
        }
        return PidRead::Invalid;
    }

    const QByteArray payload = file.read(kPidFileMaxBytes + 1);
    if (payload.size() > kPidFileMaxBytes) {
        if (detail) {
            *detail = QStringLiteral("pidfile acima do limite de %1 bytes").arg(kPidFileMaxBytes);
        }
        return PidRead::Invalid;
    }

    bool ok = false;
    const qint64 value = payload.trimmed().toLongLong(&ok);
    if (!ok || value <= 0) {
        if (detail) {
            *detail = QStringLiteral("pidfile inválido");
        }
        return PidRead::Invalid;
    }

    if (pid) {
        *pid = value;
    }
    return PidRead::Ok;
}

/// Lê o cmdline e extrai o id de uma machine marcada por um argumento
/// exatamente igual a `podstack:<id>`. Só aceita processos do usuário atual:
/// a varredura nunca deve alcançar o processo de outro usuário.
bool processMachineId(qint64 pid, QString *machineId)
{
    if (pid <= 0) {
        return false;
    }

    const QString processPath = QStringLiteral("/proc/%1").arg(pid);
    struct stat status {};
    const QByteArray nativePath = QFile::encodeName(processPath);
    if (::stat(nativePath.constData(), &status) != 0
        || status.st_uid != ::getuid()) {
        return false;
    }

    QFile cmdline(processPath + QStringLiteral("/cmdline"));
    if (!cmdline.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray payload = cmdline.read(kCmdlineMaxBytes + 1);
    if (payload.isEmpty() || payload.size() > kCmdlineMaxBytes) {
        return false;
    }

    static const QRegularExpression validId(QStringLiteral("^[A-Za-z0-9._:-]+$"));
    QSet<QString> ids;
    const QList<QByteArray> tokens = payload.split('\0');
    for (const QByteArray &token : tokens) {
        if (!token.startsWith(QByteArrayLiteral("podstack:"))) {
            continue;
        }
        const QString id = QString::fromUtf8(token.mid(9)).trimmed();
        if (id.isEmpty() || id.contains(QStringLiteral(".."))
            || !validId.match(id).hasMatch()) {
            continue;
        }
        ids.insert(id);
    }

    // Um processo com marcadores de machines diferentes é ambíguo; ignorar
    // evita encerrar o processo errado.
    if (ids.size() != 1) {
        return false;
    }
    if (machineId) {
        *machineId = *ids.constBegin();
    }
    return true;
}

/// Confirma que o PID ainda pertence à machine, evitando um PID reciclado.
bool processMatches(qint64 pid, const QString &machineId)
{
    if (pid <= 0 || machineId.isEmpty()) {
        return false;
    }

    QString foundId;
    return processMachineId(pid, &foundId) && foundId == machineId;
}

QHash<QString, qint64> scanRunningMachines()
{
    QHash<QString, qint64> running;
    const QDir proc(QStringLiteral("/proc"));
    const QStringList entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    const qint64 self = static_cast<qint64>(::getpid());

    for (const QString &entry : entries) {
        bool ok = false;
        const qint64 pid = entry.toLongLong(&ok);
        if (!ok || pid <= 0 || pid == self) {
            continue;
        }

        QString machineId;
        if (!processMachineId(pid, &machineId)) {
            continue;
        }
        if (running.contains(machineId)) {
            qCWarning(lcCore) << "Mais de um processo para a machine" << machineId
                              << "; mantendo o pid" << running.value(machineId);
            continue;
        }
        running.insert(machineId, pid);
    }
    return running;
}

qint64 knownPid(const QemuSpec &spec)
{
    qint64 pid = 0;
    if (readPidFile(spec.pidFile, &pid, nullptr) == PidRead::Ok
        && processMatches(pid, spec.machineId)) {
        return pid;
    }
    return scanRunningMachines().value(spec.machineId, 0);
}

bool waitForPid(const QemuSpec &spec, qint64 *pid, QString *detail)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < QemuProcessRuntime::kPidFileTimeoutMs) {
        qint64 candidate = 0;
        QString readDetail;
        const PidRead result = readPidFile(spec.pidFile, &candidate, &readDetail);
        if (result == PidRead::Ok) {
            if (processMatches(candidate, spec.machineId)) {
                if (pid) {
                    *pid = candidate;
                }
                return true;
            }
            readDetail = QStringLiteral("PID %1 não corresponde a podstack:%2")
                             .arg(candidate)
                             .arg(spec.machineId);
        }
        if (detail) {
            *detail = readDetail;
        }
        sleepFor(kPollIntervalMs);
    }
    return false;
}

bool waitForQmp(const QemuSpec &spec, QString *detail)
{
    QElapsedTimer timer;
    timer.start();

    QString lastError = QStringLiteral("socket QMP não apareceu: %1").arg(spec.qmpSocketPath);
    while (timer.elapsed() < QemuProcessRuntime::kQmpTimeoutMs) {
        QmpClient client(spec.qmpSocketPath);
        client.setConnectTimeout(500);
        client.setCommandTimeout(500);

        Error connectError;
        if (client.connect(500, &connectError)) {
            QString status;
            Error statusError;
            if (client.queryStatus(&status, &statusError) && !status.isEmpty()) {
                return true;
            }
            lastError = statusError.isEmpty()
                            ? QStringLiteral("query-status retornou vazio")
                            : statusError.message;
        } else {
            lastError = connectError.message;
        }
        sleepFor(kPollIntervalMs);
    }

    if (detail) {
        *detail = lastError;
    }
    return false;
}

bool waitForExit(qint64 pid, const QString &machineId, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        if (!processMatches(pid, machineId)) {
            return true;
        }
        sleepFor(100);
    }
    return !processMatches(pid, machineId);
}

bool terminateProcess(qint64 pid, const QString &machineId, int termTimeoutMs,
                      int killTimeoutMs)
{
    if (!processMatches(pid, machineId)) {
        return true;
    }

    ::kill(static_cast<pid_t>(pid), SIGTERM);
    if (waitForExit(pid, machineId, termTimeoutMs)) {
        return true;
    }

    if (processMatches(pid, machineId)) {
        ::kill(static_cast<pid_t>(pid), SIGKILL);
    }
    return waitForExit(pid, machineId, killTimeoutMs);
}

void removePidFile(const QemuSpec &spec)
{
    QFile::remove(spec.pidFile);
}

} // namespace

bool QemuProcessRuntime::start(const QemuSpec &spec, Error *error)
{
    if (error) {
        *error = {};
    }

    if (isRunning(spec, nullptr)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("A machine já está em execução."),
                                 spec.machineId);
        }
        return false;
    }

    const QString binary = QemuCommand::binary(spec.arch, error);
    if (binary.isEmpty()) {
        return false;
    }

    const QString executable = QStandardPaths::findExecutable(binary);
    if (executable.isEmpty()) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_UNSUPPORTED"),
                QStringLiteral("O binário %1 não foi encontrado.").arg(binary), binary,
                QStringLiteral("Instale o QEMU/KVM (qemu-kvm no Fedora ou qemu-system-x86 "
                               "no Debian/Ubuntu)."));
        }
        return false;
    }

    const QStringList arguments = QemuCommand::arguments(spec, error);
    if (arguments.isEmpty()) {
        return false;
    }

    // Um pidfile/socket remanescente de uma execução anterior não deve impedir o boot.
    QFile::remove(spec.pidFile);
    QFile::remove(spec.qmpSocketPath);

    qint64 launchedPid = 0;
    QString launchFailure;
    const QString scopeUnit = ProcessScope::unitName(QStringLiteral("podstack-machine"),
                                                     spec.machineId);
    if (!ProcessScope::startDetached(scopeUnit, executable, arguments, &launchedPid,
                                     &launchFailure)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível iniciar o QEMU."), executable,
                                 launchFailure);
        }
        return false;
    }

    qint64 pid = 0;
    QString detail;
    if (!waitForPid(spec, &pid, &detail)) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_TIMEOUT"),
                QStringLiteral("O QEMU não publicou um pidfile válido a tempo."), detail,
                QStringLiteral("Verifique o console.log da machine."));
        }
        return false;
    }

    if (!waitForQmp(spec, &detail)) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_TIMEOUT"),
                QStringLiteral("O QMP não respondeu dentro do tempo esperado."), detail,
                QStringLiteral("Verifique o console.log da machine."));
        }
        if (processMatches(pid, spec.machineId)) {
            ::kill(static_cast<pid_t>(pid), SIGTERM);
        }
        return false;
    }

    return true;
}

bool QemuProcessRuntime::stop(const QemuSpec &spec, bool force, Error *error)
{
    if (error) {
        *error = {};
    }

    qint64 pid = knownPid(spec);
    if (pid <= 0) {
        removePidFile(spec);
        return true;
    }

    QmpClient client(spec.qmpSocketPath);
    Error ignored;
    const bool connected = client.connect(500, &ignored);

    if (force) {
        if (connected) {
            client.quit(&ignored);
            if (waitForExit(pid, spec.machineId, kQuitTimeoutMs)) {
                removePidFile(spec);
                return true;
            }
        }
    } else if (connected) {
        client.systemPowerdown(&ignored);
        if (waitForExit(pid, spec.machineId, kPowerdownTimeoutMs)) {
            removePidFile(spec);
            return true;
        }

        client.quit(&ignored);
        if (waitForExit(pid, spec.machineId, kQuitTimeoutMs)) {
            removePidFile(spec);
            return true;
        }
    } else {
        QmpClient quitClient(spec.qmpSocketPath);
        if (quitClient.connect(500, &ignored) && quitClient.quit(&ignored)
            && waitForExit(pid, spec.machineId, kQuitTimeoutMs)) {
            removePidFile(spec);
            return true;
        }
    }

    if (!processMatches(pid, spec.machineId)) {
        removePidFile(spec);
        return true;
    }

    if (terminateProcess(pid, spec.machineId, kTermTimeoutMs, kKillTimeoutMs)) {
        removePidFile(spec);
        return true;
    }

    if (error) {
        *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                             QStringLiteral("Não foi possível encerrar o processo do QEMU."),
                             QStringLiteral("pid %1").arg(pid));
    }
    return false;
}

bool QemuProcessRuntime::isRunning(const QemuSpec &spec, Error *error)
{
    if (error) {
        *error = {};
    }

    return knownPid(spec) > 0;
}

QString QemuProcessRuntime::status(const QemuSpec &spec, Error *error)
{
    if (error) {
        *error = {};
    }

    if (knownPid(spec) <= 0) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("A machine não está em execução."),
                                 spec.machineId);
        }
        return {};
    }

    QmpClient client(spec.qmpSocketPath);
    if (!client.connect(error)) {
        return {};
    }

    QString status;
    if (!client.queryStatus(&status, error)) {
        return {};
    }
    return status;
}

QString QemuProcessRuntime::pid(const QemuSpec &spec)
{
    const qint64 runningPid = knownPid(spec);
    return runningPid > 0 ? QString::number(runningPid) : QString();
}

QHash<QString, qint64> QemuProcessRuntime::runningMachines()
{
    return scanRunningMachines();
}

qint64 QemuProcessRuntime::findPid(const QString &machineId)
{
    if (machineId.isEmpty()) {
        return 0;
    }
    return scanRunningMachines().value(machineId, 0);
}

bool QemuProcessRuntime::terminate(const QString &machineId, qint64 pid, Error *error)
{
    if (error) {
        *error = {};
    }
    if (machineId.isEmpty() || pid <= 0) {
        return true;
    }

    const bool ok = terminateProcess(pid, machineId, kOrphanTermTimeoutMs,
                                     kOrphanKillTimeoutMs);
    if (!ok && error) {
        *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                             QStringLiteral("Não foi possível encerrar o processo órfão."),
                             QStringLiteral("pid %1").arg(pid));
    }
    return ok;
}

} // namespace PodStack
