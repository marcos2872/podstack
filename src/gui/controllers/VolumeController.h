// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gui/controllers/CatalogController.h"

namespace PodStack {

class VolumeController : public CatalogController {
    Q_OBJECT

public:
    explicit VolumeController(QObject *parent = nullptr);

    Q_INVOKABLE void createVolume(const QString &name, const QString &driver);
    Q_INVOKABLE void removeVolume(const QString &name, bool force);
    Q_INVOKABLE void pruneVolumes();
    Q_INVOKABLE void cloneVolume(const QString &source, const QString &destination);
    Q_INVOKABLE void importHostPath(const QString &name, const QString &sourcePath);
    Q_INVOKABLE void batchVolumes(const QStringList &names, bool force = false);

signals:
    void pruneFinished();

protected:
    QString listMethod() const override { return QStringLiteral("volumes.list"); }
    QVariantList parseItems(const QJsonArray &items) const override;
    void onActionFinished(const QString &method, const QJsonObject &params,
                          const QJsonObject &result) override;
};

} // namespace PodStack
