// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Config.h"
#include "core/Paths.h"
#include "gui/Tray.h"

#include <QAction>
#include <QDir>
#include <QMenu>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTest>

namespace {

QAction *findAction(QMenu *menu, const QString &text)
{
    for (QAction *action : menu->actions()) {
        if (action->text().contains(text)) {
            return action;
        }
    }
    return nullptr;
}

QMenu *findSubmenu(QMenu *menu, const QString &text)
{
    QAction *action = findAction(menu, text);
    return action ? action->menu() : nullptr;
}

class TrayTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanupTestCase();

    void menuHasExpectedActions();
    void openActionEmitsOpenRequested();
    void settingsActionEmitsSettingsRequested();
    void quitActionEmitsQuitRequested();
    void configDefaults();
    void configCanDisableKeepRunning();
    void disabledTrayNeverHides();
    void statusReflectsConnection();
    void statusReflectsHealth();
    void statusReflectsSetup();
    void tooltipCountsRunningContainers();
    void containersMenuGroupsByProject();
    void containersMenuEmptyState();
    void containersMenuDisabledDuringAction();
    void containersMenuOverflow();
    void machinesSubmenu();
    void kubernetesSubmenu();
    void activationSignals();
    void pausedContainerActions();
    void requestOpenContainer();
};

void TrayTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QDir(PodStack::Paths::configDir()).removeRecursively();
}

void TrayTest::init()
{
    QDir(PodStack::Paths::configDir()).removeRecursively();
}

void TrayTest::cleanupTestCase()
{
    QDir(PodStack::Paths::configDir()).removeRecursively();
    QStandardPaths::setTestModeEnabled(false);
}

void TrayTest::menuHasExpectedActions()
{
    PodStack::TrayController tray(false);
    QMenu *menu = tray.contextMenu();
    QVERIFY(menu != nullptr);
    QVERIFY(findAction(menu, QStringLiteral("Abrir")) != nullptr);
    QVERIFY(findAction(menu, QStringLiteral("Configura")) != nullptr);
    QVERIFY(findAction(menu, QStringLiteral("Sair")) != nullptr);
}

void TrayTest::openActionEmitsOpenRequested()
{
    PodStack::TrayController tray(false);
    QSignalSpy spy(&tray, &PodStack::TrayController::openRequested);

    QAction *action = findAction(tray.contextMenu(), QStringLiteral("Abrir"));
    QVERIFY(action != nullptr);
    action->trigger();

    QCOMPARE(spy.count(), 1);
}

void TrayTest::settingsActionEmitsSettingsRequested()
{
    PodStack::TrayController tray(false);
    QSignalSpy spy(&tray, &PodStack::TrayController::settingsRequested);

    QAction *action = findAction(tray.contextMenu(), QStringLiteral("Configura"));
    QVERIFY(action != nullptr);
    action->trigger();

    QCOMPARE(spy.count(), 1);
}

void TrayTest::quitActionEmitsQuitRequested()
{
    PodStack::TrayController tray(false);
    QSignalSpy spy(&tray, &PodStack::TrayController::quitRequested);

    QAction *action = findAction(tray.contextMenu(), QStringLiteral("Sair"));
    QVERIFY(action != nullptr);
    action->trigger();

    QCOMPARE(spy.count(), 1);
}

void TrayTest::configDefaults()
{
    const PodStack::Config config;
    QCOMPARE(config.get(QStringLiteral("app.keepRunningInTray")).toBool(), true);
    QCOMPARE(config.get(QStringLiteral("app.showInTray")).toBool(), true);

    PodStack::TrayController tray(false);
    QCOMPARE(tray.keepRunningInTray(), true);
    QCOMPARE(tray.showInTray(), true);
}

void TrayTest::configCanDisableKeepRunning()
{
    PodStack::Config config;
    config.set(QStringLiteral("app.keepRunningInTray"), false);
    QString error;
    QVERIFY2(config.save(&error), qPrintable(error));

    PodStack::TrayController tray(false);
    QCOMPARE(tray.keepRunningInTray(), false);
}

void TrayTest::disabledTrayNeverHides()
{
    PodStack::TrayController tray(false);
    QCOMPARE(tray.canHideToTray(), false);
}

