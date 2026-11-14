// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon/rpc/handlers/Common.h"

using namespace PodStack::DaemonInternal;

namespace PodStack {

QJsonObject Daemon::containerAction(const QString &method, const QJsonObject &params,
                                    Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    bool ok = false;
    if (method == QStringLiteral("containers.start")) {
        ok = m_podman.startContainer(id, error);
    } else if (method == QStringLiteral("containers.stop")) {
        ok = m_podman.stopContainer(
            id, params.value(QStringLiteral("timeoutSeconds")).toInt(10), error);
    } else if (method == QStringLiteral("containers.restart")) {
        ok = m_podman.restartContainer(
            id, params.value(QStringLiteral("timeoutSeconds")).toInt(10), error);
    } else if (method == QStringLiteral("containers.remove")) {
        ok = m_podman.removeContainer(id, params.value(QStringLiteral("force")).toBool(false),
                                      params.value(QStringLiteral("volumes")).toBool(false), error);
    } else if (method == QStringLiteral("containers.pause")) {
        ok = m_podman.pauseContainer(id, error);
    } else if (method == QStringLiteral("containers.unpause")) {
        ok = m_podman.unpauseContainer(id, error);
    } else if (method == QStringLiteral("containers.kill")) {
        ok = m_podman.killContainer(id, params.value(QStringLiteral("signal")).toString(), error);
    } else if (method == QStringLiteral("containers.rename")) {
        ok = m_podman.renameContainer(id, params.value(QStringLiteral("name")).toString(), error);
    } else if (method == QStringLiteral("containers.checkpoint")) {
        ok = m_podman.checkpointContainer(
            id, params.value(QStringLiteral("leaveRunning")).toBool(false),
            params.value(QStringLiteral("tcpEstablished")).toBool(false), error);
    } else if (method == QStringLiteral("containers.restore")) {
        ok = m_podman.restoreContainer(
            id, params.value(QStringLiteral("tcpEstablished")).toBool(false), error);
    }

    if (!ok) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::containerCreate(const QJsonObject &params, Error *error)
{
    ContainerCreateSpec spec;
    spec.image = params.value(QStringLiteral("image")).toString();
    spec.name = params.value(QStringLiteral("name")).toString();
    spec.command = params.value(QStringLiteral("command")).toString();
    spec.workDir = params.value(QStringLiteral("workDir")).toString();
    spec.user = params.value(QStringLiteral("user")).toString();
    spec.remove = params.value(QStringLiteral("remove")).toBool(false);
    spec.init = params.value(QStringLiteral("init")).toBool(false);
    spec.privileged = params.value(QStringLiteral("privileged")).toBool(false);
    const bool exposeLan = params.value(QStringLiteral("exposeLan")).toBool(false);

    if (spec.image.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe a imagem do container."));
        }
        return {};
    }

    const QJsonObject env = params.value(QStringLiteral("env")).toObject();
    for (auto it = env.constBegin(); it != env.constEnd(); ++it) {
        spec.env.insert(it.key(), it.value().toString());
    }

    QMap<QString, QStringList> publishedByProtocol;
    const QJsonArray ports = params.value(QStringLiteral("ports")).toArray();
    for (const QJsonValue &value : ports) {
        const QJsonObject port = value.toObject();
        PortMapping mapping;
        mapping.hostPort = port.value(QStringLiteral("hostPort")).toInt();
        mapping.containerPort = port.value(QStringLiteral("containerPort")).toInt();
        mapping.protocol = port.value(QStringLiteral("protocol"))
                               .toString(QStringLiteral("tcp"))
                               .trimmed()
                               .toLower();
        mapping.hostIp = port.value(QStringLiteral("hostIp"))
                             .toString(exposeLan ? QStringLiteral("0.0.0.0")
                                                 : QStringLiteral("127.0.0.1"));
        if (exposeLan) {
            mapping.hostIp = QStringLiteral("0.0.0.0");
        }
        if (mapping.hostPort > 0 && mapping.containerPort > 0) {
            if (exposeLan) {
                if (mapping.protocol != QStringLiteral("tcp")
                    && mapping.protocol != QStringLiteral("udp")) {
                    if (error) {
                        *error = Error::make(
                            QStringLiteral("PODSTACK_INVALID_PARAMS"),
                            QStringLiteral("Expor na LAN aceita apenas portas tcp ou udp."));
                    }
                    return {};
                }
                if (mapping.hostPort > 65535) {
                    if (error) {
                        *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                             QStringLiteral("Porta do host inválida: %1.")
                                                 .arg(mapping.hostPort));
                    }
                    return {};
                }
                publishedByProtocol[mapping.protocol].append(
                    QString::number(mapping.hostPort));
            }
            spec.ports.append(mapping);
        }
    }

