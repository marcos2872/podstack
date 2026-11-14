// SPDX-License-Identifier: GPL-3.0-or-later
#include "podman/client/Common.h"

using namespace PodStack::PodmanInternal;

namespace PodStack {

bool PodmanClient::buildImage(const QString &contextPath, const QString &dockerfile,
                              const QString &tag, const QMap<QString, QString> &buildArgs,
                              const QString &platform,
                              const std::function<bool(const QByteArray &)> &onLine,
                              std::atomic_bool *cancel, QString *imageId, Error *error)
{
    if (error) {
        *error = {};
    }
    if (imageId) {
        imageId->clear();
    }
    if (!ensureVersion(error)) {
        return false;
    }

    const QDir context(contextPath);
    if (!context.exists()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Diretório de contexto inválido."), contextPath);
        }
        return false;
    }
    const QString dockerfilePath = context.absoluteFilePath(dockerfile);
    if (!QFileInfo::exists(dockerfilePath)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Dockerfile não encontrado."), dockerfilePath);
        }
        return false;
    }
    if (tag.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe a tag da imagem."));
        }
        return false;
    }

    QByteArray tar;
    if (!buildContextTar(contextPath, &tar, error)) {
        return false;
    }

    QString suffix = QStringLiteral("/build?t=%1&dockerfile=%2&cache=true")
                         .arg(encodeId(tag), encodeId(dockerfile));
    if (!platform.isEmpty()) {
        suffix += QStringLiteral("&platform=%1").arg(encodeId(platform));
    }
    if (!buildArgs.isEmpty()) {
        QJsonObject argsObject;
        for (auto it = buildArgs.constBegin(); it != buildArgs.constEnd(); ++it) {
            argsObject.insert(it.key(), it.value());
        }
        suffix += QStringLiteral("&buildargs=%1")
                      .arg(encodeId(QString::fromUtf8(
                          QJsonDocument(argsObject).toJson(QJsonDocument::Compact))));
    }

    QByteArray buffer;
    QString streamError;
    QString capturedImage;

    const auto onData = [&](const QByteArray &data) {
        buffer.append(data);
        int newline = -1;
        while ((newline = buffer.indexOf('\n')) >= 0) {
            const QByteArray line = buffer.left(newline).trimmed();
            buffer.remove(0, newline + 1);
            if (line.isEmpty()) {
                continue;
            }
            const QJsonDocument document = QJsonDocument::fromJson(line);
            if (document.isObject()) {
                const QJsonObject object = document.object();
                const QString message = object.value(QStringLiteral("error")).toString();
                if (!message.isEmpty()) {
                    streamError = message;
                    return false;
                }
                const QString auxId = object.value(QStringLiteral("aux"))
                                          .toObject()
                                          .value(QStringLiteral("ID"))
                                          .toString();
                if (!auxId.isEmpty()) {
                    capturedImage = auxId;
                }
            }
            if (!onLine(line)) {
                return false;
            }
        }
        return true;
    };

    QString streamFailure;
    const bool ok = m_http.streamRequest(QByteArrayLiteral("POST"), this->path(suffix), tar,
                                         QStringLiteral("application/x-tar"), onData, cancel,
                                         &streamFailure);
    if (!buffer.trimmed().isEmpty()) {
        onLine(buffer.trimmed());
    }
    if (imageId) {
        *imageId = capturedImage;
    }
    if (!streamError.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_BUILD_ERROR"),
                                 QStringLiteral("Falha no build da imagem."), streamError);
        }
        return false;
    }
    if (!ok && error) {
        *error = Error::make(QStringLiteral("PODSTACK_BUILD_ERROR"),
                             QStringLiteral("Falha no build da imagem."), streamFailure);
    }
    return ok && streamError.isEmpty();
}

} // namespace PodStack
