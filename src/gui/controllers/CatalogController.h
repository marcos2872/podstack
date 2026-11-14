// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gui/controllers/ControllerBase.h"

#include <QJsonArray>
#include <QVariantList>

namespace PodStack {

/// Base dos controllers que expõem uma lista recarregada por `*.list`.
class CatalogController : public ControllerBase {
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)

public:
    explicit CatalogController(QObject *parent = nullptr);

    QVariantList items() const { return m_items; }

    Q_INVOKABLE virtual void refresh();

signals:
    void itemsChanged();

protected:
    virtual QString listMethod() const = 0;
    virtual QJsonObject listParams() const { return {}; }
    virtual QVariantList parseItems(const QJsonArray &items) const = 0;
    void setItems(const QVariantList &items);

private:
    QVariantList m_items;
};

} // namespace PodStack
