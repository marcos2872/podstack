#include "gui/controllers/Backend.h"

#include "core/config/Paths.h"
#include "core/error/Error.h"
#include "core/helper/HelperClient.h"
#include "core/logging/LogReader.h"
#include "core/logging/Logging.h"
#include "core/logging/Redaction.h"
#include "core/rpc/DaemonBootstrap.h"
#include "core/rpc/RpcClient.h"
#include "core/setup/SetupActions.h"
#include "core/setup/SetupPolicy.h"
#include "core/system/DiskUsage.h"
#include "core/system/Proxy.h"
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
    return QString::number(value, 'f', unit == 0 ? 0 : 1) + QLatin1Char(' ') +
           QString::fromLatin1(units[unit]);
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
                sshPort > 0 ? QStringLiteral("%1@127.0.0.1:%2").arg(sshUser).arg(sshPort)
                            : QStringLiteral("-"));
    return item;
}

} // namespace

Backend::Backend(QObject *parent) : QObject(parent)
{
    connect(&m_actionWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onActionFinished);
    connect(&m_serviceWatcher, &QFutureWatcher<int>::finished, this,
            &Backend::onServiceActivationFinished);
    connect(&m_helloWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onHelloFinished);
    connect(&m_containers, &ContainerController::connectionChanged, this,
            [this](bool connected, const QString &error) {
                if (connected) {
                    if (m_connected && m_error.isEmpty()) {
                        return;
                    }
                    m_connected = true;
                    m_error.clear();
                    emit stateChanged();
                    refreshDaemonInfo();
                    return;
                }
                if (!m_connected && m_error == error) {
                    return;
                }
                m_connected = false;
                m_error = error;
                emit stateChanged();
            });
    connect(&m_containers, &ContainerController::catalogDirty, this, &Backend::refreshCatalog);
    connect(&m_containers, &ContainerController::itemsChanged, this,
            [this] { emit containersChanged(m_containers.items()); });
    connect(&m_containers, &ContainerController::containerCreated, this,
            [this](const QString &id) { emit containerCreated(id); });
    connect(&m_containers, &ContainerController::terminalRequested, this,
            &Backend::terminalRequested);
    connect(&m_machines, &MachineController::terminalRequested, this, &Backend::terminalRequested);
    connect(&m_catalogWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onCatalogFinished);
    connect(&m_settings, &SettingsController::valuesChanged, this, [this] {
        m_updates.setEnabled(m_settings.values()
                                 .value(QStringLiteral("updates.mode"), QStringLiteral("check"))
                                 .toString() != QStringLiteral("off"));
    });
    connect(&m_updates, &UpdateController::userMessage, this,
            [this](const QString &kind, const QString &text) {
                m_eventKind = kind;
                m_eventMessage = text;
                emit eventsChanged();
                m_eventTimer.start();
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

    connect(&m_eventsStream, &StreamClient::eventReceived, this, [this](const QJsonObject &event) {
        if (event.value(QStringLiteral("Type")).toString() != QStringLiteral("container")) {
            return;
        }
        QString action = event.value(QStringLiteral("Action")).toString();
        if (action == QStringLiteral("died")) {
            action = QStringLiteral("die");
        } else if (action == QStringLiteral("stopped")) {
            action = QStringLiteral("stop");
        }
        if (action != QStringLiteral("start") && action != QStringLiteral("stop") &&
            action != QStringLiteral("die") && action != QStringLiteral("create") &&
            action != QStringLiteral("remove") && action != QStringLiteral("pause") &&
            action != QStringLiteral("unpause") && action != QStringLiteral("health_status") &&
            action != QStringLiteral("oom")) {
            return;
        }
        const QJsonObject actor = event.value(QStringLiteral("Actor")).toObject();
        const QJsonObject attributes = actor.value(QStringLiteral("Attributes")).toObject();
        const QString id = actor.value(QStringLiteral("ID")).toString();
        const QString name = actor.value(QStringLiteral("Attributes"))
                                 .toObject()
                                 .value(QStringLiteral("name"))
                                 .toString();
        const QString key = id + QLatin1Char('|') + action + QLatin1Char('|') +
                            QString::number(event.value(QStringLiteral("time")).toDouble());
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
            health = attributes.value(QStringLiteral("containerHealthStatus")).toString();
        }
        if (action == QStringLiteral("health_status")) {
            detail = health.isEmpty() ? QStringLiteral("health") : health;
        }
        m_eventMessage = (name.isEmpty() ? id.left(12) : name) + QLatin1Char(' ') + detail;
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
            exitCode = event.value(QStringLiteral("ContainerExitCode")).toVariant().toString();
        }
        payload.insert(QStringLiteral("health"), health);
        payload.insert(QStringLiteral("exitCode"), exitCode);
        emit containerEvent(payload);
    });

    m_eventsStream.subscribeEvents();

    QTimer::singleShot(0, this, &Backend::refresh);
    QTimer::singleShot(0, this, &Backend::refreshCatalog);
    QTimer::singleShot(0, this, [this] { m_machines.refresh(); });
    QTimer::singleShot(0, this, [this] { m_settings.refresh(); });
    QTimer::singleShot(0, this, [this] { refreshDaemonInfo(); });
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

void Backend::restartDaemonService()
{
    if (m_serviceActivating) {
        return;
    }
    m_serviceActivating = true;
    emit serviceActivationChanged();
    // `restart` derruba um daemon travado (o `start` não resolve quando o
    // socket aceita conexões e o processo não responde).
    m_serviceWatcher.setFuture(
        QtConcurrent::run([] { return static_cast<int>(DaemonBootstrap::restart()); }));
}

void Backend::onServiceActivationFinished()
{
    m_serviceActivating = false;
    emit serviceActivationChanged();
    const auto status = static_cast<DaemonBootstrap::Status>(m_serviceWatcher.result());
    if (status == DaemonBootstrap::Status::NotInstalled) {
        m_lastError = QStringLiteral("A unit podstack.service não está instalada. Rode: %1")
                          .arg(DaemonBootstrap::manualRemedy());
        emit lastErrorChanged();
    } else if (status == DaemonBootstrap::Status::Failed) {
        m_lastError = QStringLiteral("Não foi possível reiniciar o daemon. Rode: %1")
                          .arg(DaemonBootstrap::manualRemedy());
        emit lastErrorChanged();
    }
    refresh();
    refreshDaemonInfo();
    m_eventsStream.subscribeEvents();
}

void Backend::refreshDaemonInfo()
{
    if (m_helloWatcher.isRunning()) {
        return;
    }
    m_helloWatcher.setFuture(QtConcurrent::run([] {
        RpcClient client;
        Error error;
        const QJsonObject result = client.call(QStringLiteral("system.hello"), {}, &error, 5000);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("__error"), error.toJson()}};
        }
        return result;
    }));
}

