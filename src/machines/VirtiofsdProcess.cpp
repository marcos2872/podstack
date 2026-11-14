// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/VirtiofsdProcess.h"

#include "core/Logging.h"
#include "machines/ProcessScope.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <chrono>
#include <csignal>
#include <thread>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace PodStack {
namespace {

constexpr int kSocketTimeoutMs = 5000;
constexpr int kKillTimeoutMs = 2000;
constexpr int kPollIntervalMs = 20;
constexpr int kExitPollIntervalMs = 50;
constexpr int kCmdlineMaxBytes = 64 * 1024;

bool validTag(const QString &tag)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9-]{1,32}$"));
    return pattern.match(tag).hasMatch();
}

Error invalidParams(const QString &message, const QString &detail = {},
                    const QString &remediation = {})
{
    return Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"), message, detail, remediation);
}

void sleepFor(int milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

/// Confirma que o PID pertence a este mount: mesmo uid e `--socket-path`
/// exatamente igual ao socket informado. Isso evita sinalizar um PID
/// reciclado ou um virtiofsd de outra instalação.
bool processMatches(qint64 pid, const QString &socketPath)
{
    if (pid <= 0 || socketPath.isEmpty()) {
        return false;
    }

    const QString processPath = QStringLiteral("/proc/%1").arg(pid);
    struct stat status {};
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
        if (argument == expected) {
            return true;
        }
    }
    return false;
}

/// O processo ainda existe (mesmo uid), independentemente do cmdline. É usado
/// durante o `exec` do script de teste, quando o cmdline fica momentaneamente
/// vazio e não deve ser confundido com "morreu".
bool processExists(qint64 pid)
{
    if (pid <= 0) {
        return false;
    }
    const QString processPath = QStringLiteral("/proc/%1").arg(pid);
    struct stat status {};
    const QByteArray nativePath = QFile::encodeName(processPath);
    if (::stat(nativePath.constData(), &status) != 0 || status.st_uid != ::getuid()) {
        return false;
    }

    // Em containers o PID 1 pode não recolher o filho destacado de imediato;
    // um zumbi ainda tem /proc/<pid>, mas não é um processo vivo.
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
    // Pode ser o intervalo de exec do processo; confirma antes desistir.
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

QString VirtiofsdProcess::resolveBinary(const QString &requested, Error *error)
{
    if (error) {
        *error = {};
    }

    QString binary = requested.trimmed();
    if (binary.isEmpty()) {
        binary = qEnvironmentVariable("PODSTACK_VIRTIOFSD_BINARY").trimmed();
    }
    if (binary.isEmpty()) {
        binary = QStandardPaths::findExecutable(QStringLiteral("virtiofsd"));
    }
    if (binary.isEmpty()) {
        const QString fallback = QStringLiteral("/usr/libexec/virtiofsd");
        if (QFileInfo(fallback).isExecutable()) {
            binary = fallback;
        }
    }

    if (binary.isEmpty()) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_UNSUPPORTED"),
                QStringLiteral("O binário virtiofsd não foi encontrado."),
                QStringLiteral("virtiofsd"),
                QStringLiteral("Instale o virtiofsd (virtiofsd no Fedora ou virtiofsd/"
                               "qemu-virtiofsd no Debian/Ubuntu) ou defina "
                               "PODSTACK_VIRTIOFSD_BINARY."));
        }
    }
    return binary;
}

