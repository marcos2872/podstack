// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/Internal.h"

namespace PodStack::CliInternal {



int commandPs(const QStringList &args)
{
    bool all = false;
    bool quiet = false;
    bool json = false;
    bool running = false;
    bool stopped = false;

    for (int i = 0; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--all") || arg == QStringLiteral("-a")) {
            all = true;
        } else if (arg == QStringLiteral("--running") || arg == QStringLiteral("-r")) {
            running = true;
        } else if (arg == QStringLiteral("--stopped") || arg == QStringLiteral("-s")) {
            stopped = true;
        } else if (arg == QStringLiteral("--quiet") || arg == QStringLiteral("-q")) {
            quiet = true;
        } else if (arg == QStringLiteral("--format")) {
            if (i + 1 >= args.size()) {
                err() << "--format exige um valor (table|json)\n";
                return kExitUsage;
            }
            json = args.at(++i) == QStringLiteral("json");
        } else if (arg.startsWith(QStringLiteral("--format="))) {
            json = arg.mid(9) == QStringLiteral("json");
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }

    if (running && stopped) {
        err() << "--running e --stopped são mutuamente exclusivos\n";
        return kExitUsage;
    }
    if (running || stopped) {
        all = true;
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    QJsonObject params;
    params.insert(QStringLiteral("all"), all);
    params.insert(QStringLiteral("state"), running ? QStringLiteral("running")
                                     : stopped ? QStringLiteral("stopped")
                                               : QString());

    const QJsonObject result = client.call(QStringLiteral("containers.list"), params, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        QJsonObject payload;
        payload.insert(QStringLiteral("api"), 1);
        payload.insert(QStringLiteral("items"), result.value(QStringLiteral("items")));
        out() << QJsonDocument(payload).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    const QJsonArray items = result.value(QStringLiteral("items")).toArray();
    if (quiet) {
        for (const QJsonValue &value : items) {
            out() << value.toObject().value(QStringLiteral("name")).toString() << '\n';
        }
        out().flush();
        return kExitOk;
    }

    if (items.isEmpty()) {
        out() << "Nenhum container encontrado.\n";
        out().flush();
        return kExitOk;
    }

    int nameWidth = 4;
    int stateWidth = 5;
    int imageWidth = 5;

    struct Row {
        QString name;
        QString state;
        QString image;
        QString ports;
    };
    QVector<Row> rows;
    rows.reserve(items.size());

    for (const QJsonValue &value : items) {
        const QJsonObject object = value.toObject();
        Row row;
        row.name = object.value(QStringLiteral("name")).toString();
        row.state = object.value(QStringLiteral("state")).toString();
        row.image = object.value(QStringLiteral("image")).toString();

        QStringList ports;
        const QJsonArray portArray = object.value(QStringLiteral("ports")).toArray();
        for (const QJsonValue &port : portArray) {
            ports.append(port.toString());
        }
        row.ports = ports.join(QStringLiteral(", "));

        nameWidth = qMax(nameWidth, row.name.size());
        stateWidth = qMax(stateWidth, row.state.size());
        imageWidth = qMax(imageWidth, row.image.size());
        rows.append(row);
    }

    out() << QStringLiteral("%1  %2  %3  %4\n")
                 .arg(QStringLiteral("NAME").leftJustified(nameWidth))
                 .arg(QStringLiteral("STATE").leftJustified(stateWidth))
                 .arg(QStringLiteral("IMAGE").leftJustified(imageWidth))
                 .arg(QStringLiteral("PORTS"));
    for (const Row &row : rows) {
        out() << QStringLiteral("%1  %2  %3  %4\n")
                     .arg(row.name.leftJustified(nameWidth))
                     .arg(row.state.leftJustified(stateWidth))
                     .arg(row.image.leftJustified(imageWidth))
                     .arg(row.ports);
    }
    out().flush();
    return kExitOk;
}



int commandLifecycle(const QString &action, const QStringList &args)
{
    QString id;
    QString signal;
    for (int i = 0; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--signal")) {
            if (i + 1 >= args.size()) {
                err() << "--signal exige um valor\n";
                return kExitUsage;
            }
            signal = args.at(++i);
        } else if (id.isEmpty()) {
            id = arg;
        } else {
            err() << "argumento inesperado: " << arg << '\n';
            return kExitUsage;
        }
    }

    if (id.isEmpty()) {
        err() << "Uso: podstack " << action << " <id|nome>\n";
        return kExitUsage;
    }

    QJsonObject params;
    params.insert(QStringLiteral("id"), id);
    if (!signal.isEmpty()) {
        params.insert(QStringLiteral("signal"), signal);
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    client.call(QStringLiteral("containers.") + action, params, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    out() << action << ' ' << id << ": ok\n";
    out().flush();
    return kExitOk;
}



int commandInspect(const QStringList &args)
{
    if (args.size() != 1) {
        err() << "Uso: podstack inspect <id|nome>\n";
        return kExitUsage;
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result =
        client.call(QStringLiteral("containers.inspect"),
                    QJsonObject{{QStringLiteral("id"), args.first()}}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    const QJsonObject json = result.value(QStringLiteral("json")).toObject();
    out() << QJsonDocument(json.isEmpty() ? result : json).toJson(QJsonDocument::Indented);
    out().flush();
    return kExitOk;
}



int commandExec(const QStringList &args)
{
    QString id;
    QStringList command;
    bool afterSeparator = false;

    for (const QString &arg : args) {
        if (!afterSeparator && arg == QStringLiteral("--")) {
            afterSeparator = true;
            continue;
        }
        if (id.isEmpty() && !afterSeparator) {
            id = arg;
            continue;
        }
        command.append(arg);
    }

    if (id.isEmpty() || command.isEmpty()) {
        err() << "Uso: podstack exec <id|nome> -- <comando...>\n";
        return kExitUsage;
    }

    QJsonArray commandArray;
    for (const QString &part : command) {
        commandArray.append(part);
    }

    QJsonObject params;
    params.insert(QStringLiteral("id"), id);
    params.insert(QStringLiteral("cmd"), commandArray);

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result = client.call(QStringLiteral("containers.exec"), params, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    out() << result.value(QStringLiteral("stdout")).toString();
    out().flush();

    const int exitCode = result.value(QStringLiteral("exitCode")).toInt(kExitError);
    return (exitCode >= 0 && exitCode <= 255) ? exitCode : kExitError;
}



int commandCheckpoint(const QStringList &args)
{
    QString id;
    bool json = false;
    bool leaveRunning = false;
    bool tcpEstablished = false;
    for (const QString &arg : args) {
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (arg == QStringLiteral("--leave-running")) {
            leaveRunning = true;
        } else if (arg == QStringLiteral("--tcp-established")) {
            tcpEstablished = true;
        } else if (!arg.startsWith(QLatin1Char('-')) && id.isEmpty()) {
            id = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (id.isEmpty()) {
        err() << "Uso: podstack checkpoint <id|nome> [--leave-running] [--tcp-established] "
                 "[--json]\n";
        return kExitUsage;
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result =
        client.call(QStringLiteral("containers.checkpoint"),
                    QJsonObject{{QStringLiteral("id"), id},
                                {QStringLiteral("leaveRunning"), leaveRunning},
                                {QStringLiteral("tcpEstablished"), tcpEstablished}},
                    &error, 120000);
    if (!error.isEmpty()) {
        return fail(error);
    }
    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
    } else {
        out() << "checkpoint criado: " << id << '\n';
    }
    out().flush();
    return kExitOk;
}



int commandRestore(const QStringList &args)
{
    QString id;
    bool json = false;
    bool tcpEstablished = false;
    for (const QString &arg : args) {
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (arg == QStringLiteral("--tcp-established")) {
            tcpEstablished = true;
        } else if (!arg.startsWith(QLatin1Char('-')) && id.isEmpty()) {
            id = arg;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (id.isEmpty()) {
        err() << "Uso: podstack restore <id|nome> [--tcp-established] [--json]\n";
        return kExitUsage;
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result = client.call(
        QStringLiteral("containers.restore"),
        QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("tcpEstablished"), tcpEstablished}},
        &error, 120000);
    if (!error.isEmpty()) {
        return fail(error);
    }
    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
    } else {
        out() << "checkpoint restaurado: " << id << '\n';
    }
    out().flush();
    return kExitOk;
}

} // namespace PodStack::CliInternal
