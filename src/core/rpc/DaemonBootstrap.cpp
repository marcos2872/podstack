// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/rpc/DaemonBootstrap.h"

#include "core/config/Paths.h"
#include "core/logging/Logging.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocalSocket>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QRunnable>
#include <QStandardPaths>
#include <QThread>
#include <QThreadPool>

#include <utility>

namespace PodStack::DaemonBootstrap {
namespace {

constexpr int kCommandTimeoutMs = 5000;
constexpr int kAttempts = 2;
constexpr int kRetryDelayMs = 300;
constexpr int kThrottleMs = 5000;

QMutex g_throttleMutex;
QDateTime g_lastSyncAttempt;
bool g_asyncRunning = false;

/// Com PODSTACK_CONTROL_SOCKET o socket é de dev/teste: nunca mexer no systemd.
bool bootstrapAllowed()
{
    return qEnvironmentVariableIsEmpty("PODSTACK_CONTROL_SOCKET");
}

QString systemctlProgram()
{
    const QString override = qEnvironmentVariable("PODSTACK_SYSTEMCTL");
    return override.isEmpty() ? QStringLiteral("systemctl") : override;
}

QString resolvedSocketPath(const QString &socketPath)
{
    return socketPath.isEmpty() ? Paths::controlSocket() : socketPath;
}

struct CommandResult {
    bool ok = false;
    int exitCode = -1;
    QString error;
};

CommandResult runSystemctl(const QStringList &arguments, int timeoutMs = kCommandTimeoutMs)
{
    CommandResult result;
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(systemctlProgram(), arguments);
    if (!process.waitForStarted(kCommandTimeoutMs)) {
        result.error = process.errorString();
        return result;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        result.error = QStringLiteral("tempo esgotado");
        return result;
    }
    result.exitCode = process.exitCode();
    result.ok = process.exitStatus() == QProcess::NormalExit && result.exitCode == 0;
    if (!result.ok) {
        const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (stderrText.isEmpty()) {
            result.error = QStringLiteral("exit code %1").arg(result.exitCode);
        } else {
            result.error = stderrText;
        }
    }
    return result;
}

bool connectable(const QString &socketPath, int timeoutMs)
{
    QLocalSocket socket;
    socket.connectToServer(socketPath, QIODevice::ReadWrite);
    const bool connected = socket.waitForConnected(timeoutMs);
    socket.abort();
    return connected;
}

/// Fala o mínimo de JSON-RPC para saber se o daemon está vivo. Um socket que
/// aceita conexão mas não responde (processo travado no shutdown, por
/// exemplo) é indistinguível de um daemon saudável só pelo `connect()`.
bool responsive(const QString &socketPath, int timeoutMs)
{
    QLocalSocket socket;
    socket.connectToServer(socketPath, QIODevice::ReadWrite);
    if (!socket.waitForConnected(timeoutMs)) {
        socket.abort();
        return false;
    }

    const QByteArray payload = QByteArrayLiteral(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"system.hello\",\"params\":{}}\n");
    if (socket.write(payload) != payload.size() || !socket.waitForBytesWritten(timeoutMs)) {
        socket.abort();
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    QByteArray buffer;
    while (true) {
        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0) {
            break;
        }
        if (!socket.waitForReadyRead(remaining)) {
            break;
        }
        buffer.append(socket.readAll());
        if (buffer.contains('\n')) {
            socket.abort();
            return true;
        }
    }
    socket.abort();
    return false;
}

/// O systemd cria o listening socket de forma assíncrona: depois do `start`
/// é preciso aguardar o daemon responder, não tentar uma única vez.
bool waitForResponsive(const QString &socketPath, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (true) {
        if (responsive(socketPath, 200)) {
            return true;
        }
        if (timer.elapsed() >= timeoutMs) {
            return false;
        }
        QThread::msleep(50);
    }
}

} // namespace

QStringList unitSearchPaths()
{
    QStringList entries;
    const QString override = qEnvironmentVariable("PODSTACK_USER_UNIT_DIR");
    if (!override.isEmpty()) {
        entries.append(override);
    }
    entries.append(QStringLiteral("/usr/lib/systemd/user"));
    entries.append(QStringLiteral("/usr/share/systemd/user"));
    entries.append(QStringLiteral("/usr/local/lib/systemd/user"));
    const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!dataHome.isEmpty()) {
        entries.append(dataHome + QStringLiteral("/systemd/user"));
    }

