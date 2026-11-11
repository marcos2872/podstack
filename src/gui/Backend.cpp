#include "gui/Backend.h"

#include "core/DiskUsage.h"
#include "core/Error.h"
#include "core/Proxy.h"
#include "core/RpcClient.h"
#include "core/SetupActions.h"
#include "core/SetupPolicy.h"
#include "gui/Autostart.h"
#include "gui/TerminalWindow.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>
#include <QTimer>
#include <QVariantMap>
#include <QtConcurrent>

namespace PodStack {
namespace {

QString humanBytes(qint64 bytes)
{
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return QString::number(value, 'f', unit == 0 ? 0 : 1) + QLatin1Char(' ')
           + QString::fromLatin1(units[unit]);
}

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

QJsonObject runBatchResource(const QString &prefix, const QString &idKey, const QString &action,
                             const QStringList &ids, bool force, const QJsonObject &extra)
{
    RpcClient client;
    int failed = 0;
    QJsonObject lastError;

    for (const QString &id : ids) {
        Error error;
        QJsonObject params = extra;
        params.insert(idKey, id);
        if (force) {
            params.insert(QStringLiteral("force"), true);
        }
        client.call(prefix + action, params, &error);
        if (!error.isEmpty()) {
            ++failed;
            lastError = error.toJson();
        }
    }

    QJsonObject result;
    result.insert(QStringLiteral("count"), ids.size());
    result.insert(QStringLiteral("failed"), failed);
    if (failed > 0) {
        result.insert(QStringLiteral("error"), lastError);
    } else {
        result.insert(QStringLiteral("ok"), true);
    }
    return result;
}

QJsonObject runBatchAction(const QString &action, const QStringList &ids)
{
    RpcClient client;
    int failed = 0;
    QJsonObject lastError;

    for (const QString &id : ids) {
        Error error;
        QJsonObject params;
        params.insert(QStringLiteral("id"), id);
        client.call(QStringLiteral("containers.") + action, params, &error);
        if (!error.isEmpty()) {
            ++failed;
            lastError = error.toJson();
        }
    }

    QJsonObject result;
    result.insert(QStringLiteral("count"), ids.size());
    result.insert(QStringLiteral("failed"), failed);
    if (failed > 0) {
        result.insert(QStringLiteral("error"), lastError);
    } else {
        result.insert(QStringLiteral("ok"), true);
    }
    return result;
}

QVariantMap machineVariant(const QJsonObject &machine)
{
    QVariantMap item = machine.toVariantMap();
    const QString distro = machine.value(QStringLiteral("distro")).toString();
    const QString version = machine.value(QStringLiteral("distroVersion")).toString();
    item.insert(QStringLiteral("distroLabel"),
                version.isEmpty() ? distro : distro + QLatin1Char(':') + version);
    const int sshPort = machine.value(QStringLiteral("sshPort")).toInt();
    QString sshUser = machine.value(QStringLiteral("sshUser")).toString();
    if (sshUser.isEmpty()) {
        sshUser = QStringLiteral("podstack");
    }
    item.insert(QStringLiteral("ssh"),
                sshPort > 0
                    ? QStringLiteral("%1@127.0.0.1:%2").arg(sshUser).arg(sshPort)
                    : QStringLiteral("-"));
    return item;
}

} // namespace

Backend::Backend(QObject *parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onRefreshFinished);
    connect(&m_actionWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onActionFinished);
    connect(&m_containerDetailWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onContainerDetailFinished);
    connect(&m_filesWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onFilesFinished);
    connect(&m_catalogWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onCatalogFinished);
    connect(&m_setupWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onSetupFinished);
    connect(&m_updateWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onUpdateFinished);
    connect(&m_diskWatcher, &QFutureWatcher<QJsonObject>::finished, this, &Backend::onDiskFinished);
    m_updateTimer.setInterval(6 * 60 * 60 * 1000);
    connect(&m_updateTimer, &QTimer::timeout, this, [this] { checkForUpdates(false); });
    m_updateTimer.start();
    QTimer::singleShot(30000, this, [this] { checkForUpdates(false); });
    m_diskTimer.setInterval(5 * 60 * 1000);
    connect(&m_diskTimer, &QTimer::timeout, this, &Backend::checkDiskHealth);
    m_diskTimer.start();
    QTimer::singleShot(15000, this, &Backend::checkDiskHealth);
    connect(&m_settingsWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onSettingsFinished);
    connect(&m_settingsActionWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onSettingsActionFinished);
    connect(&m_machinesWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onMachinesFinished);
    connect(&m_machineDistrosWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onMachineDistrosFinished);
    connect(&m_machineInfoWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onMachineInfoFinished);
    connect(&m_machineSerialWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onMachineSerialFinished);
    connect(&m_machineUsbWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onMachineUsbFinished);
    connect(&m_machineLogsWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onMachineLogsFinished);
    connect(&m_machineFilesWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onMachineFilesFinished);
    connect(&m_machineSnapshotsWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onMachineSnapshotsFinished);
    connect(&m_k8sStatusWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onK8sStatusFinished);
    connect(&m_k8sPodsWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onK8sPodsFinished);
    connect(&m_k8sServicesWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onK8sServicesFinished);
    connect(&m_k8sForwardWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onK8sForwardFinished);
    m_k8sTimer.setInterval(2000);
    connect(&m_k8sTimer, &QTimer::timeout, this, &Backend::refreshK8s);
    connect(&m_machineSshWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            [this] {
                const QJsonObject result = m_machineSshWatcher.result();
                if (result.contains(QStringLiteral("error"))) {
                    const QJsonObject errorObject =
                        result.value(QStringLiteral("error")).toObject();
                    m_lastError = errorObject.value(QStringLiteral("message")).toString();
                    emit lastErrorChanged();
                    return;
                }

                const QString host = result.value(QStringLiteral("host")).toString();
                const int port = result.value(QStringLiteral("port")).toInt();
                const QString user = result.value(QStringLiteral("user")).toString();
                const QString identityFile =
                    result.value(QStringLiteral("identityFile")).toString();
                const bool forwardAgent =
                    result.value(QStringLiteral("forwardAgent")).toBool();
                if (host.isEmpty() || port <= 0 || user.isEmpty()
                    || identityFile.isEmpty()) {
                    m_lastError = QStringLiteral("Resposta SSH inválida do daemon.");
                    emit lastErrorChanged();
                    return;
                }

                QString title = m_machineSshTitle;
                if (title.isEmpty()) {
                    title = QStringLiteral("machine");
                }
                QStringList arguments;
                if (forwardAgent) {
                    arguments << QStringLiteral("-A");
                }
                arguments << QStringLiteral("-i") << identityFile << QStringLiteral("-p")
                          << QString::number(port) << QStringLiteral("-o")
                          << QStringLiteral("IdentitiesOnly=yes") << QStringLiteral("-o")
                          << QStringLiteral("StrictHostKeyChecking=accept-new")
                          << QStringLiteral("%1@%2").arg(user, host);
                if (isSignalConnected(
                        QMetaMethod::fromSignal(&Backend::terminalRequested))) {
                    emit terminalRequested(title, QStringLiteral("ssh"), arguments);
                } else {
                    // Fallback para uso do Backend sem a MainWindow.
                    auto *window =
                        new TerminalWindow(title, QStringLiteral("ssh"), arguments);
                    window->show();
                    window->raise();
                    window->activateWindow();
                }
            });

    connect(&m_setupStream, &StreamClient::setupEvent, this,
            [this](const QJsonObject &event) { applySetupJob(event); });
    connect(&m_setupStream, &StreamClient::setupFinished, this,
            [this](const QJsonObject &event) {
                applySetupJob(event);
                m_setupJobActive = false;
                m_setupStatus = event.value(QStringLiteral("state")).toString();
                emit setupJobChanged();
                setupRefresh();
            });
    connect(&m_setupStream, &StreamClient::error, this, [this](const QString &message) {
        m_setupJobActive = false;
        m_setupError = message;
        m_setupBusy = false;
        emit setupJobChanged();
        emit setupChanged();
    });

    m_logsTimer.setSingleShot(true);
    m_logsTimer.setInterval(50);
    connect(&m_logsTimer, &QTimer::timeout, this, [this] {
        m_logsPending = false;
        emit logsChanged();
    });

    connect(&m_stream, &StreamClient::logLine, this, [this](const QString &line) {
        m_logLines.append(line);
        if (m_logLines.size() > 5000) {
            m_logLines = m_logLines.mid(1000);
        }
        if (!m_logsPending) {
            m_logsPending = true;
            m_logsTimer.start();
        }
    });

    connect(&m_stream, &StreamClient::stats, this, [this](const QJsonObject &stats) {
        m_stats = stats.toVariantMap();

        QString cpu = stats.value(QStringLiteral("cpu_percent")).toString();
        cpu.remove(QLatin1Char('%'));
        bool ok = false;
        const double value = cpu.toDouble(&ok);
        if (ok) {
            m_cpuHistory.append(value);
            if (m_cpuHistory.size() > 60) {
                m_cpuHistory.removeFirst();
            }
        }

        QString memory = stats.value(QStringLiteral("mem_percent")).toString();
        memory.remove(QLatin1Char('%'));
        ok = false;
        const double memoryValue = memory.toDouble(&ok);
        if (ok) {
            m_memHistory.append(memoryValue);
            if (m_memHistory.size() > 60) {
                m_memHistory.removeFirst();
            }
        }
        emit statsChanged();
    });

    connect(&m_stream, &StreamClient::error, this, [this](const QString &message) {
        m_lastError = message;
        emit lastErrorChanged();
    });

    m_detailLogsTimer.setSingleShot(true);
    m_detailLogsTimer.setInterval(50);
    connect(&m_detailLogsTimer, &QTimer::timeout, this, [this] {
        m_detailLogsPending = false;
        emit containerDetailLogsChanged();
    });

    connect(&m_detailStatsStream, &StreamClient::stats, this,
            [this](const QJsonObject &stats) {
                m_containerDetailStats = stats.toVariantMap();

                qint64 memUsed = 0;
                qint64 memLimit = 0;
                parseBytePair(stats.value(QStringLiteral("mem_usage")).toString(),
                              &memUsed, &memLimit);
                m_containerDetailStats.insert(QStringLiteral("memUsedBytes"), memUsed);
                m_containerDetailStats.insert(QStringLiteral("memLimitBytes"), memLimit);

                qint64 netRx = 0;
                qint64 netTx = 0;
                parseBytePair(stats.value(QStringLiteral("net_io")).toString(), &netRx,
                              &netTx);
                m_containerDetailStats.insert(QStringLiteral("netRxBytes"), netRx);
                m_containerDetailStats.insert(QStringLiteral("netTxBytes"), netTx);

                qint64 diskRead = 0;
                qint64 diskWrite = 0;
                parseBytePair(stats.value(QStringLiteral("block_io")).toString(), &diskRead,
                              &diskWrite);
                m_containerDetailStats.insert(QStringLiteral("diskReadBytes"), diskRead);
                m_containerDetailStats.insert(QStringLiteral("diskWriteBytes"), diskWrite);

                appendHistory(m_containerDetailCpuHistory,
                              parsePercent(stats.value(QStringLiteral("cpu_percent")).toString()));
                appendHistory(m_containerDetailMemHistory,
                              parsePercent(stats.value(QStringLiteral("mem_percent")).toString()));
                appendHistory(m_containerDetailNetRxHistory, static_cast<double>(netRx));
                appendHistory(m_containerDetailNetTxHistory, static_cast<double>(netTx));
                appendHistory(m_containerDetailDiskReadHistory, static_cast<double>(diskRead));
                appendHistory(m_containerDetailDiskWriteHistory, static_cast<double>(diskWrite));
                m_containerDetailStatsError.clear();
                emit containerDetailStatsChanged();
            });
    connect(&m_detailStatsStream, &StreamClient::error, this,
            [this](const QString &message) {
                m_containerDetailStatsError = message;
                emit containerDetailStatsChanged();
            });

    connect(&m_detailLogsStream, &StreamClient::logLine, this, [this](const QString &line) {
        m_containerDetailLogLines.append(line);
        if (m_containerDetailLogLines.size() > 5000) {
            m_containerDetailLogLines = m_containerDetailLogLines.mid(1000);
        }
        if (!m_detailLogsPending) {
            m_detailLogsPending = true;
            m_detailLogsTimer.start();
        }
    });
    connect(&m_detailLogsStream, &StreamClient::error, this,
            [this](const QString &message) {
                m_containerDetailLogsError = message;
                emit containerDetailLogsChanged();
            });

    connect(&m_activityStream, &StreamClient::systemStats, this,
            [this](const QJsonObject &sample) {
                m_activityStats = sample.toVariantMap();
                const QJsonObject host = sample.value(QStringLiteral("host")).toObject();
                m_activityCpuHistory.append(host.value(QStringLiteral("cpuPercent")).toDouble());
                if (m_activityCpuHistory.size() > 60) {
                    m_activityCpuHistory.removeFirst();
                }
                m_activityMemHistory.append(host.value(QStringLiteral("memPercent")).toDouble());
                if (m_activityMemHistory.size() > 60) {
                    m_activityMemHistory.removeFirst();
                }
                emit activityChanged();
            });
    connect(&m_activityStream, &StreamClient::error, this, [this](const QString &message) {
        m_activityActive = false;
        m_lastError = message;
        emit lastErrorChanged();
        emit activityChanged();
    });

    connect(&m_machineStatsStream, &StreamClient::machineStats, this,
            [this](const QJsonObject &sample) {
                m_machineStats = sample.toVariantMap();
                const auto append = [](QVariantList &list, double value) {
                    list.append(value);
                    if (list.size() > 60) {
                        list.removeFirst();
                    }
                };
                append(m_machineCpuHistory,
                       sample.value(QStringLiteral("cpuPercent")).toDouble());
                append(m_machineMemHistory,
                       sample.value(QStringLiteral("memPercent")).toDouble());
                append(m_machineNetRxHistory,
                       sample.value(QStringLiteral("netRx")).toDouble());
                append(m_machineNetTxHistory,
                       sample.value(QStringLiteral("netTx")).toDouble());
                m_machineStatsError.clear();
                emit machineStatsChanged();
            });
    connect(&m_machineStatsStream, &StreamClient::error, this,
            [this](const QString &message) {
                m_machineStatsError = message;
                emit machineStatsChanged();
            });

    connect(&m_machineConsoleStream, &StreamClient::machineConsoleEvent, this,
            [this](const QJsonObject &event) {
                const QString data = event.value(QStringLiteral("data")).toString();
                if (event.value(QStringLiteral("reset")).toBool()) {
                    m_machineConsoleText = data;
                } else {
                    m_machineConsoleText += data;
                }
                constexpr int kMaxConsoleChars = 512 * 1024;
                if (m_machineConsoleText.size() > kMaxConsoleChars) {
                    m_machineConsoleText.remove(0,
                                                m_machineConsoleText.size()
                                                    - kMaxConsoleChars);
                }
                m_machineConsoleActive = true;
                m_machineConsoleError.clear();
                emit machineConsoleChanged();
            });
    connect(&m_machineConsoleStream, &StreamClient::error, this,
            [this](const QString &message) {
                m_machineConsoleActive = false;
                m_machineConsoleError = message;
                emit machineConsoleChanged();
            });

    connect(&m_pullStream, &StreamClient::pullEvent, this,
            [this](const QJsonObject &event) {
                const QString status = event.value(QStringLiteral("status")).toString();
                const QString stream =
                    event.value(QStringLiteral("stream")).toString().trimmed();
                if (!status.isEmpty()) {
                    const QString progress =
                        event.value(QStringLiteral("progress")).toString();
                    m_pullStatus = progress.isEmpty() ? status : status + QStringLiteral(" ")
                                                                          + progress;
                } else if (!stream.isEmpty()) {
                    m_pullStatus = stream;
                }
                emit pullStateChanged();
            });

    connect(&m_pullStream, &StreamClient::pullFinished, this,
            [this](bool ok, const QString &error) {
                m_pullActive = false;
                m_pullStatus = ok ? QStringLiteral("Pull concluído") : error;
                emit pullStateChanged();
                if (ok) {
                    refreshImages();
                }
            });

    connect(&m_pullStream, &StreamClient::error, this, [this](const QString &message) {
        m_pullActive = false;
        m_pullStatus = message;
        emit pullStateChanged();
    });

    m_eventTimer.setSingleShot(true);
    m_eventTimer.setInterval(4000);
    connect(&m_eventTimer, &QTimer::timeout, this, [this] {
        m_eventMessage.clear();
        m_eventKind.clear();
        emit eventsChanged();
    });

    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(300);
    connect(&m_refreshTimer, &QTimer::timeout, this, &Backend::refresh);

    m_eventsRetryTimer.setSingleShot(true);
    connect(&m_eventsRetryTimer, &QTimer::timeout, this,
            [this] { m_eventsStream.subscribeEvents(); });
    connect(&m_eventsStream, &StreamClient::subscriptionReady, this,
            [this](const QJsonObject &) { m_eventsRetryDelayMs = 1000; });
    connect(&m_eventsStream, &StreamClient::error, this, [this](const QString &) {
        if (m_eventsRetryTimer.isActive()) {
            return;
        }
        m_eventsRetryTimer.start(m_eventsRetryDelayMs);
        m_eventsRetryDelayMs = qMin(m_eventsRetryDelayMs * 2, 30000);
    });

    connect(&m_composeStream, &StreamClient::composeLog, this,
            [this](const QString &service, const QString &line) {
                m_composeLogLines.append(service + QStringLiteral(" | ") + line);
                if (m_composeLogLines.size() > 5000) {
                    m_composeLogLines = m_composeLogLines.mid(1000);
                }
                emit composeLogsChanged();
            });