bool VirtiofsdProcess::start(const QString &binary, const QString &source,
                             const QString &socketPath, const QString &tag, bool readOnly,
                             Error *error)
{
    if (error) {
        *error = {};
    }

    if (isRunning()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("O virtiofsd desta tag já está em execução."),
                                 tag);
        }
        return false;
    }
    m_pid = 0;
    m_socketPath.clear();

    if (!validTag(tag)) {
        if (error) {
            *error = invalidParams(
                QStringLiteral("Tag de mount inválida."), tag,
                QStringLiteral("Use 1..32 caracteres [a-z0-9-]."));
        }
        return false;
    }
    if (!QDir::isAbsolutePath(socketPath)) {
        if (error) {
            *error = invalidParams(QStringLiteral("O socket do virtiofsd deve ser absoluto."),
                                   socketPath);
        }
        return false;
    }
    if (QFile::encodeName(socketPath).size() >= 108) {
        if (error) {
            *error = invalidParams(
                QStringLiteral("O caminho do socket do virtiofsd é longo demais."), socketPath,
                QStringLiteral("O limite de socket Unix é 108 bytes; use um diretório de "
                               "machines mais curto."));
        }
        return false;
    }

    const QFileInfo sourceInfo(source);
    if (!QDir::isAbsolutePath(source) || !sourceInfo.isDir()) {
        if (error) {
            *error = invalidParams(
                QStringLiteral("O diretório compartilhado do mount não existe."), source,
                QStringLiteral("O diretório precisa existir e ser acessível no host."));
        }
        return false;
    }
    const QString canonicalSource = sourceInfo.canonicalFilePath();
    if (canonicalSource.isEmpty()) {
        if (error) {
            *error = invalidParams(
                QStringLiteral("Não foi possível resolver o diretório compartilhado do mount."),
                source);
        }
        return false;
    }

    const QFileInfo socketInfo(socketPath);
    if (!socketInfo.absoluteDir().exists()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O diretório do socket do virtiofsd não existe."),
                                 socketInfo.absolutePath());
        }
        return false;
    }

    const QString executable = resolveBinary(binary, error);
    if (executable.isEmpty()) {
        return false;
    }
    if (executable.contains(QLatin1Char('/'))) {
        const QFileInfo executableInfo(executable);
        if (!executableInfo.isFile() || !executableInfo.isExecutable()) {
            if (error) {
                *error = Error::make(
                    QStringLiteral("PODSTACK_UNSUPPORTED"),
                    QStringLiteral("O binário virtiofsd não é executável."), executable,
                    QStringLiteral("Instale o virtiofsd ou ajuste PODSTACK_VIRTIOFSD_BINARY."));
            }
            return false;
        }
    }

    // Um socket remanescente de uma execução anterior impede o bind.
    if (socketInfo.exists()) {
        if (socketInfo.isDir()) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                     QStringLiteral("O caminho do socket é um diretório."),
                                     socketPath);
            }
            return false;
        }
        QFile::remove(socketPath);
    }

    // virtiofsd 1.14 só aceita chroot como root; no modo rootless (padrão do
    // PodStack) o namespace de usuário é o sandbox suportado.
    const QString sandbox = ::geteuid() == 0 ? QStringLiteral("chroot")
                                             : QStringLiteral("namespace");
    QStringList arguments;
    arguments << QStringLiteral("--shared-dir") << canonicalSource
              << QStringLiteral("--socket-path") << socketPath
              << QStringLiteral("--tag") << tag
              << QStringLiteral("--cache=auto")
              << QStringLiteral("--sandbox=%1").arg(sandbox)
              << QStringLiteral("--xattr");
    if (readOnly) {
        arguments << QStringLiteral("--readonly");
    }

    const QByteArray socketHash =
        QCryptographicHash::hash(socketPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(12);
    const QString scopeUnit = ProcessScope::unitName(QStringLiteral("podstack-virtiofsd"),
                                                     QString::fromLatin1(socketHash));

    qint64 pid = 0;
    QString launchFailure;
    if (!ProcessScope::startDetached(scopeUnit, executable, arguments, &pid, &launchFailure) ||
        pid <= 0) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_UNSUPPORTED"),
                QStringLiteral("Não foi possível iniciar o virtiofsd."), executable,
                launchFailure.isEmpty()
                    ? QStringLiteral("Verifique a instalação do virtiofsd e o diretório "
                                     "compartilhado.")
                    : launchFailure);
        }
        QFile::remove(socketPath);
        return false;
    }

    const bool ready = waitForSocket(pid, socketPath, kSocketTimeoutMs);
    const bool alive = processExists(pid);
    if (!ready) {
        if (error) {
            *error = Error::make(
                alive ? QStringLiteral("PODSTACK_TIMEOUT")
                      : QStringLiteral("PODSTACK_IO_ERROR"),
                alive ? QStringLiteral("O virtiofsd não criou o socket a tempo.")
                      : QStringLiteral("O virtiofsd encerrou antes de criar o socket."),
                QStringLiteral("pid %1, socket %2").arg(pid).arg(socketPath),
                QStringLiteral("Verifique o suporte a virtiofs no host, o diretório "
                               "compartilhado e o log do daemon."));
        }
        terminateIfOwned(pid, socketPath, kKillTimeoutMs);
        QFile::remove(socketPath);
        return false;
    }

    QFile::setPermissions(socketPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    m_pid = pid;
    m_socketPath = socketPath;
    qCDebug(lcCore) << QStringLiteral("virtiofsd iniciado (tag %1, pid %2, sandbox %3)")
                           .arg(tag)
                           .arg(pid)
                           .arg(sandbox);
    return true;
}

bool VirtiofsdProcess::stop(Error *error, int timeoutMs)
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
                *error = Error::make(
                    QStringLiteral("PODSTACK_IO_ERROR"),
                    QStringLiteral("Não foi possível encerrar o processo do virtiofsd."),
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

bool VirtiofsdProcess::isRunning() const
{
    return processMatches(m_pid, m_socketPath);
}

qint64 VirtiofsdProcess::pid() const
{
    return isRunning() ? m_pid : 0;
}

} // namespace PodStack
