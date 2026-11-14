// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/controllers/ContainerDetailController.h"

#include "gui/controllers/ContainerController.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>

namespace PodStack {
namespace {

double parsePercent(const QString &text)
{
    QString copy = text.trimmed();
    copy.remove(QLatin1Char('%'));
    bool ok = false;
    const double value = copy.toDouble(&ok);
    return ok ? value : 0.0;
}

qint64 parseHumanBytes(const QString &text)
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^\s*([0-9]+(?:\.[0-9]+)?)\s*([kKmMgGtT]?[bB])\s*$)"));
    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch()) {
        return 0;
    }
    double value = match.captured(1).toDouble();
    const QString unit = match.captured(2).toLower();
    if (unit == QStringLiteral("kb")) {
        value *= 1000.0;
    } else if (unit == QStringLiteral("mb")) {
        value *= 1000.0 * 1000.0;
    } else if (unit == QStringLiteral("gb")) {
        value *= 1000.0 * 1000.0 * 1000.0;
    } else if (unit == QStringLiteral("tb")) {
        value *= 1000.0 * 1000.0 * 1000.0 * 1000.0;
    }
    return static_cast<qint64>(value);
}

void parseBytePair(const QString &text, qint64 *first, qint64 *second)
{
    const int separator = text.indexOf(QStringLiteral(" / "));
    if (first) {
        *first = parseHumanBytes(separator >= 0 ? text.left(separator) : text);
    }
    if (second) {
        *second = separator >= 0 ? parseHumanBytes(text.mid(separator + 3)) : 0;
    }
}

void appendHistory(QVariantList &list, double value)
{
    list.append(value);
    if (list.size() > 60) {
        list.removeFirst();
    }
}

} // namespace

ContainerDetailController::ContainerDetailController(ContainerController *containers,
                                                     QObject *parent)
    : ControllerBase(parent), m_containers(containers)
{
    m_logsTimer = new QTimer(this);
    m_logsTimer->setSingleShot(true);
    m_logsTimer->setInterval(50);
    connect(m_logsTimer, &QTimer::timeout, this, [this] {
        m_logsPending = false;
        emit logsChanged();
    });

    connect(&m_statsStream, &StreamClient::stats, this, [this](const QJsonObject &stats) {
        m_stats = stats.toVariantMap();

        qint64 memUsed = 0;
        qint64 memLimit = 0;
        parseBytePair(stats.value(QStringLiteral("mem_usage")).toString(), &memUsed, &memLimit);
        m_stats.insert(QStringLiteral("memUsedBytes"), memUsed);
        m_stats.insert(QStringLiteral("memLimitBytes"), memLimit);

        qint64 netRx = 0;
        qint64 netTx = 0;
        parseBytePair(stats.value(QStringLiteral("net_io")).toString(), &netRx, &netTx);
        m_stats.insert(QStringLiteral("netRxBytes"), netRx);
        m_stats.insert(QStringLiteral("netTxBytes"), netTx);

        qint64 diskRead = 0;
        qint64 diskWrite = 0;
        parseBytePair(stats.value(QStringLiteral("block_io")).toString(), &diskRead, &diskWrite);
        m_stats.insert(QStringLiteral("diskReadBytes"), diskRead);
        m_stats.insert(QStringLiteral("diskWriteBytes"), diskWrite);

        appendHistory(m_cpuHistory,
                      parsePercent(stats.value(QStringLiteral("cpu_percent")).toString()));
        appendHistory(m_memHistory,
                      parsePercent(stats.value(QStringLiteral("mem_percent")).toString()));
        appendHistory(m_netRxHistory, static_cast<double>(netRx));
        appendHistory(m_netTxHistory, static_cast<double>(netTx));
        appendHistory(m_diskReadHistory, static_cast<double>(diskRead));
        appendHistory(m_diskWriteHistory, static_cast<double>(diskWrite));
        m_statsError.clear();
        emit statsChanged();
    });
    connect(&m_statsStream, &StreamClient::error, this, [this](const QString &message) {
        m_statsError = message;
        emit statsChanged();
    });

    connect(&m_logsStream, &StreamClient::logLine, this, [this](const QString &line) {
        m_logLines.append(line);
        if (m_logLines.size() > 10000) {
            m_logLines = m_logLines.mid(2000);
        }
        if (!m_logsPending) {
            m_logsPending = true;
            m_logsTimer->start();
        }
    });
    connect(&m_logsStream, &StreamClient::error, this, [this](const QString &message) {
        m_logsError = message;
        emit logsChanged();
    });
}

