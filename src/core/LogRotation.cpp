// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/LogRotation.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace PodStack::LogRotation {
namespace {

QString dateSuffix(const QDate &date)
{
    return date.toString(QStringLiteral("yyyyMMdd"));
}

bool ensureDirectory(const QString &path)
{
    if (path.isEmpty()) {
        return false;
    }
    QDir dir;
    if (!dir.mkpath(path)) {
        return false;
    }
    return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                           | QFileDevice::ExeOwner)
           || QFileInfo(path).isDir();
}

class ScopedFileLock {
public:
    explicit ScopedFileLock(const QString &path)
    {
        m_fd = ::open(QFile::encodeName(path).constData(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (m_fd >= 0) {
            (void) ::flock(m_fd, LOCK_EX);
        }
    }

    ~ScopedFileLock()
    {
        if (m_fd >= 0) {
            (void) ::flock(m_fd, LOCK_UN);
            ::close(m_fd);
        }
    }

    ScopedFileLock(const ScopedFileLock &) = delete;
    ScopedFileLock &operator=(const ScopedFileLock &) = delete;

    bool valid() const { return m_fd >= 0; }

private:
    int m_fd = -1;
};

ScopedFileLock lockFor(const Options &options)
{
    return ScopedFileLock(options.directory + QStringLiteral("/.") + options.stem
                          + QStringLiteral(".lock"));
}

bool moveOrAppend(const QString &current, const QString &target)
{
    if (QFile::rename(current, target)) {
        return true;
    }

    QFile source(current);
    QFile destination(target);
    if (!source.open(QIODevice::ReadOnly)
        || !destination.open(QIODevice::WriteOnly | QIODevice::Append)) {
        return false;
    }
    const QByteArray payload = source.readAll();
    if (destination.write(payload) != payload.size()) {
        return false;
    }
    destination.close();
    source.close();
    QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return QFile::remove(current);
}

QDate dateFromName(const QString &name)
{
    static const QRegularExpression pattern(
        QStringLiteral("-(\\d{8})(?:-\\d+)?\\.jsonl$"));
    const QRegularExpressionMatch match = pattern.match(name);
    if (!match.hasMatch()) {
        return {};
    }
    return QDate::fromString(match.captured(1), QStringLiteral("yyyyMMdd"));
}

struct RotatedFile {
    QString path;
    QDate date;
    qint64 size = 0;
    qint64 mtime = 0;
};

QVector<RotatedFile> rotatedFileList(const Options &options)
{
    QVector<RotatedFile> files;
    const QDir dir(options.directory);
    const QStringList names =
        dir.entryList({QStringLiteral("*.jsonl")}, QDir::Files | QDir::Readable);
    for (const QString &name : names) {
        if (name == QFileInfo(currentPath(options)).fileName()) {
            continue;
        }
        const QDate date = dateFromName(name);
        if (!date.isValid()) {
            continue;
        }
        const QFileInfo info(dir.filePath(name));
        files.append({info.filePath(), date, info.size(), info.lastModified().toMSecsSinceEpoch()});
    }
    std::sort(files.begin(), files.end(), [](const RotatedFile &left, const RotatedFile &right) {
        if (left.date != right.date) {
            return left.date < right.date;
        }
        return left.path < right.path;
    });
    return files;
}

} // namespace

QString currentPath(const Options &options)
{
    return options.directory + QLatin1Char('/') + options.stem + QStringLiteral(".jsonl");
}

QString rotatedPath(const Options &options, const QDate &date, int sequence)
{
    const QString suffix = dateSuffix(date);
    if (sequence > 0) {
        return options.directory + QLatin1Char('/') + options.stem + QLatin1Char('-') + suffix
               + QLatin1Char('-') + QString::number(sequence) + QStringLiteral(".jsonl");
    }
    return options.directory + QLatin1Char('/') + options.stem + QLatin1Char('-') + suffix
           + QStringLiteral(".jsonl");
}

bool rotate(const Options &options, const QDate &date, QString *error)
{
    if (error) {
        error->clear();
    }
    if (!ensureDirectory(options.directory)) {
        if (error) {
            *error = QStringLiteral("não foi possível criar o diretório de logs");
        }
        return false;
    }

    const QString current = currentPath(options);
    const QFileInfo currentInfo(current);
    if (!currentInfo.exists() || currentInfo.size() == 0) {
        return true;
    }

    const ScopedFileLock lock = lockFor(options);
    if (!lock.valid()) {
        if (error) {
            *error = QStringLiteral("não foi possível bloquear a rotação");
        }
        return false;
    }

    const QString target = rotatedPath(options, date);
    if (!moveOrAppend(current, target)) {
        if (error) {
            *error = QStringLiteral("não foi possível rotacionar %1").arg(current);
        }
        return false;
    }
    QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

bool rotateForSize(const Options &options, const QDate &date, int *sequence, QString *error)
{
    if (error) {
        error->clear();
    }
    if (sequence) {
        *sequence = 0;
    }
    if (!ensureDirectory(options.directory)) {
        if (error) {
            *error = QStringLiteral("não foi possível criar o diretório de logs");
        }
        return false;
    }

    const QString current = currentPath(options);
    const QFileInfo currentInfo(current);
    if (!currentInfo.exists() || currentInfo.size() == 0) {
        return true;
    }

    const ScopedFileLock lock = lockFor(options);
    if (!lock.valid()) {
        if (error) {
            *error = QStringLiteral("não foi possível bloquear a rotação");
        }
        return false;
    }

    for (int candidate = 1; candidate <= 1000; ++candidate) {
        const QString target = rotatedPath(options, date, candidate);
        if (QFileInfo::exists(target)) {
            continue;
        }
        if (!moveOrAppend(current, target)) {
            continue;
        }
        QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        if (sequence) {
            *sequence = candidate;
        }
        return true;
    }

    if (error) {
        *error = QStringLiteral("não foi possível encontrar um nome para a rotação por tamanho");
    }
    return false;
}

bool rotateAtStart(const Options &options, const QDate &today, QString *error)
{
    if (error) {
        error->clear();
    }
    const QFileInfo current(currentPath(options));
    if (!current.exists() || current.size() == 0) {
        return true;
    }
    const QDate fileDate = current.lastModified().date();
    if (fileDate.isValid() && fileDate < today) {
        return rotate(options, fileDate, error);
    }
    return true;
}

int prune(const Options &options, const QDate &today, QString *error)
{
    if (error) {
        error->clear();
    }
    if (!QFileInfo(options.directory).isDir()) {
        return 0;
    }

    const int retention = std::clamp(options.retentionDays, 1, 30);
    const QDate cutoff = today.addDays(-(retention - 1));
    QVector<RotatedFile> files = rotatedFileList(options);

    int removed = 0;
    for (const RotatedFile &file : files) {
        if (file.date.isValid() && file.date < cutoff) {
            if (QFile::remove(file.path)) {
                ++removed;
            }
        }
    }

    files = rotatedFileList(options);
    qint64 total = QFileInfo(currentPath(options)).size();
    for (const RotatedFile &file : files) {
        total += file.size;
    }
    const qint64 cap = std::max<qint64>(0, options.maxTotalBytes);
    for (const RotatedFile &file : files) {
        if (total <= cap) {
            break;
        }
        if (QFile::remove(file.path)) {
            total -= file.size;
            ++removed;
        }
    }
    return removed;
}

QStringList rotatedFiles(const Options &options)
{
    QVector<RotatedFile> files = rotatedFileList(options);
    std::sort(files.begin(), files.end(), [](const RotatedFile &left, const RotatedFile &right) {
        if (left.date != right.date) {
            return left.date < right.date;
        }
        if (left.mtime != right.mtime) {
            return left.mtime < right.mtime;
        }
        return left.path < right.path;
    });
    QStringList names;
    for (const RotatedFile &file : files) {
        names.append(file.path);
    }
    return names;
}

} // namespace PodStack::LogRotation
