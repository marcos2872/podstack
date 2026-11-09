// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Error.h"

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <functional>

namespace PodStack {

class PodmanClient;

/// Coleta CPU/memória/rede/disco do host e stats agregadas dos containers.
///
/// As taxas (rede/disco) e o CPU do host usam a diferença entre amostras;
/// a primeira amostra retorna taxas zero. Os parsers de /proc são estáticos e
/// testáveis com conteúdo sintético.
class StatsCollector {
public:
    using StatsProvider = std::function<bool(QJsonArray *, Error *)>;

    QJsonObject sample(PodmanClient &podman, Error *error = nullptr);
    /// Injeta a fonte de stats de containers (usado em testes).
    void setStatsProvider(StatsProvider provider);

    static void parseProcStat(const QByteArray &data, quint64 *total, quint64 *idle);
    static void parseMemInfo(const QByteArray &data, qint64 *used, qint64 *total);
    static void parseNetDev(const QByteArray &data, quint64 *rx, quint64 *tx);
    static void parseDiskStats(const QByteArray &data, quint64 *readBytes, quint64 *writeBytes);

private:
    struct ContainerPrevious {
        quint64 netRx = 0;
        quint64 netTx = 0;
        quint64 blockRead = 0;
        quint64 blockWrite = 0;
    };

    quint64 m_prevCpuTotal = 0;
    quint64 m_prevCpuIdle = 0;
    quint64 m_prevNetRx = 0;
    quint64 m_prevNetTx = 0;
    quint64 m_prevDiskRead = 0;
    quint64 m_prevDiskWrite = 0;
    qint64 m_prevTimestampMs = 0;
    QHash<QString, ContainerPrevious> m_prevContainers;
    StatsProvider m_statsProvider;
};

} // namespace PodStack
