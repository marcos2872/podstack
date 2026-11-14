// SPDX-License-Identifier: GPL-3.0-or-later
#include "podman/client/Common.h"

using namespace PodStack::PodmanInternal;

namespace PodStack {

QVector<ContainerInfo> PodmanClient::listContainers(bool all, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return {};
    }

    const QString suffix = all ? QStringLiteral("/containers/json?all=true")
                               : QStringLiteral("/containers/json?all=false");
    const HttpResponse response = m_http.request(QByteArrayLiteral("GET"), this->path(suffix));
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response,
                                       QStringLiteral("Falha ao listar containers."));
        }
        return {};
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(response.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_RESPONSE"),
                                 QStringLiteral("Resposta inválida ao listar containers."),
                                 parseError.errorString());
        }
        return {};
    }

    QVector<ContainerInfo> result;
    const QJsonArray array = document.array();
    result.reserve(array.size());

    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        ContainerInfo info;
        info.id = object.value(QStringLiteral("Id")).toString();
        if (info.id.isEmpty()) {
            info.id = object.value(QStringLiteral("ID")).toString();
        }

        const QJsonArray names = object.value(QStringLiteral("Names")).toArray();
        if (!names.isEmpty()) {
            info.name = names.first().toString();
            if (info.name.startsWith(QLatin1Char('/'))) {
                info.name.remove(0, 1);
            }
        }
        if (info.name.isEmpty()) {
            info.name = info.id.left(12);
        }

        info.image = object.value(QStringLiteral("Image")).toString();
        info.state = object.value(QStringLiteral("State")).toString();
        info.status = object.value(QStringLiteral("Status")).toString();
        info.created = jsonTimestamp(object.value(QStringLiteral("Created")));

        if (info.status.contains(QStringLiteral("(unhealthy)"))) {
            info.health = QStringLiteral("unhealthy");
        } else if (info.status.contains(QStringLiteral("(health: starting)"))) {
            info.health = QStringLiteral("starting");
        } else if (info.status.contains(QStringLiteral("(healthy)"))) {
            info.health = QStringLiteral("healthy");
        }

        const QJsonArray ports = object.value(QStringLiteral("Ports")).toArray();
        for (const QJsonValue &portValue : ports) {
            const QJsonObject port = portValue.toObject();
            const QString hostIp = port.value(QStringLiteral("HostIp"))
                                       .toString(port.value(QStringLiteral("host_ip")).toString());
            const int hostPort =
                jsonInt(port, QStringLiteral("HostPort"), QStringLiteral("host_port"));
            const int containerPort =
                jsonInt(port, QStringLiteral("ContainerPort"), QStringLiteral("container_port"));
            const QString protocol =
                port.value(QStringLiteral("Protocol"))
                    .toString(
                        port.value(QStringLiteral("protocol")).toString(QStringLiteral("tcp")));

            QString text;
            if (hostPort > 0) {
                if (!hostIp.isEmpty() && hostIp != QStringLiteral("0.0.0.0")) {
                    text = QStringLiteral("%1:%2->%3/%4")
                               .arg(hostIp)
                               .arg(hostPort)
                               .arg(containerPort)
                               .arg(protocol);
                } else {
                    text = QStringLiteral("%1->%2/%3").arg(hostPort).arg(containerPort).arg(protocol);
                }
            } else if (containerPort > 0) {
                text = QStringLiteral("%1/%2").arg(containerPort).arg(protocol);
            }
            if (!text.isEmpty()) {
                info.ports.append(text);
            }
        }

        const QJsonObject labels = object.value(QStringLiteral("Labels")).toObject();
        for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
            info.labels.insert(it.key(), it.value().toString());
        }

        result.append(info);
    }

    return result;
}

bool PodmanClient::startContainer(const QString &id, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    const HttpResponse response = m_http.request(
        QByteArrayLiteral("POST"), path(QStringLiteral("/containers/%1/start").arg(encodeId(id))));
    if (response.status == 204 || response.status == 304) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao iniciar o container."));
    }
    return false;
}

bool PodmanClient::stopContainer(const QString &id, int timeoutSeconds, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    const QString suffix =
        QStringLiteral("/containers/%1/stop?t=%2").arg(encodeId(id)).arg(timeoutSeconds);
    const HttpResponse response = m_http.request(QByteArrayLiteral("POST"), path(suffix));
    if (response.status == 204 || response.status == 304) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao parar o container."));
    }
    return false;
}

bool PodmanClient::restartContainer(const QString &id, int timeoutSeconds, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    const QString suffix =
        QStringLiteral("/containers/%1/restart?t=%2").arg(encodeId(id)).arg(timeoutSeconds);
    const HttpResponse response = m_http.request(QByteArrayLiteral("POST"), path(suffix));
    if (response.status == 204) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao reiniciar o container."));
    }
    return false;
}

