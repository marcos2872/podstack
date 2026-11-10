#include "podman/PodmanClient.h"

#include "core/Paths.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QtEndian>

#include <archive.h>
#include <archive_entry.h>

#include <chrono>
#include <thread>
#include <utility>

namespace PodStack {
namespace {

QString encodeId(const QString &id)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(id));
}

/// O Libpod nativo usa chaves minúsculas (`host_port`) e, em algumas versões,
/// valores numéricos como string; a API compat usa PascalCase. Aceite ambos.
int jsonInt(const QJsonValue &value, int fallback = 0)
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

/// `Created` vem como string ISO na API compat e como epoch (número) no
/// Libpod nativo. Normaliza os dois para uma string exibível.
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
    return QString::number(value, 'f', unit == 0 ? 0 : 2)
           + QString::fromLatin1(units[unit]);
}

QJsonObject formatContainerStats(const QJsonObject &raw)
{
    if (raw.contains(QStringLiteral("cpu_percent"))
        && raw.contains(QStringLiteral("mem_usage"))) {
        return raw;
    }

    const QJsonArray statsArray = raw.value(QStringLiteral("Stats")).toArray();
    if (!statsArray.isEmpty()) {
        const QJsonObject entry = statsArray.first().toObject();
        QJsonObject out;
        out.insert(QStringLiteral("id"),
                   entry.value(QStringLiteral("ContainerID")).toString());
        out.insert(QStringLiteral("name"),
                   entry.value(QStringLiteral("Name")).toString());

        double cpu = entry.value(QStringLiteral("AvgCPU")).toDouble();
        if (cpu <= 0.0) {
            cpu = entry.value(QStringLiteral("CPU")).toDouble();
        }
        out.insert(QStringLiteral("cpu_percent"),
                   QString::number(cpu, 'f', 2) + QLatin1Char('%'));

        const qint64 memoryUsage =
            static_cast<qint64>(entry.value(QStringLiteral("MemUsage")).toDouble());
        const qint64 memoryLimit =
            static_cast<qint64>(entry.value(QStringLiteral("MemLimit")).toDouble());
        out.insert(QStringLiteral("mem_usage"),
                   statsHumanBytes(memoryUsage) + QStringLiteral(" / ")
                       + statsHumanBytes(memoryLimit));
        out.insert(QStringLiteral("mem_percent"),
                   QString::number(entry.value(QStringLiteral("MemPerc")).toDouble(),
                                   'f', 2)
                       + QLatin1Char('%'));

        qint64 rxBytes = 0;
        qint64 txBytes = 0;
        const QJsonObject networks = entry.value(QStringLiteral("Network")).toObject();
        for (auto it = networks.constBegin(); it != networks.constEnd(); ++it) {
            const QJsonObject network = it.value().toObject();
            rxBytes += static_cast<qint64>(
                network.value(QStringLiteral("RxBytes")).toDouble());
            txBytes += static_cast<qint64>(
                network.value(QStringLiteral("TxBytes")).toDouble());
        }
        out.insert(QStringLiteral("net_io"),
                   statsHumanBytes(rxBytes) + QStringLiteral(" / ")
                       + statsHumanBytes(txBytes));
        out.insert(QStringLiteral("block_io"),
                   statsHumanBytes(static_cast<qint64>(
                       entry.value(QStringLiteral("BlockInput")).toDouble()))
                       + QStringLiteral(" / ")
                       + statsHumanBytes(static_cast<qint64>(
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
               statsHumanBytes(memoryUsage) + QStringLiteral(" / ")
                   + statsHumanBytes(memoryLimit));
    if (memoryLimit > 0) {
        out.insert(QStringLiteral("mem_percent"),
                   QString::number(memoryUsage * 100.0 / memoryLimit, 'f', 2)
                       + QLatin1Char('%'));
    }

    qint64 rxBytes = 0;
    qint64 txBytes = 0;
    const QJsonObject networks = raw.value(QStringLiteral("networks")).toObject();
    for (auto it = networks.constBegin(); it != networks.constEnd(); ++it) {
        const QJsonObject network = it.value().toObject();
        rxBytes += static_cast<qint64>(
            network.value(QStringLiteral("rx_bytes")).toDouble());
        txBytes += static_cast<qint64>(
            network.value(QStringLiteral("tx_bytes")).toDouble());
    }
    out.insert(QStringLiteral("net_io"),
               statsHumanBytes(rxBytes) + QStringLiteral(" / ")
                   + statsHumanBytes(txBytes));

    qint64 readBytes = 0;
    qint64 writeBytes = 0;
    const QJsonArray io = raw.value(QStringLiteral("blkio_stats"))
                              .toObject()
                              .value(QStringLiteral("io_service_bytes_recursive"))
                              .toArray();
    for (const QJsonValue &value : io) {
        const QJsonObject entry = value.toObject();
        const QString op = entry.value(QStringLiteral("op")).toString();
        const qint64 bytes = static_cast<qint64>(
            entry.value(QStringLiteral("value")).toDouble());
        if (op == QStringLiteral("read")) {
            readBytes += bytes;
        } else if (op == QStringLiteral("write")) {
            writeBytes += bytes;
        }
    }
    out.insert(QStringLiteral("block_io"),
               statsHumanBytes(readBytes) + QStringLiteral(" / ")
                   + statsHumanBytes(writeBytes));
    out.insert(QStringLiteral("pids"),
               QString::number(raw.value(QStringLiteral("pids_stats"))
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
        return relative == QStringLiteral(".git")
               || relative.startsWith(QStringLiteral(".git/"))
               || relative.contains(QStringLiteral("/.git/"));
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
    if (archive_write_open_memory(writer, tar->data(), static_cast<size_t>(tar->size()), &used)
        != ARCHIVE_OK) {
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


} // namespace

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

QString PodmanClient::path(const QString &suffix) const
{
    std::lock_guard<std::recursive_mutex> lock(m_versionMutex);
    if (m_apiVersion.isEmpty()) {
        return QStringLiteral("/libpod") + suffix;
    }
    return QStringLiteral("/") + m_apiVersion + QStringLiteral("/libpod") + suffix;
}

QString PodmanClient::apiVersion() const
{
    std::lock_guard<std::recursive_mutex> lock(m_versionMutex);
    return m_apiVersion;
}

Error PodmanClient::errorFromResponse(const HttpResponse &response, const QString &fallback)
{
    QString message = fallback;
    QString detail = response.error;

    const QJsonDocument document = QJsonDocument::fromJson(response.body);
    if (document.isObject()) {
        const QJsonObject object = document.object();
        const QString engineMessage = object.value(QStringLiteral("message")).toString();
        if (!engineMessage.isEmpty()) {
            message = engineMessage;
        }
        const QString cause = object.value(QStringLiteral("cause")).toString();
        if (!cause.isEmpty()) {
            detail = cause;
        } else if (detail.isEmpty()) {
            detail = QString::fromUtf8(response.body.left(512));
        }
    } else if (detail.isEmpty()) {
        detail = QString::fromUtf8(response.body.left(512));
    }

    QString code = QStringLiteral("PODSTACK_ENGINE_ERROR");
    if (response.status == 404) {
        code = QStringLiteral("PODSTACK_NOT_FOUND");
    } else if (response.status == 409) {
        code = QStringLiteral("PODSTACK_CONFLICT");
    } else if (response.status == 403) {
        code = QStringLiteral("PODSTACK_PERMISSION_DENIED");
    }

    return Error::make(code, message, detail);
}

bool PodmanClient::ping(Error *error)
{
    std::lock_guard<std::recursive_mutex> lock(m_versionMutex);
    if (error) {
        *error = {};
    }
    const HttpResponse response = m_http.request(QByteArrayLiteral("GET"), QStringLiteral("/_ping"));
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response,
                                       QStringLiteral("O Podman não respondeu ao ping."));
        }
        return false;
    }

    QString version = response.headers.value(QStringLiteral("libpod-api-version"));
    if (version.isEmpty()) {
        version = response.headers.value(QStringLiteral("api-version"));
    }
    if (!version.isEmpty() && !version.startsWith(QLatin1Char('v'))) {
        version.prepend(QLatin1Char('v'));
    }
    m_apiVersion = version;
    return true;
}

bool PodmanClient::ensureVersion(Error *error)
{
    std::lock_guard<std::recursive_mutex> lock(m_versionMutex);
    if (!m_apiVersion.isEmpty()) {
        return true;
    }
    return ping(error);
}

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

QVector<FileEntry> PodmanClient::listFiles(const QString &id, const QString &path, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return {};
    }

    const QString suffix = QStringLiteral("/containers/%1/archive?path=%2")
                               .arg(encodeId(id), encodePath(path));
    const HttpResponse response = m_http.request(QByteArrayLiteral("GET"), this->path(suffix));
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao listar arquivos."));
        }
        return {};
    }

    archive *archive = archive_read_new();
    archive_read_support_filter_all(archive);
    archive_read_support_format_tar(archive);

    if (archive_read_open_memory(archive, response.body.constData(),
                                 static_cast<size_t>(response.body.size()))
        != ARCHIVE_OK) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_ARCHIVE_ERROR"),
                                 QStringLiteral("Falha ao abrir o tar do container."),
                                 QString::fromUtf8(archive_error_string(archive)));
        }
        archive_read_free(archive);
        return {};
    }

    QVector<FileEntry> entries;
    archive_entry *entry = nullptr;
    while (archive_read_next_header(archive, &entry) == ARCHIVE_OK) {
        const char *rawName = archive_entry_pathname(entry);
        if (!rawName) {
            archive_read_data_skip(archive);
            continue;
        }

        const QString name = normalizeEntryName(QString::fromUtf8(rawName));
        if (name.isEmpty() || name == QStringLiteral(".")) {
            archive_read_data_skip(archive);
            continue;
        }

        FileEntry file;
        file.name = name;
        file.size = static_cast<qint64>(archive_entry_size(entry));
        file.directory = archive_entry_filetype(entry) == AE_IFDIR;
        file.mode = QString::number(archive_entry_mode(entry) & 07777, 8).rightJustified(4, '0');
        file.modified = QDateTime::fromSecsSinceEpoch(archive_entry_mtime(entry)).toString(Qt::ISODate);
        entries.append(file);
        archive_read_data_skip(archive);
    }

    archive_read_free(archive);
    return entries;
}

