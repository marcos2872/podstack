// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"
#include "machines/MachineManager.h"

#include <QHash>
#include <QJsonObject>

#include <mutex>

namespace PodStack {

/// Coleta CPU/memória/rede/disco de dentro de uma machine via SSH one-shot.
///
/// O comando remoto é fixo e usa apenas arquivos de `/proc` e `df`, sem
/// qemu-guest-agent. As taxas de rede/disco e o CPU usam a diferença entre
/// amostras; a primeira amostra de cada machine retorna taxas zero.
class MachineStatsCollector {
public:
    QJsonObject sample(const MachineMeta &machine, const SshInfo &ssh, Error *error = nullptr);
    /// Coleta as mesmas métricas via qemu-guest-agent (`guest-exec` + `/bin/sh`).
    QJsonObject sampleAgent(const MachineMeta &machine, const QString &agentSocketPath,
                            Error *error = nullptr);
    void reset();

private:
    QJsonObject parseOutput(const MachineMeta &machine, const QString &key,
                            const QByteArray &output, const QString &origin, Error *error);

    struct Previous {
        quint64 cpuTotal = 0;
        quint64 cpuIdle = 0;
        quint64 netRx = 0;
        quint64 netTx = 0;
        quint64 diskRead = 0;
        quint64 diskWrite = 0;
        qint64 timestampMs = 0;
    };

    QHash<QString, Previous> m_previous;
    std::mutex m_mutex;
};

} // namespace PodStack
