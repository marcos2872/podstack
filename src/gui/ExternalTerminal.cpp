// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/ExternalTerminal.h"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace PodStack {

namespace {

const QStringList kCommonTerminals = {
    QStringLiteral("gnome-terminal"), QStringLiteral("kgx"),
    QStringLiteral("konsole"),        QStringLiteral("xfce4-terminal"),
    QStringLiteral("alacritty"),      QStringLiteral("kitty"),
    QStringLiteral("foot"),           QStringLiteral("wezterm"),
    QStringLiteral("xterm"),
};

} // namespace

QString ExternalTerminal::findTerminal()
{
    const QString xdgTerminalExec =
        QStandardPaths::findExecutable(QStringLiteral("xdg-terminal-exec"));
    if (!xdgTerminalExec.isEmpty()) {
        return xdgTerminalExec;
    }

    const QString preferred = qEnvironmentVariable("TERMINAL");
    if (!preferred.isEmpty()) {
        const QString resolved = QStandardPaths::findExecutable(preferred);
        if (!resolved.isEmpty()) {
            return resolved;
        }
    }

    for (const QString &candidate : kCommonTerminals) {
        const QString resolved = QStandardPaths::findExecutable(candidate);
        if (!resolved.isEmpty()) {
            return resolved;
        }
    }
    return {};
}

QStringList ExternalTerminal::commandFor(const QString &terminal, const QString &program,
                                         const QStringList &args)
{
    const QString name = QFileInfo(terminal).fileName();
    QStringList command;

    if (name == QStringLiteral("gnome-terminal") || name == QStringLiteral("kgx")) {
        command = QStringList({QStringLiteral("--"), program}) + args;
    } else if (name == QStringLiteral("konsole") || name == QStringLiteral("xterm")
               || name == QStringLiteral("alacritty")) {
        command = QStringList({QStringLiteral("-e"), program}) + args;
    } else if (name == QStringLiteral("xfce4-terminal")) {
        command = QStringList({QStringLiteral("-x"), program}) + args;
    } else if (name == QStringLiteral("wezterm")) {
        command = QStringList({QStringLiteral("start"), QStringLiteral("--"), program}) + args;
    } else {
        // xdg-terminal-exec, kitty, foot e terminais desconhecidos aceitam o
        // comando diretamente após o nome do programa.
        command = QStringList({program}) + args;
    }
    return command;
}

bool ExternalTerminal::launch(const QString &program, const QStringList &args,
                              QString *error)
{
    if (error) {
        error->clear();
    }
    if (program.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Programa do terminal não informado.");
        }
        return false;
    }

    const QString terminal = findTerminal();
    if (terminal.isEmpty()) {
        if (error) {
            *error = QStringLiteral(
                "Nenhum terminal externo encontrado. Instale xdg-terminal-exec ou "
                "defina a variável TERMINAL.");
        }
        return false;
    }

    const QStringList command = commandFor(terminal, program, args);
    qint64 pid = 0;
    if (!QProcess::startDetached(terminal, command, QString(), &pid)) {
        if (error) {
            *error = QStringLiteral("Falha ao abrir o terminal externo “%1”.").arg(terminal);
        }
        return false;
    }
    return true;
}

} // namespace PodStack
