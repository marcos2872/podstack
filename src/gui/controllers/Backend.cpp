#include "gui/controllers/Backend.h"

#include "core/system/DiskUsage.h"
#include "core/error/Error.h"
#include "core/logging/LogReader.h"
#include "core/logging/Logging.h"
#include "core/config/Paths.h"
#include "core/system/Proxy.h"
#include "core/logging/Redaction.h"
#include "core/rpc/RpcClient.h"
#include "core/setup/SetupActions.h"
#include "core/setup/SetupPolicy.h"
#include "core/helper/HelperClient.h"
#include "gui/app/Autostart.h"
#include "gui/windows/TerminalWindow.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QUrl>
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
    connect(&m_actionWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onActionFinished);
    connect(&m_containers, &ContainerController::connectionChanged, this,
            [this](bool connected, const QString &error) {
                if (connected) {
                    if (m_connected && m_error.isEmpty()) {
                        return;
                    }
                    m_connected = true;
                    m_error.clear();
                    emit stateChanged();
                    return;
                }
                if (!m_connected && m_error == error) {
                    return;
                }
                m_connected = false;
                m_error = error;
                emit stateChanged();
            });
    connect(&m_containers, &ContainerController::catalogDirty, this,
            &Backend::refreshCatalog);
    connect(&m_containers, &ContainerController::terminalRequested, this,
            &Backend::terminalRequested);
    connect(&m_catalogWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onCatalogFinished);
    connect(&m_updateWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onUpdateFinished);
    connect(&m_systemLogsWatcher, &QFutureWatcher<SystemLogsTask>::finished, this,
            &Backend::onSystemLogsFinished);
    connect(&m_reportWatcher, &QFutureWatcher<QJsonObject>::finished, this, [this]() {
        const QJsonObject result = m_reportWatcher.result();
        if (result.contains(QStringLiteral("__error"))) {
            m_systemLogsError = result.value(QStringLiteral("__error")).toString();
            emit systemLogsChanged();
            return;
        }
        m_diagnosticReportPath = result.value(QStringLiteral("path")).toString();
        emit diagnosticReportChanged();
    });
    connect(&m_diskWatcher, &QFutureWatcher<QJsonObject>::finished, this, &Backend::onDiskFinished);
    m_updateTimer.setInterval(6 * 60 * 60 * 1000);
    connect(&m_updateTimer, &QTimer::timeout, this, [this] { checkForUpdates(false); });
    m_updateTimer.start();
    // Verifica logo após o app abrir; o modo `off` é respeitado em
    // checkForUpdates() e pode ser alterado em Configurações → Geral.
    QTimer::singleShot(5000, this, [this] { checkForUpdates(false); });
    QTimer::singleShot(1000, this, &Backend::loadUpdateState);
    m_diskTimer.setInterval(5 * 60 * 1000);
    connect(&m_diskTimer, &QTimer::timeout, this, &Backend::checkDiskHealth);
    m_diskTimer.start();
    QTimer::singleShot(15000, this, &Backend::checkDiskHealth);
    connect(&m_settings, &SettingsController::valuesChanged, this, [this] {
        m_updatesEnabled =
            m_settings.values().value(QStringLiteral("updates.mode"), QStringLiteral("check"))
                .toString()
            != QStringLiteral("off");
    });
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
                          << QStringLiteral("-o")
                          << QStringLiteral("UserKnownHostsFile=%1/known_hosts")
                                 .arg(QFileInfo(identityFile).absolutePath())
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
    QTimer::singleShot(0, this, [this] { m_settings.refresh(); });
}

QString Backend::version() const
{
    return QStringLiteral(PODSTACK_VERSION);
}

void Backend::refresh()
{
    m_containers.refresh();
}

void Backend::refreshCatalog()
{
    m_images.refresh();
    m_volumes.refresh();
    m_networks.refresh();
    m_pods.refresh();
    m_compose.refresh();
    m_builds.refresh();
    refreshSection(QStringLiteral("df"));
}

