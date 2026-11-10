// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

class QCloseEvent;
class QDockWidget;
class QQuickWidget;
class QTabWidget;

namespace PodStack {

class Backend;
class ContainerDetailDialog;
class MachineDetailDialog;
class NotificationService;
class SingleInstance;
class TrayController;

/// Janela principal do PodStack.
///
/// Hospeda a UI QML em um QQuickWidget (widget central) e os terminais em um
/// QDockWidget com abas. A visibilidade real pertence ao QMainWindow; o QML
/// apenas pede `reveal()`/`hideToTray()` via contexto `appWindow`.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /// `loadQml=false` é um hook de teste: monta a janela e o dock sem carregar
    /// o módulo QML (que só existe no binário `podstack-gui`).
    explicit MainWindow(Backend *backend, TrayController *tray,
                        NotificationService *notifications, SingleInstance *singleInstance,
                        bool startHidden, bool loadQml = true, QWidget *parent = nullptr);

    Q_INVOKABLE void reveal();
    Q_INVOKABLE void hideToTray();
    Q_INVOKABLE void openContainerDetail(const QString &id);
    Q_INVOKABLE void openContainerDetailTab(const QString &id, int detailTab);
    Q_INVOKABLE void openMachineDetail(const QString &id, int tab = 0);

    bool qmlLoaded() const;
    bool qmlFailed() const;
    QStringList qmlErrors() const;
    QDockWidget *terminalDock() const { return m_terminalDock; }
    QTabWidget *terminalTabs() const { return m_terminalTabs; }

public slots:
    void openTerminal(const QString &title, const QString &program, const QStringList &args);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createTerminalDock();
    void loadMainQml();
    void closeTerminalTab(int index);
    void updateQuitOnLastWindowClosed();
    ContainerDetailDialog *ensureDetailDialog();
    MachineDetailDialog *ensureMachineDetailDialog();

    Backend *m_backend = nullptr;
    TrayController *m_tray = nullptr;
    NotificationService *m_notifications = nullptr;
    SingleInstance *m_singleInstance = nullptr;
    bool m_startHidden = false;
    QQuickWidget *m_quickWidget = nullptr;
    QDockWidget *m_terminalDock = nullptr;
    QTabWidget *m_terminalTabs = nullptr;
    ContainerDetailDialog *m_detailDialog = nullptr;
    MachineDetailDialog *m_machineDetailDialog = nullptr;
};

} // namespace PodStack
