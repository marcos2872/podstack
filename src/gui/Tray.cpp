// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/Tray.h"

#include "core/Config.h"

#include <QAction>
#include <QColor>
#include <QJsonValue>
#include <QMap>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QSystemTrayIcon>

namespace PodStack {
namespace {

bool configBool(const QString &key, bool fallback)
{
    const QJsonValue value = Config::load().get(key);
    return value.isBool() ? value.toBool() : fallback;
}

QIcon makeTrayIcon(const QString &status)
{
    QIcon icon;
    for (int size : {16, 22, 24, 32, 48, 64}) {
        QPixmap pixmap(QStringLiteral(":/qt/qml/PodStack/assets/png/tray/podstack-tray-%1.png")
                           .arg(size));
        if (pixmap.isNull()) {
            continue;
        }
        if (status != QStringLiteral("normal")) {
            const QColor color = status == QStringLiteral("error") ? QColor(0xF8, 0x51, 0x49)
                                                                   : QColor(0xD2, 0x99, 0x22);
            const int side = pixmap.width();
            const int diameter = qMax(6, side / 3);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(QPen(QColor(15, 17, 21), qMax(1, side / 16)));
            painter.setBrush(color);
            painter.drawEllipse(QRect(side - diameter, side - diameter, diameter, diameter)
                                    .adjusted(1, 1, -1, -1));
        }
        icon.addPixmap(pixmap);
    }
    return icon;
}

} // namespace

TrayController::TrayController(QObject *parent)
    : TrayController(true, parent)
{
}

TrayController::TrayController(bool trayEnabled, QObject *parent)
    : QObject(parent)
    , m_trayEnabled(trayEnabled)
    , m_tray(new QSystemTrayIcon(this))
{
    m_icon = makeTrayIcon(m_status);
    m_tray->setIcon(m_icon);

    m_menu = std::make_unique<QMenu>();
    m_openAction = m_menu->addAction(tr("Abrir PodStack"));
    m_menu->addSeparator();
    m_containersMenu = m_menu->addMenu(tr("Containers"));
    m_machinesMenu = m_menu->addMenu(tr("Machines"));
    m_kubernetesMenu = m_menu->addMenu(tr("Kubernetes"));
    m_menu->addSeparator();
    m_activityAction = m_menu->addAction(tr("Activity Monitor"));
    m_settingsAction = m_menu->addAction(tr("Configurações"));
    m_menu->addSeparator();
    m_quitAction = m_menu->addAction(tr("Sair"));

    connect(m_menu.get(), &QMenu::aboutToShow, this,
            [this]() { emit refreshRequested(); });

    connect(m_openAction, &QAction::triggered, this, &TrayController::openRequested);
    connect(m_activityAction, &QAction::triggered, this,
            &TrayController::activityMonitorRequested);
    connect(m_settingsAction, &QAction::triggered, this, &TrayController::settingsRequested);
    connect(m_quitAction, &QAction::triggered, this, &TrayController::quitRequested);

    rebuildMachinesMenu();
    rebuildKubernetesMenu();
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger
                    || reason == QSystemTrayIcon::DoubleClick) {
                    emit openRequested();
                } else if (reason == QSystemTrayIcon::MiddleClick) {
                    emit activityMonitorRequested();
                }
            });
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, &TrayController::openRequested);

    rebuildContainersMenu();
    m_tray->setContextMenu(m_menu.get());
    updateStatus();
    showTray();
}

TrayController::~TrayController()
{
    if (m_tray) {
        m_tray->hide();
        m_tray->setContextMenu(nullptr);
    }
    m_menu.reset();
}

bool TrayController::available() const
{
    return m_trayEnabled && QSystemTrayIcon::isSystemTrayAvailable();
}

bool TrayController::keepRunningInTray() const
{
    return configBool(QStringLiteral("app.keepRunningInTray"), true);
}

bool TrayController::showInTray() const
{
    return configBool(QStringLiteral("app.showInTray"), true);
}

