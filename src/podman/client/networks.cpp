// SPDX-License-Identifier: GPL-3.0-or-later
#include "podman/client/Common.h"

using namespace PodStack::PodmanInternal;

namespace PodStack {

QVector<NetworkInfo> PodmanClient::listNetworks(Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return {};
    }

    const HttpResponse response =
        m_http.request(QByteArrayLiteral("GET"), path(QStringLiteral("/networks/json")));
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao listar redes."));
        }
        return {};
    }

    QVector<NetworkInfo> networks;
    const QJsonArray array = QJsonDocument::fromJson(response.body).array();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        NetworkInfo info;
        info.name = object.value(QStringLiteral("name")).toString();
        if (info.name.isEmpty()) {
            continue;
        }
        info.id = object.value(QStringLiteral("id")).toString();
        info.driver = object.value(QStringLiteral("driver")).toString();
        info.interfaceName = object.value(QStringLiteral("network_interface")).toString();
        info.created = object.value(QStringLiteral("created")).toString();
        info.ipv6 = object.value(QStringLiteral("ipv6_enabled")).toBool();
        info.dnsEnabled = object.value(QStringLiteral("dns_enabled")).toBool();
        info.internal = object.value(QStringLiteral("internal")).toBool();
        info.labels = labelsFromJson(object);

        const QJsonArray subnets = object.value(QStringLiteral("subnets")).toArray();
        if (!subnets.isEmpty()) {
            const QJsonObject subnet = subnets.first().toObject();
            info.subnet = subnet.value(QStringLiteral("subnet")).toString();
            info.gateway = subnet.value(QStringLiteral("gateway")).toString();
        }

        const HttpResponse inspect =
            m_http.request(QByteArrayLiteral("GET"),
                           path(QStringLiteral("/networks/%1/json").arg(encodeId(info.name))));
        if (inspect.status == 200) {
            info.containers = QJsonDocument::fromJson(inspect.body)
                                  .object()
                                  .value(QStringLiteral("containers"))
                                  .toObject()
                                  .size();
        }
        networks.append(info);
    }
    return networks;
}

QJsonObject PodmanClient::createNetwork(const QString &name, const QString &driver,
                                        const QString &subnet, const QString &gateway, bool ipv6,
                                        const QMap<QString, QString> &labels, Error *error)
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
                                 QStringLiteral("Informe o nome da rede."));
        }
        return {};
    }

    QJsonObject body;
    body.insert(QStringLiteral("name"), name);
    body.insert(QStringLiteral("driver"), driver.isEmpty() ? QStringLiteral("bridge") : driver);
    body.insert(QStringLiteral("ipv6_enabled"), ipv6);
    if (!labels.isEmpty()) {
        body.insert(QStringLiteral("labels"), jsonFromLabels(labels));
    }
    if (!subnet.isEmpty()) {
        QJsonObject subnetObject;
        subnetObject.insert(QStringLiteral("subnet"), subnet);
        if (!gateway.isEmpty()) {
            subnetObject.insert(QStringLiteral("gateway"), gateway);
        }
        body.insert(QStringLiteral("subnets"), QJsonArray{subnetObject});
    }

    const HttpResponse response = m_http.request(
        QByteArrayLiteral("POST"), path(QStringLiteral("/networks/create")),
        QJsonDocument(body).toJson(QJsonDocument::Compact), QStringLiteral("application/json"));
    if (response.status != 200 && response.status != 201) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao criar a rede."));
        }
        return {};
    }
    return QJsonDocument::fromJson(response.body).object();
}

bool PodmanClient::removeNetwork(const QString &name, bool force, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }

    const QString suffix =
        QStringLiteral("/networks/%1?force=%2")
            .arg(encodeId(name), force ? QStringLiteral("true") : QStringLiteral("false"));
    const HttpResponse response = m_http.request(QByteArrayLiteral("DELETE"), path(suffix));
    if (response.status == 200 || response.status == 204) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao remover a rede."));
    }
    return false;
}

bool PodmanClient::connectNetwork(const QString &name, const QString &containerId,
                                  const QStringList &aliases, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }

    QJsonObject body;
    body.insert(QStringLiteral("container"), containerId);
    if (!aliases.isEmpty()) {
        body.insert(QStringLiteral("aliases"), jsonFromStringList(aliases));
    }
    const QString suffix = QStringLiteral("/networks/%1/connect").arg(encodeId(name));
    const HttpResponse response = m_http.request(QByteArrayLiteral("POST"), path(suffix),
                                                 QJsonDocument(body).toJson(QJsonDocument::Compact),
                                                 QStringLiteral("application/json"));
    if (response.status == 200 || response.status == 204) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao conectar o container."));
    }
    return false;
}

bool PodmanClient::disconnectNetwork(const QString &name, const QString &containerId, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }

    QJsonObject body;
    body.insert(QStringLiteral("container"), containerId);
    const QString suffix = QStringLiteral("/networks/%1/disconnect").arg(encodeId(name));
    const HttpResponse response = m_http.request(QByteArrayLiteral("POST"), path(suffix),
                                                 QJsonDocument(body).toJson(QJsonDocument::Compact),
                                                 QStringLiteral("application/json"));
    if (response.status == 200 || response.status == 204) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao desconectar o container."));
    }
    return false;
}

} // namespace PodStack
