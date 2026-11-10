// SPDX-License-Identifier: GPL-3.0-or-later

#include "k8s/K8sManager.h"

#include "core/Config.h"
#include "core/Ids.h"
#include "core/Paths.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTcpServer>

#include <QThread>
#include <QVector>
#include <algorithm>
#include <sys/stat.h>

namespace PodStack {
namespace {

constexpr qint64 kGiB = 1024LL * 1024LL * 1024LL;

QString humanAge(const QString &timestamp)
{
    const QDateTime created = QDateTime::fromString(timestamp, Qt::ISODate);
    if (!created.isValid()) {
        return {};
    }
    const qint64 seconds = created.secsTo(QDateTime::currentDateTimeUtc());
    if (seconds < 60) {
        return QStringLiteral("%1s").arg(qMax<qint64>(0, seconds));
    }
    if (seconds < 3600) {
        return QStringLiteral("%1m").arg(seconds / 60);
    }
    if (seconds < 86400) {
        return QStringLiteral("%1h").arg(seconds / 3600);
    }
    return QStringLiteral("%1d").arg(seconds / 86400);
}

QJsonObject podToJson(const QJsonObject &item)
{
    const QJsonObject metadata = item.value(QStringLiteral("metadata")).toObject();
    const QJsonObject spec = item.value(QStringLiteral("spec")).toObject();
    const QJsonObject status = item.value(QStringLiteral("status")).toObject();
    const QString name = metadata.value(QStringLiteral("name")).toString();
    const QString ns = metadata.value(QStringLiteral("namespace")).toString();

    int ready = 0;
    int restarts = 0;
    const QJsonArray containerStatuses =
        status.value(QStringLiteral("containerStatuses")).toArray();
    for (const QJsonValue &value : containerStatuses) {
        const QJsonObject container = value.toObject();
        if (container.value(QStringLiteral("ready")).toBool()) {
            ++ready;
        }
        restarts += container.value(QStringLiteral("restartCount")).toInt();
    }
    const QJsonArray containers = spec.value(QStringLiteral("containers")).toArray();
    const int total = qMax(containers.size(), containerStatuses.size());

    QStringList containerNames;
    for (const QJsonValue &value : containers) {
        const QString containerName = value.toObject().value(QStringLiteral("name")).toString();
        if (!containerName.isEmpty()) {
            containerNames.append(containerName);
        }
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), ns + QLatin1Char('/') + name);
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("namespace"), ns);
    result.insert(QStringLiteral("status"), status.value(QStringLiteral("phase")).toString());
    result.insert(QStringLiteral("ready"), QStringLiteral("%1/%2").arg(ready).arg(total));
    result.insert(QStringLiteral("restarts"), restarts);
    result.insert(QStringLiteral("node"), spec.value(QStringLiteral("nodeName")).toString());
    result.insert(QStringLiteral("ip"), status.value(QStringLiteral("podIP")).toString());
    result.insert(QStringLiteral("age"),
                  humanAge(metadata.value(QStringLiteral("creationTimestamp")).toString()));
    result.insert(QStringLiteral("containers"), QJsonArray::fromStringList(containerNames));
    return result;
}

