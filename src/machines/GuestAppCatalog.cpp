// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/GuestAppCatalog.h"

#include "core/Paths.h"
#include "machines/GuestChannel.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>

namespace PodStack {
namespace {

constexpr int kMaxApps = 512;
constexpr int kMaxIconBytes = 256 * 1024;
constexpr int kMaxTotalIconBytes = 4 * 1024 * 1024;
constexpr int kCatalogTimeoutMs = 30000;

constexpr char kSignatureMarker[] = "__PODSTACK_SIG__";
constexpr char kFileMarker[] = "__PODSTACK_FILE__";
constexpr char kIconMarker[] = "__PODSTACK_ICON__";

constexpr char kLinuxScript[] =
    "files=$(find /usr/share/applications \"$HOME/.local/share/applications\" "
    "-maxdepth 1 -type f -name '*.desktop' 2>/dev/null | head -n 512)\n"
    "sig=$( (for f in $files; do stat -c '%Y %n' \"$f\" 2>/dev/null; done) | sort | "
    "md5sum | cut -d' ' -f1)\n"
    "echo \"__PODSTACK_SIG__$sig\"\n"
    "for f in $files; do\n"
    "  echo \"__PODSTACK_FILE__$f\"\n"
    "  cat \"$f\" 2>/dev/null\n"
    "  icon=$(sed -n 's/^Icon=//p' \"$f\" 2>/dev/null | head -n 1)\n"
    "  case \"$icon\" in\n"
    "    /*) p=\"$icon\";;\n"
    "    '') p=\"\";;\n"
    "    *) p=$(find /usr/share/icons /usr/share/pixmaps -type f \\( -name \"$icon.png\" "
    "-o -name \"$icon.svg\" -o -name \"$icon.xpm\" \\) 2>/dev/null | head -n 1);;\n"
    "  esac\n"
    "  echo \"__PODSTACK_ICON__$p\"\n"
    "done\n";

constexpr char kWindowsScript[] =
    "$ErrorActionPreference='SilentlyContinue'\n"
    "$paths=@(\"$env:APPDATA\\Microsoft\\Windows\\Start Menu\\Programs\","
    "\"$env:ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\")\n"
    "$w=New-Object -ComObject WScript.Shell\n"
    "$items=@()\n"
    "Get-ChildItem $paths -Recurse -Filter *.lnk | ForEach-Object {\n"
    "  $s=$w.CreateShortcut($_.FullName)\n"
    "  $items += [pscustomobject]@{name=$_.BaseName;path=$_.FullName;target=$s.TargetPath;"
    "args=$s.Arguments;icon=$s.IconLocation}\n"
    "}\n"
    "$items | ConvertTo-Json -Compress\n";

QString stripExecCodes(const QString &exec)
{
    static const QRegularExpression code(QStringLiteral("%[fFuUdDnNickvm]"));
    static const QRegularExpression escaped(QStringLiteral("%%"));
    QString cleaned = exec;
    cleaned.replace(escaped, QStringLiteral("%"));
    cleaned.remove(code);
    return cleaned.simplified();
}

QJsonObject parseDesktopEntry(const QString &desktopFile, const QString &content)
{
    QHash<QString, QString> keys;
    bool inDesktopEntry = false;
    for (const QString &rawLine : content.split(QLatin1Char('\n'))) {
        const QString line = rawLine.trimmed();
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            inDesktopEntry = line == QStringLiteral("[Desktop Entry]");
            continue;
        }
        if (!inDesktopEntry || line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0) {
            continue;
        }
        const QString key = line.left(equals).trimmed();
        if (!keys.contains(key)) {
            keys.insert(key, line.mid(equals + 1).trimmed());
        }
    }

    const QString type = keys.value(QStringLiteral("Type"), QStringLiteral("Application"));
    if (type != QStringLiteral("Application")) {
        return {};
    }
    if (keys.value(QStringLiteral("NoDisplay")).compare(QStringLiteral("true"),
                                                        Qt::CaseInsensitive)
            == 0
        || keys.value(QStringLiteral("Hidden")).compare(QStringLiteral("true"), Qt::CaseInsensitive)
               == 0
        || keys.value(QStringLiteral("Terminal")).compare(QStringLiteral("true"),
                                                          Qt::CaseInsensitive)
               == 0) {
        return {};
    }
    const QString exec = stripExecCodes(keys.value(QStringLiteral("Exec")));
    if (exec.isEmpty()) {
        return {};
    }

