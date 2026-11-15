// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QIcon>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <memory>

class QAction;
class QMenu;
class QSystemTrayIcon;

namespace PodStack {

/// Bandeja do PodStack (StatusNotifierItem via QSystemTrayIcon).
///
/// O controlador é uma view: não faz RPC. Ele expõe o menu e emite sinais que
/// a janela principal (QML) e o `main.cpp` traduzem em ações.
class TrayController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit TrayController(QObject *parent = nullptr);
    /// `trayEnabled=false` evita registrar SNI (usado em testes).
    explicit TrayController(bool trayEnabled, QObject *parent = nullptr);
    ~TrayController() override;

    bool available() const;
    bool keepRunningInTray() const;
    Q_INVOKABLE bool showInTray() const;

    /// Só esconde a janela se existe bandeja visível e a config permite.
    Q_INVOKABLE bool canHideToTray() const;
    /// Reaplica showInTray/keepRunningInTray depois de mudar a configuração.
    Q_INVOKABLE void reloadConfig();
    /// Encaminha um pedido de navegação (usado por notificações).
    Q_INVOKABLE void requestOpenContainer(const QString &id, const QString &tab);

    const QIcon &icon() const { return m_icon; }
    QMenu *contextMenu() const { return m_menu.get(); }
    QSystemTrayIcon *trayIcon() const { return m_tray; }
    QString status() const { return m_status; }

public slots:
    void setContainers(const QVariantList &containers);
    void setComposeProjects(const QVariantList &projects);
    void setMachines(const QVariantList &machines);
    void setKubernetes(bool enabled);
    void setConnected(bool connected);
    void setSetupPending(bool pending);
    void setActionActive(bool active);

signals:
    void openRequested();
    void settingsRequested();
    void quitRequested();
    void statusChanged();
    void refreshRequested();
    void activityMonitorRequested();
    void updateCheckRequested();
    void containerActionRequested(const QString &action, const QString &id);
    void openContainerRequested(const QString &id, const QString &tab);
    void composeActionRequested(const QString &action, const QString &projectPath);
    void openComposeRequested(const QString &project, const QString &service, bool logs);
    void machineActionRequested(const QString &action, const QString &id);
    void machineOpenRequested(const QString &id);
    void machineSshRequested(const QString &id);
    void openViewRequested(const QString &section);

private:
    void showTray();
    void updateStatus();
    void updateIcon();
    void rebuildContainersMenu();
    void rebuildMachinesMenu();
    void rebuildKubernetesMenu();
    void addContainerMenu(QMenu *parent, const QVariantMap &container);
    void addMachineMenu(QMenu *parent, const QVariantMap &machine);
    QString projectPath(const QString &name) const;
    int runningCount() const;
    int unhealthyCount() const;

    QIcon m_icon;
    bool m_trayEnabled = true;
    QSystemTrayIcon *m_tray = nullptr;
    std::unique_ptr<QMenu> m_menu;
    QMenu *m_containersMenu = nullptr;
    QMenu *m_machinesMenu = nullptr;
    QMenu *m_kubernetesMenu = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_activityAction = nullptr;
    QAction *m_updateAction = nullptr;
    QAction *m_settingsAction = nullptr;
    QAction *m_quitAction = nullptr;
    QVariantList m_containers;
    QVariantList m_composeProjects;
    QVariantList m_machines;
    bool m_k8sEnabled = false;
    bool m_connected = true;
    bool m_setupPending = false;
    bool m_actionActive = false;
    QString m_status = QStringLiteral("normal");
};

} // namespace PodStack
