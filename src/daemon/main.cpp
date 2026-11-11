#include "core/Config.h"
#include "core/Logging.h"
#include "core/Paths.h"
#include "daemon/CaddyProxy.h"
#include "daemon/Daemon.h"
#include "daemon/DnsServer.h"
#include "daemon/HttpProxy.h"
#include "daemon/RpcServer.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QSocketNotifier>
#include <QThreadPool>

#include <csignal>
#include <memory>
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

    std::unique_ptr<PodStack::HttpProxy> httpProxy;
    const PodStack::Config config = PodStack::Config::load();
    const bool exposeLan = config.get(QStringLiteral("network.exposeLan")).toBool(false);
    const int httpPort = config.get(QStringLiteral("network.httpPort")).toInt(8080);
    if (config.get(QStringLiteral("network.bridge")).toBool(true) && httpPort >= 1 &&
        httpPort <= 65535) {
        httpProxy = std::make_unique<PodStack::HttpProxy>(
            [&daemon](QVector<PodStack::ContainerInfo> *containers, PodStack::Error *proxyError) {
                return daemon.listContainersForProxy(containers, proxyError);
            });
        httpProxy->setExposeLan(exposeLan);
        PodStack::Error proxyError;
        if (!httpProxy->listen(static_cast<quint16>(httpPort), &proxyError)) {
            qCWarning(lcRpc) << "Proxy HTTP de *.localhost indisponível:" << proxyError.message
                             << proxyError.detail;
            httpProxy.reset();
        }
    }

    std::unique_ptr<PodStack::CaddyProxy> caddyProxy;
    const int httpsPort = config.get(QStringLiteral("network.httpsPort")).toInt(8443);
    if (httpProxy && config.get(QStringLiteral("network.https")).toBool(false) && httpsPort >= 1 &&
        httpsPort <= 65535) {
        caddyProxy = std::make_unique<PodStack::CaddyProxy>();
        caddyProxy->setExposeLan(exposeLan);
        PodStack::Error caddyError;
        if (!caddyProxy->start(static_cast<quint16>(httpsPort), static_cast<quint16>(httpPort),
                               &caddyError)) {
            qCWarning(lcRpc) << "HTTPS de *.localhost/*.podstack.local indisponível:"
                             << caddyError.message << caddyError.detail;
            caddyProxy.reset();
        }
    }

    std::unique_ptr<PodStack::DnsServer> dnsServer;
    if (config.get(QStringLiteral("network.bridge")).toBool(true)) {
        dnsServer = std::make_unique<PodStack::DnsServer>();
        PodStack::Error dnsError;
        if (!dnsServer->listen(5353, &dnsError)) {
            qCWarning(lcRpc) << "DNS de *.podstack.local indisponível:" << dnsError.message
                             << dnsError.detail;
            dnsServer.reset();
        }
    }

    qInfo("podstackd %s pronto", PODSTACK_VERSION);
    const int exitCode = app.exec();
    // Workers de RPC ainda podem estar usando o Daemon/RpcServer (ex.: um
    // restart de machine em andamento). Espera antes de destruir os objetos.
    QThreadPool::globalInstance()->waitForDone();
    return exitCode;
}