    connect(&m_composeStream, &StreamClient::error, this, [this](const QString &message) {
        m_lastError = message;
        emit lastErrorChanged();
    });

    connect(&m_buildStream, &StreamClient::subscriptionReady, this,
            [this](const QJsonObject &result) {
                const QString buildId = result.value(QStringLiteral("buildId")).toString();
                if (!buildId.isEmpty()) {
                    m_activeBuildId = buildId;
                    emit buildStateChanged();
                }
            });

    connect(&m_buildStream, &StreamClient::buildEvent, this,
            [this](const QJsonObject &event) {
                const QString line = event.value(QStringLiteral("line")).toString();
                if (!line.isEmpty()) {
                    m_buildLog.append(line);
                    if (m_buildLog.size() > 5000) {
                        m_buildLog = m_buildLog.mid(1000);
                    }
                    m_buildStatus = line;
                    emit buildLogChanged();
                }
            });

    connect(&m_buildStream, &StreamClient::buildFinished, this,
            [this](const QJsonObject &result) {
                const QString status = result.value(QStringLiteral("status")).toString();
                m_buildActive = false;
                m_buildStatus = status == QStringLiteral("success")
                                    ? QStringLiteral("Build concluído")
                                    : status == QStringLiteral("cancelled")
                                      ? QStringLiteral("Build cancelado")
                                      : result.value(QStringLiteral("error")).toString();
                if (m_buildStatus.isEmpty()) {
                    m_buildStatus = status;
                }
                emit buildStateChanged();
                refreshBuilds();
            });

    connect(&m_buildStream, &StreamClient::error, this, [this](const QString &message) {
        m_buildActive = false;
        m_buildStatus = message;
        emit buildStateChanged();
        emit lastErrorChanged();
    });

    connect(&m_machineCreateStream, &StreamClient::subscriptionReady, this,
            [this](const QJsonObject &result) {
                const QString jobId = result.value(QStringLiteral("jobId")).toString();
                if (!jobId.isEmpty()) {
                    m_machineCreateJobId = jobId;
                    emit machineCreateStateChanged();
                }
            });

    connect(&m_machineCreateStream, &StreamClient::machineCreateEvent, this,
            [this](const QJsonObject &event) {
                const QString jobId = event.value(QStringLiteral("jobId")).toString();
                if (!jobId.isEmpty()) {
                    m_machineCreateJobId = jobId;
                }
                const QString stage = event.value(QStringLiteral("stage")).toString();
                if (!stage.isEmpty()) {
                    m_machineCreateStage = stage;
                }
                m_machineCreateProgress =
                    qMax(m_machineCreateProgress,
                         qBound(0, event.value(QStringLiteral("progress")).toInt(), 100));
                emit machineCreateStateChanged();
            });

    connect(&m_machineCreateStream, &StreamClient::machineCreateFinished, this,
            [this](const QJsonObject &result) {
                const QJsonObject errorObject =
                    result.value(QStringLiteral("error")).toObject();
                const QString code = errorObject.value(QStringLiteral("code")).toString();
                m_machineCreateActive = false;
                m_machineCreateProgress = 100;
                m_machineCreateError.clear();
                if (!errorObject.isEmpty() && code != QStringLiteral("PODSTACK_CANCELLED")) {
                    m_machineCreateStage = QStringLiteral("error");
                    m_machineCreateError =
                        errorObject.value(QStringLiteral("message")).toString();
                    const QString remediation =
                        errorObject.value(QStringLiteral("remediation")).toString();
                    if (!remediation.isEmpty()) {
                        m_machineCreateError += QLatin1Char(' ') + remediation;
                    }
                } else if (code == QStringLiteral("PODSTACK_CANCELLED")) {
                    m_machineCreateStage = QStringLiteral("cancelled");
                } else {
                    m_machineCreateStage = QStringLiteral("done");
                }
                emit machineCreateStateChanged();
                refreshMachines();
                if (!m_machineCreateError.isEmpty()) {
                    m_lastError = m_machineCreateError;
                    emit lastErrorChanged();
                    emit actionFinished(false, m_machineCreateError);
                } else {
                    emit actionFinished(true, {});
                }
            });

    connect(&m_machineCreateStream, &StreamClient::error, this,
            [this](const QString &message) {
                if (!m_machineCreateActive) {
                    return;
                }
                m_machineCreateActive = false;
                m_machineCreateStage = QStringLiteral("error");
                m_machineCreateError = message;
                emit machineCreateStateChanged();
                m_lastError = message;
                emit lastErrorChanged();
                emit actionFinished(false, message);
            });

    connect(&m_k8sJobStream, &StreamClient::k8sJobEvent, this, [this](const QJsonObject &event) {
        m_k8sJobStage = event.value(QStringLiteral("stage")).toString();
        m_k8sJobProgress = event.value(QStringLiteral("progress")).toInt();
        emit k8sJobChanged();
    });
    connect(
        &m_k8sJobStream, &StreamClient::k8sJobFinished, this, [this](const QJsonObject &result) {
            m_k8sJobActive = false;
            const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
            if (!errorObject.isEmpty()) {
                m_k8sJobError = errorObject.value(QStringLiteral("message")).toString();
            } else {
                const QJsonObject cluster = result.value(QStringLiteral("cluster")).toObject();
                m_k8sState = cluster.value(QStringLiteral("state")).toString();
                m_k8sEnabled = !m_k8sState.isEmpty() && m_k8sState != QStringLiteral("disabled");
                m_k8sJobError.clear();
            }
            emit k8sJobChanged();
            emit k8sChanged();
            refreshK8sStatus();
            if (m_k8sEnabled) {
                refreshK8s();
            }
        });
    connect(&m_k8sJobStream, &StreamClient::error, this, [this](const QString &message) {
        m_k8sJobActive = false;
        m_k8sJobError = message;
        emit k8sJobChanged();
    });
    connect(&m_k8sLogsStream, &StreamClient::k8sLogLine, this, [this](const QString &line) {
        m_k8sLogLines.append(line);
        if (m_k8sLogLines.size() > 5000) {
            m_k8sLogLines = m_k8sLogLines.mid(1000);
        }
        emit k8sLogsChanged();
    });
    connect(&m_k8sLogsStream, &StreamClient::k8sLogFinished, this,
            [this](bool ok, const QString &error) {
                m_k8sLogActive = false;
                if (!ok) {
                    m_k8sLogError = error;
                }
                emit k8sLogsChanged();
            });
    connect(&m_k8sLogsStream, &StreamClient::error, this, [this](const QString &message) {
        m_k8sLogActive = false;
        m_k8sLogError = message;
        emit k8sLogsChanged();
    });
    connect(&m_eventsStream, &StreamClient::eventReceived, this,
            [this](const QJsonObject &event) {
                if (event.value(QStringLiteral("Type")).toString()
                    != QStringLiteral("container")) {
                    return;
                }
                QString action = event.value(QStringLiteral("Action")).toString();
                if (action == QStringLiteral("died")) {
                    action = QStringLiteral("die");
                } else if (action == QStringLiteral("stopped")) {
                    action = QStringLiteral("stop");
                }
                if (action != QStringLiteral("start") && action != QStringLiteral("stop")
                    && action != QStringLiteral("die") && action != QStringLiteral("create")
                    && action != QStringLiteral("remove")
                    && action != QStringLiteral("pause")
                    && action != QStringLiteral("unpause")
                    && action != QStringLiteral("health_status")
                    && action != QStringLiteral("oom")) {
                    return;
                }
                const QJsonObject actor = event.value(QStringLiteral("Actor")).toObject();
                const QJsonObject attributes =
                    actor.value(QStringLiteral("Attributes")).toObject();
                const QString id = actor.value(QStringLiteral("ID")).toString();
                const QString name = actor.value(QStringLiteral("Attributes"))
                                         .toObject()
                                         .value(QStringLiteral("name"))
                                         .toString();
                const QString key = id + QLatin1Char('|') + action + QLatin1Char('|')
                                    + QString::number(
                                        event.value(QStringLiteral("time")).toDouble());
                if (m_recentEvents.contains(key)) {
                    return;
                }
                if (m_recentEvents.size() > 500) {
                    m_recentEvents.clear();
                }
                m_recentEvents.insert(key);

                m_eventKind = action;
                QString detail = action;
                QString health = attributes.value(QStringLiteral("health_status")).toString();
                if (health.isEmpty()) {
                    health =
                        attributes.value(QStringLiteral("containerHealthStatus")).toString();
                }
                if (action == QStringLiteral("health_status")) {
                    detail = health.isEmpty() ? QStringLiteral("health") : health;
                }
                m_eventMessage = (name.isEmpty() ? id.left(12) : name)
                                 + QLatin1Char(' ') + detail;
                emit eventsChanged();
                m_eventTimer.start();
                m_refreshTimer.start();

                QVariantMap payload;
                payload.insert(QStringLiteral("action"), action);
                payload.insert(QStringLiteral("id"), id);
                payload.insert(QStringLiteral("name"), name);
                QString exitCode = attributes.value(QStringLiteral("containerExitCode")).toString();
                if (exitCode.isEmpty()) {
                    exitCode = attributes.value(QStringLiteral("exitCode")).toString();
                }
                if (exitCode.isEmpty()) {
                    exitCode =
                        event.value(QStringLiteral("ContainerExitCode")).toVariant().toString();
                }
                payload.insert(QStringLiteral("health"), health);
                payload.insert(QStringLiteral("exitCode"), exitCode);
                emit containerEvent(payload);
            });

    m_eventsStream.subscribeEvents();

    QTimer::singleShot(0, this, &Backend::refresh);
    QTimer::singleShot(0, this, &Backend::refreshCatalog);
    QTimer::singleShot(0, this, &Backend::refreshMachines);
    QTimer::singleShot(0, this, &Backend::refreshSettings);
    QTimer::singleShot(0, this, &Backend::refreshK8sStatus);
}

QString Backend::version() const
{
    return QStringLiteral(PODSTACK_VERSION);
}

void Backend::refresh()
{
    if (m_watcher.isRunning()) {
        m_refreshAgain = true;
        return;
    }

    m_watcher.setFuture(QtConcurrent::run([] {
        RpcClient client;
        Error error;
        QJsonObject params;
        params.insert(QStringLiteral("all"), true);

        QJsonObject result = client.call(QStringLiteral("containers.list"), params, &error);
        if (!error.isEmpty()) {
            QJsonObject payload;
            payload.insert(QStringLiteral("error"), error.toJson());
            return payload;
        }
        return result;
    }));
}

void Backend::refreshCatalog()
{
    refreshImages();
    refreshVolumes();
    refreshNetworks();
    refreshPods();
    refreshSection(QStringLiteral("df"));
}

void Backend::refreshDiskUsage()
{
    refreshSection(QStringLiteral("df"));
}

void Backend::refreshImages()
{
    refreshSection(QStringLiteral("images"));
}

void Backend::refreshVolumes()
{
    refreshSection(QStringLiteral("volumes"));
}

void Backend::refreshNetworks()
{
    refreshSection(QStringLiteral("networks"));
}

void Backend::refreshPods()
{
    refreshSection(QStringLiteral("pods"));
}

void Backend::refreshK8sStatus()
{
    if (m_k8sStatusWatcher.isRunning()) {
        return;
    }
    m_k8sStatusWatcher.setFuture(QtConcurrent::run([] {
        RpcClient client;
        Error error;
        QJsonObject result = client.call(QStringLiteral("k8s.status"), QJsonObject{}, &error);
        if (!error.isEmpty()) {
            result.insert(QStringLiteral("error"), error.toJson());
        }
        return result;
    }));
}

void Backend::refreshK8s()
{
    if (!m_k8sEnabled) {
        refreshK8sStatus();
        return;
    }
    if (m_k8sPodsWatcher.isRunning() || m_k8sServicesWatcher.isRunning()) {
        return;
    }
    m_k8sLoading = true;
    emit k8sChanged();

    m_k8sPodsWatcher.setFuture(QtConcurrent::run([] {
        RpcClient client;
        Error error;
        QJsonObject result = client.call(QStringLiteral("k8s.pods.list"), QJsonObject{}, &error);
        if (!error.isEmpty()) {
            result.insert(QStringLiteral("error"), error.toJson());
        }
        return result;
    }));
    m_k8sServicesWatcher.setFuture(QtConcurrent::run([] {
        RpcClient client;
        Error error;
        QJsonObject result =
            client.call(QStringLiteral("k8s.services.list"), QJsonObject{}, &error);
        if (!error.isEmpty()) {
            result.insert(QStringLiteral("error"), error.toJson());
        }
        return result;
    }));
}

void Backend::startK8sWatch()
{
    if (!m_k8sEnabled) {
        refreshK8sStatus();
        return;
    }
    if (!m_k8sTimer.isActive()) {
        m_k8sTimer.start();
    }
    refreshK8s();
}

void Backend::stopK8sWatch()
{
    m_k8sTimer.stop();
}

void Backend::onK8sStatusFinished()
{
    const QJsonObject result = m_k8sStatusWatcher.result();
    if (result.value(QStringLiteral("error")).isObject()) {
        m_k8sError = result.value(QStringLiteral("error"))
                         .toObject()
                         .value(QStringLiteral("message"))
                         .toString();
    } else {
        m_k8sState = result.value(QStringLiteral("state")).toString();
        m_k8sEnabled = !m_k8sState.isEmpty() && m_k8sState != QStringLiteral("disabled");
        m_k8sError.clear();
    }
    emit k8sChanged();
    if (m_k8sEnabled && m_k8sTimer.isActive()) {
        refreshK8s();
    }
}

void Backend::onK8sPodsFinished()
{
    const QJsonObject result = m_k8sPodsWatcher.result();
    if (result.contains(QStringLiteral("error"))) {
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        m_k8sError = errorObject.value(QStringLiteral("message")).toString();
        if (errorObject.value(QStringLiteral("code")).toString() ==
            QStringLiteral("PODSTACK_NOT_FOUND")) {
            m_k8sState = QStringLiteral("disabled");
            m_k8sEnabled = false;
            m_k8sTimer.stop();
        }
    } else {
        m_k8sPods = result.value(QStringLiteral("items")).toArray().toVariantList();
        if (!m_k8sServicesWatcher.isRunning()) {
            m_k8sError.clear();
        }
    }
    m_k8sLoading = m_k8sPodsWatcher.isRunning() || m_k8sServicesWatcher.isRunning();
    emit k8sChanged();
}

void Backend::onK8sServicesFinished()
{
    const QJsonObject result = m_k8sServicesWatcher.result();
    if (result.contains(QStringLiteral("error"))) {
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        m_k8sError = errorObject.value(QStringLiteral("message")).toString();
        if (errorObject.value(QStringLiteral("code")).toString() ==
            QStringLiteral("PODSTACK_NOT_FOUND")) {
            m_k8sState = QStringLiteral("disabled");
            m_k8sEnabled = false;
            m_k8sTimer.stop();
        }
    } else {
        m_k8sServices = result.value(QStringLiteral("items")).toArray().toVariantList();
        if (!m_k8sPodsWatcher.isRunning()) {
            m_k8sError.clear();
        }
    }
    m_k8sLoading = m_k8sPodsWatcher.isRunning() || m_k8sServicesWatcher.isRunning();
    emit k8sChanged();
}

void Backend::startK8sLogs(const QString &namespaceName, const QString &pod,
                           const QString &container)
{
    m_k8sLogLines.clear();
    m_k8sLogError.clear();
    m_k8sLogActive = true;
    emit k8sLogsChanged();
    m_k8sLogsStream.subscribeK8sLogs(namespaceName, pod, container);
}

void Backend::stopK8sLogs()
{
    m_k8sLogsStream.unsubscribe();
    m_k8sLogActive = false;
    emit k8sLogsChanged();
}

void Backend::clearK8sLogs()
{
    m_k8sLogLines.clear();
    emit k8sLogsChanged();
}

void Backend::deleteK8sPod(const QString &namespaceName, const QString &name)
{
    runAction(
        QStringLiteral("k8s.pod.delete"),
        QJsonObject{{QStringLiteral("namespace"), namespaceName}, {QStringLiteral("name"), name}});
}

void Backend::deleteK8sService(const QString &namespaceName, const QString &name)
{
    runAction(
        QStringLiteral("k8s.service.delete"),
        QJsonObject{{QStringLiteral("namespace"), namespaceName}, {QStringLiteral("name"), name}});
}

void Backend::startK8sPortForward(const QString &namespaceName, const QString &name, int localPort,
                                  int remotePort, const QString &resource)
{
    if (m_k8sForwardWatcher.isRunning()) {
        return;
    }
    m_k8sPortForwardError.clear();
    m_k8sForwardWatcher.setFuture(QtConcurrent::run([namespaceName, name, localPort, remotePort,
                                                     resource] {
        RpcClient client;
        Error error;
        QJsonObject result = client.call(QStringLiteral("k8s.portforward.start"),
                                         QJsonObject{{QStringLiteral("resource"), resource},
                                                     {QStringLiteral("namespace"), namespaceName},
                                                     {QStringLiteral("pod"), name},
                                                     {QStringLiteral("localPort"), localPort},
                                                     {QStringLiteral("remotePort"), remotePort}},
                                         &error);
        if (!error.isEmpty()) {
            result.insert(QStringLiteral("error"), error.toJson());
        }
        return result;
    }));
}

