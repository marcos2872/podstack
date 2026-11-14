// SPDX-License-Identifier: GPL-3.0-or-later
#include "podman/client/Common.h"

using namespace PodStack::PodmanInternal;

namespace PodStack {

QVector<ImageInfo> PodmanClient::listImages(bool all, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return {};
    }

    const QString suffix = QStringLiteral("/images/json?all=%1")
                               .arg(all ? QStringLiteral("true") : QStringLiteral("false"));
    const HttpResponse response = m_http.request(QByteArrayLiteral("GET"), path(suffix));
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao listar imagens."));
        }
        return {};
    }

    QVector<ImageInfo> images;
    const QJsonArray array = QJsonDocument::fromJson(response.body).array();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        ImageInfo info;
        info.id = object.value(QStringLiteral("Id")).toString();
        if (info.id.isEmpty()) {
            continue;
        }
        info.repoTags = stringListFromJson(object.value(QStringLiteral("RepoTags")).toArray());
        info.digest = object.value(QStringLiteral("Digest")).toString();
        info.size = static_cast<qint64>(object.value(QStringLiteral("Size")).toDouble());
        info.created = static_cast<qint64>(object.value(QStringLiteral("Created")).toDouble());
        info.containers = object.value(QStringLiteral("Containers")).toInt();
        info.dangling = info.repoTags.isEmpty();
        info.labels = labelsFromJson(object);
        images.append(info);
    }
    return images;
}

QJsonObject PodmanClient::inspectImage(const QString &id, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return {};
    }

    const QString suffix = QStringLiteral("/images/%1/json").arg(encodeId(id));
    const HttpResponse response = m_http.request(QByteArrayLiteral("GET"), path(suffix));
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao inspecionar a imagem."));
        }
        return {};
    }
    return QJsonDocument::fromJson(response.body).object();
}

QVector<ImageHistoryEntry> PodmanClient::imageHistory(const QString &id, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return {};
    }

    const QString suffix = QStringLiteral("/images/%1/history").arg(encodeId(id));
    const HttpResponse response = m_http.request(QByteArrayLiteral("GET"), path(suffix));
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao ler o histórico."));
        }
        return {};
    }

    QVector<ImageHistoryEntry> entries;
    const QJsonArray array = QJsonDocument::fromJson(response.body).array();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        ImageHistoryEntry entry;
        entry.id = object.value(QStringLiteral("Id")).toString();
        entry.created = static_cast<qint64>(object.value(QStringLiteral("Created")).toDouble());
        entry.createdBy = object.value(QStringLiteral("CreatedBy")).toString();
        entry.comment = object.value(QStringLiteral("Comment")).toString();
        entry.size = static_cast<qint64>(object.value(QStringLiteral("Size")).toDouble());
        entries.append(entry);
    }
    return entries;
}

bool PodmanClient::pullImage(const QString &reference, const QString &platform,
                             const std::function<bool(const QByteArray &)> &onLine,
                             std::atomic_bool *cancel, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    if (reference.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe a imagem para o pull."));
        }
        return false;
    }

    QString suffix = QStringLiteral("/images/pull?reference=%1").arg(encodeId(reference));
    if (!platform.isEmpty()) {
        suffix += QStringLiteral("&platform=%1").arg(encodeId(platform));
    }

    QByteArray buffer;
    QString streamError;
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
            }
            if (!onLine(line)) {
                return false;
            }
        }
        return true;
    };

    QString streamFailure;
    const bool ok = m_http.streamRequest(QByteArrayLiteral("POST"), path(suffix), {}, {},
                                         onData, cancel, &streamFailure);
    if (!buffer.trimmed().isEmpty()) {
        onLine(buffer.trimmed());
    }
    if (!streamError.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_ENGINE_ERROR"),
                                 QStringLiteral("Falha no pull da imagem."), streamError);
        }
        return false;
    }
    if (!ok && error) {
        *error = Error::make(QStringLiteral("PODSTACK_ENGINE_ERROR"),
                             QStringLiteral("Falha no pull da imagem."), streamFailure);
    }
    return ok && streamError.isEmpty();
}

