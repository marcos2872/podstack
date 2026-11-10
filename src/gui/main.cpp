// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/Autostart.h"
#include "gui/Backend.h"
#include "gui/ContainerListModel.h"
#include "gui/MainWindow.h"
#include "gui/Notifications.h"
#include "gui/SingleInstance.h"
#include "gui/Tray.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QIcon>
#include <QQuickStyle>

#include <cstdio>

#include <QtQml/qqmlextensionplugin.h>
#include <qqml.h>

Q_IMPORT_QML_PLUGIN(PodStackPlugin)

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    std::fprintf(stderr, "podstack-gui: aplicação iniciada\n");
    std::fflush(stderr);
    QGuiApplication::setApplicationName(QStringLiteral("PodStack"));
    QGuiApplication::setOrganizationName(QStringLiteral("PodStack"));
    QGuiApplication::setApplicationVersion(QStringLiteral(PODSTACK_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("dev.podstack.PodStack"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("PodStack"));
    parser.addHelpOption();
    parser.addOption(QCommandLineOption(QStringLiteral("hidden"),
                                        QStringLiteral("Inicia apenas na bandeja.")));
    parser.addOption(QCommandLineOption(QStringLiteral("quit"),
                                        QStringLiteral("Encerra a instância em execução.")));
    parser.process(app);

    PodStack::SingleInstance singleInstance;
    const bool startHidden = parser.isSet(QStringLiteral("hidden"));
    const bool requestQuit = parser.isSet(QStringLiteral("quit"));

    QString instanceError;
    const QString command = requestQuit ? QStringLiteral("quit")
                                        : (startHidden ? QString() : QStringLiteral("show"));
    if (!singleInstance.start(command, &instanceError)) {
        if (!instanceError.isEmpty()) {
            std::fprintf(stderr, "podstack-gui: single instance falhou: %s\n",
                         qUtf8Printable(instanceError));
            std::fflush(stderr);
            return 1;
        }
        return 0;
    }
    std::fprintf(stderr, "podstack-gui: single instance ok\n");
    std::fflush(stderr);
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
    QObject::connect(&backend, &PodStack::Backend::stateChanged, &tray,
                     [&backend, &tray]() {
                         tray.setConnected(backend.connected());
                         tray.setSetupPending(backend.setupPending());
                     });
    QObject::connect(&backend, &PodStack::Backend::setupChanged, &tray,
                     [&backend, &tray]() { tray.setSetupPending(backend.setupPending()); });
    QObject::connect(&backend, &PodStack::Backend::composeProjectsChanged, &tray,
                     [&backend, &tray]() {
                         tray.setComposeProjects(backend.composeProjects());
                     });
    QObject::connect(&backend, &PodStack::Backend::actionStateChanged, &tray,
                     [&backend, &tray]() { tray.setActionActive(backend.actionActive()); });
    QObject::connect(&tray, &PodStack::TrayController::containerActionRequested, &backend,
                     [&backend](const QString &action, const QString &id) {
                         backend.lifecycle(action, id);
                     });
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
    QObject::connect(&backend, &PodStack::Backend::containerEvent, &notifications,
                     &PodStack::NotificationService::notifyContainerEvent);
    tray.setConnected(backend.connected());
    tray.setSetupPending(backend.setupPending());
    tray.setComposeProjects(backend.composeProjects());
    tray.setActionActive(backend.actionActive());

    QGuiApplication::setWindowIcon(tray.icon());

    PodStack::MainWindow window(&backend, &tray, &notifications, &singleInstance, startHidden);
    std::fprintf(stderr, "podstack-gui: main window criada (loaded=%d failed=%d)\n",
                 window.qmlLoaded() ? 1 : 0, window.qmlFailed() ? 1 : 0);
    std::fflush(stderr);
    // O QQuickWidget pode carregar de forma assíncrona (especialmente com QML
    // cache frio no CI); só falhe se o carregamento terminou em erro.
    if (window.qmlFailed()) {
        std::fprintf(stderr, "podstack-gui: falha ao carregar a UI: %s\n",
                     qPrintable(window.qmlErrors().join(QStringLiteral(" | "))));
        std::fflush(stderr);
        return 1;
    }

    QObject::connect(&tray, &PodStack::TrayController::quitRequested, &app,
                     &QCoreApplication::quit);
    QObject::connect(&singleInstance, &PodStack::SingleInstance::quitRequested, &app,
                     &QCoreApplication::quit);

    return app.exec();
}