void Backend::stopK8sPortForward()
{
    const QString id = m_k8sPortForwardId;
    m_k8sPortForwardActive = false;
    m_k8sPortForwardId.clear();
    emit k8sForwardChanged();
    if (id.isEmpty()) {
        return;
    }
    (void) QtConcurrent::run([id] {
        RpcClient client;
        Error error;
        client.call(QStringLiteral("k8s.portforward.stop"), QJsonObject{{QStringLiteral("id"), id}},
                    &error);
    });
}

void Backend::onK8sForwardFinished()
{
    const QJsonObject result = m_k8sForwardWatcher.result();
    if (result.value(QStringLiteral("error")).isObject()) {
        m_k8sPortForwardError = result.value(QStringLiteral("error"))
                                    .toObject()
                                    .value(QStringLiteral("message"))
                                    .toString();
        m_k8sPortForwardActive = false;
        m_k8sPortForwardId.clear();
    } else {
        m_k8sPortForwardId = result.value(QStringLiteral("id")).toString();
        m_k8sPortForwardActive = !m_k8sPortForwardId.isEmpty();
        m_k8sPortForwardError.clear();
    }
    emit k8sForwardChanged();
}

void Backend::enableK8s()
{
    m_k8sJobActive = true;
    m_k8sJobStage = QStringLiteral("starting");
    m_k8sJobProgress = 0;
    m_k8sJobError.clear();
    emit k8sJobChanged();
    m_k8sJobStream.startK8sJob(QStringLiteral("k8s.enable"), QJsonObject{});
}

void Backend::disableK8s(bool force)
{
    m_k8sJobActive = true;
    m_k8sJobStage = QStringLiteral("disabling");
    m_k8sJobProgress = 0;
    m_k8sJobError.clear();
    emit k8sJobChanged();
    m_k8sJobStream.startK8sJob(QStringLiteral("k8s.disable"),
                               QJsonObject{{QStringLiteral("force"), force}});
}

void Backend::resetK8s()
{
    m_k8sJobActive = true;
    m_k8sJobStage = QStringLiteral("resetting");
    m_k8sJobProgress = 0;
    m_k8sJobError.clear();
    emit k8sJobChanged();
    m_k8sJobStream.startK8sJob(QStringLiteral("k8s.reset"), QJsonObject{});
}

void Backend::refreshSection(const QString &section)
{
    if (!m_catalogQueue.contains(section)) {
        m_catalogQueue.append(section);
    }
    startNextCatalog();
}

void Backend::startNextCatalog()
{
    if (m_catalogWatcher.isRunning() || m_catalogQueue.isEmpty()) {
        return;
    }

    m_catalogSection = m_catalogQueue.takeFirst();

    QString method;
    if (m_catalogSection == QStringLiteral("images")) {
        method = QStringLiteral("images.list");
    } else if (m_catalogSection == QStringLiteral("volumes")) {
        method = QStringLiteral("volumes.list");
    } else if (m_catalogSection == QStringLiteral("networks")) {
        method = QStringLiteral("networks.list");
    } else if (m_catalogSection == QStringLiteral("pods")) {
        method = QStringLiteral("pods.list");
    } else if (m_catalogSection == QStringLiteral("df")) {
        method = QStringLiteral("system.df");
    } else if (m_catalogSection == QStringLiteral("compose")) {
        method = QStringLiteral("compose.projects.list");
    } else if (m_catalogSection == QStringLiteral("builds")) {
        method = QStringLiteral("builds.list");
    } else {
        startNextCatalog();
        return;
    }

    QJsonObject params;
    if (m_catalogSection == QStringLiteral("df")) {
        params.insert(QStringLiteral("detailed"), true);
    }

    m_catalogWatcher.setFuture(QtConcurrent::run([method, params] {
        RpcClient client;
        Error error;
        const QJsonObject result = client.call(method, params, &error);
        if (!error.isEmpty()) {
            QJsonObject payload;
            payload.insert(QStringLiteral("error"), error.toJson());
            return payload;
        }
        return result;
    }));
}

void Backend::onCatalogFinished()
{
    const QJsonObject result = m_catalogWatcher.result();
    if (result.contains(QStringLiteral("error"))) {
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        m_lastError = errorObject.value(QStringLiteral("message")).toString();
        emit lastErrorChanged();
        if (m_catalogSection == QStringLiteral("df")) {
            m_prunePreview.clear();
            emit prunePreviewChanged();
        }
        startNextCatalog();
        return;
    }

    const QJsonArray items = result.value(QStringLiteral("items")).toArray();

    if (m_catalogSection == QStringLiteral("images")) {
        m_images.clear();
        for (const QJsonValue &value : items) {
            const QJsonObject image = value.toObject();
            QVariantMap item;
            const QJsonArray tags = image.value(QStringLiteral("repoTags")).toArray();
            item.insert(QStringLiteral("id"),
                        image.value(QStringLiteral("id")).toString());
            item.insert(QStringLiteral("name"),
                        tags.isEmpty() ? QStringLiteral("<none>:<none>")
                                       : tags.first().toString());
            item.insert(QStringLiteral("digest"),
                        image.value(QStringLiteral("digest")).toString());
            item.insert(QStringLiteral("size"),
                        static_cast<double>(
                            image.value(QStringLiteral("size")).toDouble()));
            item.insert(QStringLiteral("created"),
                        static_cast<double>(
                            image.value(QStringLiteral("created")).toDouble()));
            item.insert(QStringLiteral("containers"),
                        image.value(QStringLiteral("containers")).toInt());
            item.insert(QStringLiteral("dangling"),
                        image.value(QStringLiteral("dangling")).toBool());
            m_images.append(item);
        }
        emit imagesChanged();
    } else if (m_catalogSection == QStringLiteral("volumes")) {
        m_volumes.clear();
        for (const QJsonValue &value : items) {
            const QJsonObject volume = value.toObject();
            QVariantMap item;
            item.insert(QStringLiteral("name"),
                        volume.value(QStringLiteral("name")).toString());
            item.insert(QStringLiteral("driver"),
                        volume.value(QStringLiteral("driver")).toString());
            item.insert(QStringLiteral("mountpoint"),
                        volume.value(QStringLiteral("mountpoint")).toString());
            item.insert(QStringLiteral("size"),
                        static_cast<double>(
                            volume.value(QStringLiteral("size")).toDouble()));
            item.insert(QStringLiteral("links"),
                        volume.value(QStringLiteral("links")).toInt());
            m_volumes.append(item);
        }
        emit volumesChanged();
    } else if (m_catalogSection == QStringLiteral("networks")) {
        m_networks.clear();
        for (const QJsonValue &value : items) {
            const QJsonObject network = value.toObject();
            QVariantMap item;
            item.insert(QStringLiteral("name"),
                        network.value(QStringLiteral("name")).toString());
            item.insert(QStringLiteral("driver"),
                        network.value(QStringLiteral("driver")).toString());
            item.insert(QStringLiteral("subnet"),
                        network.value(QStringLiteral("subnet")).toString());
            item.insert(QStringLiteral("gateway"),
                        network.value(QStringLiteral("gateway")).toString());
            item.insert(QStringLiteral("containers"),
                        network.value(QStringLiteral("containers")).toInt());
            m_networks.append(item);
        }
        emit networksChanged();
    } else if (m_catalogSection == QStringLiteral("df")) {
        const QJsonObject df = result.value(QStringLiteral("json")).toObject();
        qint64 volumesSize = 0;
        for (const QJsonValue &value : df.value(QStringLiteral("Volumes")).toArray()) {
            volumesSize += static_cast<qint64>(
                value.toObject().value(QStringLiteral("Size")).toDouble());
        }
        qint64 containersSize = 0;
        for (const QJsonValue &value : df.value(QStringLiteral("Containers")).toArray()) {
            containersSize += static_cast<qint64>(
                value.toObject().value(QStringLiteral("Size")).toDouble());
        }
        m_diskUsage.clear();
        m_diskUsage.insert(QStringLiteral("images"),
                           humanBytes(static_cast<qint64>(
                               df.value(QStringLiteral("ImagesSize")).toDouble())));
        m_diskUsage.insert(QStringLiteral("volumes"), humanBytes(volumesSize));
        m_diskUsage.insert(QStringLiteral("containers"), humanBytes(containersSize));
        emit diskUsageChanged();

        QJsonObject sections = result.value(QStringLiteral("sections")).toObject();
        double totalReclaimable =
            result.value(QStringLiteral("totalReclaimableBytes")).toDouble();
        if (sections.isEmpty()) {
            const QJsonObject normalized = normalizeDiskUsage(df, true);
            sections = normalized.value(QStringLiteral("sections")).toObject();
            totalReclaimable =
                normalized.value(QStringLiteral("totalReclaimableBytes")).toDouble();
        }
        m_prunePreview = sections.toVariantMap();
        m_prunePreview.insert(QStringLiteral("totalReclaimableBytes"), totalReclaimable);
        emit prunePreviewChanged();
    } else if (m_catalogSection == QStringLiteral("compose")) {
        m_composeProjects.clear();
        for (const QJsonValue &value : items) {
            const QJsonObject project = value.toObject();
            QVariantMap item;
            item.insert(QStringLiteral("name"),
                        project.value(QStringLiteral("name")).toString());
            item.insert(QStringLiteral("status"),
                        project.value(QStringLiteral("status")).toString());
            item.insert(QStringLiteral("workingDir"),
                        project.value(QStringLiteral("workingDir")).toString());
            const QJsonArray configFiles =
                project.value(QStringLiteral("configFiles")).toArray();
            item.insert(QStringLiteral("configFile"),
                        configFiles.isEmpty()
                            ? QString()
                            : configFiles.first().toString());
            QStringList services;
            for (const QJsonValue &service :
                 project.value(QStringLiteral("services")).toArray()) {
                const QJsonObject serviceObject = service.toObject();
                services.append(serviceObject.value(QStringLiteral("name")).toString());
            }
            item.insert(QStringLiteral("services"), services.join(QStringLiteral(", ")));
            m_composeProjects.append(item);
        }
        emit composeProjectsChanged();
    } else if (m_catalogSection == QStringLiteral("builds")) {
        m_builds.clear();
        for (const QJsonValue &value : items) {
            const QJsonObject build = value.toObject();
            QVariantMap item;
            item.insert(QStringLiteral("id"),
                        build.value(QStringLiteral("id")).toString());
            item.insert(QStringLiteral("imageRef"),
                        build.value(QStringLiteral("imageRef")).toString());
            item.insert(QStringLiteral("status"),
                        build.value(QStringLiteral("status")).toString());
            const int durationMs = build.value(QStringLiteral("durationMs")).toInt();
            item.insert(QStringLiteral("durationMs"), durationMs);
            item.insert(QStringLiteral("duration"),
                        QString::number(durationMs / 1000.0, 'f', 1)
                            + QStringLiteral("s"));
            item.insert(QStringLiteral("startedAt"),
                        build.value(QStringLiteral("startedAt")).toString());
            item.insert(QStringLiteral("trigger"),
                        build.value(QStringLiteral("trigger")).toString());
            item.insert(QStringLiteral("contextPath"),
                        build.value(QStringLiteral("contextPath")).toString());
            QStringList platforms;
            for (const QJsonValue &platform :
                 build.value(QStringLiteral("platforms")).toArray()) {
                const QString value = platform.toString();
                if (!value.isEmpty()) {
                    platforms.append(value);
                }
            }
            item.insert(QStringLiteral("platforms"),
                        platforms.join(QStringLiteral(", ")));
            m_builds.append(item);
        }
        emit buildsChanged();
    } else if (m_catalogSection == QStringLiteral("pods")) {
        m_pods.clear();
        for (const QJsonValue &value : items) {
            const QJsonObject pod = value.toObject();
            QVariantMap item;
            item.insert(QStringLiteral("id"),
                        pod.value(QStringLiteral("id")).toString());
            item.insert(QStringLiteral("name"),
                        pod.value(QStringLiteral("name")).toString());
            item.insert(QStringLiteral("status"),
                        pod.value(QStringLiteral("status")).toString());
            item.insert(QStringLiteral("containers"),
                        pod.value(QStringLiteral("containers")).toInt());
            item.insert(QStringLiteral("created"),
                        pod.value(QStringLiteral("created")).toString());
            m_pods.append(item);
        }
        emit podsChanged();
    }

    startNextCatalog();
}

void Backend::pullImage(const QString &reference, const QString &platform)
{
    if (reference.isEmpty()) {
        m_lastError = QStringLiteral("Informe a imagem para o pull.");
        emit lastErrorChanged();
        return;
    }

    m_pullActive = true;
    m_pullStatus = QStringLiteral("Iniciando pull de %1...").arg(reference);
    emit pullStateChanged();
    m_pullStream.pullImage(reference, platform);
}

void Backend::cancelPull()
{
    m_pullStream.unsubscribe();
    m_pullActive = false;
    m_pullStatus = QStringLiteral("Pull cancelado");
    emit pullStateChanged();
}

void Backend::removeImage(const QString &id, bool force)
{
    runAction(QStringLiteral("images.remove"),
              QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("force"), force}});
}

void Backend::pruneImages(bool all)
{
    runAction(QStringLiteral("images.prune"), QJsonObject{{QStringLiteral("all"), all}});
}

void Backend::tagImage(const QString &id, const QString &repoTag)
{
    QString repo = repoTag;
    QString tag = QStringLiteral("latest");
    const int slash = repo.lastIndexOf(QLatin1Char('/'));
    const int colon = repo.lastIndexOf(QLatin1Char(':'));
    if (colon > slash) {
        tag = repo.mid(colon + 1);
        repo = repo.left(colon);
    }

    runAction(QStringLiteral("images.tag"),
              QJsonObject{{QStringLiteral("id"), id},
                          {QStringLiteral("repo"), repo},
                          {QStringLiteral("tag"), tag}});
}

void Backend::untagImage(const QString &id, const QString &repoTag)
{
    QString repo = repoTag;
    QString tag = QStringLiteral("latest");
    const int slash = repo.lastIndexOf(QLatin1Char('/'));
    const int colon = repo.lastIndexOf(QLatin1Char(':'));
    if (colon > slash) {
        tag = repo.mid(colon + 1);
        repo = repo.left(colon);
    }

    runAction(QStringLiteral("images.untag"),
              QJsonObject{{QStringLiteral("id"), id},
                          {QStringLiteral("repo"), repo},
                          {QStringLiteral("tag"), tag}});
}

void Backend::exportImage(const QString &id, const QString &path)
{
    runAction(QStringLiteral("images.export"),
              QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("path"), path}});
}

void Backend::loadImage(const QString &path)
{
    runAction(QStringLiteral("images.load"), QJsonObject{{QStringLiteral("path"), path}});
}

void Backend::createVolume(const QString &name, const QString &driver)
{
    runAction(QStringLiteral("volumes.create"),
              QJsonObject{{QStringLiteral("name"), name},
                          {QStringLiteral("driver"), driver}});
}

void Backend::removeVolume(const QString &name, bool force)
{
    runAction(QStringLiteral("volumes.remove"),
              QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("force"), force}});
}

void Backend::pruneVolumes()
{
    runAction(QStringLiteral("volumes.prune"), {});
}

void Backend::cloneVolume(const QString &source, const QString &destination)
{
    runAction(QStringLiteral("volumes.clone"),
              QJsonObject{{QStringLiteral("source"), source},
                          {QStringLiteral("destination"), destination}});
}

void Backend::importHostPath(const QString &name, const QString &sourcePath)
{
    runAction(QStringLiteral("volumes.importHostPath"),
              QJsonObject{{QStringLiteral("name"), name},
                          {QStringLiteral("sourcePath"), sourcePath}});
}

void Backend::createNetwork(const QString &name, const QString &subnet,
                            const QString &gateway)
{
    runAction(QStringLiteral("networks.create"),
              QJsonObject{{QStringLiteral("name"), name},
                          {QStringLiteral("subnet"), subnet},
                          {QStringLiteral("gateway"), gateway}});
}

void Backend::removeNetwork(const QString &name, bool force)
{
    runAction(QStringLiteral("networks.remove"),
              QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("force"), force}});
}

void Backend::connectNetwork(const QString &name, const QString &containerId)
{
    runAction(QStringLiteral("networks.connect"),
              QJsonObject{{QStringLiteral("name"), name},
                          {QStringLiteral("containerId"), containerId}});
}

void Backend::disconnectNetwork(const QString &name, const QString &containerId)
{
    runAction(QStringLiteral("networks.disconnect"),
              QJsonObject{{QStringLiteral("name"), name},
                          {QStringLiteral("containerId"), containerId}});
}

void Backend::createPod(const QString &name)
{
    runAction(QStringLiteral("pods.create"), QJsonObject{{QStringLiteral("name"), name}});
}

void Backend::podAction(const QString &action, const QString &id)
{
    runAction(QStringLiteral("pods.") + action, QJsonObject{{QStringLiteral("id"), id}});
}

void Backend::removePod(const QString &id, bool force)
{
    runAction(QStringLiteral("pods.remove"),
              QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("force"), force}});
}

