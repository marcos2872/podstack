// SPDX-License-Identifier: GPL-3.0-or-later
#include "podman/client/Common.h"

using namespace PodStack::PodmanInternal;

namespace PodStack {

QVector<PodInfo> PodmanClient::listPods(Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return {};
    }

    const HttpResponse response =
        m_http.request(QByteArrayLiteral("GET"), path(QStringLiteral("/pods/json")));
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao listar pods."));
        }
        return {};
    }

    QVector<PodInfo> pods;
    const QJsonArray array = QJsonDocument::fromJson(response.body).array();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        PodInfo info;
        info.id = object.value(QStringLiteral("Id")).toString();
        info.name = object.value(QStringLiteral("Name")).toString();
        if (info.id.isEmpty() && info.name.isEmpty()) {
            continue;
        }
        info.status = object.value(QStringLiteral("Status")).toString();
        info.created = jsonTimestamp(object.value(QStringLiteral("Created")));
        info.infraId = object.value(QStringLiteral("InfraId")).toString();
        info.containers =
            object.value(QStringLiteral("Containers")).toArray().size();
        info.labels = labelsFromJson(object);
        pods.append(info);
    }
    return pods;
}

QJsonObject PodmanClient::createPod(const QString &name, const QMap<QString, QString> &labels,
                                    Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return {};
    }
    if (name.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o nome do pod."));
        }
        return {};
    }

    QJsonObject body;
    body.insert(QStringLiteral("name"), name);
    if (!labels.isEmpty()) {
        body.insert(QStringLiteral("labels"), jsonFromLabels(labels));
    }

    const HttpResponse response =
        m_http.request(QByteArrayLiteral("POST"), path(QStringLiteral("/pods/create")),
                       QJsonDocument(body).toJson(QJsonDocument::Compact),
                       QStringLiteral("application/json"));
    if (response.status != 200 && response.status != 201) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao criar o pod."));
        }
        return {};
    }
    return QJsonDocument::fromJson(response.body).object();
}

bool PodmanClient::podAction(const QString &id, const QString &action, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    if (action != QStringLiteral("start") && action != QStringLiteral("stop")
        && action != QStringLiteral("restart") && action != QStringLiteral("pause")
        && action != QStringLiteral("unpause") && action != QStringLiteral("kill")) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Ação de pod inválida."), action);
        }
        return false;
    }

    const QString suffix = QStringLiteral("/pods/%1/%2").arg(encodeId(id), action);
    const HttpResponse response = m_http.request(QByteArrayLiteral("POST"), path(suffix));
    if (response.status == 200 || response.status == 204) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha na ação do pod."));
    }
    return false;
}

bool PodmanClient::removePod(const QString &id, bool force, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }

    const QString suffix = QStringLiteral("/pods/%1?force=%2")
                               .arg(encodeId(id), force ? QStringLiteral("true")
                                                        : QStringLiteral("false"));
    const HttpResponse response = m_http.request(QByteArrayLiteral("DELETE"), path(suffix));
    if (response.status == 200 || response.status == 204) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao remover o pod."));
    }
    return false;
}

} // namespace PodStack
