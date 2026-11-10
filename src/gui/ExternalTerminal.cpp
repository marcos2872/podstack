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

QString resolveExecutable(const QString &candidate)
{
    const QStringList parts = QProcess::splitCommand(candidate.trimmed());
    if (parts.isEmpty()) {
        return {};
    }
    return QStandardPaths::findExecutable(parts.first());
}

QString settingValue(const QString &program, const QStringList &arguments)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(1500) || !process.waitForFinished(2000)) {
        process.kill();
        process.waitForFinished(500);
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return {};
    }
    QString value = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    const int newline = value.indexOf(QLatin1Char('\n'));
    if (newline >= 0) {
        value = value.left(newline).trimmed();
    }
    if (value.size() >= 2
        && ((value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\'')))
            || (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))))) {
        value = value.mid(1, value.size() - 2);
    }
    return value;
}

QString terminalFromTermProgram()
{
    const QString program = qEnvironmentVariable("TERM_PROGRAM").trimmed().toLower();
    if (program.isEmpty()) {
        return {};
    }
    QString candidate;
    if (program.contains(QStringLiteral("alacritty"))) {
        candidate = QStringLiteral("alacritty");
    } else if (program.contains(QStringLiteral("ghostty"))) {
        candidate = QStringLiteral("ghostty");
    } else if (program.contains(QStringLiteral("wezterm"))) {
        candidate = QStringLiteral("wezterm");
    } else if (program.contains(QStringLiteral("foot"))) {
        candidate = QStringLiteral("foot");
    } else if (program.contains(QStringLiteral("konsole"))) {
        candidate = QStringLiteral("konsole");
    } else if (program.contains(QStringLiteral("kitty"))) {
        candidate = QStringLiteral("kitty");
    } else if (program.contains(QStringLiteral("xfce"))) {
        candidate = QStringLiteral("xfce4-terminal");
    } else if (program.contains(QStringLiteral("gnome"))) {
        candidate = QStringLiteral("gnome-terminal");
    }
    return candidate.isEmpty() ? QString() : resolveExecutable(candidate);
}

QString terminalFromDesktop()
{
    const QString desktop =
        (qEnvironmentVariable("XDG_CURRENT_DESKTOP") + QLatin1Char(':')
         + qEnvironmentVariable("DESKTOP_SESSION"))
            .toLower();

    QStringList order;
    if (desktop.contains(QStringLiteral("kde"))) {
        order = {QStringLiteral("konsole"), QStringLiteral("alacritty"),
                 QStringLiteral("kitty"), QStringLiteral("xterm")};
    } else if (desktop.contains(QStringLiteral("gnome"))
               || desktop.contains(QStringLiteral("unity"))) {
        order = {QStringLiteral("kgx"), QStringLiteral("gnome-terminal"),
                 QStringLiteral("alacritty"), QStringLiteral("xterm")};
    } else if (desktop.contains(QStringLiteral("xfce"))) {
        order = {QStringLiteral("xfce4-terminal"), QStringLiteral("alacritty"),
                 QStringLiteral("xterm")};
    } else {
        // niri, sway, Hyprland e afins: não há um "default" padronizado;
        // respeita TERM_PROGRAM/$TERMINAL antes e prefere os terminais leves.
        order = {QStringLiteral("alacritty"), QStringLiteral("ghostty"),
                 QStringLiteral("kitty"), QStringLiteral("foot"),
                 QStringLiteral("wezterm"), QStringLiteral("konsole"),
                 QStringLiteral("xterm")};
    }

    for (const QString &candidate : order) {
        const QString resolved = resolveExecutable(candidate);
        if (!resolved.isEmpty()) {
            return resolved;
        }
    }
    return {};
}

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
        const QString resolved = resolveExecutable(preferred);
        if (!resolved.isEmpty()) {
            return resolved;
        }
    }

    // xdg-settings (freedesktop) e gsettings (GNOME) apontam para o terminal
    // padrão configurado; alguns sistemas configuram xdg-terminal-exec sem o
    // pacote instalado, então só aceitamos um binário que exista.
    const QString xdgDefault =
        settingValue(QStringLiteral("xdg-settings"),
                     {QStringLiteral("get"), QStringLiteral("default-terminal-emulator")});
    // Em algumas versões o xdg-settings não conhece a chave e imprime o usage
    // em vez de falhar; não trate "xdg-settings - get ..." como terminal.
    if (!xdgDefault.isEmpty() && !xdgDefault.startsWith(QStringLiteral("xdg-settings"))
        && !xdgDefault.startsWith(QStringLiteral("Usage:"))) {
        const QString resolved = resolveExecutable(xdgDefault);
        if (!resolved.isEmpty()) {
            return resolved;
        }
    }

    const QString gnomeDefault = settingValue(
        QStringLiteral("gsettings"),
        {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.default-applications.terminal"),
         QStringLiteral("exec")});
    if (!gnomeDefault.isEmpty()) {
        const QString resolved = resolveExecutable(gnomeDefault);
        if (!resolved.isEmpty()) {
            return resolved;
        }
    }

    for (const QString &tool : {QStringLiteral("kreadconfig6"), QStringLiteral("kreadconfig5")}) {
        if (QStandardPaths::findExecutable(tool).isEmpty()) {
            continue;
        }
        const QString kdeDefault = settingValue(
            tool, {QStringLiteral("--file"), QStringLiteral("kdeglobals"),
                   QStringLiteral("--group"), QStringLiteral("General"),
                   QStringLiteral("--key"), QStringLiteral("TerminalApplication")});
        if (!kdeDefault.isEmpty()) {
            const QString resolved = resolveExecutable(kdeDefault);
            if (!resolved.isEmpty()) {
                return resolved;
            }
        }
    }

    const QString termProgram = terminalFromTermProgram();
    if (!termProgram.isEmpty()) {
        return termProgram;
    }

    const QString desktop = terminalFromDesktop();
    if (!desktop.isEmpty()) {
        return desktop;
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
               || name == QStringLiteral("alacritty")
               || name == QStringLiteral("ghostty")) {
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
