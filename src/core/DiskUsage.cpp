// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/DiskUsage.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QString>

#include <initializer_list>

namespace PodStack {
namespace {

QString firstString(const QJsonObject &object, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const QJsonValue value = object.value(QLatin1String(key));
        if (value.isString()) {
            const QString text = value.toString().trimmed();
            if (!text.isEmpty()) {
                return text;
            }
        }
    }
    return {};
}

qint64 toBytes(const QJsonValue &value)
{
    if (value.isString()) {
        bool ok = false;
        const qint64 parsed = value.toString().toLongLong(&ok);
        return ok ? parsed : 0;
    }
    return static_cast<qint64>(value.toDouble());
}

int toCount(const QJsonValue &value)
{
    return static_cast<int>(toBytes(value));
}

QString containerName(const QJsonObject &object)
{
    const QJsonValue names = object.value(QStringLiteral("Names"));
    if (names.isArray()) {
        for (const QJsonValue &entry : names.toArray()) {
            const QString name = entry.toString().trimmed();
            if (!name.isEmpty()) {
                return name;
            }
        }
    }
    if (names.isString() && !names.toString().trimmed().isEmpty()) {
        return names.toString().trimmed();
    }
    return firstString(object, {"Name", "ContainerName"});
}

QString repositoryFromTags(const QJsonObject &object)
{
    const QJsonArray tags = object.value(QStringLiteral("RepoTags")).toArray();
    for (const QJsonValue &entry : tags) {
        QString reference = entry.toString().trimmed();
        if (reference.isEmpty()) {
            continue;
        }
        const int slash = reference.lastIndexOf(QLatin1Char('/'));
        const int colon = reference.lastIndexOf(QLatin1Char(':'));
        if (colon > slash) {
            reference = reference.left(colon);
        }
        return reference;
    }
    return {};
}

QString tagFromTags(const QJsonObject &object)
{
    const QJsonArray tags = object.value(QStringLiteral("RepoTags")).toArray();
    for (const QJsonValue &entry : tags) {
        const QString reference = entry.toString().trimmed();
        if (reference.isEmpty()) {
            continue;
        }
        const int slash = reference.lastIndexOf(QLatin1Char('/'));
        const int colon = reference.lastIndexOf(QLatin1Char(':'));
        if (colon > slash) {
            return reference.mid(colon + 1);
        }
        return {};
    }
    return {};
}

QJsonObject makeSection(int count, qint64 sizeBytes, qint64 reclaimableBytes,
                        const QJsonArray &items, bool detailed)
{
    QJsonObject section;
    section.insert(QStringLiteral("count"), count);
    section.insert(QStringLiteral("sizeBytes"), static_cast<double>(sizeBytes));
    section.insert(QStringLiteral("reclaimableBytes"), static_cast<double>(reclaimableBytes));
    if (detailed) {
        section.insert(QStringLiteral("items"), items);
    }
    return section;
}

} // namespace

