// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon/StatsCollector.h"

#include "podman/PodmanClient.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QList>

#include <cmath>

namespace PodStack {
namespace {

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

double rounded(double value)
{
    return std::round(value * 100.0) / 100.0;
}

qint64 rate(quint64 current, quint64 previous, double seconds)
{
    if (seconds <= 0.0 || current < previous) {
        return 0;
    }
    return static_cast<qint64>((current - previous) / seconds);
}

} // namespace

void StatsCollector::parseProcStat(const QByteArray &data, quint64 *total, quint64 *idle)
{
    if (total) {
        *total = 0;
    }
    if (idle) {
        *idle = 0;
    }

    const int lineEnd = data.indexOf('\n');
    const QByteArray line = lineEnd >= 0 ? data.left(lineEnd) : data;
    const QList<QByteArray> fields = line.simplified().split(' ');
    if (fields.size() < 8 || fields.first() != QByteArrayLiteral("cpu")) {
        return;
    }

    quint64 sum = 0;
    for (int index = 1; index < 8; ++index) {
        sum += fields.at(index).toULongLong();
    }
    if (total) {
        *total = sum;
    }
    if (idle) {
        *idle = fields.at(4).toULongLong() + fields.at(5).toULongLong();
    }
}

void StatsCollector::parseMemInfo(const QByteArray &data, qint64 *used, qint64 *total)
{
    qint64 memTotal = 0;
    qint64 memAvailable = 0;

    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &line : lines) {
        const int separator = line.indexOf(':');
        if (separator < 0) {
            continue;
        }
        const QByteArray key = line.left(separator).trimmed();
        const QList<QByteArray> fields = line.mid(separator + 1).simplified().split(' ');
        if (fields.isEmpty()) {
            continue;
        }
        const qint64 value = fields.first().toLongLong() * 1024;
        if (key == QByteArrayLiteral("MemTotal")) {
            memTotal = value;
        } else if (key == QByteArrayLiteral("MemAvailable")) {
            memAvailable = value;
        }
    }

    if (total) {
        *total = memTotal;
    }
    if (used) {
        *used = memTotal > memAvailable ? memTotal - memAvailable : 0;
    }
}

void StatsCollector::parseNetDev(const QByteArray &data, quint64 *rx, quint64 *tx)
{
    if (rx) {
        *rx = 0;
    }
    if (tx) {
        *tx = 0;
    }

    const QList<QByteArray> lines = data.split('\n');
    for (int index = 0; index < lines.size(); ++index) {
        const QByteArray &line = lines.at(index);
        if (!line.contains(':')) {
            continue;
        }
        const QByteArray name = line.left(line.indexOf(':')).trimmed();
        if (name.isEmpty() || name == QByteArrayLiteral("lo")
            || name.startsWith(QByteArrayLiteral("lo:"))) {
            continue;
        }
        const QList<QByteArray> fields =
            line.mid(line.indexOf(':') + 1).simplified().split(' ');
        if (fields.size() < 9) {
            continue;
        }
        if (rx) {
            *rx += fields.at(0).toULongLong();
        }
        if (tx) {
            *tx += fields.at(8).toULongLong();
        }
    }
}

void StatsCollector::parseDiskStats(const QByteArray &data, quint64 *readBytes,
                                    quint64 *writeBytes)
{
    if (readBytes) {
        *readBytes = 0;
    }
    if (writeBytes) {
        *writeBytes = 0;
    }

    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> fields = line.simplified().split(' ');
        if (fields.size() < 10) {
            continue;
        }
        const QByteArray name = fields.at(2);
        if (name.startsWith(QByteArrayLiteral("loop"))
            || name.startsWith(QByteArrayLiteral("ram"))) {
            continue;
        }
        if (readBytes) {
            *readBytes += fields.at(5).toULongLong() * 512;
        }
        if (writeBytes) {
            *writeBytes += fields.at(9).toULongLong() * 512;
        }
    }
}