QJsonObject serviceToJson(const QJsonObject &item)
{
    const QJsonObject metadata = item.value(QStringLiteral("metadata")).toObject();
    const QJsonObject spec = item.value(QStringLiteral("spec")).toObject();
    const QJsonObject status = item.value(QStringLiteral("status")).toObject();
    const QString name = metadata.value(QStringLiteral("name")).toString();
    const QString ns = metadata.value(QStringLiteral("namespace")).toString();

    QStringList ports;
    const QJsonArray portArray = spec.value(QStringLiteral("ports")).toArray();
    for (const QJsonValue &value : portArray) {
        const QJsonObject port = value.toObject();
        const int number = port.value(QStringLiteral("port")).toInt();
        const QString protocol =
            port.value(QStringLiteral("protocol")).toString(QStringLiteral("TCP"));
        if (number > 0) {
            ports.append(QStringLiteral("%1/%2").arg(number).arg(protocol));
        }
    }

    QString external;
    const QJsonArray ingress = status.value(QStringLiteral("loadBalancer"))
                                   .toObject()
                                   .value(QStringLiteral("ingress"))
                                   .toArray();
    if (!ingress.isEmpty()) {
        const QJsonObject entry = ingress.first().toObject();
        external = entry.value(QStringLiteral("ip")).toString();
        if (external.isEmpty()) {
            external = entry.value(QStringLiteral("hostname")).toString();
        }
    }
    if (external.isEmpty()) {
        const QJsonArray externalIps = spec.value(QStringLiteral("externalIPs")).toArray();
        if (!externalIps.isEmpty()) {
            external = externalIps.first().toString();
        }
    }

    QStringList selector;
    const QJsonObject selectorObject = spec.value(QStringLiteral("selector")).toObject();
    for (auto it = selectorObject.constBegin(); it != selectorObject.constEnd(); ++it) {
        selector.append(it.key() + QLatin1Char('=') + it.value().toString());
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), ns + QLatin1Char('/') + name);
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("namespace"), ns);
    result.insert(QStringLiteral("type"), spec.value(QStringLiteral("type")).toString());
    result.insert(QStringLiteral("clusterIP"), spec.value(QStringLiteral("clusterIP")).toString());
    result.insert(QStringLiteral("externalIP"), external);
    result.insert(QStringLiteral("ports"), ports.join(QStringLiteral(", ")));
    result.insert(QStringLiteral("selector"), selector.join(QStringLiteral(", ")));
    result.insert(QStringLiteral("age"),
                  humanAge(metadata.value(QStringLiteral("creationTimestamp")).toString()));
    return result;
}

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
    QString candidate = override.trimmed();
    if (candidate.isEmpty()) {
        candidate = qEnvironmentVariable("PODSTACK_SSH_BINARY").trimmed();
    }
    if (!candidate.isEmpty()) {
        const QString resolved = QStandardPaths::findExecutable(candidate);
        if (!resolved.isEmpty()) {
            return resolved;
        }
        const QFileInfo info(candidate);
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
        QStringLiteral("UserKnownHostsFile=%1")
            .arg(QFileInfo(ssh.identityFile).absolutePath() + QStringLiteral("/known_hosts")),
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
        if (timeoutMs > 0 && timer.elapsed() >= timeoutMs) {
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

QString K8sManager::kubeconfigDirFor(const Options &options)
{
    QString directory = options.kubeconfigDir.trimmed();
    if (directory.isEmpty()) {
        const Config config = Config::load();
        directory = config.get(QStringLiteral("kubernetes.kubeconfigPath")).toString().trimmed();
    }
    if (directory.isEmpty()) {
        directory = Paths::dataDir() + QStringLiteral("/kubeconfigs");
    }
    if (directory == QStringLiteral("~")) {
        directory = QDir::homePath();
    } else if (directory.startsWith(QStringLiteral("~/"))) {
        directory = QDir::homePath() + directory.mid(1);
    }
    return directory;
}

QString K8sManager::contextNameFor(const Options &options, const QString &clusterName)
{
    QString prefix = options.contextPrefix;
    if (prefix.isEmpty()) {
        const Config config = Config::load();
        prefix = config.get(QStringLiteral("kubernetes.contextPrefix")).toString();
    }
    if (prefix.isEmpty()) {
        prefix = QStringLiteral("podstack-");
    }
    return prefix + clusterName;
}

QString K8sManager::rewriteKubeconfig(const QString &raw, const QString &context, int apiPort,
                                      Error *error)
{
    QStringList lines = raw.split(QLatin1Char('\n'));
    bool serverReplaced = false;
    bool contextReplaced = false;
    for (QString &line : lines) {
        const QString trimmed = line.trimmed();
        const int indent = line.indexOf(trimmed);
        const QString padding = indent > 0 ? line.left(indent) : QString();
        if (trimmed.startsWith(QStringLiteral("server:"))) {
            line = padding + QStringLiteral("server: https://127.0.0.1:%1").arg(apiPort);
            serverReplaced = true;
        } else if (trimmed.startsWith(QStringLiteral("current-context:"))) {
            line = padding + QStringLiteral("current-context: ") + context;
            contextReplaced = true;
        } else if (trimmed == QStringLiteral("name: default")) {
            line = padding + QStringLiteral("name: ") + context;
        } else if (trimmed == QStringLiteral("- name: default")) {
            line = padding + QStringLiteral("- name: ") + context;
        } else if (trimmed == QStringLiteral("cluster: default")) {
            line = padding + QStringLiteral("cluster: ") + context;
        } else if (trimmed == QStringLiteral("user: default")) {
            line = padding + QStringLiteral("user: ") + context;
        }
    }
    if (!serverReplaced || !contextReplaced) {
        setError(error, QStringLiteral("PODSTACK_IO_ERROR"),
                 QStringLiteral("O kubeconfig gerado pelo k3s não tem o formato esperado."));
        return {};
    }
    return lines.join(QLatin1Char('\n'));
}

int K8sManager::allocateApiPort(Error *error) const
{
    QList<int> candidates;
    if (m_options.apiPort > 0) {
        candidates.append(m_options.apiPort);
    }
    candidates.append(6443);
    candidates.append(0);
    for (int candidate : candidates) {
        QTcpServer probe;
        if (probe.listen(QHostAddress::LocalHost, candidate)) {
            const int port = probe.serverPort();
            probe.close();
            if (port > 0) {
                return port;
            }
        }
    }
    setError(error, QStringLiteral("PODSTACK_IO_ERROR"),
             QStringLiteral("Não foi possível reservar uma porta para o API server."));
    return 0;
}

bool K8sManager::clusterSsh(SshInfo *ssh, Error *error)
{
    if (!m_machines) {
        setError(error, QStringLiteral("PODSTACK_INTERNAL"),
                 QStringLiteral("Gerenciador de machines indisponível."));
        return false;
    }
    Error storeError;
    const K8sCluster cluster = m_store.get(m_options.clusterName, &storeError);
    if (!storeError.isEmpty()) {
        if (error) {
            *error =
                storeError.code == QStringLiteral("PODSTACK_NOT_FOUND")
                    ? Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                  QStringLiteral("Kubernetes não está habilitado."),
                                  m_options.clusterName,
                                  QStringLiteral("Habilite o cluster antes de listar recursos."))
                    : storeError;
        }
        return false;
    }
    if (cluster.state != QStringLiteral("ready") || cluster.vmMachineId.isEmpty()) {
        setError(error, QStringLiteral("PODSTACK_CONFLICT"),
                 QStringLiteral("O cluster Kubernetes não está pronto."), cluster.state,
                 QStringLiteral("Aguarde o cluster ficar Ready."));
        return false;
    }
    Error machineError;
    const MachineMeta machine = m_machines->get(cluster.vmMachineId, &machineError);
    if (!machineError.isEmpty() || machine.id.isEmpty()) {
        if (error) {
            *error = machineError.isEmpty()
                         ? Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                       QStringLiteral("A VM do cluster não existe."))
                         : machineError;
        }
        return false;
    }
    if (machine.state != QStringLiteral("running")) {
        setError(error, QStringLiteral("PODSTACK_CONFLICT"),
                 QStringLiteral("A VM do cluster não está rodando."), machine.state,
                 QStringLiteral("Inicie a machine do cluster."));
        return false;
    }
    return m_machines->sshInfo(machine.id, ssh, error);
}

