// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/controllers/SystemController.h"

#include "core/config/Paths.h"
#include "core/error/Error.h"
#include "core/helper/HelperClient.h"
#include "core/logging/Redaction.h"
#include "core/rpc/RpcClient.h"

#include <QDate>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>

namespace PodStack {

SystemController::SystemController(QObject *parent) : ControllerBase(parent)
{
    connect(&m_logsWatcher, &QFutureWatcher<Task>::finished, this,
            &SystemController::onLogsFinished);
    connect(&m_diskWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &SystemController::onDiskFinished);
    connect(&m_reportWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &SystemController::onReportFinished);
    connect(&m_activityStream, &StreamClient::systemStats, this, [this](const QJsonObject &sample) {
        m_activityStats = sample.toVariantMap();
        const QJsonObject host = sample.value(QStringLiteral("host")).toObject();
        const auto append = [](QVariantList &list, const QString &text) {
            QString copy = text;
            copy.remove(QLatin1Char('%'));
            bool ok = false;
            const double value = copy.toDouble(&ok);
            if (ok) {
                list.append(value);
                if (list.size() > 60) {
                    list.removeFirst();
                }
            }
        };
        append(m_activityCpuHistory,
               host.value(QStringLiteral("cpuPercent")).toVariant().toString());
        append(m_activityMemHistory,
               host.value(QStringLiteral("memPercent")).toVariant().toString());
        emit activityChanged();
    });

    auto *diskTimer = new QTimer(this);
    diskTimer->setInterval(5 * 60 * 1000);
    connect(diskTimer, &QTimer::timeout, this, &SystemController::checkDiskHealth);
    diskTimer->start();
    QTimer::singleShot(15000, this, &SystemController::checkDiskHealth);
}

void SystemController::loadLogs(const QString &level, const QString &component, int sinceDays)
{
    if (m_logsWatcher.isRunning()) {
        return;
    }
    m_logsBusy = true;
    m_logsError.clear();
    emit logsChanged();

    LogReadOptions options;
    options.since = QDate::currentDate().addDays(-(qMax(1, sinceDays) - 1));
    options.level = level;
    options.component = component;
    m_pendingOptions = options;
    m_pendingComponent = component;
    m_logsWatcher.setFuture(QtConcurrent::run([options]() {
        Task task;
        readUserLogs(options, &task.result, &task.error);
        return task;
    }));
}

void SystemController::onLogsFinished()
{
    m_logsBusy = false;
    const Task task = m_logsWatcher.result();
    if (!task.error.isEmpty()) {
        m_logsError = task.error.message;
        emit logsChanged();
        return;
    }

    m_lastLogs = task.result.lines;
    m_logsTruncated = task.result.truncated;

    if (m_pendingComponent.isEmpty() || m_pendingComponent == QStringLiteral("helper") ||
        m_pendingComponent == QStringLiteral("package")) {
        HelperClient helper;
        QVariantMap reply;
        QString helperError;
        if (helper.callMap(QStringLiteral("GetLogs"), {}, &reply, &helperError, 60000)) {
            for (const QVariant &rawValue : reply.value(QStringLiteral("lines")).toList()) {
                const QString raw = rawValue.toString();
                const QJsonObject object = QJsonDocument::fromJson(raw.toUtf8()).object();
                const QString helperComponent =
                    object.value(QStringLiteral("jobId")).toString().isEmpty()
                        ? QStringLiteral("helper")
                        : QStringLiteral("package");
                if (!m_pendingComponent.isEmpty() && m_pendingComponent != helperComponent) {
                    continue;
                }
                LogLine line;
                line.ts = object.value(QStringLiteral("ts")).toString();
                line.level = object.value(QStringLiteral("level")).toString(QStringLiteral("info"));
                line.component = helperComponent;
                line.message = Redaction::text(object.value(QStringLiteral("message")).toString());
                line.raw = Redaction::text(raw);
                m_lastLogs.append(line);
            }
            if (reply.value(QStringLiteral("truncated")).toBool()) {
                m_logsTruncated = true;
            }
        }
    }

    m_logs.clear();
    for (const LogLine &line : m_lastLogs) {
        QVariantMap entry;
        entry.insert(QStringLiteral("ts"), line.ts);
        entry.insert(QStringLiteral("level"), line.level);
        entry.insert(QStringLiteral("component"), line.component);
        entry.insert(QStringLiteral("message"), line.message);
        m_logs.append(entry);
    }
    emit logsChanged();
}

bool SystemController::exportLogs(const QString &path)
{
    Error error;
    if (!PodStack::exportLogs(m_lastLogs, path, &error)) {
        m_logsError = error.message;
        emit logsChanged();
        return false;
    }
    return true;
}

void SystemController::clearLogs()
{
    Error error;
    if (!clearUserLogs(&error)) {
        m_logsError = error.message;
        emit logsChanged();
        return;
    }
    loadLogs();
}

void SystemController::generateDiagnosticReport()
{
    if (m_reportWatcher.isRunning()) {
        return;
    }
    m_reportWatcher.setFuture(QtConcurrent::run([]() {
        RpcClient client;
        Error error;
        QJsonObject result =
            client.call(QStringLiteral("system.report"),
                        QJsonObject{{QStringLiteral("includeLogs"), true}}, &error, 120000);
        if (!error.isEmpty()) {
            result.insert(QStringLiteral("__error"), error.message);
        }
        return result;
    }));
}

void SystemController::onReportFinished()
{
    const QJsonObject result = m_reportWatcher.result();
    if (result.contains(QStringLiteral("__error"))) {
        m_logsError = result.value(QStringLiteral("__error")).toString();
        emit logsChanged();
        return;
    }
    m_diagnosticReportPath = result.value(QStringLiteral("path")).toString();
    emit diagnosticReportChanged();
}

void SystemController::checkDiskHealth()
{
    if (m_diskRunning) {
        return;
    }
    m_diskRunning = true;
    m_diskWatcher.setFuture(QtConcurrent::run([] {
        RpcClient client;
        Error error;
        QJsonObject result = client.call(QStringLiteral("system.disk"), {}, &error);
        if (!error.isEmpty()) {
            result.insert(QStringLiteral("__error"), error.message);
        }
        return result;
    }));
}

void SystemController::onDiskFinished()
{
    m_diskRunning = false;
    const QJsonObject result = m_diskWatcher.result();
    if (result.contains(QStringLiteral("__error"))) {
        // Antes a falha de `system.disk` era descartada: o painel de disco
        // ficava vazio sem explicar nada.
        const QJsonValue value = result.value(QStringLiteral("__error"));
        const QString message = value.isObject()
                                    ? value.toObject().value(QStringLiteral("message")).toString()
                                    : value.toString();
        setLastError(message.isEmpty() ? QStringLiteral("Falha ao consultar o disco.") : message);
        return;
    }
    m_diskHealth = result.toVariantMap();
    emit diskHealthChanged();
    if (result.value(QStringLiteral("low")).toBool()) {
        const int threshold = result.value(QStringLiteral("thresholdPercent")).toInt();
        for (const QJsonValue &value : result.value(QStringLiteral("filesystems")).toArray()) {
            const QJsonObject filesystem = value.toObject();
            if (filesystem.value(QStringLiteral("usedPercent")).toDouble() >= threshold) {
                QVariantMap sample = filesystem.toVariantMap();
                sample.insert(QStringLiteral("thresholdPercent"), threshold);
                emit diskLow(sample);
                break;
            }
        }
    }
}

void SystemController::openLogsFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::stateDir() + QStringLiteral("/logs")));
}

void SystemController::startActivityMonitor()
{
    m_activityActive = true;
    m_activityCpuHistory.clear();
    m_activityMemHistory.clear();
    m_activityStream.subscribeSystemStats(2);
    emit activityChanged();
}

void SystemController::stopActivityMonitor()
{
    m_activityActive = false;
    m_activityStream.unsubscribe();
    emit activityChanged();
}

} // namespace PodStack