    if (exposeLan) {
        if (publishedByProtocol.isEmpty()) {
            if (error) {
                *error = Error::make(
                    QStringLiteral("PODSTACK_INVALID_PARAMS"),
                    QStringLiteral("Informe ao menos uma porta publicada para expor na LAN."));
            }
            return {};
        }
        int totalPorts = 0;
        for (const QStringList &hostPorts : publishedByProtocol) {
            totalPorts += hostPorts.size();
        }
        if (totalPorts > 32) {
            if (error) {
                *error = Error::make(
                    QStringLiteral("PODSTACK_INVALID_PARAMS"),
                    QStringLiteral("No máximo 32 portas podem ser expostas por container."));
            }
            return {};
        }
        if (!m_helper.available()) {
            if (error) {
                *error = helperRequiredError();
            }
            return {};
        }
    }

    const QJsonArray mounts = params.value(QStringLiteral("mounts")).toArray();
    for (const QJsonValue &value : mounts) {
        const QJsonObject mount = value.toObject();
        MountMapping mapping;
        mapping.source = mount.value(QStringLiteral("source")).toString();
        mapping.destination = mount.value(QStringLiteral("destination")).toString();
        mapping.readOnly = mount.value(QStringLiteral("readOnly")).toBool(false);
        if (!mapping.source.isEmpty() && !mapping.destination.isEmpty()) {
            spec.mounts.append(mapping);
        }
    }

    const QJsonObject labels = params.value(QStringLiteral("labels")).toObject();
    for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
        spec.labels.insert(it.key(), it.value().toString());
    }

    applyProxyEnvironment(spec);
    applyCaTrustMount(spec);

    const QString id = m_podman.createContainer(spec, error);
    if (id.isEmpty()) {
        return {};
    }

    const bool start = params.value(QStringLiteral("start")).toBool(true);
    if (start && !m_podman.startContainer(id, error)) {
        return {};
    }

    if (exposeLan) {
        QStringList openedProtocols;
        for (auto it = publishedByProtocol.constBegin(); it != publishedByProtocol.constEnd();
             ++it) {
            QString helperError;
            const bool ok = m_helper.call(
                QStringLiteral("OpenPorts"),
                QVariantList{QVariant::fromValue(it.value()), it.key()}, &helperError);
            if (!ok) {
                for (const QString &protocol : openedProtocols) {
                    QString ignored;
                    (void)m_helper.call(
                        QStringLiteral("ClosePorts"),
                        QVariantList{QVariant::fromValue(publishedByProtocol.value(protocol)),
                                     protocol},
                        &ignored);
                }
                Error cleanupError;
                (void)m_podman.removeContainer(id, true, false, &cleanupError);
                if (error) {
                    *error = Error::make(
                        QStringLiteral("PODSTACK_IO_ERROR"),
                        QStringLiteral("O container foi criado, mas o firewall não abriu as "
                                       "portas; a criação foi desfeita."),
                        helperError,
                        QStringLiteral("Verifique o firewalld/ufw e tente novamente."));
                }
                return {};
            }
            openedProtocols.append(it.key());
        }
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("started"), start);
    result.insert(QStringLiteral("exposeLan"), exposeLan);
    return result;
}