bool PodmanClient::downloadFile(const QString &id, const QString &path,
                                const QString &destination, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }

    const QString suffix = QStringLiteral("/containers/%1/archive?path=%2")
                               .arg(encodeId(id), encodePath(path));
    const HttpResponse response = m_http.request(QByteArrayLiteral("GET"), this->path(suffix));
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao baixar o arquivo."));
        }
        return false;
    }

    archive *archive = archive_read_new();
    archive_read_support_filter_all(archive);
    archive_read_support_format_tar(archive);
    if (archive_read_open_memory(archive, response.body.constData(),
                                 static_cast<size_t>(response.body.size()))
        != ARCHIVE_OK) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_ARCHIVE_ERROR"),
                                 QStringLiteral("Falha ao abrir o tar do container."),
                                 QString::fromUtf8(archive_error_string(archive)));
        }
        archive_read_free(archive);
        return false;
    }

    const QString normalized = normalizeEntryName(path);
    const QString baseName = QFileInfo(path).fileName();
    bool found = false;

    archive_entry *entry = nullptr;
    while (archive_read_next_header(archive, &entry) == ARCHIVE_OK) {
        const char *rawName = archive_entry_pathname(entry);
        if (!rawName || archive_entry_filetype(entry) == AE_IFDIR) {
            archive_read_data_skip(archive);
            continue;
        }

        const QString name = normalizeEntryName(QString::fromUtf8(rawName));
        const bool matches = name == normalized || name == baseName
                             || name.endsWith(QLatin1Char('/') + baseName);
        if (!matches) {
            archive_read_data_skip(archive);
            continue;
        }

        QDir().mkpath(QFileInfo(destination).absolutePath());
        QFile output(destination);
        if (!output.open(QIODevice::WriteOnly)) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                     QStringLiteral("Não foi possível escrever o arquivo."),
                                     output.errorString());
            }
            archive_read_free(archive);
            return false;
        }

        char buffer[8192];
        while (true) {
            const la_ssize_t bytes = archive_read_data(archive, buffer, sizeof(buffer));
            if (bytes < 0) {
                if (error) {
                    *error = Error::make(QStringLiteral("PODSTACK_ARCHIVE_ERROR"),
                                         QStringLiteral("Falha ao extrair o arquivo."),
                                         QString::fromUtf8(archive_error_string(archive)));
                }
                archive_read_free(archive);
                return false;
            }
            if (bytes == 0) {
                break;
            }
            output.write(buffer, static_cast<qint64>(bytes));
        }

        found = true;
        break;
    }

    archive_read_free(archive);

    if (!found) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Arquivo não encontrado no container."), path);
        }
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Imagens
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Volumes
// ---------------------------------------------------------------------------