void Backend::onRefreshFinished()
{
    const QJsonObject result = m_watcher.result();

    if (result.contains(QStringLiteral("error"))) {
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        const QString message = errorObject.value(QStringLiteral("message")).toString();
        if (m_connected || m_error != message) {
            m_connected = false;
            m_error = message;
            emit stateChanged();
        }
        m_containers.clear();
        emit containersChanged({});
        if (m_refreshAgain) {
            m_refreshAgain = false;
            QTimer::singleShot(0, this, &Backend::refresh);
        }
        return;
    }

    if (!m_connected || !m_error.isEmpty()) {
        m_connected = true;
        m_error.clear();
        emit stateChanged();
    }

    QVariantList items;
    const QJsonArray array = result.value(QStringLiteral("items")).toArray();
    items.reserve(array.size());

    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        QVariantMap item;
        item.insert(QStringLiteral("id"), object.value(QStringLiteral("id")).toString());
        item.insert(QStringLiteral("name"), object.value(QStringLiteral("name")).toString());
        item.insert(QStringLiteral("state"), object.value(QStringLiteral("state")).toString());
        item.insert(QStringLiteral("image"), object.value(QStringLiteral("image")).toString());
        item.insert(QStringLiteral("health"), object.value(QStringLiteral("health")).toString());
        item.insert(QStringLiteral("status"), object.value(QStringLiteral("status")).toString());

        QVariantMap labels;
        const QJsonObject labelObject = object.value(QStringLiteral("labels")).toObject();
        for (auto it = labelObject.constBegin(); it != labelObject.constEnd(); ++it) {
            labels.insert(it.key(), it.value().toString());
        }
        item.insert(QStringLiteral("labels"), labels);
        item.insert(QStringLiteral("project"),
                    labels.value(QStringLiteral("com.docker.compose.project")).toString());
        item.insert(QStringLiteral("service"),
                    labels.value(QStringLiteral("com.docker.compose.service")).toString());

        QStringList ports;
        const QJsonArray portArray = object.value(QStringLiteral("ports")).toArray();
        for (const QJsonValue &port : portArray) {
            ports.append(port.toString());
        }
        item.insert(QStringLiteral("ports"), ports.join(QStringLiteral(", ")));
        items.append(item);
    }

    m_containers = items;
    emit containersChanged(items);
    if (m_refreshAgain) {
        m_refreshAgain = false;
        QTimer::singleShot(0, this, &Backend::refresh);
    }
}

void Backend::lifecycle(const QString &action, const QString &id)
{
    if (action.isEmpty() || id.isEmpty()) {
        m_lastError = QStringLiteral("Ação ou container inválido.");
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
        return;
    }

    QJsonObject params;
    params.insert(QStringLiteral("id"), id);
    runAction(QStringLiteral("containers.") + action, params);
}

void Backend::removeContainer(const QString &id, bool force, bool volumes)
{
    if (id.isEmpty()) {
        m_lastError = QStringLiteral("Container inválido.");
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
        return;
    }

    QJsonObject params;
    params.insert(QStringLiteral("id"), id);
    params.insert(QStringLiteral("force"), force);
    params.insert(QStringLiteral("volumes"), volumes);
    runAction(QStringLiteral("containers.remove"), params);
}

void Backend::loadContainerDetail(const QString &id)
{
    if (id.isEmpty()) {
        m_containerDetail.clear();
        m_containerDetailJson = {};
        m_containerDetailError = QStringLiteral("Container inválido.");
        m_containerDetailLoading = false;
        emit containerDetailChanged();
        return;
    }

    if (m_containerDetailWatcher.isRunning()) {
        m_pendingContainerDetailId = id;
        return;
    }

    m_containerDetail.clear();
    m_containerDetailJson = {};
    m_containerDetailError.clear();
    m_containerDetailLoading = true;
    emit containerDetailChanged();

    m_containerDetailWatcher.setFuture(QtConcurrent::run([id] {
        RpcClient client;
        Error error;
        const QJsonObject result =
            client.call(QStringLiteral("containers.inspect"),
                        QJsonObject{{QStringLiteral("id"), id}}, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("error"), error.toJson()}};
        }
        return result;
    }));
}

void Backend::onContainerDetailFinished()
{
    const QJsonObject result = m_containerDetailWatcher.result();
    m_containerDetailLoading = false;

    if (result.value(QStringLiteral("error")).isObject()) {
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        m_containerDetailError = errorObject.value(QStringLiteral("message")).toString();
        if (m_containerDetailError.isEmpty()) {
            m_containerDetailError = QStringLiteral("Falha ao carregar o inspect.");
        }
    } else {
        const QJsonObject json = result.value(QStringLiteral("json")).toObject();
        if (json.isEmpty()) {
            m_containerDetailError = QStringLiteral("Inspect vazio ou inválido.");
        } else {
            m_containerDetailJson = json;
            m_containerDetail = json.toVariantMap();
            m_containerDetailError.clear();
        }
    }
    emit containerDetailChanged();

    if (!m_pendingContainerDetailId.isEmpty()) {
        const QString next = m_pendingContainerDetailId;
        m_pendingContainerDetailId.clear();
        loadContainerDetail(next);
    }
}

void Backend::copyContainerInspect()
{
    if (m_containerDetailJson.isEmpty()) {
        return;
    }
    // Sem QGuiApplication (ex.: suíte guiless) o clipboard não existe.
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        return;
    }
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(QJsonDocument(m_containerDetailJson).toJson(QJsonDocument::Indented));
    }
    emit actionFinished(true, {});
}

void Backend::startContainerDetailStats(const QString &id)
{
    m_containerDetailStats.clear();
    m_containerDetailCpuHistory.clear();
    m_containerDetailMemHistory.clear();
    m_containerDetailNetRxHistory.clear();
    m_containerDetailNetTxHistory.clear();
    m_containerDetailDiskReadHistory.clear();
    m_containerDetailDiskWriteHistory.clear();
    m_containerDetailStatsError.clear();
    emit containerDetailStatsChanged();

    if (id.isEmpty()) {
        m_containerDetailStatsError = QStringLiteral("Container inválido.");
        emit containerDetailStatsChanged();
        return;
    }

    m_detailStatsStream.subscribeStats(id);
}

void Backend::stopContainerDetailStats()
{
    m_detailStatsStream.unsubscribe();
}

void Backend::startContainerDetailLogs(const QString &id, int tail, bool follow)
{
    m_detailLogsTimer.stop();
    m_containerDetailLogLines.clear();
    m_containerDetailLogsError.clear();
    m_detailLogsPending = false;
    emit containerDetailLogsChanged();

    if (id.isEmpty()) {
        m_containerDetailLogsError = QStringLiteral("Container inválido.");
        emit containerDetailLogsChanged();
        return;
    }

    if (tail <= 0) {
        tail = 200;
    }
    tail = qBound(1, tail, 2000);
    m_detailLogsStream.subscribeLogs(id, tail, follow);
}

void Backend::stopContainerDetailLogs()
{
    m_detailLogsTimer.stop();
    m_detailLogsPending = false;
    m_detailLogsStream.unsubscribe();
}

void Backend::clearContainerDetailLogs()
{
    m_detailLogsTimer.stop();
    m_containerDetailLogLines.clear();
    m_detailLogsPending = false;
    emit containerDetailLogsChanged();
}

void Backend::containerDetailLifecycle(const QString &action, const QString &id)
{
    if (id.isEmpty()
        || (action != QStringLiteral("start") && action != QStringLiteral("stop")
            && action != QStringLiteral("restart") && action != QStringLiteral("pause")
            && action != QStringLiteral("unpause") && action != QStringLiteral("kill"))) {
        m_lastError = QStringLiteral("Ação ou container inválido.");
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
        return;
    }
    lifecycle(action, id);
}

void Backend::removeContainerDetail(const QString &id, bool force)
{
    removeContainer(id, force, false);
}

void Backend::createContainer(const QVariantMap &spec, bool start)
{
    QJsonObject params;
    params.insert(QStringLiteral("image"), spec.value(QStringLiteral("image")).toString());
    params.insert(QStringLiteral("name"), spec.value(QStringLiteral("name")).toString());
    params.insert(QStringLiteral("command"), spec.value(QStringLiteral("command")).toString());
    params.insert(QStringLiteral("workDir"), spec.value(QStringLiteral("workDir")).toString());
    params.insert(QStringLiteral("user"), spec.value(QStringLiteral("user")).toString());
    params.insert(QStringLiteral("remove"), spec.value(QStringLiteral("remove")).toBool());
    params.insert(QStringLiteral("init"), spec.value(QStringLiteral("init")).toBool());
    params.insert(QStringLiteral("privileged"),
                  spec.value(QStringLiteral("privileged")).toBool());
    const bool exposeLan = spec.value(QStringLiteral("exposeLan")).toBool();
    params.insert(QStringLiteral("exposeLan"), exposeLan);
    params.insert(QStringLiteral("start"), start);

    QJsonObject env;
    const QString envText = spec.value(QStringLiteral("envText")).toString();
    const QStringList envLines =
        envText.split(QRegularExpression(QStringLiteral("[,\\n]")), Qt::SkipEmptyParts);
    for (const QString &line : envLines) {
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals > 0) {
            env.insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
        }
    }
    params.insert(QStringLiteral("env"), env);

    QJsonArray ports;
    const QString portsText = spec.value(QStringLiteral("portsText")).toString();
    const QStringList portItems =
        portsText.split(QRegularExpression(QStringLiteral("[,\\n]")), Qt::SkipEmptyParts);
    for (const QString &item : portItems) {
        const QStringList parts = item.trimmed().split(QLatin1Char(':'));
        if (parts.size() < 2) {
            continue;
        }

        QString containerPart = parts.last();
        QString protocol = QStringLiteral("tcp");
        const int slash = containerPart.indexOf(QLatin1Char('/'));
        if (slash > 0) {
            protocol = containerPart.mid(slash + 1);
            containerPart = containerPart.left(slash);
        }
        const int containerPort = containerPart.toInt();
        if (containerPort <= 0) {
            continue;
        }

        QJsonObject port;
        port.insert(QStringLiteral("containerPort"), containerPort);
        port.insert(QStringLiteral("protocol"), protocol);
        if (parts.size() == 2) {
            port.insert(QStringLiteral("hostPort"), parts.at(0).toInt());
            port.insert(QStringLiteral("hostIp"), exposeLan ? QStringLiteral("0.0.0.0")
                                                            : QStringLiteral("127.0.0.1"));
        } else {
            port.insert(QStringLiteral("hostIp"),
                        exposeLan ? QStringLiteral("0.0.0.0") : parts.at(0));
            port.insert(QStringLiteral("hostPort"), parts.at(1).toInt());
        }
        ports.append(port);
    }
    params.insert(QStringLiteral("ports"), ports);

    QJsonArray mounts;
    const QString mountsText = spec.value(QStringLiteral("mountsText")).toString();
    const QStringList mountLines = mountsText.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : mountLines) {
        const QStringList parts = line.trimmed().split(QLatin1Char(':'));
        if (parts.size() < 2 || parts.at(0).isEmpty() || parts.at(1).isEmpty()) {
            continue;
        }
        QJsonObject mount;
        mount.insert(QStringLiteral("source"), parts.at(0));
        mount.insert(QStringLiteral("destination"), parts.at(1));
        mount.insert(QStringLiteral("readOnly"),
                     parts.size() >= 3 && parts.at(2) == QStringLiteral("ro"));
        mounts.append(mount);
    }
    params.insert(QStringLiteral("mounts"), mounts);

    runAction(QStringLiteral("containers.create"), params);
}

void Backend::exposeContainerLan(const QString &id, bool expose)
{
    if (id.isEmpty()) {
        m_lastError = QStringLiteral("Container inválido.");
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
        return;
    }

    QJsonObject params;
    params.insert(QStringLiteral("id"), id);
    params.insert(QStringLiteral("expose"), expose);
    runAction(QStringLiteral("containers.exposeLan"), params);
}

void Backend::subscribeLogs(const QString &id, int tail, bool follow)
{
    m_logLines.clear();
    m_logsPending = false;
    emit logsChanged();
    m_stream.subscribeLogs(id, tail, follow);
}

void Backend::unsubscribeLogs()
{
    m_stream.unsubscribe();
}

void Backend::clearLogs()
{
    m_logLines.clear();
    emit logsChanged();
}

void Backend::subscribeStats(const QString &id)
{
    m_stats.clear();
    m_cpuHistory.clear();
    m_memHistory.clear();
    emit statsChanged();
    m_stream.subscribeStats(id);
}

void Backend::unsubscribeStats()
{
    m_stream.unsubscribe();
}

void Backend::startActivityMonitor()
{
    m_activityActive = true;
    m_activityCpuHistory.clear();
    m_activityMemHistory.clear();
    m_activityStream.subscribeSystemStats(2);
    emit activityChanged();
}

void Backend::stopActivityMonitor()
{
    m_activityActive = false;
    m_activityStream.unsubscribe();
    emit activityChanged();
}

void Backend::startMachineStats(const QString &id)
{
    m_machineStats.clear();
    m_machineCpuHistory.clear();
    m_machineMemHistory.clear();
    m_machineNetRxHistory.clear();
    m_machineNetTxHistory.clear();
    m_machineStatsError.clear();
    emit machineStatsChanged();

    if (id.isEmpty()) {
        m_machineStatsError = QStringLiteral("Machine inválida.");
        m_lastError = m_machineStatsError;
        emit lastErrorChanged();
        emit machineStatsChanged();
        return;
    }

    m_machineStatsStream.subscribeMachineStats(id, 2);
}

void Backend::stopMachineStats()
{
    m_machineStatsStream.unsubscribe();

    const bool clearGlobalError = !m_machineStatsError.isEmpty()
                                  && m_lastError == m_machineStatsError;
    m_machineStatsError.clear();
    emit machineStatsChanged();
    if (clearGlobalError) {
        m_lastError.clear();
        emit lastErrorChanged();
    }
}

void Backend::followMachineConsole(const QString &id, int tailBytes)
{
    if (id.isEmpty()) {
        m_machineConsoleActive = false;
        m_machineConsoleError = QStringLiteral("Machine inválida.");
        m_machineConsoleText.clear();
        emit machineConsoleChanged();
        return;
    }

    if (tailBytes <= 0) {
        tailBytes = 64 * 1024;
    }
    tailBytes = qBound(1, tailBytes, 512 * 1024);

    m_machineConsoleText.clear();
    m_machineConsoleError.clear();
    m_machineConsoleActive = true;
    emit machineConsoleChanged();
    m_machineConsoleStream.startMachineConsole(id, tailBytes);
}

void Backend::stopMachineConsole()
{
    m_machineConsoleStream.stopMachineConsole();
    m_machineConsoleActive = false;
    m_machineConsoleText.clear();
    m_machineConsoleError.clear();
    emit machineConsoleChanged();
}

QVariantMap Backend::settings() const
{
    QVariantMap values;
    values.insert(QStringLiteral("engineMode"),
                  m_settingsValues.value(QStringLiteral("engine.mode"),
                                         QStringLiteral("rootless")));
    values.insert(QStringLiteral("theme"),
                  m_settingsValues.value(QStringLiteral("app.theme"),
                                         QStringLiteral("system")));
    values.insert(QStringLiteral("keepRunningInTray"),
                  m_settingsValues.value(QStringLiteral("app.keepRunningInTray"), true));
    values.insert(QStringLiteral("showInTray"),
                  m_settingsValues.value(QStringLiteral("app.showInTray"), true));
    values.insert(QStringLiteral("startAtLogin"),
                  m_settingsValues.value(QStringLiteral("app.startAtLogin"), false));
    values.insert(QStringLiteral("notifications"),
                  m_settingsValues.value(QStringLiteral("app.notifications"), true));
    values.insert(QStringLiteral("proxy"),
                  m_settingsValues.value(QStringLiteral("network.proxy"),
                                         QStringLiteral("auto")));
    QStringList proxyExclude;
    const QVariantList excludeList =
        m_settingsValues.value(QStringLiteral("network.proxyExclude")).toList();
    for (const QVariant &entry : excludeList) {
        const QString text = entry.toString();
        if (!text.isEmpty()) {
            proxyExclude.append(text);
        }
    }
    values.insert(QStringLiteral("proxyExclude"), proxyExclude);
    return values;
}

void Backend::refreshSettings()
{
    if (m_settingsWatcher.isRunning()) {
        m_settingsRefreshAgain = true;
        return;
    }

    m_settingsWatcher.setFuture(QtConcurrent::run([] {
        RpcClient client;
        Error error;
        const QJsonObject values =
            client.call(QStringLiteral("settings.get"), {}, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("error"), error.toJson()}};
        }
        const QJsonObject schema =
            client.call(QStringLiteral("settings.schema"), {}, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("error"), error.toJson()}};
        }
        QJsonObject result;
        result.insert(QStringLiteral("values"), values.value(QStringLiteral("values")));
        result.insert(QStringLiteral("schema"), schema.value(QStringLiteral("schema")));
        return result;
    }));
}

void Backend::onSettingsFinished()
{
    const QJsonObject result = m_settingsWatcher.result();
    if (result.contains(QStringLiteral("error"))) {
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        m_settingsError = errorObject.value(QStringLiteral("message")).toString();
        if (m_settingsError.isEmpty()) {
            m_settingsError = QStringLiteral("Falha ao carregar as configurações.");
        }
        emit settingsErrorChanged();
    } else {
        m_settingsValues = result.value(QStringLiteral("values")).toObject().toVariantMap();
        m_settingsSchema = result.value(QStringLiteral("schema")).toArray().toVariantList();
        const bool hadError = !m_settingsError.isEmpty();
        m_settingsError.clear();
        emit settingsSchemaChanged();
        emit settingsValuesChanged();
        emit settingsChanged();
        if (hadError) {
            emit settingsErrorChanged();
        }
    }

    if (m_settingsRefreshAgain) {
        m_settingsRefreshAgain = false;
        refreshSettings();
    }
}

void Backend::setSettingsError(const QString &error)
{
    m_settingsError = error;
    m_lastError = error;
    emit settingsErrorChanged();
    emit lastErrorChanged();
}

