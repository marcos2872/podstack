// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/MainWindow.h"

#include "gui/Backend.h"
#include "gui/Notifications.h"
#include "gui/SingleInstance.h"
#include "gui/TerminalWindow.h"
#include "gui/Tray.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QDockWidget>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickWidget>
#include <QTabWidget>

#include <qtermwidget.h>

namespace PodStack {

MainWindow::MainWindow(Backend *backend, TrayController *tray,
                       NotificationService *notifications, SingleInstance *singleInstance,
                       bool startHidden, bool loadQml, QWidget *parent)
    : QMainWindow(parent)
    , m_backend(backend)
    , m_tray(tray)
    , m_notifications(notifications)
    , m_singleInstance(singleInstance)
    , m_startHidden(startHidden)
{
    setWindowTitle(QStringLiteral("PodStack"));
    if (m_tray) {
        setWindowIcon(m_tray->icon());
    }
    resize(1240, 780);
    setMinimumSize(900, 600);

    if (loadQml) {
        loadMainQml();
    }
    createTerminalDock();

    if (m_backend) {
        connect(m_backend, &Backend::terminalRequested, this, &MainWindow::openTerminal);
        connect(m_backend, &Backend::settingsChanged, this,
                &MainWindow::updateQuitOnLastWindowClosed);
    }
    updateQuitOnLastWindowClosed();

    // `--hidden` só mantém a janela escondida quando a bandeja está configurada
    // para aparecer; sem bandeja, o comportamento antigo era abrir a janela.
    const bool hideOnStart = m_startHidden && m_tray && m_tray->showInTray();
    if (!hideOnStart) {
        show();
    }
}

void MainWindow::loadMainQml()
{
    m_quickWidget = new QQuickWidget(this);
    m_quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_quickWidget->setClearColor(palette().color(QPalette::Window));

    QQmlContext *context = m_quickWidget->rootContext();
    context->setContextProperty(QStringLiteral("backend"), m_backend);
    context->setContextProperty(QStringLiteral("tray"), m_tray);
    context->setContextProperty(QStringLiteral("notifications"), m_notifications);
    context->setContextProperty(QStringLiteral("singleInstance"), m_singleInstance);
    context->setContextProperty(QStringLiteral("startHidden"), m_startHidden);
    context->setContextProperty(QStringLiteral("appWindow"), this);

    connect(m_quickWidget, &QQuickWidget::statusChanged, this,
            [this](QQuickWidget::Status status) {
                if (status != QQuickWidget::Error) {
                    return;
                }
                for (const QQmlError &error : m_quickWidget->errors()) {
                    qWarning().noquote() << error.toString();
                }
            });
    connect(m_quickWidget, &QQuickWidget::sceneGraphError, this,
            [](QQuickWindow::SceneGraphError, const QString &message) {
                qWarning("podstack-gui: falha ao renderizar a UI: %s",
                         qUtf8Printable(message));
            });

    m_quickWidget->loadFromModule(QStringLiteral("PodStack"), QStringLiteral("Main"));
    setCentralWidget(m_quickWidget);
}

void MainWindow::createTerminalDock()
{
    m_terminalDock = new QDockWidget(tr("Terminal"), this);
    m_terminalDock->setObjectName(QStringLiteral("terminalDock"));
    m_terminalDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_terminalDock->setFeatures(QDockWidget::DockWidgetClosable
                                | QDockWidget::DockWidgetMovable
                                | QDockWidget::DockWidgetFloatable);

    m_terminalTabs = new QTabWidget(m_terminalDock);
    m_terminalTabs->setObjectName(QStringLiteral("terminalTabs"));
    m_terminalTabs->setTabsClosable(true);
    m_terminalTabs->setMovable(true);
    m_terminalTabs->setDocumentMode(true);
    m_terminalTabs->setElideMode(Qt::ElideRight);
    m_terminalDock->setWidget(m_terminalTabs);

    connect(m_terminalTabs, &QTabWidget::tabCloseRequested, this,
            &MainWindow::closeTerminalTab);

    addDockWidget(Qt::BottomDockWidgetArea, m_terminalDock);
    resizeDocks({m_terminalDock}, {320}, Qt::Vertical);
    m_terminalDock->hide();
}

void MainWindow::openTerminal(const QString &title, const QString &program,
                              const QStringList &args)
{
    if (!m_terminalDock || !m_terminalTabs) {
        return;
    }

    auto *terminal = createTerminalWidget(program, args, m_terminalTabs);
    const QString label = title.isEmpty() ? program : title;
    const int index = m_terminalTabs->addTab(terminal, label);
    m_terminalTabs->setTabToolTip(
        index, (program + QLatin1Char(' ') + args.join(QLatin1Char(' '))).trimmed());
    m_terminalTabs->setCurrentIndex(index);

    m_terminalDock->show();
    m_terminalDock->raise();
    terminal->setFocus();
}

void MainWindow::closeTerminalTab(int index)
{
    if (!m_terminalTabs || index < 0 || index >= m_terminalTabs->count()) {
        return;
    }

    QWidget *page = m_terminalTabs->widget(index);
    m_terminalTabs->removeTab(index);
    delete page;
    if (m_terminalTabs->count() == 0) {
        m_terminalDock->hide();
    }
}

void MainWindow::reveal()
{
    if (isMinimized()) {
        showNormal();
    } else {
        show();
    }
    raise();
    activateWindow();
}

void MainWindow::hideToTray()
{
    hide();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    updateQuitOnLastWindowClosed();

    if (m_tray && m_tray->canHideToTray()) {
        event->ignore();
        hideToTray();
        return;
    }

    event->accept();
    // Com a bandeja ativa, quitOnLastWindowClosed fica desligado para que o
    // fechar-para-bandeja funcione; se esconder não é permitido, encerramos
    // explicitamente.
    if (m_tray && m_tray->available() && m_tray->showInTray()) {
        QCoreApplication::quit();
    }
}

void MainWindow::updateQuitOnLastWindowClosed()
{
    const bool trayActive = m_tray && m_tray->available() && m_tray->showInTray();
    QApplication::setQuitOnLastWindowClosed(!trayActive);
}

bool MainWindow::qmlLoaded() const
{
    return m_quickWidget && m_quickWidget->status() != QQuickWidget::Error
           && m_quickWidget->rootObject() != nullptr;
}

} // namespace PodStack
