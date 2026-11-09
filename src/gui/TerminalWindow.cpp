// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/TerminalWindow.h"

#include <QFontDatabase>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <qtermwidget.h>

namespace PodStack {

QTermWidget *createTerminalWidget(const QString &program, const QStringList &args,
                                  QWidget *parent)
{
    auto *terminal = new QTermWidget(0, parent);
    terminal->setColorScheme(QStringLiteral("DarkPastels"));
    terminal->setTerminalSizeHint(true);
    terminal->setScrollBarPosition(QTermWidgetInterface::ScrollBarRight);

    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(11);
    terminal->setTerminalFont(font);

    terminal->setShellProgram(program);
    terminal->setArgs(args);
    terminal->startShellProgram();
    return terminal;
}

TerminalWindow::TerminalWindow(const QString &containerId, const QString &program,
                               const QStringList &args, QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("PodStack — %1").arg(containerId));
    resize(900, 600);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QString executable = program;
    QStringList arguments = args;
    if (executable.isEmpty()) {
        executable = QStandardPaths::findExecutable(QStringLiteral("podman"));
        arguments = {QStringLiteral("exec"), QStringLiteral("-it"), containerId,
                     QStringLiteral("/bin/sh")};
    }
    layout->addWidget(createTerminalWidget(executable, arguments, this));
}

} // namespace PodStack