void Backend::setSetting(const QString &key, const QVariant &value)
{
    const bool applyAutostart = key == QStringLiteral("app.startAtLogin");
    setSettingInternal(key, value, applyAutostart);
}

void Backend::setSettingInternal(const QString &key, const QVariant &value,
                                 bool applyAutostart)
{
    if (key.isEmpty()) {
        setSettingsError(QStringLiteral("Chave de configuração inválida."));
        emit actionFinished(false, m_settingsError);
        return;
    }

    QJsonValue json = QJsonValue::fromVariant(value);
    if (!value.isValid() || value.isNull()) {
        json = QJsonValue(QJsonValue::Null);
    }
    const QJsonObject params{{QStringLiteral("key"), key},
                             {QStringLiteral("value"), json}};
    enqueueSettingsRequest(QStringLiteral("settings.set"), params, applyAutostart);
}

void Backend::resetSetting(const QString &key)
{
    QJsonObject params;
    if (!key.isEmpty()) {
        params.insert(QStringLiteral("key"), key);
    }
    const bool applyAutostart =
        key.isEmpty() || key == QStringLiteral("app.startAtLogin");
    enqueueSettingsRequest(QStringLiteral("settings.reset"), params, applyAutostart);
}

void Backend::enqueueSettingsRequest(const QString &method, const QJsonObject &params,
                                     bool applyAutostart)
{
    SettingsRequest request;
    request.method = method;
    request.params = params;
    request.applyAutostart = applyAutostart;
    m_settingsQueue.enqueue(request);
    startNextSettingsRequest();
}

void Backend::startNextSettingsRequest()
{
    if (m_settingsActionActive || m_settingsQueue.isEmpty()) {
        return;
    }

    m_settingsCurrentRequest = m_settingsQueue.dequeue();
    m_settingsActionActive = true;
    const QString method = m_settingsCurrentRequest.method;
    const QJsonObject params = m_settingsCurrentRequest.params;
    m_settingsActionWatcher.setFuture(QtConcurrent::run([method, params] {
        RpcClient client;
        Error error;
        const QJsonObject result = client.call(method, params, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("error"), error.toJson()}};
        }
        return result;
    }));
}

void Backend::onSettingsActionFinished()
{
    const QJsonObject result = m_settingsActionWatcher.result();
    const SettingsRequest request = m_settingsCurrentRequest;
    const QString key = request.params.value(QStringLiteral("key")).toString();
    bool ok = true;
    QString message;

    if (result.contains(QStringLiteral("error"))) {
        ok = false;
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        message = errorObject.value(QStringLiteral("message")).toString();
        const QString remediation =
            errorObject.value(QStringLiteral("remediation")).toString();
        if (!remediation.isEmpty()) {
            message += QLatin1Char(' ') + remediation;
        }
    } else {
        if (result.contains(QStringLiteral("values"))) {
            m_settingsValues =
                result.value(QStringLiteral("values")).toObject().toVariantMap();
            emit settingsValuesChanged();
            emit settingsChanged();
        }

        // O daemon devolve restartRequired quando o valor mudou e a definição
        // exige reinício; sem isso o usuário salvava e nada avisava.
        if (result.value(QStringLiteral("restartRequired")).toBool(false) && !key.isEmpty() &&
            !m_pendingRestartKeys.contains(key)) {
            m_pendingRestartKeys.append(key);
            emit pendingRestartKeysChanged();
        }

        if (request.applyAutostart) {
            QString autostartError;
            const bool enabled =
                request.params.value(QStringLiteral("value")).toBool();
            if (!Autostart::setEnabled(enabled, &autostartError)) {
                ok = false;
                message = autostartError;
            }
        }
    }

    if (ok) {
        if (!m_settingsError.isEmpty()) {
            m_settingsError.clear();
            emit settingsErrorChanged();
        }
    } else {
        setSettingsError(message);
    }

    emit settingFinished(key, ok, message);
    emit actionFinished(ok, message);
    m_settingsActionActive = false;
    startNextSettingsRequest();
}

void Backend::saveEngineMode(const QString &mode)
{
    if (mode != QStringLiteral("rootless") && mode != QStringLiteral("rootful")) {
        setSettingsError(QStringLiteral("Modo de engine inválido."));
        emit actionFinished(false, m_settingsError);
        return;
    }

    setSettingInternal(QStringLiteral("engine.mode"), mode);
}

void Backend::saveTraySettings(bool keepRunning, bool showInTray, bool startAtLogin,
                               bool notifications)
{
    setSettingInternal(QStringLiteral("app.keepRunningInTray"), keepRunning);
    setSettingInternal(QStringLiteral("app.showInTray"), showInTray);
    setSettingInternal(QStringLiteral("app.startAtLogin"), startAtLogin, true);
    setSettingInternal(QStringLiteral("app.notifications"), notifications);
}

void Backend::saveProxySettings(const QString &proxy, const QString &excludeText)
{
    const QString normalizedProxy = proxy.trimmed();
    QStringList excludes;
    const QStringList parts = excludeText.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString entry = part.trimmed();
        if (!entry.isEmpty()) {
            excludes.append(entry);
        }
    }

    Error validation;
    if (!Proxy::validate(normalizedProxy, excludes, &validation)) {
        setSettingsError(validation.message);
        emit actionFinished(false, m_settingsError);
        return;
    }

    QJsonArray excludeArray;
    for (const QString &entry : excludes) {
        excludeArray.append(entry);
    }

    setSettingInternal(QStringLiteral("network.proxy"), normalizedProxy);
    setSettingInternal(QStringLiteral("network.proxyExclude"), excludeArray.toVariantList());
}

void Backend::saveTheme(const QString &theme)
{
    if (theme != QStringLiteral("system") && theme != QStringLiteral("dark")
        && theme != QStringLiteral("light")) {
        setSettingsError(QStringLiteral("Tema inválido."));
        emit actionFinished(false, m_settingsError);
        return;
    }

    setSettingInternal(QStringLiteral("app.theme"), theme);
}

void Backend::runSetupRequest(const QString &method, const QJsonObject &params)
{
    if (m_setupWatcher.isRunning()) {
        return;
    }
    m_setupPendingMethod = method;
    m_setupBusy = true;
    m_setupError.clear();
    emit setupChanged();

    m_setupWatcher.setFuture(QtConcurrent::run([method, params] {
        RpcClient client;
        Error error;
        const QJsonObject result = client.call(method, params, &error);
        QJsonObject object = result;
        if (!error.isEmpty()) {
            object.insert(QStringLiteral("__error"), error.message);
        }
        return object;
    }));
}

void Backend::onSetupFinished()
{
    const QJsonObject result = m_setupWatcher.result();
    m_setupBusy = false;

    if (result.contains(QStringLiteral("__error"))) {
        m_setupError = result.value(QStringLiteral("__error")).toString();
        if (m_setupPendingMethod == QStringLiteral("setup.status")) {
            m_setupLoaded = true;
        }
        emit setupChanged();
        return;
    }

    m_setupError.clear();

    if (m_setupPendingMethod == QStringLiteral("setup.status")) {
        applySetupStatus(result);
    } else if (m_setupPendingMethod == QStringLiteral("setup.check")) {
        m_setupChecks = result.value(QStringLiteral("checks")).toArray().toVariantList();
        emit setupChanged();
    } else if (m_setupPendingMethod == QStringLiteral("setup.validate")) {
        m_setupChecks = result.value(QStringLiteral("checks")).toArray().toVariantList();
        bool ready = true;
        for (const QVariant &checkValue : m_setupChecks) {
            const QVariantMap check = checkValue.toMap();
            if (!SetupPolicy::isCheckSatisfied(check.value(QStringLiteral("id")).toString(),
                                               check.value(QStringLiteral("status")).toString())) {
                ready = false;
                break;
            }
        }
        m_setupSummary.insert(QStringLiteral("ready"), ready);
        m_setupStatus = ready ? QStringLiteral("Tudo pronto.")
                              : QStringLiteral("Alguns itens precisam de atenção.");
        emit setupChanged();
    } else if (m_setupPendingMethod == QStringLiteral("setup.dockerImpact")) {
        m_setupDockerImpact = result.toVariantMap();
        emit setupChanged();
    } else if (m_setupPendingMethod == QStringLiteral("setup.complete")) {
        m_setupCompleted = result.value(QStringLiteral("completed")).toBool();
        if (result.value(QStringLiteral("skipped")).toBool()) {
            m_setupShowOnboarding = false;
        }
        emit setupChanged();
    }

    m_setupPendingMethod.clear();
}

void Backend::applySetupStatus(const QJsonObject &status)
{
    m_setupLoaded = true;
    m_setupCompleted = status.value(QStringLiteral("completed")).toBool();
    m_setupShowOnboarding = status.value(QStringLiteral("showOnboarding")).toBool(true);
    m_setupDockerPolicy = status.value(QStringLiteral("dockerPolicy")).toString();
    m_setupChecks = status.value(QStringLiteral("checks")).toArray().toVariantList();
    m_setupSummary = status.value(QStringLiteral("summary")).toObject().toVariantMap();
    const QJsonObject active = status.value(QStringLiteral("activeJob")).toObject();
    applySetupJob(active);
    if (!active.isEmpty() && !m_setupJobActive) {
        const QString jobId = active.value(QStringLiteral("jobId")).toString();
        if (!jobId.isEmpty()) {
            m_setupStream.subscribeSetup(jobId);
        }
    }
    emit setupChanged();
}

void Backend::applySetupJob(const QJsonObject &job)
{
    if (job.isEmpty()) {
        return;
    }

    const QString state = job.value(QStringLiteral("state")).toString();
    m_setupJobId = job.value(QStringLiteral("jobId")).toString();
    m_setupProgress = job.value(QStringLiteral("progress")).toInt();
    m_setupJobActive = state == QStringLiteral("running") || state == QStringLiteral("queued")
                       || state == QStringLiteral("cancelling");
    if (!state.isEmpty()) {
        m_setupStatus = state;
    }
    const QString error = job.value(QStringLiteral("error")).toString();
    if (!error.isEmpty()) {
        m_setupError = error;
    }
    m_setupLog.clear();
    const QJsonArray lines = job.value(QStringLiteral("lines")).toArray();
    for (const QJsonValue &line : lines) {
        m_setupLog.append(line.toString());
    }
    emit setupJobChanged();
}

QString Backend::setupPodmanAction() const
{
    for (const QVariant &value : m_setupChecks) {
        const QVariantMap check = value.toMap();
        if (check.value(QStringLiteral("id")).toString() == QStringLiteral("podman")) {
            return SetupActions::podmanActionForStatus(
                check.value(QStringLiteral("status")).toString());
        }
    }
    return {};
}

bool Backend::setupMandatoryOk() const
{
    if (m_setupChecks.isEmpty()) {
        return false;
    }
    for (const QVariant &value : m_setupChecks) {
        const QVariantMap check = value.toMap();
        const QString id = check.value(QStringLiteral("id")).toString();
        const QString status = check.value(QStringLiteral("status")).toString();
        if (!SetupPolicy::isCheckSatisfied(id, status)) {
            return false;
        }
    }
    return true;
}

void Backend::setupRefresh()
{
    runSetupRequest(QStringLiteral("setup.status"), {});
}

void Backend::setupCheck()
{
    runSetupRequest(QStringLiteral("setup.check"), {});
}

void Backend::setupValidate()
{
    runSetupRequest(QStringLiteral("setup.validate"), {});
}

void Backend::setupApply(const QString &action, const QVariantList &components)
{
    if (m_setupJobActive) {
        return;
    }

    QJsonObject params;
    params.insert(QStringLiteral("action"), action);
    if (!components.isEmpty()) {
        params.insert(QStringLiteral("components"), QJsonArray::fromVariantList(components));
    }

    m_setupError.clear();
    m_setupLog.clear();
    m_setupProgress = 0;
    m_setupJobActive = true;
    m_setupStatus = QStringLiteral("Iniciando…");
    emit setupJobChanged();
    m_setupStream.startSetup(params);
}

void Backend::checkForUpdates(bool interactive)
{
    if (m_updateCheckRunning) {
        return;
    }
    m_updateCheckRunning = true;
    m_updateInteractive = interactive;

    QJsonObject params;
    params.insert(QStringLiteral("force"), interactive);
    m_updateWatcher.setFuture(QtConcurrent::run([params] {
        RpcClient client;
        Error error;
        QJsonObject result = client.call(QStringLiteral("updates.check"), params, &error);
        if (!error.isEmpty()) {
            result.insert(QStringLiteral("__error"), error.message);
        }
        return result;
    }));
}

void Backend::onUpdateFinished()
{
    m_updateCheckRunning = false;
    const QJsonObject result = m_updateWatcher.result();
    if (result.contains(QStringLiteral("__error"))) {
        const QString reason = result.value(QStringLiteral("__error")).toString();
        m_updateStatus = QVariantMap{{QStringLiteral("status"), QStringLiteral("unavailable")},
                                     {QStringLiteral("reason"), reason}};
        emit updateStatusChanged();
        emit updateCheckFailed(reason);
        if (m_updateInteractive) {
            m_eventKind = QStringLiteral("error");
            m_eventMessage = tr("Falha ao verificar atualizações: %1").arg(reason);
            emit eventsChanged();
        }
        return;
    }

    m_updateStatus = result.toVariantMap();
    emit updateStatusChanged();

    const bool available = result.value(QStringLiteral("updateAvailable")).toBool();
    if (available) {
        emit updateAvailable(m_updateStatus);
    }
    if (m_updateInteractive) {
        m_eventKind = QStringLiteral("info");
        m_eventMessage = available
                             ? tr("PodStack %1 está disponível.")
                                   .arg(result.value(QStringLiteral("latestVersion")).toString())
                             : tr("Nenhuma atualização disponível.");
        emit eventsChanged();
    }
}

void Backend::setupLoadDockerImpact()
{
    if (m_setupWatcher.isRunning()) {
        return;
    }
    m_setupDockerImpact.clear();
    emit setupChanged();
    runSetupRequest(QStringLiteral("setup.dockerImpact"), {});
}