bool K8sManager::execRemote(const SshInfo &ssh, const QString &script, const Line &onLine,
                            const Cancelled &cancelled, int timeoutMs, Error *error) const
{
    if (m_options.remoteExec) {
        return m_options.remoteExec(ssh, script, onLine, cancelled, timeoutMs, error);
    }
    return runSsh(resolveBinary(m_options.sshBinary), ssh, script, onLine, cancelled, timeoutMs,
                  error) == RunResult::Completed &&
           (error == nullptr || error->isEmpty());
}

QJsonArray K8sManager::kubectlItems(const QString &resource, const QString &namespaceFilter,
                                    Error *error)
{
    const QString ns = namespaceFilter.trimmed();
    if (!ns.isEmpty()) {
        static const QRegularExpression validNamespace(
            QStringLiteral("^[a-z0-9]([-a-z0-9]*[a-z0-9])?$"));
        if (ns.size() > 63 || !validNamespace.match(ns).hasMatch()) {
            setError(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                     QStringLiteral("Namespace inválido."), ns);
            return {};
        }
    }

    SshInfo ssh;
    if (!clusterSsh(&ssh, error)) {
        return {};
    }

    const QString command =
        ns.isEmpty() ? QStringLiteral("sudo k3s kubectl get %1 -A -o json").arg(resource)
                     : QStringLiteral("sudo k3s kubectl get %1 -n %2 -o json").arg(resource, ns);
    QString raw;
    const auto collect = [&raw](const QString &line) {
        raw += line;
        raw += QLatin1Char('\n');
    };
    Error execError;
    if (!execRemote(ssh, command, collect, {}, 30000, &execError) || !execError.isEmpty()) {
        if (error) {
            *error = execError.isEmpty()
                         ? Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                       QStringLiteral("Não foi possível consultar o cluster."))
                         : execError;
        }
        return {};
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(raw.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("PODSTACK_IO_ERROR"),
                 QStringLiteral("Resposta inválida do kubectl."), parseError.errorString());
        return {};
    }
    const QJsonArray items = document.object().value(QStringLiteral("items")).toArray();
    QVector<QJsonObject> objects;
    objects.reserve(items.size());
    for (const QJsonValue &value : items) {
        if (value.isObject()) {
            objects.append(resource == QStringLiteral("pods") ? podToJson(value.toObject())
                                                              : serviceToJson(value.toObject()));
        }
    }
    std::sort(objects.begin(), objects.end(), [](const QJsonObject &a, const QJsonObject &b) {
        const QString aKey = a.value(QStringLiteral("namespace")).toString() + QLatin1Char('/') +
                             a.value(QStringLiteral("name")).toString();
        const QString bKey = b.value(QStringLiteral("namespace")).toString() + QLatin1Char('/') +
                             b.value(QStringLiteral("name")).toString();
        return aKey < bKey;
    });
    QJsonArray result;
    for (const QJsonObject &object : objects) {
        result.append(object);
    }
    return result;
}

