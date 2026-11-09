// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/Autostart.h"

#include "core/Config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace PodStack::Autostart {
namespace {

constexpr auto kMarker = "X-PodStack-Autostart=true";

QString quoteExec(const QString &path)
{
    if (!path.contains(QLatin1Char(' ')) && !path.contains(QLatin1Char('\t'))) {
        return path;
    }
    QString escaped = path;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

bool isOwnFile()
{
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    return file.readAll().contains(kMarker);
}

} // namespace

QString filePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/autostart/dev.podstack.PodStack.desktop");
}

bool isEnabled()
{
    return QFile::exists(filePath());
}

bool setEnabled(bool enabled, QString *error)
{
    if (!enabled) {
        if (isEnabled() && !isOwnFile()) {
            return true; // não remove um autostart que não é nosso
        }
        if (isEnabled() && !QFile::remove(filePath())) {
            if (error) {
                *error = QStringLiteral("Não foi possível remover %1.").arg(filePath());
            }
            return false;
        }
        return true;
    }

    const QString directory = QFileInfo(filePath()).absolutePath();
    if (!QDir().mkpath(directory)) {
        if (error) {
            *error = QStringLiteral("Não foi possível criar %1.").arg(directory);
        }
        return false;
    }

    const QString exec = quoteExec(QCoreApplication::applicationFilePath())
        + QStringLiteral(" --hidden");
    const QString content =
        QStringLiteral("[Desktop Entry]\n"
                       "Type=Application\n"
                       "Version=1.0\n"
                       "Name=PodStack\n"
                       "Comment=Containers, Compose, VMs Linux e Kubernetes sobre Podman\n"
                       "Exec=%1\n"
                       "Icon=dev.podstack.PodStack\n"
                       "Terminal=false\n"
                       "Categories=Development;System;\n"
                       "X-GNOME-Autostart-enabled=true\n"
                       "%2\n")
            .arg(exec, QString::fromLatin1(kMarker));

    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("Não foi possível abrir %1: %2")
                         .arg(filePath(), file.errorString());
        }
        return false;
    }
    if (file.write(content.toUtf8()) < 0 || !file.commit()) {
        if (error) {
            *error = QStringLiteral("Não foi possível salvar %1.").arg(filePath());
        }
        return false;
    }
    QFile::setPermissions(filePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

void syncFromConfig()
{
    const Config config = Config::load();
    const bool enabled = config.get(QStringLiteral("app.startAtLogin")).toBool(false);
    if (enabled == isEnabled()) {
        return;
    }
    QString error;
    if (!setEnabled(enabled, &error)) {
        qWarning("podstack: autostart: %s", qUtf8Printable(error));
    }
}

} // namespace PodStack::Autostart