QVector<VolumeInfo> PodmanClient::listVolumes(Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return {};
    }

    const HttpResponse response =
        m_http.request(QByteArrayLiteral("GET"), path(QStringLiteral("/volumes/json")));
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao listar volumes."));
        }
        return {};
    }

    QHash<QString, QPair<qint64, qint64>> usage;
    QHash<QString, int> links;
    const HttpResponse df =
        m_http.request(QByteArrayLiteral("GET"), path(QStringLiteral("/system/df")));
    if (df.status == 200) {
        const QJsonArray volumes = QJsonDocument::fromJson(df.body)
                                       .object()
                                       .value(QStringLiteral("Volumes"))
                                       .toArray();
        for (const QJsonValue &value : volumes) {
            const QJsonObject object = value.toObject();
            const QString name = object.value(QStringLiteral("VolumeName")).toString();
            if (!name.isEmpty()) {
                usage.insert(name,
                             {static_cast<qint64>(object.value(QStringLiteral("Size")).toDouble()),
                              static_cast<qint64>(
                                  object.value(QStringLiteral("ReclaimableSize")).toDouble())});
                links.insert(name, object.value(QStringLiteral("Links")).toInt());
            }
        }
    }

    QVector<VolumeInfo> volumes;
    const QJsonArray array = QJsonDocument::fromJson(response.body).array();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        VolumeInfo info;
        info.name = object.value(QStringLiteral("Name")).toString();
        if (info.name.isEmpty()) {
            continue;
        }
        info.driver = object.value(QStringLiteral("Driver")).toString();
        info.mountpoint = object.value(QStringLiteral("Mountpoint")).toString();
        info.created = object.value(QStringLiteral("CreatedAt")).toString();
        info.labels = labelsFromJson(object);
        if (usage.contains(info.name)) {
            info.size = usage.value(info.name).first;
            info.reclaimable = usage.value(info.name).second;
        }
        info.links = links.value(info.name, 0);
        volumes.append(info);
    }
    return volumes;
}