void Backend::refreshDiskUsage()
{
    refreshSection(QStringLiteral("df"));
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
    if (m_catalogSection == QStringLiteral("df")) {
        method = QStringLiteral("system.df");
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

    if (m_catalogSection == QStringLiteral("df")) {
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

    }

    startNextCatalog();
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

void Backend::checkForUpdates(bool interactive)
{
    if (m_updateCheckRunning) {
        return;
    }
    if (!interactive && !m_updatesEnabled) {
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

void Backend::loadUpdateState()
{
    QFile file(Paths::stateDir() + QStringLiteral("/updates/state.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    const QString phase = object.value(QStringLiteral("phase")).toString();
    if (phase != QStringLiteral("done") && phase != QStringLiteral("failed")) {
        return;
    }
    QVariantMap state = object.toVariantMap();
    if (phase == QStringLiteral("done")) {
        state.insert(QStringLiteral("status"), QStringLiteral("installed"));
        state.insert(QStringLiteral("reason"), tr("Atualização instalada."));
    } else {
        state.insert(QStringLiteral("status"), QStringLiteral("failed"));
        state.insert(QStringLiteral("reason"), object.value(QStringLiteral("message")).toString());
    }
    m_updateStatus = state;
    emit updateStatusChanged();
}

void Backend::installUpdate()
{
    startUpdateRunner(false);
}

void Backend::resumeUpdate()
{
    startUpdateRunner(true);
}

void Backend::startUpdateRunner(bool resume)
{
    if (qEnvironmentVariableIsSet("FLATPAK_ID") ||
        QFileInfo::exists(QStringLiteral("/.flatpak-info"))) {
        emit updateCheckFailed(
            tr("O update assistido não está disponível no Flatpak; use o pacote do host."));
        return;
    }
    if (QStandardPaths::findExecutable(QStringLiteral("systemctl")).isEmpty()) {
        emit updateCheckFailed(
            tr("O update assistido exige systemd de usuário; use o caminho manual."));
        return;
    }
    const QString stateDir = Paths::stateDir() + QStringLiteral("/updates");
    if (!QDir().mkpath(stateDir)) {
        emit updateCheckFailed(tr("Não foi possível preparar o estado do update."));
        return;
    }
    QFile::setPermissions(stateDir,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QFile marker(stateDir + QStringLiteral("/gui-was-open"));
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit updateCheckFailed(tr("Não foi possível gravar o marcador do update."));
        return;
    }
    marker.close();
    QFile::setPermissions(marker.fileName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    const QString sibling = QCoreApplication::applicationDirPath() + QStringLiteral("/podstack");
    QString program = QFileInfo(sibling).isExecutable()
                          ? sibling
                          : QStandardPaths::findExecutable(QStringLiteral("podstack"));
    QStringList arguments{QStringLiteral("update"), QStringLiteral("--install"),
                          QStringLiteral("--runner"), QStringLiteral("--yes"),
                          QStringLiteral("--json")};
    if (resume) {
        arguments.append(QStringLiteral("--resume"));
    }
    if (!QProcess::startDetached(program, arguments)) {
        QFile::remove(marker.fileName());
        emit updateCheckFailed(tr("Não foi possível iniciar o runner do update."));
        return;
    }
    QCoreApplication::quit();
}

void Backend::loadSystemLogs(const QString &level, const QString &component, int sinceDays)
{
    if (m_systemLogsWatcher.isRunning()) {
        return;
    }
    m_systemLogsBusy = true;
    m_systemLogsError.clear();
    emit systemLogsChanged();

    LogReadOptions options;
    options.since = QDate::currentDate().addDays(-(qMax(1, sinceDays) - 1));
    options.level = level;
    options.component = component;
    m_pendingLogOptions = options;
    m_pendingLogComponent = component;
    m_systemLogsWatcher.setFuture(QtConcurrent::run([options]() {
        SystemLogsTask task;
        readUserLogs(options, &task.result, &task.error);
        return task;
    }));
}

void Backend::onSystemLogsFinished()
{
    m_systemLogsBusy = false;
    const SystemLogsTask task = m_systemLogsWatcher.result();
    if (!task.error.isEmpty()) {
        m_systemLogsError = task.error.message;
        emit systemLogsChanged();
        return;
    }

    m_lastLogs = task.result.lines;
    m_systemLogsTruncated = task.result.truncated;

    if (m_pendingLogComponent.isEmpty() || m_pendingLogComponent == QStringLiteral("helper") ||
        m_pendingLogComponent == QStringLiteral("package")) {
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
                if (!m_pendingLogComponent.isEmpty() && m_pendingLogComponent != helperComponent) {
                    continue;
                }
                const QString lineLevel =
                    object.value(QStringLiteral("level")).toString(QStringLiteral("info"));
                LogLine line;
                line.ts = object.value(QStringLiteral("ts")).toString();
                line.level = lineLevel;
                line.component = helperComponent;
                line.message = Redaction::text(object.value(QStringLiteral("message")).toString());
                line.raw = Redaction::text(raw);
                m_lastLogs.append(line);
            }
            if (reply.value(QStringLiteral("truncated")).toBool()) {
                m_systemLogsTruncated = true;
            }
        }
    }

    m_systemLogs.clear();
    for (const LogLine &line : m_lastLogs) {
        QVariantMap entry;
        entry.insert(QStringLiteral("ts"), line.ts);
        entry.insert(QStringLiteral("level"), line.level);
        entry.insert(QStringLiteral("component"), line.component);
        entry.insert(QStringLiteral("message"), line.message);
        m_systemLogs.append(entry);
    }
    emit systemLogsChanged();
}

bool Backend::exportSystemLogs(const QString &path)
{
    Error error;
    if (!exportLogs(m_lastLogs, path, &error)) {
        m_systemLogsError = error.message;
        emit systemLogsChanged();
        return false;
    }
    return true;
}

void Backend::clearSystemLogs()
{
    Error error;
    if (!clearUserLogs(&error)) {
        m_systemLogsError = error.message;
        emit systemLogsChanged();
        return;
    }
    loadSystemLogs();
}

void Backend::generateDiagnosticReport()
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

void Backend::clearUpdateState()
{
    QFile::remove(Paths::stateDir() + QStringLiteral("/updates/state.json"));
    m_updateStatus.clear();
    emit updateStatusChanged();
}

void Backend::openLogsFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::stateDir() + QStringLiteral("/logs")));
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
            m_compose.refresh();
        } else if (prefix == QStringLiteral("builds.")) {
            m_builds.refresh();
        }
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
