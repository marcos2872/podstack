// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace PodStack {

/// Estado de UI persistido entre sessões (geometria, seção e abas).
///
/// Fica em `state/ui-state.json` (0600) separado do config versionado; o
/// `PODSTACK_UI_STATE` permite usar um arquivo isolado em testes.
class UiState {
public:
    static QString defaultPath();

    bool load(const QString &path = defaultPath());
    bool save(const QString &path = defaultPath()) const;

    static QStringList supportedSections();
    static QStringList supportedMachineTabs();
    static bool isValidSection(const QString &section);
    static bool isValidContainerTab(int tab);
    static bool isValidMachineTab(const QString &tab);

    QByteArray windowGeometry;
    QString section = QStringLiteral("containers");
    int containerTab = 0;
    QString machineTab = QStringLiteral("info");

private:
    QString m_lastPath;
};

} // namespace PodStack
