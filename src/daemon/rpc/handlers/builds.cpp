// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon/rpc/handlers/Common.h"

using namespace PodStack::DaemonInternal;

namespace PodStack {

QJsonObject Daemon::buildCancel(const QJsonObject &params, Error *error)
{
    const QString id = params.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o build."));
        }
        return {};
    }

    QString subscriptionId;
    {
        std::lock_guard<std::mutex> lock(m_buildMutex);
        subscriptionId = m_buildSubscriptions.value(id);
    }
    if (subscriptionId.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Build não está em execução."), id);
        }
        return {};
    }

    QJsonObject result =
        unsubscribe(QJsonObject{{QStringLiteral("subscriptionId"), subscriptionId}});
    result.insert(QStringLiteral("id"), id);
    return result;
}

QJsonObject Daemon::buildGet(const QJsonObject &params, Error *error)
{
    const QString id = params.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o build."));
        }
        return {};
    }

    const BuildRecord record = m_buildStore.get(id, error);
    if (error && !error->isEmpty()) {
        return {};
    }
    return record.toJson();
}

QJsonObject Daemon::buildRemove(const QJsonObject &params, Error *error)
{
    const QString id = params.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o build."));
        }
        return {};
    }

    {
        std::lock_guard<std::mutex> lock(m_buildMutex);
        const QString subscriptionId = m_buildSubscriptions.value(id);
        if (!subscriptionId.isEmpty()) {
            unsubscribe(QJsonObject{{QStringLiteral("subscriptionId"), subscriptionId}});
        }
    }

    const BuildRecord record = m_buildStore.get(id, error);
    if (error && !error->isEmpty()) {
        return {};
    }
    if (!record.logPath.isEmpty()) {
        QFile::remove(record.logPath);
    }
    if (!m_buildStore.remove(id, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("removed"), true);
    return result;
}

QJsonObject Daemon::buildsList(const QJsonObject &params, Error *error)
{
    const int limit = params.value(QStringLiteral("limit")).toInt(50);
    const int offset = params.value(QStringLiteral("offset")).toInt(0);

    QJsonArray items;
    for (const BuildRecord &record : m_buildStore.list(limit, offset, error)) {
        items.append(record.toJson());
    }
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("items"), items);
    result.insert(QStringLiteral("total"), items.size());
    return result;
}