QJsonObject PodmanClient::inspectVolume(const QString &name, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return {};
    }

    const QString suffix = QStringLiteral("/volumes/%1/json").arg(encodeId(name));
    const HttpResponse response = m_http.request(QByteArrayLiteral("GET"), path(suffix));
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao inspecionar o volume."));
        }
        return {};
    }
    return QJsonDocument::fromJson(response.body).object();
}

QJsonObject PodmanClient::createVolume(const QString &name, const QString &driver,
                                       const QMap<QString, QString> &labels,
                                       const QMap<QString, QString> &options, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return {};
    }

    QJsonObject body;
    if (!name.isEmpty()) {
        body.insert(QStringLiteral("Name"), name);
    }
    if (!driver.isEmpty()) {
        body.insert(QStringLiteral("Driver"), driver);
    }
    if (!labels.isEmpty()) {
        body.insert(QStringLiteral("Labels"), jsonFromLabels(labels));
    }
    if (!options.isEmpty()) {
        body.insert(QStringLiteral("Options"), jsonFromLabels(options));
    }

    const HttpResponse response =
        m_http.request(QByteArrayLiteral("POST"), path(QStringLiteral("/volumes/create")),
                       QJsonDocument(body).toJson(QJsonDocument::Compact),
                       QStringLiteral("application/json"));
    if (response.status != 200 && response.status != 201) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao criar o volume."));
        }
        return {};
    }
    return QJsonDocument::fromJson(response.body).object();
}

bool PodmanClient::removeVolume(const QString &name, bool force, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }

    const QString suffix = QStringLiteral("/volumes/%1?force=%2")
                               .arg(encodeId(name), force ? QStringLiteral("true")
                                                          : QStringLiteral("false"));
    const HttpResponse response = m_http.request(QByteArrayLiteral("DELETE"), path(suffix));
    if (response.status == 200 || response.status == 204) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response, QStringLiteral("Falha ao remover o volume."));
    }
    return false;
}

bool PodmanClient::pruneVolumes(qint64 *reclaimedBytes, Error *error)
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

    const HttpResponse response =
        m_http.request(QByteArrayLiteral("POST"), path(QStringLiteral("/volumes/prune")));
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao limpar volumes."));
        }
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(response.body);
    qint64 total = 0;
    if (document.isArray()) {
        for (const QJsonValue &value : document.array()) {
            total += static_cast<qint64>(
                value.toObject().value(QStringLiteral("SpaceReclaimed")).toDouble());
        }
    } else if (document.isObject()) {
        total = static_cast<qint64>(
            document.object().value(QStringLiteral("SpaceReclaimed")).toDouble());
    }
    if (reclaimedBytes) {
        *reclaimedBytes = total;
    }
    return true;
}

bool PodmanClient::cloneVolume(const QString &source, const QString &destination, Error *error)
{
    if (error) {
        *error = {};
    }
    if (source.isEmpty() || destination.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o volume de origem e o destino."));
        }
        return false;
    }

    const QJsonObject sourceJson = inspectVolume(source, error);
    if (error && !error->isEmpty()) {
        return false;
    }
    const QString sourcePath = sourceJson.value(QStringLiteral("Mountpoint")).toString();
    if (sourcePath.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_RESPONSE"),
                                 QStringLiteral("O volume de origem não tem mountpoint."));
        }
        return false;
    }

    const QJsonObject destinationJson = createVolume(destination, {}, {}, {}, error);
    if (!destinationJson.isEmpty() && destinationJson.contains(QStringLiteral("Name"))) {
        // criado agora
    } else if (error && error->code == QStringLiteral("PODSTACK_CONFLICT")) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("O volume de destino já existe."));
        }
        return false;
    }

    const QJsonObject destinationInfo = inspectVolume(destination, error);
    if (error && !error->isEmpty()) {
        return false;
    }
    const QString destinationPath =
        destinationInfo.value(QStringLiteral("Mountpoint")).toString();

    const QString cp = QStandardPaths::findExecutable(QStringLiteral("cp"));
    if (cp.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("cp não encontrado no sistema."));
        }
        return false;
    }

    QProcess process;
    process.start(cp, {QStringLiteral("-a"), QStringLiteral("--reflink=auto"),
                       sourcePath + QStringLiteral("/."), destinationPath + QStringLiteral("/")});
    process.waitForFinished(600000);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString detail = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Falha ao clonar o volume."), detail);
        }
        return false;
    }
    return true;
}

