// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"
#include "k8s/K8sStore.h"
#include "machines/MachineManager.h"

#include <QJsonArray>
#include <QString>

#include <atomic>
#include <functional>
#include <optional>

namespace PodStack {

/// Orquestra a VM dedicada e a instalação do k3s (T5-01).
class K8sManager {
public:
    using Progress = std::function<void(const QString &stage, int progress)>;
    using Cancelled = std::function<bool()>;
    using Line = std::function<void(const QString &line)>;
    using RemoteExec =
        std::function<bool(const SshInfo &ssh, const QString &script, const Line &onLine,
                           const Cancelled &cancelled, int timeoutMs, Error *error)>;

    struct Options {
        Options() = default;

        QString storePath; // vazio = podstack.db
        QString clusterName = QStringLiteral("default");
        QString machineName = QStringLiteral("podstack-k8s");
        QString distro = QStringLiteral("ubuntu");
        QString version = QStringLiteral("24.04");
        QString sshBinary;     // vazio = PODSTACK_SSH_BINARY/QStandardPaths
        QString installScript; // vazio = instalador oficial do k3s
        QString kubeconfigDir; // vazio = setting kubernetes.kubeconfigPath
        QString contextPrefix; // vazio = setting kubernetes.contextPrefix
        int apiPort = 0;       // 0 = escolhe 6443 ou uma porta livre
        int sshReadyTimeoutMs = 120000;
        int installTimeoutMs = 600000;
        int readyTimeoutMs = 130000;
        RemoteExec remoteExec; // vazio = SSH real
    };

    struct EnableRequest {
        std::optional<int> cpus;
        std::optional<qint64> memoryBytes;
        std::optional<qint64> diskBytes;
    };

    explicit K8sManager(MachineManager *machines);
    K8sManager(MachineManager *machines, Options options);

    /// Estado atual; reconcilia a machine da VM para refletir stopped/error.
    K8sCluster status(Error *error = nullptr);
    K8sCluster status(const QString &clusterName, Error *error = nullptr);

    /// Pods do cluster (namespace vazio = todos), no formato consumido pela GUI/CLI.
    QJsonArray pods(const QString &namespaceFilter, Error *error = nullptr);
    /// Services do cluster (namespace vazio = todos).
    QJsonArray services(const QString &namespaceFilter, Error *error = nullptr);
    /// Acompanha os logs de um container do pod via `kubectl logs -f`.
    bool streamLogs(const QString &namespaceName, const QString &pod, const QString &container,
                    int tailLines, const Line &onLine, const Cancelled &cancelled,
                    Error *error = nullptr);
    static bool validateLogsRequest(const QString &namespaceName, const QString &pod,
                                    const QString &container, int tailLines,
                                    Error *error = nullptr);
    /// Remove um Pod/Service do cluster.
    bool deleteResource(const QString &resource, const QString &namespaceName, const QString &name,
                        Error *error = nullptr);
    /// Mantém um `kubectl port-forward` ativo até o cancelamento.
    bool portForward(const QString &resource, const QString &namespaceName, const QString &name,
                     int localPort, int remotePort, bool exposeLan, const Line &onLine,
                     const Cancelled &cancelled, Error *error = nullptr);
    static bool validatePortForward(const QString &resource, const QString &namespaceName,
                                    const QString &name, int localPort, int remotePort,
                                    Error *error = nullptr);
    /// Verifica se o cluster está pronto e a VM rodando (sem abrir um stream).
    bool checkClusterReady(Error *error = nullptr);

    /// Provisiona a VM (se preciso), instala o k3s e aguarda o node Ready.
    /// Idempotente quando o cluster já está pronto.
    K8sCluster enable(const EnableRequest &request, const Progress &progress,
                      const Cancelled &cancelled, Error *error = nullptr);
    /// Remove a VM, o kubeconfig e o registro do cluster (idempotente).
    K8sCluster disable(bool force, const Progress &progress, const Cancelled &cancelled,
                       Error *error = nullptr);

private:
    QString machineNameForNewCluster();
    bool waitForSsh(const SshInfo &ssh, const Cancelled &cancelled, Error *error);
    bool waitForNodeReady(const SshInfo &ssh, const Cancelled &cancelled, Error *error);
    bool ensureApiAccess(K8sCluster &cluster, MachineMeta &machine, const SshInfo &ssh,
                         const Cancelled &cancelled, Error *error);
    bool clusterSsh(SshInfo *ssh, Error *error);
    bool execRemote(const SshInfo &ssh, const QString &script, const Line &onLine,
                    const Cancelled &cancelled, int timeoutMs, Error *error) const;
    QJsonArray kubectlItems(const QString &resource, const QString &namespaceFilter, Error *error);
    int allocateApiPort(Error *error) const;
    void persistState(K8sCluster &cluster, const QString &state, const QString &errorText,
                      bool touchUpdatedAt = true);
    static QString nowIso();
    static QString kubeconfigDirFor(const Options &options);
    static QString contextNameFor(const Options &options, const QString &clusterName);
    static QString rewriteKubeconfig(const QString &raw, const QString &context, int apiPort,
                                     Error *error);

    MachineManager *m_machines = nullptr;
    Options m_options;
    K8sStore m_store;
    std::atomic_bool m_enabling{false};
};

} // namespace PodStack
