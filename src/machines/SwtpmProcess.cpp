// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/SwtpmProcess.h"

#include "core/Logging.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <chrono>
#include <csignal>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

namespace PodStack {
namespace {

constexpr int kSocketTimeoutMs = 5000;
constexpr int kKillTimeoutMs = 2000;
constexpr int kPollIntervalMs = 20;
constexpr int kExitPollIntervalMs = 50;
constexpr int kCmdlineMaxBytes = 64 * 1024;

Error invalidParams(const QString &message, const QString &detail = {},
                    const QString &remediation = {})
{
    return Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"), message, detail, remediation);
}

void sleepFor(int milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

/// Confirma que o PID pertence a esta machine: mesmo uid e o caminho do socket
/// de controle presente no `cmdline` (o argumento é `type=unixio,path=...,mode=...`).
bool processMatches(qint64 pid, const QString &socketPath)
{
    if (pid <= 0 || socketPath.isEmpty()) {
        return false;
    }

    const QString processPath = QStringLiteral("/proc/%1").arg(pid);
    struct stat status{};
    const QByteArray nativePath = QFile::encodeName(processPath);
    if (::stat(nativePath.constData(), &status) != 0 || status.st_uid != ::getuid()) {
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

    const QByteArray expected = QFile::encodeName(socketPath);
    const QList<QByteArray> arguments = payload.split('\0');
    for (const QByteArray &argument : arguments) {
        if (argument.contains(expected)) {
            return true;
        }
    }
    return false;
}

bool processExists(qint64 pid)
{
    if (pid <= 0) {
        return false;
    }
    const QString processPath = QStringLiteral("/proc/%1").arg(pid);
    struct stat status{};
    const QByteArray nativePath = QFile::encodeName(processPath);
    if (::stat(nativePath.constData(), &status) != 0 || status.st_uid != ::getuid()) {
        return false;
    }

    QFile statFile(processPath + QStringLiteral("/stat"));
    if (statFile.open(QIODevice::ReadOnly)) {
        const QByteArray stat = statFile.read(4096);
        const int closeParen = stat.lastIndexOf(')');
        if (closeParen >= 0 && closeParen + 2 < stat.size()) {
            const char state = stat.at(closeParen + 2);
            if (state == 'Z' || state == 'X') {
                return false;
            }
        }
    }
    return true;
}

bool waitForSocket(qint64 pid, const QString &socketPath, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (QFileInfo::exists(socketPath) && processMatches(pid, socketPath)) {
            return true;
        }
        if (!processExists(pid)) {
            return false;
        }
        sleepFor(kPollIntervalMs);
    }
    return QFileInfo::exists(socketPath) && processMatches(pid, socketPath);
}

bool waitForExit(qint64 pid, const QString &socketPath, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (!processMatches(pid, socketPath)) {
            return true;
        }
        sleepFor(kExitPollIntervalMs);
    }
    return !processMatches(pid, socketPath);
}

void terminateIfOwned(qint64 pid, const QString &socketPath, int timeoutMs)
{
    bool owned = false;
    for (int attempt = 0; attempt < 10 && !owned; ++attempt) {
        owned = processMatches(pid, socketPath);
        if (!owned) {
            sleepFor(kPollIntervalMs);
        }
    }
    if (!owned) {
        return;
    }

    ::kill(static_cast<pid_t>(pid), SIGTERM);
    if (!waitForExit(pid, socketPath, timeoutMs)) {
        ::kill(static_cast<pid_t>(pid), SIGKILL);
        waitForExit(pid, socketPath, kKillTimeoutMs);
    }
}

} // namespace

QString SwtpmProcess::resolveBinary(const QString &requested, Error *error)
{
    if (error) {
        *error = {};
    }

    QString binary = requested.trimmed();
    if (binary.isEmpty()) {
        binary = qEnvironmentVariable("PODSTACK_SWTPM_BINARY").trimmed();
    }
    if (binary.isEmpty()) {
        binary = QStandardPaths::findExecutable(QStringLiteral("swtpm"));
    }

    if (binary.isEmpty()) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_DEPENDENCY_MISSING"),
                QStringLiteral("O binário swtpm não foi encontrado."), QStringLiteral("swtpm"),
                QStringLiteral("Instale o swtpm (swtpm no Fedora/Ubuntu) ou defina "
                               "PODSTACK_SWTPM_BINARY."));
        }
    }
    return binary;
}

QString SwtpmProcess::socketPathFor(const QString &machineDir)
{
    return machineDir + QStringLiteral("/swtpm.sock");
}

QString SwtpmProcess::stateDirFor(const QString &machineDir)
{
    return machineDir + QStringLiteral("/tpm");
}

