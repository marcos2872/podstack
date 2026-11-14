// SPDX-License-Identifier: GPL-3.0-or-later
#include "podman/client/Common.h"

using namespace PodStack::PodmanInternal;

namespace PodStack {

QJsonObject ContainerInfo::toJson() const
{    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("name"), name);
    obj.insert(QStringLiteral("image"), image);
    obj.insert(QStringLiteral("state"), state);
    obj.insert(QStringLiteral("status"), status);
    obj.insert(QStringLiteral("created"), created);
    obj.insert(QStringLiteral("health"), health);

    QJsonArray portArray;
    for (const QString &port : ports) {
        portArray.append(port);
    }
    obj.insert(QStringLiteral("ports"), portArray);

    QJsonObject labelObject;
    for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
        labelObject.insert(it.key(), it.value());
    }
    obj.insert(QStringLiteral("labels"), labelObject);
    return obj;
}

QJsonObject FileEntry::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("size"), static_cast<double>(size));
    object.insert(QStringLiteral("directory"), directory);
    object.insert(QStringLiteral("mode"), mode);
    object.insert(QStringLiteral("modified"), modified);
    return object;
}

QJsonObject ImageInfo::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    QJsonArray tags;
    for (const QString &tag : repoTags) {
        tags.append(tag);
    }
    object.insert(QStringLiteral("repoTags"), tags);
    object.insert(QStringLiteral("digest"), digest);
    object.insert(QStringLiteral("size"), static_cast<double>(size));
    object.insert(QStringLiteral("created"), static_cast<double>(created));
    object.insert(QStringLiteral("containers"), containers);
    object.insert(QStringLiteral("dangling"), dangling);
    QJsonObject labelObject;
    for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
        labelObject.insert(it.key(), it.value());
    }
    object.insert(QStringLiteral("labels"), labelObject);
    return object;
}

QJsonObject ImageHistoryEntry::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("created"), static_cast<double>(created));
    object.insert(QStringLiteral("createdBy"), createdBy);
    object.insert(QStringLiteral("comment"), comment);
    object.insert(QStringLiteral("size"), static_cast<double>(size));
    return object;
}

QJsonObject VolumeInfo::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("driver"), driver);
    object.insert(QStringLiteral("mountpoint"), mountpoint);
    object.insert(QStringLiteral("created"), created);
    object.insert(QStringLiteral("size"), static_cast<double>(size));
    object.insert(QStringLiteral("reclaimable"), static_cast<double>(reclaimable));
    object.insert(QStringLiteral("links"), links);
    QJsonObject labelObject;
    for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
        labelObject.insert(it.key(), it.value());
    }
    object.insert(QStringLiteral("labels"), labelObject);
    return object;
}

QJsonObject NetworkInfo::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("driver"), driver);
    object.insert(QStringLiteral("interface"), interfaceName);
    object.insert(QStringLiteral("subnet"), subnet);
    object.insert(QStringLiteral("gateway"), gateway);
    object.insert(QStringLiteral("created"), created);
    object.insert(QStringLiteral("ipv6"), ipv6);
    object.insert(QStringLiteral("dnsEnabled"), dnsEnabled);
    object.insert(QStringLiteral("internal"), internal);
    object.insert(QStringLiteral("containers"), containers);
    QJsonObject labelObject;
    for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
        labelObject.insert(it.key(), it.value());
    }
    object.insert(QStringLiteral("labels"), labelObject);
    return object;
}

QJsonObject PodInfo::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("status"), status);
    object.insert(QStringLiteral("created"), created);
    object.insert(QStringLiteral("infraId"), infraId);
    object.insert(QStringLiteral("containers"), containers);
    QJsonObject labelObject;
    for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
        labelObject.insert(it.key(), it.value());
    }
    object.insert(QStringLiteral("labels"), labelObject);
    return object;
}

QJsonObject ContainerCreateSpec::toJson() const
{
    QJsonObject object;
    if (!image.isEmpty()) {
        object.insert(QStringLiteral("image"), image);
    }
    if (!name.isEmpty()) {
        object.insert(QStringLiteral("name"), name);
    }
    if (!command.isEmpty()) {
        QJsonArray commandArray;
        for (const QString &part : QProcess::splitCommand(command)) {
            commandArray.append(part);
        }
        object.insert(QStringLiteral("command"), commandArray);
    }
    if (!env.isEmpty()) {
        QJsonObject envObject;
        for (auto it = env.constBegin(); it != env.constEnd(); ++it) {
            envObject.insert(it.key(), it.value());
        }
        object.insert(QStringLiteral("env"), envObject);
    }
    if (!ports.isEmpty()) {
        QJsonArray portArray;
        for (const PortMapping &port : ports) {
            QJsonObject entry;
            entry.insert(QStringLiteral("host_ip"), port.hostIp);
            entry.insert(QStringLiteral("host_port"), port.hostPort);
            entry.insert(QStringLiteral("container_port"), port.containerPort);
            entry.insert(QStringLiteral("protocol"), port.protocol);
            portArray.append(entry);
        }
        object.insert(QStringLiteral("portmappings"), portArray);
    }
    if (!mounts.isEmpty()) {
        QJsonArray mountArray;
        for (const MountMapping &mount : mounts) {
            QJsonObject entry;
            entry.insert(QStringLiteral("type"), QStringLiteral("bind"));
            entry.insert(QStringLiteral("source"), mount.source);
            entry.insert(QStringLiteral("destination"), mount.destination);
            QJsonArray options;
            options.append(QStringLiteral("rbind"));
            options.append(mount.readOnly ? QStringLiteral("ro") : QStringLiteral("rw"));
            entry.insert(QStringLiteral("options"), options);
            mountArray.append(entry);
        }
        object.insert(QStringLiteral("mounts"), mountArray);
    }
    if (!labels.isEmpty()) {
        QJsonObject labelObject;
        for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
            labelObject.insert(it.key(), it.value());
        }
        object.insert(QStringLiteral("labels"), labelObject);
    }
    if (!workDir.isEmpty()) {
        object.insert(QStringLiteral("work_dir"), workDir);
    }
    if (!user.isEmpty()) {
        object.insert(QStringLiteral("user"), user);
    }
    if (remove) {
        object.insert(QStringLiteral("remove"), true);
    }
    if (init) {
        object.insert(QStringLiteral("init"), true);
    }
    if (privileged) {
        object.insert(QStringLiteral("privileged"), true);
    }
    return object;
}

PodmanClient::PodmanClient(QString socketPath)
    : m_http(socketPath.isEmpty() ? Paths::podmanSocket() : std::move(socketPath))
{
}

} // namespace PodStack
