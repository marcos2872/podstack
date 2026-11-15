// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/rpc/StreamClient.h"
#include "gui/controllers/CatalogController.h"

#include <QStringList>

namespace PodStack {

class ComposeController : public CatalogController {
    Q_OBJECT
    Q_PROPERTY(QStringList logLines READ logLines NOTIFY logLinesChanged)

public:
    explicit ComposeController(QObject *parent = nullptr);

    QStringList logLines() const { return m_logLines; }

    Q_INVOKABLE void composeUp(const QString &projectPath);
    Q_INVOKABLE void composeDown(const QString &projectPath, bool volumes);
    Q_INVOKABLE void composeRestart(const QString &projectPath, const QString &service);
    Q_INVOKABLE void composeStop(const QString &projectPath, const QString &service);
    Q_INVOKABLE void composeStart(const QString &projectPath, const QString &service);
    Q_INVOKABLE void subscribeComposeLogs(const QString &project, const QString &service);
    Q_INVOKABLE void unsubscribeComposeLogs();
    Q_INVOKABLE void batchCompose(const QStringList &projectPaths, const QString &action);

signals:
    void logLinesChanged();

protected:
    QString listMethod() const override { return QStringLiteral("compose.projects.list"); }
    QVariantList parseItems(const QJsonArray &items) const override;
    void onActionFinished(const QString &method, const QJsonObject &params,
                          const QJsonObject &result) override;

private:
    QStringList m_logLines;
    StreamClient m_stream;
};

} // namespace PodStack
