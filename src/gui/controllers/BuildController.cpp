// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/controllers/BuildController.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QTimer>

namespace PodStack {

BuildController::BuildController(QObject *parent) : CatalogController(parent)
{
    connect(&m_stream, &StreamClient::subscriptionReady, this, [this](const QJsonObject &result) {
        const QString buildId = result.value(QStringLiteral("buildId")).toString();
        if (!buildId.isEmpty()) {
            m_activeBuildId = buildId;
            emit buildStateChanged();
        }
    });
    connect(&m_stream, &StreamClient::buildEvent, this, [this](const QJsonObject &event) {
        const QString line = event.value(QStringLiteral("line")).toString();
        if (line.isEmpty()) {
            return;
        }
        m_logLines.append(line);
        if (m_logLines.size() > 10000) {
            m_logLines = m_logLines.mid(2000);
        }
        m_buildStatus = line;
        emit logLinesChanged();
    });
    connect(&m_stream, &StreamClient::buildFinished, this, [this](const QJsonObject &result) {
        const QString status = result.value(QStringLiteral("status")).toString();
        m_buildActive = false;
        m_buildStatus = status == QStringLiteral("success") ? QStringLiteral("Build concluído")
                        : status == QStringLiteral("cancelled")
                            ? QStringLiteral("Build cancelado")
                            : result.value(QStringLiteral("error")).toString();
        if (m_buildStatus.isEmpty()) {
            m_buildStatus = status;
        }
        emit buildStateChanged();
        refresh();
        // O daemon persiste o build logo após o done; uma segunda leitura
        // evita a corrida em máquinas lentas (CI do DEB).
        QTimer::singleShot(200, this, &BuildController::refresh);
    });
    connect(&m_stream, &StreamClient::error, this, [this](const QString &message) {
        m_buildActive = false;
        m_buildStatus = message;
        emit buildStateChanged();
        setLastError(message);
    });
}

QVariantList BuildController::parseItems(const QJsonArray &items) const
{
    QVariantList parsed;
    parsed.reserve(items.size());
    for (const QJsonValue &value : items) {
        const QJsonObject build = value.toObject();
        QVariantMap item;
        item.insert(QStringLiteral("id"), build.value(QStringLiteral("id")).toString());
        item.insert(QStringLiteral("imageRef"), build.value(QStringLiteral("imageRef")).toString());
        item.insert(QStringLiteral("status"), build.value(QStringLiteral("status")).toString());
        const int durationMs = build.value(QStringLiteral("durationMs")).toInt();
        item.insert(QStringLiteral("durationMs"), durationMs);
        item.insert(QStringLiteral("duration"),
                    QString::number(durationMs / 1000.0, 'f', 1) + QStringLiteral("s"));
        item.insert(QStringLiteral("startedAt"),
                    build.value(QStringLiteral("startedAt")).toString());
        item.insert(QStringLiteral("trigger"), build.value(QStringLiteral("trigger")).toString());
        item.insert(QStringLiteral("contextPath"),
                    build.value(QStringLiteral("contextPath")).toString());
        QStringList platforms;
        for (const QJsonValue &platform : build.value(QStringLiteral("platforms")).toArray()) {
            const QString value = platform.toString();
            if (!value.isEmpty()) {
                platforms.append(value);
            }
        }
        item.insert(QStringLiteral("platforms"), platforms.join(QStringLiteral(", ")));
        parsed.append(item);
    }
    return parsed;
}

void BuildController::startBuild(const QVariantMap &spec)
{
    const QString contextPath = spec.value(QStringLiteral("contextPath")).toString();
    const QString tag = spec.value(QStringLiteral("tag")).toString();
    if (contextPath.isEmpty() || tag.isEmpty()) {
        setLastError(QStringLiteral("Informe o contexto e a tag do build."));
        return;
    }

    QJsonObject buildArgs;
    const QString argsText = spec.value(QStringLiteral("buildArgsText")).toString();
    const QStringList lines =
        argsText.split(QRegularExpression(QStringLiteral("[,\\n]")), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals > 0) {
            buildArgs.insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
        }
    }

    QString dockerfile = spec.value(QStringLiteral("dockerfile")).toString();
    if (dockerfile.isEmpty()) {
        dockerfile = QStringLiteral("Dockerfile");
    }

    QStringList platforms;
    const auto appendPlatform = [&platforms](const QString &value) {
        const QString platform = value.trimmed();
        if (!platform.isEmpty() && !platforms.contains(platform)) {
            platforms.append(platform);
        }
    };
    const QVariant platformsValue = spec.value(QStringLiteral("platforms"));
    if (platformsValue.metaType().id() == QMetaType::QStringList) {
        for (const QString &platform : platformsValue.toStringList()) {
            appendPlatform(platform);
        }
    } else if (platformsValue.metaType().id() == QMetaType::QVariantList) {
        for (const QVariant &value : platformsValue.toList()) {
            appendPlatform(value.toString());
        }
    } else {
        for (const QString &part :
             platformsValue.toString().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            appendPlatform(part);
        }
    }
    if (platforms.isEmpty()) {
        for (const QString &part : spec.value(QStringLiteral("platform"))
                                       .toString()
                                       .split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            appendPlatform(part);
        }
    }

    QJsonArray platformArray;
    for (const QString &platform : platforms) {
        platformArray.append(platform);
    }

    QJsonObject params;
    params.insert(QStringLiteral("contextPath"), contextPath);
    params.insert(QStringLiteral("tag"), tag);
    params.insert(QStringLiteral("dockerfile"), dockerfile);
    params.insert(QStringLiteral("platforms"), platformArray);
    if (platforms.size() == 1) {
        params.insert(QStringLiteral("platform"), platforms.first());
    }
    params.insert(QStringLiteral("buildArgs"), buildArgs);
    params.insert(QStringLiteral("trigger"), QStringLiteral("gui"));

    m_logLines.clear();
    emit logLinesChanged();
    m_buildActive = true;
    m_buildStatus = QStringLiteral("Iniciando build de %1...").arg(tag);
    m_activeBuildId.clear();
    emit buildStateChanged();
    m_stream.startBuild(params);
}

void BuildController::cancelBuild()
{
    m_stream.unsubscribe();
    m_buildStatus = QStringLiteral("Cancelando build...");
    emit buildStateChanged();
}

void BuildController::removeBuild(const QString &id)
{
    runAction(QStringLiteral("builds.remove"), QJsonObject{{QStringLiteral("id"), id}});
}

void BuildController::batchBuilds(const QStringList &ids)
{
    runJob(
        QStringLiteral("builds.batch"), {},
        [ids] {
            return runBatchCalls(QStringLiteral("builds."), QStringLiteral("id"),
                                 QStringLiteral("remove"), ids, false, {});
        },
        {}, 120000);
}

void BuildController::onActionFinished(const QString &method, const QJsonObject &params,
                                       const QJsonObject &result)
{
    ControllerBase::onActionFinished(method, params, result);
    if (isErrorResult(result)) {
        return;
    }
    refresh();
}

} // namespace PodStack