    QStringList paths;
    paths.reserve(entries.size());
    for (const QString &entry : entries) {
        paths.append(QDir(entry).filePath(QStringLiteral("podstack.socket")));
    }
    return paths;
}

bool unitInstalled()
{
    const QStringList paths = unitSearchPaths();
    for (const QString &path : paths) {
        if (QFileInfo::exists(path)) {
            return true;
        }
    }
    return false;
}

Probe probe(const QString &socketPath, int timeoutMs)
{
    Probe result;
    result.unitInstalled = unitInstalled();
    const QString path = resolvedSocketPath(socketPath);
    result.socketExists = QFileInfo::exists(path);
    result.connectable = connectable(path, timeoutMs);
    result.responds = result.connectable && responsive(path, timeoutMs);
    return result;
}

Status ensureRunning(const QString &socketPath, QString *detail)
{
    if (detail) {
        detail->clear();
    }
    if (!bootstrapAllowed()) {
        return Status::CustomSocket;
    }
    if (!unitInstalled()) {
        return Status::NotInstalled;
    }

    const QString path = resolvedSocketPath(socketPath);
    if (responsive(path, 700)) {
        return Status::AlreadyRunning;
    }

    QString lastFailure;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        if (attempt > 0) {
            QThread::msleep(kRetryDelayMs);
        }
        // daemon-reload é tolerado: numa instalação recém-feita o user
        // manager ainda não conhece a unit e o reload pode falhar.
        const CommandResult reload =
            runSystemctl({QStringLiteral("--user"), QStringLiteral("daemon-reload")});
        if (!reload.ok) {
            qCWarning(lcRpc) << "systemctl --user daemon-reload falhou:" << reload.error;
        }
        const CommandResult start = runSystemctl(
            {QStringLiteral("--user"), QStringLiteral("start"), QStringLiteral("podstack.socket")});
        if (!start.ok) {
            lastFailure = start.error;
            qCWarning(lcRpc) << "systemctl --user start podstack.socket falhou (exit"
                             << start.exitCode << "):" << start.error;
            // O preinst/pre do pacote mascara o socket com `--runtime` durante o
            // upgrade. Se o postinst não conseguiu desmascarar (user manager
            // fora do alcance do root), o app fica sem socket até alguém rodar
            // o unmask. `--runtime` não toca na máscara persistente que o
            // usuário tenha criado, então é seguro tentar de dentro da sessão.
            const CommandResult unmask =
                runSystemctl({QStringLiteral("--user"), QStringLiteral("unmask"),
                              QStringLiteral("--runtime"), QStringLiteral("podstack.socket")});
            if (!unmask.ok) {
                qCWarning(lcRpc) << "systemctl --user unmask --runtime podstack.socket falhou:"
                                 << unmask.error;
            }
            continue;
        }
        if (waitForResponsive(path, 2000)) {
            return Status::Started;
        }
        lastFailure = QStringLiteral("o socket não respondeu depois do start");
        qCWarning(lcRpc) << "podstack.socket iniciado, mas" << path << "não responde";
    }

    if (detail) {
        *detail = std::move(lastFailure);
    }
    return Status::Failed;
}