void Backend::onHelloFinished()
{
    const QJsonObject result = m_helloWatcher.result();
    if (result.contains(QStringLiteral("__error"))) {
        return;
    }

    const QString daemonVersion = result.value(QStringLiteral("daemonVersion")).toString();
    const int apiVersion = result.value(QStringLiteral("apiVersion")).toInt();
    QString mismatch;
    if (apiVersion > 1) {
        mismatch = QStringLiteral("O daemon usa a API %1 e esta versão da GUI fala a API 1.")
                       .arg(apiVersion);
    } else if (!daemonVersion.isEmpty() && daemonVersion != QStringLiteral(PODSTACK_VERSION)) {
        mismatch = QStringLiteral("GUI %1 e daemon %2 — reinicie o daemon para alinhar.")
                       .arg(QStringLiteral(PODSTACK_VERSION), daemonVersion);
    }

    if (m_daemonVersion == daemonVersion && m_daemonMismatch == mismatch) {
        return;
    }
    m_daemonVersion = daemonVersion;
    m_daemonMismatch = mismatch;
    emit daemonInfoChanged();
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
            volumesSize +=
                static_cast<qint64>(value.toObject().value(QStringLiteral("Size")).toDouble());
        }
        qint64 containersSize = 0;
        for (const QJsonValue &value : df.value(QStringLiteral("Containers")).toArray()) {
            containersSize +=
                static_cast<qint64>(value.toObject().value(QStringLiteral("Size")).toDouble());
        }
        m_diskUsage.clear();
        m_diskUsage.insert(
            QStringLiteral("images"),
            humanBytes(static_cast<qint64>(df.value(QStringLiteral("ImagesSize")).toDouble())));
        m_diskUsage.insert(QStringLiteral("volumes"), humanBytes(volumesSize));
        m_diskUsage.insert(QStringLiteral("containers"), humanBytes(containersSize));
        emit diskUsageChanged();

        QJsonObject sections = result.value(QStringLiteral("sections")).toObject();
        double totalReclaimable = result.value(QStringLiteral("totalReclaimableBytes")).toDouble();
        if (sections.isEmpty()) {
            const QJsonObject normalized = normalizeDiskUsage(df, true);
            sections = normalized.value(QStringLiteral("sections")).toObject();
            totalReclaimable = normalized.value(QStringLiteral("totalReclaimableBytes")).toDouble();
        }
        m_prunePreview = sections.toVariantMap();
        m_prunePreview.insert(QStringLiteral("totalReclaimableBytes"), totalReclaimable);
        emit prunePreviewChanged();
    }

    startNextCatalog();
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

void Backend::openPath(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QString Backend::choosePath(const QString &mode, const QString &title, const QString &filter,
                            const QString &initialPath)
{
    const QString resolvedTitle = title.isEmpty() ? QStringLiteral("Selecionar arquivo") : title;
    const QString resolvedFilter =
        filter.isEmpty() ? QStringLiteral("Todos os arquivos (*)") : filter;
    if (mode == QStringLiteral("save")) {
        return QFileDialog::getSaveFileName(nullptr, resolvedTitle, initialPath, resolvedFilter);
    }
    if (mode == QStringLiteral("directory")) {
        return QFileDialog::getExistingDirectory(nullptr, resolvedTitle, initialPath);
    }
    return QFileDialog::getOpenFileName(nullptr, resolvedTitle, initialPath, resolvedFilter);
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
        const QString remediation = errorObject.value(QStringLiteral("remediation")).toString();
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
            m_machines.refresh();
        } else if (prefix == QStringLiteral("compose.")) {
            m_compose.refresh();
        } else if (prefix == QStringLiteral("builds.")) {
            m_builds.refresh();
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
