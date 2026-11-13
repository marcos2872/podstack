// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Config.h"
#include "gui/Autostart.h"
#include "gui/Backend.h"
#include "gui/ContainerListModel.h"
#include "gui/MainWindow.h"
#include "gui/Notifications.h"
#include "gui/SingleInstance.h"
#include "gui/Translation.h"
#include "gui/Tray.h"
#include "gui/UnixSignals.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QIcon>
#include <QQuickStyle>

#include <QtQml/qqmlextensionplugin.h>
#include <qqml.h>

Q_IMPORT_QML_PLUGIN(PodStackPlugin)

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("PodStack"));
    QGuiApplication::setOrganizationName(QStringLiteral("PodStack"));
    QGuiApplication::setApplicationVersion(QStringLiteral(PODSTACK_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("dev.podstack.PodStack"));

    const QString language =
        PodStack::Config::load().get(QStringLiteral("app.language")).toString();
    if (!PodStack::installTranslation(&app, language)) {
        qWarning("podstack-gui: catálogo de tradução %s não carregou.", qUtf8Printable(language));
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("PodStack"));
    parser.addHelpOption();
    parser.addOption(
        QCommandLineOption(QStringLiteral("hidden"), QStringLiteral("Inicia apenas na bandeja.")));
    parser.addOption(QCommandLineOption(QStringLiteral("quit"),
                                        QStringLiteral("Encerra a instância em execução.")));
    parser.process(app);

    PodStack::SingleInstance singleInstance;
    const bool startHidden = parser.isSet(QStringLiteral("hidden"));
    const bool requestQuit = parser.isSet(QStringLiteral("quit"));

    QString instanceError;
    const QString command =
        requestQuit ? QStringLiteral("quit") : (startHidden ? QString() : QStringLiteral("show"));
    if (!singleInstance.start(command, &instanceError)) {
        if (!instanceError.isEmpty()) {
            qWarning("podstack-gui: %s", qUtf8Printable(instanceError));
            return 1;
        }
        return 0;
    }
    if (requestQuit) {
        return 0;
    }

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    qmlRegisterType<PodStack::ContainerListModel>("PodStack", 1, 0, "ContainerListModel");

    PodStack::Autostart::syncFromConfig();

    PodStack::Backend backend;
    PodStack::TrayController tray;
    PodStack::NotificationService notifications;
    notifications.setTrayIcon(tray.trayIcon());

    QObject::connect(&backend, &PodStack::Backend::containersChanged, &tray,
                     &PodStack::TrayController::setContainers);
    QObject::connect(&backend, &PodStack::Backend::stateChanged, &tray, [&backend, &tray]() {
        tray.setConnected(backend.connected());
        tray.setSetupPending(backend.setupPending());
    });
    QObject::connect(&backend, &PodStack::Backend::setupChanged, &tray,
                     [&backend, &tray]() { tray.setSetupPending(backend.setupPending()); });
    QObject::connect(&backend, &PodStack::Backend::machinesChanged, &tray,
                     [&backend, &tray]() { tray.setMachines(backend.machines()); });
    QObject::connect(&backend, &PodStack::Backend::k8sChanged, &tray,
                     [&backend, &tray]() { tray.setKubernetes(backend.k8sEnabled()); });
    QObject::connect(&backend, &PodStack::Backend::composeProjectsChanged, &tray,
                     [&backend, &tray]() { tray.setComposeProjects(backend.composeProjects()); });
    QObject::connect(&backend, &PodStack::Backend::actionStateChanged, &tray,
                     [&backend, &tray]() { tray.setActionActive(backend.actionActive()); });
    QObject::connect(
        &tray, &PodStack::TrayController::containerActionRequested, &backend,
        [&backend](const QString &action, const QString &id) { backend.lifecycle(action, id); });
    QObject::connect(&tray, &PodStack::TrayController::composeActionRequested, &backend,
                     [&backend](const QString &action, const QString &path) {
                         if (action == QStringLiteral("start")) {
                             backend.composeStart(path, QString());
                         } else if (action == QStringLiteral("stop")) {
                             backend.composeStop(path, QString());
                         } else if (action == QStringLiteral("restart")) {
                             backend.composeRestart(path, QString());
                         }
                     });
    QObject::connect(&tray, &PodStack::TrayController::refreshRequested, &backend,
                     &PodStack::Backend::refresh);
    QObject::connect(&backend, &PodStack::Backend::settingsChanged, &tray,
                     &PodStack::TrayController::reloadConfig);
    QObject::connect(&tray, &PodStack::TrayController::machineActionRequested, &backend,
                     [&backend](const QString &action, const QString &id) {
                         backend.machineLifecycle(action, id);
                     });
    QObject::connect(&tray, &PodStack::TrayController::machineSshRequested, &backend,
                     [&backend](const QString &id) { backend.openMachineSsh(id); });
    QObject::connect(&backend, &PodStack::Backend::containerEvent, &notifications,
                     &PodStack::NotificationService::notifyContainerEvent);
    QObject::connect(&tray, &PodStack::TrayController::updateCheckRequested, &backend,
                     [&backend]() { backend.checkForUpdates(true); });
    QObject::connect(&backend, &PodStack::Backend::updateAvailable, &notifications,
                     [&notifications](const QVariantMap &release) {
                         notifications.notifyUpdate(
                             release.value(QStringLiteral("latestVersion")).toString(),
                             release.value(QStringLiteral("channel")).toString(),
                             release.value(QStringLiteral("releaseUrl")).toString());
                     });
    QObject::connect(&backend, &PodStack::Backend::updateCheckFailed, &notifications,
                     &PodStack::NotificationService::notifyUpdateError);
    QObject::connect(&backend, &PodStack::Backend::diskLow, &notifications,
                     &PodStack::NotificationService::notifyDiskLow);
    tray.setConnected(backend.connected());
    tray.setSetupPending(backend.setupPending());
    tray.setComposeProjects(backend.composeProjects());
    tray.setMachines(backend.machines());
    tray.setKubernetes(backend.k8sEnabled());
    tray.setActionActive(backend.actionActive());

    // A dock usa o ícone do desktop entry via app_id; em dev (sem o pacote
    // instalado) o fallback é o ícone do app embutido no recurso do QML, não
    // o ícone da bandeja (que carrega o ponto de status).
    QIcon appIcon =
        QIcon::fromTheme(QStringLiteral("dev.podstack.PodStack"),
                         QIcon(QStringLiteral(":/qt/qml/PodStack/assets/podstack-icon.svg")));
    if (appIcon.isNull()) {
        appIcon = tray.icon();
    }
    QGuiApplication::setWindowIcon(appIcon);

    PodStack::MainWindow window(&backend, &tray, &notifications, &singleInstance, startHidden);
    QObject::connect(&tray, &PodStack::TrayController::openViewRequested, &window,
                     &PodStack::MainWindow::openSection);
    QObject::connect(&tray, &PodStack::TrayController::machineOpenRequested, &window,
                     [&window](const QString &id) { window.openMachineDetail(id); });
    // O QQuickWidget pode carregar de forma assíncrona (especialmente com QML
    // cache frio no CI); só falhe se o carregamento terminou em erro.
    if (window.qmlFailed()) {
        qWarning("podstack-gui: falha ao carregar a UI: %s",
                 qPrintable(window.qmlErrors().join(QStringLiteral(" | "))));
        return 1;
    }

    QObject::connect(&tray, &PodStack::TrayController::quitRequested, &app,
                     &QCoreApplication::quit);
    QObject::connect(&singleInstance, &PodStack::SingleInstance::quitRequested, &app,
                     &QCoreApplication::quit);

    // O systemd encerra a GUI com SIGTERM no update; salva o UiState antes de
    // sair para a janela reabrir na mesma seção/aba.
    QString signalError;
    if (!PodStack::installTerminationHandler(
            &app,
            [&window, &app]() {
                window.prepareForExit();
                app.quit();
            },
            &signalError)) {
        qWarning("podstack-gui: %s", qUtf8Printable(signalError));
    }

    return app.exec();
}