QJsonArray K8sManager::pods(const QString &namespaceFilter, Error *error)
{
    if (error) {
        *error = {};
    }
    return kubectlItems(QStringLiteral("pods"), namespaceFilter, error);
}

QJsonArray K8sManager::services(const QString &namespaceFilter, Error *error)
{
    if (error) {
        *error = {};
    }
    return kubectlItems(QStringLiteral("services"), namespaceFilter, error);
}

bool K8sManager::validateLogsRequest(const QString &namespaceName, const QString &pod,
                                     const QString &container, int tailLines, Error *error)
{
    if (error) {
        *error = {};
    }
    static const QRegularExpression validName(QStringLiteral("^[a-z0-9]([-a-z0-9]*[a-z0-9])?$"));
    const QString ns = namespaceName.trimmed();
    const QString podName = pod.trimmed();
    const QString containerName = container.trimmed();
    if (ns.isEmpty() || ns.size() > 63 || !validName.match(ns).hasMatch() || podName.isEmpty() ||
        podName.size() > 253 || !validName.match(podName).hasMatch() ||
        (!containerName.isEmpty() &&
         (containerName.size() > 63 || !validName.match(containerName).hasMatch()))) {
        setError(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                 QStringLiteral("Namespace, pod ou container inválido."),
                 QStringLiteral("%1/%2/%3").arg(ns, podName, containerName));
        return false;
    }
    if (tailLines < 1 || tailLines > 5000) {
        setError(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                 QStringLiteral("tail deve ficar entre 1 e 5000."), QString::number(tailLines));
        return false;
    }
    return true;
}

bool K8sManager::streamLogs(const QString &namespaceName, const QString &pod,
                            const QString &container, int tailLines, const Line &onLine,
                            const Cancelled &cancelled, Error *error)
{
    if (!validateLogsRequest(namespaceName, pod, container, tailLines, error)) {
        return false;
    }
    const QString ns = namespaceName.trimmed();
    const QString podName = pod.trimmed();
    const QString containerName = container.trimmed();

    SshInfo ssh;
    if (!clusterSsh(&ssh, error)) {
        return false;
    }

    QString command = QStringLiteral("sudo k3s kubectl logs -n %1 %2").arg(ns, podName);
    if (!containerName.isEmpty()) {
        command += QStringLiteral(" -c ") + containerName;
    }
    command += QStringLiteral(" --tail=%1 --timestamps -f").arg(tailLines);

    Error execError;
    const bool ok = execRemote(ssh, command, onLine, cancelled, 0, &execError);
    if (!ok && error) {
        *error = execError;
    }
    return ok && execError.isEmpty();
}

