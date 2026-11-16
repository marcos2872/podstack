// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon/Pools.h"
#include "daemon/rpc/handlers/Common.h"

using namespace PodStack::DaemonInternal;

namespace PodStack {

QJsonObject Daemon::composeAction(const QString &method, const QJsonObject &params, Error *error)
{
    const QString projectPath = params.value(QStringLiteral("projectPath")).toString();
    if (projectPath.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o diretório do projeto Compose."));
        }
        return {};
    }

    const QString action = method.section(QLatin1Char('.'), 1);
    const QString service = params.value(QStringLiteral("service")).toString();
    bool ok = false;
    if (action == QStringLiteral("up")) {
        ok = m_compose.up(projectPath, params.value(QStringLiteral("detach")).toBool(true), error);
    } else if (action == QStringLiteral("down")) {
        ok = m_compose.down(projectPath, params.value(QStringLiteral("volumes")).toBool(false),
                            error);
    } else if (action == QStringLiteral("restart")) {
        ok = m_compose.restart(projectPath, service, error);
    } else if (action == QStringLiteral("stop")) {
        ok = m_compose.stop(projectPath, service, error);
    } else if (action == QStringLiteral("start")) {
        ok = m_compose.start(projectPath, service, error);
    } else if (error) {
        *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                             QStringLiteral("Ação Compose inválida."), action);
    }
    if (!ok) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("projectPath"), projectPath);
    result.insert(QStringLiteral("service"), service);
    result.insert(QStringLiteral("action"), action);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::composeProjects(Error *error)
{
    QJsonArray items;
    for (const ComposeProjectInfo &project : m_compose.listProjects(error)) {
        items.append(project.toJson());
    }
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("items"), items);
    result.insert(QStringLiteral("total"), items.size());
    return result;
}

QJsonObject Daemon::subscribeComposeLogs(const QJsonObject &params, const Notify &notify,
                                         Error *error)
{
    const QString project = params.value(QStringLiteral("project")).toString();
    if (project.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o projeto Compose."));
        }
        return {};
    }
    const QString serviceFilter = params.value(QStringLiteral("service")).toString();

    const QVector<ContainerInfo> containers = m_podman.listContainers(true, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    QVector<QPair<QString, QString>> targets;
    for (const ContainerInfo &container : containers) {
        if (container.labels.value(QStringLiteral("com.docker.compose.project")) != project) {
            continue;
        }
        const QString service =
            container.labels.value(QStringLiteral("com.docker.compose.service"));
        if (!serviceFilter.isEmpty() && service != serviceFilter) {
            continue;
        }
        targets.append({container.id, service});
    }
    if (targets.isEmpty()) {
        if (error) {
            *error =
                Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                            QStringLiteral("Nenhum container do projeto encontrado."), project);
        }
        return {};
    }

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);
    auto remaining = std::make_shared<std::atomic_int>(targets.size());

    for (const auto &target : targets) {
        const QString containerId = target.first;
        const QString service = target.second;
        (void) QtConcurrent::run(
            PodStack::backgroundPool(),
            [this, project, service, containerId, subscriptionId, subscription, remaining, notify] {
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
                        payload.insert(QStringLiteral("project"), project);
                        payload.insert(QStringLiteral("service"), service);
                        payload.insert(QStringLiteral("containerId"), containerId);
                        payload.insert(QStringLiteral("line"), QString::fromUtf8(line));
                        notify(QStringLiteral("compose.logs.delta"), payload);
                        if (subscription->cancel.load()) {
                            return false;
                        }
                    }
                    return !subscription->cancel.load();
                };

                m_podman.streamLogs(containerId, 200, true, onData, &subscription->cancel,
                                    &streamError);
                if (--(*remaining) == 0) {
                    removeSubscription(subscriptionId);
                }
            });
    }

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    result.insert(QStringLiteral("project"), project);
    return result;
}

} // namespace PodStack
