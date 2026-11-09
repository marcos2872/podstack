// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/MachineStats.h"

#include "daemon/StatsCollector.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

#include <cmath>

namespace PodStack {
namespace {

constexpr int kSshTotalTimeoutMs = 6000;
constexpr qint64 kMaxOutputBytes = 2 * 1024 * 1024;
constexpr int kMaxErrorDetail = 4096;

const QString kRemoteCommand =
    QStringLiteral("cat /proc/stat; echo __PODSTACK_MEM__; cat /proc/meminfo; "
                   "echo __PODSTACK_NET__; cat /proc/net/dev; "
                   "echo __PODSTACK_DISK__; cat /proc/diskstats; "
                   "echo __PODSTACK_DF__; df -B1 -P /");

const QByteArray kMemMarker = QByteArrayLiteral("__PODSTACK_MEM__");
const QByteArray kNetMarker = QByteArrayLiteral("__PODSTACK_NET__");
const QByteArray kDiskMarker = QByteArrayLiteral("__PODSTACK_DISK__");
const QByteArray kDfMarker = QByteArrayLiteral("__PODSTACK_DF__");

bool fails(Error *error, const Error &value)
{
    if (error) {
        *error = value;
    }
    return false;
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

QString sshBinary()
{
    const QString override = qEnvironmentVariable("PODSTACK_SSH_BINARY").trimmed();
    if (!override.isEmpty()) {
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

QString processDetail(const QByteArray &output)
{
    QString detail = QString::fromUtf8(output).trimmed();
    if (detail.size() > kMaxErrorDetail) {
        detail = detail.left(kMaxErrorDetail);
    }
    return detail;
}

bool parseDf(const QByteArray &data, quint64 *used, quint64 *total)
{
    if (used) {
        *used = 0;
    }
    if (total) {
        *total = 0;
    }

    const QList<QByteArray> lines = data.split('\n');
    for (int index = lines.size() - 1; index >= 0; --index) {
        const QByteArray line = lines.at(index).trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const QList<QByteArray> fields = line.simplified().split(' ');
        if (fields.size() < 4) {
            return false;
        }

        bool totalOk = false;
        bool usedOk = false;
        const quint64 parsedTotal = fields.at(1).toULongLong(&totalOk);
        const quint64 parsedUsed = fields.at(2).toULongLong(&usedOk);
        if (!totalOk || !usedOk) {
            return false;
        }
        if (used) {
            *used = parsedUsed;
        }
        if (total) {
            *total = parsedTotal;
        }
        return true;
    }
    return false;
}

} // namespace

QJsonObject MachineStatsCollector::sample(const MachineMeta &machine, const SshInfo &ssh,
                                          Error *error)
{
    if (error) {
        *error = {};
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    if (machine.state != QStringLiteral("running")) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                          QStringLiteral("A machine não está em execução."), machine.id,
                          QStringLiteral("Inicie a machine e tente novamente.")));
        return {};
    }

    const QFileInfo keyInfo(machine.sshKeyPath);
    if (machine.sshKeyPath.isEmpty() || keyInfo.isSymLink() || !keyInfo.isFile()) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                          QStringLiteral("A chave SSH da machine não existe."), machine.id,
                          QStringLiteral("Recrie a machine para gerar uma nova chave.")));
        return {};
    }

    if (ssh.host.isEmpty() || ssh.user.isEmpty() || ssh.port <= 0) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                          QStringLiteral("O destino SSH da machine é inválido."), machine.id,
                          QStringLiteral("Recrie a machine para regenerar os dados de SSH.")));
        return {};
    }

    const QString binary = sshBinary();
    if (binary.isEmpty()) {
        fails(error,
              Error::make(
                  QStringLiteral("PODSTACK_UNSUPPORTED"),
                  QStringLiteral("O cliente SSH não foi encontrado."), QStringLiteral("ssh"),
                  QStringLiteral("Instale o OpenSSH client (openssh-clients no Fedora ou "
                                 "openssh-client no Debian/Ubuntu).")));
        return {};
    }

    const QString identity =
        ssh.identityFile.isEmpty() ? machine.sshKeyPath : ssh.identityFile;
    const QStringList arguments{
        QStringLiteral("-i"),
        identity,
        QStringLiteral("-p"),
        QString::number(ssh.port),
        QStringLiteral("-o"),
        QStringLiteral("IdentitiesOnly=yes"),
        QStringLiteral("-o"),
        QStringLiteral("StrictHostKeyChecking=accept-new"),
        QStringLiteral("-o"),
        QStringLiteral("BatchMode=yes"),
        QStringLiteral("-o"),
        QStringLiteral("ConnectTimeout=3"),
        QStringLiteral("%1@%2").arg(ssh.user, ssh.host),
        kRemoteCommand,
    };

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    QElapsedTimer timer;
    timer.start();
    process.start(binary, arguments);
    const int startTimeout = kSshTotalTimeoutMs - static_cast<int>(timer.elapsed());
    if (startTimeout <= 0 || !process.waitForStarted(startTimeout)) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                          QStringLiteral("Não foi possível executar o SSH."),
                          process.errorString(),
                          QStringLiteral("Verifique o OpenSSH e as permissões da chave.")));
        return {};
    }
    const int finishTimeout = kSshTotalTimeoutMs - static_cast<int>(timer.elapsed());
    if (finishTimeout <= 0 || !process.waitForFinished(finishTimeout)) {
        process.kill();
        process.waitForFinished(1000);
        fails(error,
              Error::make(QStringLiteral("PODSTACK_TIMEOUT"),
                          QStringLiteral("A coleta de métricas excedeu o tempo limite."),
                          machine.id,
                          QStringLiteral("Verifique a rede/SSH da machine e tente novamente.")));
        return {};
    }

    const QByteArray output = process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QString detail = processDetail(process.readAllStandardError());
        if (detail.isEmpty()) {
            detail = machine.id;
        }
        fails(error,
              Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                          QStringLiteral("Não foi possível coletar as métricas da machine."),
                          detail,
                          QStringLiteral("Confirme que a machine está em execução e que o "
                                         "SSH responde.")));
        return {};
    }
    if (output.size() > kMaxOutputBytes) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                          QStringLiteral("A resposta de métricas da machine é grande demais."),
                          machine.id,
                          QStringLiteral("Verifique o shell remoto da machine.")));
        return {};
    }

    const int memPosition = output.indexOf(kMemMarker);
    const int netPosition =
        memPosition >= 0 ? output.indexOf(kNetMarker, memPosition + kMemMarker.size()) : -1;
    const int diskPosition =
        netPosition >= 0 ? output.indexOf(kDiskMarker, netPosition + kNetMarker.size()) : -1;
    const int dfPosition =
        diskPosition >= 0 ? output.indexOf(kDfMarker, diskPosition + kDiskMarker.size()) : -1;
    if (memPosition < 0 || netPosition < 0 || diskPosition < 0 || dfPosition < 0) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                          QStringLiteral("Resposta SSH incompleta para as métricas da machine."),
                          machine.id,
                          QStringLiteral("Verifique se /proc e df estão disponíveis na machine.")));
        return {};
    }

    const QByteArray cpuData = output.left(memPosition);
    const QByteArray memData =
        output.mid(memPosition + kMemMarker.size(),
                   netPosition - (memPosition + kMemMarker.size()));
    const QByteArray netData =
        output.mid(netPosition + kNetMarker.size(),
                   diskPosition - (netPosition + kNetMarker.size()));
    const QByteArray diskData =
        output.mid(diskPosition + kDiskMarker.size(),
                   dfPosition - (diskPosition + kDiskMarker.size()));
    const QByteArray dfData = output.mid(dfPosition + kDfMarker.size());

    quint64 cpuTotal = 0;
    quint64 cpuIdle = 0;
    quint64 netRx = 0;
    quint64 netTx = 0;
    quint64 diskRead = 0;
    quint64 diskWrite = 0;
    qint64 memUsed = 0;
    qint64 memTotal = 0;
    quint64 diskUsed = 0;
    quint64 diskTotal = 0;

    StatsCollector::parseProcStat(cpuData, &cpuTotal, &cpuIdle);
    StatsCollector::parseMemInfo(memData, &memUsed, &memTotal);
    StatsCollector::parseNetDev(netData, &netRx, &netTx);
    StatsCollector::parseDiskStats(diskData, &diskRead, &diskWrite);
    if (!parseDf(dfData, &diskUsed, &diskTotal)) {
        fails(error,
              Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                          QStringLiteral("Não foi possível interpretar o uso de disco da "
                                         "machine."),
                          machine.id,
                          QStringLiteral("Verifique se df está disponível na machine.")));
        return {};
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString key = machine.id.isEmpty()
                            ? QStringLiteral("%1:%2").arg(ssh.host).arg(ssh.port)
                            : machine.id;
    const Previous previous = m_previous.value(key);
    const double seconds =
        previous.timestampMs > 0 ? qMax(0.001, (now - previous.timestampMs) / 1000.0) : 0.0;

    double cpuPercent = 0.0;
    if (previous.cpuTotal > 0 && cpuTotal >= previous.cpuTotal
        && cpuIdle >= previous.cpuIdle) {
        const double deltaTotal = static_cast<double>(cpuTotal - previous.cpuTotal);
        const double deltaIdle = static_cast<double>(cpuIdle - previous.cpuIdle);
        if (deltaTotal > 0.0) {
            cpuPercent = 100.0 * (deltaTotal - deltaIdle) / deltaTotal;
        }
    }

    QJsonObject result;
    result.insert(QStringLiteral("ts"), now);
    result.insert(QStringLiteral("cpuPercent"), rounded(cpuPercent));
    result.insert(QStringLiteral("memUsed"), memUsed);
    result.insert(QStringLiteral("memTotal"), memTotal);
    result.insert(QStringLiteral("memPercent"),
                  memTotal > 0 ? rounded(100.0 * static_cast<double>(memUsed) / memTotal)
                               : 0.0);
    result.insert(QStringLiteral("netRx"), rate(netRx, previous.netRx, seconds));
    result.insert(QStringLiteral("netTx"), rate(netTx, previous.netTx, seconds));
    result.insert(QStringLiteral("diskRead"), rate(diskRead, previous.diskRead, seconds));
    result.insert(QStringLiteral("diskWrite"), rate(diskWrite, previous.diskWrite, seconds));
    result.insert(QStringLiteral("diskUsed"), static_cast<qint64>(diskUsed));
    result.insert(QStringLiteral("diskTotal"), static_cast<qint64>(diskTotal));
    result.insert(QStringLiteral("diskPercent"),
                  diskTotal > 0
                      ? rounded(100.0 * static_cast<double>(diskUsed) / diskTotal)
                      : 0.0);

    Previous next;
    next.cpuTotal = cpuTotal;
    next.cpuIdle = cpuIdle;
    next.netRx = netRx;
    next.netTx = netTx;
    next.diskRead = diskRead;
    next.diskWrite = diskWrite;
    next.timestampMs = now;
    m_previous.insert(key, next);
    return result;
}

void MachineStatsCollector::reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_previous.clear();
}

} // namespace PodStack