QJsonObject normalizeDiskUsage(const QJsonObject &json, bool detailed)
{
    QJsonArray imageItems;
    qint64 imagesSize = 0;
    qint64 imagesReclaimable = 0;

    for (const QJsonValue &value : json.value(QStringLiteral("Images")).toArray()) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject image = value.toObject();
        const QString id = firstString(image, {"ImageID", "Id", "ID"});
        const QString repository = firstString(image, {"Repository"});
        const QString tag = firstString(image, {"Tag"});
        const QString normalizedRepository =
            repository.isEmpty() ? repositoryFromTags(image) : repository;
        const QString normalizedTag = tag.isEmpty() ? tagFromTags(image) : tag;
        if (id.isEmpty() && normalizedRepository.isEmpty() && normalizedTag.isEmpty()) {
            continue;
        }

        const qint64 size = toBytes(image.value(QStringLiteral("Size")));
        const qint64 uniqueSize =
            image.contains(QStringLiteral("UniqueSize"))
                ? toBytes(image.value(QStringLiteral("UniqueSize")))
                : size;
        const int containers = toCount(image.value(QStringLiteral("Containers")));
        const bool dangling =
            normalizedRepository.isEmpty() || normalizedRepository == QStringLiteral("<none>");
        const bool unused = containers == 0;

        QJsonObject item;
        item.insert(QStringLiteral("id"), id);
        item.insert(QStringLiteral("repository"), normalizedRepository);
        item.insert(QStringLiteral("tag"), normalizedTag);
        item.insert(QStringLiteral("sizeBytes"), static_cast<double>(size));
        item.insert(QStringLiteral("uniqueSizeBytes"), static_cast<double>(uniqueSize));
        item.insert(QStringLiteral("containers"), containers);
        item.insert(QStringLiteral("dangling"), dangling);
        item.insert(QStringLiteral("unused"), unused);
        imageItems.append(item);

        imagesSize += size;
        if (dangling) {
            imagesReclaimable += uniqueSize;
        }
    }

    QJsonArray containerItems;
    qint64 containersSize = 0;
    qint64 containersReclaimable = 0;

    for (const QJsonValue &value : json.value(QStringLiteral("Containers")).toArray()) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject container = value.toObject();
        const QString id = firstString(container, {"ContainerID", "Id", "ID"});
        const QString name = containerName(container);
        if (id.isEmpty() && name.isEmpty()) {
            continue;
        }

        const QString status = firstString(container, {"Status", "State"});
        const QString state = firstString(container, {"State"}).toLower();
        const QString normalizedStatus = status.toLower();
        const bool running = state == QStringLiteral("running")
                             || state == QStringLiteral("paused")
                             || normalizedStatus == QStringLiteral("running")
                             || normalizedStatus == QStringLiteral("paused");
        const qint64 size = toBytes(container.value(QStringLiteral("Size")));

        QJsonObject item;
        item.insert(QStringLiteral("id"), id);
        item.insert(QStringLiteral("name"), name);
        item.insert(QStringLiteral("status"), status);
        item.insert(QStringLiteral("sizeBytes"), static_cast<double>(size));
        item.insert(QStringLiteral("running"), running);
        containerItems.append(item);

        containersSize += size;
        if (!running) {
            containersReclaimable += size;
        }
    }

    QJsonArray volumeItems;
    qint64 volumesSize = 0;
    qint64 volumesReclaimable = 0;

    for (const QJsonValue &value : json.value(QStringLiteral("Volumes")).toArray()) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject volume = value.toObject();
        const QString name = firstString(volume, {"VolumeName", "Name"});
        if (name.isEmpty()) {
            continue;
        }

        const qint64 size = toBytes(volume.value(QStringLiteral("Size")));
        const qint64 reclaimable =
            toBytes(volume.value(QStringLiteral("ReclaimableSize")));
        const int links = toCount(volume.value(QStringLiteral("Links")));
        const bool inUse = links > 0;
        const qint64 effectiveReclaimable = inUse ? 0 : reclaimable;

        QJsonObject item;
        item.insert(QStringLiteral("name"), name);
        item.insert(QStringLiteral("sizeBytes"), static_cast<double>(size));
        item.insert(QStringLiteral("reclaimableBytes"),
                    static_cast<double>(effectiveReclaimable));
        item.insert(QStringLiteral("inUse"), inUse);
        volumeItems.append(item);

        volumesSize += size;
        volumesReclaimable += effectiveReclaimable;
    }

    QJsonObject sections;
    sections.insert(QStringLiteral("images"),
                    makeSection(imageItems.size(), imagesSize, imagesReclaimable, imageItems,
                                detailed));
    sections.insert(QStringLiteral("containers"),
                    makeSection(containerItems.size(), containersSize, containersReclaimable,
                                containerItems, detailed));
    sections.insert(QStringLiteral("volumes"),
                    makeSection(volumeItems.size(), volumesSize, volumesReclaimable, volumeItems,
                                detailed));
    sections.insert(QStringLiteral("buildCache"),
                    makeSection(0, 0, 0, QJsonArray(), detailed));

    QJsonObject result;
    result.insert(QStringLiteral("json"), json);
    result.insert(QStringLiteral("detailed"), detailed);
    result.insert(QStringLiteral("sections"), sections);
    result.insert(QStringLiteral("totalReclaimableBytes"),
                  static_cast<double>(imagesReclaimable + containersReclaimable
                                      + volumesReclaimable));
    return result;
}

} // namespace PodStack