bool SwtpmProcess::start(const QString &binary, const QString &stateDir, const QString &socketPath,
                         Error *error)
{
    if (error) {
        *error = {};
    }

    if (isRunning()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("O swtpm desta machine já está em execução."),
                                 socketPath);
        }
        return false;
    }
    m_pid = 0;
    m_socketPath.clear();

    if (!QDir::isAbsolutePath(stateDir)) {
        if (error) {
            *error = invalidParams(
                QStringLiteral("O diretório de estado do TPM deve ser absoluto."), stateDir);
        }
        return false;
    }
    if (!QDir::isAbsolutePath(socketPath)) {
        if (error) {
            *error =
                invalidParams(QStringLiteral("O socket do TPM deve ser absoluto."), socketPath);
        }
        return false;
    }
    if (QFile::encodeName(socketPath).size() >= 108) {
        if (error) {
            *error = invalidParams(
                QStringLiteral("O caminho do socket do TPM é longo demais."), socketPath,
                QStringLiteral("O limite de socket Unix é 108 bytes; use um diretório de "
                               "machines mais curto."));
        }
        return false;
    }

    if (!QDir().mkpath(stateDir)) {
        if (error) {
            *error =
                Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                            QStringLiteral("Não foi possível criar o diretório do TPM."), stateDir);
        }
        return false;
    }
    if (!QFile::setPermissions(stateDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                             QFileDevice::ExeOwner)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível restringir o diretório do TPM "
                                                "a 0700."),
                                 stateDir);
        }
        return false;
    }

    const QFileInfo socketInfo(socketPath);
    if (!socketInfo.absoluteDir().exists()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O diretório do socket do TPM não existe."),
                                 socketInfo.absolutePath());
        }
        return false;
    }
    if (socketInfo.exists()) {
        if (socketInfo.isDir()) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                     QStringLiteral("O caminho do socket do TPM é um diretório."),
                                     socketPath);
            }
            return false;
        }
        QFile::remove(socketPath);
    }

    const QString executable = resolveBinary(binary, error);
    if (executable.isEmpty()) {
        return false;
    }
    if (executable.contains(QLatin1Char('/'))) {
        const QFileInfo executableInfo(executable);
        if (!executableInfo.isFile() || !executableInfo.isExecutable()) {
            if (error) {
                *error =
                    Error::make(QStringLiteral("PODSTACK_DEPENDENCY_MISSING"),
                                QStringLiteral("O binário swtpm não é executável."), executable,
                                QStringLiteral("Instale o swtpm ou ajuste PODSTACK_SWTPM_BINARY."));
            }
            return false;
        }
    }

    QStringList arguments;
    arguments << QStringLiteral("socket") << QStringLiteral("--tpm2")
              << QStringLiteral("--tpmstate") << QStringLiteral("dir=%1,mode=0600").arg(stateDir)
              << QStringLiteral("--ctrl")
              << QStringLiteral("type=unixio,path=%1,mode=0600").arg(socketPath);

    qint64 pid = 0;
    if (!QProcess::startDetached(executable, arguments, QString(), &pid) || pid <= 0) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_DEPENDENCY_MISSING"),
                QStringLiteral("Não foi possível iniciar o swtpm."), executable,
                QStringLiteral("Verifique a instalação do swtpm e o diretório de estado do TPM."));
        }
        QFile::remove(socketPath);
        return false;
    }

    const bool ready = waitForSocket(pid, socketPath, kSocketTimeoutMs);
    const bool alive = processExists(pid);
    if (!ready) {
        if (error) {
            *error = Error::make(
                alive ? QStringLiteral("PODSTACK_TIMEOUT") : QStringLiteral("PODSTACK_IO_ERROR"),
                alive ? QStringLiteral("O swtpm não criou o socket a tempo.")
                      : QStringLiteral("O swtpm encerrou antes de criar o socket."),
                QStringLiteral("pid %1, socket %2").arg(pid).arg(socketPath),
                QStringLiteral("Verifique a instalação do swtpm e o diretório de estado do TPM."));
        }
        terminateIfOwned(pid, socketPath, kKillTimeoutMs);
        QFile::remove(socketPath);
        return false;
    }

    QFile::setPermissions(socketPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    m_pid = pid;
    m_socketPath = socketPath;
    qCDebug(lcCore)
        << QStringLiteral("swtpm iniciado (pid %1, socket %2)").arg(pid).arg(socketPath);
    return true;
}

bool SwtpmProcess::adopt(const QString &socketPath)
{
    m_pid = 0;
    m_socketPath.clear();
    if (socketPath.isEmpty() || !QFileInfo::exists(socketPath)) {
        return false;
    }

    // Varre /proc em busca do swtpm desta machine; o cmdline contém o socket.
    QDir proc(QStringLiteral("/proc"));
    const QStringList entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool numeric = false;
        const qint64 pid = entry.toLongLong(&numeric);
        if (!numeric || pid <= 0) {
            continue;
        }
        if (processMatches(pid, socketPath)) {
            m_pid = pid;
            m_socketPath = socketPath;
            return true;
        }
    }
    return false;
}

bool SwtpmProcess::stop(Error *error, int timeoutMs)
{
    if (error) {
        *error = {};
    }
    if (m_pid <= 0) {
        return true;
    }

    if (!processMatches(m_pid, m_socketPath)) {
        m_pid = 0;
        QFile::remove(m_socketPath);
        m_socketPath.clear();
        return true;
    }

    ::kill(static_cast<pid_t>(m_pid), SIGTERM);
    if (!waitForExit(m_pid, m_socketPath, timeoutMs)) {
        ::kill(static_cast<pid_t>(m_pid), SIGKILL);
        if (!waitForExit(m_pid, m_socketPath, kKillTimeoutMs)) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                     QStringLiteral("Não foi possível encerrar o processo do "
                                                    "swtpm."),
                                     QStringLiteral("pid %1").arg(m_pid));
            }
            return false;
        }
    }

    m_pid = 0;
    QFile::remove(m_socketPath);
    m_socketPath.clear();
    return true;
}

bool SwtpmProcess::isRunning() const
{
    return processMatches(m_pid, m_socketPath);
}

qint64 SwtpmProcess::pid() const
{
    return isRunning() ? m_pid : 0;
}

} // namespace PodStack
