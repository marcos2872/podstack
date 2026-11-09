// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/MachineArchive.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>

#include <archive.h>
#include <archive_entry.h>

#include <utility>

namespace PodStack {
namespace {

constexpr int kChunkBytes = 256 * 1024;
constexpr qint64 kMaxArtifactBytes = 1LL << 40; // 1 TiB por artefato
constexpr qint64 kMaxTotalBytes = 2LL << 40;    // 2 TiB por pacote
constexpr qint64 kMaxManifestBytes = 1024 * 1024;
constexpr int kMaxEntries = 8;

const QString kDiskName = QStringLiteral("disk.qcow2");
const QString kSeedName = QStringLiteral("seed.iso");
const QString kKeyName = QStringLiteral("id_ed25519");
const QString kPublicKeyName = QStringLiteral("id_ed25519.pub");
const QString kManifestName = QStringLiteral("manifest.json");

bool fails(Error *error, const QString &code, const QString &message,
           const QString &detail = {})
{
    if (error) {
        *error = Error::make(code, message, detail);
    }
    return false;
}

bool cancelledNow(const MachineArchive::Cancelled &cancelled)
{
    return cancelled && cancelled();
}

bool cancel(Error *error)
{
    return fails(error, QStringLiteral("PODSTACK_CANCELLED"),
                 QStringLiteral("Operação cancelada."));
}

QString archiveError(struct archive *handle)
{
    const char *message = archive_error_string(handle);
    return message ? QString::fromUtf8(message) : QStringLiteral("erro desconhecido");
}

struct ArchiveGuard {
    struct archive *handle = nullptr;

    ~ArchiveGuard()
    {
        if (handle) {
            archive_read_free(handle);
        }
    }
};

struct ArchiveWriteGuard {
    struct archive *handle = nullptr;

    ~ArchiveWriteGuard()
    {
        if (handle) {
            archive_write_close(handle);
            archive_write_free(handle);
        }
    }
};

bool writeEntry(struct archive *handle, const QString &name, const QByteArray &payload,
                qint64 mtime, Error *error)
{
    struct archive_entry *entry = archive_entry_new();
    if (!entry) {
        return fails(error, QStringLiteral("PODSTACK_INTERNAL"),
                     QStringLiteral("Não foi possível criar a entrada do arquivo."));
    }

    const QByteArray nameBytes = name.toUtf8();
    archive_entry_set_pathname(entry, nameBytes.constData());
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0600);
    archive_entry_set_size(entry, payload.size());
    archive_entry_set_mtime(entry, static_cast<time_t>(mtime), 0);

    const int status = archive_write_header(handle, entry);
    archive_entry_free(entry);
    if (status != ARCHIVE_OK) {
        return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                     QStringLiteral("Falha ao gravar o manifesto no arquivo."),
                     archiveError(handle));
    }

    if (!payload.isEmpty()) {
        const la_ssize_t written = archive_write_data(handle, payload.constData(),
                                                      static_cast<size_t>(payload.size()));
        if (written != payload.size()) {
            return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                         QStringLiteral("Falha ao gravar o manifesto no arquivo."),
                         archiveError(handle));
        }
    }
    if (archive_write_finish_entry(handle) != ARCHIVE_OK) {
        return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                     QStringLiteral("Falha ao finalizar o manifesto no arquivo."),
                     archiveError(handle));
    }
    return true;
}

