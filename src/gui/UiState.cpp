// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/UiState.h"

#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcessEnvironment>
#include <QSaveFile>

namespace PodStack {

QString UiState::defaultPath()
{
    const QString overridePath =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("PODSTACK_UI_STATE"));
    if (!overridePath.isEmpty()) {
        return overridePath;
    }
    return Paths::stateDir() + QStringLiteral("/ui-state.json");
}

QStringList UiState::supportedSections()
{
    return {QStringLiteral("containers"), QStringLiteral("images"),   QStringLiteral("volumes"),
            QStringLiteral("networks"),   QStringLiteral("pods"),     QStringLiteral("compose"),
            QStringLiteral("builds"),     QStringLiteral("machines"), QStringLiteral("kubernetes")};
}

QStringList UiState::supportedMachineTabs()
{
    return {QStringLiteral("info"), QStringLiteral("logs"), QStringLiteral("stats"),
            QStringLiteral("terminal"), QStringLiteral("files")};
}

bool UiState::isValidSection(const QString &section)
{
    return supportedSections().contains(section);
}

bool UiState::isValidContainerTab(int tab)
{
    return tab >= 0 && tab <= 8;
}

bool UiState::isValidMachineTab(const QString &tab)
{
    return supportedMachineTabs().contains(tab);
}

bool UiState::load(const QString &path)
{
    m_lastPath = path;
    windowGeometry.clear();
    section = QStringLiteral("containers");
    containerTab = 0;
    machineTab = QStringLiteral("info");

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt() != 1) {
        return false;
    }

    const QByteArray geometry =
        QByteArray::fromBase64(root.value(QStringLiteral("windowGeometry")).toString().toLatin1());
    if (!geometry.isEmpty()) {
        windowGeometry = geometry;
    }

    const QString storedSection = root.value(QStringLiteral("section")).toString();
    if (isValidSection(storedSection)) {
        section = storedSection;
    }

    const QJsonObject detail = root.value(QStringLiteral("detail")).toObject();
    const int storedContainerTab = detail.value(QStringLiteral("containerTab")).toInt(0);
    if (isValidContainerTab(storedContainerTab)) {
        containerTab = storedContainerTab;
    }
    const QString storedMachineTab = detail.value(QStringLiteral("machineTab")).toString();
    if (isValidMachineTab(storedMachineTab)) {
        machineTab = storedMachineTab;
    }
    return true;
}

bool UiState::save(const QString &path) const
{
    const QDir directory = QFileInfo(path).absoluteDir();
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        return false;
    }

    QJsonObject detail;
    detail.insert(QStringLiteral("containerTab"), containerTab);
    detail.insert(QStringLiteral("machineTab"), machineTab);

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("windowGeometry"), QString::fromLatin1(windowGeometry.toBase64()));
    root.insert(QStringLiteral("section"), section);
    root.insert(QStringLiteral("detail"), detail);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (file.write(payload) != payload.size()) {
        return false;
    }
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        return false;
    }
    return file.commit();
}

} // namespace PodStack
