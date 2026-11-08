#include "core/Logging.h"
#include "core/Paths.h"
#include "daemon/Daemon.h"
#include "daemon/RpcServer.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QSocketNotifier>

#include <csignal>
#include <unistd.h>

namespace {

int gSignalFd[2] = {-1, -1};

void handleSignal(int)
{
    const char byte = 1;
    if (gSignalFd[1] >= 0) {
        [[maybe_unused]] const ssize_t written = ::write(gSignalFd[1], &byte, 1);
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("PodStack"));
    QCoreApplication::setOrganizationName(QStringLiteral("PodStack"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PODSTACK_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("PodStack daemon (podstackd)"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption verboseOption(QStringList{QStringLiteral("verbose")},
                                     QStringLiteral("Habilita logs de debug."));
    QCommandLineOption socketOption(QStringLiteral("socket"),
                                    QStringLiteral("Socket de controle."),
                                    QStringLiteral("path"), PodStack::Paths::controlSocket());
    parser.addOption(verboseOption);
    parser.addOption(socketOption);
    parser.process(app);

    PodStack::Logging::init(parser.isSet(verboseOption));

    QString error;
    if (!PodStack::Paths::ensureDirs(&error)) {
        qCritical().noquote() << error;
        return 1;
    }

    PodStack::Daemon daemon;
    PodStack::RpcServer server(&daemon);

    PodStack::Error listenError;
    const QString socketPath = parser.value(socketOption);
    if (!server.listen(socketPath, &listenError)) {
        qCritical().noquote() << listenError.message << listenError.detail;
        return 1;
    }

    if (::pipe(gSignalFd) == 0) {
        auto *notifier = new QSocketNotifier(gSignalFd[0], QSocketNotifier::Read, &app);
        QObject::connect(notifier, &QSocketNotifier::activated, &app, [&app] {
            char byte = 0;
            [[maybe_unused]] const ssize_t readBytes = ::read(gSignalFd[0], &byte, 1);
            app.quit();
        });
        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);
    }

    qInfo("podstackd %s pronto", PODSTACK_VERSION);
    return app.exec();
}