bool writeFileEntry(struct archive *handle, const QString &name, const QString &path,
                    qint64 *writtenBytes, qint64 totalBytes, QCryptographicHash *hash,
                    const MachineArchive::Progress &progress,
                    const MachineArchive::Cancelled &cancelled, Error *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                     QStringLiteral("Não foi possível ler um arquivo da machine."),
                     file.errorString());
    }
    const QFileInfo info(file);
    if (!info.isFile() || info.size() < 0) {
        return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                     QStringLiteral("Arquivo inválido para exportação."), path);
    }

    struct archive_entry *entry = archive_entry_new();
    if (!entry) {
        return fails(error, QStringLiteral("PODSTACK_INTERNAL"),
                     QStringLiteral("Não foi possível criar a entrada do arquivo."));
    }
    const QByteArray nameBytes = name.toUtf8();
    archive_entry_set_pathname(entry, nameBytes.constData());
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0600);
    archive_entry_set_size(entry, info.size());
    archive_entry_set_mtime(entry, static_cast<time_t>(info.lastModified().toSecsSinceEpoch()),
                            0);

    const int status = archive_write_header(handle, entry);
    archive_entry_free(entry);
    if (status != ARCHIVE_OK) {
        return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                     QStringLiteral("Falha ao gravar um arquivo no pacote."),
                     archiveError(handle));
    }

    QByteArray buffer(kChunkBytes, '\0');
    while (true) {
        if (cancelledNow(cancelled)) {
            return cancel(error);
        }
        const qint64 read = file.read(buffer.data(), buffer.size());
        if (read < 0) {
            return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                         QStringLiteral("Falha ao ler um arquivo da machine."),
                         file.errorString());
        }
        if (read == 0) {
            break;
        }
        hash->addData(QByteArrayView(buffer.constData(), read));

        qint64 offset = 0;
        while (offset < read) {
            const la_ssize_t wrote = archive_write_data(
                handle, buffer.constData() + offset, static_cast<size_t>(read - offset));
            if (wrote <= 0) {
                return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                             QStringLiteral("Falha ao gravar um arquivo no pacote."),
                             archiveError(handle));
            }
            offset += wrote;
        }

        *writtenBytes += read;
        if (progress && totalBytes > 0) {
            progress(QStringLiteral("archive"),
                     static_cast<int>(qBound<qint64>(qint64(0), *writtenBytes * 100 / totalBytes,
                                                     qint64(100))));
        }
    }

    if (archive_write_finish_entry(handle) != ARCHIVE_OK) {
        return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                     QStringLiteral("Falha ao finalizar um arquivo no pacote."),
                     archiveError(handle));
    }
    return true;
}

} // namespace

QString MachineArchiveManifest::formatId()
{
    return QStringLiteral("podstack.machine/1");
}

QJsonObject MachineArchiveManifest::toJson() const
{
    QJsonArray artifactArray;
    for (const MachineArchiveArtifact &artifact : artifacts) {
        artifactArray.append(QJsonObject{
            {QStringLiteral("path"), artifact.path},
            {QStringLiteral("size"), artifact.size},
            {QStringLiteral("sha256"), artifact.sha256},
        });
    }

    return QJsonObject{
        {QStringLiteral("format"), format},
        {QStringLiteral("createdAt"), createdAt},
        {QStringLiteral("name"), name},
        {QStringLiteral("distro"), distro},
        {QStringLiteral("distroVersion"), distroVersion},
        {QStringLiteral("arch"), arch},
        {QStringLiteral("cpus"), cpus},
        {QStringLiteral("memoryBytes"), memoryBytes},
        {QStringLiteral("diskBytes"), diskBytes},
        {QStringLiteral("sshUser"), sshUser},
        {QStringLiteral("isolateNetwork"), isolateNetwork},
        {QStringLiteral("forwardSshAgent"), forwardSshAgent},
        {QStringLiteral("artifacts"), artifactArray},
    };
}