QJsonObject Daemon::containerDuplicate(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QJsonObject json = m_podman.inspectContainer(id, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    const QJsonObject config = json.value(QStringLiteral("Config")).toObject();
    const QString image = config.value(QStringLiteral("Image")).toString();
    if (image.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_RESPONSE"),
                                 QStringLiteral("Inspect sem imagem para duplicar."));
        }
        return {};
    }

    ContainerCreateSpec spec;
    spec.image = image;
    spec.name = params.value(QStringLiteral("name")).toString();
    if (spec.name.isEmpty()) {
        spec.name = id.left(12) + QStringLiteral("-copy");
    }
    spec.workDir = config.value(QStringLiteral("WorkingDir")).toString();
    spec.user = config.value(QStringLiteral("User")).toString();

    QStringList command;
    const QJsonArray commandArray = config.value(QStringLiteral("Cmd")).toArray();
    for (const QJsonValue &value : commandArray) {
        command.append(value.toString());
    }
    spec.command = command.join(QLatin1Char(' '));

    const QJsonArray env = config.value(QStringLiteral("Env")).toArray();
    for (const QJsonValue &value : env) {
        const QString entry = value.toString();
        const int equals = entry.indexOf(QLatin1Char('='));
        if (equals > 0) {
            spec.env.insert(entry.left(equals), entry.mid(equals + 1));
        }
    }

    applyProxyEnvironment(spec);

    const QJsonObject bindings = json.value(QStringLiteral("HostConfig"))
                                     .toObject()
                                     .value(QStringLiteral("PortBindings"))
                                     .toObject();
    for (auto it = bindings.constBegin(); it != bindings.constEnd(); ++it) {
        const QStringList keyParts = it.key().split(QLatin1Char('/'));
        if (keyParts.size() != 2) {
            continue;
        }
        const int containerPort = keyParts.at(0).toInt();
        const QJsonArray hosts = it.value().toArray();
        for (const QJsonValue &hostValue : hosts) {
            const QJsonObject host = hostValue.toObject();
            const int hostPort = host.value(QStringLiteral("HostPort")).toString().toInt();
            if (containerPort > 0) {
                spec.ports.append({hostPort, containerPort, keyParts.at(1),
                                   host.value(QStringLiteral("HostIp"))
                                       .toString(QStringLiteral("0.0.0.0"))});
            }
        }
    }

    const QJsonArray mounts = json.value(QStringLiteral("Mounts")).toArray();
    for (const QJsonValue &value : mounts) {
        const QJsonObject mount = value.toObject();
        const QString source = mount.value(QStringLiteral("Source")).toString();
        const QString destination = mount.value(QStringLiteral("Destination")).toString();
        if (!source.isEmpty() && !destination.isEmpty()) {
            spec.mounts.append({source, destination, !mount.value(QStringLiteral("RW")).toBool()});
        }
    }

    const QJsonObject labels = config.value(QStringLiteral("Labels")).toObject();
    for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
        spec.labels.insert(it.key(), it.value().toString());
    }
    applyCaTrustMount(spec);

    const QString newId = m_podman.createContainer(spec, error);
    if (newId.isEmpty()) {
        return {};
    }
    if (!m_podman.startContainer(newId, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), newId);
    result.insert(QStringLiteral("source"), id);
    result.insert(QStringLiteral("started"), true);
    return result;
}

QJsonObject Daemon::containerExec(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QJsonArray commandArray = params.value(QStringLiteral("cmd")).toArray();
    if (commandArray.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o comando a executar."));
        }
        return {};
    }

    QStringList command;
    for (const QJsonValue &value : commandArray) {
        command.append(value.toString());
    }

    ExecResult execResult;
    if (!m_podman.execOnce(id, command, &execResult, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("exitCode"), execResult.exitCode);
    result.insert(QStringLiteral("stdout"), QString::fromUtf8(execResult.output));
    return result;
}

