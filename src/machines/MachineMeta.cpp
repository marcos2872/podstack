// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/MachineMeta.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

namespace PodStack {
namespace {

Error ioError(const QString &message, const QString &detail = {})
{
    return Error::make(QStringLiteral("PODSTACK_IO_ERROR"), message, detail);
}

QJsonObject portToJson(const MachinePort &port)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), port.id);
    object.insert(QStringLiteral("hostPort"), port.hostPort);
    object.insert(QStringLiteral("guestPort"), port.guestPort);
    object.insert(QStringLiteral("protocol"), port.protocol);
    object.insert(QStringLiteral("exposeLan"), port.exposeLan);
    object.insert(QStringLiteral("label"), port.label);
    return object;
}

MachinePort portFromJson(const QJsonObject &object)
{
    MachinePort port;
    port.id = object.value(QStringLiteral("id")).toString();
    port.hostPort = object.value(QStringLiteral("hostPort")).toInt();
    port.guestPort = object.value(QStringLiteral("guestPort")).toInt();
    port.protocol = object.value(QStringLiteral("protocol")).toString();
    port.exposeLan = object.value(QStringLiteral("exposeLan")).toBool();
    port.label = object.value(QStringLiteral("label")).toString();
    return port;
}

QJsonObject mountToJson(const MachineMount &mount)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), mount.id);
    object.insert(QStringLiteral("source"), mount.source);
    object.insert(QStringLiteral("target"), mount.target);
    object.insert(QStringLiteral("readOnly"), mount.readOnly);
    object.insert(QStringLiteral("tag"), mount.tag);
    return object;
}

MachineMount mountFromJson(const QJsonObject &object)
{
    MachineMount mount;
    mount.id = object.value(QStringLiteral("id")).toString();
    mount.source = object.value(QStringLiteral("source")).toString();
    mount.target = object.value(QStringLiteral("target")).toString();
    mount.readOnly = object.value(QStringLiteral("readOnly")).toBool();
    mount.tag = object.value(QStringLiteral("tag")).toString();
    return mount;
}

bool ensureParentDirectory(const QString &parent, Error *error)
{
    const QFileInfo info(parent);
    if (info.exists()) {
        if (!info.isDir()) {
            if (error) {
                *error = ioError(QStringLiteral("O caminho do meta.json não é um diretório."),
                                 parent);
            }
            return false;
        }
    } else if (!QDir().mkpath(parent)) {
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível criar o diretório do meta.json."),
                             parent);
        }
        return false;
    }

    if (!QFile::setPermissions(parent, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                           | QFileDevice::ExeOwner)) {
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível restringir o diretório a 0700."),
                             parent);
        }
        return false;
    }
    return true;
}

} // namespace

QJsonObject GuestChannelInfo::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("channel"), channel);
    object.insert(QStringLiteral("role"), role);
    object.insert(QStringLiteral("version"), version);
    object.insert(QStringLiteral("lastSeenAt"), lastSeenAt);
    return object;
}

GuestChannelInfo GuestChannelInfo::fromJson(const QJsonObject &object)
{
    GuestChannelInfo info;
    info.channel = object.value(QStringLiteral("channel")).toString();
    info.role = object.value(QStringLiteral("role")).toString();
    info.version = object.value(QStringLiteral("version")).toString();
    info.lastSeenAt = object.value(QStringLiteral("lastSeenAt")).toString();
    return info;
}

QJsonObject MachineUsbDevice::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("bus"), bus);
    object.insert(QStringLiteral("address"), address);
    object.insert(QStringLiteral("vendorId"), vendorId);
    object.insert(QStringLiteral("productId"), productId);
    object.insert(QStringLiteral("description"), description);
    return object;
}

MachineUsbDevice MachineUsbDevice::fromJson(const QJsonObject &object)
{
    MachineUsbDevice device;
    device.bus = object.value(QStringLiteral("bus")).toInt();
    device.address = object.value(QStringLiteral("address")).toInt();
    device.vendorId = object.value(QStringLiteral("vendorId")).toString();
    device.productId = object.value(QStringLiteral("productId")).toString();
    device.description = object.value(QStringLiteral("description")).toString();
    return device;
}

QJsonObject MachineSnapshot::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("sizeBytes"), sizeBytes);
    object.insert(QStringLiteral("date"), date);
    return object;
}

