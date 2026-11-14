// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gui/controllers/CatalogController.h"

namespace PodStack {

class NetworkController : public CatalogController {
    Q_OBJECT

public:
    explicit NetworkController(QObject *parent = nullptr);

    Q_INVOKABLE void createNetwork(const QString &name, const QString &subnet,
                                   const QString &gateway);
    Q_INVOKABLE void removeNetwork(const QString &name, bool force);
    Q_INVOKABLE void connectNetwork(const QString &name, const QString &containerId);
    Q_INVOKABLE void disconnectNetwork(const QString &name, const QString &containerId);
    Q_INVOKABLE void batchNetworks(const QStringList &names, bool force = false);

protected:
    QString listMethod() const override { return QStringLiteral("networks.list"); }
    QVariantList parseItems(const QJsonArray &items) const override;
    void onActionFinished(const QString &method, const QJsonObject &params,
                          const QJsonObject &result) override;
};

} // namespace PodStack
