// SPDX-License-Identifier: GPL-3.0-or-later

#include "k8s/K8sManager.h"

#include "core/Config.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QThread>
#include <QVector>

namespace PodStack {
namespace {

constexpr qint64 kGiB = 1024LL * 1024LL * 1024LL;

QString currentIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

void setError(Error *error, const QString &code, const QString &message, const QString &detail = {},
              const QString &remediation = {})
{
    if (error) {
        *error = Error::make(code, message, detail, remediation);
    }
}

QString resolveBinary(const QString &override)
{
    if (!override.trimmed().isEmpty()) {
        const QString resolved = QStandardPaths::findExecutable(override);
        if (!resolved.isEmpty()) {
            return resolved;
        }
        const QFileInfo info(override);
        if (info.isFile() && info.isExecutable()) {
            return info.absoluteFilePath();
        }
        return {};
    }
    return QStandardPaths::findExecutable(QStringLiteral("ssh"));
}

QString defaultInstallScript()
{
    return QStringLiteral(
        "set -e\n"
        "if command -v k3s >/dev/null 2>&1 && sudo systemctl is-active --quiet k3s; then\n"
        "  exit 0\n"
        "fi\n"
        "curl -sfL https://get.k3s.io | "
        "INSTALL_K3S_EXEC=\"server --disable=traefik --write-kubeconfig-mode=600\" sh -\n");
}

QString readyScript()
{
    return QStringLiteral(
        "for i in $(seq 1 60); do\n"
        "  if sudo k3s kubectl get nodes --no-headers 2>/dev/null | grep -q ' Ready'; then\n"
        "    exit 0\n"
        "  fi\n"
        "  sleep 2\n"
        "done\n"
        "echo 'k3s nao ficou Ready a tempo' >&2\n"
        "exit 1\n");
}

enum class RunResult {
    Completed,
    Cancelled,
    TimedOut,
    StartFailed,
};

RunResult runSsh(const QString &binary, const SshInfo &ssh, const QString &script,
                 const K8sManager::Line &onLine, const K8sManager::Cancelled &cancelled,
                 int timeoutMs, Error *error)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    const QStringList arguments{
        QStringLiteral("-i"),
        ssh.identityFile,
        QStringLiteral("-p"),
        QString::number(ssh.port),
        QStringLiteral("-o"),
        QStringLiteral("IdentitiesOnly=yes"),
        QStringLiteral("-o"),
        QStringLiteral("StrictHostKeyChecking=accept-new"),
        QStringLiteral("-o"),
        QStringLiteral("BatchMode=yes"),
        QStringLiteral("-o"),
        QStringLiteral("ConnectTimeout=5"),
        QStringLiteral("%1@%2").arg(ssh.user, ssh.host),
        script,
    };
    process.start(binary, arguments);
    if (!process.waitForStarted(30000)) {
        setError(error, QStringLiteral("PODSTACK_IO_ERROR"),
                 QStringLiteral("Não foi possível executar o SSH."), process.errorString(),
                 QStringLiteral("Verifique o OpenSSH e as permissões da chave."));
        return RunResult::StartFailed;
    }

    QElapsedTimer timer;
    timer.start();
    QByteArray buffer;
    QVector<QString> recentLines;

    const auto dispatch = [&](bool final) {
        buffer += process.readAllStandardOutput();
        int newline = -1;
        while ((newline = buffer.indexOf('\n')) >= 0) {
            QByteArray raw = buffer.left(newline);
            buffer.remove(0, newline + 1);
            if (raw.endsWith('\r')) {
                raw.chop(1);
            }
            if (raw.isEmpty()) {
                continue;
            }
            const QString line = QString::fromUtf8(raw);
            recentLines.append(line);
            if (recentLines.size() > 20) {
                recentLines.removeFirst();
            }
            if (onLine) {
                onLine(line);
            }
        }
        if (final && !buffer.isEmpty()) {
            QByteArray raw = buffer;
            buffer.clear();
            if (raw.endsWith('\r')) {
                raw.chop(1);
            }
            if (!raw.isEmpty()) {
                const QString line = QString::fromUtf8(raw);
                recentLines.append(line);
                if (recentLines.size() > 20) {
                    recentLines.removeFirst();
                }
                if (onLine) {
                    onLine(line);
                }
            }
        }
    };