void TrayTest::statusReflectsConnection()
{
    PodStack::TrayController tray(false);
    QSignalSpy spy(&tray, &PodStack::TrayController::statusChanged);

    tray.setConnected(false);
    QCOMPARE(tray.status(), QStringLiteral("error"));
    tray.setConnected(true);
    QCOMPARE(tray.status(), QStringLiteral("normal"));
    QCOMPARE(spy.count(), 2);
}

void TrayTest::statusReflectsHealth()
{
    PodStack::TrayController tray(false);
    tray.setConnected(true);

    const QVariantMap unhealthy{{QStringLiteral("state"), QStringLiteral("running")},
                                {QStringLiteral("health"), QStringLiteral("unhealthy")}};
    tray.setContainers({unhealthy});
    QCOMPARE(tray.status(), QStringLiteral("attention"));

    const QVariantMap healthy{{QStringLiteral("state"), QStringLiteral("running")},
                              {QStringLiteral("health"), QStringLiteral("healthy")}};
    tray.setContainers({healthy});
    QCOMPARE(tray.status(), QStringLiteral("normal"));
}

void TrayTest::statusReflectsSetup()
{
    PodStack::TrayController tray(false);
    tray.setConnected(true);
    tray.setSetupPending(true);
    QCOMPARE(tray.status(), QStringLiteral("attention"));
    tray.setSetupPending(false);
    QCOMPARE(tray.status(), QStringLiteral("normal"));
}

void TrayTest::tooltipCountsRunningContainers()
{
    PodStack::TrayController tray(false);
    tray.setConnected(true);

    const QVariantMap running{{QStringLiteral("state"), QStringLiteral("running")},
                              {QStringLiteral("health"), QString()}};
    const QVariantMap stopped{{QStringLiteral("state"), QStringLiteral("exited")},
                              {QStringLiteral("health"), QString()}};
    tray.setContainers({running, running, stopped});

    QVERIFY(tray.trayIcon()->toolTip().contains(QStringLiteral("2")));
}

void TrayTest::containersMenuGroupsByProject()
{
    PodStack::TrayController tray(false);
    tray.setConnected(true);

    const QVariantMap web{{QStringLiteral("id"), QStringLiteral("id-web")},
                          {QStringLiteral("name"), QStringLiteral("web")},
                          {QStringLiteral("state"), QStringLiteral("running")},
                          {QStringLiteral("health"), QString()},
                          {QStringLiteral("project"), QStringLiteral("app")}};
    const QVariantMap db{{QStringLiteral("id"), QStringLiteral("id-db")},
                         {QStringLiteral("name"), QStringLiteral("db")},
                         {QStringLiteral("state"), QStringLiteral("exited")},
                         {QStringLiteral("health"), QString()}};
    tray.setContainers({web, db});
    tray.setComposeProjects({QVariantMap{{QStringLiteral("name"), QStringLiteral("app")},
                                         {QStringLiteral("workingDir"),
                                          QStringLiteral("/tmp/app")}}});

    QSignalSpy refreshSpy(&tray, &PodStack::TrayController::refreshRequested);
    emit tray.contextMenu()->aboutToShow();
    QCOMPARE(refreshSpy.count(), 1);

    QMenu *containersMenu = findSubmenu(tray.contextMenu(), QStringLiteral("Containers"));
    QVERIFY(containersMenu != nullptr);
    QMenu *project = findSubmenu(containersMenu, QStringLiteral("app"));
    QVERIFY(project != nullptr);
    QAction *startProject = findAction(project, QStringLiteral("Iniciar projeto"));
    QVERIFY(startProject != nullptr);
    QVERIFY(startProject->isEnabled());

    QMenu *webMenu = findSubmenu(project, QStringLiteral("web"));
    QVERIFY(webMenu != nullptr);

    QSignalSpy actionSpy(&tray, &PodStack::TrayController::containerActionRequested);
    QAction *stop = findAction(webMenu, QStringLiteral("Parar"));
    QVERIFY(stop != nullptr);
    stop->trigger();
    QCOMPARE(actionSpy.count(), 1);
    QCOMPARE(actionSpy.first().at(0).toString(), QStringLiteral("stop"));
    QCOMPARE(actionSpy.first().at(1).toString(), QStringLiteral("id-web"));

    QSignalSpy composeSpy(&tray, &PodStack::TrayController::composeActionRequested);
    startProject->trigger();
    QCOMPARE(composeSpy.count(), 1);
    QCOMPARE(composeSpy.first().at(0).toString(), QStringLiteral("start"));
    QCOMPARE(composeSpy.first().at(1).toString(), QStringLiteral("/tmp/app"));

    QMenu *loose = findSubmenu(containersMenu, QStringLiteral("db"));
    QVERIFY(loose != nullptr);
    QVERIFY(findAction(loose, QStringLiteral("Iniciar")) != nullptr);

    QSignalSpy stopProjectSpy(&tray, &PodStack::TrayController::composeActionRequested);
    findAction(project, QStringLiteral("Parar projeto"))->trigger();
    findAction(project, QStringLiteral("Reiniciar projeto"))->trigger();
    QCOMPARE(stopProjectSpy.count(), 2);

    QSignalSpy navigationSpy(&tray, &PodStack::TrayController::openComposeRequested);
    findAction(project, QStringLiteral("Logs do projeto"))->trigger();
    findAction(project, QStringLiteral("Abrir Compose"))->trigger();
    QCOMPARE(navigationSpy.count(), 2);

    QSignalSpy containerSpy(&tray, &PodStack::TrayController::openContainerRequested);
    findAction(webMenu, QStringLiteral("Logs"))->trigger();
    findAction(webMenu, QStringLiteral("Abrir no PodStack"))->trigger();
    QCOMPARE(containerSpy.count(), 2);
}