void Backend::checkDiskHealth()
{
    if (m_diskCheckRunning) {
        return;
    }
    m_diskCheckRunning = true;
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

void Backend::onDiskFinished()
{
    m_diskCheckRunning = false;
    const QJsonObject result = m_diskWatcher.result();
    if (result.contains(QStringLiteral("__error"))) {
        return;
    }
    m_diskHealth = result.toVariantMap();
    emit diskHealthChanged();

    if (result.value(QStringLiteral("low")).toBool()) {
        const int threshold = result.value(QStringLiteral("thresholdPercent")).toInt();
        const QJsonArray filesystems = result.value(QStringLiteral("filesystems")).toArray();
        for (const QJsonValue &value : filesystems) {
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

void Backend::setupRemoveDocker()
{
    if (m_setupJobActive) {
        return;
    }

    QJsonObject params;
    params.insert(QStringLiteral("confirm"), true);
    m_setupError.clear();
    m_setupLog.clear();
    m_setupProgress = 0;
    m_setupJobActive = true;
    m_setupStatus = QStringLiteral("Removendo o Docker…");
    emit setupJobChanged();
    m_setupStream.startSetupRemoveDocker(params);
}

void Backend::setupCancel()
{
    if (m_setupJobId.isEmpty()) {
        return;
    }
    QJsonObject params;
    params.insert(QStringLiteral("jobId"), m_setupJobId);
    runSetupRequest(QStringLiteral("setup.cancel"), params);
}

void Backend::setupComplete()
{
    if (!setupMandatoryOk()) {
        m_setupError = QStringLiteral(
            "Conclua os itens obrigatórios antes de abrir o PodStack.");
        emit setupChanged();
        return;
    }

    m_setupError.clear();
    emit setupChanged();
    QJsonObject params;
    params.insert(QStringLiteral("skipped"), false);
    runSetupRequest(QStringLiteral("setup.complete"), params);
}

void Backend::setupSkip()
{
    QJsonObject params;
    params.insert(QStringLiteral("skipped"), true);
    runSetupRequest(QStringLiteral("setup.complete"), params);
}

void Backend::openTerminal(const QString &id)
{
    if (id.isEmpty()) {
        m_lastError = QStringLiteral("Container inválido.");
        emit lastErrorChanged();
        return;
    }

    QString title = id;
    for (const QVariant &value : m_containers) {
        const QVariantMap container = value.toMap();
        if (container.value(QStringLiteral("id")).toString() == id) {
            const QString name = container.value(QStringLiteral("name")).toString();
            if (!name.isEmpty()) {
                title = name;
            }
            break;
        }
    }

    const QStringList arguments{QStringLiteral("exec"), QStringLiteral("-it"), id,
                                QStringLiteral("/bin/sh")};
    if (isSignalConnected(QMetaMethod::fromSignal(&Backend::terminalRequested))) {
        emit terminalRequested(title, QStringLiteral("podman"), arguments);
        return;
    }

    // Fallback para uso do Backend sem a MainWindow.
    auto *window = new TerminalWindow(id, QStringLiteral("podman"), arguments);
    window->show();
    window->raise();
    window->activateWindow();
}

void Backend::openMachineSsh(const QString &id)
{
    if (id.isEmpty()) {
        m_lastError = QStringLiteral("Machine inválida.");
        emit lastErrorChanged();
        return;
    }
    if (m_machineSshWatcher.isRunning()) {
        return;
    }

    m_machineSshTitle = id;
    for (const QVariant &value : m_machines) {
        const QVariantMap machine = value.toMap();
        if (machine.value(QStringLiteral("id")).toString() == id) {
            const QString name = machine.value(QStringLiteral("name")).toString();
            if (!name.isEmpty()) {
                m_machineSshTitle = name;
            }
            break;
        }
    }

    m_machineSshWatcher.setFuture(QtConcurrent::run([id] {
        RpcClient client;
        Error error;
        const QJsonObject result =
            client.call(QStringLiteral("machines.ssh"),
                        QJsonObject{{QStringLiteral("id"), id}}, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("error"), error.toJson()}};
        }
        return result;
    }));
}

void Backend::listFiles(const QString &id, const QString &path)
{
    if (m_filesWatcher.isRunning()) {
        return;
    }

    m_filesPath = path;
    m_filesWatcher.setFuture(QtConcurrent::run([id, path] {
        RpcClient client;
        Error error;
        QJsonObject params;
        params.insert(QStringLiteral("id"), id);
        params.insert(QStringLiteral("path"), path);

        QJsonObject result = client.call(QStringLiteral("containers.files.list"), params, &error);
        if (!error.isEmpty()) {
            QJsonObject payload;
            payload.insert(QStringLiteral("error"), error.toJson());
            return payload;
        }
        return result;
    }));
}

void Backend::onFilesFinished()
{
    const QJsonObject result = m_filesWatcher.result();
    if (result.contains(QStringLiteral("error"))) {
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        m_lastError = errorObject.value(QStringLiteral("message")).toString();
        emit lastErrorChanged();
        return;
    }

    m_filesPath = result.value(QStringLiteral("path")).toString(m_filesPath);
    m_files.clear();
    const QJsonArray entries = result.value(QStringLiteral("entries")).toArray();
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        QVariantMap item;
        item.insert(QStringLiteral("name"), entry.value(QStringLiteral("name")).toString());
        item.insert(QStringLiteral("size"), entry.value(QStringLiteral("size")).toDouble());
        item.insert(QStringLiteral("directory"), entry.value(QStringLiteral("directory")).toBool());
        item.insert(QStringLiteral("mode"), entry.value(QStringLiteral("mode")).toString());
        item.insert(QStringLiteral("modified"), entry.value(QStringLiteral("modified")).toString());
        m_files.append(item);
    }
    emit filesChanged();
}

void Backend::downloadFile(const QString &id, const QString &path)
{
    const QString baseName = QFileInfo(path).fileName();
    if (baseName.isEmpty()) {
        m_lastError = QStringLiteral("Caminho inválido.");
        emit lastErrorChanged();
        return;
    }

    QString directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (directory.isEmpty()) {
        directory = QDir::homePath();
    }
    QDir().mkpath(directory);

    QJsonObject params;
    params.insert(QStringLiteral("id"), id);
    params.insert(QStringLiteral("path"), path);
    params.insert(QStringLiteral("destination"), directory + QLatin1Char('/') + baseName);
    runAction(QStringLiteral("containers.files.download"), params);
}

void Backend::renameContainer(const QString &id, const QString &name)
{
    if (id.isEmpty() || name.isEmpty()) {
        m_lastError = QStringLiteral("Informe o container e o novo nome.");
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
        return;
    }

    QJsonObject params;
    params.insert(QStringLiteral("id"), id);
    params.insert(QStringLiteral("name"), name);
    runAction(QStringLiteral("containers.rename"), params);
}

void Backend::duplicateContainer(const QString &id)
{
    if (id.isEmpty()) {
        m_lastError = QStringLiteral("Container inválido.");
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
        return;
    }

    QJsonObject params;
    params.insert(QStringLiteral("id"), id);
    runAction(QStringLiteral("containers.duplicate"), params);
}

void Backend::batchAll(const QString &action)
{
    QStringList ids;
    for (const QVariant &value : m_containers) {
        const QString id = value.toMap().value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) {
            ids.append(id);
        }
    }
    batchIds(ids, action);
}

void Backend::batchIds(const QStringList &ids, const QString &action)
{
    if (action != QStringLiteral("start") && action != QStringLiteral("stop")
        && action != QStringLiteral("restart") && action != QStringLiteral("remove")) {
        m_lastError = QStringLiteral("Ação de lote inválida.");
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
        return;
    }
    if (ids.isEmpty()) {
        return;
    }

    const PendingAction pending{
        QStringLiteral("__batch__"),
        QJsonObject{{QStringLiteral("action"), action},
                    {QStringLiteral("ids"), QJsonArray::fromStringList(ids)}},
        60000,
    };
    if (m_actionActive) {
        enqueueAction(pending);
        return;
    }
    startAction(pending);
}

void Backend::batchResource(const QString &prefix, const QString &idKey, const QString &action,
                            const QStringList &ids, bool force, const QJsonObject &extra,
                            int timeoutMs)
{
    if (ids.isEmpty()) {
        return;
    }
    const PendingAction pending{
        QStringLiteral("__batch_resource__"),
        QJsonObject{{QStringLiteral("prefix"), prefix},
                    {QStringLiteral("idKey"), idKey},
                    {QStringLiteral("action"), action},
                    {QStringLiteral("ids"), QJsonArray::fromStringList(ids)},
                    {QStringLiteral("force"), force},
                    {QStringLiteral("extra"), extra}},
        timeoutMs,
    };
    if (m_actionActive) {
        enqueueAction(pending);
        return;
    }
    startAction(pending);
}

void Backend::batchImages(const QStringList &ids, bool force)
{
    batchResource(QStringLiteral("images."), QStringLiteral("id"), QStringLiteral("remove"), ids,
                  force, {}, 120000);
}

void Backend::batchVolumes(const QStringList &names, bool force)
{
    batchResource(QStringLiteral("volumes."), QStringLiteral("name"), QStringLiteral("remove"),
                  names, force, {}, 120000);
}

void Backend::batchNetworks(const QStringList &names, bool force)
{
    batchResource(QStringLiteral("networks."), QStringLiteral("name"), QStringLiteral("remove"),
                  names, force, {}, 120000);
}

void Backend::batchPods(const QStringList &ids, const QString &action, bool force)
{
    if (action != QStringLiteral("start") && action != QStringLiteral("stop") &&
        action != QStringLiteral("restart") && action != QStringLiteral("remove")) {
        m_lastError = QStringLiteral("Ação de lote inválida.");
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
        return;
    }
    batchResource(QStringLiteral("pods."), QStringLiteral("id"), action, ids, force, {}, 120000);
}

void Backend::batchMachines(const QStringList &ids, const QString &action, bool force)
{
    if (action != QStringLiteral("start") && action != QStringLiteral("stop") &&
        action != QStringLiteral("restart") && action != QStringLiteral("delete")) {
        m_lastError = QStringLiteral("Ação de lote inválida.");
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
        return;
    }
    batchResource(QStringLiteral("machines."), QStringLiteral("id"), action, ids, force, {},
                  600000);
}

void Backend::batchCompose(const QStringList &projectPaths, const QString &action)
{
    if (action != QStringLiteral("up") && action != QStringLiteral("down") &&
        action != QStringLiteral("restart") && action != QStringLiteral("stop") &&
        action != QStringLiteral("start")) {
        m_lastError = QStringLiteral("Ação de lote inválida.");
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
        return;
    }
    QJsonObject extra;
    if (action == QStringLiteral("up")) {
        extra.insert(QStringLiteral("detach"), true);
    } else if (action == QStringLiteral("down")) {
        extra.insert(QStringLiteral("volumes"), false);
    } else {
        extra.insert(QStringLiteral("service"), QString());
    }
    batchResource(QStringLiteral("compose."), QStringLiteral("projectPath"), action, projectPaths,
                  false, extra, 600000);
}

void Backend::batchBuilds(const QStringList &ids)
{
    batchResource(QStringLiteral("builds."), QStringLiteral("id"), QStringLiteral("remove"), ids,
                  false, {}, 120000);
}

void Backend::uploadFile(const QString &id, const QString &path, const QString &sourcePath)
{
    if (id.isEmpty() || path.isEmpty() || sourcePath.isEmpty()) {
        m_lastError = QStringLiteral("Informe o container, o destino e o arquivo local.");
        emit lastErrorChanged();
        return;
    }

    runAction(QStringLiteral("containers.files.upload"),
              QJsonObject{{QStringLiteral("id"), id},
                          {QStringLiteral("path"), path},
                          {QStringLiteral("sourcePath"), sourcePath}});
}

void Backend::refreshCompose()
{
    refreshSection(QStringLiteral("compose"));
}

void Backend::composeUp(const QString &projectPath)
{
    runAction(QStringLiteral("compose.up"),
              QJsonObject{{QStringLiteral("projectPath"), projectPath},
                          {QStringLiteral("detach"), true}},
              600000);
}

void Backend::composeDown(const QString &projectPath, bool volumes)
{
    runAction(QStringLiteral("compose.down"),
              QJsonObject{{QStringLiteral("projectPath"), projectPath},
                          {QStringLiteral("volumes"), volumes}},
              600000);
}

void Backend::composeRestart(const QString &projectPath, const QString &service)
{
    runAction(QStringLiteral("compose.restart"),
              QJsonObject{{QStringLiteral("projectPath"), projectPath},
                          {QStringLiteral("service"), service}},
              600000);
}

void Backend::composeStop(const QString &projectPath, const QString &service)
{
    runAction(QStringLiteral("compose.stop"),
              QJsonObject{{QStringLiteral("projectPath"), projectPath},
                          {QStringLiteral("service"), service}},
              600000);
}

void Backend::composeStart(const QString &projectPath, const QString &service)
{
    runAction(QStringLiteral("compose.start"),
              QJsonObject{{QStringLiteral("projectPath"), projectPath},
                          {QStringLiteral("service"), service}},
              600000);
}

void Backend::subscribeComposeLogs(const QString &project, const QString &service)
{
    m_composeLogLines.clear();
    emit composeLogsChanged();
    m_composeStream.subscribeComposeLogs(project, service);
}

void Backend::unsubscribeComposeLogs()
{
    m_composeStream.unsubscribe();
}

void Backend::openPath(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QString Backend::choosePath(const QString &mode, const QString &title,
                            const QString &filter, const QString &initialPath)
{
    const QString resolvedTitle =
        title.isEmpty() ? QStringLiteral("Selecionar arquivo") : title;
    const QString resolvedFilter =
        filter.isEmpty() ? QStringLiteral("Todos os arquivos (*)") : filter;
    if (mode == QStringLiteral("save")) {
        return QFileDialog::getSaveFileName(nullptr, resolvedTitle, initialPath,
                                            resolvedFilter);
    }
    if (mode == QStringLiteral("directory")) {
        return QFileDialog::getExistingDirectory(nullptr, resolvedTitle, initialPath);
    }
    return QFileDialog::getOpenFileName(nullptr, resolvedTitle, initialPath,
                                        resolvedFilter);
}

void Backend::refreshBuilds()
{
    refreshSection(QStringLiteral("builds"));
}

void Backend::startBuild(const QVariantMap &spec)
{
    const QString contextPath = spec.value(QStringLiteral("contextPath")).toString();
    const QString tag = spec.value(QStringLiteral("tag")).toString();
    if (contextPath.isEmpty() || tag.isEmpty()) {
        m_lastError = QStringLiteral("Informe o contexto e a tag do build.");
        emit lastErrorChanged();
        return;
    }

    QJsonObject buildArgs;
    const QString argsText = spec.value(QStringLiteral("buildArgsText")).toString();
    const QStringList lines =
        argsText.split(QRegularExpression(QStringLiteral("[,\\n]")), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals > 0) {
            buildArgs.insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
        }
    }

    QString dockerfile = spec.value(QStringLiteral("dockerfile")).toString();
    if (dockerfile.isEmpty()) {
        dockerfile = QStringLiteral("Dockerfile");
    }

    QStringList platforms;
    const auto appendPlatform = [&platforms](const QString &value) {
        const QString platform = value.trimmed();
        if (!platform.isEmpty() && !platforms.contains(platform)) {
            platforms.append(platform);
        }
    };
    const QVariant platformsValue = spec.value(QStringLiteral("platforms"));
    if (platformsValue.metaType().id() == QMetaType::QStringList) {
        for (const QString &platform : platformsValue.toStringList()) {
            appendPlatform(platform);
        }
    } else if (platformsValue.metaType().id() == QMetaType::QVariantList) {
        for (const QVariant &value : platformsValue.toList()) {
            appendPlatform(value.toString());
        }
    } else {
        for (const QString &part : platformsValue.toString().split(QLatin1Char(','),
                                                                    Qt::SkipEmptyParts)) {
            appendPlatform(part);
        }
    }
    if (platforms.isEmpty()) {
        for (const QString &part : spec.value(QStringLiteral("platform"))
                                       .toString()
                                       .split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            appendPlatform(part);
        }
    }

    QJsonArray platformArray;
    for (const QString &platform : platforms) {
        platformArray.append(platform);
    }

    QJsonObject params;
    params.insert(QStringLiteral("contextPath"), contextPath);
    params.insert(QStringLiteral("tag"), tag);
    params.insert(QStringLiteral("dockerfile"), dockerfile);
    params.insert(QStringLiteral("platforms"), platformArray);
    if (platforms.size() == 1) {
        params.insert(QStringLiteral("platform"), platforms.first());
    }
    params.insert(QStringLiteral("buildArgs"), buildArgs);
    params.insert(QStringLiteral("trigger"), QStringLiteral("gui"));

    m_buildLog.clear();
    emit buildLogChanged();
    m_buildActive = true;
    m_buildStatus = QStringLiteral("Iniciando build de %1...").arg(tag);
    m_activeBuildId.clear();
    emit buildStateChanged();
    m_buildStream.startBuild(params);
}

void Backend::cancelBuild()
{
    m_buildStream.unsubscribe();
    m_buildStatus = QStringLiteral("Cancelando build...");
    emit buildStateChanged();
}

void Backend::removeBuild(const QString &id)
{
    runAction(QStringLiteral("builds.remove"), QJsonObject{{QStringLiteral("id"), id}});
}

void Backend::refreshMachines()
{
    if (m_machinesWatcher.isRunning()) {
        m_machinesRefreshAgain = true;
        return;
    }

    m_machinesWatcher.setFuture(QtConcurrent::run([] {
        RpcClient client;
        Error error;
        const QJsonObject result = client.call(QStringLiteral("machines.list"), {}, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("error"), error.toJson()}};
        }
        return result;
    }));
}

void Backend::onMachinesFinished()
{
    const QJsonObject result = m_machinesWatcher.result();
    if (result.contains(QStringLiteral("error"))) {
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        m_lastError = errorObject.value(QStringLiteral("message")).toString();
        emit lastErrorChanged();
    } else {
        QVariantList items;
        const QJsonArray array = result.value(QStringLiteral("items")).toArray();
        items.reserve(array.size());
        for (const QJsonValue &value : array) {
            items.append(machineVariant(value.toObject()));
        }
        m_machines = items;
        emit machinesChanged();
    }

    if (m_machinesRefreshAgain) {
        m_machinesRefreshAgain = false;
        refreshMachines();
    }
}

void Backend::refreshMachineDistros()
{
    if (m_machineDistrosLoaded || m_machineDistrosWatcher.isRunning()) {
        return;
    }

    m_machineDistrosWatcher.setFuture(QtConcurrent::run([] {
        RpcClient client;
        Error error;
        const QJsonObject result =
            client.call(QStringLiteral("machines.distros"), {}, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("error"), error.toJson()}};
        }
        return result;
    }));
}

void Backend::onMachineDistrosFinished()
{
    const QJsonObject result = m_machineDistrosWatcher.result();
    if (result.contains(QStringLiteral("error"))) {
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        m_lastError = errorObject.value(QStringLiteral("message")).toString();
        emit lastErrorChanged();
        return;
    }

    QVariantList items;
    const QJsonArray array = result.value(QStringLiteral("items")).toArray();
    items.reserve(array.size());
    for (const QJsonValue &value : array) {
        const QJsonObject distro = value.toObject();
        QVariantMap item = distro.toVariantMap();
        const QString name = distro.value(QStringLiteral("distro")).toString();
        const QString version = distro.value(QStringLiteral("version")).toString();
        item.insert(QStringLiteral("label"),
                    version.isEmpty() ? name : name + QLatin1Char(':') + version);
        items.append(item);
    }
    m_machineDistros = items;
    m_machineDistrosLoaded = true;
    emit machineDistrosChanged();
}

void Backend::refreshMachineSerialPorts(const QString &id)
{
    if (id.isEmpty() || m_machineSerialWatcher.isRunning()) {
        return;
    }
    m_machineSerialWatcher.setFuture(QtConcurrent::run([id] {
        RpcClient client;
        Error error;
        const QJsonObject result = client.call(QStringLiteral("machines.serial.list"),
                                               QJsonObject{{QStringLiteral("id"), id}}, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("__error"), error.message}};
        }
        return result;
    }));
}

void Backend::onMachineSerialFinished()
{
    const QJsonObject result = m_machineSerialWatcher.result();
    if (result.contains(QStringLiteral("__error"))) {
        m_lastError = result.value(QStringLiteral("__error")).toString();
        emit lastErrorChanged();
        return;
    }
    m_machineSerialPorts = result.value(QStringLiteral("ports")).toArray().toVariantList();
    emit machineSerialPortsChanged();
}

void Backend::attachMachineSerialPort(const QString &id, const QString &path)
{
    if (id.isEmpty() || path.isEmpty()) {
        return;
    }
    runAction(QStringLiteral("machines.serial.attach"),
              QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("path"), path}}, 180000);
}

