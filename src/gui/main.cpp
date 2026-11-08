#include "gui/Backend.h"
#include "gui/ContainerListModel.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include <qqml.h>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("PodStack"));
    QGuiApplication::setOrganizationName(QStringLiteral("PodStack"));
    QGuiApplication::setApplicationVersion(QStringLiteral(PODSTACK_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("dev.podstack.PodStack"));

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    qmlRegisterType<PodStack::ContainerListModel>("PodStack", 1, 0, "ContainerListModel");

    PodStack::Backend backend;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.loadFromModule(QStringLiteral("PodStack"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    return app.exec();
}