void TrayTest::containersMenuEmptyState()
{
    PodStack::TrayController tray(false);
    tray.setConnected(true);
    tray.setContainers({});

    emit tray.contextMenu()->aboutToShow();

    QMenu *containers = findSubmenu(tray.contextMenu(), QStringLiteral("Containers"));
    QVERIFY(containers != nullptr);
    QAction *empty = findAction(containers, QStringLiteral("Nenhum container"));
    QVERIFY(empty != nullptr);
    QVERIFY(!empty->isEnabled());
}

void TrayTest::containersMenuDisabledDuringAction()
{
    PodStack::TrayController tray(false);
    tray.setConnected(true);
    tray.setActionActive(true);
    tray.setContainers({QVariantMap{{QStringLiteral("id"), QStringLiteral("id-1")},
                                    {QStringLiteral("name"), QStringLiteral("one")},
                                    {QStringLiteral("state"), QStringLiteral("running")}}});

    emit tray.contextMenu()->aboutToShow();

    QMenu *containersMenu = findSubmenu(tray.contextMenu(), QStringLiteral("Containers"));
    QVERIFY(containersMenu != nullptr);
    QMenu *one = findSubmenu(containersMenu, QStringLiteral("one"));
    QVERIFY(one != nullptr);
    QAction *stop = findAction(one, QStringLiteral("Parar"));
    QVERIFY(stop != nullptr);
    QVERIFY(!stop->isEnabled());
}

void TrayTest::machinesSubmenu()
{
    PodStack::TrayController tray(false);
    const QVariantMap machine{{QStringLiteral("id"), QStringLiteral("m1")},
                              {QStringLiteral("name"), QStringLiteral("dev")},
                              {QStringLiteral("state"), QStringLiteral("running")}};
    tray.setMachines({machine});

    QMenu *machines = findSubmenu(tray.contextMenu(), QStringLiteral("Machines"));
    QVERIFY(machines != nullptr);
    QVERIFY(findAction(machines, QStringLiteral("Abrir Machines")) != nullptr);
    QMenu *dev = findSubmenu(machines, QStringLiteral("dev"));
    QVERIFY(dev != nullptr);
    QVERIFY(findAction(dev, QStringLiteral("Parar"))->isEnabled());
    QVERIFY(!findAction(dev, QStringLiteral("Iniciar"))->isEnabled());

    QSignalSpy actionSpy(&tray, &PodStack::TrayController::machineActionRequested);
    findAction(dev, QStringLiteral("Reiniciar"))->trigger();
    QCOMPARE(actionSpy.count(), 1);
    QCOMPARE(actionSpy.first().at(0).toString(), QStringLiteral("restart"));

    QSignalSpy openSpy(&tray, &PodStack::TrayController::machineOpenRequested);
    findAction(dev, QStringLiteral("Abrir detalhe"))->trigger();
    QCOMPARE(openSpy.count(), 1);

    QSignalSpy sshSpy(&tray, &PodStack::TrayController::machineSshRequested);
    findAction(dev, QStringLiteral("SSH"))->trigger();
    QCOMPARE(sshSpy.count(), 1);
}