bool TrayController::canHideToTray() const
{
    // A checagem é feita no momento do fechamento: se o host SNI sumiu,
    // fechar deve encerrar o app em vez de deixá-lo sem janela.
    return available() && showInTray() && keepRunningInTray();
}

void TrayController::showTray()
{
    if (m_trayEnabled && showInTray()) {
        m_tray->show();
    }
}

void TrayController::requestOpenContainer(const QString &id, const QString &tab)
{
    emit openContainerRequested(id, tab);
}

void TrayController::reloadConfig()
{
    if (!m_trayEnabled) {
        return;
    }
    if (showInTray()) {
        m_tray->show();
    } else {
        m_tray->hide();
    }
    updateStatus();
}

void TrayController::setContainers(const QVariantList &containers)
{
    m_containers = containers;
    rebuildContainersMenu();
    updateStatus();
}

void TrayController::setMachines(const QVariantList &machines)
{
    m_machines = machines;
    rebuildMachinesMenu();
}

void TrayController::setKubernetes(bool enabled)
{
    if (m_k8sEnabled == enabled) {
        return;
    }
    m_k8sEnabled = enabled;
    rebuildKubernetesMenu();
}

void TrayController::setComposeProjects(const QVariantList &projects)
{
    m_composeProjects = projects;
    rebuildContainersMenu();
}

void TrayController::setConnected(bool connected)
{
    if (m_connected == connected) {
        return;
    }
    m_connected = connected;
    updateStatus();
}

void TrayController::setSetupPending(bool pending)
{
    if (m_setupPending == pending) {
        return;
    }
    m_setupPending = pending;
    updateStatus();
}

void TrayController::setActionActive(bool active)
{
    if (m_actionActive == active) {
        return;
    }
    m_actionActive = active;
    rebuildContainersMenu();
    rebuildMachinesMenu();
}

QString TrayController::projectPath(const QString &name) const
{
    for (const QVariant &value : m_composeProjects) {
        const QVariantMap project = value.toMap();
        if (project.value(QStringLiteral("name")).toString() == name) {
            return project.value(QStringLiteral("workingDir")).toString();
        }
    }
    return {};
}

int TrayController::runningCount() const
{
    int count = 0;
    for (const QVariant &value : m_containers) {
        if (value.toMap().value(QStringLiteral("state")).toString()
            == QStringLiteral("running")) {
            ++count;
        }
    }
    return count;
}

int TrayController::unhealthyCount() const
{
    int count = 0;
    for (const QVariant &value : m_containers) {
        if (value.toMap().value(QStringLiteral("health")).toString()
            == QStringLiteral("unhealthy")) {
            ++count;
        }
    }
    return count;
}

void TrayController::updateStatus()
{
    QString next = QStringLiteral("normal");
    if (!m_connected) {
        next = QStringLiteral("error");
    } else if (m_setupPending || unhealthyCount() > 0) {
        next = QStringLiteral("attention");
    }

    const int running = runningCount();
    QString tooltip = tr("PodStack — %1 containers em execução").arg(running);
    if (!m_connected) {
        tooltip = tr("PodStack — daemon indisponível");
    } else if (m_setupPending) {
        tooltip = tr("PodStack — configuração pendente");
    } else if (unhealthyCount() > 0) {
        tooltip += tr(" — %1 com problema").arg(unhealthyCount());
    }
    m_tray->setToolTip(tooltip);

    if (next != m_status) {
        m_status = next;
        updateIcon();
        emit statusChanged();
    }
}

void TrayController::updateIcon()
{
    m_icon = makeTrayIcon(m_status);
    m_tray->setIcon(m_icon);
}