QJsonObject MachineMeta::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("distro"), distro);
    object.insert(QStringLiteral("distroVersion"), distroVersion);
    object.insert(QStringLiteral("provisioner"), provisioner);
    object.insert(QStringLiteral("arch"), arch);
    object.insert(QStringLiteral("cpus"), cpus);
    object.insert(QStringLiteral("memoryBytes"), memoryBytes);
    object.insert(QStringLiteral("diskBytes"), diskBytes);
    object.insert(QStringLiteral("diskPath"), diskPath);
    object.insert(QStringLiteral("seedPath"), seedPath);
    object.insert(QStringLiteral("sshKeyPath"), sshKeyPath);
    object.insert(QStringLiteral("sshPort"), sshPort);
    object.insert(QStringLiteral("sshUser"), sshUser);
    object.insert(QStringLiteral("state"), state);
    object.insert(QStringLiteral("isolated"), isolated);
    object.insert(QStringLiteral("isolateNetwork"), isolateNetwork);
    object.insert(QStringLiteral("forwardSshAgent"), forwardSshAgent);
    object.insert(QStringLiteral("lastBootAt"), lastBootAt);
    object.insert(QStringLiteral("createdAt"), createdAt);
    object.insert(QStringLiteral("updatedAt"), updatedAt);
    object.insert(QStringLiteral("error"), error);

    QJsonArray portArray;
    for (const MachinePort &port : ports) {
        portArray.append(portToJson(port));
    }
    object.insert(QStringLiteral("ports"), portArray);

    QJsonArray mountArray;
    for (const MachineMount &mount : mounts) {
        mountArray.append(mountToJson(mount));
    }
    object.insert(QStringLiteral("mounts"), mountArray);

    QJsonArray serialArray;
    for (const QString &serialPort : serialPorts) {
        serialArray.append(serialPort);
    }
    object.insert(QStringLiteral("serialPorts"), serialArray);

    QJsonArray usbArray;
    for (const MachineUsbDevice &device : usbDevices) {
        usbArray.append(device.toJson());
    }
    object.insert(QStringLiteral("usbDevices"), usbArray);
    object.insert(QStringLiteral("audioEnabled"), audioEnabled);
    object.insert(QStringLiteral("windowsEdition"), windowsEdition);
    object.insert(QStringLiteral("installerIso"), installerIso);
    object.insert(QStringLiteral("virtioWinIso"), virtioWinIso);
    object.insert(QStringLiteral("windowsSsh"), windowsSsh);
    object.insert(QStringLiteral("installPending"), installPending);
    object.insert(QStringLiteral("errorReason"), errorReason);
    object.insert(QStringLiteral("guestReady"), guestReady);
    object.insert(QStringLiteral("guestOs"), guestOs);
    object.insert(QStringLiteral("origin"),
                  guestChannels.isEmpty() ? QString() : guestChannels.first().channel);
    QJsonArray channelArray;
    for (const GuestChannelInfo &channel : guestChannels) {
        channelArray.append(channel.toJson());
    }
    object.insert(QStringLiteral("guestChannels"), channelArray);
    return object;
}