bool K8sManager::deleteResource(const QString &resource, const QString &namespaceName,
                                const QString &name, Error *error)
{
    if (error) {
        *error = {};
    }
    static const QRegularExpression validName(QStringLiteral("^[a-z0-9]([-a-z0-9]*[a-z0-9])?$"));
    const QString ns = namespaceName.trimmed();
    const QString target = name.trimmed();
    if (ns.isEmpty() || !validName.match(ns).hasMatch() || target.isEmpty() ||
        target.size() > 253 || !validName.match(target).hasMatch()) {
        setError(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                 QStringLiteral("Namespace ou nome inválido."),
                 QStringLiteral("%1/%2").arg(ns, target));
        return false;
    }
    SshInfo ssh;
    if (!clusterSsh(&ssh, error)) {
        return false;
    }
    const QString command = QStringLiteral("sudo k3s kubectl delete %1 -n %2 %3 --wait=false")
                                .arg(resource, ns, target);
    Error execError;
    if (!execRemote(ssh, command, {}, {}, 60000, &execError) || !execError.isEmpty()) {
        if (error) {
            *error = execError.isEmpty()
                         ? Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                       QStringLiteral("Não foi possível remover o recurso."))
                         : execError;
        }
        return false;
    }
    return true;
}

bool K8sManager::validatePortForward(const QString &namespaceName, const QString &pod,
                                     int localPort, int remotePort, Error *error)
{
    if (error) {
        *error = {};
    }
    static const QRegularExpression validName(QStringLiteral("^[a-z0-9]([-a-z0-9]*[a-z0-9])?$"));
    const QString ns = namespaceName.trimmed();
    const QString podName = pod.trimmed();
    if (ns.isEmpty() || !validName.match(ns).hasMatch() || podName.isEmpty() ||
        podName.size() > 253 || !validName.match(podName).hasMatch()) {
        setError(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                 QStringLiteral("Namespace ou pod inválido."));
        return false;
    }
    if (localPort < 1 || localPort > 65535 || remotePort < 1 || remotePort > 65535) {
        setError(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                 QStringLiteral("Portas devem ficar entre 1 e 65535."));
        return false;
    }
    return true;
}

bool K8sManager::checkClusterReady(Error *error)
{
    SshInfo ssh;
    return clusterSsh(&ssh, error);
}

bool K8sManager::portForward(const QString &namespaceName, const QString &pod, int localPort,
                             int remotePort, const Line &onLine, const Cancelled &cancelled,
                             Error *error)
{
    if (!validatePortForward(namespaceName, pod, localPort, remotePort, error)) {
        return false;
    }
    SshInfo ssh;
    if (!clusterSsh(&ssh, error)) {
        return false;
    }
    const QString command = QStringLiteral("sudo k3s kubectl port-forward -n %1 pod/%2 %3:%4")
                                .arg(namespaceName.trimmed(), pod.trimmed())
                                .arg(localPort)
                                .arg(remotePort);
    Error execError;
    const bool ok = execRemote(ssh, command, onLine, cancelled, 0, &execError);
    if (!ok && error) {
        *error = execError;
    }
    return ok && execError.isEmpty();
}

