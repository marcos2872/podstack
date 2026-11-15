// SPDX-License-Identifier: GPL-3.0-or-later
#include "podman/client/Internal.h"

#include "core/config/Paths.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <archive.h>
#include <archive_entry.h>

namespace PodStack::PodmanInternal {

QString encodeId(const QString &id)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(id));
}

int jsonInt(const QJsonValue &value, int fallback)
{
    if (value.isDouble()) {
        return value.toInt(fallback);
    }
    if (value.isString()) {
        bool ok = false;
        const int parsed = value.toString().toInt(&ok);
        return ok ? parsed : fallback;
    }
    return fallback;
}

int jsonInt(const QJsonObject &object, const QString &primary, const QString &alternate)
{
    const QJsonValue value = object.value(primary);
    if (!value.isUndefined() && !value.isNull()) {
        return jsonInt(value);
    }
    return jsonInt(object.value(alternate));
}

QString jsonTimestamp(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        const qint64 seconds = static_cast<qint64>(value.toDouble());
        if (seconds > 0) {
            return QDateTime::fromSecsSinceEpoch(seconds, Qt::UTC).toString(Qt::ISODate);
        }
    }
    return {};
}

QString encodePath(const QString &path)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(path));
}

QString normalizeEntryName(QString name)
{
    while (name.startsWith(QStringLiteral("./"))) {
        name.remove(0, 2);
    }
    while (name.startsWith(QLatin1Char('/'))) {
        name.remove(0, 1);
    }
    while (name.endsWith(QLatin1Char('/')) && name.size() > 1) {
        name.chop(1);
    }
    return name;
}

QStringList stringListFromJson(const QJsonArray &array)
{
    QStringList list;
    for (const QJsonValue &value : array) {
        const QString text = value.toString();
        if (!text.isEmpty()) {
            list.append(text);
        }
    }
    return list;
}

QJsonArray jsonFromStringList(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

QMap<QString, QString> labelsFromJson(const QJsonObject &object)
{
    QMap<QString, QString> labels;
    const QJsonObject source = object.value(QStringLiteral("labels")).toObject();
    for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
        labels.insert(it.key(), it.value().toString());
    }
    return labels;
}

QJsonObject jsonFromLabels(const QMap<QString, QString> &labels)
{
    QJsonObject object;
    for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
        object.insert(it.key(), it.value());
    }
    return object;
}

QString statsHumanBytes(qint64 bytes)
{
    static const char *units[] = {"B", "kB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1000.0 && unit < 4) {
        value /= 1000.0;
        ++unit;
    }
    return QString::number(value, 'f', unit == 0 ? 0 : 2) + QString::fromLatin1(units[unit]);
}

