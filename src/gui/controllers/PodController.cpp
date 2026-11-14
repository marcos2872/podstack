// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/controllers/PodController.h"

#include <QJsonObject>

namespace PodStack {

PodController::PodController(QObject *parent) : CatalogController(parent) {}

QVariantList PodController::parseItems(const QJsonArray &items) const
{
    QVariantList parsed;
    parsed.reserve(items.size());
    for (const QJsonValue &value : items) {
        const QJsonObject pod = value.toObject();
        QVariantMap item;
        item.insert(QStringLiteral("id"), pod.value(QStringLiteral("id")).toString());
        item.insert(QStringLiteral("name"), pod.value(QStringLiteral("name")).toString());
        item.insert(QStringLiteral("status"), pod.value(QStringLiteral("status")).toString());
        item.insert(QStringLiteral("containers"), pod.value(QStringLiteral("containers")).toInt());
        item.insert(QStringLiteral("created"), pod.value(QStringLiteral("created")).toString());
        parsed.append(item);
    }
    return parsed;
}

void PodController::createPod(const QString &name)
{
    runAction(QStringLiteral("pods.create"), QJsonObject{{QStringLiteral("name"), name}});
}

void PodController::podAction(const QString &action, const QString &id)
{
    runAction(QStringLiteral("pods.") + action, QJsonObject{{QStringLiteral("id"), id}});
}

void PodController::removePod(const QString &id, bool force)
{
    runAction(QStringLiteral("pods.remove"),
              QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("force"), force}});
}

void PodController::batchPods(const QStringList &ids, const QString &action, bool force)
{
    runJob(
        QStringLiteral("pods.batch"), {},
        [ids, action, force] {
            return runBatchCalls(QStringLiteral("pods."), QStringLiteral("id"), action, ids, force,
                                 {});
        },
        {}, 120000);
}

void PodController::onActionFinished(const QString &method, const QJsonObject &params,
                                     const QJsonObject &result)
{
    ControllerBase::onActionFinished(method, params, result);
    if (isErrorResult(result)) {
        return;
    }
    refresh();
}

} // namespace PodStack