QJsonObject Daemon::containerExposeLan(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }
    const bool expose = params.value(QStringLiteral("expose")).toBool(false);

    const QJsonObject json = m_podman.inspectContainer(id, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    const QJsonObject bindings =
        json.value(QStringLiteral("HostConfig"))
            .toObject()
            .value(QStringLiteral("PortBindings"))
            .toObject();

    QMap<QString, QStringList> portsByProtocol;
    QJsonArray portSpecs;
    bool anyPort = false;
    bool wildcardOnly = true;
    for (auto it = bindings.constBegin(); it != bindings.constEnd(); ++it) {
        const QStringList keyParts = it.key().split(QLatin1Char('/'));
        if (keyParts.size() != 2) {
            continue;
        }
        const QString protocol = keyParts.at(1).trimmed().toLower();
        const QJsonArray hosts = it.value().toArray();
        for (const QJsonValue &hostValue : hosts) {
            const QJsonObject host = hostValue.toObject();
            const int hostPort = host.value(QStringLiteral("HostPort")).toString().toInt();
            if (hostPort <= 0) {
                continue;
            }
            anyPort = true;
            const QString hostIp = host.value(QStringLiteral("HostIp")).toString();
            if (!isWildcardHostIp(hostIp)) {
                wildcardOnly = false;
            }
            if (protocol != QStringLiteral("tcp") && protocol != QStringLiteral("udp")) {
                if (error) {
                    *error = Error::make(
                        QStringLiteral("PODSTACK_UNSUPPORTED"),
                        QStringLiteral("O firewall da LAN só gerencia portas tcp e udp."));
                }
                return {};
            }
            portsByProtocol[protocol].append(QString::number(hostPort));
            portSpecs.append(QStringLiteral("%1/%2").arg(hostPort).arg(protocol));
        }
    }

    if (!anyPort) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_INVALID_PARAMS"),
                QStringLiteral("O container não publica portas."),
                QString(), QStringLiteral("Publique portas ao criar o container."));
        }
        return {};
    }

    if (!wildcardOnly) {
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_UNSUPPORTED"),
                QStringLiteral("As portas publicadas não estão em 0.0.0.0. Recrie o container "
                               "com a opção 'Expor na LAN'."),
                QStringLiteral("O bind atual aponta para o loopback ou um IP específico."),
                QStringLiteral("Remova o container e crie-o novamente com 'Expor na LAN'."));
        }
        return {};
    }

    if (!m_helper.available()) {
        if (error) {
            *error = helperRequiredError();
        }
        return {};
    }

    const QString method = expose ? QStringLiteral("OpenPorts") : QStringLiteral("ClosePorts");
    QStringList changedProtocols;
    for (auto it = portsByProtocol.constBegin(); it != portsByProtocol.constEnd(); ++it) {
        QString helperError;
        const bool ok = m_helper.call(
            method, QVariantList{QVariant::fromValue(it.value()), it.key()}, &helperError);
        if (!ok) {
            if (expose) {
                for (const QString &protocol : changedProtocols) {
                    QString ignored;
                    (void)m_helper.call(
                        QStringLiteral("ClosePorts"),
                        QVariantList{QVariant::fromValue(portsByProtocol.value(protocol)),
                                     protocol},
                        &ignored);
                }
            }
            if (error) {
                *error = Error::make(
                    QStringLiteral("PODSTACK_IO_ERROR"),
                    QStringLiteral("Não foi possível %1 as portas no firewall.")
                        .arg(expose ? QStringLiteral("abrir") : QStringLiteral("fechar")),
                    helperError,
                    QStringLiteral("Verifique o firewalld/ufw e tente novamente."));
            }
            return {};
        }
        changedProtocols.append(it.key());
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("expose"), expose);
    result.insert(QStringLiteral("ports"), portSpecs);
    return result;
}

QJsonObject Daemon::containerFilesDownload(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString path = params.value(QStringLiteral("path")).toString();
    const QString destination = params.value(QStringLiteral("destination")).toString();
    if (path.isEmpty() || destination.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o caminho e o destino."));
        }
        return {};
    }

    if (!m_podman.downloadFile(id, path, destination, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("path"), path);
    result.insert(QStringLiteral("destination"), destination);
    return result;
}

