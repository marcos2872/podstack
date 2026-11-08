#include "gui/Backend.h"

#include "core/Config.h"
#include "core/Error.h"
#include "core/RpcClient.h"
#include "gui/TerminalWindow.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
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

} // namespace

Backend::Backend(QObject *parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onRefreshFinished);
    connect(&m_actionWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onActionFinished);
    connect(&m_filesWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onFilesFinished);
    connect(&m_catalogWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onCatalogFinished);

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

    connect(&m_eventsStream, &StreamClient::eventReceived, this,
            [this](const QJsonObject &event) {
                if (event.value(QStringLiteral("Type")).toString()
                    != QStringLiteral("container")) {
                    return;
                }
                const QString action = event.value(QStringLiteral("Action")).toString();
                if (action != QStringLiteral("start") && action != QStringLiteral("stop")
                    && action != QStringLiteral("die") && action != QStringLiteral("create")
                    && action != QStringLiteral("remove")
                    && action != QStringLiteral("pause")
                    && action != QStringLiteral("unpause")) {
                    return;
                }
                const QJsonObject actor = event.value(QStringLiteral("Actor")).toObject();
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
                m_eventMessage = (name.isEmpty() ? id.left(12) : name)
                                 + QLatin1Char(' ') + action;
                emit eventsChanged();
                m_eventTimer.start();
            });

    m_eventsStream.subscribeEvents();

    QTimer::singleShot(0, this, &Backend::refresh);
    QTimer::singleShot(0, this, &Backend::refreshCatalog);
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

void Backend::refreshCatalog()
{
    refreshImages();
    refreshVolumes();
    refreshNetworks();
    refreshPods();
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

    m_catalogWatcher.setFuture(QtConcurrent::run([method] {
        RpcClient client;
        Error error;
        const QJsonObject result = client.call(method, {}, &error);
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
        item.insert(QStringLiteral("status"), object.value(QStringLiteral("status")).toString());

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
    m_memHistory.clear();
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
    values.insert(QStringLiteral("theme"),
                  config.get(QStringLiteral("app.theme")).toString());
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

void Backend::saveTheme(const QString &theme)
{
    if (theme != QStringLiteral("system") && theme != QStringLiteral("dark")
        && theme != QStringLiteral("light")) {
        m_lastError = QStringLiteral("Tema inválido.");
        emit lastErrorChanged();
        return;
    }

    Config config = Config::load();
    config.set(QStringLiteral("app.theme"), theme);
    QString error;
    if (!config.save(&error)) {
        m_lastError = error;
        emit lastErrorChanged();
    }
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

    QJsonObject params;
    params.insert(QStringLiteral("contextPath"), contextPath);
    params.insert(QStringLiteral("tag"), tag);
    params.insert(QStringLiteral("dockerfile"), dockerfile);
    params.insert(QStringLiteral("platform"),
                  spec.value(QStringLiteral("platform")).toString());
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

void Backend::runAction(const QString &method, const QJsonObject &params, int timeoutMs)
{
    if (m_actionWatcher.isRunning()) {
        return;
    }

    m_actionWatcher.setFuture(QtConcurrent::run([method, params, timeoutMs] {
        RpcClient client;
        Error error;
        QJsonObject result = client.call(method, params, &error, timeoutMs);
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
    refreshCatalog();
}

} // namespace PodStack
