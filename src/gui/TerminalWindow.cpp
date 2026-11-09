// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/TerminalWindow.h"

#include "gui/TerminalView.h"

#include <QStandardPaths>
#include <QVBoxLayout>

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

    QString executable = program;
    QStringList arguments = args;
    if (executable.isEmpty()) {
        executable = QStandardPaths::findExecutable(QStringLiteral("podman"));
        arguments = {QStringLiteral("exec"), QStringLiteral("-it"), containerId,
                     QStringLiteral("/bin/sh")};
    }
    auto *terminal = new TerminalView(containerId, executable, arguments, this);
    terminal->setTitleVisible(false);
    layout->addWidget(terminal);
}

} // namespace PodStack