bool PodmanClient::exportVolume(const QString &name, const QString &destination, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }

    const QJsonObject volume = inspectVolume(name, error);
    if (error && !error->isEmpty()) {
        return false;
    }
    const QString root = volume.value(QStringLiteral("Mountpoint")).toString();
    if (root.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_RESPONSE"),
                                 QStringLiteral("O volume não tem mountpoint."));
        }
        return false;
    }

    archive *writer = archive_write_new();
    archive_write_add_filter_zstd(writer);
    archive_write_set_format_pax_restricted(writer);
    if (archive_write_open_filename(writer, destination.toUtf8().constData()) != ARCHIVE_OK) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_ARCHIVE_ERROR"),
                                 QStringLiteral("Falha ao criar o arquivo do volume."),
                                 QString::fromUtf8(archive_error_string(writer)));
        }
        archive_write_free(writer);
        return false;
    }

    const QDir base(root);
    QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        if (info.isSymLink()) {
            continue;
        }
        const QByteArray relative = base.relativeFilePath(path).toUtf8();
        archive_entry *entry = archive_entry_new();
        archive_entry_set_pathname(entry, relative.constData());
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
    return true;
}

bool PodmanClient::importVolume(const QString &name, const QString &source, bool force,
                                Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    if (name.isEmpty() || source.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o volume e o arquivo de origem."));
        }
        return false;
    }

    Error inspectError;
    inspectVolume(name, &inspectError);
    if (inspectError.isEmpty() && !force) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                                 QStringLiteral("O volume já existe. Use force para sobrescrever."));
        }
        return false;
    }
    if (!inspectError.isEmpty() && inspectError.code != QStringLiteral("PODSTACK_NOT_FOUND")) {
        if (error) {
            *error = inspectError;
        }
        return false;
    }
    if (inspectError.code == QStringLiteral("PODSTACK_NOT_FOUND")) {
        createVolume(name, {}, {}, {}, error);
        if (error && !error->isEmpty()) {
            return false;
        }
    }

    const QJsonObject volume = inspectVolume(name, error);
    if (error && !error->isEmpty()) {
        return false;
    }
    const QString root = volume.value(QStringLiteral("Mountpoint")).toString();
    if (root.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_RESPONSE"),
                                 QStringLiteral("O volume não tem mountpoint."));
        }
        return false;
    }

    archive *reader = archive_read_new();
    archive_read_support_filter_all(reader);
    archive_read_support_format_tar(reader);
    if (archive_read_open_filename(reader, source.toUtf8().constData(), 10240) != ARCHIVE_OK) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_ARCHIVE_ERROR"),
                                 QStringLiteral("Falha ao abrir o arquivo do volume."),
                                 QString::fromUtf8(archive_error_string(reader)));
        }
        archive_read_free(reader);
        return false;
    }

    archive_entry *entry = nullptr;
    while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
        const char *rawName = archive_entry_pathname(entry);
        if (!rawName) {
            archive_read_data_skip(reader);
            continue;
        }
        const QString relative = normalizeEntryName(QString::fromUtf8(rawName));
        if (relative.isEmpty() || relative == QStringLiteral(".")) {
            archive_read_data_skip(reader);
            continue;
        }
        if (relative.startsWith(QLatin1Char('/')) || relative.contains(QStringLiteral(".."))) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("O arquivo contém caminhos inseguros."),
                                     relative);
            }
            archive_read_free(reader);
            return false;
        }
        if (archive_entry_filetype(entry) == AE_IFLNK) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("Symlinks não são suportados na importação."),
                                     relative);
            }
            archive_read_free(reader);
            return false;
        }

        const QString target = QDir(root).absoluteFilePath(relative);
        if (archive_entry_filetype(entry) == AE_IFDIR) {
            QDir().mkpath(target);
            archive_read_data_skip(reader);
            continue;
        }

        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile file(target);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                     QStringLiteral("Não foi possível escrever o arquivo."),
                                     file.errorString());
            }
            archive_read_free(reader);
            return false;
        }
        char buffer[8192];
        while (true) {
            const la_ssize_t bytes = archive_read_data(reader, buffer, sizeof(buffer));
            if (bytes < 0) {
                if (error) {
                    *error = Error::make(QStringLiteral("PODSTACK_ARCHIVE_ERROR"),
                                         QStringLiteral("Falha ao extrair o volume."),
                                         QString::fromUtf8(archive_error_string(reader)));
                }
                archive_read_free(reader);
                return false;
            }
            if (bytes == 0) {
                break;
            }
            file.write(buffer, static_cast<qint64>(bytes));
        }
    }

    archive_read_free(reader);
    return true;
}

