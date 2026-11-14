// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/controllers/ImageController.h"

#include "core/error/Error.h"

#include <QJsonObject>
#include <QtConcurrent>

namespace PodStack {

ImageController::ImageController(QObject *parent) : CatalogController(parent)
{
    connect(&m_pullStream, &StreamClient::pullEvent, this, [this](const QJsonObject &event) {
        const QString status = event.value(QStringLiteral("status")).toString();
        const QString stream = event.value(QStringLiteral("stream")).toString().trimmed();
        if (!status.isEmpty()) {
            const QString progress = event.value(QStringLiteral("progress")).toString();
            m_pullStatus = progress.isEmpty() ? status : status + QStringLiteral(" ") + progress;
        } else if (!stream.isEmpty()) {
            m_pullStatus = stream;
        }
        emit pullStateChanged();
    });
    connect(&m_pullStream, &StreamClient::pullFinished, this,
            [this](bool ok, const QString &error) {
                m_pullActive = false;
                m_pullStatus = ok ? QStringLiteral("Pull concluído") : error;
                emit pullStateChanged();
                if (ok) {
                    refresh();
                }
            });
    connect(&m_pullStream, &StreamClient::error, this, [this](const QString &message) {
        m_pullActive = false;
        m_pullStatus = message;
        emit pullStateChanged();
    });
}

QVariantList ImageController::parseItems(const QJsonArray &items) const
{
    QVariantList parsed;
    parsed.reserve(items.size());
    for (const QJsonValue &value : items) {
        const QJsonObject image = value.toObject();
        const QJsonArray tags = image.value(QStringLiteral("repoTags")).toArray();
        QVariantMap item;
        item.insert(QStringLiteral("id"), image.value(QStringLiteral("id")).toString());
        item.insert(QStringLiteral("name"),
                    tags.isEmpty() ? QStringLiteral("<none>:<none>") : tags.first().toString());
        item.insert(QStringLiteral("digest"), image.value(QStringLiteral("digest")).toString());
        item.insert(QStringLiteral("size"),
                    static_cast<double>(image.value(QStringLiteral("size")).toDouble()));
        item.insert(QStringLiteral("created"),
                    static_cast<double>(image.value(QStringLiteral("created")).toDouble()));
        item.insert(QStringLiteral("containers"),
                    image.value(QStringLiteral("containers")).toInt());
        item.insert(QStringLiteral("dangling"), image.value(QStringLiteral("dangling")).toBool());
        parsed.append(item);
    }
    return parsed;
}

void ImageController::pullImage(const QString &reference, const QString &platform)
{
    if (reference.isEmpty()) {
        setLastError(QStringLiteral("Informe a imagem para o pull."));
        return;
    }
    m_pullActive = true;
    m_pullStatus = QStringLiteral("Iniciando pull de %1...").arg(reference);
    emit pullStateChanged();
    m_pullStream.pullImage(reference, platform);
}

void ImageController::cancelPull()
{
    m_pullStream.unsubscribe();
    m_pullActive = false;
    m_pullStatus = QStringLiteral("Pull cancelado");
    emit pullStateChanged();
}

void ImageController::removeImage(const QString &id, bool force)
{
    runAction(QStringLiteral("images.remove"),
              QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("force"), force}});
}

void ImageController::pruneImages(bool all)
{
    runAction(QStringLiteral("images.prune"), QJsonObject{{QStringLiteral("all"), all}});
}

void ImageController::tagImage(const QString &id, const QString &repoTag)
{
    QString repo = repoTag;
    QString tag = QStringLiteral("latest");
    const int slash = repo.lastIndexOf(QLatin1Char('/'));
    const int colon = repo.lastIndexOf(QLatin1Char(':'));
    if (colon > slash) {
        tag = repo.mid(colon + 1);
        repo = repo.left(colon);
    }
    runAction(QStringLiteral("images.tag"), QJsonObject{{QStringLiteral("id"), id},
                                                        {QStringLiteral("repo"), repo},
                                                        {QStringLiteral("tag"), tag}});
}

void ImageController::untagImage(const QString &id, const QString &repoTag)
{
    QString repo = repoTag;
    QString tag = QStringLiteral("latest");
    const int slash = repo.lastIndexOf(QLatin1Char('/'));
    const int colon = repo.lastIndexOf(QLatin1Char(':'));
    if (colon > slash) {
        tag = repo.mid(colon + 1);
        repo = repo.left(colon);
    }
    runAction(QStringLiteral("images.untag"), QJsonObject{{QStringLiteral("id"), id},
                                                          {QStringLiteral("repo"), repo},
                                                          {QStringLiteral("tag"), tag}});
}

void ImageController::exportImage(const QString &id, const QString &path)
{
    runAction(QStringLiteral("images.export"),
              QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("path"), path}});
}

void ImageController::loadImage(const QString &path)
{
    runAction(QStringLiteral("images.load"), QJsonObject{{QStringLiteral("path"), path}});
}

void ImageController::batchImages(const QStringList &ids, bool force)
{
    runJob(
        QStringLiteral("images.batch"), {},
        [ids, force] {
            return runBatchCalls(QStringLiteral("images."), QStringLiteral("id"),
                                 QStringLiteral("remove"), ids, force, {});
        },
        {}, 120000);
}

void ImageController::onActionFinished(const QString &method, const QJsonObject &params,
                                       const QJsonObject &result)
{
    ControllerBase::onActionFinished(method, params, result);
    if (isErrorResult(result)) {
        return;
    }
    clearLastError();
    if (method == QStringLiteral("images.prune")) {
        emit pruneFinished();
    }
    refresh();
}

} // namespace PodStack
