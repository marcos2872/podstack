// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/Internal.h"

using namespace PodStack::CliInternal;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("PodStack"));
    QCoreApplication::setOrganizationName(QStringLiteral("PodStack"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PODSTACK_VERSION));

    PodStack::Logging::init(false);

    QStringList args = app.arguments();
    args.removeFirst();
    if (args.isEmpty()) {
        printUsage();
        return kExitUsage;
    }

    const QString command = args.takeFirst();
    if (command == QStringLiteral("version") || command == QStringLiteral("--version")) {
        return commandVersion();
    }
    if (command == QStringLiteral("status")) {
        return commandStatus();
    }
    if (command == QStringLiteral("ps")) {
        return commandPs(args);
    }
    if (command == QStringLiteral("pause")) {
        return commandLifecycle(QStringLiteral("pause"), args);
    }
    if (command == QStringLiteral("unpause")) {
        return commandLifecycle(QStringLiteral("unpause"), args);
    }
    if (command == QStringLiteral("kill")) {
        return commandLifecycle(QStringLiteral("kill"), args);
    }
    if (command == QStringLiteral("checkpoint")) {
        return commandCheckpoint(args);
    }
    if (command == QStringLiteral("restore")) {
        return commandRestore(args);
    }
    if (command == QStringLiteral("inspect")) {
        return commandInspect(args);
    }
    if (command == QStringLiteral("exec")) {
        return commandExec(args);
    }
    if (command == QStringLiteral("images")) {
        return commandImages(args);
    }
    if (command == QStringLiteral("volumes")) {
        return commandVolumes(args);
    }
    if (command == QStringLiteral("networks")) {
        return commandNetworks(args);
    }
    if (command == QStringLiteral("pods")) {
        return commandPods(args);
    }
    if (command == QStringLiteral("compose")) {
        return commandCompose(args);
    }
    if (command == QStringLiteral("build")) {
        return commandBuild(args);
    }
    if (command == QStringLiteral("builds")) {
        return commandBuilds(args);
    }
    if (command == QStringLiteral("machine")) {
        return commandMachine(args);
    }
    if (command == QStringLiteral("kube")) {
        return commandKube(args);
    }
    if (command == QStringLiteral("ssh")) {
        return machineSshCommand(args);
    }
    if (command == QStringLiteral("setup")) {
        return commandSetup(args);
    }
    if (command == QStringLiteral("config")) {
        return commandConfig(args);
    }
    if (command == QStringLiteral("system")) {
        return commandSystem(args);
    }
    if (command == QStringLiteral("top")) {
        return commandTop(args);
    }
    if (command == QStringLiteral("doctor")) {
        return commandDoctor();
    }
    if (command == QStringLiteral("update")) {
        return commandUpdate(args);
    }
    if (command == QStringLiteral("report")) {
        return commandReport(args);
    }
    if (command == QStringLiteral("help") || command == QStringLiteral("--help")
        || command == QStringLiteral("-h")) {
        printUsage();
        return kExitOk;
    }

    err() << "comando desconhecido: " << command << '\n';
    printUsage();
    return kExitUsage;
}