void TrayController::rebuildContainersMenu()
{
    if (!m_containersMenu) {
        return;
    }

    m_containersMenu->clear();
    const QList<QMenu *> staleMenus =
        m_containersMenu->findChildren<QMenu *>(Qt::FindDirectChildrenOnly);
    for (QMenu *menu : staleMenus) {
        delete menu;
    }

    QMap<QString, QList<QVariantMap>> projects;
    QList<QVariantMap> loose;
    for (const QVariant &value : m_containers) {
        const QVariantMap container = value.toMap();
        const QString project = container.value(QStringLiteral("project")).toString();
        if (project.isEmpty()) {
            loose.append(container);
        } else {
            projects[project].append(container);
        }
    }

    const int limit = 25;
    int shown = 0;
    const auto addOne = [this, &shown, limit](QMenu *parent, const QVariantMap &container) {
        if (shown >= limit) {
            return;
        }
        addContainerMenu(parent, container);
        ++shown;
    };

    for (auto it = projects.constBegin(); it != projects.constEnd(); ++it) {
        QMenu *projectMenu = m_containersMenu->addMenu(it.key());
        const QString path = projectPath(it.key());
        const bool pathReady = !path.isEmpty() && !m_actionActive;

        QAction *start = projectMenu->addAction(tr("Iniciar projeto"));
        start->setEnabled(pathReady);
        connect(start, &QAction::triggered, this, [this, path]() {
            emit composeActionRequested(QStringLiteral("start"), path);
        });

        QAction *stop = projectMenu->addAction(tr("Parar projeto"));
        stop->setEnabled(pathReady);
        connect(stop, &QAction::triggered, this, [this, path]() {
            emit composeActionRequested(QStringLiteral("stop"), path);
        });

        QAction *restart = projectMenu->addAction(tr("Reiniciar projeto"));
        restart->setEnabled(pathReady);
        connect(restart, &QAction::triggered, this, [this, path]() {
            emit composeActionRequested(QStringLiteral("restart"), path);
        });

        QAction *logs = projectMenu->addAction(tr("Logs do projeto"));
        connect(logs, &QAction::triggered, this, [this, name = it.key()]() {
            emit openComposeRequested(name, QString(), true);
        });

        QAction *open = projectMenu->addAction(tr("Abrir Compose"));
        connect(open, &QAction::triggered, this, [this, name = it.key()]() {
            emit openComposeRequested(name, QString(), false);
        });

        if (!it.value().isEmpty()) {
            projectMenu->addSeparator();
        }
        for (const QVariantMap &container : it.value()) {
            addOne(projectMenu, container);
        }
    }

    if (!loose.isEmpty()) {
        if (!projects.isEmpty()) {
            m_containersMenu->addSeparator();
        }
        for (const QVariantMap &container : loose) {
            addOne(m_containersMenu, container);
        }
    }

    if (m_containers.isEmpty()) {
        QAction *empty = m_containersMenu->addAction(tr("Nenhum container"));
        empty->setEnabled(false);
    } else if (shown < m_containers.size()) {
        m_containersMenu->addSeparator();
        QAction *all = m_containersMenu->addAction(tr("Ver todos no PodStack"));
        connect(all, &QAction::triggered, this, [this]() {
            emit openViewRequested(QStringLiteral("containers"));
        });
    }

    // O SNI exporta o QMenu como DBusMenu uma única vez; reexportamos após
    // cada mudança para que os submenus dinâmicos cheguem ao host da bandeja.
    m_tray->setContextMenu(m_menu.get());
}

void TrayController::rebuildMachinesMenu()
{
    if (!m_machinesMenu) {
        return;
    }
    m_machinesMenu->clear();

    QAction *openAll = m_machinesMenu->addAction(tr("Abrir Machines"));
    connect(openAll, &QAction::triggered, this,
            [this]() { emit openViewRequested(QStringLiteral("machines")); });

    if (m_machines.isEmpty()) {
        QAction *empty = m_machinesMenu->addAction(tr("Nenhuma machine"));
        empty->setEnabled(false);
    } else {
        m_machinesMenu->addSeparator();
        for (const QVariant &value : m_machines) {
            addMachineMenu(m_machinesMenu, value.toMap());
        }
    }
    m_tray->setContextMenu(m_menu.get());
}

