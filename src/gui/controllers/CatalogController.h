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
    /// Leitura da lista em andamento (para o estado "carregando" da página).
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

public:
    explicit CatalogController(QObject *parent = nullptr);

    QVariantList items() const { return m_items; }
    bool loading() const { return m_loading; }

    Q_INVOKABLE virtual void refresh();

signals:
    void itemsChanged();
    void loadingChanged();

protected:
    virtual QString listMethod() const = 0;
    virtual QJsonObject listParams() const { return {}; }
    virtual QVariantList parseItems(const QJsonArray &items) const = 0;
    void setItems(const QVariantList &items);

    /// Estado de leitura compartilhado com os controllers derivados que
    /// reimplementam `refresh()` (o de containers fecha o `loading` só na
    /// resposta mais nova).
    bool m_loading = false;
    /// Um refresh pedido durante uma leitura é coalescido: só dispara uma nova
    /// chamada quando a resposta atual chega. Sem isso, eventos frequentes
    /// empilhavam RPCs obsoletos (cada um ocupando um handler do daemon).
    bool m_refreshPending = false;
    /// Cada refresh ganha uma geração; a resposta de uma leitura antiga é
    /// descartada para não sobrescrever um retrato mais novo (eventos e ação
    /// disparam refresh concorrentes, cada um em sua thread/socket).
    quint64 m_refreshGeneration = 0;

private:
    QVariantList m_items;
};

} // namespace PodStack