MachineArchiveManifest MachineArchiveManifest::fromJson(const QJsonObject &object, Error *error)
{
    if (error) {
        *error = {};
    }

    MachineArchiveManifest manifest;
    manifest.format = object.value(QStringLiteral("format")).toString();
    if (manifest.format != formatId()) {
        fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
              QStringLiteral("Formato de arquivo de machine não suportado."), manifest.format);
        return {};
    }

    manifest.createdAt = object.value(QStringLiteral("createdAt")).toString();
    manifest.name = object.value(QStringLiteral("name")).toString();
    manifest.distro = object.value(QStringLiteral("distro")).toString();
    manifest.distroVersion = object.value(QStringLiteral("distroVersion")).toString();
    manifest.arch = object.value(QStringLiteral("arch")).toString();
    manifest.cpus = object.value(QStringLiteral("cpus")).toInt(0);
    manifest.memoryBytes =
        static_cast<qint64>(object.value(QStringLiteral("memoryBytes")).toDouble());
    manifest.diskBytes =
        static_cast<qint64>(object.value(QStringLiteral("diskBytes")).toDouble());
    manifest.sshUser = object.value(QStringLiteral("sshUser")).toString();
    manifest.isolateNetwork = object.value(QStringLiteral("isolateNetwork")).toBool();
    manifest.forwardSshAgent = object.value(QStringLiteral("forwardSshAgent")).toBool();

    if (manifest.name.isEmpty() || manifest.distro.isEmpty() || manifest.sshUser.isEmpty()) {
        fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
              QStringLiteral("O manifesto da machine está incompleto."));
        return {};
    }
    static const QRegularExpression sshUserPattern(
        QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.-]*$"));
    if (manifest.sshUser.size() > 32 || !sshUserPattern.match(manifest.sshUser).hasMatch()) {
        fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
              QStringLiteral("O manifesto tem um usuário SSH inválido."), manifest.sshUser);
        return {};
    }
    if (manifest.cpus < 1 || manifest.cpus > 256) {
        fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
              QStringLiteral("O manifesto tem um número de CPUs inválido."));
        return {};
    }
    if (manifest.memoryBytes < 1024LL * 1024LL * 1024LL
        || manifest.memoryBytes > 1024LL * 1024LL * 1024LL * 1024LL
        || manifest.diskBytes < 1024LL * 1024LL * 1024LL
        || manifest.diskBytes > 64LL * 1024LL * 1024LL * 1024LL * 1024LL) {
        fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
              QStringLiteral("O manifesto tem recursos inválidos."));
        return {};
    }
    if (manifest.arch != QStringLiteral("amd64") && manifest.arch != QStringLiteral("arm64")) {
        fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
              QStringLiteral("O manifesto tem uma arquitetura inválida."), manifest.arch);
        return {};
    }

    const QJsonValue artifactsValue = object.value(QStringLiteral("artifacts"));
    if (!artifactsValue.isArray()) {
        fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
              QStringLiteral("O manifesto não lista os artefatos."));
        return {};
    }
    const QJsonArray items = artifactsValue.toArray();
    for (const QJsonValue &value : items) {
        if (!value.isObject()) {
            fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                  QStringLiteral("Artefato inválido no manifesto."));
            return {};
        }
        const QJsonObject item = value.toObject();
        MachineArchiveArtifact artifact;
        artifact.path = item.value(QStringLiteral("path")).toString();
        artifact.size =
            static_cast<qint64>(item.value(QStringLiteral("size")).toDouble());
        artifact.sha256 = item.value(QStringLiteral("sha256")).toString();
        if (artifact.path.isEmpty() || artifact.size < 0 || artifact.sha256.size() != 64) {
            fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                  QStringLiteral("Artefato inválido no manifesto."), artifact.path);
            return {};
        }
        manifest.artifacts.append(artifact);
    }

    return manifest;
}

bool MachineArchive::write(const QString &archivePath, const MachineArchiveManifest &manifest,
                           const QString &diskPath, const QString &seedPath,
                           const QString &sshKeyPath, const QString &sshPublicKeyPath,
                           const Progress &progress, const Cancelled &cancelled, Error *error)
{
    if (error) {
        *error = {};
    }
    if (archivePath.isEmpty()) {
        return fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                     QStringLiteral("Informe o arquivo de destino do export."));
    }

    struct Source {
        QString name;
        QString path;
    };
    const QVector<Source> sources{
        {kDiskName, diskPath},
        {kSeedName, seedPath},
        {kKeyName, sshKeyPath},
        {kPublicKeyName, sshPublicKeyPath},
    };

    qint64 totalBytes = 0;
    for (const Source &source : sources) {
        const QFileInfo info(source.path);
        if (!info.isFile()) {
            return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                         QStringLiteral("Arquivo da machine ausente para exportação."),
                         source.path);
        }
        totalBytes += info.size();
    }

    // O pacote contém a chave privada SSH: nasce 0600 antes de qualquer byte.
    QFile output(archivePath);
    ArchiveWriteGuard guard;
    guard.handle = archive_write_new();
    if (!guard.handle) {
        return fails(error, QStringLiteral("PODSTACK_INTERNAL"),
                     QStringLiteral("Não foi possível iniciar o pacote da machine."));
    }
    if (archive_write_add_filter_zstd(guard.handle) != ARCHIVE_OK) {
        return fails(error, QStringLiteral("PODSTACK_UNSUPPORTED"),
                     QStringLiteral("Este sistema não suporta compressão zstd no libarchive."),
                     archiveError(guard.handle));
    }
    archive_write_set_format_pax_restricted(guard.handle);

    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                     QStringLiteral("Não foi possível criar o arquivo de export."),
                     output.errorString());
    }
    if (!output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        return fails(error, QStringLiteral("PODSTACK_PERMISSION_DENIED"),
                     QStringLiteral("Não foi possível restringir o arquivo de export a 0600."),
                     archivePath);
    }
    if (archive_write_open_fd(guard.handle, static_cast<int>(output.handle())) != ARCHIVE_OK) {
        return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                     QStringLiteral("Não foi possível criar o arquivo de export."),
                     archiveError(guard.handle));
    }

    QVector<MachineArchiveArtifact> artifacts;
    qint64 writtenBytes = 0;
    for (const Source &source : sources) {
        if (cancelledNow(cancelled)) {
            return cancel(error);
        }

        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!writeFileEntry(guard.handle, source.name, source.path, &writtenBytes, totalBytes,
                            &hash, progress, cancelled, error)) {
            return false;
        }
        artifacts.append(MachineArchiveArtifact{
            source.name,
            QFileInfo(source.path).size(),
            QString::fromLatin1(hash.result().toHex()),
        });
    }

    MachineArchiveManifest serialized = manifest;
    serialized.format = MachineArchiveManifest::formatId();
    serialized.artifacts = artifacts;
    const QByteArray payload =
        QJsonDocument(serialized.toJson()).toJson(QJsonDocument::Indented);
    if (!writeEntry(guard.handle, kManifestName, payload,
                    QDateTime::currentSecsSinceEpoch(), error)) {
        return false;
    }

    if (archive_write_close(guard.handle) != ARCHIVE_OK) {
        return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                     QStringLiteral("Falha ao finalizar o arquivo de export."),
                     archiveError(guard.handle));
    }
    archive_write_free(guard.handle);
    guard.handle = nullptr;

    if (progress) {
        progress(QStringLiteral("archive"), 100);
    }
    return true;
}

