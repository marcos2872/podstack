// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/rpc/StreamClient.h"
#include "gui/controllers/CatalogController.h"

#include <QString>

namespace PodStack {

class ImageController : public CatalogController {
    Q_OBJECT
    Q_PROPERTY(bool pullActive READ pullActive NOTIFY pullStateChanged)
    Q_PROPERTY(QString pullStatus READ pullStatus NOTIFY pullStateChanged)

public:
    explicit ImageController(QObject *parent = nullptr);

    bool pullActive() const { return m_pullActive; }
    QString pullStatus() const { return m_pullStatus; }

    Q_INVOKABLE void pullImage(const QString &reference, const QString &platform = {});
    Q_INVOKABLE void cancelPull();
    Q_INVOKABLE void removeImage(const QString &id, bool force);
    Q_INVOKABLE void pruneImages(bool all);
    Q_INVOKABLE void tagImage(const QString &id, const QString &repoTag);
    Q_INVOKABLE void untagImage(const QString &id, const QString &repoTag);
    Q_INVOKABLE void exportImage(const QString &id, const QString &path);
    Q_INVOKABLE void loadImage(const QString &path);
    Q_INVOKABLE void batchImages(const QStringList &ids, bool force = false);

signals:
    void pullStateChanged();
    /// Emitido ao terminar um pull (sucesso ou erro), com a referência pedida.
    /// A página de containers usa isso para repetir a criação que faltava imagem.
    void pullCompleted(bool ok, const QString &reference, const QString &error);
    void pruneFinished();

protected:
    QString listMethod() const override { return QStringLiteral("images.list"); }
    QVariantList parseItems(const QJsonArray &items) const override;
    void onActionFinished(const QString &method, const QJsonObject &params,
                          const QJsonObject &result) override;

private:
    bool m_pullActive = false;
    QString m_pullStatus;
    QString m_pullReference;
    StreamClient m_pullStream;
};

} // namespace PodStack
