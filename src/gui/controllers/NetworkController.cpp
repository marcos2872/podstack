// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/controllers/NetworkController.h"

#include <QJsonObject>

namespace PodStack {

NetworkController::NetworkController(QObject *parent) : CatalogController(parent) {}

QVariantList NetworkController::parseItems(const QJsonArray &items) const
{
    QVariantList parsed;
    parsed.reserve(items.size());
    for (const QJsonValue &value : items) {
        const QJsonObject network = value.toObject();
        QVariantMap item;
        item.insert(QStringLiteral("name"), network.value(QStringLiteral("name")).toString());
        item.insert(QStringLiteral("driver"), network.value(QStringLiteral("driver")).toString());
        item.insert(QStringLiteral("subnet"), network.value(QStringLiteral("subnet")).toString());
        item.insert(QStringLiteral("gateway"), network.value(QStringLiteral("gateway")).toString());
        item.insert(QStringLiteral("containers"),
                    network.value(QStringLiteral("containers")).toInt());
        parsed.append(item);
    }
    return parsed;
}

void NetworkController::createNetwork(const QString &name, const QString &subnet,
                                      const QString &gateway)
{
    runAction(QStringLiteral("networks.create"), QJsonObject{{QStringLiteral("name"), name},
                                                             {QStringLiteral("subnet"), subnet},
                                                             {QStringLiteral("gateway"), gateway}});
}

void NetworkController::removeNetwork(const QString &name, bool force)
{
    runAction(QStringLiteral("networks.remove"),
              QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("force"), force}});
}

void NetworkController::connectNetwork(const QString &name, const QString &containerId)
{
    runAction(
        QStringLiteral("networks.connect"),
        QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("containerId"), containerId}});
}

void NetworkController::disconnectNetwork(const QString &name, const QString &containerId)
{
    runAction(
        QStringLiteral("networks.disconnect"),
        QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("containerId"), containerId}});
}

void NetworkController::batchNetworks(const QStringList &names, bool force)
{
    runJob(
        QStringLiteral("networks.batch"), {},
        [names, force] {
            return runBatchCalls(QStringLiteral("networks."), QStringLiteral("name"),
                                 QStringLiteral("remove"), names, force, {});
        },
        {}, 120000);
}

void NetworkController::onActionFinished(const QString &method, const QJsonObject &params,
                                         const QJsonObject &result)
{
    ControllerBase::onActionFinished(method, params, result);
    if (isErrorResult(result)) {
        return;
    }
    refresh();
}

} // namespace PodStack