bool MachineArchive::extract(const QString &archivePath, const QString &directory,
                             MachineArchiveManifest *manifest, Paths *paths,
                             const Progress &progress, const Cancelled &cancelled, Error *error)
{
    if (error) {
        *error = {};
    }
    if (manifest) {
        *manifest = {};
    }
    if (paths) {
        *paths = {};
    }

    const QFileInfo archiveInfo(archivePath);
    if (!archiveInfo.isFile()) {
        return fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                     QStringLiteral("Informe um arquivo de machine existente."), archivePath);
    }
    const QFileInfo directoryInfo(directory);
    if (!directoryInfo.isDir()) {
        return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                     QStringLiteral("Diretório temporário do import indisponível."), directory);
    }

    const QStringList expected{kDiskName, kSeedName, kKeyName, kPublicKeyName, kManifestName};
    const QSet<QString> allowed(expected.begin(), expected.end());

    ArchiveGuard guard;
    guard.handle = archive_read_new();
    if (!guard.handle) {
        return fails(error, QStringLiteral("PODSTACK_INTERNAL"),
                     QStringLiteral("Não foi possível abrir o pacote da machine."));
    }
    archive_read_support_filter_zstd(guard.handle);
    archive_read_support_format_tar(guard.handle);

    const QByteArray archiveBytes = QFile::encodeName(archivePath);
    if (archive_read_open_filename(guard.handle, archiveBytes.constData(), 10240)
        != ARCHIVE_OK) {
        return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                     QStringLiteral("Não foi possível ler o arquivo de machine."),
                     archiveError(guard.handle));
    }

    QSet<QString> seen;
    QHash<QString, QByteArray> hashes;
    QHash<QString, qint64> sizes;
    QByteArray manifestPayload;
    QByteArray buffer(kChunkBytes, '\0');
    int entryCount = 0;
    qint64 declaredTotal = 0;

    while (true) {
        if (cancelledNow(cancelled)) {
            return cancel(error);
        }

        struct archive_entry *entry = nullptr;
        const int status = archive_read_next_header(guard.handle, &entry);
        if (status == ARCHIVE_EOF) {
            break;
        }
        if (status != ARCHIVE_OK) {
            return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                         QStringLiteral("O arquivo de machine está corrompido."),
                         archiveError(guard.handle));
        }

        if (++entryCount > kMaxEntries) {
            return fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                         QStringLiteral("O arquivo de machine tem entradas demais."));
        }

        const QString name = QString::fromUtf8(archive_entry_pathname(entry));
        if (!allowed.contains(name) || seen.contains(name)) {
            return fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                         QStringLiteral("O arquivo de machine contém uma entrada inválida."),
                         name);
        }
        if (archive_entry_filetype(entry) != AE_IFREG) {
            return fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                         QStringLiteral("O arquivo de machine contém uma entrada não regular."),
                         name);
        }
        if (archive_entry_hardlink(entry) != nullptr
            || archive_entry_symlink(entry) != nullptr) {
            return fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                         QStringLiteral("O arquivo de machine contém um link."), name);
        }
        const qint64 declaredSize = archive_entry_size(entry);
        if (declaredSize < 0 || declaredSize > kMaxArtifactBytes) {
            return fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                         QStringLiteral("O arquivo de machine tem um tamanho inválido."), name);
        }
        declaredTotal += declaredSize;
        if (declaredTotal > kMaxTotalBytes) {
            return fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                         QStringLiteral("O arquivo de machine excede o tamanho máximo."));
        }
        seen.insert(name);

        QCryptographicHash hash(QCryptographicHash::Sha256);
        QFile output(directory + QLatin1Char('/') + name);
        if (name != kManifestName) {
            if (!output.open(QIODevice::WriteOnly)) {
                return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                             QStringLiteral("Não foi possível extrair um arquivo da machine."),
                             output.errorString());
            }
            if (!output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
                output.close();
                return fails(error, QStringLiteral("PODSTACK_PERMISSION_DENIED"),
                             QStringLiteral("Não foi possível restringir um arquivo da "
                                            "machine a 0600."),
                             name);
            }
        }

        qint64 total = 0;
        while (true) {
            if (cancelledNow(cancelled)) {
                return cancel(error);
            }
            const la_ssize_t read =
                archive_read_data(guard.handle, buffer.data(), buffer.size());
            if (read < 0) {
                return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                             QStringLiteral("O arquivo de machine está corrompido."),
                             archiveError(guard.handle));
            }
            if (read == 0) {
                break;
            }
            hash.addData(QByteArrayView(buffer.constData(), read));
            total += read;
            if (total > kMaxArtifactBytes) {
                return fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                             QStringLiteral("O arquivo de machine excede o tamanho máximo."),
                             name);
            }
            if (name == kManifestName) {
                if (manifestPayload.size() + read > kMaxManifestBytes) {
                    return fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("O manifesto da machine é grande demais."));
                }
                manifestPayload.append(buffer.constData(), read);
            } else if (output.write(buffer.constData(), read) != read) {
                return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                             QStringLiteral("Não foi possível extrair um arquivo da machine."),
                             output.errorString());
            }
        }

        if (name != kManifestName) {
            output.close();
            if (output.error() != QFileDevice::NoError) {
                return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                             QStringLiteral("Não foi possível extrair um arquivo da machine."),
                             output.errorString());
            }
        }

        if (total != declaredSize) {
            return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                         QStringLiteral("O arquivo de machine está truncado."), name);
        }
        hashes.insert(name, hash.result());
        sizes.insert(name, total);

        if (progress) {
            progress(QStringLiteral("verify"),
                     static_cast<int>(qMin(100, entryCount * 100 / expected.size())));
        }
    }

    for (const QString &name : expected) {
        if (!seen.contains(name)) {
            return fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                          QStringLiteral("O arquivo de machine está incompleto."), name);
        }
    }

    Error manifestError;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifestPayload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                     QStringLiteral("O manifesto da machine é inválido."),
                     parseError.errorString());
    }
    MachineArchiveManifest parsed =
        MachineArchiveManifest::fromJson(document.object(), &manifestError);
    if (!manifestError.isEmpty()) {
        if (error) {
            *error = manifestError;
        }
        return false;
    }

    QSet<QString> declared;
    for (const MachineArchiveArtifact &artifact : parsed.artifacts) {
        if (!allowed.contains(artifact.path) || artifact.path == kManifestName
            || declared.contains(artifact.path)) {
            return fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                          QStringLiteral("O manifesto lista um artefato inválido."),
                          artifact.path);
        }
        declared.insert(artifact.path);

        const QByteArray expectedHash = artifact.sha256.toLatin1().toLower();
        if (sizes.value(artifact.path) != artifact.size
            || hashes.value(artifact.path).toHex() != expectedHash) {
            return fails(error, QStringLiteral("PODSTACK_IO_ERROR"),
                          QStringLiteral("O checksum de um arquivo da machine não confere."),
                          artifact.path);
        }
    }
    if (declared.size() != 4 || !declared.contains(kDiskName) || !declared.contains(kSeedName)
        || !declared.contains(kKeyName) || !declared.contains(kPublicKeyName)) {
        return fails(error, QStringLiteral("PODSTACK_INVALID_PARAMS"),
                     QStringLiteral("O manifesto não descreve todos os arquivos da machine."));
    }

    if (manifest) {
        *manifest = parsed;
    }
    if (paths) {
        paths->directory = directory;
        paths->diskPath = directory + QLatin1Char('/') + kDiskName;
        paths->seedPath = directory + QLatin1Char('/') + kSeedName;
        paths->sshKeyPath = directory + QLatin1Char('/') + kKeyName;
        paths->sshPublicKeyPath = directory + QLatin1Char('/') + kPublicKeyName;
    }
    return true;
}

} // namespace PodStack
