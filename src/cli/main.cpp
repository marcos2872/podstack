#include "core/Error.h"
#include "core/Logging.h"
#include "core/RpcClient.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTextStream>

namespace {

constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitUsage = 2;
constexpr int kExitEngineUnavailable = 7;

QTextStream &out()
{
    static QTextStream stream(stdout);
    stream.setEncoding(QStringConverter::Utf8);
    return stream;
}

QTextStream &err()
{
    static QTextStream stream(stderr);
    stream.setEncoding(QStringConverter::Utf8);
    return stream;
}

void printUsage()
{
    out() << "Uso: podstack <comando> [opções]\n\n"
          << "Comandos:\n"
          << "  version                 Mostra a versão\n"
          << "  status                  Estado do daemon/Podman\n"
          << "  ps [--all|--running|--stopped] [--format json] [--quiet]\n"
          << "                          Lista containers\n"
          << "  pause <id|nome>         Pausa um container\n"
          << "  unpause <id|nome>       Despausa um container\n"
          << "  kill [--signal S] <id>  Mata um container\n"
          << "  inspect <id|nome>       Mostra o JSON do container\n"
          << "  exec <id|nome> -- <cmd...>\n"
          << "                          Executa um comando no container (hijack)\n"
          << "  doctor                  Diagnóstico rápido\n"
          << "  help                    Esta ajuda\n";
    out().flush();
}

int fail(const PodStack::Error &error)
{
    err() << error.message << '\n';
    if (!error.detail.isEmpty()) {
        err() << "  detalhe: " << error.detail << '\n';
    }
    if (!error.remediation.isEmpty()) {
        err() << "  ação: " << error.remediation << '\n';
    }
    err().flush();

    if (error.code == QStringLiteral("PODSTACK_DAEMON_UNAVAILABLE")
        || error.code == QStringLiteral("PODSTACK_ENGINE_UNAVAILABLE")) {
        return kExitEngineUnavailable;
    }
    return kExitError;
}

int commandVersion()
{
    out() << "PodStack " << QStringLiteral(PODSTACK_VERSION) << '\n';
    out().flush();
    return kExitOk;
}

int commandStatus()
{
    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result = client.call(QStringLiteral("system.status"), {}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    const QString state = result.value(QStringLiteral("state")).toString();
    const QJsonObject podman = result.value(QStringLiteral("podman")).toObject();
    out() << "estado: " << (state.isEmpty() ? QStringLiteral("desconhecido") : state) << '\n';
    out() << "engine: " << result.value(QStringLiteral("engine")).toString() << '\n';
    out() << "podman: " << podman.value(QStringLiteral("apiVersion")).toString() << '\n';
    out() << "socket: " << podman.value(QStringLiteral("socket")).toString() << '\n';
    if (result.contains(QStringLiteral("error"))) {
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        out() << "erro: " << errorObject.value(QStringLiteral("message")).toString() << '\n';
        out().flush();
        return kExitError;
    }
    out().flush();
    return kExitOk;
}

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

int commandDoctor()
{
    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result = client.call(QStringLiteral("system.doctor"), {}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    bool allOk = true;
    const QJsonArray checks = result.value(QStringLiteral("checks")).toArray();
    for (const QJsonValue &value : checks) {
        const QJsonObject check = value.toObject();
        const bool ok = check.value(QStringLiteral("status")).toString() == QStringLiteral("ok");
        allOk = allOk && ok;
        out() << (ok ? "[ok]   " : "[fail] ") << check.value(QStringLiteral("id")).toString()
              << " - " << check.value(QStringLiteral("detail")).toString() << '\n';
        const QString remediation = check.value(QStringLiteral("remediation")).toString();
        if (!remediation.isEmpty()) {
            out() << "       ação: " << remediation << '\n';
        }
    }
    out().flush();
    return allOk ? kExitOk : kExitError;
}

} // namespace

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
    if (command == QStringLiteral("inspect")) {
        return commandInspect(args);
    }
    if (command == QStringLiteral("exec")) {
        return commandExec(args);
    }
    if (command == QStringLiteral("doctor")) {
        return commandDoctor();
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
