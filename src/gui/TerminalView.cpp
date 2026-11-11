// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/TerminalView.h"

#include "core/Config.h"
#include "gui/ExternalTerminal.h"

#include <QAction>
#include <QClipboard>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QShortcut>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <qtermwidget.h>

namespace PodStack {

TerminalView::TerminalView(const QString &title, const QString &program, const QStringList &args,
                           QWidget *parent)
    : QWidget(parent), m_title(title), m_program(program), m_arguments(args)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

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

    m_layout->addWidget(m_toolbar);

    m_terminal = new QTermWidget(0, this);
    configureTerminal(m_terminal);
    m_layout->addWidget(m_terminal, 1);
    startShell();
    connect(m_terminal, SIGNAL(finished()), this, SLOT(handleTerminalFinished()));
}

void TerminalView::handleTerminalFinished()
{
    restartSession();
}

void TerminalView::configureTerminal(QTermWidget *terminal)
{
    terminal->setObjectName(QStringLiteral("terminal"));
    terminal->setColorScheme(QStringLiteral("DarkPastels"));
    terminal->setTerminalSizeHint(true);
    terminal->setScrollBarPosition(QTermWidgetInterface::ScrollBarRight);

    // O keytab "default" manda Backspace como ^H (\b), que aparece literal em
    // shells cujo erase é ^? (readline/podman/ssh). O keytab "linux" manda \x7f.
    terminal->setKeyBindings(QStringLiteral("linux"));

    QStringList environment;
    for (const QString &entry : QProcessEnvironment::systemEnvironment().toStringList()) {
        if (!entry.startsWith(QStringLiteral("TERM=")) &&
            !entry.startsWith(QStringLiteral("COLORTERM="))) {
            environment.append(entry);
        }
    }
    environment.append(QStringLiteral("TERM=xterm-256color"));
    environment.append(QStringLiteral("COLORTERM=truecolor"));
    terminal->setEnvironment(environment);

    const PodStack::Config config = PodStack::Config::load();
    const QString family = config.get(QStringLiteral("terminal.fontFamily")).toString();
    QFont font = (family.isEmpty() || family == QStringLiteral("monospace"))
                     ? QFontDatabase::systemFont(QFontDatabase::FixedFont)
                     : QFont(family);
    int fontSize = config.get(QStringLiteral("terminal.fontSize")).toInt();
    if (fontSize < 8 || fontSize > 32) {
        fontSize = 12;
    }
    font.setPointSize(fontSize);
    terminal->setTerminalFont(font);
    int scrollback = config.get(QStringLiteral("terminal.scrollback")).toInt();
    if (scrollback < 1000 || scrollback > 100000) {
        scrollback = 10000;
    }
    terminal->setHistorySize(scrollback);

    // O QTermWidget expõe copy/paste como slots, mas não instala atalhos:
    // sem isso, Ctrl+Shift+C/V e o menu de contexto não funcionam no app.
    auto *copyShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")), terminal);
    copyShortcut->setObjectName(QStringLiteral("terminalCopyShortcut"));
    copyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copyShortcut, &QShortcut::activated, terminal, &QTermWidget::copyClipboard);

    auto *copyInsert = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Insert")), terminal);
    copyInsert->setObjectName(QStringLiteral("terminalCopyInsertShortcut"));
    copyInsert->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copyInsert, &QShortcut::activated, terminal, &QTermWidget::copyClipboard);

    auto *pasteShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")), terminal);
    pasteShortcut->setObjectName(QStringLiteral("terminalPasteShortcut"));
    pasteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(pasteShortcut, &QShortcut::activated, terminal, &QTermWidget::pasteClipboard);

    auto *pasteInsert = new QShortcut(QKeySequence(QStringLiteral("Shift+Insert")), terminal);
    pasteInsert->setObjectName(QStringLiteral("terminalPasteInsertShortcut"));
    pasteInsert->setContext(Qt::WidgetWithChildrenShortcut);
    connect(pasteInsert, &QShortcut::activated, terminal, &QTermWidget::pasteClipboard);

    terminal->setConfirmMultilinePaste(false);
    terminal->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(terminal, &QWidget::customContextMenuRequested, terminal,
            [terminal](const QPoint &position) {
                QMenu menu(terminal);
                QAction *copyAction = menu.addAction(QObject::tr("Copiar"));
                QAction *pasteAction = menu.addAction(QObject::tr("Colar"));
                QAction *chosen = menu.exec(terminal->mapToGlobal(position));
                if (chosen == copyAction) {
                    terminal->copyClipboard();
                } else if (chosen == pasteAction) {
                    terminal->pasteClipboard();
                }
            });
}

void TerminalView::startShell()
{
    if (!m_terminal || m_program.isEmpty()) {
        return;
    }
    m_terminal->setShellProgram(m_program);
    m_terminal->setArgs(m_arguments);
    m_terminal->startShellProgram();
    m_lastStart.restart();
}

void TerminalView::restartSession()
{
    if (!m_restartEnabled || m_program.isEmpty() || !m_terminal) {
        return;
    }

    // Um shell que sai logo após iniciar (container parado, comando inválido)
    // não deve gerar um loop infinito de reinícios. `exit` normal, após alguns
    // segundos, reinicia imediatamente.
    if (m_lastStart.isValid() && m_lastStart.elapsed() < 1000) {
        if (++m_fastExits >= 3) {
            m_restartEnabled = false;
            emit sessionEnded();
            return;
        }
    } else {
        m_fastExits = 0;
    }

    QTimer::singleShot(120, this, &TerminalView::rebuildTerminal);
}

void TerminalView::rebuildTerminal()
{
    if (!m_terminal || !m_layout) {
        return;
    }

    m_terminal->disconnect(this);
    m_layout->removeWidget(m_terminal);
    m_terminal->deleteLater();

    m_terminal = new QTermWidget(0, this);
    configureTerminal(m_terminal);
    m_layout->addWidget(m_terminal, 1);
    startShell();
    connect(m_terminal, SIGNAL(finished()), this, SLOT(handleTerminalFinished()));
    ++m_restartCount;
    focusTerminal();
    emit sessionRestarted();
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