    GuestApp app;
    app.appId = desktopFile;
    app.desktopFile = desktopFile;
    app.name = keys.value(QStringLiteral("Name"));
    if (app.name.isEmpty()) {
        app.name = QFileInfo(desktopFile).completeBaseName();
    }
    app.exec = exec;
    app.categories = keys.value(QStringLiteral("Categories"))
                         .split(QLatin1Char(';'), Qt::SkipEmptyParts);
    return app.toJson();
}

QByteArray readIcon(GuestChannel &channel, const QString &path, qint64 *budget)
{
    if (path.isEmpty() || *budget <= 0) {
        return {};
    }
    QByteArray data;
    const qint64 limit = qMin<qint64>(kMaxIconBytes, *budget);
    if (!channel.readFile(path, limit, &data, nullptr) || data.isEmpty()) {
        return {};
    }
    *budget -= data.size();
    return data.toBase64();
}

QJsonArray parseLinuxCatalog(GuestChannel &channel, const QByteArray &output)
{
    QJsonArray items;
    QString currentPath;
    QString currentContent;
    QHash<QString, QString> iconCache;
    qint64 iconBudget = kMaxTotalIconBytes;

    const QString text = QString::fromUtf8(output);
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (line.startsWith(QString::fromLatin1(kFileMarker))) {
            currentPath = line.mid(static_cast<int>(qstrlen(kFileMarker)));
            currentContent.clear();
            continue;
        }
        if (line.startsWith(QString::fromLatin1(kIconMarker))) {
            if (!currentPath.isEmpty() && items.size() < kMaxApps) {
                const QJsonObject item = parseDesktopEntry(currentPath, currentContent);
                if (!item.isEmpty()) {
                    QJsonObject copy = item;
                    const QString iconPath =
                        line.mid(static_cast<int>(qstrlen(kIconMarker))).trimmed();
                    if (!iconPath.isEmpty()) {
                        if (!iconCache.contains(iconPath)) {
                            iconCache.insert(iconPath,
                                             QString::fromLatin1(
                                                 readIcon(channel, iconPath, &iconBudget)));
                        }
                        copy.insert(QStringLiteral("icon"), iconCache.value(iconPath));
                    }
                    items.append(copy);
                }
            }
            currentPath.clear();
            currentContent.clear();
            continue;
        }
        if (!currentPath.isEmpty()) {
            currentContent += line + QLatin1Char('\n');
        }
    }
    return items;
}

QJsonArray parseWindowsCatalog(GuestChannel &channel, const QByteArray &output)
{
    QJsonArray items;
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(output.trimmed(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return items;
    }
    QJsonArray source;
    if (document.isArray()) {
        source = document.array();
    } else if (document.isObject()) {
        source.append(document.object());
    }

    QHash<QString, QString> iconCache;
    qint64 iconBudget = kMaxTotalIconBytes;
    for (const QJsonValue &value : source) {
        if (!value.isObject() || items.size() >= kMaxApps) {
            continue;
        }
        const QJsonObject object = value.toObject();
        const QString path = object.value(QStringLiteral("path")).toString();
        const QString target = object.value(QStringLiteral("target")).toString();
        if (path.isEmpty() || target.isEmpty()) {
            continue;
        }
        GuestApp app;
        app.appId = path;
        app.desktopFile = path;
        app.name = object.value(QStringLiteral("name")).toString();
        if (app.name.isEmpty()) {
            app.name = QFileInfo(path).completeBaseName();
        }
        app.exec = target;
        const QString args = object.value(QStringLiteral("args")).toString().trimmed();
        if (!args.isEmpty()) {
            app.exec += QLatin1Char(' ') + args;
        }

        const QString iconLocation = object.value(QStringLiteral("icon")).toString();
        QString iconPath = iconLocation;
        const int comma = iconPath.lastIndexOf(QLatin1Char(','));
        if (comma > 0) {
            iconPath.truncate(comma);
        }
        iconPath = iconPath.trimmed();
        if (QFileInfo(iconPath).isAbsolute() && !iconPath.isEmpty()) {
            if (!iconCache.contains(iconPath)) {
                iconCache.insert(iconPath,
                                 QString::fromLatin1(readIcon(channel, iconPath, &iconBudget)));
            }
            QJsonObject json = app.toJson();
            json.insert(QStringLiteral("icon"), iconCache.value(iconPath));
            items.append(json);
        } else {
            items.append(app.toJson());
        }
    }
    return items;
}

Error guestUnreachable(const QString &machineId, const QString &detail = {})
{
    Error error = Error::make(QStringLiteral("PODSTACK_DEPENDENCY_MISSING"),
                              QStringLiteral("O canal do guest não respondeu para listar apps."),
                              detail.isEmpty() ? machineId : detail,
                              QStringLiteral("Inicie a machine e confirme o agente/SSH."));
    error.reason = QStringLiteral("guest_unreachable");
    return error;
}

} // namespace