QJsonObject Daemon::containerFilesList(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString path = params.value(QStringLiteral("path")).toString(QStringLiteral("/"));
    const QVector<FileEntry> entries = m_podman.listFiles(id, path, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonArray items;
    for (const FileEntry &entry : entries) {
        items.append(entry.toJson());
    }

    QJsonObject result;
    result.insert(QStringLiteral("path"), path);
    result.insert(QStringLiteral("entries"), items);
    return result;
}

QJsonObject Daemon::containerFilesUpload(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString path = params.value(QStringLiteral("path")).toString();
    const QString sourcePath = params.value(QStringLiteral("sourcePath")).toString();
    if (path.isEmpty() || sourcePath.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o destino e o arquivo de origem."));
        }
        return {};
    }

    if (!m_podman.uploadFile(id, path, sourcePath, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("path"), path);
    result.insert(QStringLiteral("sourcePath"), sourcePath);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::containerInspect(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QJsonObject json = m_podman.inspectContainer(id, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("json"), json);
    return result;
}

QJsonObject Daemon::containersList(const QJsonObject &params, Error *error)
{
    const bool all = params.value(QStringLiteral("all")).toBool(false);
    const QString state = params.value(QStringLiteral("state")).toString();
    const QVector<ContainerInfo> containers = m_podman.listContainers(all, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonArray items;
    for (const ContainerInfo &container : containers) {
        if (!state.isEmpty() && state != QStringLiteral("all")) {
            if (state == QStringLiteral("running") && container.state != QStringLiteral("running")) {
                continue;
            }
            if (state == QStringLiteral("paused") && container.state != QStringLiteral("paused")) {
                continue;
            }
            if (state == QStringLiteral("stopped")
                && container.state != QStringLiteral("exited")
                && container.state != QStringLiteral("created")
                && container.state != QStringLiteral("dead")
                && container.state != QStringLiteral("stopped")) {
                continue;
            }
        }
        items.append(container.toJson());
    }

    QJsonObject result;
    result.insert(QStringLiteral("items"), items);
    result.insert(QStringLiteral("total"), items.size());
    return result;
}

QJsonObject Daemon::subscribeEvents(const Notify &notify, Error *error)
{
    if (error) {
        *error = {};
    }

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);

    (void)QtConcurrent::run([this, subscriptionId, subscription, notify] {
        QByteArray buffer;
        Error streamError;

        const auto onData = [&](const QByteArray &data) {
            buffer.append(data);
            int newline = -1;
            while ((newline = buffer.indexOf('\n')) >= 0) {
                const QByteArray line = buffer.left(newline);
                buffer.remove(0, newline + 1);
                const QJsonDocument document = QJsonDocument::fromJson(line);
                if (document.isObject()) {
                    notify(QStringLiteral("event"), document.object());
                }
                if (subscription->cancel.load()) {
                    return false;
                }
            }
            return !subscription->cancel.load();
        };

        m_podman.streamEvents(onData, &subscription->cancel, &streamError);
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    return result;
}

QJsonObject Daemon::subscribeLogs(const QJsonObject &params, const Notify &notify, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const int tail = params.value(QStringLiteral("tail")).toInt(200);
    const bool follow = params.value(QStringLiteral("follow")).toBool(true);

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);

    (void)QtConcurrent::run([this, id, tail, follow, subscriptionId, subscription, notify] {
        QByteArray buffer;
        Error streamError;

        const auto onData = [&](const QByteArray &data) {
            buffer.append(data);
            int newline = -1;
            while ((newline = buffer.indexOf('\n')) >= 0) {
                QByteArray line = buffer.left(newline);
                buffer.remove(0, newline + 1);
                if (line.endsWith('\r')) {
                    line.chop(1);
                }
                QJsonObject payload;
                payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
                payload.insert(QStringLiteral("line"), QString::fromUtf8(line));
                notify(QStringLiteral("containers.logs.delta"), payload);
                if (subscription->cancel.load()) {
                    return false;
                }
            }
            return !subscription->cancel.load();
        };

        m_podman.streamLogs(id, tail, follow, onData, &subscription->cancel, &streamError);
        if (!streamError.isEmpty() && !subscription->cancel.load()) {
            QJsonObject payload;
            payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
            payload.insert(QStringLiteral("error"), streamError.toJson());
            notify(QStringLiteral("containers.logs.error"), payload);
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    return result;
}

QJsonObject Daemon::subscribeStats(const QJsonObject &params, const Notify &notify, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const int interval = params.value(QStringLiteral("intervalSeconds")).toInt(1);
    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);

    (void)QtConcurrent::run([this, id, interval, subscriptionId, subscription, notify] {
        Error streamError;
        const auto onLine = [&](const QByteArray &line) {
            const QJsonDocument document = QJsonDocument::fromJson(line);
            if (document.isObject()) {
                QJsonObject payload = document.object();
                payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
                notify(QStringLiteral("containers.stats.delta"), payload);
            }
            return !subscription->cancel.load();
        };

        m_podman.streamStats(id, interval, onLine, &subscription->cancel, &streamError);
        if (!streamError.isEmpty() && !subscription->cancel.load()) {
            QJsonObject payload;
            payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
            payload.insert(QStringLiteral("error"), streamError.toJson());
            notify(QStringLiteral("containers.stats.error"), payload);
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    return result;
}

QJsonObject Daemon::subscribeSystemStats(const QJsonObject &params, const Notify &notify,
                                         Error *error)
{
    if (error) {
        *error = {};
    }

    const int interval = qBound(1, params.value(QStringLiteral("intervalSeconds")).toInt(2), 10);
    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);

    (void)QtConcurrent::run([this, interval, subscriptionId, subscription, notify] {
        StatsCollector collector;
        while (!subscription->cancel.load()) {
            Error sampleError;
            QJsonObject sample = collector.sample(m_podman, &sampleError);
            if (!sample.isEmpty()) {
                sample.insert(QStringLiteral("subscriptionId"), subscriptionId);
                notify(QStringLiteral("system.stats.delta"), sample);
            } else if (!sampleError.isEmpty()) {
                QJsonObject payload;
                payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
                payload.insert(QStringLiteral("error"), sampleError.toJson());
                notify(QStringLiteral("system.stats.error"), payload);
                break;
            }

            for (int tick = 0; tick < interval * 10; ++tick) {
                if (subscription->cancel.load()) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    return result;
}

} // namespace PodStack