void TrayTest::kubernetesSubmenu()
{
    PodStack::TrayController tray(false);
    QMenu *kubernetes = findSubmenu(tray.contextMenu(), QStringLiteral("Kubernetes"));
    QVERIFY(kubernetes != nullptr);
    QVERIFY(findAction(kubernetes, QStringLiteral("Cluster desabilitado")) != nullptr);

    QSignalSpy viewSpy(&tray, &PodStack::TrayController::openViewRequested);
    findAction(kubernetes, QStringLiteral("Abrir Kubernetes"))->trigger();
    QCOMPARE(viewSpy.count(), 1);
    QCOMPARE(viewSpy.first().at(0).toString(), QStringLiteral("kubernetes"));

    tray.setKubernetes(true);
    QVERIFY(findAction(kubernetes, QStringLiteral("Cluster habilitado")) != nullptr);
}

void TrayTest::activationSignals()
{
    PodStack::TrayController tray(false);
    QSignalSpy openSpy(&tray, &PodStack::TrayController::openRequested);
    QSignalSpy activitySpy(&tray, &PodStack::TrayController::activityMonitorRequested);

    emit tray.trayIcon()->activated(QSystemTrayIcon::Trigger);
    QCOMPARE(openSpy.count(), 1);

    emit tray.trayIcon()->activated(QSystemTrayIcon::MiddleClick);
    QCOMPARE(activitySpy.count(), 1);

    tray.reloadConfig();
}

void TrayTest::pausedContainerActions()
{
    PodStack::TrayController tray(false);
    tray.setContainers({QVariantMap{{QStringLiteral("id"), QStringLiteral("paused-1")},
                                    {QStringLiteral("name"), QStringLiteral("paused")},
                                    {QStringLiteral("state"), QStringLiteral("paused")}}});

    QMenu *containersMenu = findSubmenu(tray.contextMenu(), QStringLiteral("Containers"));
    QVERIFY(containersMenu != nullptr);
    QMenu *paused = findSubmenu(containersMenu, QStringLiteral("paused"));
    QVERIFY(paused != nullptr);

    QSignalSpy spy(&tray, &PodStack::TrayController::containerActionRequested);
    findAction(paused, QStringLiteral("Retomar"))->trigger();
    findAction(paused, QStringLiteral("Parar"))->trigger();
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("unpause"));
}

void TrayTest::requestOpenContainer()
{
    PodStack::TrayController tray(false);
    QSignalSpy spy(&tray, &PodStack::TrayController::openContainerRequested);
    tray.requestOpenContainer(QStringLiteral("abc"), QStringLiteral("logs"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("abc"));
    QCOMPARE(spy.first().at(1).toString(), QStringLiteral("logs"));
}

void TrayTest::containersMenuOverflow()
{
    PodStack::TrayController tray(false);
    tray.setConnected(true);

    QVariantList containers;
    for (int index = 0; index < 30; ++index) {
        containers.append(QVariantMap{
            {QStringLiteral("id"), QStringLiteral("id-%1").arg(index)},
            {QStringLiteral("name"), QStringLiteral("container-%1").arg(index)},
            {QStringLiteral("state"), QStringLiteral("exited")}});
    }
    tray.setContainers(containers);

    emit tray.contextMenu()->aboutToShow();

    QSignalSpy spy(&tray, &PodStack::TrayController::openViewRequested);
    QMenu *containersMenu = findSubmenu(tray.contextMenu(), QStringLiteral("Containers"));
    QVERIFY(containersMenu != nullptr);
    QAction *all = findAction(containersMenu, QStringLiteral("Ver todos"));
    QVERIFY(all != nullptr);
    all->trigger();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("containers"));
}

} // namespace

QTEST_MAIN(TrayTest)
#include "test_tray.moc"
