// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/LogReader.h"

#include "core/LogRotation.h"
#include "core/Logging.h"
#include "core/Paths.h"
#include "core/Redaction.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace PodStack {
namespace {

int levelValue(const QString &level)
{
    if (level == QStringLiteral("error")) {
        return 0;
    }
    if (level == QStringLiteral("warn")) {
        return 1;
    }
    if (level == QStringLiteral("debug")) {
        return 3;
    }
    if (level == QStringLiteral("trace")) {
        return 4;
    }
    return 2;
}

QString componentFor(const QJsonObject &object)
{
    const QString category = object.value(QStringLiteral("category")).toString();
    if (category.startsWith(QStringLiteral("podstack.setup"))) {
        return QStringLiteral("setup");
    }
    if (category.startsWith(QStringLiteral("podstack.update"))) {
        return QStringLiteral("update");
    }
    if (category == QStringLiteral("qml")) {
        return QStringLiteral("qml");
    }
    const QString process = object.value(QStringLiteral("process")).toString();
    if (process.startsWith(QStringLiteral("podstack-gui"))) {
        return QStringLiteral("gui");
    }
    if (process.startsWith(QStringLiteral("podstack-cli"))) {
        return QStringLiteral("cli");
    }
    return QStringLiteral("daemon");
}

} // namespace

bool readUserLogs(const LogReadOptions &options, LogReadResult *result, Error *error)
{
    if (error) {
        *error = {};
    }
    if (result) {
        *result = LogReadResult();
    }
    const QString directory = options.directory.isEmpty()
                                  ? Paths::stateDir() + QStringLiteral("/logs")
                                  : options.directory;
    const QDate since = options.since.isValid() ? options.since : QDate::currentDate().addDays(-6);
    const int requestedLevel = options.level.isEmpty() ? 4 : levelValue(options.level);
    qint64 bytes = 0;

    const QDir dir(directory);
    const QStringList names =
        dir.entryList({QStringLiteral("*.jsonl")}, QDir::Files | QDir::Readable, QDir::Name);
    for (const QString &name : names) {
        const QFileInfo info(dir.filePath(name));
        if (info.lastModified().date() < since) {
            continue;
        }
        if (result) {
            result->files.append(info.filePath());
        }
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        while (!file.atEnd()) {
            const QString raw = QString::fromUtf8(file.readLine()).trimmed();
            if (raw.isEmpty()) {
                continue;
            }
            const QJsonObject object = QJsonDocument::fromJson(raw.toUtf8()).object();
            const QString level =
                object.value(QStringLiteral("level")).toString(QStringLiteral("info"));
            const QString component = componentFor(object);
            if (levelValue(level) > requestedLevel) {
                continue;
            }
            if (!options.component.isEmpty() && component != options.component) {
                continue;
            }
            const qint64 lineBytes = raw.toUtf8().size();
            if ((result && result->lines.size() >= options.maxLines) ||
                (options.maxBytes > 0 && bytes + lineBytes > options.maxBytes)) {
                if (result) {
                    result->truncated = true;
                }
                return true;
            }
            bytes += lineBytes;
            if (result) {
                LogLine line;
                line.ts = object.value(QStringLiteral("ts")).toString();
                line.level = level;
                line.component = component;
                line.message = Redaction::text(object.value(QStringLiteral("message")).toString());
                line.raw = Redaction::text(raw);
                line.levelValue = levelValue(level);
                result->lines.append(line);
            }
        }
    }
    return true;
}

bool exportLogs(const QVector<LogLine> &lines, const QString &path, Error *error)
{
    if (error) {
        *error = {};
    }
    const QFileInfo target(path);
    const QString canonicalDir = target.absoluteDir().canonicalPath();
    if (canonicalDir.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Destino de export inválido."), path);
        }
        return false;
    }
    const QString destination = canonicalDir + QLatin1Char('/') + target.fileName();
    if (QFileInfo::exists(destination) && QFileInfo(destination).isSymLink()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("O destino de export é um symlink."), destination);
        }
        return false;
    }

    QSaveFile file(destination);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível abrir o destino do export."),
                                 destination);
        }
        return false;
    }
    for (const LogLine &line : lines) {
        if (file.write(line.raw.toUtf8() + '\n') < 0) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                     QStringLiteral("Falha ao escrever o export."), destination);
            }
            return false;
        }
    }
    if (!file.commit()) {
        if (error) {
            *error =
                Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                            QStringLiteral("Não foi possível concluir o export."), destination);
        }
        return false;
    }
    QFile::setPermissions(destination, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

bool clearUserLogs(Error *error)
{
    if (error) {
        *error = {};
    }
    if (Logging::logDirectory().isEmpty()) {
        LogRotation::clear(Paths::stateDir() + QStringLiteral("/logs"));
    } else {
        Logging::clear();
    }
    return true;
}

} // namespace PodStack
