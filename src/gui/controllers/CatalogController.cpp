// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/controllers/CatalogController.h"

namespace PodStack {

CatalogController::CatalogController(QObject *parent) : ControllerBase(parent) {}

void CatalogController::refresh()
{
    if (m_loading) {
        m_refreshPending = true;
        return;
    }
    m_loading = true;
    emit loadingChanged();
    const quint64 generation = ++m_refreshGeneration;
    callRpc(listMethod(), listParams(), [this, generation](const QJsonObject &result) {
        if (generation != m_refreshGeneration) {
            // Uma leitura mais nova já foi pedida; esta chegou atrasada.
            return;
        }
        m_loading = false;
        emit loadingChanged();
        if (isErrorResult(result)) {
            setLastError(errorMessage(result));
        } else {
            setItems(parseItems(result.value(QStringLiteral("items")).toArray()));
        }
        if (m_refreshPending) {
            m_refreshPending = false;
            refresh();
        }
    });
}

void CatalogController::setItems(const QVariantList &items)
{
    m_items = items;
    emit itemsChanged();
}

} // namespace PodStack