QJsonObject formatContainerStats(const QJsonObject &raw)
{
    if (raw.contains(QStringLiteral("cpu_percent")) && raw.contains(QStringLiteral("mem_usage"))) {
        return raw;
    }

    const QJsonArray statsArray = raw.value(QStringLiteral("Stats")).toArray();
    if (!statsArray.isEmpty()) {
        const QJsonObject entry = statsArray.first().toObject();
        QJsonObject out;
        out.insert(QStringLiteral("id"), entry.value(QStringLiteral("ContainerID")).toString());
        out.insert(QStringLiteral("name"), entry.value(QStringLiteral("Name")).toString());

        double cpu = entry.value(QStringLiteral("AvgCPU")).toDouble();
        if (cpu <= 0.0) {
            cpu = entry.value(QStringLiteral("CPU")).toDouble();
        }
        out.insert(QStringLiteral("cpu_percent"), QString::number(cpu, 'f', 2) + QLatin1Char('%'));

        const qint64 memoryUsage =
            static_cast<qint64>(entry.value(QStringLiteral("MemUsage")).toDouble());
        const qint64 memoryLimit =
            static_cast<qint64>(entry.value(QStringLiteral("MemLimit")).toDouble());
        out.insert(QStringLiteral("mem_usage"), statsHumanBytes(memoryUsage) +
                                                    QStringLiteral(" / ") +
                                                    statsHumanBytes(memoryLimit));
        out.insert(QStringLiteral("mem_percent"),
                   QString::number(entry.value(QStringLiteral("MemPerc")).toDouble(), 'f', 2) +
                       QLatin1Char('%'));

        qint64 rxBytes = 0;
        qint64 txBytes = 0;
        const QJsonObject networks = entry.value(QStringLiteral("Network")).toObject();
        for (auto it = networks.constBegin(); it != networks.constEnd(); ++it) {
            const QJsonObject network = it.value().toObject();
            rxBytes += static_cast<qint64>(network.value(QStringLiteral("RxBytes")).toDouble());
            txBytes += static_cast<qint64>(network.value(QStringLiteral("TxBytes")).toDouble());
        }
        out.insert(QStringLiteral("net_io"),
                   statsHumanBytes(rxBytes) + QStringLiteral(" / ") + statsHumanBytes(txBytes));
        out.insert(QStringLiteral("block_io"),
                   statsHumanBytes(
                       static_cast<qint64>(entry.value(QStringLiteral("BlockInput")).toDouble())) +
                       QStringLiteral(" / ") +
                       statsHumanBytes(static_cast<qint64>(
                           entry.value(QStringLiteral("BlockOutput")).toDouble())));
        out.insert(QStringLiteral("pids"),
                   QString::number(entry.value(QStringLiteral("PIDs")).toInt()));
        return out;
    }

    QJsonObject out;
    QString id = raw.value(QStringLiteral("Id")).toString();
    if (id.isEmpty()) {
        id = raw.value(QStringLiteral("id")).toString();
    }
    out.insert(QStringLiteral("id"), id);
    out.insert(QStringLiteral("name"), raw.value(QStringLiteral("name")).toString());

    const QJsonObject cpu = raw.value(QStringLiteral("cpu_stats")).toObject();
    const QJsonObject precpu = raw.value(QStringLiteral("precpu_stats")).toObject();
    const double total = cpu.value(QStringLiteral("cpu_usage"))
                             .toObject()
                             .value(QStringLiteral("total_usage"))
                             .toDouble();
    const double preTotal = precpu.value(QStringLiteral("cpu_usage"))
                                .toObject()
                                .value(QStringLiteral("total_usage"))
                                .toDouble();
    const double system = cpu.value(QStringLiteral("system_cpu_usage")).toDouble();
    const double preSystem = precpu.value(QStringLiteral("system_cpu_usage")).toDouble();
    const int online = cpu.value(QStringLiteral("online_cpus")).toInt(1);
    double cpuPercent = 0.0;
    if (preSystem > 0.0 && system > preSystem && total > preTotal) {
        cpuPercent = ((total - preTotal) / (system - preSystem)) * online * 100.0;
    }
    out.insert(QStringLiteral("cpu_percent"),
               QString::number(cpuPercent, 'f', 2) + QLatin1Char('%'));

    const QJsonObject memory = raw.value(QStringLiteral("memory_stats")).toObject();
    const qint64 memoryUsage =
        static_cast<qint64>(memory.value(QStringLiteral("usage")).toDouble());
    const qint64 memoryLimit =
        static_cast<qint64>(memory.value(QStringLiteral("limit")).toDouble());
    out.insert(QStringLiteral("mem_usage"),
               statsHumanBytes(memoryUsage) + QStringLiteral(" / ") + statsHumanBytes(memoryLimit));
    if (memoryLimit > 0) {
        out.insert(QStringLiteral("mem_percent"),
                   QString::number(memoryUsage * 100.0 / memoryLimit, 'f', 2) + QLatin1Char('%'));
    }

    qint64 rxBytes = 0;
    qint64 txBytes = 0;
    const QJsonObject networks = raw.value(QStringLiteral("networks")).toObject();
    for (auto it = networks.constBegin(); it != networks.constEnd(); ++it) {
        const QJsonObject network = it.value().toObject();
        rxBytes += static_cast<qint64>(network.value(QStringLiteral("rx_bytes")).toDouble());
        txBytes += static_cast<qint64>(network.value(QStringLiteral("tx_bytes")).toDouble());
    }
    out.insert(QStringLiteral("net_io"),
               statsHumanBytes(rxBytes) + QStringLiteral(" / ") + statsHumanBytes(txBytes));

    qint64 readBytes = 0;
    qint64 writeBytes = 0;
    const QJsonArray io = raw.value(QStringLiteral("blkio_stats"))
                              .toObject()
                              .value(QStringLiteral("io_service_bytes_recursive"))
                              .toArray();
    for (const QJsonValue &value : io) {
        const QJsonObject entry = value.toObject();
        const QString op = entry.value(QStringLiteral("op")).toString();
        const qint64 bytes = static_cast<qint64>(entry.value(QStringLiteral("value")).toDouble());
        if (op == QStringLiteral("read")) {
            readBytes += bytes;
        } else if (op == QStringLiteral("write")) {
            writeBytes += bytes;
        }
    }
    out.insert(QStringLiteral("block_io"),
               statsHumanBytes(readBytes) + QStringLiteral(" / ") + statsHumanBytes(writeBytes));
    out.insert(QStringLiteral("pids"), QString::number(raw.value(QStringLiteral("pids_stats"))
                                                           .toObject()
                                                           .value(QStringLiteral("current"))
                                                           .toInt()));
    return out;
}