bool PodmanClient::importHostPath(const QString &name, const QString &sourcePath, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    if (name.isEmpty() || sourcePath.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o volume e o diretório de origem."));
        }
        return false;
    }
    if (!QFileInfo(sourcePath).isDir()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O caminho de origem não é um diretório."),
                                 sourcePath);
        }
        return false;
    }

    Error inspectError;
    inspectVolume(name, &inspectError);
    if (!inspectError.isEmpty()) {
        if (inspectError.code == QStringLiteral("PODSTACK_NOT_FOUND")) {
            createVolume(name, {}, {}, {}, error);
            if (error && !error->isEmpty()) {
                return false;
            }
        } else {
            if (error) {
                *error = inspectError;
            }
            return false;
        }
    }

    const QJsonObject volume = inspectVolume(name, error);
    if (error && !error->isEmpty()) {
        return false;
    }
    const QString destinationPath = volume.value(QStringLiteral("Mountpoint")).toString();
    if (destinationPath.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_RESPONSE"),
                                 QStringLiteral("O volume não tem mountpoint."));
        }
        return false;
    }

    const QString cp = QStandardPaths::findExecutable(QStringLiteral("cp"));
    if (cp.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("cp não encontrado no sistema."));
        }
        return false;
    }

    QProcess process;
    process.start(cp, {QStringLiteral("-a"), QStringLiteral("--reflink=auto"),
                       sourcePath + QStringLiteral("/."),
                       destinationPath + QStringLiteral("/")});
    process.waitForFinished(600000);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString detail = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Falha ao importar do host."), detail);
        }
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Redes
// ---------------------------------------------------------------------------

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

        const HttpResponse inspect = m_http.request(
            QByteArrayLiteral("GET"),
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
                                        const QString &subnet, const QString &gateway,
                                        bool ipv6, const QMap<QString, QString> &labels,
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
                                 QStringLiteral("Informe o nome da rede."));
        }
        return {};
    }

    QJsonObject body;
    body.insert(QStringLiteral("name"), name);
    body.insert(QStringLiteral("driver"),
                driver.isEmpty() ? QStringLiteral("bridge") : driver);
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

    const HttpResponse response =
        m_http.request(QByteArrayLiteral("POST"), path(QStringLiteral("/networks/create")),
                       QJsonDocument(body).toJson(QJsonDocument::Compact),
                       QStringLiteral("application/json"));
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

    const QString suffix = QStringLiteral("/networks/%1?force=%2")
                               .arg(encodeId(name), force ? QStringLiteral("true")
                                                          : QStringLiteral("false"));
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
    const HttpResponse response =
        m_http.request(QByteArrayLiteral("POST"), path(suffix),
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

bool PodmanClient::disconnectNetwork(const QString &name, const QString &containerId,
                                     Error *error)
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
    const HttpResponse response =
        m_http.request(QByteArrayLiteral("POST"), path(suffix),
                       QJsonDocument(body).toJson(QJsonDocument::Compact),
                       QStringLiteral("application/json"));
    if (response.status == 200 || response.status == 204) {
        return true;
    }
    if (error) {
        *error = errorFromResponse(response,
                                   QStringLiteral("Falha ao desconectar o container."));
    }
    return false;
}

// ---------------------------------------------------------------------------
// Pods
// ---------------------------------------------------------------------------

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

QJsonObject PodmanClient::systemDf(Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return {};
    }

    const HttpResponse response =
        m_http.request(QByteArrayLiteral("GET"), path(QStringLiteral("/system/df")));
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao ler o uso de disco."));
        }
        return {};
    }
    return QJsonDocument::fromJson(response.body).object();
}

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