bool K8sManager::ensureApiAccess(K8sCluster &cluster, MachineMeta &machine, const SshInfo &ssh,
                                 const Cancelled &cancelled, Error *error)
{
    SshInfo currentSsh = ssh;
    int apiPort = cluster.apiPort;
    if (apiPort <= 0) {
        apiPort = allocateApiPort(error);
        if (apiPort <= 0) {
            return false;
        }
    }

    bool hasApiPort = false;
    for (const MachinePort &port : machine.ports) {
        if (port.label == QStringLiteral("k8s-api")) {
            hasApiPort = port.hostPort == apiPort && port.guestPort == 6443 &&
                         port.protocol == QStringLiteral("tcp");
            break;
        }
    }
    if (!hasApiPort) {
        QVector<MachinePort> next;
        next.reserve(machine.ports.size() + 1);
        for (const MachinePort &port : machine.ports) {
            if (port.label != QStringLiteral("k8s-api")) {
                next.append(port);
            }
        }
        MachinePort api;
        api.id = newUuidV7();
        api.hostPort = apiPort;
        api.guestPort = 6443;
        api.protocol = QStringLiteral("tcp");
        api.exposeLan = false;
        api.label = QStringLiteral("k8s-api");
        next.append(api);

        Error portError;
        machine = m_machines->setPorts(machine.id, next, &portError);
        if (!portError.isEmpty() || machine.id.isEmpty()) {
            if (error) {
                *error = portError.isEmpty()
                             ? Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                           QStringLiteral("Não foi possível expor o API server."))
                             : portError;
            }
            return false;
        }

        SshInfo refreshed;
        if (!m_machines->sshInfo(machine.id, &refreshed, error)) {
            return false;
        }
        if (!waitForSsh(refreshed, cancelled, error) ||
            !waitForNodeReady(refreshed, cancelled, error)) {
            return false;
        }
        currentSsh = refreshed;
    }

    QString raw;
    const auto collect = [&raw](const QString &line) {
        raw += line;
        raw += QLatin1Char('\n');
    };
    Error readError;
    const QString script = QStringLiteral("sudo cat /etc/rancher/k3s/k3s.yaml");
    const bool read =
        m_options.remoteExec
            ? m_options.remoteExec(currentSsh, script, collect, cancelled, 30000, &readError)
            : runSsh(resolveBinary(m_options.sshBinary), currentSsh, script, collect, cancelled,
                     30000, &readError) == RunResult::Completed &&
                  readError.isEmpty();
    if (!read || !readError.isEmpty() || raw.trimmed().isEmpty()) {
        if (error) {
            *error = readError.isEmpty()
                         ? Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                       QStringLiteral("Não foi possível ler o kubeconfig do k3s."))
                         : readError;
        }
        return false;
    }

    const QString context = contextNameFor(m_options, cluster.name);
    Error rewriteError;
    const QString kubeconfig = rewriteKubeconfig(raw, context, apiPort, &rewriteError);
    if (kubeconfig.isEmpty()) {
        if (error) {
            *error = rewriteError;
        }
        return false;
    }

    const QString directory = kubeconfigDirFor(m_options);
    if (directory.isEmpty() || !QDir().mkpath(directory)) {
        setError(error, QStringLiteral("PODSTACK_IO_ERROR"),
                 QStringLiteral("Não foi possível criar o diretório de kubeconfigs."), directory);
        return false;
    }
    QFile::setPermissions(directory,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    QString safeName = cluster.name;
    safeName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    if (safeName.isEmpty()) {
        safeName = QStringLiteral("default");
    }
    const QString path = directory + QLatin1Char('/') + safeName + QStringLiteral(".yaml");
    bool written = false;
    QString writeError;
    {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            writeError = file.errorString();
        } else {
            const QByteArray payload = kubeconfig.toUtf8();
            written = file.write(payload) == payload.size() &&
                      file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) &&
                      file.commit();
            if (!written) {
                writeError = file.errorString();
            }
        }
    }
    if (!written) {
        setError(error, QStringLiteral("PODSTACK_IO_ERROR"),
                 QStringLiteral("Não foi possível gravar o kubeconfig."), writeError);
        return false;
    }
    const QByteArray nativePath = QFile::encodeName(path);
    if (::chmod(nativePath.constData(), S_IRUSR | S_IWUSR) != 0) {
        setError(error, QStringLiteral("PODSTACK_IO_ERROR"),
                 QStringLiteral("Não foi possível restringir o kubeconfig a 0600."), path);
        return false;
    }

    cluster.apiPort = apiPort;
    cluster.contextName = context;
    cluster.kubeconfigPath = path;
    return true;
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
    return status(m_options.clusterName, error);
}

K8sCluster K8sManager::status(const QString &clusterName, Error *error)
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
    K8sCluster cluster = m_store.get(clusterName, &storeError);
    if (storeError.code == QStringLiteral("PODSTACK_NOT_FOUND")) {
        K8sCluster disabled;
        disabled.name = clusterName;
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

    report(QStringLiteral("access"), 92);
    Error accessError;
    if (!ensureApiAccess(cluster, machine, ssh, cancelled, &accessError)) {
        return fail(accessError);
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
