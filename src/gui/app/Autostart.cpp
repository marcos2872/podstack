// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/app/Autostart.h"

#include "core/config/Config.h"

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
    // O arquivo pode existir sem ser nosso (outro app com o mesmo caminho).
    // Checar só a existência fazia o settings dizer "ligado" sem autostart.
    return isOwnFile();
}

bool setEnabled(bool enabled, QString *error)
{
    if (!enabled) {
        if (!isOwnFile()) {
            return true; // não remove um autostart que não é nosso
        }
        if (!QFile::remove(filePath())) {
            if (error) {
                *error = QStringLiteral("Não foi possível remover %1.").arg(filePath());
            }
            return false;
        }
        return true;
    }

    if (QFile::exists(filePath()) && !isOwnFile()) {
        if (error) {
            *error = QStringLiteral("Já existe um autostart de outro aplicativo em %1.")
                         .arg(filePath());
        }
        return false;
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