MachineMeta MachineMeta::fromJson(const QJsonObject &object)
{
    MachineMeta meta;
    meta.id = object.value(QStringLiteral("id")).toString();
    meta.name = object.value(QStringLiteral("name")).toString();
    meta.distro = object.value(QStringLiteral("distro")).toString();
    meta.distroVersion = object.value(QStringLiteral("distroVersion")).toString();
    meta.provisioner = object.value(QStringLiteral("provisioner")).toString();
    if (meta.provisioner.isEmpty()) {
        meta.provisioner = QStringLiteral("linux");
    }
    meta.arch = object.value(QStringLiteral("arch")).toString();
    meta.cpus = object.value(QStringLiteral("cpus")).toInt(2);
    meta.memoryBytes = object.value(QStringLiteral("memoryBytes")).toInteger();
    meta.diskBytes = object.value(QStringLiteral("diskBytes")).toInteger();
    meta.diskPath = object.value(QStringLiteral("diskPath")).toString();
    meta.seedPath = object.value(QStringLiteral("seedPath")).toString();
    meta.sshKeyPath = object.value(QStringLiteral("sshKeyPath")).toString();
    meta.sshPort = object.value(QStringLiteral("sshPort")).toInt();
    meta.sshUser = object.value(QStringLiteral("sshUser")).toString();
    meta.state = object.value(QStringLiteral("state")).toString();
    meta.isolated = object.value(QStringLiteral("isolated")).toBool();
    meta.isolateNetwork = object.value(QStringLiteral("isolateNetwork")).toBool();
    meta.forwardSshAgent = object.value(QStringLiteral("forwardSshAgent")).toBool();
    meta.lastBootAt = object.value(QStringLiteral("lastBootAt")).toString();
    meta.createdAt = object.value(QStringLiteral("createdAt")).toString();
    meta.updatedAt = object.value(QStringLiteral("updatedAt")).toString();
    meta.error = object.value(QStringLiteral("error")).toString();

    const QJsonArray portArray = object.value(QStringLiteral("ports")).toArray();
    for (const QJsonValue &value : portArray) {
        if (value.isObject()) {
            meta.ports.append(portFromJson(value.toObject()));
        }
    }

    const QJsonArray mountArray = object.value(QStringLiteral("mounts")).toArray();
    for (const QJsonValue &value : mountArray) {
        if (value.isObject()) {
            meta.mounts.append(mountFromJson(value.toObject()));
        }
    }

    const QJsonArray serialArray = object.value(QStringLiteral("serialPorts")).toArray();
    for (const QJsonValue &value : serialArray) {
        if (value.isString() && !value.toString().isEmpty()) {
            meta.serialPorts.append(value.toString());
        }
    }

    const QJsonArray usbArray = object.value(QStringLiteral("usbDevices")).toArray();
    for (const QJsonValue &value : usbArray) {
        if (value.isObject()) {
            meta.usbDevices.append(MachineUsbDevice::fromJson(value.toObject()));
        }
    }
    meta.audioEnabled = object.value(QStringLiteral("audioEnabled")).toBool();
    meta.windowsEdition = object.value(QStringLiteral("windowsEdition")).toString();
    meta.installerIso = object.value(QStringLiteral("installerIso")).toString();
    meta.virtioWinIso = object.value(QStringLiteral("virtioWinIso")).toString();
    meta.windowsSsh = object.value(QStringLiteral("windowsSsh")).toBool();
    meta.installPending = object.value(QStringLiteral("installPending")).toBool();
    meta.errorReason = object.value(QStringLiteral("errorReason")).toString();
    meta.guestReady = object.value(QStringLiteral("guestReady")).toBool();
    meta.guestOs = object.value(QStringLiteral("guestOs")).toObject();
    const QJsonArray channelArray = object.value(QStringLiteral("guestChannels")).toArray();
    for (const QJsonValue &value : channelArray) {
        if (value.isObject()) {
            meta.guestChannels.append(GuestChannelInfo::fromJson(value.toObject()));
        }
    }
    return meta;
}

bool MachineMetaFile::write(const QString &path, const MachineMeta &meta, Error *error)
{
    if (error) {
        *error = {};
    }
    if (path.isEmpty()) {
        if (error) {
            *error = ioError(QStringLiteral("Caminho do meta.json vazio."));
        }
        return false;
    }

    const QFileInfo target(path);
    if (target.isDir()) {
        if (error) {
            *error = ioError(QStringLiteral("O caminho do meta.json aponta para um diretório."),
                             path);
        }
        return false;
    }
    if (!ensureParentDirectory(target.absolutePath(), error)) {
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível abrir o meta.json para escrita."),
                             file.errorString());
        }
        return false;
    }

    const QByteArray payload = QJsonDocument(meta.toJson()).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        file.cancelWriting();
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível gravar o meta.json."),
                             file.errorString());
        }
        return false;
    }

    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        file.cancelWriting();
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível restringir o meta.json a 0600."),
                             path);
        }
        return false;
    }

    if (!file.commit()) {
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível publicar o meta.json."),
                             file.errorString());
        }
        return false;
    }
    return true;
}

MachineMeta MachineMetaFile::read(const QString &path, Error *error)
{
    if (error) {
        *error = {};
    }

    QFile file(path);
    if (!file.exists()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("meta.json não encontrado."), path);
        }
        return {};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível ler o meta.json."),
                             file.errorString());
        }
        return {};
    }

    const QByteArray payload = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        if (error) {
            *error = ioError(QStringLiteral("Falha ao ler o meta.json."), file.errorString());
        }
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = ioError(QStringLiteral("meta.json inválido."),
                             parseError.error != QJsonParseError::NoError
                                 ? parseError.errorString()
                                 : QStringLiteral("o conteúdo não é um objeto JSON"));
        }
        return {};
    }
    return MachineMeta::fromJson(document.object());
}

} // namespace PodStack