void Backend::detachMachineSerialPort(const QString &id, const QString &path)
{
    if (id.isEmpty() || path.isEmpty()) {
        return;
    }
    runAction(QStringLiteral("machines.serial.detach"),
              QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("path"), path}}, 180000);
}

void Backend::refreshMachineUsbDevices(const QString &id)
{
    if (id.isEmpty() || m_machineUsbWatcher.isRunning()) {
        return;
    }
    m_machineUsbWatcher.setFuture(QtConcurrent::run([id] {
        RpcClient client;
        Error error;
        const QJsonObject result = client.call(QStringLiteral("machines.usb.list"),
                                               QJsonObject{{QStringLiteral("id"), id}}, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("__error"), error.message}};
        }
        return result;
    }));
}

void Backend::onMachineUsbFinished()
{
    const QJsonObject result = m_machineUsbWatcher.result();
    if (result.contains(QStringLiteral("__error"))) {
        m_lastError = result.value(QStringLiteral("__error")).toString();
        emit lastErrorChanged();
        return;
    }
    m_machineUsbDevices = result.value(QStringLiteral("devices")).toArray().toVariantList();
    emit machineUsbDevicesChanged();
}

void Backend::attachMachineUsbDevice(const QString &id, int bus, int address)
{
    if (id.isEmpty() || bus <= 0 || address <= 0) {
        return;
    }
    runAction(QStringLiteral("machines.usb.attach"),
              QJsonObject{{QStringLiteral("id"), id},
                          {QStringLiteral("bus"), bus},
                          {QStringLiteral("address"), address}},
              180000);
}

void Backend::detachMachineUsbDevice(const QString &id, int bus, int address)
{
    if (id.isEmpty() || bus <= 0 || address <= 0) {
        return;
    }
    runAction(QStringLiteral("machines.usb.detach"),
              QJsonObject{{QStringLiteral("id"), id},
                          {QStringLiteral("bus"), bus},
                          {QStringLiteral("address"), address}},
              180000);
}

void Backend::setMachineAudio(const QString &id, bool enabled)
{
    if (id.isEmpty()) {
        return;
    }
    runAction(QStringLiteral("machines.audio.set"),
              QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("enabled"), enabled}},
              180000);
}

void Backend::loadMachineInfo(const QString &id)
{
    if (id.isEmpty()) {
        m_lastError = QStringLiteral("Machine inválida.");
        emit lastErrorChanged();
        return;
    }

    if (m_machineInfoWatcher.isRunning()) {
        m_pendingMachineInfoId = id;
        return;
    }

    m_machineInfoWatcher.setFuture(QtConcurrent::run([id] {
        RpcClient client;
        Error error;
        const QJsonObject result =
            client.call(QStringLiteral("machines.get"),
                        QJsonObject{{QStringLiteral("id"), id}}, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("error"), error.toJson()}};
        }
        return result;
    }));
}

void Backend::onMachineInfoFinished()
{
    const QJsonObject result = m_machineInfoWatcher.result();
    // O objeto Machine tem um campo "error" (string) que não é um erro de RPC.
    if (result.value(QStringLiteral("error")).isObject()) {
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        m_lastError = errorObject.value(QStringLiteral("message")).toString();
        emit lastErrorChanged();
    } else if (!result.value(QStringLiteral("id")).toString().isEmpty()) {
        m_machineInfo = machineVariant(result);
        emit machineInfoChanged();
    }

    if (!m_pendingMachineInfoId.isEmpty()) {
        const QString next = m_pendingMachineInfoId;
        m_pendingMachineInfoId.clear();
        loadMachineInfo(next);
    }
}

void Backend::loadMachineLogs(const QString &id, int tail)
{
    if (id.isEmpty()) {
        m_lastError = QStringLiteral("Machine inválida.");
        emit lastErrorChanged();
        return;
    }

    if (tail <= 0) {
        tail = 200;
    }
    tail = qBound(1, tail, 2000);

    if (m_machineLogsWatcher.isRunning()) {
        m_pendingMachineLogsId = id;
        m_pendingMachineLogsTail = tail;
        return;
    }

    m_machineLogsWatcher.setFuture(QtConcurrent::run([id, tail] {
        RpcClient client;
        Error error;
        const QJsonObject result =
            client.call(QStringLiteral("machines.logs"),
                        QJsonObject{{QStringLiteral("id"), id},
                                    {QStringLiteral("tail"), tail}},
                        &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("error"), error.toJson()}};
        }
        return result;
    }));
}

void Backend::onMachineLogsFinished()
{
    const QJsonObject result = m_machineLogsWatcher.result();
    if (result.contains(QStringLiteral("error"))) {
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        m_lastError = errorObject.value(QStringLiteral("message")).toString();
        emit lastErrorChanged();
    } else {
        QStringList lines;
        const QJsonArray array = result.value(QStringLiteral("lines")).toArray();
        lines.reserve(array.size());
        for (const QJsonValue &value : array) {
            lines.append(value.toString());
        }
        m_machineLogLines = lines;
        m_machineLogsCapturedAt =
            QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        emit machineLogsChanged();
    }

    if (!m_pendingMachineLogsId.isEmpty()) {
        const QString next = m_pendingMachineLogsId;
        const int tail = m_pendingMachineLogsTail;
        m_pendingMachineLogsId.clear();
        loadMachineLogs(next, tail);
    }
}

void Backend::refreshMachineFiles(const QString &id, const QString &path)
{
    if (id.isEmpty()) {
        m_machineFilesError = QStringLiteral("Machine inválida.");
        emit machineFilesChanged();
        return;
    }

    const QString target = path.isEmpty() ? QStringLiteral("/") : path;
    m_machineFilesPath = target;
    if (!m_machineFilesWatcher.isRunning()) {
        m_machineFiles.clear();
    }
    startMachineFilesRequest(QStringLiteral("list"), id,
                             QStringLiteral("machines.files.list"),
                             QJsonObject{{QStringLiteral("id"), id},
                                         {QStringLiteral("path"), target}},
                             30000);
}

void Backend::downloadMachineFile(const QString &id, const QString &path,
                                  const QString &fileName)
{
    if (id.isEmpty() || path.isEmpty()) {
        m_machineFilesError = QStringLiteral("Informe a machine e o arquivo remoto.");
        emit machineFilesChanged();
        return;
    }

    QString name = fileName;
    if (name.isEmpty()) {
        name = QFileInfo(path).fileName();
    }
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..")
        || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        m_machineFilesError = QStringLiteral("Nome de arquivo remoto inválido.");
        emit machineFilesChanged();
        return;
    }

    QString directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (directory.isEmpty()) {
        directory = QDir::homePath();
    }
    if (!QDir().mkpath(directory)) {
        m_machineFilesError = QStringLiteral("Não foi possível criar a pasta de downloads.");
        emit machineFilesChanged();
        return;
    }

    const QString destination = directory + QLatin1Char('/') + name;
    startMachineFilesRequest(QStringLiteral("download"), id,
                             QStringLiteral("machines.files.download"),
                             QJsonObject{{QStringLiteral("id"), id},
                                         {QStringLiteral("path"), path},
                                         {QStringLiteral("destination"), destination}},
                             600000);
}

void Backend::uploadMachineFile(const QString &id, const QString &remoteDir,
                                const QString &localPath)
{
    if (id.isEmpty() || remoteDir.isEmpty() || localPath.isEmpty()) {
        m_machineFilesError = QStringLiteral("Informe a machine, o destino e o arquivo local.");
        emit machineFilesChanged();
        return;
    }

    const QFileInfo source(localPath);
    const QString name = source.fileName();
    if (name.isEmpty()) {
        m_machineFilesError = QStringLiteral("Arquivo local inválido.");
        emit machineFilesChanged();
        return;
    }

    const QString base = remoteDir.endsWith(QLatin1Char('/')) ? remoteDir
                                                              : remoteDir + QLatin1Char('/');
    startMachineFilesRequest(QStringLiteral("upload"), id,
                             QStringLiteral("machines.files.upload"),
                             QJsonObject{{QStringLiteral("id"), id},
                                         {QStringLiteral("path"), base + name},
                                         {QStringLiteral("sourcePath"), localPath}},
                             600000);
}

void Backend::startMachineFilesRequest(const QString &operation, const QString &id,
                                       const QString &method, const QJsonObject &params,
                                       int timeoutMs)
{
    if (m_machineFilesWatcher.isRunning()) {
        m_machineFilesError =
            QStringLiteral("Aguarde a operação de arquivos atual terminar.");
        emit machineFilesChanged();
        return;
    }

    m_machineFilesMachineId = id;
    m_machineFilesOperation = operation;
    m_machineFilesBusy = true;
    m_machineFilesError.clear();
    emit machineFilesChanged();

    m_machineFilesWatcher.setFuture(QtConcurrent::run([method, params, timeoutMs] {
        RpcClient client;
        Error error;
        const QJsonObject result = client.call(method, params, &error, timeoutMs);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("error"), error.toJson()}};
        }
        return result;
    }));
}

void Backend::onMachineFilesFinished()
{
    const QJsonObject result = m_machineFilesWatcher.result();
    const QString operation = m_machineFilesOperation;
    m_machineFilesOperation.clear();
    m_machineFilesBusy = false;

    if (result.value(QStringLiteral("error")).isObject()) {
        m_machineFilesError =
            result.value(QStringLiteral("error")).toObject().value(QStringLiteral("message"))
                .toString();
        if (m_machineFilesError.isEmpty()) {
            m_machineFilesError = QStringLiteral("A operação de arquivos da machine falhou.");
        }
        if (operation == QStringLiteral("list")) {
            m_machineFiles.clear();
        }
        emit machineFilesChanged();
        return;
    }

    m_machineFilesError.clear();
    if (operation == QStringLiteral("list")) {
        m_machineFilesPath = result.value(QStringLiteral("path"))
                                 .toString(m_machineFilesPath);
        m_machineFiles.clear();
        const QJsonArray entries = result.value(QStringLiteral("items")).toArray();
        for (const QJsonValue &value : entries) {
            const QJsonObject entry = value.toObject();
            QVariantMap item;
            item.insert(QStringLiteral("name"),
                        entry.value(QStringLiteral("name")).toString());
            item.insert(QStringLiteral("path"),
                        entry.value(QStringLiteral("path")).toString());
            item.insert(QStringLiteral("directory"),
                        entry.value(QStringLiteral("directory")).toBool());
            item.insert(QStringLiteral("size"),
                        entry.value(QStringLiteral("size")).toDouble());
            item.insert(QStringLiteral("modified"),
                        entry.value(QStringLiteral("modified")).toString());
            m_machineFiles.append(item);
        }
        emit machineFilesChanged();
        return;
    }

    emit machineFilesChanged();
    if (operation == QStringLiteral("upload") && !m_machineFilesMachineId.isEmpty()) {
        const QString machineId = m_machineFilesMachineId;
        const QString path = m_machineFilesPath;
        QTimer::singleShot(0, this, [this, machineId, path] {
            refreshMachineFiles(machineId, path);
        });
    }
}

void Backend::copyMachineLogs()
{
    if (m_machineLogLines.isEmpty()) {
        return;
    }
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(m_machineLogLines.join(QLatin1Char('\n')));
    }
    emit actionFinished(true, {});
}

void Backend::machineLifecycle(const QString &action, const QString &id, bool force)
{
    if (id.isEmpty()
        || (action != QStringLiteral("start") && action != QStringLiteral("stop")
            && action != QStringLiteral("restart"))) {
        m_lastError = QStringLiteral("Ação ou machine inválida.");
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
        return;
    }

    QJsonObject params;
    params.insert(QStringLiteral("id"), id);
    if (force) {
        params.insert(QStringLiteral("force"), true);
    }
    runAction(QStringLiteral("machines.") + action, params, 120000);
}

void Backend::removeMachine(const QString &id, bool force)
{
    if (id.isEmpty()) {
        m_lastError = QStringLiteral("Machine inválida.");
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
        return;
    }

    runAction(QStringLiteral("machines.delete"),
              QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("force"), force}},
              120000);
}

void Backend::renameMachine(const QString &id, const QString &name)
{
    if (id.isEmpty() || name.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("Informe a machine e o novo nome.");
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
        return;
    }

    runAction(QStringLiteral("machines.rename"),
              QJsonObject{{QStringLiteral("id"), id},
                          {QStringLiteral("name"), name.trimmed()}});
}

void Backend::cloneMachine(const QString &id, const QString &name)
{
    const auto fail = [this](const QString &message) {
        m_machineCreateActive = false;
        m_machineCreateStage = QStringLiteral("error");
        m_machineCreateProgress = 0;
        m_machineCreateError = message;
        emit machineCreateStateChanged();
        m_lastError = message;
        emit lastErrorChanged();
        emit actionFinished(false, message);
    };

    if (m_machineCreateActive) {
        fail(QStringLiteral("Já existe uma operação de machine em andamento."));
        return;
    }
    if (id.isEmpty() || name.trimmed().isEmpty()) {
        fail(QStringLiteral("Informe a machine de origem e o nome da cópia."));
        return;
    }

    m_machineCreateActive = true;
    m_machineJobKind = QStringLiteral("clone");
    m_machineCreateJobId.clear();
    m_machineCreateStage = QStringLiteral("validating");
    m_machineCreateProgress = 0;
    m_machineCreateError.clear();
    emit machineCreateStateChanged();

    QJsonObject params;
    params.insert(QStringLiteral("id"), id);
    params.insert(QStringLiteral("name"), name.trimmed());
    m_machineCreateStream.startMachineClone(params);
}

void Backend::updateMachine(const QVariantMap &spec)
{
    const QString id = spec.value(QStringLiteral("id")).toString().trimmed();
    const bool hasCpus = spec.contains(QStringLiteral("cpus"));
    const bool hasMemory = spec.contains(QStringLiteral("memoryGiB"));
    const bool hasDisk = spec.contains(QStringLiteral("diskGiB"));
    const bool hasForwardAgent = spec.contains(QStringLiteral("forwardSshAgent"));
    if (id.isEmpty() || (!hasCpus && !hasMemory && !hasDisk && !hasForwardAgent)) {
        m_lastError = QStringLiteral("Informe a machine e ao menos um recurso ou o SSH agent.");
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
        return;
    }

    constexpr qint64 kGiB = 1024LL * 1024LL * 1024LL;
    QJsonObject params;
    params.insert(QStringLiteral("id"), id);
    if (hasCpus) {
        const int cpus = spec.value(QStringLiteral("cpus")).toInt();
        if (cpus < 1 || cpus > 256) {
            m_lastError = QStringLiteral("Número de CPUs fora do intervalo 1..256.");
            emit lastErrorChanged();
            emit actionFinished(false, m_lastError);
            return;
        }
        params.insert(QStringLiteral("cpus"), cpus);
    }
    if (hasMemory) {
        const int memoryGiB = spec.value(QStringLiteral("memoryGiB")).toInt();
        if (memoryGiB < 1) {
            m_lastError = QStringLiteral("Memória insuficiente (mínimo 1 GiB).");
            emit lastErrorChanged();
            emit actionFinished(false, m_lastError);
            return;
        }
        params.insert(QStringLiteral("memoryBytes"), static_cast<qint64>(memoryGiB) * kGiB);
    }
    if (hasDisk) {
        const int diskGiB = spec.value(QStringLiteral("diskGiB")).toInt();
        if (diskGiB < 1) {
            m_lastError = QStringLiteral("Disco insuficiente (mínimo 1 GiB).");
            emit lastErrorChanged();
            emit actionFinished(false, m_lastError);
            return;
        }
        params.insert(QStringLiteral("diskBytes"), static_cast<qint64>(diskGiB) * kGiB);
    }
    if (hasForwardAgent) {
        params.insert(QStringLiteral("forwardSshAgent"),
                      spec.value(QStringLiteral("forwardSshAgent")).toBool());
    }

    runAction(QStringLiteral("machines.update"), params, 180000);
}

void Backend::setMachinePorts(const QString &id, const QVariantList &ports)
{
    const auto fail = [this](const QString &message) {
        m_lastError = message;
        emit lastErrorChanged();
        emit actionFinished(false, message);
    };

    if (id.isEmpty()) {
        fail(QStringLiteral("Machine inválida."));
        return;
    }

    QJsonArray array;
    for (const QVariant &value : ports) {
        const QVariantMap entry = value.toMap();
        bool hostOk = false;
        bool guestOk = false;
        const int hostPort = entry.value(QStringLiteral("hostPort")).toInt(&hostOk);
        const int guestPort = entry.value(QStringLiteral("guestPort")).toInt(&guestOk);
        if (!hostOk || hostPort < 1 || hostPort > 65535) {
            fail(QStringLiteral("Porta do host fora do intervalo 1..65535."));
            return;
        }
        if (!guestOk || guestPort < 1 || guestPort > 65535) {
            fail(QStringLiteral("Porta do guest fora do intervalo 1..65535."));
            return;
        }

        QString protocol =
            entry.value(QStringLiteral("protocol"), QStringLiteral("tcp")).toString().trimmed()
                .toLower();
        if (protocol.isEmpty()) {
            protocol = QStringLiteral("tcp");
        }
        if (protocol != QStringLiteral("tcp") && protocol != QStringLiteral("udp")) {
            fail(QStringLiteral("Protocolo de porta inválido; use tcp ou udp."));
            return;
        }

        QJsonObject object;
        const QString portId = entry.value(QStringLiteral("id")).toString().trimmed();
        if (!portId.isEmpty()) {
            object.insert(QStringLiteral("id"), portId);
        }
        object.insert(QStringLiteral("hostPort"), hostPort);
        object.insert(QStringLiteral("guestPort"), guestPort);
        object.insert(QStringLiteral("protocol"), protocol);
        object.insert(QStringLiteral("exposeLan"),
                      entry.value(QStringLiteral("exposeLan")).toBool());
        object.insert(QStringLiteral("label"),
                      entry.value(QStringLiteral("label")).toString());
        array.append(object);
    }

    runAction(QStringLiteral("machines.ports.set"),
              QJsonObject{{QStringLiteral("id"), id},
                          {QStringLiteral("ports"), array}},
              180000);
}

