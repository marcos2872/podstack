// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/Internal.h"

namespace PodStack::CliInternal {

int commandMachine(const QStringList &args)
{
    if (args.isEmpty()) {
        printMachineUsage(err());
        return kExitUsage;
    }

    const QString &subcommand = args.first();
    if (subcommand == QStringLiteral("--help") || subcommand == QStringLiteral("-h") ||
        subcommand == QStringLiteral("help")) {
        printMachineUsage(out());
        return kExitOk;
    }
    if (subcommand == QStringLiteral("list") || subcommand == QStringLiteral("ls")) {
        return machineListCommand(args);
    }
    if (subcommand == QStringLiteral("info")) {
        return machineInfoCommand(args);
    }
    if (subcommand == QStringLiteral("logs")) {
        return machineLogsCommand(args);
    }
    if (subcommand == QStringLiteral("console")) {
        return machineConsoleCommand(args);
    }
    if (subcommand == QStringLiteral("create")) {
        return machineCreateCommand(args);
    }
    if (subcommand == QStringLiteral("start") || subcommand == QStringLiteral("stop") ||
        subcommand == QStringLiteral("restart")) {
        return machineLifecycleCommand(subcommand, args);
    }
    if (subcommand == QStringLiteral("delete") || subcommand == QStringLiteral("rm")) {
        return machineDeleteCommand(args);
    }
    if (subcommand == QStringLiteral("export")) {
        return machineExportCommand(args);
    }
    if (subcommand == QStringLiteral("import")) {
        return machineImportCommand(args);
    }
    if (subcommand == QStringLiteral("rename")) {
        return machineRenameCommand(args);
    }
    if (subcommand == QStringLiteral("clone")) {
        return machineCloneCommand(args);
    }
    if (subcommand == QStringLiteral("update")) {
        return machineUpdateCommand(args);
    }
    if (subcommand == QStringLiteral("port") || subcommand == QStringLiteral("ports")) {
        return machinePortCommand(args.mid(1));
    }
    if (subcommand == QStringLiteral("serial")) {
        return machineSerialCommand(args.mid(1));
    }
    if (subcommand == QStringLiteral("usb")) {
        return machineUsbCommand(args.mid(1));
    }
    if (subcommand == QStringLiteral("audio")) {
        return machineAudioCommand(args.mid(1));
    }
    if (subcommand == QStringLiteral("snapshot") || subcommand == QStringLiteral("snapshots")) {
        return machineSnapshotCommand(args.mid(1));
    }
    if (subcommand == QStringLiteral("default")) {
        return machineDefaultCommand(args);
    }
    if (subcommand == QStringLiteral("ssh")) {
        return machineSshCommand(args.mid(1));
    }
    if (subcommand == QStringLiteral("files")) {
        return machineFilesCommand(args);
    }
    if (subcommand == QStringLiteral("pull") || subcommand == QStringLiteral("push")) {
        return machineTransferCommand(subcommand, args);
    }
    if (subcommand == QStringLiteral("distros")) {
        bool json = false;
        for (int i = 1; i < args.size(); ++i) {
            const QString &arg = args.at(i);
            if (arg == QStringLiteral("--json")) {
                json = true;
            } else {
                err() << "opção desconhecida: " << arg << '\n';
                return kExitUsage;
            }
        }

        PodStack::RpcClient client;
        PodStack::Error error;
        const QJsonObject result = client.call(QStringLiteral("machines.distros"), {}, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }

        if (json) {
            out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
            out().flush();
            return kExitOk;
        }

        const QJsonArray items = result.value(QStringLiteral("items")).toArray();
        out() << QStringLiteral("%1 %2 %3 %4 %5 %6\n")
                     .arg(QStringLiteral("DISTRO").leftJustified(10),
                          QStringLiteral("VERSÃO").leftJustified(8),
                          QStringLiteral("ARCH").leftJustified(7),
                          QStringLiteral("PADRÃO").leftJustified(8),
                          QStringLiteral("CACHE").leftJustified(7), QStringLiteral("URL"));
        for (const QJsonValue &value : items) {
            const QJsonObject item = value.toObject();
            out() << QStringLiteral("%1 %2 %3 %4 %5 %6\n")
                         .arg(item.value(QStringLiteral("distro")).toString().leftJustified(10),
                              item.value(QStringLiteral("version")).toString().leftJustified(8),
                              item.value(QStringLiteral("arch")).toString().leftJustified(7),
                              (item.value(QStringLiteral("default")).toBool()
                                   ? QStringLiteral("sim")
                                   : QStringLiteral("não"))
                                  .leftJustified(8),
                              (item.value(QStringLiteral("cached")).toBool()
                                   ? QStringLiteral("sim")
                                   : QStringLiteral("não"))
                                  .leftJustified(7),
                              item.value(QStringLiteral("url")).toString());
        }
        out().flush();
        return kExitOk;
    }

    err() << "subcomando desconhecido: machine " << subcommand << '\n';
    printMachineUsage(err());
    return kExitUsage;
}

int machineListCommand(const QStringList &args)
{
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result = client.call(QStringLiteral("machines.list"), {}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    struct Row {
        QString name;
        QString distro;
        QString arch;
        QString state;
        QString ssh;
    };
    QVector<Row> rows;
    int nameWidth = 4;
    int distroWidth = 6;
    int archWidth = 4;
    int stateWidth = 5;
    for (const QJsonValue &value : result.value(QStringLiteral("items")).toArray()) {
        const QJsonObject machine = value.toObject();
        Row row;
        row.name = machine.value(QStringLiteral("name")).toString();
        row.distro = machineDistroLabel(machine);
        row.arch = machine.value(QStringLiteral("arch")).toString();
        row.state = machine.value(QStringLiteral("state")).toString();
        row.ssh = machineSsh(machine);
        nameWidth = qMax(nameWidth, row.name.size());
        distroWidth = qMax(distroWidth, row.distro.size());
        archWidth = qMax(archWidth, row.arch.size());
        stateWidth = qMax(stateWidth, row.state.size());
        rows.append(row);
    }

    out() << QStringLiteral("%1  %2  %3  %4  %5\n")
                 .arg(QStringLiteral("NOME").leftJustified(nameWidth))
                 .arg(QStringLiteral("DISTRO").leftJustified(distroWidth))
                 .arg(QStringLiteral("ARCH").leftJustified(archWidth))
                 .arg(QStringLiteral("STATE").leftJustified(stateWidth))
                 .arg(QStringLiteral("SSH"));
    for (const Row &row : rows) {
        out() << QStringLiteral("%1  %2  %3  %4  %5\n")
                     .arg(row.name.leftJustified(nameWidth))
                     .arg(row.distro.leftJustified(distroWidth))
                     .arg(row.arch.leftJustified(archWidth))
                     .arg(row.state.leftJustified(stateWidth))
                     .arg(row.ssh);
    }
    out().flush();
    return kExitOk;
}

int machineInfoCommand(const QStringList &args)
{
    QString id;
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (!arg.startsWith(QLatin1Char('-')) && id.isEmpty()) {
            id = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (id.isEmpty()) {
        printMachineUsage(err());
        return kExitUsage;
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject machine = client.call(QStringLiteral("machines.get"),
                                            QJsonObject{{QStringLiteral("id"), id}}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(machine).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    out() << "id: " << machine.value(QStringLiteral("id")).toString() << '\n'
          << "nome: " << machine.value(QStringLiteral("name")).toString() << '\n'
          << "distro: " << machineDistroLabel(machine) << '\n'
          << "arch: " << machine.value(QStringLiteral("arch")).toString() << '\n'
          << "estado: " << machine.value(QStringLiteral("state")).toString() << '\n'
          << "cpus: " << machine.value(QStringLiteral("cpus")).toInt() << '\n'
          << "memória: " << formatSize(machine.value(QStringLiteral("memoryBytes")).toInteger())
          << '\n'
          << "disco: " << formatSize(machine.value(QStringLiteral("diskBytes")).toInteger()) << '\n'
          << "ssh: " << machineSsh(machine) << '\n'
          << "criada: " << machine.value(QStringLiteral("createdAt")).toString() << '\n';
    const QString machineError = machine.value(QStringLiteral("error")).toString();
    if (!machineError.isEmpty()) {
        out() << "erro: " << machineError << '\n';
    }
    out().flush();
    return kExitOk;
}

int machineLogsCommand(const QStringList &args)
{
    QString id;
    int tail = 200;
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (arg == QStringLiteral("--tail") && i + 1 < args.size()) {
            bool ok = false;
            tail = args.at(++i).toInt(&ok);
            if (!ok || tail < 1) {
                err() << "--tail exige um inteiro positivo\n";
                return kExitUsage;
            }
        } else if (!arg.startsWith(QLatin1Char('-')) && id.isEmpty()) {
            id = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (id.isEmpty()) {
        printMachineUsage(err());
        return kExitUsage;
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result = client.call(
        QStringLiteral("machines.logs"),
        QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("tail"), tail}}, &error, 30000);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    const QJsonArray lines = result.value(QStringLiteral("lines")).toArray();
    for (const QJsonValue &line : lines) {
        out() << line.toString() << '\n';
    }
    out().flush();
    return kExitOk;
}

int machineConsoleCommand(const QStringList &args)
{
    QString id;
    qint64 tailBytes = 64LL * 1024LL;
    bool noFollow = false;
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--tail") && i + 1 < args.size()) {
            bool ok = false;
            tailBytes = args.at(++i).toLongLong(&ok);
            if (!ok || tailBytes < 1) {
                err() << "--tail exige um inteiro positivo (bytes)\n";
                return kExitUsage;
            }
        } else if (arg.startsWith(QStringLiteral("--tail="))) {
            bool ok = false;
            tailBytes = arg.mid(7).toLongLong(&ok);
            if (!ok || tailBytes < 1) {
                err() << "--tail exige um inteiro positivo (bytes)\n";
                return kExitUsage;
            }
        } else if (arg == QStringLiteral("--no-follow")) {
            noFollow = true;
        } else if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (!arg.startsWith(QLatin1Char('-')) && id.isEmpty()) {
            id = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (id.isEmpty()) {
        printMachineUsage(err());
        return kExitUsage;
    }
    if (json && !noFollow) {
        err() << "--json só é aceito com --no-follow\n";
        return kExitUsage;
    }

    const QJsonObject params{{QStringLiteral("id"), id}, {QStringLiteral("tailBytes"), tailBytes}};

    if (noFollow) {
        PodStack::RpcClient client;
        PodStack::Error error;
        const QJsonObject result =
            client.call(QStringLiteral("machines.console.read"), params, &error, 30000);
        if (!error.isEmpty()) {
            return fail(error);
        }

        if (json) {
            out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
            out().flush();
            return kExitOk;
        }

        const QString data = result.value(QStringLiteral("data")).toString();
        out() << data;
        if (!data.isEmpty() && !data.endsWith(QLatin1Char('\n'))) {
            out() << '\n';
        }
        out().flush();
        return kExitOk;
    }

    PodStack::RpcStream stream;
    PodStack::Error error;
    const bool ok = stream.run(
        QStringLiteral("machines.console"), params,
        [&](const QString &method, const QJsonObject &payload) {
            if (method == QStringLiteral("machines.console.delta")) {
                out() << payload.value(QStringLiteral("data")).toString();
                out().flush();
            }
        },
        &error, 0, false);
    return ok ? kExitOk : fail(error);
}

int machineCreateCommand(const QStringList &args)
{
    int cpus = 2;
    qint64 memoryBytes = 4LL * 1024LL * 1024LL * 1024LL;
    qint64 diskBytes = 20LL * 1024LL * 1024LL * 1024LL;
    QString user;
    QString userDataPath;
    bool isolateNetwork = false;
    bool forwardSshAgent = false;
    bool isolated = false;
    QStringList positional;

    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--cpus") && i + 1 < args.size()) {
            bool ok = false;
            cpus = args.at(++i).toInt(&ok);
            if (!ok) {
                err() << "--cpus exige um número inteiro\n";
                return kExitUsage;
            }
        } else if (arg == QStringLiteral("--memory") && i + 1 < args.size()) {
            if (!parseSize(args.at(++i), &memoryBytes)) {
                err() << "--memory inválido; use bytes ou sufixo K/M/G/T (ex.: 4G)\n";
                return kExitUsage;
            }
        } else if (arg == QStringLiteral("--disk") && i + 1 < args.size()) {
            if (!parseSize(args.at(++i), &diskBytes)) {
                err() << "--disk inválido; use bytes ou sufixo K/M/G/T (ex.: 20G)\n";
                return kExitUsage;
            }
        } else if (arg == QStringLiteral("--user") && i + 1 < args.size()) {
            user = args.at(++i);
        } else if (arg == QStringLiteral("--user-data") && i + 1 < args.size()) {
            userDataPath = args.at(++i);
        } else if (arg == QStringLiteral("--isolate-network")) {
            isolateNetwork = true;
        } else if (arg == QStringLiteral("--isolated")) {
            isolated = true;
        } else if (arg == QStringLiteral("--forward-ssh-agent")) {
            forwardSshAgent = true;
        } else if (arg.startsWith(QLatin1Char('-'))) {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        } else {
            positional.append(arg);
        }
    }

    if (positional.size() != 2) {
        printMachineUsage(err());
        return kExitUsage;
    }

    QString userData;
    if (!userDataPath.isEmpty()) {
        QFile file(userDataPath);
        if (!file.open(QIODevice::ReadOnly)) {
            err() << "não foi possível ler " << userDataPath << ": " << file.errorString() << '\n';
            err().flush();
            return kExitError;
        }
        userData = QString::fromUtf8(file.readAll());
    }

    QJsonObject params;
    params.insert(QStringLiteral("distro"), positional.at(0));
    params.insert(QStringLiteral("name"), positional.at(1));
    params.insert(QStringLiteral("cpus"), cpus);
    params.insert(QStringLiteral("memoryBytes"), memoryBytes);
    params.insert(QStringLiteral("diskBytes"), diskBytes);
    if (!user.isEmpty()) {
        params.insert(QStringLiteral("user"), user);
    }
    if (!userData.isEmpty()) {
        params.insert(QStringLiteral("userData"), userData);
    }
    if (isolateNetwork) {
        params.insert(QStringLiteral("isolateNetwork"), true);
    }
    if (isolated) {
        params.insert(QStringLiteral("isolated"), true);
    }
    if (forwardSshAgent) {
        params.insert(QStringLiteral("forwardSshAgent"), true);
    }

    QString jobId;
    QString machineId;
    QString machineState;
    PodStack::Error doneError;
    PodStack::RpcStream stream;
    PodStack::Error error;
    const bool ok = stream.run(
        QStringLiteral("machines.create"), params,
        [&](const QString &method, const QJsonObject &payload) {
            if (method == QStringLiteral("machines.delta")) {
                jobId = payload.value(QStringLiteral("jobId")).toString();
                out() << '[' << payload.value(QStringLiteral("stage")).toString() << "] "
                      << payload.value(QStringLiteral("progress")).toInt() << "%\n";
                out().flush();
            } else if (method == QStringLiteral("machines.done")) {
                jobId = payload.value(QStringLiteral("jobId")).toString();
                const QJsonObject errorObject = payload.value(QStringLiteral("error")).toObject();
                if (!errorObject.isEmpty()) {
                    doneError = errorFromJson(errorObject);
                }
                const QJsonObject machine = payload.value(QStringLiteral("machine")).toObject();
                machineId = machine.value(QStringLiteral("id")).toString();
                machineState = machine.value(QStringLiteral("state")).toString();
            }
        },
        &error);
    if (!ok) {
        return fail(error);
    }
    if (!doneError.isEmpty()) {
        return fail(doneError);
    }
    if (machineId.isEmpty()) {
        err() << "a criação terminou sem retornar a machine " << jobId << '\n';
        err().flush();
        return kExitError;
    }

    out() << "machine " << machineId << " criada (" << machineState << ")\n";
    out().flush();
    return kExitOk;
}

int machineLifecycleCommand(const QString &action, const QStringList &args)
{
    QString id;
    bool json = false;
    bool force = false;
    const bool acceptsForce = action == QStringLiteral("stop");
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (arg == QStringLiteral("--force") && acceptsForce) {
            force = true;
        } else if (!arg.startsWith(QLatin1Char('-')) && id.isEmpty()) {
            id = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (id.isEmpty()) {
        printMachineUsage(err());
        return kExitUsage;
    }

    QString method;
    if (action == QStringLiteral("start")) {
        method = QStringLiteral("machines.start");
    } else if (action == QStringLiteral("stop")) {
        method = QStringLiteral("machines.stop");
    } else {
        method = QStringLiteral("machines.restart");
    }

    QJsonObject params;
    params.insert(QStringLiteral("id"), id);
    if (force) {
        params.insert(QStringLiteral("force"), true);
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result = client.call(method, params, &error, 120000);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    const QString name = result.value(QStringLiteral("name")).toString();
    const QString display = name.isEmpty() ? result.value(QStringLiteral("id")).toString() : name;
    const QString state = result.value(QStringLiteral("state")).toString();
    if (action == QStringLiteral("start")) {
        out() << "machine " << display << " iniciada (" << state << ")\n";
    } else if (action == QStringLiteral("stop")) {
        out() << "machine " << display << " parada (" << state << ")\n";
    } else {
        out() << "machine " << display << " reiniciada (" << state << ")\n";
    }
    out().flush();
    return kExitOk;
}

int machineDeleteCommand(const QStringList &args)
{
    QString id;
    bool json = false;
    bool force = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (arg == QStringLiteral("--force")) {
            force = true;
        } else if (!arg.startsWith(QLatin1Char('-')) && id.isEmpty()) {
            id = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (id.isEmpty()) {
        printMachineUsage(err());
        return kExitUsage;
    }

    QJsonObject params;
    params.insert(QStringLiteral("id"), id);
    if (force) {
        params.insert(QStringLiteral("force"), true);
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result =
        client.call(QStringLiteral("machines.delete"), params, &error, 120000);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    out() << "machine " << id << " removida\n";
    out().flush();
    return kExitOk;
}

int machineExportCommand(const QStringList &args)
{
    QString id;
    QString file;
    bool force = false;
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (arg == QStringLiteral("--force")) {
            force = true;
        } else if (!arg.startsWith(QLatin1Char('-')) && id.isEmpty()) {
            id = arg;
        } else if (!arg.startsWith(QLatin1Char('-')) && file.isEmpty()) {
            file = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (id.isEmpty() || file.isEmpty()) {
        printMachineUsage(err());
        return kExitUsage;
    }

    QJsonObject params;
    params.insert(QStringLiteral("id"), id);
    params.insert(QStringLiteral("path"), QFileInfo(file).absoluteFilePath());
    if (force) {
        params.insert(QStringLiteral("force"), true);
    }

    QJsonObject donePayload;
    PodStack::Error doneError;
    PodStack::RpcStream stream;
    PodStack::Error error;
    const bool ok = stream.run(
        QStringLiteral("machines.export"), params,
        [&](const QString &method, const QJsonObject &payload) {
            if (method == QStringLiteral("machines.delta")) {
                if (!json) {
                    out() << '[' << payload.value(QStringLiteral("stage")).toString() << "] "
                          << payload.value(QStringLiteral("progress")).toInt() << "%\n";
                    out().flush();
                }
            } else if (method == QStringLiteral("machines.done")) {
                donePayload = payload;
                const QJsonObject errorObject = payload.value(QStringLiteral("error")).toObject();
                if (!errorObject.isEmpty()) {
                    doneError = errorFromJson(errorObject);
                }
            }
        },
        &error);
    if (!ok) {
        return fail(error);
    }
    if (!doneError.isEmpty()) {
        return fail(doneError);
    }

    if (json) {
        out() << QJsonDocument(donePayload).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    const QJsonObject machine = donePayload.value(QStringLiteral("machine")).toObject();
    out() << "machine " << machine.value(QStringLiteral("id")).toString() << " exportada para "
          << donePayload.value(QStringLiteral("path")).toString() << '\n';
    out().flush();
    return kExitOk;
}

int machineImportCommand(const QStringList &args)
{
    QString file;
    QString name;
    bool force = false;
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (arg == QStringLiteral("--force")) {
            force = true;
        } else if (arg == QStringLiteral("--name")) {
            if (i + 1 >= args.size()) {
                err() << "falta o valor de --name\n";
                return kExitUsage;
            }
            name = args.at(++i);
        } else if (!arg.startsWith(QLatin1Char('-')) && file.isEmpty()) {
            file = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (file.isEmpty()) {
        printMachineUsage(err());
        return kExitUsage;
    }

    QJsonObject params;
    params.insert(QStringLiteral("path"), QFileInfo(file).absoluteFilePath());
    if (!name.isEmpty()) {
        params.insert(QStringLiteral("name"), name);
    }
    if (force) {
        params.insert(QStringLiteral("force"), true);
    }

    QJsonObject donePayload;
    PodStack::Error doneError;
    PodStack::RpcStream stream;
    PodStack::Error error;
    const bool ok = stream.run(
        QStringLiteral("machines.import"), params,
        [&](const QString &method, const QJsonObject &payload) {
            if (method == QStringLiteral("machines.delta")) {
                if (!json) {
                    out() << '[' << payload.value(QStringLiteral("stage")).toString() << "] "
                          << payload.value(QStringLiteral("progress")).toInt() << "%\n";
                    out().flush();
                }
            } else if (method == QStringLiteral("machines.done")) {
                donePayload = payload;
                const QJsonObject errorObject = payload.value(QStringLiteral("error")).toObject();
                if (!errorObject.isEmpty()) {
                    doneError = errorFromJson(errorObject);
                }
            }
        },
        &error);
    if (!ok) {
        return fail(error);
    }
    if (!doneError.isEmpty()) {
        return fail(doneError);
    }

    if (json) {
        out() << QJsonDocument(donePayload).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    const QJsonObject machine = donePayload.value(QStringLiteral("machine")).toObject();
    out() << "machine " << machine.value(QStringLiteral("id")).toString() << " importada como "
          << machine.value(QStringLiteral("name")).toString() << " ("
          << machine.value(QStringLiteral("state")).toString() << ")\n";
    out().flush();
    return kExitOk;
}

int machineRenameCommand(const QStringList &args)
{
    QString id;
    QString name;
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (!arg.startsWith(QLatin1Char('-')) && id.isEmpty()) {
            id = arg;
        } else if (!arg.startsWith(QLatin1Char('-')) && name.isEmpty()) {
            name = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (id.isEmpty() || name.isEmpty()) {
        printMachineUsage(err());
        return kExitUsage;
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result = client.call(
        QStringLiteral("machines.rename"),
        QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("name"), name}}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    out() << "machine " << result.value(QStringLiteral("id")).toString() << " renomeada para "
          << result.value(QStringLiteral("name")).toString() << '\n';
    out().flush();
    return kExitOk;
}

int machineCloneCommand(const QStringList &args)
{
    QString id;
    QString name;
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (!arg.startsWith(QLatin1Char('-')) && id.isEmpty()) {
            id = arg;
        } else if (!arg.startsWith(QLatin1Char('-')) && name.isEmpty()) {
            name = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (id.isEmpty() || name.isEmpty()) {
        printMachineUsage(err());
        return kExitUsage;
    }

    QJsonObject donePayload;
    PodStack::Error doneError;
    PodStack::RpcStream stream;
    PodStack::Error error;
    const bool ok = stream.run(
        QStringLiteral("machines.clone"),
        QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("name"), name}},
        [&](const QString &method, const QJsonObject &payload) {
            if (method == QStringLiteral("machines.delta")) {
                if (!json) {
                    out() << '[' << payload.value(QStringLiteral("stage")).toString() << "] "
                          << payload.value(QStringLiteral("progress")).toInt() << "%\n";
                    out().flush();
                }
            } else if (method == QStringLiteral("machines.done")) {
                donePayload = payload;
                const QJsonObject errorObject = payload.value(QStringLiteral("error")).toObject();
                if (!errorObject.isEmpty()) {
                    doneError = errorFromJson(errorObject);
                }
            }
        },
        &error);
    if (!ok) {
        return fail(error);
    }
    if (!doneError.isEmpty()) {
        return fail(doneError);
    }

    if (json) {
        out() << QJsonDocument(donePayload).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    const QJsonObject machine = donePayload.value(QStringLiteral("machine")).toObject();
    out() << "machine " << machine.value(QStringLiteral("id")).toString() << " clonada como "
          << machine.value(QStringLiteral("name")).toString() << " ("
          << machine.value(QStringLiteral("state")).toString() << ")\n";
    out().flush();
    return kExitOk;
}

int machineUpdateCommand(const QStringList &args)
{
    QString id;
    int cpus = 0;
    qint64 memoryBytes = 0;
    qint64 diskBytes = 0;
    bool hasCpus = false;
    bool hasMemory = false;
    bool hasDisk = false;
    bool hasForwardAgent = false;
    bool forwardAgent = false;
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (arg == QStringLiteral("--cpus") && i + 1 < args.size()) {
            bool ok = false;
            cpus = args.at(++i).toInt(&ok);
            if (!ok) {
                err() << "--cpus exige um número inteiro\n";
                return kExitUsage;
            }
            hasCpus = true;
        } else if (arg == QStringLiteral("--memory") && i + 1 < args.size()) {
            if (!parseSize(args.at(++i), &memoryBytes)) {
                err() << "--memory inválido; use bytes ou sufixo K/M/G/T (ex.: 4G)\n";
                return kExitUsage;
            }
            hasMemory = true;
        } else if (arg == QStringLiteral("--disk") && i + 1 < args.size()) {
            if (!parseSize(args.at(++i), &diskBytes)) {
                err() << "--disk inválido; use bytes ou sufixo K/M/G/T (ex.: 40G)\n";
                return kExitUsage;
            }
            hasDisk = true;
        } else if (arg == QStringLiteral("--forward-agent") ||
                   arg == QStringLiteral("--no-forward-agent")) {
            const bool requested = arg == QStringLiteral("--forward-agent");
            if (hasForwardAgent && forwardAgent != requested) {
                err() << "use apenas um de --forward-agent/--no-forward-agent\n";
                return kExitUsage;
            }
            hasForwardAgent = true;
            forwardAgent = requested;
        } else if (!arg.startsWith(QLatin1Char('-')) && id.isEmpty()) {
            id = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }

    if (id.isEmpty() || (!hasCpus && !hasMemory && !hasDisk && !hasForwardAgent)) {
        printMachineUsage(err());
        return kExitUsage;
    }

    QJsonObject params;
    params.insert(QStringLiteral("id"), id);
    if (hasCpus) {
        params.insert(QStringLiteral("cpus"), cpus);
    }
    if (hasMemory) {
        params.insert(QStringLiteral("memoryBytes"), memoryBytes);
    }
    if (hasDisk) {
        params.insert(QStringLiteral("diskBytes"), diskBytes);
    }
    if (hasForwardAgent) {
        params.insert(QStringLiteral("forwardSshAgent"), forwardAgent);
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result =
        client.call(QStringLiteral("machines.update"), params, &error, 180000);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    const QString name = result.value(QStringLiteral("name")).toString();
    const QString display = name.isEmpty() ? result.value(QStringLiteral("id")).toString() : name;
    out() << "machine " << display
          << " atualizada: " << result.value(QStringLiteral("cpus")).toInt() << " CPUs, "
          << formatSize(result.value(QStringLiteral("memoryBytes")).toInteger()) << " RAM, "
          << formatSize(result.value(QStringLiteral("diskBytes")).toInteger()) << " disco ("
          << result.value(QStringLiteral("state")).toString() << ")\n";
    out().flush();
    return kExitOk;
}

int machinePortListCommand(const QStringList &args)
{
    QString id;
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (!arg.startsWith(QLatin1Char('-')) && id.isEmpty()) {
            id = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (id.isEmpty()) {
        printMachineUsage(err());
        return kExitUsage;
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result = client.call(QStringLiteral("machines.ports.list"),
                                           QJsonObject{{QStringLiteral("id"), id}}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    const QJsonArray ports = result.value(QStringLiteral("items")).toArray();
    int idWidth = 2;
    int hostWidth = 4;
    int guestWidth = 5;
    int protoWidth = 5;
    int bindWidth = 9;
    int labelWidth = 5;
    for (const QJsonValue &value : ports) {
        const QJsonObject port = value.toObject();
        idWidth = qMax(idWidth, port.value(QStringLiteral("id")).toString().size());
        hostWidth =
            qMax(hostWidth, QString::number(port.value(QStringLiteral("hostPort")).toInt()).size());
        guestWidth = qMax(guestWidth,
                          QString::number(port.value(QStringLiteral("guestPort")).toInt()).size());
        protoWidth = qMax(protoWidth, port.value(QStringLiteral("protocol")).toString().size());
        bindWidth = qMax(bindWidth, (port.value(QStringLiteral("exposeLan")).toBool()
                                         ? QStringLiteral("0.0.0.0 (LAN)")
                                         : QStringLiteral("127.0.0.1"))
                                        .size());
        labelWidth = qMax(labelWidth, port.value(QStringLiteral("label")).toString().size());
    }

    out() << QStringLiteral("%1  %2  %3  %4  %5  %6\n")
                 .arg(QStringLiteral("ID").leftJustified(idWidth),
                      QStringLiteral("HOST").leftJustified(hostWidth),
                      QStringLiteral("GUEST").leftJustified(guestWidth),
                      QStringLiteral("PROTO").leftJustified(protoWidth),
                      QStringLiteral("BIND").leftJustified(bindWidth), QStringLiteral("LABEL"));
    for (const QJsonValue &value : ports) {
        const QJsonObject port = value.toObject();
        out()
            << QStringLiteral("%1  %2  %3  %4  %5  %6\n")
                   .arg(port.value(QStringLiteral("id")).toString().leftJustified(idWidth),
                        QString::number(port.value(QStringLiteral("hostPort")).toInt())
                            .leftJustified(hostWidth),
                        QString::number(port.value(QStringLiteral("guestPort")).toInt())
                            .leftJustified(guestWidth),
                        port.value(QStringLiteral("protocol")).toString().leftJustified(protoWidth),
                        (port.value(QStringLiteral("exposeLan")).toBool()
                             ? QStringLiteral("0.0.0.0 (LAN)")
                             : QStringLiteral("127.0.0.1"))
                            .leftJustified(bindWidth),
                        port.value(QStringLiteral("label")).toString());
    }
    out().flush();
    return kExitOk;
}

int machinePortAddCommand(const QStringList &args)
{
    QString id;
    QString label;
    int hostPort = 0;
    int guestPort = 0;
    bool hasHost = false;
    bool hasGuest = false;
    bool udp = false;
    bool exposeLan = false;
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (arg == QStringLiteral("--host") && i + 1 < args.size()) {
            bool ok = false;
            hostPort = args.at(++i).toInt(&ok);
            if (!ok || hostPort < 1 || hostPort > 65535) {
                err() << "--host exige uma porta 1..65535\n";
                return kExitUsage;
            }
            hasHost = true;
        } else if (arg == QStringLiteral("--guest") && i + 1 < args.size()) {
            bool ok = false;
            guestPort = args.at(++i).toInt(&ok);
            if (!ok || guestPort < 1 || guestPort > 65535) {
                err() << "--guest exige uma porta 1..65535\n";
                return kExitUsage;
            }
            hasGuest = true;
        } else if (arg == QStringLiteral("--udp")) {
            udp = true;
        } else if (arg == QStringLiteral("--lan")) {
            exposeLan = true;
        } else if (arg == QStringLiteral("--label") && i + 1 < args.size()) {
            label = args.at(++i);
        } else if (!arg.startsWith(QLatin1Char('-')) && id.isEmpty()) {
            id = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (id.isEmpty() || !hasHost || !hasGuest) {
        printMachineUsage(err());
        return kExitUsage;
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject listed = client.call(QStringLiteral("machines.ports.list"),
                                           QJsonObject{{QStringLiteral("id"), id}}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    QJsonArray ports = machinePortsWithoutSsh(listed.value(QStringLiteral("items")).toArray());
    QJsonObject port;
    port.insert(QStringLiteral("hostPort"), hostPort);
    port.insert(QStringLiteral("guestPort"), guestPort);
    port.insert(QStringLiteral("protocol"), udp ? QStringLiteral("udp") : QStringLiteral("tcp"));
    port.insert(QStringLiteral("exposeLan"), exposeLan);
    port.insert(QStringLiteral("label"), label);
    ports.append(port);

    const QJsonObject result = client.call(
        QStringLiteral("machines.ports.set"),
        QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("ports"), ports}}, &error, 180000);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    out() << "porta " << hostPort << " → " << guestPort << '/'
          << (udp ? QStringLiteral("udp") : QStringLiteral("tcp")) << " adicionada à machine "
          << machinePortLabel(result) << " (" << result.value(QStringLiteral("state")).toString()
          << ")\n";
    out().flush();
    return kExitOk;
}

int machinePortRemoveCommand(const QStringList &args)
{
    QString id;
    QString target;
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (!arg.startsWith(QLatin1Char('-')) && id.isEmpty()) {
            id = arg;
        } else if (!arg.startsWith(QLatin1Char('-')) && target.isEmpty()) {
            target = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (id.isEmpty() || target.isEmpty()) {
        printMachineUsage(err());
        return kExitUsage;
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject listed = client.call(QStringLiteral("machines.ports.list"),
                                           QJsonObject{{QStringLiteral("id"), id}}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    const QJsonArray ports = listed.value(QStringLiteral("items")).toArray();
    bool numeric = false;
    const int hostPort = target.toInt(&numeric);
    for (int index = 0; index < ports.size(); ++index) {
        const QJsonObject port = ports.at(index).toObject();
        if (!machinePortIsSsh(port)) {
            continue;
        }
        if (port.value(QStringLiteral("id")).toString() == target ||
            (numeric && port.value(QStringLiteral("hostPort")).toInt() == hostPort)) {
            return fail(PodStack::Error::make(
                QStringLiteral("PODSTACK_INVALID_PARAMS"),
                QStringLiteral("A porta SSH é gerenciada pela machine e não pode ser removida."),
                target, QStringLiteral("Use o SSH da machine para acesso remoto.")));
        }
    }

    int removeIndex = -1;
    for (int index = 0; index < ports.size(); ++index) {
        const QJsonObject port = ports.at(index).toObject();
        if (machinePortIsSsh(port)) {
            continue;
        }
        if (port.value(QStringLiteral("id")).toString() == target) {
            removeIndex = index;
            break;
        }
    }
    if (removeIndex < 0 && numeric) {
        for (int index = 0; index < ports.size(); ++index) {
            const QJsonObject port = ports.at(index).toObject();
            if (machinePortIsSsh(port)) {
                continue;
            }
            if (port.value(QStringLiteral("hostPort")).toInt() != hostPort) {
                continue;
            }
            if (removeIndex >= 0) {
                return fail(PodStack::Error::make(
                    QStringLiteral("PODSTACK_INVALID_PARAMS"),
                    QStringLiteral("Mais de uma porta usa esse hostPort; use o ID."), target));
            }
            removeIndex = index;
        }
    }
    if (removeIndex < 0) {
        return fail(PodStack::Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                          QStringLiteral("Porta não encontrada na machine."),
                                          target));
    }

    QJsonArray remaining;
    for (int index = 0; index < ports.size(); ++index) {
        if (index != removeIndex && !machinePortIsSsh(ports.at(index).toObject())) {
            remaining.append(ports.at(index));
        }
    }

    const QJsonObject result =
        client.call(QStringLiteral("machines.ports.set"),
                    QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("ports"), remaining}},
                    &error, 180000);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    out() << "porta " << target << " removida da machine " << machinePortLabel(result) << " ("
          << result.value(QStringLiteral("state")).toString() << ")\n";
    out().flush();
    return kExitOk;
}

int machinePortCommand(const QStringList &args)
{
    if (args.isEmpty() || args.first() == QStringLiteral("--help") ||
        args.first() == QStringLiteral("-h") || args.first() == QStringLiteral("help")) {
        printMachineUsage(args.isEmpty() ? err() : out());
        return args.isEmpty() ? kExitUsage : kExitOk;
    }
    const QString &action = args.first();
    if (action == QStringLiteral("ls") || action == QStringLiteral("list")) {
        return machinePortListCommand(args);
    }
    if (action == QStringLiteral("add")) {
        return machinePortAddCommand(args);
    }
    if (action == QStringLiteral("rm") || action == QStringLiteral("remove")) {
        return machinePortRemoveCommand(args);
    }
    printMachineUsage(err());
    return kExitUsage;
}

int machineSerialCommand(const QStringList &args)
{
    if (args.isEmpty() || args.first() == QStringLiteral("--help") ||
        args.first() == QStringLiteral("-h") || args.first() == QStringLiteral("help")) {
        printMachineUsage(args.isEmpty() ? err() : out());
        return args.isEmpty() ? kExitUsage : kExitOk;
    }
    const QString &action = args.first();
    if (action != QStringLiteral("list") && action != QStringLiteral("ls") &&
        action != QStringLiteral("attach") && action != QStringLiteral("detach")) {
        printMachineUsage(err());
        return kExitUsage;
    }

    QString id;
    QString path;
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (!arg.startsWith(QLatin1Char('-')) && id.isEmpty()) {
            id = arg;
        } else if (!arg.startsWith(QLatin1Char('-')) && path.isEmpty()) {
            path = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (id.isEmpty() ||
        (action != QStringLiteral("list") && action != QStringLiteral("ls") && path.isEmpty())) {
        printMachineUsage(err());
        return kExitUsage;
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    QJsonObject params;
    params.insert(QStringLiteral("id"), id);

    if (action == QStringLiteral("list") || action == QStringLiteral("ls")) {
        const QJsonObject result =
            client.call(QStringLiteral("machines.serial.list"), params, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        if (json) {
            out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
            out().flush();
            return kExitOk;
        }
        const QJsonArray attached = result.value(QStringLiteral("machine"))
                                        .toObject()
                                        .value(QStringLiteral("serialPorts"))
                                        .toArray();
        QStringList attachedList;
        for (const QJsonValue &value : attached) {
            attachedList.append(value.toString());
        }
        out() << QStringLiteral("%1  %2  %3  %4\n")
                     .arg(QStringLiteral("PORTA").leftJustified(20),
                          QStringLiteral("TIPO").leftJustified(6),
                          QStringLiteral("LEITURA").leftJustified(8), QStringLiteral("ANEXADA"));
        for (const QJsonValue &value : result.value(QStringLiteral("ports")).toArray()) {
            const QJsonObject port = value.toObject();
            const QString pathValue = port.value(QStringLiteral("path")).toString();
            out() << QStringLiteral("%1  %2  %3  %4\n")
                         .arg(pathValue.leftJustified(20),
                              port.value(QStringLiteral("kind")).toString().leftJustified(6),
                              port.value(QStringLiteral("readable")).toBool()
                                  ? QStringLiteral("sim").leftJustified(8)
                                  : QStringLiteral("não").leftJustified(8),
                              attachedList.contains(pathValue) ? QStringLiteral("sim")
                                                               : QStringLiteral("não"));
        }
        out().flush();
        return kExitOk;
    }

    params.insert(QStringLiteral("path"), path);
    const QString method = action == QStringLiteral("attach")
                               ? QStringLiteral("machines.serial.attach")
                               : QStringLiteral("machines.serial.detach");
    const QJsonObject result = client.call(method, params, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }
    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }
    out() << (action == QStringLiteral("attach") ? "Porta anexada: " : "Porta desanexada: ") << path
          << '\n';
    out().flush();
    return kExitOk;
}

int machineAudioCommand(const QStringList &args)
{
    if (args.isEmpty()) {
        err() << "Uso: podstack machine audio <id|nome> on|off [--json]\n";
        return kExitUsage;
    }
    QString id;
    QString state;
    bool json = false;
    for (const QString &arg : args) {
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (!arg.startsWith(QLatin1Char('-')) && id.isEmpty()) {
            id = arg;
        } else if (!arg.startsWith(QLatin1Char('-')) && state.isEmpty()) {
            state = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (id.isEmpty() || (state != QStringLiteral("on") && state != QStringLiteral("off"))) {
        err() << "Uso: podstack machine audio <id|nome> on|off [--json]\n";
        return kExitUsage;
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result =
        client.call(QStringLiteral("machines.audio.set"),
                    QJsonObject{{QStringLiteral("id"), id},
                                {QStringLiteral("enabled"), state == QStringLiteral("on")}},
                    &error);
    if (!error.isEmpty()) {
        return fail(error);
    }
    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }
    out() << QStringLiteral("Áudio %1: %2\n")
                 .arg(state == QStringLiteral("on") ? QStringLiteral("ativado")
                                                    : QStringLiteral("desativado"),
                      result.value(QStringLiteral("name")).toString());
    out().flush();
    return kExitOk;
}

int machineUsbCommand(const QStringList &args)
{
    if (args.isEmpty() || args.first() == QStringLiteral("--help") ||
        args.first() == QStringLiteral("-h") || args.first() == QStringLiteral("help")) {
        printMachineUsage(args.isEmpty() ? err() : out());
        return args.isEmpty() ? kExitUsage : kExitOk;
    }
    const QString &action = args.first();
    if (action != QStringLiteral("list") && action != QStringLiteral("ls") &&
        action != QStringLiteral("attach") && action != QStringLiteral("detach")) {
        printMachineUsage(err());
        return kExitUsage;
    }

    QString id;
    QString device;
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (!arg.startsWith(QLatin1Char('-')) && id.isEmpty()) {
            id = arg;
        } else if (!arg.startsWith(QLatin1Char('-')) && device.isEmpty()) {
            device = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (id.isEmpty() ||
        (action != QStringLiteral("list") && action != QStringLiteral("ls") && device.isEmpty())) {
        printMachineUsage(err());
        return kExitUsage;
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    QJsonObject params;
    params.insert(QStringLiteral("id"), id);

    if (action == QStringLiteral("list") || action == QStringLiteral("ls")) {
        const QJsonObject result = client.call(QStringLiteral("machines.usb.list"), params, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        if (json) {
            out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
            out().flush();
            return kExitOk;
        }
        QSet<QString> attached;
        for (const QJsonValue &value : result.value(QStringLiteral("machine"))
                                           .toObject()
                                           .value(QStringLiteral("usbDevices"))
                                           .toArray()) {
            const QJsonObject deviceJson = value.toObject();
            attached.insert(QStringLiteral("%1:%2")
                                .arg(deviceJson.value(QStringLiteral("bus")).toInt())
                                .arg(deviceJson.value(QStringLiteral("address")).toInt()));
        }
        out() << QStringLiteral("%1  %2  %3  %4\n")
                     .arg(QStringLiteral("BUS:ADDR").leftJustified(10),
                          QStringLiteral("VID:PID").leftJustified(11),
                          QStringLiteral("DESCRIÇÃO").leftJustified(32), QStringLiteral("ANEXADO"));
        for (const QJsonValue &value : result.value(QStringLiteral("devices")).toArray()) {
            const QJsonObject deviceJson = value.toObject();
            const QString key = QStringLiteral("%1:%2")
                                    .arg(deviceJson.value(QStringLiteral("bus")).toInt())
                                    .arg(deviceJson.value(QStringLiteral("address")).toInt());
            out() << QStringLiteral("%1  %2  %3  %4\n")
                         .arg(key.leftJustified(10),
                              (deviceJson.value(QStringLiteral("vendorId")).toString() +
                               QLatin1Char(':') +
                               deviceJson.value(QStringLiteral("productId")).toString())
                                  .leftJustified(11),
                              deviceJson.value(QStringLiteral("description"))
                                  .toString()
                                  .leftJustified(32),
                              attached.contains(key) ? QStringLiteral("sim")
                                                     : QStringLiteral("não"));
        }
        out().flush();
        return kExitOk;
    }

    const QStringList parts = device.split(QLatin1Char(':'));
    bool busOk = false;
    bool addressOk = false;
    const int bus = parts.size() == 2 ? parts.at(0).toInt(&busOk) : 0;
    const int address = parts.size() == 2 ? parts.at(1).toInt(&addressOk) : 0;
    if (!busOk || !addressOk || bus <= 0 || address <= 0) {
        err() << "Informe o dispositivo como BUS:ADDR (ex.: 1:4).\n";
        return kExitUsage;
    }
    params.insert(QStringLiteral("bus"), bus);
    params.insert(QStringLiteral("address"), address);
    const QString method = action == QStringLiteral("attach")
                               ? QStringLiteral("machines.usb.attach")
                               : QStringLiteral("machines.usb.detach");
    const QJsonObject result = client.call(method, params, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }
    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }
    out() << (action == QStringLiteral("attach") ? "Dispositivo USB anexado: "
                                                 : "Dispositivo USB desanexado: ")
          << device << '\n';
    out().flush();
    return kExitOk;
}

int machineSnapshotListCommand(const QStringList &args)
{
    QString id;
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (!arg.startsWith(QLatin1Char('-')) && id.isEmpty()) {
            id = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (id.isEmpty()) {
        printMachineUsage(err());
        return kExitUsage;
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result = client.call(QStringLiteral("machines.snapshots.list"),
                                           QJsonObject{{QStringLiteral("id"), id}}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    const QJsonArray items = result.value(QStringLiteral("items")).toArray();
    out() << QStringLiteral("%1  %2  %3\n")
                 .arg(QStringLiteral("NOME").leftJustified(24),
                      QStringLiteral("DATA").leftJustified(24), QStringLiteral("TAMANHO"));
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const qint64 sizeBytes =
            static_cast<qint64>(item.value(QStringLiteral("sizeBytes")).toDouble());
        out() << QStringLiteral("%1  %2  %3\n")
                     .arg(item.value(QStringLiteral("name")).toString().leftJustified(24),
                          item.value(QStringLiteral("date")).toString().leftJustified(24),
                          sizeBytes > 0 ? humanBytes(sizeBytes) : QStringLiteral("-"));
    }
    if (items.isEmpty()) {
        out() << "  (nenhum snapshot)\n";
    }
    out().flush();
    return kExitOk;
}

int machineSnapshotCreateCommand(const QStringList &args)
{
    QString id;
    QString snapshotName;
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (!arg.startsWith(QLatin1Char('-')) && id.isEmpty()) {
            id = arg;
        } else if (!arg.startsWith(QLatin1Char('-')) && snapshotName.isEmpty()) {
            snapshotName = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (id.isEmpty() || snapshotName.isEmpty()) {
        printMachineUsage(err());
        return kExitUsage;
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result =
        client.call(QStringLiteral("machines.snapshots.create"),
                    QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("name"), snapshotName}},
                    &error, 180000);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    out() << "snapshot " << result.value(QStringLiteral("name")).toString() << " criado na machine "
          << id << '\n';
    out().flush();
    return kExitOk;
}

int machineSnapshotRestoreCommand(const QStringList &args)
{
    QString id;
    QString snapshotName;
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (!arg.startsWith(QLatin1Char('-')) && id.isEmpty()) {
            id = arg;
        } else if (!arg.startsWith(QLatin1Char('-')) && snapshotName.isEmpty()) {
            snapshotName = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (id.isEmpty() || snapshotName.isEmpty()) {
        printMachineUsage(err());
        return kExitUsage;
    }

    err() << "aviso: restaurar o snapshot " << snapshotName << " substitui o disco atual da "
          << "machine " << id << "; alterações não salvas serão perdidas.\n";
    err().flush();

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result =
        client.call(QStringLiteral("machines.snapshots.restore"),
                    QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("name"), snapshotName}},
                    &error, 180000);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    out() << "snapshot " << snapshotName << " restaurado na machine "
          << result.value(QStringLiteral("name")).toString() << " ("
          << result.value(QStringLiteral("state")).toString() << ")\n";
    out().flush();
    return kExitOk;
}

int machineSnapshotRemoveCommand(const QStringList &args)
{
    QString id;
    QString snapshotName;
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (!arg.startsWith(QLatin1Char('-')) && id.isEmpty()) {
            id = arg;
        } else if (!arg.startsWith(QLatin1Char('-')) && snapshotName.isEmpty()) {
            snapshotName = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (id.isEmpty() || snapshotName.isEmpty()) {
        printMachineUsage(err());
        return kExitUsage;
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result =
        client.call(QStringLiteral("machines.snapshots.remove"),
                    QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("name"), snapshotName}},
                    &error, 180000);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    out() << "snapshot " << snapshotName << " removido da machine " << id << '\n';
    out().flush();
    return kExitOk;
}

int machineSnapshotCommand(const QStringList &args)
{
    if (args.isEmpty() || args.first() == QStringLiteral("--help") ||
        args.first() == QStringLiteral("-h") || args.first() == QStringLiteral("help")) {
        printMachineUsage(args.isEmpty() ? err() : out());
        return args.isEmpty() ? kExitUsage : kExitOk;
    }
    const QString &action = args.first();
    if (action == QStringLiteral("ls") || action == QStringLiteral("list")) {
        return machineSnapshotListCommand(args);
    }
    if (action == QStringLiteral("create")) {
        return machineSnapshotCreateCommand(args);
    }
    if (action == QStringLiteral("restore")) {
        return machineSnapshotRestoreCommand(args);
    }
    if (action == QStringLiteral("rm") || action == QStringLiteral("remove")) {
        return machineSnapshotRemoveCommand(args);
    }
    printMachineUsage(err());
    return kExitUsage;
}

int machineDefaultCommand(const QStringList &args)
{
    QString name;
    bool hasName = false;
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (!arg.startsWith(QLatin1Char('-')) && !hasName) {
            name = arg;
            hasName = true;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }

    QJsonObject params;
    if (hasName) {
        params.insert(QStringLiteral("name"), name);
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result = client.call(QStringLiteral("machines.default"), params, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    const QString resolved = result.value(QStringLiteral("name")).toString();
    if (resolved.isEmpty()) {
        out() << "nenhuma machine default definida (use: podstack machine default <nome>)\n";
    } else {
        out() << "machine default: " << resolved << '\n';
    }
    out().flush();
    return kExitOk;
}

int machineSshCommand(const QStringList &args)
{
    QString id;
    QStringList command;
    QString x11Flag;
    for (int i = 0; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--")) {
            command = args.mid(i + 1);
            break;
        }
        if (arg == QStringLiteral("--help") || arg == QStringLiteral("-h")) {
            printMachineUsage(out());
            return kExitOk;
        }
        if (arg == QStringLiteral("--x11") || arg == QStringLiteral("-X")) {
            x11Flag = QStringLiteral("-X");
        } else if (arg == QStringLiteral("--x11-trusted") || arg == QStringLiteral("-Y")) {
            x11Flag = QStringLiteral("-Y");
        } else if (id.isEmpty() && !arg.startsWith(QLatin1Char('-'))) {
            id = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    PodStack::RpcClient client;
    PodStack::Error error;
    if (id.isEmpty()) {
        const QJsonObject defaultResult =
            client.call(QStringLiteral("machines.default"), {}, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        id = defaultResult.value(QStringLiteral("name")).toString();
        if (id.isEmpty()) {
            return fail(PodStack::Error::make(
                QStringLiteral("PODSTACK_NOT_FOUND"),
                QStringLiteral("Nenhuma machine default definida."), {},
                QStringLiteral("Defina a machine default com: podstack machine default <nome>.")));
        }
    }

    const QJsonObject result = client.call(QStringLiteral("machines.ssh"),
                                           QJsonObject{{QStringLiteral("id"), id}}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    const QString host = result.value(QStringLiteral("host")).toString();
    const int port = result.value(QStringLiteral("port")).toInt();
    const QString user = result.value(QStringLiteral("user")).toString();
    const QString identityFile = result.value(QStringLiteral("identityFile")).toString();
    const bool forwardAgent = result.value(QStringLiteral("forwardAgent")).toBool();
    if (host.isEmpty() || port <= 0 || user.isEmpty() || identityFile.isEmpty()) {
        err() << "resposta SSH inválida do daemon\n";
        err().flush();
        return kExitError;
    }

    QStringList sshArguments;
    if (!x11Flag.isEmpty()) {
        sshArguments << x11Flag;
    }
    if (forwardAgent) {
        sshArguments << QStringLiteral("-A");
    }
    sshArguments << QStringLiteral("-i") << identityFile << QStringLiteral("-p")
                 << QString::number(port) << QStringLiteral("-o")
                 << QStringLiteral("IdentitiesOnly=yes") << QStringLiteral("-o")
                 << QStringLiteral("StrictHostKeyChecking=accept-new") << QStringLiteral("-o")
                 << QStringLiteral("UserKnownHostsFile=%1/known_hosts")
                        .arg(QFileInfo(identityFile).absolutePath())
                 << QStringLiteral("%1@%2").arg(user, host);
    sshArguments += command;

    std::vector<std::string> storage;
    storage.reserve(static_cast<size_t>(sshArguments.size()) + 2);
    storage.emplace_back("ssh");
    for (const QString &argument : sshArguments) {
        storage.emplace_back(argument.toStdString());
    }

    std::vector<char *> argv;
    argv.reserve(storage.size() + 1);
    for (std::string &argument : storage) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    execvp("ssh", argv.data());
    const int savedErrno = errno;
    err() << "não foi possível executar o ssh: "
          << QString::fromLocal8Bit(std::strerror(savedErrno)) << '\n';
    err().flush();
    return kExitError;
}

int machineFilesCommand(const QStringList &args)
{
    if (args.size() < 2 || args.at(1) == QStringLiteral("--help") ||
        args.at(1) == QStringLiteral("-h") || args.at(1) == QStringLiteral("help")) {
        printMachineUsage(args.size() < 2 ? err() : out());
        return args.size() < 2 ? kExitUsage : kExitOk;
    }

    const QString &subcommand = args.at(1);
    if (subcommand != QStringLiteral("ls") && subcommand != QStringLiteral("list")) {
        err() << "subcomando desconhecido: machine files " << subcommand << '\n';
        printMachineUsage(err());
        return kExitUsage;
    }

    QString id;
    QString path = QStringLiteral("/");
    bool json = false;
    for (int i = 2; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (arg == QStringLiteral("--path")) {
            if (i + 1 >= args.size()) {
                err() << "faltou o valor de --path\n";
                return kExitUsage;
            }
            path = args.at(++i);
        } else if (arg == QStringLiteral("--help") || arg == QStringLiteral("-h")) {
            printMachineUsage(out());
            return kExitOk;
        } else if (arg.startsWith(QLatin1Char('-'))) {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        } else if (id.isEmpty()) {
            id = arg;
        } else {
            err() << "argumento inesperado: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (id.isEmpty()) {
        printMachineUsage(err());
        return kExitUsage;
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result = client.call(
        QStringLiteral("machines.files.list"),
        QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("path"), path}}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    const QString resolvedPath = result.value(QStringLiteral("path")).toString(path);
    out() << "caminho: " << resolvedPath << '\n';
    out() << QStringLiteral("%1 %2 %3 %4\n")
                 .arg(QStringLiteral("TIPO").leftJustified(8),
                      QStringLiteral("TAMANHO").leftJustified(10),
                      QStringLiteral("MODIFICADO").leftJustified(17), QStringLiteral("NOME"));
    const QJsonArray items = result.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const bool directory = item.value(QStringLiteral("directory")).toBool();
        out() << QStringLiteral("%1 %2 %3 %4\n")
                     .arg((directory ? QStringLiteral("dir") : QStringLiteral("arquivo"))
                              .leftJustified(8),
                          (directory ? QStringLiteral("-")
                                     : humanBytes(item.value(QStringLiteral("size")).toInteger()))
                              .leftJustified(10),
                          item.value(QStringLiteral("modified")).toString().leftJustified(17),
                          item.value(QStringLiteral("name")).toString());
    }
    out().flush();
    return kExitOk;
}

int machineTransferCommand(const QString &direction, const QStringList &args)
{
    QStringList positional;
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (arg == QStringLiteral("--help") || arg == QStringLiteral("-h")) {
            printMachineUsage(out());
            return kExitOk;
        } else if (arg.startsWith(QLatin1Char('-'))) {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        } else {
            positional.append(arg);
        }
    }
    if (positional.size() != 3) {
        printMachineUsage(err());
        return kExitUsage;
    }

    const QString id = positional.at(0);
    const bool pull = direction == QStringLiteral("pull");
    const QString remote = pull ? positional.at(1) : positional.at(2);
    QString local = QDir::cleanPath(QFileInfo(positional.at(pull ? 2 : 1)).absoluteFilePath());
    if (pull && QFileInfo(local).isDir()) {
        const QString name = QFileInfo(remote).fileName();
        if (!name.isEmpty()) {
            local += QLatin1Char('/') + name;
        }
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    QJsonObject result;
    if (pull) {
        result = client.call(QStringLiteral("machines.files.download"),
                             QJsonObject{{QStringLiteral("id"), id},
                                         {QStringLiteral("path"), remote},
                                         {QStringLiteral("destination"), local}},
                             &error, 600000);
    } else {
        result = client.call(QStringLiteral("machines.files.upload"),
                             QJsonObject{{QStringLiteral("id"), id},
                                         {QStringLiteral("path"), remote},
                                         {QStringLiteral("sourcePath"), local}},
                             &error, 600000);
    }
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    if (pull) {
        out() << "baixado " << result.value(QStringLiteral("path")).toString(remote) << " -> "
              << result.value(QStringLiteral("destination")).toString(local) << '\n';
    } else {
        out() << "enviado " << result.value(QStringLiteral("sourcePath")).toString(local) << " -> "
              << result.value(QStringLiteral("path")).toString(remote) << '\n';
    }
    out().flush();
    return kExitOk;
}

} // namespace PodStack::CliInternal
