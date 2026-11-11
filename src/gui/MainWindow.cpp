// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/MainWindow.h"

#include "gui/Backend.h"
#include "gui/ContainerDetailDialog.h"
#include "gui/MachineDetailDialog.h"
#include "gui/Notifications.h"
#include "gui/SingleInstance.h"
#include "gui/TerminalView.h"
#include "gui/Tray.h"
#include "gui/UiState.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QDockWidget>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QScreen>
#include <QTabWidget>
#include <QTimer>

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
    m_uiState.load();
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
    restoreUiState();

    m_uiStateSaveTimer.setSingleShot(true);
    m_uiStateSaveTimer.setInterval(500);
    connect(&m_uiStateSaveTimer, &QTimer::timeout, this, &MainWindow::saveUiState);

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
    context->setContextProperty(QStringLiteral("startupSection"), m_uiState.section);

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

    auto *terminal = new TerminalView(title, program, args, m_terminalTabs);
    terminal->setTitleVisible(false);
    const QString label = title.isEmpty() ? program : title;
    const int index = m_terminalTabs->addTab(terminal, label);
    m_terminalTabs->setTabToolTip(
        index, (program + QLatin1Char(' ') + args.join(QLatin1Char(' '))).trimmed());
    m_terminalTabs->setCurrentIndex(index);

    m_terminalDock->show();
    m_terminalDock->raise();
    terminal->focusTerminal();
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

void MainWindow::openSection(const QString &section)
{
    reveal();
    QObject *root = m_quickWidget ? m_quickWidget->rootObject() : nullptr;
    if (root) {
        QMetaObject::invokeMethod(root, "openSection", Q_ARG(QVariant, QVariant(section)));
    }
}

void MainWindow::hideToTray()
{
    hide();
}

ContainerDetailDialog *MainWindow::ensureDetailDialog()
{
    if (!m_detailDialog) {
        m_detailDialog = new ContainerDetailDialog(m_backend, this);
    }
    return m_detailDialog;
}

void MainWindow::openContainerDetail(const QString &id)
{
    if (id.isEmpty()) {
        return;
    }
    ensureDetailDialog()->openContainer(id, m_uiState.containerTab);
}

void MainWindow::openContainerDetailTab(const QString &id, int detailTab)
{
    if (id.isEmpty()) {
        return;
    }
    ensureDetailDialog()->openContainer(id, detailTab);
}

MachineDetailDialog *MainWindow::ensureMachineDetailDialog()
{
    if (!m_machineDetailDialog) {
        m_machineDetailDialog = new MachineDetailDialog(m_backend, this);
    }
    return m_machineDetailDialog;
}

void MainWindow::openMachineDetail(const QString &id, int tab)
{
    if (id.isEmpty()) {
        return;
    }
    if (tab < 0) {
        tab = UiState::supportedMachineTabs().indexOf(m_uiState.machineTab);
        if (tab < 0) {
            tab = 0;
        }
    }
    ensureMachineDetailDialog()->openMachine(id, tab);
}

QString MainWindow::startupSection() const
{
    return m_uiState.section;
}

int MainWindow::startupContainerTab() const
{
    return m_uiState.containerTab;
}

QString MainWindow::startupMachineTab() const
{
    return m_uiState.machineTab;
}

void MainWindow::rememberSection(const QString &section)
{
    if (!UiState::isValidSection(section) || m_uiState.section == section) {
        return;
    }
    m_uiState.section = section;
    scheduleUiStateSave();
}

void MainWindow::rememberDetailTab(const QString &kind, const QVariant &tab)
{
    if (kind == QStringLiteral("container")) {
        const int index = tab.toInt();
        if (!UiState::isValidContainerTab(index) || m_uiState.containerTab == index) {
            return;
        }
        m_uiState.containerTab = index;
    } else if (kind == QStringLiteral("machine")) {
        const QString name = tab.toString();
        if (!UiState::isValidMachineTab(name) || m_uiState.machineTab == name) {
            return;
        }
        m_uiState.machineTab = name;
    } else {
        return;
    }
    scheduleUiStateSave();
}

void MainWindow::restoreUiState()
{
    if (m_uiState.windowGeometry.isEmpty() || !restoreGeometry(m_uiState.windowGeometry)) {
        return;
    }
    const QRect frame = frameGeometry();
    bool onScreen = false;
    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen->availableGeometry().intersects(frame)) {
            onScreen = true;
            break;
        }
    }
    if (!onScreen) {
        resize(1240, 780);
        if (QScreen *screen = QGuiApplication::primaryScreen()) {
            move(screen->availableGeometry().center() - rect().center());
        }
    }
}

void MainWindow::scheduleUiStateSave()
{
    m_uiStateSaveTimer.start();
}

void MainWindow::saveUiState()
{
    if (!m_uiState.windowGeometry.isEmpty() || isVisible()) {
        m_uiState.windowGeometry = saveGeometry();
    }
    (void) m_uiState.save();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveUiState();
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

bool MainWindow::qmlFailed() const
{
    return m_quickWidget && m_quickWidget->status() == QQuickWidget::Error;
}

QStringList MainWindow::qmlErrors() const
{
    QStringList messages;
    if (!m_quickWidget) {
        return messages;
    }
    for (const QQmlError &error : m_quickWidget->errors()) {
        messages.append(error.toString());
    }
    return messages;
}

} // namespace PodStack
