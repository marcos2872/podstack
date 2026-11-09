// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/TerminalView.h"

#include "gui/ExternalTerminal.h"

#include <QFontDatabase>
#include <QFile>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QToolButton>
#include <QVBoxLayout>

#include <qtermwidget.h>

namespace PodStack {

TerminalView::TerminalView(const QString &title, const QString &program,
                           const QStringList &args, QWidget *parent)
    : QWidget(parent)
    , m_title(title)
    , m_program(program)
    , m_arguments(args)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_toolbar = new QWidget(this);
    m_toolbar->setObjectName(QStringLiteral("terminalToolbar"));
    auto *toolbarLayout = new QHBoxLayout(m_toolbar);
    toolbarLayout->setContentsMargins(6, 3, 6, 3);
    toolbarLayout->setSpacing(6);

    m_titleLabel = new QLabel(title, m_toolbar);
    m_titleLabel->setObjectName(QStringLiteral("terminalTitle"));
    m_titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    toolbarLayout->addWidget(m_titleLabel, 1);

    m_externalButton = new QToolButton(m_toolbar);
    m_externalButton->setObjectName(QStringLiteral("terminalExternalButton"));
    m_externalButton->setText(tr("Abrir externo"));
    m_externalButton->setToolTip(tr("Abrir a mesma sessão em um terminal externo"));
    m_externalButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_externalButton->setAutoRaise(true);
    const QString iconPath = QStringLiteral(":/qt/qml/PodStack/assets/icons/external.svg");
    if (QFile::exists(iconPath)) {
        m_externalButton->setIcon(QIcon(iconPath));
    }
    connect(m_externalButton, &QToolButton::clicked, this, &TerminalView::openExternal);
    toolbarLayout->addWidget(m_externalButton);

    layout->addWidget(m_toolbar);

    m_terminal = new QTermWidget(0, this);
    m_terminal->setObjectName(QStringLiteral("terminal"));
    m_terminal->setColorScheme(QStringLiteral("DarkPastels"));
    m_terminal->setTerminalSizeHint(true);
    m_terminal->setScrollBarPosition(QTermWidgetInterface::ScrollBarRight);

    // O keytab "default" manda Backspace como ^H (\b), que aparece literal em
    // shells cujo erase é ^? (readline/podman/ssh). O keytab "linux" manda \x7f.
    m_terminal->setKeyBindings(QStringLiteral("linux"));

    QStringList environment;
    for (const QString &entry : QProcessEnvironment::systemEnvironment().toStringList()) {
        if (!entry.startsWith(QStringLiteral("TERM="))
            && !entry.startsWith(QStringLiteral("COLORTERM="))) {
            environment.append(entry);
        }
    }
    environment.append(QStringLiteral("TERM=xterm-256color"));
    environment.append(QStringLiteral("COLORTERM=truecolor"));
    m_terminal->setEnvironment(environment);

    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(11);
    m_terminal->setTerminalFont(font);

    if (!m_program.isEmpty()) {
        m_terminal->setShellProgram(m_program);
        m_terminal->setArgs(m_arguments);
        m_terminal->startShellProgram();
    }

    layout->addWidget(m_terminal, 1);
}

void TerminalView::setTitleVisible(bool visible)
{
    if (m_titleLabel) {
        m_titleLabel->setVisible(visible);
    }
}

void TerminalView::setToolbarVisible(bool visible)
{
    if (m_toolbar) {
        m_toolbar->setVisible(visible);
    }
}

void TerminalView::focusTerminal()
{
    if (m_terminal) {
        m_terminal->setFocus();
    }
}

void TerminalView::openExternal()
{
    QString error;
    if (!ExternalTerminal::launch(m_program, m_arguments, &error)) {
        QMessageBox::warning(this, tr("Terminal externo"), error);
    }
}

} // namespace PodStack
