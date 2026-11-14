// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon/rpc/handlers/Common.h"

using namespace PodStack::DaemonInternal;

namespace PodStack {

QJsonObject Daemon::imageExport(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }
    const QString path = params.value(QStringLiteral("path")).toString();
    if (path.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o arquivo de destino."));
        }
        return {};
    }

    if (!m_podman.exportImage(id, path, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("path"), path);
    return result;
}

QJsonObject Daemon::imageHistory(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    QJsonArray items;
    for (const ImageHistoryEntry &entry : m_podman.imageHistory(id, error)) {
        items.append(entry.toJson());
    }
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("items"), items);
    return result;
}

QJsonObject Daemon::imageInspect(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QJsonObject json = m_podman.inspectImage(id, error);
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("json"), json);
    return result;
}

QJsonObject Daemon::imageLoad(const QJsonObject &params, Error *error)
{
    const QString path = params.value(QStringLiteral("path")).toString();
    if (path.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o arquivo da imagem."));
        }
        return {};
    }

    if (!m_podman.loadImage(path, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("path"), path);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::imagePrune(const QJsonObject &params, Error *error)
{
    const bool all = params.value(QStringLiteral("all")).toBool(false);
    qint64 reclaimed = 0;
    if (!m_podman.pruneImages(all, &reclaimed, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("reclaimedBytes"), static_cast<double>(reclaimed));
    return result;
}

QJsonObject Daemon::imagePull(const QJsonObject &params, const Notify &notify, Error *error)
{
    const QString reference = params.value(QStringLiteral("reference")).toString();
    if (reference.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe a imagem para o pull."));
        }
        return {};
    }
    const QString platform = params.value(QStringLiteral("platform")).toString();

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);

    (void)QtConcurrent::run([this, reference, platform, subscriptionId, subscription, notify] {
        Error streamError;
        const auto onLine = [&](const QByteArray &line) {
            const QJsonDocument document = QJsonDocument::fromJson(line);
            if (!document.isObject()) {
                return !subscription->cancel.load();
            }
            QJsonObject payload = document.object();
            payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
            notify(QStringLiteral("images.pull.delta"), payload);
            return !subscription->cancel.load();
        };

        m_podman.pullImage(reference, platform, onLine, &subscription->cancel, &streamError);

        QJsonObject payload;
        payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
        payload.insert(QStringLiteral("reference"), reference);
        if (!streamError.isEmpty() && !subscription->cancel.load()) {
            payload.insert(QStringLiteral("error"), streamError.toJson());
            notify(QStringLiteral("images.pull.error"), payload);
        } else {
            payload.insert(QStringLiteral("cancelled"), subscription->cancel.load());
            notify(QStringLiteral("images.pull.done"), payload);
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    result.insert(QStringLiteral("reference"), reference);
    return result;
}

QJsonObject Daemon::imagePush(const QJsonObject &params, const Notify &notify, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }
    const QString destination = params.value(QStringLiteral("destination")).toString();
    if (destination.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o destino do push."));
        }
        return {};
    }

    auto subscription = std::make_shared<Subscription>();
    const QString subscriptionId = createSubscription(subscription);

    (void)QtConcurrent::run([this, id, destination, subscriptionId, subscription, notify] {
        Error streamError;
        const auto onLine = [&](const QByteArray &line) {
            const QJsonDocument document = QJsonDocument::fromJson(line);
            if (!document.isObject()) {
                return !subscription->cancel.load();
            }
            QJsonObject payload = document.object();
            payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
            notify(QStringLiteral("images.push.delta"), payload);
            return !subscription->cancel.load();
        };

        m_podman.pushImage(id, destination, onLine, &subscription->cancel, &streamError);

        QJsonObject payload;
        payload.insert(QStringLiteral("subscriptionId"), subscriptionId);
        payload.insert(QStringLiteral("destination"), destination);
        if (!streamError.isEmpty() && !subscription->cancel.load()) {
            payload.insert(QStringLiteral("error"), streamError.toJson());
            notify(QStringLiteral("images.push.error"), payload);
        } else {
            payload.insert(QStringLiteral("cancelled"), subscription->cancel.load());
            notify(QStringLiteral("images.push.done"), payload);
        }
        removeSubscription(subscriptionId);
    });

    QJsonObject result;
    result.insert(QStringLiteral("subscriptionId"), subscriptionId);
    result.insert(QStringLiteral("destination"), destination);
    return result;
}

QJsonObject Daemon::imageRemove(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const bool force = params.value(QStringLiteral("force")).toBool(false);
    if (!m_podman.removeImage(id, force, error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("removed"), true);
    return result;
}

QJsonObject Daemon::imagesList(const QJsonObject &params, Error *error)
{
    const bool all = params.value(QStringLiteral("all")).toBool(false);
    QJsonArray items;
    for (const ImageInfo &image : m_podman.listImages(all, error)) {
        items.append(image.toJson());
    }
    if (error && !error->isEmpty()) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("items"), items);
    result.insert(QStringLiteral("total"), items.size());
    return result;
}

QJsonObject Daemon::imageTag(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString repo = params.value(QStringLiteral("repo")).toString();
    if (repo.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o repositório da tag."));
        }
        return {};
    }

    if (!m_podman.tagImage(id, repo, params.value(QStringLiteral("tag")).toString(), error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("repo"), repo);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QJsonObject Daemon::imageUntag(const QJsonObject &params, Error *error)
{
    const QString id = requireId(params, error);
    if (id.isEmpty()) {
        return {};
    }

    const QString repo = params.value(QStringLiteral("repo")).toString();
    if (repo.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o repositório da tag."));
        }
        return {};
    }

    if (!m_podman.untagImage(id, repo, params.value(QStringLiteral("tag")).toString(), error)) {
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("repo"), repo);
    result.insert(QStringLiteral("ok"), true);
    return result;
}

} // namespace PodStack
