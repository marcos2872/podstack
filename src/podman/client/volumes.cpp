// SPDX-License-Identifier: GPL-3.0-or-later
#include "podman/client/Common.h"

using namespace PodStack::PodmanInternal;

namespace PodStack {

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
        const QJsonArray volumes =
            QJsonDocument::fromJson(df.body).object().value(QStringLiteral("Volumes")).toArray();
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

    const HttpResponse response = m_http.request(
        QByteArrayLiteral("POST"), path(QStringLiteral("/volumes/create")),
        QJsonDocument(body).toJson(QJsonDocument::Compact), QStringLiteral("application/json"));
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

    const QString suffix =
        QStringLiteral("/volumes/%1?force=%2")
            .arg(encodeId(name), force ? QStringLiteral("true") : QStringLiteral("false"));
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
    const QString destinationPath = destinationInfo.value(QStringLiteral("Mountpoint")).toString();

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
            *error =
                Error::make(QStringLiteral("PODSTACK_CONFLICT"),
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
                *error =
                    Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                QStringLiteral("O arquivo contém caminhos inseguros."), relative);
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
            *error =
                Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                            QStringLiteral("O caminho de origem não é um diretório."), sourcePath);
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
                       sourcePath + QStringLiteral("/."), destinationPath + QStringLiteral("/")});
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

} // namespace PodStack
