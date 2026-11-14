// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/controllers/CatalogController.h"

namespace PodStack {

CatalogController::CatalogController(QObject *parent) : ControllerBase(parent) {}

void CatalogController::refresh()
{
    callRpc(listMethod(), listParams(), [this](const QJsonObject &result) {
        if (isErrorResult(result)) {
            setLastError(errorMessage(result));
            return;
        }
        setItems(parseItems(result.value(QStringLiteral("items")).toArray()));
    });
}

void CatalogController::setItems(const QVariantList &items)
{
    m_items = items;
    emit itemsChanged();
}

} // namespace PodStack