    while (process.state() != QProcess::NotRunning) {
        if (cancelled && cancelled()) {
            process.kill();
            process.waitForFinished(5000);
            setError(error, QStringLiteral("PODSTACK_CANCELLED"),
                     QStringLiteral("Instalação do cluster cancelada."));
            return RunResult::Cancelled;
        }
        if (timer.elapsed() >= timeoutMs) {
            process.kill();
            process.waitForFinished(5000);
            setError(error, QStringLiteral("PODSTACK_TIMEOUT"),
                     QStringLiteral("Tempo esgotado ao executar o comando no cluster."),
                     recentLines.join(QLatin1Char('\n')));
            return RunResult::TimedOut;
        }
        if (process.waitForReadyRead(200)) {
            dispatch(false);
        }
    }
    dispatch(true);

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        setError(error, QStringLiteral("PODSTACK_IO_ERROR"),
                 QStringLiteral("O comando remoto falhou no cluster."),
                 recentLines.join(QLatin1Char('\n')));
        return RunResult::Completed;
    }
    return RunResult::Completed;
}

} // namespace

K8sManager::K8sManager(MachineManager *machines) : K8sManager(machines, Options{}) {}

K8sManager::K8sManager(MachineManager *machines, Options options)
    : m_machines(machines), m_options(std::move(options)), m_store(m_options.storePath)
{
}

QString K8sManager::nowIso()
{
    return currentIso();
}

void K8sManager::persistState(K8sCluster &cluster, const QString &state, const QString &errorText,
                              bool touchUpdatedAt)
{
    cluster.state = state;
    cluster.error = errorText;
    if (cluster.createdAt.isEmpty()) {
        cluster.createdAt = nowIso();
    }
    if (touchUpdatedAt) {
        cluster.updatedAt = nowIso();
    }
    Error ignored;
    m_store.upsert(cluster, &ignored);
}

QString K8sManager::machineNameForNewCluster()
{
    for (int index = 0; index < 100; ++index) {
        const QString candidate =
            index == 0 ? m_options.machineName
                       : QStringLiteral("%1-%2").arg(m_options.machineName).arg(index + 1);
        Error lookupError;
        const MachineMeta existing = m_machines->get(candidate, &lookupError);
        if (lookupError.code == QStringLiteral("PODSTACK_NOT_FOUND")) {
            return candidate;
        }
        if (!existing.id.isEmpty()) {
            continue;
        }
    }
    return {};
}

K8sCluster K8sManager::status(Error *error)
{
    if (error) {
        *error = {};
    }
    if (!m_machines) {
        setError(error, QStringLiteral("PODSTACK_INTERNAL"),
                 QStringLiteral("Gerenciador de machines indisponível."));
        return {};
    }

    Error storeError;
    K8sCluster cluster = m_store.get(m_options.clusterName, &storeError);
    if (storeError.code == QStringLiteral("PODSTACK_NOT_FOUND")) {
        K8sCluster disabled;
        disabled.name = m_options.clusterName;
        disabled.state = QStringLiteral("disabled");
        return disabled;
    }
    if (!storeError.isEmpty()) {
        if (error) {
            *error = storeError;
        }
        return {};
    }

    if ((cluster.state == QStringLiteral("provisioning") ||
         cluster.state == QStringLiteral("installing")) &&
        !m_enabling.load()) {
        cluster.state = QStringLiteral("error");
        cluster.error = QStringLiteral("Provisionamento interrompido; tente habilitar novamente.");
        return cluster;
    }

    if (cluster.vmMachineId.isEmpty()) {
        return cluster;
    }

    Error machineError;
    const MachineMeta machine = m_machines->get(cluster.vmMachineId, &machineError);
    if (!machineError.isEmpty()) {
        if (machineError.code == QStringLiteral("PODSTACK_NOT_FOUND")) {
            cluster.state = QStringLiteral("error");
            cluster.error = QStringLiteral("A VM do cluster não existe mais.");
        } else if (error) {
            *error = machineError;
        }
        return cluster;
    }

    if (cluster.state == QStringLiteral("ready") && machine.state != QStringLiteral("running")) {
        cluster.state = machine.state.isEmpty() ? QStringLiteral("stopped") : machine.state;
    }
    return cluster;
}

