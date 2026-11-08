#include "gui/TerminalWindow.h"

#include <QStandardPaths>
#include <QVBoxLayout>

#include <qtermwidget.h>

namespace PodStack {

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

    auto *terminal = new QTermWidget(0, this);
    terminal->setColorScheme(QStringLiteral("DarkPastels"));
    terminal->setTerminalSizeHint(true);
    terminal->setScrollBarPosition(QTermWidgetInterface::ScrollBarRight);

    if (program.isEmpty()) {
        terminal->setShellProgram(QStandardPaths::findExecutable(QStringLiteral("podman")));
        terminal->setArgs({QStringLiteral("exec"), QStringLiteral("-it"), containerId,
                           QStringLiteral("/bin/sh")});
    } else {
        terminal->setShellProgram(program);
        terminal->setArgs(args);
    }
    terminal->startShellProgram();

    layout->addWidget(terminal);
}

} // namespace PodStack
