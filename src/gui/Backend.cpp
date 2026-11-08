#include "gui/Backend.h"

#include "core/Config.h"
#include "core/Error.h"
#include "core/RpcClient.h"
#include "gui/TerminalWindow.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <QtConcurrent>

namespace PodStack {

Backend::Backend(QObject *parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onRefreshFinished);
    connect(&m_actionWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onActionFinished);
    connect(&m_filesWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onFilesFinished);

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
        emit statsChanged();
    });

    connect(&m_stream, &StreamClient::error, this, [this](const QString &message) {
        m_lastError = message;
        emit lastErrorChanged();
    });

    QTimer::singleShot(0, this, &Backend::refresh);
}

QString Backend::version() const
{
    return QStringLiteral(PODSTACK_VERSION);
}

void Backend::refresh()
{
    if (m_watcher.isRunning()) {
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
        emit containersChanged({});
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

        QStringList ports;
        const QJsonArray portArray = object.value(QStringLiteral("ports")).toArray();
        for (const QJsonValue &port : portArray) {
            ports.append(port.toString());
        }
        item.insert(QStringLiteral("ports"), ports.join(QStringLiteral(", ")));
        items.append(item);
    }

    emit containersChanged(items);
    m_containers = items;
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
            port.insert(QStringLiteral("hostIp"), QStringLiteral("0.0.0.0"));
        } else {
            port.insert(QStringLiteral("hostIp"), parts.at(0));
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
    emit statsChanged();
    m_stream.subscribeStats(id);
}

void Backend::unsubscribeStats()
{
    m_stream.unsubscribe();
}

QVariantMap Backend::settings() const
{
    const Config config = Config::load();
    QVariantMap values;
    values.insert(QStringLiteral("engineMode"),
                  config.get(QStringLiteral("engine.mode")).toString());
    return values;
}

void Backend::saveEngineMode(const QString &mode)
{
    if (mode != QStringLiteral("rootless") && mode != QStringLiteral("rootful")) {
        m_lastError = QStringLiteral("Modo de engine inválido.");
        emit lastErrorChanged();
        return;
    }

    Config config = Config::load();
    config.set(QStringLiteral("engine.mode"), mode);

    QString error;
    if (!config.save(&error)) {
        m_lastError = error;
        emit lastErrorChanged();
        return;
    }

    m_lastError.clear();
    emit lastErrorChanged();
    emit actionFinished(true, {});
}

void Backend::openTerminal(const QString &id)
{
    if (id.isEmpty()) {
        m_lastError = QStringLiteral("Container inválido.");
        emit lastErrorChanged();
        return;
    }

    auto *window = new TerminalWindow(id);
    window->show();
    window->raise();
    window->activateWindow();
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
    if (action != QStringLiteral("start") && action != QStringLiteral("stop")
        && action != QStringLiteral("restart") && action != QStringLiteral("remove")) {
        m_lastError = QStringLiteral("Ação de lote inválida.");
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
        return;
    }

    QStringList ids;
    for (const QVariant &value : m_containers) {
        const QString id = value.toMap().value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) {
            ids.append(id);
        }
    }
    if (ids.isEmpty()) {
        return;
    }

    if (m_actionWatcher.isRunning()) {
        return;
    }

    m_actionWatcher.setFuture(QtConcurrent::run([action, ids] {
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
    }));
}

void Backend::runAction(const QString &method, const QJsonObject &params)
{
    if (m_actionWatcher.isRunning()) {
        return;
    }

    m_actionWatcher.setFuture(QtConcurrent::run([method, params] {
        RpcClient client;
        Error error;
        QJsonObject result = client.call(method, params, &error);
        if (!error.isEmpty()) {
            QJsonObject payload;
            payload.insert(QStringLiteral("error"), error.toJson());
            return payload;
        }
        return result;
    }));
}

void Backend::onActionFinished()
{
    const QJsonObject result = m_actionWatcher.result();
    if (result.contains(QStringLiteral("error"))) {
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        m_lastError = errorObject.value(QStringLiteral("message")).toString();
        emit lastErrorChanged();
        emit actionFinished(false, m_lastError);
        return;
    }

    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit lastErrorChanged();
    }
    emit actionFinished(true, {});
    refresh();
}

} // namespace PodStack
