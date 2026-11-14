// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gui/controllers/CatalogController.h"

namespace PodStack {

class PodController : public CatalogController {
    Q_OBJECT

public:
    explicit PodController(QObject *parent = nullptr);

    Q_INVOKABLE void createPod(const QString &name);
    Q_INVOKABLE void podAction(const QString &action, const QString &id);
    Q_INVOKABLE void removePod(const QString &id, bool force);
    Q_INVOKABLE void batchPods(const QStringList &ids, const QString &action, bool force = false);

protected:
    QString listMethod() const override { return QStringLiteral("pods.list"); }
    QVariantList parseItems(const QJsonArray &items) const override;
    void onActionFinished(const QString &method, const QJsonObject &params,
                          const QJsonObject &result) override;
};

} // namespace PodStack