bool PodmanClient::pushImage(const QString &id, const QString &destination,
                             const std::function<bool(const QByteArray &)> &onLine,
                             std::atomic_bool *cancel, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    if (destination.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o destino do push."));
        }
        return false;
    }

    const QString suffix = QStringLiteral("/images/%1/push?destination=%2")
                               .arg(encodeId(id), encodeId(destination));
    QByteArray buffer;
    QString streamError;

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
            }
            if (!onLine(line)) {
                return false;
            }
        }
        return true;
    };

    QString streamFailure;
    const bool ok = m_http.streamRequest(QByteArrayLiteral("POST"), path(suffix), {}, {},
                                         onData, cancel, &streamFailure);
    if (!streamError.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_ENGINE_ERROR"),
                                 QStringLiteral("Falha no push da imagem."), streamError);
        }
        return false;
    }
    if (!ok && error) {
        *error = Error::make(QStringLiteral("PODSTACK_ENGINE_ERROR"),
                             QStringLiteral("Falha no push da imagem."), streamFailure);
    }
    return ok && streamError.isEmpty();
}

bool PodmanClient::tagImage(const QString &id, const QString &repo, const QString &tag,
                            Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }

    QString suffix = QStringLiteral("/images/%1/tag?repo=%2").arg(encodeId(id), encodeId(repo));
    if (!tag.isEmpty()) {
        suffix += QStringLiteral("&tag=%1").arg(encodeId(tag));
    }
    const HttpResponse response = m_http.request(QByteArrayLiteral("POST"), path(suffix));
    if (response.status == 200 || response.status == 201 || response.status == 204) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao marcar a imagem."));
    }
    return false;
}

bool PodmanClient::untagImage(const QString &id, const QString &repo, const QString &tag,
                              Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    if (repo.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe a referência para remover a tag."));
        }
        return false;
    }

    QString suffix = QStringLiteral("/images/%1/untag?repo=%2").arg(encodeId(id), encodeId(repo));
    if (!tag.isEmpty()) {
        suffix += QStringLiteral("&tag=%1").arg(encodeId(tag));
    }
    const HttpResponse response = m_http.request(QByteArrayLiteral("POST"), path(suffix));
    if (response.status == 200 || response.status == 201 || response.status == 204) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao remover a tag."));
    }
    return false;
}

bool PodmanClient::removeImage(const QString &id, bool force, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }

    const QString suffix = QStringLiteral("/images/%1?force=%2")
                               .arg(encodeId(id), force ? QStringLiteral("true")
                                                        : QStringLiteral("false"));
    const HttpResponse response = m_http.request(QByteArrayLiteral("DELETE"), path(suffix));
    if (response.status == 200 || response.status == 204) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao remover a imagem."));
    }
    return false;
}

bool PodmanClient::pruneImages(bool all, qint64 *reclaimedBytes, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    if (reclaimedBytes) {
        *reclaimedBytes = 0;
    }

    const QString suffix = QStringLiteral("/images/prune?all=%1")
                               .arg(all ? QStringLiteral("true") : QStringLiteral("false"));
    const HttpResponse response = m_http.request(QByteArrayLiteral("POST"), path(suffix));
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao limpar imagens."));
        }
        return false;
    }

    const QJsonObject object = QJsonDocument::fromJson(response.body).object();
    if (reclaimedBytes) {
        *reclaimedBytes =
            static_cast<qint64>(object.value(QStringLiteral("SpaceReclaimed")).toDouble());
    }
    return true;
}

bool PodmanClient::exportImage(const QString &id, const QString &destination, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    if (destination.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o destino do export."));
        }
        return false;
    }

    const QString suffix = QStringLiteral("/images/%1/get").arg(encodeId(id));
    const HttpResponse response =
        m_http.request(QByteArrayLiteral("GET"), path(suffix), {}, {}, 600000);
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao exportar a imagem."));
        }
        return false;
    }

    QFile file(destination);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível gravar o arquivo."),
                                 file.errorString());
        }
        return false;
    }
    file.write(response.body);
    file.close();
    return true;
}

bool PodmanClient::loadImage(const QString &source, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }

    QFile file(source);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível abrir o arquivo."),
                                 file.errorString());
        }
        return false;
    }

    const QByteArray boundary = QByteArrayLiteral("----PodStackBoundary7MA4YWxkTrZu0gW");
    QByteArray body;
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"file\"; filename=\"image.tar\"\r\n";
    body += "Content-Type: application/x-tar\r\n\r\n";
    body += file.readAll();
    body += "\r\n--" + boundary + "--\r\n";
    file.close();

    const HttpResponse response = m_http.request(
        QByteArrayLiteral("POST"), path(QStringLiteral("/images/load")), body,
        QStringLiteral("multipart/form-data; boundary=") + QString::fromLatin1(boundary),
        600000);
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao carregar a imagem."));
        }
        return false;
    }
    return true;
}

} // namespace PodStack
