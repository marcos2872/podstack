// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/QemuRuntime.h"

#include "machines/QmpClient.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <chrono>
#include <csignal>
#include <thread>
#include <sys/types.h>

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

/// Confirma que o PID ainda pertence à machine, evitando um PID reciclado.
bool processMatches(qint64 pid, const QString &machineId)
{
    if (pid <= 0 || machineId.isEmpty()) {
        return false;
    }

    QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (!cmdline.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray payload = cmdline.read(kCmdlineMaxBytes);
    if (payload.isEmpty()) {
        return false;
    }
    return payload.contains(QByteArrayLiteral("podstack:") + machineId.toUtf8());
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
    if (!QProcess::startDetached(executable, arguments, QString(), &launchedPid)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível iniciar o QEMU."), executable);
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

    qint64 pid = 0;
    QString detail;
    if (readPidFile(spec.pidFile, &pid, &detail) != PidRead::Ok
        || !processMatches(pid, spec.machineId)) {
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

    ::kill(static_cast<pid_t>(pid), SIGTERM);
    if (waitForExit(pid, spec.machineId, kTermTimeoutMs)) {
        removePidFile(spec);
        return true;
    }

    if (processMatches(pid, spec.machineId)) {
        ::kill(static_cast<pid_t>(pid), SIGKILL);
    }
    if (waitForExit(pid, spec.machineId, kKillTimeoutMs)) {
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

    qint64 pid = 0;
    if (readPidFile(spec.pidFile, &pid, nullptr) != PidRead::Ok) {
        return false;
    }
    return processMatches(pid, spec.machineId);
}

QString QemuProcessRuntime::status(const QemuSpec &spec, Error *error)
{
    if (error) {
        *error = {};
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
    qint64 pid = 0;
    if (readPidFile(spec.pidFile, &pid, nullptr) != PidRead::Ok
        || !processMatches(pid, spec.machineId)) {
        return {};
    }
    return QString::number(pid);
}

} // namespace PodStack