Status restart(const QString &socketPath, QString *detail)
{
    if (detail) {
        detail->clear();
    }
    if (!bootstrapAllowed()) {
        return Status::CustomSocket;
    }
    if (!unitInstalled()) {
        return Status::NotInstalled;
    }

    const CommandResult reload =
        runSystemctl({QStringLiteral("--user"), QStringLiteral("daemon-reload")});
    if (!reload.ok) {
        qCWarning(lcRpc) << "systemctl --user daemon-reload falhou:" << reload.error;
    }
    // Botão "Reiniciar daemon": a unit pode continuar mascarada com --runtime
    // desde o upgrade. Sem isso o restart falha e o banner não tem remediação.
    const CommandResult unmask =
        runSystemctl({QStringLiteral("--user"), QStringLiteral("unmask"),
                      QStringLiteral("--runtime"), QStringLiteral("podstack.socket")});
    if (!unmask.ok) {
        qCWarning(lcRpc) << "systemctl --user unmask --runtime podstack.socket falhou:"
                         << unmask.error;
    }

    // Timeout maior: o stop de um daemon travado pode esperar o TimeoutStopSec
    // da unit (20 s) antes do SIGKILL.
    const CommandResult stopped = runSystemctl(
        {QStringLiteral("--user"), QStringLiteral("restart"), QStringLiteral("podstack.service")},
        30000);
    if (!stopped.ok) {
        if (detail) {
            *detail = stopped.error;
        }
        qCWarning(lcRpc) << "systemctl --user restart podstack.service falhou:" << stopped.error;
        return Status::Failed;
    }

    const QString path = resolvedSocketPath(socketPath);
    if (waitForResponsive(path, 5000)) {
        return Status::Started;
    }
    if (detail) {
        *detail = QStringLiteral("o daemon não respondeu depois do restart");
    }
    qCWarning(lcRpc) << "podstack.service reiniciado, mas" << path << "não responde";
    return Status::Failed;
}

Status ensureRunningThrottled(const QString &socketPath, QString *detail)
{
    {
        QMutexLocker locker(&g_throttleMutex);
        const QDateTime now = QDateTime::currentDateTimeUtc();
        if (g_lastSyncAttempt.isValid() && g_lastSyncAttempt.msecsTo(now) < kThrottleMs) {
            if (detail) {
                detail->clear();
            }
            return Status::Throttled;
        }
        g_lastSyncAttempt = now;
    }
    return ensureRunning(socketPath, detail);
}

void ensureRunningAsync(const QString &socketPath)
{
    if (!bootstrapAllowed() || !unitInstalled()) {
        return;
    }
    {
        QMutexLocker locker(&g_throttleMutex);
        const QDateTime now = QDateTime::currentDateTimeUtc();
        if (g_asyncRunning) {
            return;
        }
        if (g_lastSyncAttempt.isValid() && g_lastSyncAttempt.msecsTo(now) < kThrottleMs) {
            return;
        }
        g_lastSyncAttempt = now;
        g_asyncRunning = true;
    }

    const QString path = resolvedSocketPath(socketPath);
    QThreadPool::globalInstance()->start(QRunnable::create([path] {
        ensureRunning(path);
        QMutexLocker locker(&g_throttleMutex);
        g_asyncRunning = false;
    }));
}

EngineStatus ensureEngineRunning(const QString &podmanSocketPath, bool enabled, QString *detail)
{
    if (detail) {
        detail->clear();
    }
    if (podmanSocketPath.isEmpty()) {
        if (detail) {
            *detail = QStringLiteral("caminho do socket do Podman vazio");
        }
        return EngineStatus::Failed;
    }
    if (QFileInfo::exists(podmanSocketPath)) {
        return EngineStatus::AlreadyRunning;
    }
    if (!enabled) {
        if (detail) {
            *detail = QStringLiteral("engine.autoStart desativado");
        }
        return EngineStatus::Disabled;
    }

    const CommandResult start = runSystemctl(
        {QStringLiteral("--user"), QStringLiteral("start"), QStringLiteral("podman.socket")});
    if (!start.ok) {
        if (detail) {
            *detail = start.error;
        }
        qCWarning(lcRpc) << "systemctl --user start podman.socket falhou:" << start.error;
        return EngineStatus::Failed;
    }

    // O systemd cria o socket de forma assíncrona; espera curta para o ping do
    // Podman não falhar logo após o start.
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 2000) {
        if (QFileInfo::exists(podmanSocketPath)) {
            return EngineStatus::Started;
        }
        QThread::msleep(50);
    }
    if (detail) {
        *detail = QStringLiteral("podman.socket iniciado, mas o socket não apareceu");
    }
    qCWarning(lcRpc) << "podman.socket iniciado, mas" << podmanSocketPath << "não apareceu";
    return EngineStatus::Failed;
}

QString manualRemedy()
{
    // `restart` também ativa o socket parado; é o único remédio que resolve
    // um daemon que aceita conexões mas não responde.
    return QStringLiteral("systemctl --user restart podstack.service");
}

} // namespace PodStack::DaemonBootstrap