QJsonObject Daemon::buildStart(const QJsonObject &params, const Notify &notify, Error *error)
{
    const QString contextPath = params.value(QStringLiteral("contextPath")).toString();
    const QString tag = params.value(QStringLiteral("tag")).toString();
    const QString dockerfile =
        params.value(QStringLiteral("dockerfile")).toString(QStringLiteral("Dockerfile"));
    const QString trigger = params.value(QStringLiteral("trigger")).toString(QStringLiteral("gui"));

    if (contextPath.isEmpty() || tag.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o contexto e a tag do build."));
        }
        return {};
    }

    QStringList platforms;
    const QJsonValue platformsValue = params.value(QStringLiteral("platforms"));
    if (platformsValue.isArray()) {
        for (const QJsonValue &value : platformsValue.toArray()) {
            const QString platform = value.toString().trimmed();
            if (!platform.isEmpty() && !platforms.contains(platform)) {
                platforms.append(platform);
            }
        }
    } else if (platformsValue.isString()) {
        for (const QString &part :
             platformsValue.toString().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString platform = part.trimmed();
            if (!platform.isEmpty() && !platforms.contains(platform)) {
                platforms.append(platform);
            }
        }
    }
    // Compatibilidade com clientes antigos: `platform` single ou separado por vírgula.
    if (platforms.isEmpty()) {
        for (const QString &part : params.value(QStringLiteral("platform"))
                                       .toString()
                                       .split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString platform = part.trimmed();
            if (!platform.isEmpty() && !platforms.contains(platform)) {
                platforms.append(platform);
            }
        }
    }
    if (!platforms.isEmpty()) {
        Error platformError;
        if (!ManifestBuilder::validatePlatforms(platforms, &platformError)) {
            if (error) {
                *error = platformError;
            }
            return {};
        }
    }

    QMap<QString, QString> buildArgs;
    const QJsonObject argsObject = params.value(QStringLiteral("buildArgs")).toObject();
    for (auto it = argsObject.constBegin(); it != argsObject.constEnd(); ++it) {
        buildArgs.insert(it.key(), it.value().toString());
    }

    const QString buildId =
        QStringLiteral("build-%1-%2").arg(QDateTime::currentSecsSinceEpoch()).arg(m_nextBuild++);
    const QString logDir = Paths::stateDir() + QStringLiteral("/builds");
    QDir().mkpath(logDir);
    const QString logPath = logDir + QLatin1Char('/') + buildId + QStringLiteral(".log");

    BuildRecord record;
    record.id = buildId;
    record.imageRef = tag;
    record.status = QStringLiteral("running");
    record.startedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    record.trigger = trigger;
    record.contextPath = contextPath;
    record.dockerfile = dockerfile;
    record.logPath = logPath;
    record.platforms = platforms;
    if (!m_buildStore.insert(record, error)) {
        return {};
    }

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);
    {
        std::lock_guard<std::mutex> lock(m_buildMutex);
        m_buildSubscriptions.insert(buildId, subscriptionId);
    }

    if (platforms.size() > 1) {
        // Manifest list: o Libpod REST não cria manifest lists, então usamos o CLI.
        (void) QtConcurrent::run([this, buildId, contextPath, dockerfile, tag, buildArgs, platforms,
                                  logPath, subscriptionId, subscription, notify] {
            QElapsedTimer timer;
            timer.start();

            QFile logFile(logPath);
            if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
                qCWarning(lcPodman)
                    << "Não foi possível abrir o log do build:" << logFile.errorString();
            }

            const auto onLine = [&](const QString &line) {
                if (logFile.isOpen()) {
                    logFile.write(line.toUtf8());
                    logFile.write("\n");
                    logFile.flush();
                }
                QJsonObject payload;
                payload.insert(QStringLiteral("buildId"), buildId);
                payload.insert(QStringLiteral("line"), line);
                notify(QStringLiteral("builds.delta"), payload);
            };
            const auto cancelled = [subscription] { return subscription->cancel.load(); };

            ManifestBuilder builder;
            Error buildError;
            const bool ok = builder.build(contextPath, dockerfile, tag, buildArgs, platforms,
                                          onLine, cancelled, &buildError);
            if (logFile.isOpen()) {
                logFile.close();
            }

            const bool wasCancelled = subscription->cancel.load();
            const QString status =
                wasCancelled ? QStringLiteral("cancelled")
                             : (ok ? QStringLiteral("success") : QStringLiteral("failed"));
            const int durationMs = static_cast<int>(timer.elapsed());
            const QString errorText = ok ? QString() : buildError.message;

            m_buildStore.finish(buildId, status, errorText, durationMs);

            QJsonObject payload;
            payload.insert(QStringLiteral("buildId"), buildId);
            payload.insert(QStringLiteral("status"), status);
            payload.insert(QStringLiteral("imageId"), QString());
            payload.insert(QStringLiteral("error"), errorText);
            payload.insert(QStringLiteral("durationMs"), durationMs);
            payload.insert(QStringLiteral("steps"), 0);
            notify(QStringLiteral("builds.done"), payload);

            {
                std::lock_guard<std::mutex> lock(m_buildMutex);
                m_buildSubscriptions.remove(buildId);
            }
            removeSubscription(subscriptionId);
        });

        QJsonObject result;
        result.insert(QStringLiteral("buildId"), buildId);
        result.insert(QStringLiteral("subscriptionId"), subscriptionId);
        result.insert(QStringLiteral("logPath"), logPath);
        return result;
    }

    const QString platform = platforms.isEmpty() ? QString() : platforms.first();
    (void) QtConcurrent::run([this, buildId, contextPath, dockerfile, tag, buildArgs, platform,
                              logPath, subscriptionId, subscription, notify] {
        QElapsedTimer timer;
        timer.start();

        QFile logFile(logPath);
        if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
            qCWarning(lcPodman) << "Não foi possível abrir o log do build:"
                                << logFile.errorString();
        }
        int stepIndex = 0;
        bool sawStep = false;

        const auto onLine = [&](const QByteArray &line) {
            QString text;
            const QJsonDocument document = QJsonDocument::fromJson(line);
            if (document.isObject()) {
                const QJsonObject object = document.object();
                text = object.value(QStringLiteral("stream")).toString();
                const QString errorText = object.value(QStringLiteral("error")).toString();
                if (!errorText.isEmpty()) {
                    text = errorText;
                }
                if (text.isEmpty()) {
                    text = object.value(QStringLiteral("status")).toString();
                }
            }
            if (text.isEmpty()) {
                text = QString::fromUtf8(line);
            }
            if (!text.endsWith(QLatin1Char('\n'))) {
                text += QLatin1Char('\n');
            }
            if (logFile.isOpen()) {
                logFile.write(text.toUtf8());
                logFile.flush();
            }

            QJsonObject payload;
            payload.insert(QStringLiteral("buildId"), buildId);
            payload.insert(QStringLiteral("line"), text.trimmed());
            notify(QStringLiteral("builds.delta"), payload);

            if (text.startsWith(QStringLiteral("STEP "))) {
                const QRegularExpression expression(
                    QStringLiteral("^STEP\\s+(\\d+)/(\\d+):\\s*(.*)$"));
                const QRegularExpressionMatch match = expression.match(text.trimmed());
                if (match.hasMatch()) {
                    BuildStepRecord step;
                    step.id = buildId + QStringLiteral("-step-") + match.captured(1);
                    step.buildId = buildId;
                    step.index = match.captured(1).toInt();
                    step.name = match.captured(3);
                    step.status = QStringLiteral("running");
                    step.startedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
                    step.logPath = logPath;
                    m_buildStore.addStep(step);
                    stepIndex = step.index;
                    sawStep = true;
                }
            }
            return !subscription->cancel.load();
        };

        QString imageId;
        Error buildError;
        const bool ok = m_podman.buildImage(contextPath, dockerfile, tag, buildArgs, platform,
                                            onLine, &subscription->cancel, &imageId, &buildError);
        if (logFile.isOpen()) {
            logFile.close();
        }

        const bool cancelled = subscription->cancel.load();
        const QString status = cancelled
                                   ? QStringLiteral("cancelled")
                                   : (ok ? QStringLiteral("success") : QStringLiteral("failed"));
        const int durationMs = static_cast<int>(timer.elapsed());
        const QString errorText = ok ? QString() : buildError.message;

        m_buildStore.finish(buildId, status, errorText, durationMs);
        m_buildStore.finishSteps(buildId, status == QStringLiteral("success")
                                              ? QStringLiteral("success")
                                              : QStringLiteral("failed"));

        QJsonObject payload;
        payload.insert(QStringLiteral("buildId"), buildId);
        payload.insert(QStringLiteral("status"), status);
        payload.insert(QStringLiteral("imageId"), imageId);
        payload.insert(QStringLiteral("error"), errorText);
        payload.insert(QStringLiteral("durationMs"), durationMs);
        payload.insert(QStringLiteral("steps"), sawStep ? stepIndex : 0);
        notify(QStringLiteral("builds.done"), payload);

        {
            std::lock_guard<std::mutex> lock(m_buildMutex);
            m_buildSubscriptions.remove(buildId);
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("buildId"), buildId);
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    result.insert(QStringLiteral("logPath"), logPath);
    return result;
}

} // namespace PodStack
