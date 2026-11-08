// SPDX-License-Identifier: GPL-3.0-or-later

#include "helper/HelperService.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>

#include <cstdio>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("podstack-helper"));
    QCoreApplication::setOrganizationName(QStringLiteral("PodStack"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PODSTACK_VERSION));

    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        std::fprintf(stderr, "podstack-helper: barramento de sistema indisponível: %s\n",
                     qUtf8Printable(bus.lastError().message()));
        return 1;
    }

    PodStack::HelperService service;
    if (!bus.registerService(QStringLiteral("dev.podstack.Helper1"))) {
        std::fprintf(stderr, "podstack-helper: falha ao registrar o serviço: %s\n",
                     qUtf8Printable(bus.lastError().message()));
        return 1;
    }
    if (!bus.registerObject(QStringLiteral("/dev/podstack/Helper1"), &service,
                            QDBusConnection::ExportAllSlots)) {
        std::fprintf(stderr, "podstack-helper: falha ao exportar o objeto: %s\n",
                     qUtf8Printable(bus.lastError().message()));
        return 1;
    }

    return app.exec();
}