bool PodmanClient::removeContainer(const QString &id, bool force, bool volumes, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    const QString suffix = QStringLiteral("/containers/%1?force=%2&volumes=%3")
                               .arg(encodeId(id))
                               .arg(force ? QStringLiteral("true") : QStringLiteral("false"))
                               .arg(volumes ? QStringLiteral("true") : QStringLiteral("false"));
    const HttpResponse response =
        m_http.request(QByteArrayLiteral("DELETE"), path(suffix));
    if (response.status == 204 || response.status == 200) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao remover o container."));
    }
    return false;
}

bool PodmanClient::pauseContainer(const QString &id, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    const HttpResponse response = m_http.request(
        QByteArrayLiteral("POST"), path(QStringLiteral("/containers/%1/pause").arg(encodeId(id))));
    if (response.status == 204) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao pausar o container."));
    }
    return false;
}

bool PodmanClient::checkpointContainer(const QString &id, bool leaveRunning, bool tcpEstablished,
                                       Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    const QString suffix =
        QStringLiteral("/containers/%1/checkpoint?keep=false&leaveRunning=%2"
                       "&tcpEstablished=%3")
            .arg(encodeId(id), leaveRunning ? QStringLiteral("true") : QStringLiteral("false"),
                 tcpEstablished ? QStringLiteral("true") : QStringLiteral("false"));
    const HttpResponse response = m_http.request(QByteArrayLiteral("POST"), path(suffix));
    if (response.status == 200 || response.status == 204) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response,
                                   QStringLiteral("Falha ao criar o checkpoint do container."));
    }
    return false;
}

bool PodmanClient::restoreContainer(const QString &id, bool tcpEstablished, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    const QString suffix =
        QStringLiteral("/containers/%1/restore?tcpEstablished=%2")
            .arg(encodeId(id), tcpEstablished ? QStringLiteral("true") : QStringLiteral("false"));
    const HttpResponse response = m_http.request(QByteArrayLiteral("POST"), path(suffix));
    if (response.status == 200 || response.status == 204) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response,
                                   QStringLiteral("Falha ao restaurar o checkpoint do container."));
    }
    return false;
}

bool PodmanClient::unpauseContainer(const QString &id, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    const HttpResponse response = m_http.request(
        QByteArrayLiteral("POST"), path(QStringLiteral("/containers/%1/unpause").arg(encodeId(id))));
    if (response.status == 204) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao despausar o container."));
    }
    return false;
}

bool PodmanClient::killContainer(const QString &id, const QString &signal, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }

    QString suffix = QStringLiteral("/containers/%1/kill").arg(encodeId(id));
    if (!signal.isEmpty()) {
        suffix += QStringLiteral("?signal=")
                  + QString::fromLatin1(QUrl::toPercentEncoding(signal));
    }

    const HttpResponse response = m_http.request(QByteArrayLiteral("POST"), path(suffix));
    if (response.status == 204) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao matar o container."));
    }
    return false;
}

bool PodmanClient::renameContainer(const QString &id, const QString &name, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    if (name.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o novo nome."));
        }
        return false;
    }

    const QString suffix = QStringLiteral("/containers/%1/rename?name=%2")
                               .arg(encodeId(id), encodeId(name));
    const HttpResponse response = m_http.request(QByteArrayLiteral("POST"), this->path(suffix));
    if (response.status == 204) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao renomear o container."));
    }
    return false;
}

QJsonObject PodmanClient::inspectContainer(const QString &id, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return {};
    }

    const HttpResponse response = m_http.request(
        QByteArrayLiteral("GET"), path(QStringLiteral("/containers/%1/json").arg(encodeId(id))));
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao inspecionar o container."));
        }
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(response.body);
    if (!document.isObject()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_RESPONSE"),
                                 QStringLiteral("Resposta inválida do inspect."));
        }
        return {};
    }
    return document.object();
}

QString PodmanClient::createContainer(const ContainerCreateSpec &spec, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return {};
    }
    if (spec.image.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe a imagem do container."));
        }
        return {};
    }

    const QByteArray body = QJsonDocument(spec.toJson()).toJson(QJsonDocument::Compact);
    const HttpResponse response = m_http.request(QByteArrayLiteral("POST"),
                                                  path(QStringLiteral("/containers/create")),
                                                  body, QStringLiteral("application/json"));
    if (response.status != 201 && response.status != 200) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao criar o container."));
        }
        return {};
    }

    const QJsonObject object = QJsonDocument::fromJson(response.body).object();
    const QString id = object.value(QStringLiteral("Id")).toString();
    if (id.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_RESPONSE"),
                                 QStringLiteral("O Podman não retornou o ID do container."));
        }
        return {};
    }
    return id;
}

} // namespace PodStack
