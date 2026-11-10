// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"
#include "k8s/K8sStore.h"
#include "machines/MachineManager.h"

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

    /// Provisiona a VM (se preciso), instala o k3s e aguarda o node Ready.
    /// Idempotente quando o cluster já está pronto.
    K8sCluster enable(const EnableRequest &request, const Progress &progress,
                      const Cancelled &cancelled, Error *error = nullptr);

private:
    QString machineNameForNewCluster();
    bool waitForSsh(const SshInfo &ssh, const Cancelled &cancelled, Error *error);
    bool waitForNodeReady(const SshInfo &ssh, const Cancelled &cancelled, Error *error);
    void persistState(K8sCluster &cluster, const QString &state, const QString &errorText,
                      bool touchUpdatedAt = true);
    static QString nowIso();

    MachineManager *m_machines = nullptr;
    Options m_options;
    K8sStore m_store;
    std::atomic_bool m_enabling{false};
};

} // namespace PodStack