bool buildContextTar(const QString &contextPath, QByteArray *tar, Error *error)
{
    static constexpr qint64 kMaxContextBytes = 512LL * 1024 * 1024;
    const QDir base(contextPath);
    const auto skipped = [](const QString &relative) {
        return relative == QStringLiteral(".git") || relative.startsWith(QStringLiteral(".git/")) ||
               relative.contains(QStringLiteral("/.git/"));
    };

    qint64 estimated = 65536;
    QDirIterator count(contextPath, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot,
                       QDirIterator::Subdirectories);
    while (count.hasNext()) {
        const QString path = count.next();
        if (skipped(base.relativeFilePath(path))) {
            continue;
        }
        const QFileInfo info(path);
        estimated += info.isFile() ? info.size() + 1024 : 1024;
    }

    if (estimated > kMaxContextBytes) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_CONTEXT_TOO_LARGE"),
                                 QStringLiteral("O contexto de build excede 512 MB."),
                                 QString::number(estimated));
        }
        return false;
    }

    tar->resize(static_cast<int>(estimated));
    size_t used = 0;
    archive *writer = archive_write_new();
    archive_write_set_format_pax_restricted(writer);
    if (archive_write_open_memory(writer, tar->data(), static_cast<size_t>(tar->size()), &used) !=
        ARCHIVE_OK) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_ARCHIVE_ERROR"),
                                 QStringLiteral("Falha ao montar o contexto de build."),
                                 QString::fromUtf8(archive_error_string(writer)));
        }
        archive_write_free(writer);
        return false;
    }

    QDirIterator it(contextPath, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString relative = base.relativeFilePath(path);
        if (skipped(relative)) {
            continue;
        }
        const QFileInfo info(path);
        if (info.isSymLink()) {
            continue;
        }

        const QByteArray entryName = relative.toUtf8();
        archive_entry *entry = archive_entry_new();
        archive_entry_set_pathname(entry, entryName.constData());
        archive_entry_set_perm(entry, info.isDir() ? 0755 : 0644);
        archive_entry_set_mtime(entry, info.lastModified().toSecsSinceEpoch(), 0);
        if (info.isDir()) {
            archive_entry_set_filetype(entry, AE_IFDIR);
            archive_entry_set_size(entry, 0);
        } else {
            archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_size(entry, info.size());
        }
        archive_write_header(writer, entry);

        if (info.isFile()) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) {
                char buffer[8192];
                while (true) {
                    const qint64 bytes = file.read(buffer, sizeof(buffer));
                    if (bytes <= 0) {
                        break;
                    }
                    archive_write_data(writer, buffer, static_cast<size_t>(bytes));
                }
            }
        }
        archive_entry_free(entry);
    }

    archive_write_close(writer);
    archive_write_free(writer);
    tar->resize(static_cast<int>(used));
    return true;
}

} // namespace PodStack::PodmanInternal
