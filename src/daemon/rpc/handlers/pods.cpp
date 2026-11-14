// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon/rpc/handlers/Common.h"

using namespace PodStack::DaemonInternal;

namespace PodStack {

QJsonObject Daemon::podAction(const QString &method, const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString action = method.section(QLatin1Char('.'), 1);
    if (!m_podman.podAction(id, action, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("action"), action);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::podCreate(const QJsonObject &params, Error *error)
{
    const QJsonObject json = m_podman.createPod(params.value(QStringLiteral("name")).toString(),
                                                labelsFromParams(params), error);
    if (error && !error->isEmpty()) {
        return {};
    }
    return json;
}

QJsonObject Daemon::podRemove(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const bool force = params.value(QStringLiteral("force")).toBool(false);
    if (!m_podman.removePod(id, force, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("removed"), true);
    return result;
}

QJsonObject Daemon::podsList(Error *error)
{
    QJsonArray items;
    for (const PodInfo &pod : m_podman.listPods(error)) {
        items.append(pod.toJson());
    }
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("items"), items);
    result.insert(QStringLiteral("total"), items.size());
    return result;
}

} // namespace PodStack