QJsonObject GuestApp::toJson(int runningSessions) const
{
    QJsonObject object;
    object.insert(QStringLiteral("appId"), appId);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("icon"), icon);
    QJsonArray categoryArray;
    for (const QString &category : categories) {
        categoryArray.append(category);
    }
    object.insert(QStringLiteral("categories"), categoryArray);
    object.insert(QStringLiteral("desktopFile"), desktopFile);
    object.insert(QStringLiteral("exec"), exec);
    object.insert(QStringLiteral("runningSessions"), runningSessions);
    return object;
}

QJsonArray GuestAppCatalog::list(const MachineMeta &machine, bool refresh, Error *error)
{
    if (error) {
        *error = {};
    }
    const QString directory = Paths::machineDir(machine.id);
    if (directory.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Machine inválida para o catálogo."), machine.id);
        }
        return {};
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_cache.constFind(machine.id);
        if (!refresh && it != m_cache.constEnd()
            && it->cachedAt.msecsTo(QDateTime::currentDateTimeUtc()) < kCacheTtlMs) {
            return it->items;
        }
    }

    GuestChannel channel(directory + QStringLiteral("/qga.sock"));
    Error pingError;
    if (!channel.ping(&pingError)) {
        if (error) {
            *error = guestUnreachable(machine.id, pingError.message);
        }
        return {};
    }

    const bool windows = machine.provisioner == QStringLiteral("windows");
    int exitCode = -1;
    QByteArray output;
    QByteArray stderrData;
    Error execError;
    const bool executed =
        windows ? channel.exec(QStringLiteral("powershell.exe"),
                               {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                                QStringLiteral("-Command"),
                                QString::fromLatin1(kWindowsScript)},
                               kCatalogTimeoutMs, &exitCode, &output, &stderrData, &execError)
                : channel.exec(QStringLiteral("/bin/sh"),
                               {QStringLiteral("-c"), QString::fromLatin1(kLinuxScript)},
                               kCatalogTimeoutMs, &exitCode, &output, &stderrData, &execError);
    if (!executed || exitCode != 0) {
        if (error) {
            *error = guestUnreachable(machine.id, execError.message);
        }
        return {};
    }

    QJsonArray items;
    QString signature;
    if (windows) {
        items = parseWindowsCatalog(channel, output);
        signature = QString::fromLatin1(
            QCryptographicHash::hash(output, QCryptographicHash::Sha256).toHex());
    } else {
        const int signaturePosition = output.indexOf(kSignatureMarker);
        if (signaturePosition >= 0) {
            const int end = output.indexOf('\n', signaturePosition);
            signature = QString::fromLatin1(
                output.mid(signaturePosition + static_cast<int>(qstrlen(kSignatureMarker)),
                           end - signaturePosition - static_cast<int>(qstrlen(kSignatureMarker))))
                            .trimmed();
        }
        items = parseLinuxCatalog(channel, output);
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        Cache cache;
        cache.items = items;
        cache.cachedAt = QDateTime::currentDateTimeUtc();
        cache.signature = signature;
        m_cache.insert(machine.id, cache);
    }
    return items;
}

void GuestAppCatalog::invalidate(const QString &machineId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.remove(machineId);
}

QDateTime GuestAppCatalog::cachedAt(const QString &machineId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cache.value(machineId).cachedAt;
}

} // namespace PodStack