void Backend::createMachineSnapshot(const QString &id, const QString &name)
{
    const auto fail = [this](const QString &message) {
        m_lastError = message;
        emit lastErrorChanged();
        emit actionFinished(false, message);
    };

    if (id.isEmpty()) {
        fail(QStringLiteral("Machine inválida."));
        return;
    }
    if (name.trimmed().isEmpty()) {
        fail(QStringLiteral("Informe o nome do snapshot."));
        return;
    }
    runAction(QStringLiteral("machines.snapshots.create"),
              QJsonObject{{QStringLiteral("id"), id},
                          {QStringLiteral("name"), name.trimmed()}},
              180000);
}

void Backend::restoreMachineSnapshot(const QString &id, const QString &name)
{
    const auto fail = [this](const QString &message) {
        m_lastError = message;
        emit lastErrorChanged();
        emit actionFinished(false, message);
    };

    if (id.isEmpty()) {
        fail(QStringLiteral("Machine inválida."));
        return;
    }
    if (name.trimmed().isEmpty()) {
        fail(QStringLiteral("Informe o nome do snapshot."));
        return;
    }
    runAction(QStringLiteral("machines.snapshots.restore"),
              QJsonObject{{QStringLiteral("id"), id},
                          {QStringLiteral("name"), name.trimmed()}},
              180000);
}

void Backend::removeMachineSnapshot(const QString &id, const QString &name)
{
    const auto fail = [this](const QString &message) {
        m_lastError = message;
        emit lastErrorChanged();
        emit actionFinished(false, message);
    };

    if (id.isEmpty()) {
        fail(QStringLiteral("Machine inválida."));
        return;
    }
    if (name.trimmed().isEmpty()) {
        fail(QStringLiteral("Informe o nome do snapshot."));
        return;
    }
    runAction(QStringLiteral("machines.snapshots.remove"),
              QJsonObject{{QStringLiteral("id"), id},
                          {QStringLiteral("name"), name.trimmed()}},
              180000);
}

void Backend::refreshMachineSnapshots(const QString &id)
{
    if (id.isEmpty()) {
        m_machineSnapshotsError = QStringLiteral("Machine inválida.");
        emit machineSnapshotsChanged();
        return;
    }

    if (m_machineSnapshotsWatcher.isRunning()) {
        m_pendingMachineSnapshotsId = id;
        return;
    }

    m_machineSnapshotsError.clear();
    m_machineSnapshotsWatcher.setFuture(QtConcurrent::run([id] {
        RpcClient client;
        Error error;
        const QJsonObject result =
            client.call(QStringLiteral("machines.snapshots.list"),
                        QJsonObject{{QStringLiteral("id"), id}}, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("error"), error.toJson()}};
        }
        return result;
    }));
}

void Backend::onMachineSnapshotsFinished()
{
    const QJsonObject result = m_machineSnapshotsWatcher.result();
    if (result.value(QStringLiteral("error")).isObject()) {
        m_machineSnapshotsError =
            result.value(QStringLiteral("error")).toObject().value(QStringLiteral("message"))
                .toString();
        if (m_machineSnapshotsError.isEmpty()) {
            m_machineSnapshotsError =
                QStringLiteral("Não foi possível listar os snapshots da machine.");
        }
    } else {
        m_machineSnapshotsError.clear();
        QVariantList items;
        const QJsonArray array = result.value(QStringLiteral("items")).toArray();
        items.reserve(array.size());
        for (const QJsonValue &value : array) {
            items.append(value.toObject().toVariantMap());
        }
        m_machineSnapshots = items;
    }
    emit machineSnapshotsChanged();

    if (!m_pendingMachineSnapshotsId.isEmpty()) {
        const QString next = m_pendingMachineSnapshotsId;
        m_pendingMachineSnapshotsId.clear();
        refreshMachineSnapshots(next);
    }
}

void Backend::createMachine(const QVariantMap &spec)
{
    const auto fail = [this](const QString &message) {
        m_machineCreateActive = false;
        m_machineCreateStage = QStringLiteral("error");
        m_machineCreateProgress = 0;
        m_machineCreateError = message;
        emit machineCreateStateChanged();
        m_lastError = message;
        emit lastErrorChanged();
        emit actionFinished(false, message);
    };

    const QString name = spec.value(QStringLiteral("name")).toString().trimmed();
    static const QRegularExpression namePattern(
        QStringLiteral("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
    if (name.isEmpty() || !namePattern.match(name).hasMatch()) {
        fail(QStringLiteral("Nome de machine inválido."));
        return;
    }

    const QString distro = spec.value(QStringLiteral("distro")).toString().trimmed();
    if (distro.isEmpty()) {
        fail(QStringLiteral("Informe a distro da machine."));
        return;
    }
    const bool isolated = spec.value(QStringLiteral("isolated")).toBool();

    const int cpus = spec.value(QStringLiteral("cpus"), 2).toInt();
    if (cpus < 1 || cpus > 256) {
        fail(QStringLiteral("Número de CPUs fora do intervalo 1..256."));
        return;
    }

    const int memoryGiB = spec.value(QStringLiteral("memoryGiB"), 4).toInt();
    const int diskGiB = spec.value(QStringLiteral("diskGiB"), 20).toInt();
    if (memoryGiB < 1) {
        fail(QStringLiteral("Memória insuficiente (mínimo 1 GiB)."));
        return;
    }
    if (diskGiB < 1) {
        fail(QStringLiteral("Disco insuficiente (mínimo 1 GiB)."));
        return;
    }

    constexpr qint64 kGiB = 1024LL * 1024LL * 1024LL;
    QJsonObject params;
    params.insert(QStringLiteral("name"), name);
    params.insert(QStringLiteral("distro"), distro);
    const QString version = spec.value(QStringLiteral("version")).toString().trimmed();
    if (!version.isEmpty()) {
        params.insert(QStringLiteral("version"), version);
    }
    params.insert(QStringLiteral("cpus"), cpus);
    params.insert(QStringLiteral("memoryBytes"), static_cast<qint64>(memoryGiB) * kGiB);
    params.insert(QStringLiteral("diskBytes"), static_cast<qint64>(diskGiB) * kGiB);
    const QString user = spec.value(QStringLiteral("user")).toString();
    if (!user.isEmpty()) {
        params.insert(QStringLiteral("user"), user);
    }
    const QString userData = spec.value(QStringLiteral("userData")).toString();
    if (!userData.isEmpty()) {
        params.insert(QStringLiteral("userData"), userData);
    }
    const QString password = spec.value(QStringLiteral("password")).toString();
    if (!password.isEmpty()) {
        params.insert(QStringLiteral("password"), password);
    }
    params.insert(QStringLiteral("isolated"), isolated);
    // O modo isolado implica rede restrita e sem SSH agent; o daemon reforça.
    params.insert(QStringLiteral("isolateNetwork"),
                  isolated
                      || spec.value(QStringLiteral("isolateNetwork")).toBool());
    params.insert(QStringLiteral("forwardSshAgent"),
                  isolated
                      ? false
                      : spec.value(QStringLiteral("forwardSshAgent")).toBool());

    QJsonArray mounts;
    const QString mountsText = spec.value(QStringLiteral("mountsText")).toString();
    const QStringList mountLines = mountsText.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &rawLine : mountLines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const QStringList parts = line.split(QLatin1Char(':'));
        if (parts.size() < 2 || parts.size() > 3) {
            fail(QStringLiteral("Mount inválido: use origem:destino[:ro]."));
            return;
        }
        const QString source = parts.at(0).trimmed();
        const QString target = parts.at(1).trimmed();
        const QString mode = parts.size() == 3 ? parts.at(2).trimmed() : QString();
        if (source.isEmpty() || target.isEmpty() || !source.startsWith(QLatin1Char('/'))
            || !target.startsWith(QLatin1Char('/'))) {
            fail(QStringLiteral("Mount inválido: origem e destino precisam ser absolutos."));
            return;
        }
        if (!mode.isEmpty() && mode != QStringLiteral("ro")) {
            fail(QStringLiteral("Mount inválido: o modo deve ser 'ro'."));
            return;
        }
        QJsonObject mount;
        mount.insert(QStringLiteral("source"), source);
        mount.insert(QStringLiteral("target"), target);
        mount.insert(QStringLiteral("readOnly"), mode == QStringLiteral("ro"));
        mounts.append(mount);
    }
    if (!mounts.isEmpty()) {
        if (isolated) {
            fail(QStringLiteral("O modo isolado não permite mounts do host."));
            return;
        }
        params.insert(QStringLiteral("mounts"), mounts);
    }

    m_machineCreateActive = true;
    m_machineJobKind = QStringLiteral("create");
    m_machineCreateJobId.clear();
    m_machineCreateStage = QStringLiteral("validating");
    m_machineCreateProgress = 0;
    m_machineCreateError.clear();
    emit machineCreateStateChanged();
    m_machineCreateStream.startMachineCreate(params);
}

void Backend::cancelMachineCreate()
{
    if (m_machineCreateJobId.isEmpty()) {
        return;
    }

    const QString jobId = m_machineCreateJobId;
    m_machineCreateStage = QStringLiteral("cancelling");
    emit machineCreateStateChanged();
    (void)QtConcurrent::run([jobId] {
        RpcClient client;
        Error error;
        client.call(QStringLiteral("machines.cancel"),
                    QJsonObject{{QStringLiteral("jobId"), jobId}}, &error, 30000);
    });
}

void Backend::resetMachineCreateState()
{
    if (m_machineCreateActive) {
        return;
    }
    m_machineCreateStage.clear();
    m_machineCreateProgress = 0;
    m_machineCreateError.clear();
    m_machineCreateJobId.clear();
    m_machineJobKind.clear();
    emit machineCreateStateChanged();
}

void Backend::exportMachine(const QString &id, const QString &path, bool force)
{    const auto fail = [this](const QString &message) {
        m_lastError = message;
        emit lastErrorChanged();
        emit actionFinished(false, message);
    };

    if (m_machineCreateActive) {
        fail(QStringLiteral("Já existe uma operação de machine em andamento."));
        return;
    }
    if (id.isEmpty() || path.trimmed().isEmpty()) {
        fail(QStringLiteral("Informe a machine e o arquivo de destino do export."));
        return;
    }

    m_machineCreateActive = true;
    m_machineJobKind = QStringLiteral("export");
    m_machineCreateJobId.clear();
    m_machineCreateStage = QStringLiteral("validating");
    m_machineCreateProgress = 0;
    m_machineCreateError.clear();
    emit machineCreateStateChanged();

    QJsonObject params;
    params.insert(QStringLiteral("id"), id);
    params.insert(QStringLiteral("path"), path.trimmed());
    if (force) {
        params.insert(QStringLiteral("force"), true);
    }
    m_machineCreateStream.startMachineExport(params);
}

void Backend::importMachine(const QString &path, const QString &name, bool force)
{
    const auto fail = [this](const QString &message) {
        m_lastError = message;
        emit lastErrorChanged();
        emit actionFinished(false, message);
    };

    if (m_machineCreateActive) {
        fail(QStringLiteral("Já existe uma operação de machine em andamento."));
        return;
    }
    if (path.trimmed().isEmpty()) {
        fail(QStringLiteral("Informe o arquivo de machine para importar."));
        return;
    }

    m_machineCreateActive = true;
    m_machineJobKind = QStringLiteral("import");
    m_machineCreateJobId.clear();
    m_machineCreateStage = QStringLiteral("validating");
    m_machineCreateProgress = 0;
    m_machineCreateError.clear();
    emit machineCreateStateChanged();

    QJsonObject params;
    params.insert(QStringLiteral("path"), path.trimmed());
    if (!name.trimmed().isEmpty()) {
        params.insert(QStringLiteral("name"), name.trimmed());
    }
    if (force) {
        params.insert(QStringLiteral("force"), true);
    }
    m_machineCreateStream.startMachineImport(params);
}

QString Backend::machineArchivePath(const QString &id) const
{
    QString name;
    for (const QVariant &value : m_machines) {
        const QVariantMap machine = value.toMap();
        if (machine.value(QStringLiteral("id")).toString() == id) {
            name = machine.value(QStringLiteral("name")).toString();
            break;
        }
    }
    QString directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (directory.isEmpty()) {
        directory = QDir::homePath();
    }
    if (name.isEmpty()) {
        name = QStringLiteral("machine");
    }
    return directory + QLatin1Char('/') + name + QStringLiteral(".tar.zst");
}

void Backend::runAction(const QString &method, const QJsonObject &params, int timeoutMs)
{
    const PendingAction pending{method, params, timeoutMs};
    if (m_actionActive) {
        enqueueAction(pending);
        return;
    }
    startAction(pending);
}

void Backend::startAction(const PendingAction &action)
{
    m_actionActive = true;
    m_activeActionMethod = action.method;
    m_activeActionParams = action.params;
    emit actionStateChanged();

    if (action.method == QStringLiteral("__batch__")) {
        const QString batchAction = action.params.value(QStringLiteral("action")).toString();
        const QJsonArray array = action.params.value(QStringLiteral("ids")).toArray();
        QStringList ids;
        ids.reserve(array.size());
        for (const QJsonValue &value : array) {
            ids.append(value.toString());
        }
        m_actionWatcher.setFuture(QtConcurrent::run(runBatchAction, batchAction, ids));
        return;
    }

    if (action.method == QStringLiteral("__batch_resource__")) {
        const QString prefix = action.params.value(QStringLiteral("prefix")).toString();
        const QString idKey = action.params.value(QStringLiteral("idKey")).toString();
        const QString batchAction = action.params.value(QStringLiteral("action")).toString();
        const QJsonArray array = action.params.value(QStringLiteral("ids")).toArray();
        const bool force = action.params.value(QStringLiteral("force")).toBool();
        const QJsonObject extra = action.params.value(QStringLiteral("extra")).toObject();
        QStringList ids;
        ids.reserve(array.size());
        for (const QJsonValue &value : array) {
            ids.append(value.toString());
        }
        m_actionWatcher.setFuture(
            QtConcurrent::run(runBatchResource, prefix, idKey, batchAction, ids, force, extra));
        return;
    }

    m_actionWatcher.setFuture(QtConcurrent::run([action] {
        RpcClient client;
        Error error;
        QJsonObject result = client.call(action.method, action.params, &error, action.timeoutMs);
        if (!error.isEmpty()) {
            QJsonObject payload;
            payload.insert(QStringLiteral("error"), error.toJson());
            return payload;
        }
        return result;
    }));
}

void Backend::enqueueAction(const PendingAction &action)
{
    if (m_actionQueue.size() >= 32) {
        m_lastError = QStringLiteral("Muitas ações pendentes. Aguarde a conclusão.");
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
        return;
    }
    m_actionQueue.append(action);
}

void Backend::onActionFinished()
{
    const QJsonObject result = m_actionWatcher.result();
    // Resultados de machines trazem um campo "error" (string) do próprio
    // metadado; só é erro de RPC quando o valor é um objeto.
    if (result.value(QStringLiteral("error")).isObject()) {
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        QString message = errorObject.value(QStringLiteral("message")).toString();
        const QString detail = errorObject.value(QStringLiteral("detail")).toString();
        if (!detail.isEmpty() && !message.contains(detail)) {
            message += QLatin1Char(' ') + detail;
        }
        const QString remediation =
            errorObject.value(QStringLiteral("remediation")).toString();
        if (!remediation.isEmpty()) {
            message += QLatin1Char(' ') + remediation;
        }
        m_lastError = message;
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
    } else {
        if (!m_lastError.isEmpty()) {
            m_lastError.clear();
            emit lastErrorChanged();
        }
        emit actionFinished(true, {});
        refresh();
        refreshCatalog();
    }

    if (m_activeActionMethod == QStringLiteral("__batch_resource__")) {
        const QString prefix = m_activeActionParams.value(QStringLiteral("prefix")).toString();
        if (prefix == QStringLiteral("machines.")) {
            refreshMachines();
        } else if (prefix == QStringLiteral("compose.")) {
            refreshCompose();
        } else if (prefix == QStringLiteral("builds.")) {
            refreshBuilds();
        }
    }

    if (m_activeActionMethod.startsWith(QStringLiteral("k8s."))) {
        refreshK8s();
    }

    if (m_activeActionMethod.startsWith(QStringLiteral("machines."))) {
        refreshMachines();
        const QString machineId = m_activeActionParams.value(QStringLiteral("id")).toString();
        if (!machineId.isEmpty()
            && m_activeActionMethod != QStringLiteral("machines.delete")) {
            // Revalida o estado/portas depois de start/stop/restart.
            loadMachineInfo(machineId);
        }
        if (!machineId.isEmpty()
            && m_activeActionMethod.startsWith(QStringLiteral("machines.snapshots."))) {
            // Criação/restauração/remoção alteram o disco: relê os snapshots.
            refreshMachineSnapshots(machineId);
        }
    }
    m_activeActionParams = {};

    if (!m_actionQueue.isEmpty()) {
        const PendingAction next = m_actionQueue.takeFirst();
        startAction(next);
        return;
    }

    m_actionActive = false;
    emit actionStateChanged();
}

} // namespace PodStack
