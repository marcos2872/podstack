// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/controllers/VolumeController.h"

#include <QJsonObject>

namespace PodStack {

VolumeController::VolumeController(QObject *parent) : CatalogController(parent) {}

QVariantList VolumeController::parseItems(const QJsonArray &items) const
{
    QVariantList parsed;
    parsed.reserve(items.size());
    for (const QJsonValue &value : items) {
        const QJsonObject volume = value.toObject();
        QVariantMap item;
        item.insert(QStringLiteral("name"), volume.value(QStringLiteral("name")).toString());
        item.insert(QStringLiteral("driver"), volume.value(QStringLiteral("driver")).toString());
        item.insert(QStringLiteral("mountpoint"),
                    volume.value(QStringLiteral("mountpoint")).toString());
        item.insert(QStringLiteral("size"),
                    static_cast<double>(volume.value(QStringLiteral("size")).toDouble()));
        item.insert(QStringLiteral("links"), volume.value(QStringLiteral("links")).toInt());
        parsed.append(item);
    }
    return parsed;
}

void VolumeController::createVolume(const QString &name, const QString &driver)
{
    runAction(QStringLiteral("volumes.create"),
              QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("driver"), driver}});
}

void VolumeController::removeVolume(const QString &name, bool force)
{
    runAction(QStringLiteral("volumes.remove"),
              QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("force"), force}});
}

void VolumeController::pruneVolumes()
{
    runAction(QStringLiteral("volumes.prune"), {});
}

void VolumeController::cloneVolume(const QString &source, const QString &destination)
{
    runAction(QStringLiteral("volumes.clone"),
              QJsonObject{{QStringLiteral("source"), source},
                          {QStringLiteral("destination"), destination}});
}

void VolumeController::importHostPath(const QString &name, const QString &sourcePath)
{
    runAction(
        QStringLiteral("volumes.importHostPath"),
        QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("sourcePath"), sourcePath}});
}

void VolumeController::batchVolumes(const QStringList &names, bool force)
{
    runJob(
        QStringLiteral("volumes.batch"), {},
        [names, force] {
            return runBatchCalls(QStringLiteral("volumes."), QStringLiteral("name"),
                                 QStringLiteral("remove"), names, force, {});
        },
        {}, 120000);
}

void VolumeController::onActionFinished(const QString &method, const QJsonObject &params,
                                        const QJsonObject &result)
{
    ControllerBase::onActionFinished(method, params, result);
    if (isErrorResult(result)) {
        return;
    }
    if (method == QStringLiteral("volumes.prune")) {
        emit pruneFinished();
    }
    refresh();
}

} // namespace PodStack
