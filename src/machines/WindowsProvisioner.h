// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"

#include <QString>

namespace PodStack {

/// Dados do unattend gerado para uma instalação Windows.
struct WindowsProvisionOptions {
    QString machineName;  // nome RFC 1123 da machine
    QString computerName; // nome NetBIOS (<= 15)
    QString user = QStringLiteral("podstack");
    QString password;
    QString edition = QStringLiteral("Windows 11 Pro");
    bool installSsh = false;
    QString sshPublicKey;
    QString locale;   // ex.: pt-BR
    QString timeZone; // ID Windows (ex.: E. South America Standard Time)
};

struct WindowsProvisionResult {
    QString xml;
    QString answerFilePath;
    QString answerIsoPath;
};

/// Gera o `autounattend.xml`/ISO e valida as ISOs de instalação do guest Windows.
class WindowsProvisioner {
public:
    static QString defaultEdition();
    static QString generatePassword();
    static QString windowsComputerName(const QString &machineName);
    static QString windowsTimeZone(const QString &ianaId);
    static QString localeName();

    static bool validateInstallerIso(const QString &path, Error *error = nullptr);
    static bool validateVirtioWinIso(const QString &path, Error *error = nullptr);
    static bool validateAnswerFile(const QString &path, Error *error = nullptr);

    static QString buildXml(const WindowsProvisionOptions &options);

    /// Grava `autounattend.xml` (0600) e `autounattend.iso` no diretório da machine.
    /// `userAnswerFile` vazio gera o XML; quando informado, o conteúdo é validado e usado.
    static bool prepare(const QString &machineDir, const WindowsProvisionOptions &options,
                        const QString &userAnswerFile, WindowsProvisionResult *result,
                        Error *error = nullptr);
};

} // namespace PodStack