bool PodmanClient::uploadFile(const QString &id, const QString &path,
                              const QString &sourcePath, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }

    const QFileInfo info(sourcePath);
    if (!info.exists() || !info.isFile()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Arquivo de origem não encontrado."),
                                 sourcePath);
        }
        return false;
    }

    QFile file(sourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível ler o arquivo."),
                                 file.errorString());
        }
        return false;
    }

    QByteArray tar;
    tar.resize(static_cast<int>(info.size() + 65536));
    size_t used = 0;

    archive *writer = archive_write_new();
    archive_write_set_format_pax_restricted(writer);
    if (archive_write_open_memory(writer, tar.data(), static_cast<size_t>(tar.size()), &used)
        != ARCHIVE_OK) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_ARCHIVE_ERROR"),
                                 QStringLiteral("Falha ao montar o tar de upload."),
                                 QString::fromUtf8(archive_error_string(writer)));
        }
        archive_write_free(writer);
        return false;
    }

    const QByteArray entryName = info.fileName().toUtf8();
    archive_entry *entry = archive_entry_new();
    archive_entry_set_pathname(entry, entryName.constData());
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_entry_set_size(entry, info.size());
    archive_entry_set_mtime(entry, info.lastModified().toSecsSinceEpoch(), 0);
    archive_write_header(writer, entry);

    char buffer[8192];
    while (true) {
        const qint64 bytes = file.read(buffer, sizeof(buffer));
        if (bytes <= 0) {
            break;
        }
        archive_write_data(writer, buffer, static_cast<size_t>(bytes));
    }
    archive_entry_free(entry);
    archive_write_close(writer);
    archive_write_free(writer);
    file.close();

    tar.resize(static_cast<int>(used));

    const QString suffix = QStringLiteral("/containers/%1/archive?path=%2")
                               .arg(encodeId(id), encodePath(path));
    const HttpResponse response = m_http.request(QByteArrayLiteral("PUT"), this->path(suffix),
                                                 tar, QStringLiteral("application/x-tar"),
                                                 600000);
    if (response.status != 200 && response.status != 204) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao enviar o arquivo."));
        }
        return false;
    }
    return true;
}

bool PodmanClient::execOnce(const QString &id, const QStringList &cmd, ExecResult *result,
                            Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }
    if (cmd.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Informe o comando para exec."));
        }
        return false;
    }

    QJsonObject spec;
    spec.insert(QStringLiteral("AttachStdin"), false);
    spec.insert(QStringLiteral("AttachStdout"), true);
    spec.insert(QStringLiteral("AttachStderr"), true);
    spec.insert(QStringLiteral("Tty"), true);

    QJsonArray command;
    for (const QString &part : cmd) {
        command.append(part);
    }
    spec.insert(QStringLiteral("Cmd"), command);

    const QByteArray createBody = QJsonDocument(spec).toJson(QJsonDocument::Compact);
    const HttpResponse create = m_http.request(
        QByteArrayLiteral("POST"), path(QStringLiteral("/containers/%1/exec").arg(encodeId(id))),
        createBody, QStringLiteral("application/json"));
    if (create.status != 201 && create.status != 200) {
        if (error) {
            *error = errorFromResponse(create, QStringLiteral("Falha ao criar o exec."));
        }
        return false;
    }

    const QJsonObject created = QJsonDocument::fromJson(create.body).object();
    const QString execId = created.value(QStringLiteral("Id")).toString();
    if (execId.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_RESPONSE"),
                                 QStringLiteral("O Podman não retornou o ID do exec."));
        }
        return false;
    }

    QJsonObject startSpec;
    startSpec.insert(QStringLiteral("Detach"), false);
    startSpec.insert(QStringLiteral("Tty"), true);
    const QByteArray startBody = QJsonDocument(startSpec).toJson(QJsonDocument::Compact);

    ExecResult local;
    QString hijackError;
    const bool hijacked = m_http.hijack(
        path(QStringLiteral("/exec/%1/start").arg(encodeId(execId))), startBody,
        [&local](const QByteArray &data) {
            local.output.append(data);
            return true;
        },
        nullptr, &hijackError);

    if (!hijacked) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_EXEC_FAILED"),
                                 QStringLiteral("Falha no exec do container."), hijackError);
        }
        return false;
    }

    const HttpResponse inspect =
        m_http.request(QByteArrayLiteral("GET"), path(QStringLiteral("/exec/%1/json").arg(execId)));
    if (inspect.status == 200) {
        const QJsonObject object = QJsonDocument::fromJson(inspect.body).object();
        local.exitCode = object.value(QStringLiteral("ExitCode")).toInt(-1);
    }

    if (result) {
        *result = local;
    }
    return true;
}