void StatsCollector::setStatsProvider(StatsProvider provider)
{
    m_statsProvider = std::move(provider);
}

QJsonObject StatsCollector::sample(PodmanClient &podman, Error *error)
{
    if (error) {
        *error = {};
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double seconds =
        m_prevTimestampMs > 0 ? qMax(0.001, (now - m_prevTimestampMs) / 1000.0) : 0.0;

    quint64 cpuTotal = 0;
    quint64 cpuIdle = 0;
    quint64 netRx = 0;
    quint64 netTx = 0;
    quint64 diskRead = 0;
    quint64 diskWrite = 0;
    qint64 memUsed = 0;
    qint64 memTotal = 0;

    parseProcStat(readFile(QStringLiteral("/proc/stat")), &cpuTotal, &cpuIdle);
    parseMemInfo(readFile(QStringLiteral("/proc/meminfo")), &memUsed, &memTotal);
    parseNetDev(readFile(QStringLiteral("/proc/net/dev")), &netRx, &netTx);
    parseDiskStats(readFile(QStringLiteral("/proc/diskstats")), &diskRead, &diskWrite);

    double hostCpu = 0.0;
    if (m_prevCpuTotal > 0 && cpuTotal >= m_prevCpuTotal) {
        const double deltaTotal = static_cast<double>(cpuTotal - m_prevCpuTotal);
        const double deltaIdle = static_cast<double>(cpuIdle - m_prevCpuIdle);
        if (deltaTotal > 0.0) {
            hostCpu = 100.0 * (deltaTotal - deltaIdle) / deltaTotal;
        }
    }

    QJsonObject host;
    host.insert(QStringLiteral("cpuPercent"), rounded(hostCpu));
    host.insert(QStringLiteral("memUsed"), memUsed);
    host.insert(QStringLiteral("memTotal"), memTotal);
    host.insert(QStringLiteral("memPercent"),
                memTotal > 0 ? rounded(100.0 * memUsed / memTotal) : 0.0);
    host.insert(QStringLiteral("netRx"), rate(netRx, m_prevNetRx, seconds));
    host.insert(QStringLiteral("netTx"), rate(netTx, m_prevNetTx, seconds));
    host.insert(QStringLiteral("diskRead"), rate(diskRead, m_prevDiskRead, seconds));
    host.insert(QStringLiteral("diskWrite"), rate(diskWrite, m_prevDiskWrite, seconds));

    QJsonArray rawContainers;
    const bool ok = m_statsProvider ? m_statsProvider(&rawContainers, error)
                                    : podman.statsSnapshot(&rawContainers, error);
    if (!ok) {
        return {};
    }

    QJsonArray containers;
    QHash<QString, ContainerPrevious> nextPrevious;
    double totalCpu = 0.0;
    qint64 totalMem = 0;
    qint64 totalLimit = 0;
    qint64 totalRx = 0;
    qint64 totalTx = 0;
    int totalPids = 0;

    for (const QJsonValue &value : rawContainers) {
        const QJsonObject entry = value.toObject();
        QString id = entry.value(QStringLiteral("ContainerID")).toString();
        if (id.isEmpty()) {
            id = entry.value(QStringLiteral("Id")).toString();
        }
        if (id.isEmpty()) {
            id = entry.value(QStringLiteral("id")).toString();
        }
        QString name = entry.value(QStringLiteral("Name")).toString();
        if (name.isEmpty()) {
            name = entry.value(QStringLiteral("name")).toString();
        }

        double cpu = entry.value(QStringLiteral("CPU")).toDouble();
        if (cpu <= 0.0) {
            cpu = entry.value(QStringLiteral("AvgCPU")).toDouble();
        }
        if (cpu <= 0.0) {
            QString text = entry.value(QStringLiteral("cpu_percent")).toString();
            text.remove(QLatin1Char('%'));
            cpu = text.toDouble();
        }

        const qint64 memUsage =
            static_cast<qint64>(entry.value(QStringLiteral("MemUsage")).toDouble());
        const qint64 memLimit =
            static_cast<qint64>(entry.value(QStringLiteral("MemLimit")).toDouble());
        double memPercent = entry.value(QStringLiteral("MemPerc")).toDouble();
        if (memPercent <= 0.0 && memTotal > 0) {
            memPercent = 100.0 * memUsage / memTotal;
        }

        quint64 rx = 0;
        quint64 tx = 0;
        const QJsonObject networks = entry.value(QStringLiteral("Network")).toObject();
        for (auto it = networks.constBegin(); it != networks.constEnd(); ++it) {
            const QJsonObject network = it.value().toObject();
            rx += static_cast<quint64>(
                network.value(QStringLiteral("RxBytes")).toDouble());
            tx += static_cast<quint64>(
                network.value(QStringLiteral("TxBytes")).toDouble());
        }

        const quint64 blockRead = static_cast<quint64>(
            entry.value(QStringLiteral("BlockInput")).toDouble());
        const quint64 blockWrite = static_cast<quint64>(
            entry.value(QStringLiteral("BlockOutput")).toDouble());
        const int pids = entry.value(QStringLiteral("PIDs")).toInt();

        const ContainerPrevious previous = m_prevContainers.value(id);
        const qint64 rxRate = rate(rx, previous.netRx, seconds);
        const qint64 txRate = rate(tx, previous.netTx, seconds);
        const qint64 readRate = rate(blockRead, previous.blockRead, seconds);
        const qint64 writeRate = rate(blockWrite, previous.blockWrite, seconds);

        QJsonObject item;
        item.insert(QStringLiteral("id"), id);
        item.insert(QStringLiteral("name"), name);
        item.insert(QStringLiteral("cpuPercent"), rounded(cpu));
        item.insert(QStringLiteral("memUsed"), memUsage);
        item.insert(QStringLiteral("memLimit"), memLimit);
        item.insert(QStringLiteral("memPercent"), rounded(memPercent));
        item.insert(QStringLiteral("netRx"), rxRate);
        item.insert(QStringLiteral("netTx"), txRate);
        item.insert(QStringLiteral("diskRead"), readRate);
        item.insert(QStringLiteral("diskWrite"), writeRate);
        item.insert(QStringLiteral("pids"), pids);
        containers.append(item);

        nextPrevious.insert(id, ContainerPrevious{rx, tx, blockRead, blockWrite});
        totalCpu += cpu;
        totalMem += memUsage;
        totalLimit += memLimit;
        totalRx += rxRate;
        totalTx += txRate;
        totalPids += pids;
    }

    QJsonObject totals;
    totals.insert(QStringLiteral("cpuPercent"), rounded(totalCpu));
    totals.insert(QStringLiteral("memUsed"), totalMem);
    totals.insert(QStringLiteral("memLimit"), totalLimit);
    totals.insert(QStringLiteral("memPercent"),
                  memTotal > 0 ? rounded(100.0 * totalMem / memTotal) : 0.0);
    totals.insert(QStringLiteral("netRx"), totalRx);
    totals.insert(QStringLiteral("netTx"), totalTx);
    totals.insert(QStringLiteral("pids"), totalPids);

    QJsonObject result;
    result.insert(QStringLiteral("ts"), now);
    result.insert(QStringLiteral("host"), host);
    result.insert(QStringLiteral("containers"), containers);
    result.insert(QStringLiteral("totals"), totals);

    m_prevCpuTotal = cpuTotal;
    m_prevCpuIdle = cpuIdle;
    m_prevNetRx = netRx;
    m_prevNetTx = netTx;
    m_prevDiskRead = diskRead;
    m_prevDiskWrite = diskWrite;
    m_prevTimestampMs = now;
    m_prevContainers = nextPrevious;
    return result;
}

} // namespace PodStack