bool K8sManager::waitForSsh(const SshInfo &ssh, const Cancelled &cancelled, Error *error)
{
    QElapsedTimer timer;
    timer.start();
    QString lastDetail;
    while (timer.elapsed() < m_options.sshReadyTimeoutMs) {
        if (cancelled && cancelled()) {
            setError(error, QStringLiteral("PODSTACK_CANCELLED"),
                     QStringLiteral("Provisionamento do cluster cancelado."));
            return false;
        }

        Error attemptError;
        const QString script = QStringLiteral("cloud-init status --wait >/dev/null 2>&1 || true\n"
                                              "sudo -n true\n");
        const bool ready =
            m_options.remoteExec
                ? m_options.remoteExec(ssh, script, {}, cancelled, 20000, &attemptError)
                : runSsh(resolveBinary(m_options.sshBinary), ssh, script, {}, cancelled, 20000,
                         &attemptError) == RunResult::Completed &&
                      attemptError.isEmpty();
        if (ready && attemptError.isEmpty()) {
            return true;
        }
        if (attemptError.code == QStringLiteral("PODSTACK_CANCELLED")) {
            if (error) {
                *error = attemptError;
            }
            return false;
        }
        if (!attemptError.detail.isEmpty()) {
            lastDetail = attemptError.detail;
        } else if (!attemptError.message.isEmpty()) {
            lastDetail = attemptError.message;
        }
        QThread::msleep(1000);
    }

    setError(error, QStringLiteral("PODSTACK_TIMEOUT"),
             QStringLiteral("A VM do cluster não ficou acessível por SSH a tempo."), lastDetail,
             QStringLiteral("Verifique o console da machine e a rede do host."));
    return false;
}

bool K8sManager::waitForNodeReady(const SshInfo &ssh, const Cancelled &cancelled, Error *error)
{
    const QString script = readyScript();
    Error execError;
    const bool ok =
        m_options.remoteExec
            ? m_options.remoteExec(ssh, script, {}, cancelled, m_options.readyTimeoutMs, &execError)
            : runSsh(resolveBinary(m_options.sshBinary), ssh, script, {}, cancelled,
                     m_options.readyTimeoutMs, &execError) == RunResult::Completed &&
                  execError.isEmpty();
    if (!ok && error) {
        if (execError.isEmpty()) {
            *error = Error::make(QStringLiteral("PODSTACK_TIMEOUT"),
                                 QStringLiteral("O node do cluster não ficou Ready a tempo."));
        } else {
            *error = execError;
        }
    }
    return ok && execError.isEmpty();
}