bool PodmanClient::streamLogs(const QString &id, int tail, bool follow,
                              const std::function<bool(const QByteArray &)> &onData,
                              std::atomic_bool *cancel, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }

    const QString suffix = QStringLiteral("/containers/%1/logs?follow=%2&stdout=true&stderr=true"
                                          "&tail=%3&timestamps=true")
                               .arg(encodeId(id))
                               .arg(follow ? QStringLiteral("true") : QStringLiteral("false"))
                               .arg(tail);
    QString streamError;

    QByteArray demuxBuffer;
    bool multipart = true;
    bool detected = false;

    const auto onMultiplexedData = [&](const QByteArray &data) {
        demuxBuffer.append(data);

        if (!detected && demuxBuffer.size() >= 8) {
            const auto streamType = static_cast<quint8>(demuxBuffer.at(0));
            const bool zeroPadding = demuxBuffer.at(1) == 0 && demuxBuffer.at(2) == 0
                                     && demuxBuffer.at(3) == 0;
            multipart = streamType <= 2 && zeroPadding;
            detected = true;
        }

        if (detected && !multipart) {
            const QByteArray raw = demuxBuffer;
            demuxBuffer.clear();
            return onData(raw);
        }

        while (multipart && demuxBuffer.size() >= 8) {
            const quint32 length = qFromBigEndian<quint32>(
                reinterpret_cast<const uchar *>(demuxBuffer.constData() + 4));
            if (length > 16 * 1024 * 1024) {
                const QByteArray raw = demuxBuffer;
                demuxBuffer.clear();
                return onData(raw);
            }
            if (demuxBuffer.size() < static_cast<int>(8 + length)) {
                break;
            }
            const QByteArray payload = demuxBuffer.mid(8, static_cast<int>(length));
            demuxBuffer.remove(0, static_cast<int>(8 + length));
            if (!onData(payload)) {
                return false;
            }
        }
        return true;
    };

    const bool ok = m_http.stream(path(suffix), onMultiplexedData, cancel, &streamError);
    if (!demuxBuffer.isEmpty()) {
        onData(demuxBuffer);
    }
    if (!ok && error) {
        *error = Error::make(QStringLiteral("PODSTACK_ENGINE_ERROR"),
                             QStringLiteral("Falha ao ler os logs do container."), streamError);
    }
    return ok;
}

bool PodmanClient::streamStats(const QString &id, int intervalSeconds,
                               const std::function<bool(const QByteArray &)> &onLine,
                               std::atomic_bool *cancel, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }

    const QString suffix = QStringLiteral("/containers/stats?stream=false&containers=%1")
                               .arg(encodeId(id));
    const int safeInterval = qBound(1, intervalSeconds, 60);

    while (true) {
        if (cancel && cancel->load()) {
            return true;
        }

        const HttpResponse response =
            m_http.request(QByteArrayLiteral("GET"), path(suffix), {}, {}, 20000);
        if (response.status != 200) {
            if (error) {
                *error = errorFromResponse(
                    response, QStringLiteral("Falha ao ler as estatísticas do container."));
            }
            return false;
        }

        const QJsonObject raw = QJsonDocument::fromJson(response.body).object();
        const QJsonObject formatted = formatContainerStats(raw);
        if (!formatted.isEmpty()) {
            if (!onLine(QJsonDocument(formatted).toJson(QJsonDocument::Compact))) {
                return true;
            }
        } else if (!onLine(response.body)) {
            return true;
        }

        if (!cancel) {
            return true;
        }

        for (int tick = 0; tick < safeInterval * 10; ++tick) {
            if (cancel->load()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

bool PodmanClient::statsSnapshot(QJsonArray *containers, Error *error)
{
    if (error) {
        *error = {};
    }
    if (containers) {
        *containers = QJsonArray();
    }
    if (!ensureVersion(error)) {
        return false;
    }

    const HttpResponse response =
        m_http.request(QByteArrayLiteral("GET"),
                       path(QStringLiteral("/containers/stats?stream=false&all=true")), {}, {},
                       20000);
    if (response.status != 200) {
        if (error) {
            *error = errorFromResponse(
                response, QStringLiteral("Falha ao ler as estatísticas dos containers."));
        }
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(response.body);
    QJsonArray array;
    if (document.isArray()) {
        array = document.array();
    } else if (document.isObject()) {
        array = document.object().value(QStringLiteral("Stats")).toArray();
    }
    if (containers) {
        *containers = array;
    }
    return true;
}

bool PodmanClient::streamEvents(const std::function<bool(const QByteArray &)> &onData,
                                std::atomic_bool *cancel, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }

    QString streamError;
    const bool ok = m_http.stream(path(QStringLiteral("/events?stream=true")), onData, cancel,
                                  &streamError);
    if (!ok && error) {
        *error = Error::make(QStringLiteral("PODSTACK_ENGINE_ERROR"),
                             QStringLiteral("Falha ao ler os eventos do Podman."), streamError);
    }
    return ok;
}

} // namespace PodStack
