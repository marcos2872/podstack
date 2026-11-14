// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon/rpc/handlers/Common.h"

using namespace PodStack::DaemonInternal;

namespace PodStack {

QJsonObject Daemon::networkConnect(const QJsonObject &params, Error *error)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    const QString containerId = params.value(QStringLiteral("containerId")).toString();
    if (name.isEmpty() || containerId.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe a rede e o container."));
        }
        return {};
    }

    if (!m_podman.connectNetwork(name, containerId, aliasesFromParams(params), error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("containerId"), containerId);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::networkCreate(const QJsonObject &params, Error *error)
{
    const QJsonObject json =
        m_podman.createNetwork(params.value(QStringLiteral("name")).toString(),
                               params.value(QStringLiteral("driver")).toString(),
                               params.value(QStringLiteral("subnet")).toString(),
                               params.value(QStringLiteral("gateway")).toString(),
                               params.value(QStringLiteral("ipv6")).toBool(false),
                               labelsFromParams(params), error);
    if (error && !error->isEmpty()) {
        return {};
    }
    return json;
}

QJsonObject Daemon::networkDisconnect(const QJsonObject &params, Error *error)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    const QString containerId = params.value(QStringLiteral("containerId")).toString();
    if (name.isEmpty() || containerId.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe a rede e o container."));
        }
        return {};
    }

    if (!m_podman.disconnectNetwork(name, containerId, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("containerId"), containerId);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::networkRemove(const QJsonObject &params, Error *error)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o nome da rede."));
        }
        return {};
    }

    const bool force = params.value(QStringLiteral("force")).toBool(false);
    if (!m_podman.removeNetwork(name, force, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("removed"), true);
    return result;
}

QJsonObject Daemon::networksList(Error *error)
{
    QJsonArray items;
    for (const NetworkInfo &network : m_podman.listNetworks(error)) {
        items.append(network.toJson());
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
