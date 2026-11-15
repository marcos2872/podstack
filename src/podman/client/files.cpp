// SPDX-License-Identifier: GPL-3.0-or-later
#include "podman/client/Common.h"

using namespace PodStack::PodmanInternal;

namespace PodStack {

QVector<FileEntry> PodmanClient::listFiles(const QString &id, const QString &path, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return {};
    }

    const QString suffix =
        QStringLiteral("/containers/%1/archive?path=%2").arg(encodeId(id), encodePath(path));
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
                                 static_cast<size_t>(response.body.size())) != ARCHIVE_OK) {
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
        file.modified =
            QDateTime::fromSecsSinceEpoch(archive_entry_mtime(entry)).toString(Qt::ISODate);
        entries.append(file);
        archive_read_data_skip(archive);
    }

    archive_read_free(archive);
    return entries;
}

bool PodmanClient::downloadFile(const QString &id, const QString &path, const QString &destination,
                                Error *error)
{
    if (error) {
        *error = {};
    }
    if (!ensureVersion(error)) {
        return false;
    }

    const QString suffix =
        QStringLiteral("/containers/%1/archive?path=%2").arg(encodeId(id), encodePath(path));
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
                                 static_cast<size_t>(response.body.size())) != ARCHIVE_OK) {
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
        const bool matches =
            name == normalized || name == baseName || name.endsWith(QLatin1Char('/') + baseName);
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

bool PodmanClient::uploadFile(const QString &id, const QString &path, const QString &sourcePath,
                              Error *error)
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
                                 QStringLiteral("Arquivo de origem não encontrado."), sourcePath);
        }
        return false;
    }

    QFile file(sourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error =
                Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                            QStringLiteral("Não foi possível ler o arquivo."), file.errorString());
        }
        return false;
    }

    QByteArray tar;
    tar.resize(static_cast<int>(info.size() + 65536));
    size_t used = 0;

    archive *writer = archive_write_new();
    archive_write_set_format_pax_restricted(writer);
    if (archive_write_open_memory(writer, tar.data(), static_cast<size_t>(tar.size()), &used) !=
        ARCHIVE_OK) {
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

    const QString suffix =
        QStringLiteral("/containers/%1/archive?path=%2").arg(encodeId(id), encodePath(path));
    const HttpResponse response = m_http.request(QByteArrayLiteral("PUT"), this->path(suffix), tar,
                                                 QStringLiteral("application/x-tar"), 600000);
    if (response.status != 200 && response.status != 204) {
        if (error) {
            *error = errorFromResponse(response, QStringLiteral("Falha ao enviar o arquivo."));
        }
        return false;
    }
    return true;
}

} // namespace PodStack
