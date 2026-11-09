// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/Autostart.h"
#include "gui/Backend.h"
#include "gui/ContainerListModel.h"
#include "gui/Notifications.h"
#include "gui/SingleInstance.h"
#include "gui/Tray.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include <qqml.h>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
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

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("tray"), &tray);
    engine.rootContext()->setContextProperty(QStringLiteral("notifications"), &notifications);
    engine.rootContext()->setContextProperty(QStringLiteral("singleInstance"), &singleInstance);
    engine.rootContext()->setContextProperty(QStringLiteral("startHidden"), startHidden);
    engine.loadFromModule(QStringLiteral("PodStack"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    QGuiApplication::setWindowIcon(tray.icon());
    QObject::connect(&tray, &PodStack::TrayController::quitRequested, &app,
                     &QCoreApplication::quit);
    QObject::connect(&singleInstance, &PodStack::SingleInstance::quitRequested, &app,
                     &QCoreApplication::quit);

    return app.exec();
}