K8sCluster K8sManager::enable(const EnableRequest &request, const Progress &progress,
                              const Cancelled &cancelled, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!m_machines) {
        setError(error, QStringLiteral("PODSTACK_INTERNAL"),
                 QStringLiteral("Gerenciador de machines indisponível."));
        return {};
    }
    if (m_enabling.exchange(true)) {
        setError(error, QStringLiteral("PODSTACK_CONFLICT"),
                 QStringLiteral("Já existe um provisionamento de cluster em andamento."));
        return {};
    }
    const auto resetFlag = qScopeGuard([this] { m_enabling.store(false); });

    const auto report = [&progress](const QString &stage, int value) {
        if (progress) {
            progress(stage, value);
        }
    };

    const Config config = Config::load();
    const int cpus = request.cpus.value_or(config.get(QStringLiteral("kubernetes.cpus")).toInt(2));
    const qint64 memoryBytes = request.memoryBytes.value_or(
        config.get(QStringLiteral("kubernetes.memoryBytes")).toInteger(4 * kGiB));
    const qint64 diskBytes = request.diskBytes.value_or(
        config.get(QStringLiteral("kubernetes.diskBytes")).toInteger(20 * kGiB));
    if (cpus < 1 || cpus > 256 || memoryBytes < kGiB || diskBytes < kGiB) {
        setError(
            error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
            QStringLiteral("Recursos inválidos para a VM do cluster."),
            QStringLiteral("cpus=%1 memory=%2 disk=%3").arg(cpus).arg(memoryBytes).arg(diskBytes));
        return {};
    }

    Error storeError;
    K8sCluster cluster = m_store.get(m_options.clusterName, &storeError);
    if (!storeError.isEmpty() && storeError.code != QStringLiteral("PODSTACK_NOT_FOUND")) {
        if (error) {
            *error = storeError;
        }
        return {};
    }
    if (cluster.id.isEmpty()) {
        cluster.id = QStringLiteral("k8s-%1").arg(m_options.clusterName);
        cluster.name = m_options.clusterName;
        cluster.provider = QStringLiteral("k3s-vm");
        cluster.state = QStringLiteral("provisioning");
        cluster.createdAt = nowIso();
        cluster.updatedAt = cluster.createdAt;
    }

    MachineMeta machine;
    bool startedByUs = false;
    if (!cluster.vmMachineId.isEmpty()) {
        Error machineError;
        machine = m_machines->get(cluster.vmMachineId, &machineError);
        if (!machineError.isEmpty() && machineError.code != QStringLiteral("PODSTACK_NOT_FOUND")) {
            if (error) {
                *error = machineError;
            }
            return {};
        }
        if (machine.id.isEmpty()) {
            cluster.vmMachineId.clear();
        }
    }

    if (!machine.id.isEmpty() && machine.state == QStringLiteral("running") &&
        cluster.state == QStringLiteral("ready")) {
        report(QStringLiteral("ready"), 100);
        return cluster;
    }

    const auto fail = [&](const Error &failure) -> K8sCluster {
        const bool cancelledFailure = failure.code == QStringLiteral("PODSTACK_CANCELLED");
        persistState(
            cluster, cancelledFailure ? QStringLiteral("disabled") : QStringLiteral("error"),
            failure.message +
                (failure.detail.isEmpty() ? QString() : QStringLiteral(": ") + failure.detail));
        if (!machine.id.isEmpty() && (startedByUs || machine.state == QStringLiteral("running"))) {
            m_machines->stop(machine.id, true, nullptr);
        }
        if (error) {
            *error = failure;
        }
        return {};
    };

    if (machine.id.isEmpty()) {
        report(QStringLiteral("machine"), 5);
        CreateMachineRequest createRequest;
        createRequest.name = machineNameForNewCluster();
        if (createRequest.name.isEmpty()) {
            return fail(Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                    QStringLiteral("Não há nome livre para a VM do cluster.")));
        }
        createRequest.distro = m_options.distro;
        createRequest.version = m_options.version;
        createRequest.user = QStringLiteral("podstack");
        createRequest.cpus = cpus;
        createRequest.memoryBytes = memoryBytes;
        createRequest.diskBytes = diskBytes;

        Error createError;
        machine = m_machines->create(
            createRequest,
            [&report](const QString &stage, int value) { report(stage, 5 + (value * 50) / 100); },
            cancelled, &createError);
        if (machine.id.isEmpty()) {
            if (createError.isEmpty()) {
                createError = Error::make(QStringLiteral("PODSTACK_INTERNAL"),
                                          QStringLiteral("Falha ao criar a VM do cluster."));
            }
            cluster.vmMachineId = machine.id;
            return fail(createError);
        }
        cluster.vmMachineId = machine.id;
        startedByUs = true;
        if (!createError.isEmpty()) {
            return fail(createError);
        }
        persistState(cluster, QStringLiteral("provisioning"), {});
    } else if (machine.state != QStringLiteral("running")) {
        report(QStringLiteral("machine"), 50);
        Error startError;
        machine = m_machines->start(machine.id, &startError);
        if (!startError.isEmpty() || machine.state != QStringLiteral("running")) {
            if (startError.isEmpty()) {
                startError = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                         QStringLiteral("A VM do cluster não iniciou."));
            }
            return fail(startError);
        }
        startedByUs = true;
    }

    SshInfo ssh;
    Error sshError;
    if (!m_machines->sshInfo(machine.id, &ssh, &sshError)) {
        return fail(sshError);
    }

    report(QStringLiteral("ssh"), 55);
    Error waitError;
    if (!waitForSsh(ssh, cancelled, &waitError)) {
        return fail(waitError);
    }

    persistState(cluster, QStringLiteral("installing"), {});
    report(QStringLiteral("install"), 65);
    const QString script =
        m_options.installScript.isEmpty() ? defaultInstallScript() : m_options.installScript;
    Error installError;
    const bool installed =
        m_options.remoteExec
            ? m_options.remoteExec(ssh, script, {}, cancelled, m_options.installTimeoutMs,
                                   &installError)
            : runSsh(resolveBinary(m_options.sshBinary), ssh, script, {}, cancelled,
                     m_options.installTimeoutMs, &installError) == RunResult::Completed &&
                  installError.isEmpty();
    if (!installed || !installError.isEmpty()) {
        if (installError.isEmpty()) {
            installError = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                       QStringLiteral("Falha ao instalar o k3s na VM."));
        }
        return fail(installError);
    }

    report(QStringLiteral("ready"), 85);
    Error readyError;
    if (!waitForNodeReady(ssh, cancelled, &readyError)) {
        return fail(readyError);
    }

    persistState(cluster, QStringLiteral("ready"), {});
    Config enabledConfig = Config::load();
    enabledConfig.set(QStringLiteral("kubernetes.enabled"), true);
    QString configError;
    if (!enabledConfig.save(&configError)) {
        qWarning("podstack.k8s: falha ao persistir kubernetes.enabled: %s",
                 qUtf8Printable(configError));
    }
    report(QStringLiteral("ready"), 100);
    return cluster;
}

} // namespace PodStack
