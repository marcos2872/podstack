// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/rpc/StreamClient.h"
#include "gui/controllers/CatalogController.h"

#include <QStringList>

namespace PodStack {

class BuildController : public CatalogController {
    Q_OBJECT
    Q_PROPERTY(QStringList logLines READ logLines NOTIFY logLinesChanged)
    Q_PROPERTY(bool buildActive READ buildActive NOTIFY buildStateChanged)
    Q_PROPERTY(QString buildStatus READ buildStatus NOTIFY buildStateChanged)
    Q_PROPERTY(QString activeBuildId READ activeBuildId NOTIFY buildStateChanged)

public:
    explicit BuildController(QObject *parent = nullptr);

    QStringList logLines() const { return m_logLines; }
    bool buildActive() const { return m_buildActive; }
    QString buildStatus() const { return m_buildStatus; }
    QString activeBuildId() const { return m_activeBuildId; }

    Q_INVOKABLE void startBuild(const QVariantMap &spec);
    Q_INVOKABLE void cancelBuild();
    Q_INVOKABLE void removeBuild(const QString &id);
    Q_INVOKABLE void batchBuilds(const QStringList &ids);

signals:
    void logLinesChanged();
    void buildStateChanged();

protected:
    QString listMethod() const override { return QStringLiteral("builds.list"); }
    QVariantList parseItems(const QJsonArray &items) const override;
    void onActionFinished(const QString &method, const QJsonObject &params,
                          const QJsonObject &result) override;

private:
    /// `builds.cancel` precisa do id, que só chega em `subscriptionReady`;
    /// guardar o pedido evita cancelamento silenciosamente perdido.
    void requestBuildCancel();

    QStringList m_logLines;
    bool m_buildActive = false;
    bool m_cancelRequested = false;
    QString m_buildStatus;
    QString m_activeBuildId;
    StreamClient m_stream;
};

} // namespace PodStack