void ContainerDetailController::load(const QString &id)
{
    if (id.isEmpty()) {
        m_detail.clear();
        m_detailJson = {};
        m_error = QStringLiteral("Container inválido.");
        m_loading = false;
        emit detailChanged();
        return;
    }

    m_detail.clear();
    m_detailJson = {};
    m_error.clear();
    m_loading = true;
    emit detailChanged();

    callRpc(QStringLiteral("containers.inspect"), QJsonObject{{QStringLiteral("id"), id}},
            [this, id](const QJsonObject &result) {
                m_loading = false;
                if (isErrorResult(result)) {
                    m_error = errorMessage(result);
                    if (m_error.isEmpty()) {
                        m_error = QStringLiteral("Falha ao carregar o inspect.");
                    }
                } else {
                    const QJsonObject json = result.value(QStringLiteral("json")).toObject();
                    if (json.isEmpty()) {
                        m_error = QStringLiteral("Inspect vazio ou inválido.");
                    } else {
                        m_detailJson = json;
                        m_detail = json.toVariantMap();
                        m_error.clear();
                    }
                }
                emit detailChanged();
                if (!m_pendingId.isEmpty() && m_pendingId != id) {
                    const QString next = m_pendingId;
                    m_pendingId.clear();
                    load(next);
                }
            });
}

void ContainerDetailController::copyInspect()
{
    if (m_detailJson.isEmpty()) {
        return;
    }
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        return;
    }
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(QJsonDocument(m_detailJson).toJson(QJsonDocument::Indented));
    }
}

void ContainerDetailController::startStats(const QString &id)
{
    m_stats.clear();
    m_cpuHistory.clear();
    m_memHistory.clear();
    m_netRxHistory.clear();
    m_netTxHistory.clear();
    m_diskReadHistory.clear();
    m_diskWriteHistory.clear();
    m_statsError.clear();
    emit statsChanged();

    if (id.isEmpty()) {
        m_statsError = QStringLiteral("Container inválido.");
        emit statsChanged();
        return;
    }
    m_statsStream.subscribeStats(id);
}

void ContainerDetailController::stopStats()
{
    m_statsStream.unsubscribe();
}

void ContainerDetailController::startLogs(const QString &id, int tail, bool follow)
{
    m_logsTimer->stop();
    m_logLines.clear();
    m_logsError.clear();
    m_logsPending = false;
    emit logsChanged();

    if (id.isEmpty()) {
        m_logsError = QStringLiteral("Container inválido.");
        emit logsChanged();
        return;
    }
    if (tail <= 0) {
        tail = 200;
    }
    tail = qBound(1, tail, 2000);
    m_logsStream.subscribeLogs(id, tail, follow);
}

void ContainerDetailController::stopLogs()
{
    m_logsTimer->stop();
    m_logsPending = false;
    m_logsStream.unsubscribe();
}

void ContainerDetailController::clearLogs()
{
    m_logsTimer->stop();
    m_logLines.clear();
    m_logsPending = false;
    emit logsChanged();
}

void ContainerDetailController::lifecycle(const QString &action, const QString &id)
{
    if (id.isEmpty() || (action != QStringLiteral("start") && action != QStringLiteral("stop") &&
                         action != QStringLiteral("restart") && action != QStringLiteral("pause") &&
                         action != QStringLiteral("unpause") && action != QStringLiteral("kill"))) {
        setLastError(QStringLiteral("Ação ou container inválido."));
        return;
    }
    if (m_containers) {
        m_containers->lifecycle(action, id);
    }
}

void ContainerDetailController::remove(const QString &id, bool force)
{
    if (m_containers) {
        m_containers->removeContainer(id, force, false);
    }
}

} // namespace PodStack
