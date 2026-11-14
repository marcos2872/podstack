// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/controllers/ComposeController.h"

#include <QJsonObject>

namespace PodStack {

ComposeController::ComposeController(QObject *parent) : CatalogController(parent)
{
    connect(&m_stream, &StreamClient::composeLog, this,
            [this](const QString &service, const QString &line) {
                m_logLines.append(service + QStringLiteral(" | ") + line);
                if (m_logLines.size() > 10000) {
                    m_logLines = m_logLines.mid(2000);
                }
                emit logLinesChanged();
            });
    connect(&m_stream, &StreamClient::error, this,
            [this](const QString &message) { setLastError(message); });
}

QVariantList ComposeController::parseItems(const QJsonArray &items) const
{
    QVariantList parsed;
    parsed.reserve(items.size());
    for (const QJsonValue &value : items) {
        const QJsonObject project = value.toObject();
        QVariantMap item;
        item.insert(QStringLiteral("name"), project.value(QStringLiteral("name")).toString());
        item.insert(QStringLiteral("status"), project.value(QStringLiteral("status")).toString());
        item.insert(QStringLiteral("workingDir"),
                    project.value(QStringLiteral("workingDir")).toString());
        const QJsonArray configFiles = project.value(QStringLiteral("configFiles")).toArray();
        item.insert(QStringLiteral("configFile"),
                    configFiles.isEmpty() ? QString() : configFiles.first().toString());
        QStringList services;
        for (const QJsonValue &service : project.value(QStringLiteral("services")).toArray()) {
            services.append(service.toObject().value(QStringLiteral("name")).toString());
        }
        item.insert(QStringLiteral("services"), services.join(QStringLiteral(", ")));
        parsed.append(item);
    }
    return parsed;
}

void ComposeController::composeUp(const QString &projectPath)
{
    runAction(
        QStringLiteral("compose.up"),
        QJsonObject{{QStringLiteral("projectPath"), projectPath}, {QStringLiteral("detach"), true}},
        {}, 600000);
}

void ComposeController::composeDown(const QString &projectPath, bool volumes)
{
    runAction(QStringLiteral("compose.down"),
              QJsonObject{{QStringLiteral("projectPath"), projectPath},
                          {QStringLiteral("volumes"), volumes}},
              {}, 600000);
}

void ComposeController::composeRestart(const QString &projectPath, const QString &service)
{
    runAction(QStringLiteral("compose.restart"),
              QJsonObject{{QStringLiteral("projectPath"), projectPath},
                          {QStringLiteral("service"), service}},
              {}, 600000);
}

void ComposeController::composeStop(const QString &projectPath, const QString &service)
{
    runAction(QStringLiteral("compose.stop"),
              QJsonObject{{QStringLiteral("projectPath"), projectPath},
                          {QStringLiteral("service"), service}},
              {}, 600000);
}

void ComposeController::composeStart(const QString &projectPath, const QString &service)
{
    runAction(QStringLiteral("compose.start"),
              QJsonObject{{QStringLiteral("projectPath"), projectPath},
                          {QStringLiteral("service"), service}},
              {}, 600000);
}

void ComposeController::subscribeComposeLogs(const QString &project, const QString &service)
{
    m_logLines.clear();
    emit logLinesChanged();
    m_stream.subscribeComposeLogs(project, service);
}

void ComposeController::unsubscribeComposeLogs()
{
    m_stream.unsubscribe();
}

void ComposeController::batchCompose(const QStringList &projectPaths, const QString &action)
{
    if (action != QStringLiteral("up") && action != QStringLiteral("down") &&
        action != QStringLiteral("restart") && action != QStringLiteral("stop") &&
        action != QStringLiteral("start")) {
        setLastError(QStringLiteral("Ação de lote inválida."));
        return;
    }
    QJsonObject extra;
    if (action == QStringLiteral("up")) {
        extra.insert(QStringLiteral("detach"), true);
    } else if (action == QStringLiteral("down")) {
        extra.insert(QStringLiteral("volumes"), false);
    } else {
        extra.insert(QStringLiteral("service"), QString());
    }
    runJob(
        QStringLiteral("compose.batch"), {},
        [projectPaths, action, extra] {
            return runBatchCalls(QStringLiteral("compose."), QStringLiteral("projectPath"), action,
                                 projectPaths, false, extra);
        },
        {}, 600000);
}

void ComposeController::onActionFinished(const QString &method, const QJsonObject &params,
                                         const QJsonObject &result)
{
    ControllerBase::onActionFinished(method, params, result);
    if (isErrorResult(result)) {
        return;
    }
    refresh();
}

} // namespace PodStack