void TrayController::rebuildKubernetesMenu()
{
    if (!m_kubernetesMenu) {
        return;
    }
    m_kubernetesMenu->clear();

    QAction *open = m_kubernetesMenu->addAction(tr("Abrir Kubernetes"));
    connect(open, &QAction::triggered, this,
            [this]() { emit openViewRequested(QStringLiteral("kubernetes")); });
    QAction *state = m_kubernetesMenu->addAction(m_k8sEnabled ? tr("Cluster habilitado")
                                                              : tr("Cluster desabilitado"));
    state->setEnabled(false);
    if (!m_k8sEnabled) {
        QAction *enable = m_kubernetesMenu->addAction(tr("Habilitar no PodStack"));
        connect(enable, &QAction::triggered, this,
                [this]() { emit openViewRequested(QStringLiteral("kubernetes")); });
    }
    m_tray->setContextMenu(m_menu.get());
}

void TrayController::addMachineMenu(QMenu *parent, const QVariantMap &machine)
{
    const QString id = machine.value(QStringLiteral("id")).toString();
    QString name = machine.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
        name = id.left(12);
    }
    const QString state = machine.value(QStringLiteral("state")).toString();
    const bool running = state == QStringLiteral("running");
    QMenu *menu = parent->addMenu(name);

    QAction *start = menu->addAction(tr("Iniciar"));
    start->setEnabled(!running && !m_actionActive);
    connect(start, &QAction::triggered, this,
            [this, id]() { emit machineActionRequested(QStringLiteral("start"), id); });
    QAction *stop = menu->addAction(tr("Parar"));
    stop->setEnabled(running && !m_actionActive);
    connect(stop, &QAction::triggered, this,
            [this, id]() { emit machineActionRequested(QStringLiteral("stop"), id); });
    QAction *restart = menu->addAction(tr("Reiniciar"));
    restart->setEnabled(running && !m_actionActive);
    connect(restart, &QAction::triggered, this,
            [this, id]() { emit machineActionRequested(QStringLiteral("restart"), id); });

    menu->addSeparator();
    QAction *open = menu->addAction(tr("Abrir detalhe"));
    connect(open, &QAction::triggered, this, [this, id]() { emit machineOpenRequested(id); });
    QAction *ssh = menu->addAction(tr("SSH"));
    ssh->setEnabled(running);
    connect(ssh, &QAction::triggered, this, [this, id]() { emit machineSshRequested(id); });
}

void TrayController::addContainerMenu(QMenu *parent, const QVariantMap &container)
{
    const QString id = container.value(QStringLiteral("id")).toString();
    QString label = container.value(QStringLiteral("name")).toString();
    if (label.isEmpty()) {
        label = id.left(12);
    }

    const QString state = container.value(QStringLiteral("state")).toString();
    const QString health = container.value(QStringLiteral("health")).toString();
    if (health == QStringLiteral("unhealthy")) {
        label += tr(" — unhealthy");
    } else if (state != QStringLiteral("running")) {
        label += tr(" (parado)");
    }

    QMenu *menu = parent->addMenu(label);
    const auto addAction = [this, menu, id](const QString &text, const QString &action) {
        QAction *item = menu->addAction(text);
        item->setEnabled(!m_actionActive);
        connect(item, &QAction::triggered, this, [this, action, id]() {
            emit containerActionRequested(action, id);
        });
    };

    if (state == QStringLiteral("running")) {
        addAction(tr("Parar"), QStringLiteral("stop"));
        addAction(tr("Reiniciar"), QStringLiteral("restart"));
    } else if (state == QStringLiteral("paused")) {
        addAction(tr("Retomar"), QStringLiteral("unpause"));
        addAction(tr("Parar"), QStringLiteral("stop"));
    } else {
        addAction(tr("Iniciar"), QStringLiteral("start"));
    }

    menu->addSeparator();
    QAction *logs = menu->addAction(tr("Logs"));
    logs->setEnabled(!id.isEmpty());
    connect(logs, &QAction::triggered, this, [this, id]() {
        emit openContainerRequested(id, QStringLiteral("logs"));
    });

    QAction *open = menu->addAction(tr("Abrir no PodStack"));
    connect(open, &QAction::triggered, this, [this, id]() {
        emit openContainerRequested(id, QStringLiteral("info"));
    });
}

} // namespace PodStack
