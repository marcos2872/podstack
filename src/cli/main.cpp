#include "core/Error.h"
#include "core/Logging.h"
#include "core/RpcClient.h"
#include "core/RpcStream.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTextStream>
#include <QVector>

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
          << "  images [ls|pull|push|rm|prune|history|tag|inspect|export|load]\n"
          << "                          Gerencia imagens\n"
          << "  volumes [ls|create|rm|prune|clone|export|import]\n"
          << "                          Gerencia volumes\n"
          << "  networks [ls|create|rm|connect|disconnect]\n"
          << "                          Gerencia redes\n"
          << "  pods [ls|create|start|stop|restart|rm]\n"
          << "                          Gerencia pods\n"
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

QString humanBytes(qint64 bytes)
{
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return QString::number(value, 'f', unit == 0 ? 0 : 1) + QLatin1Char(' ')
           + QString::fromLatin1(units[unit]);
}

QString epochDate(qint64 epoch)
{
    return QDateTime::fromSecsSinceEpoch(epoch).toString(QStringLiteral("yyyy-MM-dd"));
}

int commandImages(const QStringList &args)
{
    const QString sub = args.isEmpty() ? QStringLiteral("ls") : args.first();

    if (sub == QStringLiteral("ls") || sub == QStringLiteral("list")) {
        PodStack::RpcClient client;
        PodStack::Error error;
        const QJsonObject result = client.call(QStringLiteral("images.list"), {}, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }

        struct Row {
            QString name;
            QString id;
            QString size;
            QString containers;
        };
        QVector<Row> rows;
        int nameWidth = 10;
        for (const QJsonValue &value : result.value(QStringLiteral("items")).toArray()) {
            const QJsonObject image = value.toObject();
            const QJsonArray tags = image.value(QStringLiteral("repoTags")).toArray();
            Row row;
            row.name = tags.isEmpty() ? QStringLiteral("<none>:<none>")
                                      : tags.first().toString();
            row.id = image.value(QStringLiteral("id")).toString().left(12);
            row.size = humanBytes(static_cast<qint64>(
                image.value(QStringLiteral("size")).toDouble()));
            row.containers = QString::number(image.value(QStringLiteral("containers")).toInt());
            nameWidth = qMax(nameWidth, row.name.size());
            rows.append(row);
        }

        out() << QStringLiteral("%1  %2  %3  %4\n")
                     .arg(QStringLiteral("REPOSITORY:TAG").leftJustified(nameWidth))
                     .arg(QStringLiteral("IMAGE ID").leftJustified(12))
                     .arg(QStringLiteral("SIZE").leftJustified(9))
                     .arg(QStringLiteral("CONTAINERS"));
        for (const Row &row : rows) {
            out() << QStringLiteral("%1  %2  %3  %4\n")
                         .arg(row.name.leftJustified(nameWidth))
                         .arg(row.id.leftJustified(12))
                         .arg(row.size.leftJustified(9))
                         .arg(row.containers);
        }
        out().flush();
        return kExitOk;
    }

    if (sub == QStringLiteral("pull") || sub == QStringLiteral("push")) {
        QString reference;
        QString platform;
        QString destination;
        for (int i = 1; i < args.size(); ++i) {
            if (args.at(i) == QStringLiteral("--platform") && i + 1 < args.size()) {
                platform = args.at(++i);
            } else if (args.at(i) == QStringLiteral("--destination") && i + 1 < args.size()) {
                destination = args.at(++i);
            } else if (reference.isEmpty()) {
                reference = args.at(i);
            }
        }
        if (reference.isEmpty() || (sub == QStringLiteral("push") && destination.isEmpty())) {
            err() << "Uso: podstack images " << sub
                  << (sub == QStringLiteral("push") ? " <id> --destination <repo>"
                                                    : " <imagem> [--platform p]")
                  << '\n';
            return kExitUsage;
        }

        QJsonObject params;
        params.insert(sub == QStringLiteral("pull") ? QStringLiteral("reference")
                                                     : QStringLiteral("id"),
                      reference);
        if (!platform.isEmpty()) {
            params.insert(QStringLiteral("platform"), platform);
        }
        if (!destination.isEmpty()) {
            params.insert(QStringLiteral("destination"), destination);
        }

        PodStack::RpcStream stream;
        PodStack::Error error;
        const QString prefix = sub == QStringLiteral("pull")
                                   ? QStringLiteral("images.pull")
                                   : QStringLiteral("images.push");
        const bool ok = stream.run(
            prefix, params,
            [&](const QString &method, const QJsonObject &payload) {
                const QString text = payload.value(QStringLiteral("stream")).toString();
                if (!text.isEmpty()) {
                    out() << text;
                    if (!text.endsWith(QLatin1Char('\n'))) {
                        out() << '\n';
                    }
                    out().flush();
                }
                const QString status = payload.value(QStringLiteral("status")).toString();
                if (!status.isEmpty()) {
                    const QString progress =
                        payload.value(QStringLiteral("progress")).toString();
                    out() << status;
                    if (!progress.isEmpty()) {
                        out() << " " << progress;
                    }
                    out() << '\n';
                    out().flush();
                }
                if (method.endsWith(QStringLiteral(".error"))) {
                    const QJsonObject failure =
                        payload.value(QStringLiteral("error")).toObject();
                    err() << failure.value(QStringLiteral("message")).toString() << '\n';
                    err().flush();
                }
            },
            &error);
        if (!ok) {
            return fail(error);
        }
        out() << sub << " concluído\n";
        out().flush();
        return kExitOk;
    }

    if (sub == QStringLiteral("rm") || sub == QStringLiteral("remove")) {
        QString id;
        bool force = false;
        for (int i = 1; i < args.size(); ++i) {
            if (args.at(i) == QStringLiteral("--force")) {
                force = true;
            } else if (id.isEmpty()) {
                id = args.at(i);
            }
        }
        if (id.isEmpty()) {
            err() << "Uso: podstack images rm <id> [--force]\n";
            return kExitUsage;
        }
        PodStack::RpcClient client;
        PodStack::Error error;
        client.call(QStringLiteral("images.remove"),
                    QJsonObject{{QStringLiteral("id"), id},
                                {QStringLiteral("force"), force}},
                    &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        out() << "removida " << id << '\n';
        out().flush();
        return kExitOk;
    }

    if (sub == QStringLiteral("prune")) {
        const bool all = args.contains(QStringLiteral("--all"));
        PodStack::RpcClient client;
        PodStack::Error error;
        const QJsonObject result =
            client.call(QStringLiteral("images.prune"),
                        QJsonObject{{QStringLiteral("all"), all}}, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        out() << "liberado "
              << humanBytes(static_cast<qint64>(
                     result.value(QStringLiteral("reclaimedBytes")).toDouble()))
              << '\n';
        out().flush();
        return kExitOk;
    }

    if (sub == QStringLiteral("history")) {
        if (args.size() < 2) {
            err() << "Uso: podstack images history <id>\n";
            return kExitUsage;
        }
        PodStack::RpcClient client;
        PodStack::Error error;
        const QJsonObject result =
            client.call(QStringLiteral("images.history"),
                        QJsonObject{{QStringLiteral("id"), args.at(1)}}, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        for (const QJsonValue &value : result.value(QStringLiteral("items")).toArray()) {
            const QJsonObject entry = value.toObject();
            out() << epochDate(static_cast<qint64>(
                       entry.value(QStringLiteral("created")).toDouble()))
                  << "  "
                  << humanBytes(static_cast<qint64>(
                         entry.value(QStringLiteral("size")).toDouble()))
                         .leftJustified(9)
                  << "  " << entry.value(QStringLiteral("createdBy")).toString() << '\n';
        }
        out().flush();
        return kExitOk;
    }

    if (sub == QStringLiteral("tag") || sub == QStringLiteral("untag")) {
        if (args.size() < 3) {
            err() << "Uso: podstack images " << sub << " <id> <repo:tag>\n";
            return kExitUsage;
        }
        QString repo = args.at(2);
        QString tag = QStringLiteral("latest");
        const int slash = repo.lastIndexOf(QLatin1Char('/'));
        const int colon = repo.lastIndexOf(QLatin1Char(':'));
        if (colon > slash) {
            tag = repo.mid(colon + 1);
            repo = repo.left(colon);
        }
        PodStack::RpcClient client;
        PodStack::Error error;
        client.call(QStringLiteral("images.") + sub,
                    QJsonObject{{QStringLiteral("id"), args.at(1)},
                                {QStringLiteral("repo"), repo},
                                {QStringLiteral("tag"), tag}},
                    &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        out() << (sub == QStringLiteral("tag") ? "tag aplicada" : "tag removida") << ": "
              << repo << ':' << tag << '\n';
        out().flush();
        return kExitOk;
    }

    if (sub == QStringLiteral("inspect")) {
        if (args.size() < 2) {
            err() << "Uso: podstack images inspect <id>\n";
            return kExitUsage;
        }
        PodStack::RpcClient client;
        PodStack::Error error;
        const QJsonObject result =
            client.call(QStringLiteral("images.inspect"),
                        QJsonObject{{QStringLiteral("id"), args.at(1)}}, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        out() << QJsonDocument(result.value(QStringLiteral("json")).toObject())
                     .toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    if (sub == QStringLiteral("export") || sub == QStringLiteral("load")) {
        if (args.size() < (sub == QStringLiteral("export") ? 3 : 2)) {
            err() << "Uso: podstack images " << sub << " <origem> <arquivo>\n";
            return kExitUsage;
        }
        QJsonObject params;
        if (sub == QStringLiteral("export")) {
            params.insert(QStringLiteral("id"), args.at(1));
            params.insert(QStringLiteral("path"), args.at(2));
        } else {
            params.insert(QStringLiteral("path"), args.at(1));
        }
        PodStack::RpcClient client;
        PodStack::Error error;
        client.call(sub == QStringLiteral("export") ? QStringLiteral("images.export")
                                                     : QStringLiteral("images.load"),
                    params, &error, 600000);
        if (!error.isEmpty()) {
            return fail(error);
        }
        out() << sub << " concluído\n";
        out().flush();
        return kExitOk;
    }

    err() << "subcomando desconhecido: images " << sub << '\n';
    return kExitUsage;
}

int commandVolumes(const QStringList &args)
{
    const QString sub = args.isEmpty() ? QStringLiteral("ls") : args.first();

    if (sub == QStringLiteral("ls") || sub == QStringLiteral("list")) {
        PodStack::RpcClient client;
        PodStack::Error error;
        const QJsonObject result = client.call(QStringLiteral("volumes.list"), {}, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        struct Row {
            QString name;
            QString driver;
            QString size;
            QString links;
            QString mountpoint;
        };
        QVector<Row> rows;
        int nameWidth = 10;
        for (const QJsonValue &value : result.value(QStringLiteral("items")).toArray()) {
            const QJsonObject volume = value.toObject();
            Row row;
            row.name = volume.value(QStringLiteral("name")).toString();
            row.driver = volume.value(QStringLiteral("driver")).toString();
            row.size = humanBytes(
                static_cast<qint64>(volume.value(QStringLiteral("size")).toDouble()));
            row.links = QString::number(volume.value(QStringLiteral("links")).toInt());
            row.mountpoint = volume.value(QStringLiteral("mountpoint")).toString();
            nameWidth = qMax(nameWidth, row.name.size());
            rows.append(row);
        }
        out() << QStringLiteral("%1  %2  %3  %4  %5\n")
                     .arg(QStringLiteral("NAME").leftJustified(nameWidth))
                     .arg(QStringLiteral("DRIVER").leftJustified(8))
                     .arg(QStringLiteral("SIZE").leftJustified(9))
                     .arg(QStringLiteral("LINKS").leftJustified(5))
                     .arg(QStringLiteral("MOUNTPOINT"));
        for (const Row &row : rows) {
            out() << QStringLiteral("%1  %2  %3  %4  %5\n")
                         .arg(row.name.leftJustified(nameWidth))
                         .arg(row.driver.leftJustified(8))
                         .arg(row.size.leftJustified(9))
                         .arg(row.links.leftJustified(5))
                         .arg(row.mountpoint);
        }
        out().flush();
        return kExitOk;
    }

    if (sub == QStringLiteral("create")) {
        QString name;
        QString driver;
        for (int i = 1; i < args.size(); ++i) {
            if (args.at(i) == QStringLiteral("--driver") && i + 1 < args.size()) {
                driver = args.at(++i);
            } else if (name.isEmpty()) {
                name = args.at(i);
            }
        }
        if (name.isEmpty()) {
            err() << "Uso: podstack volumes create <nome> [--driver driver]\n";
            return kExitUsage;
        }
        PodStack::RpcClient client;
        PodStack::Error error;
        const QJsonObject result =
            client.call(QStringLiteral("volumes.create"),
                        QJsonObject{{QStringLiteral("name"), name},
                                    {QStringLiteral("driver"), driver}},
                        &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        out() << "volume " << result.value(QStringLiteral("Name")).toString() << " criado\n";
        out().flush();
        return kExitOk;
    }

    if (sub == QStringLiteral("rm") || sub == QStringLiteral("remove")) {
        QString name;
        bool force = false;
        for (int i = 1; i < args.size(); ++i) {
            if (args.at(i) == QStringLiteral("--force")) {
                force = true;
            } else if (name.isEmpty()) {
                name = args.at(i);
            }
        }
        if (name.isEmpty()) {
            err() << "Uso: podstack volumes rm <nome> [--force]\n";
            return kExitUsage;
        }
        PodStack::RpcClient client;
        PodStack::Error error;
        client.call(QStringLiteral("volumes.remove"),
                    QJsonObject{{QStringLiteral("name"), name},
                                {QStringLiteral("force"), force}},
                    &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        out() << "volume " << name << " removido\n";
        out().flush();
        return kExitOk;
    }

    if (sub == QStringLiteral("prune")) {
        PodStack::RpcClient client;
        PodStack::Error error;
        const QJsonObject result =
            client.call(QStringLiteral("volumes.prune"), {}, &error, 600000);
        if (!error.isEmpty()) {
            return fail(error);
        }
        out() << "liberado "
              << humanBytes(static_cast<qint64>(
                     result.value(QStringLiteral("reclaimedBytes")).toDouble()))
              << '\n';
        out().flush();
        return kExitOk;
    }

    if (sub == QStringLiteral("clone") || sub == QStringLiteral("export")
        || sub == QStringLiteral("import") || sub == QStringLiteral("import-host")) {
        bool force = args.contains(QStringLiteral("--force"));
        QStringList positional;
        for (int i = 1; i < args.size(); ++i) {
            if (!args.at(i).startsWith(QStringLiteral("--"))) {
                positional.append(args.at(i));
            }
        }
        if (positional.size() < 2) {
            err() << "Uso: podstack volumes " << sub << " <origem> <destino> [--force]\n";
            return kExitUsage;
        }
        QJsonObject params;
        if (sub == QStringLiteral("clone")) {
            params.insert(QStringLiteral("source"), positional.at(0));
            params.insert(QStringLiteral("destination"), positional.at(1));
        } else if (sub == QStringLiteral("import-host")) {
            params.insert(QStringLiteral("name"), positional.at(0));
            params.insert(QStringLiteral("sourcePath"), positional.at(1));
        } else {
            params.insert(QStringLiteral("name"), positional.at(0));
            params.insert(QStringLiteral("path"), positional.at(1));
            params.insert(QStringLiteral("force"), force);
        }
        PodStack::RpcClient client;
        PodStack::Error error;
        client.call(sub == QStringLiteral("import-host")
                        ? QStringLiteral("volumes.importHostPath")
                        : QStringLiteral("volumes.") + sub,
                    params, &error, 600000);
        if (!error.isEmpty()) {
            return fail(error);
        }
        out() << sub << " concluído\n";
        out().flush();
        return kExitOk;
    }

    err() << "subcomando desconhecido: volumes " << sub << '\n';
    return kExitUsage;
}

int commandNetworks(const QStringList &args)
{
    const QString sub = args.isEmpty() ? QStringLiteral("ls") : args.first();

    if (sub == QStringLiteral("ls") || sub == QStringLiteral("list")) {
        PodStack::RpcClient client;
        PodStack::Error error;
        const QJsonObject result = client.call(QStringLiteral("networks.list"), {}, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        struct Row {
            QString name;
            QString driver;
            QString subnet;
            QString containers;
        };
        QVector<Row> rows;
        int nameWidth = 10;
        for (const QJsonValue &value : result.value(QStringLiteral("items")).toArray()) {
            const QJsonObject network = value.toObject();
            Row row;
            row.name = network.value(QStringLiteral("name")).toString();
            row.driver = network.value(QStringLiteral("driver")).toString();
            row.subnet = network.value(QStringLiteral("subnet")).toString();
            row.containers =
                QString::number(network.value(QStringLiteral("containers")).toInt());
            nameWidth = qMax(nameWidth, row.name.size());
            rows.append(row);
        }
        out() << QStringLiteral("%1  %2  %3  %4\n")
                     .arg(QStringLiteral("NAME").leftJustified(nameWidth))
                     .arg(QStringLiteral("DRIVER").leftJustified(8))
                     .arg(QStringLiteral("SUBNET").leftJustified(18))
                     .arg(QStringLiteral("CONTAINERS"));
        for (const Row &row : rows) {
            out() << QStringLiteral("%1  %2  %3  %4\n")
                         .arg(row.name.leftJustified(nameWidth))
                         .arg(row.driver.leftJustified(8))
                         .arg(row.subnet.leftJustified(18))
                         .arg(row.containers);
        }
        out().flush();
        return kExitOk;
    }

    if (sub == QStringLiteral("create")) {
        QString name;
        QString driver;
        QString subnet;
        QString gateway;
        bool ipv6 = false;
        for (int i = 1; i < args.size(); ++i) {
            if (args.at(i) == QStringLiteral("--driver") && i + 1 < args.size()) {
                driver = args.at(++i);
            } else if (args.at(i) == QStringLiteral("--subnet") && i + 1 < args.size()) {
                subnet = args.at(++i);
            } else if (args.at(i) == QStringLiteral("--gateway") && i + 1 < args.size()) {
                gateway = args.at(++i);
            } else if (args.at(i) == QStringLiteral("--ipv6")) {
                ipv6 = true;
            } else if (name.isEmpty()) {
                name = args.at(i);
            }
        }
        if (name.isEmpty()) {
            err() << "Uso: podstack networks create <nome> [--subnet CIDR] [--gateway IP]\n";
            return kExitUsage;
        }
        PodStack::RpcClient client;
        PodStack::Error error;
        const QJsonObject result =
            client.call(QStringLiteral("networks.create"),
                        QJsonObject{{QStringLiteral("name"), name},
                                    {QStringLiteral("driver"), driver},
                                    {QStringLiteral("subnet"), subnet},
                                    {QStringLiteral("gateway"), gateway},
                                    {QStringLiteral("ipv6"), ipv6}},
                        &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        out() << "rede " << result.value(QStringLiteral("name")).toString() << " criada\n";
        out().flush();
        return kExitOk;
    }

    if (sub == QStringLiteral("rm") || sub == QStringLiteral("remove")) {
        QString name;
        bool force = false;
        for (int i = 1; i < args.size(); ++i) {
            if (args.at(i) == QStringLiteral("--force")) {
                force = true;
            } else if (name.isEmpty()) {
                name = args.at(i);
            }
        }
        if (name.isEmpty()) {
            err() << "Uso: podstack networks rm <nome> [--force]\n";
            return kExitUsage;
        }
        PodStack::RpcClient client;
        PodStack::Error error;
        client.call(QStringLiteral("networks.remove"),
                    QJsonObject{{QStringLiteral("name"), name},
                                {QStringLiteral("force"), force}},
                    &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        out() << "rede " << name << " removida\n";
        out().flush();
        return kExitOk;
    }

    if (sub == QStringLiteral("connect") || sub == QStringLiteral("disconnect")) {
        QString name;
        QString container;
        QStringList aliases;
        for (int i = 1; i < args.size(); ++i) {
            if (args.at(i) == QStringLiteral("--alias") && i + 1 < args.size()) {
                aliases.append(args.at(++i));
            } else if (name.isEmpty()) {
                name = args.at(i);
            } else if (container.isEmpty()) {
                container = args.at(i);
            }
        }
        if (name.isEmpty() || container.isEmpty()) {
            err() << "Uso: podstack networks " << sub << " <rede> <container> [--alias a]\n";
            return kExitUsage;
        }
        QJsonArray aliasArray;
        for (const QString &alias : aliases) {
            aliasArray.append(alias);
        }
        PodStack::RpcClient client;
        PodStack::Error error;
        client.call(QStringLiteral("networks.") + sub,
                    QJsonObject{{QStringLiteral("name"), name},
                                {QStringLiteral("containerId"), container},
                                {QStringLiteral("aliases"), aliasArray}},
                    &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        out() << sub << " concluído\n";
        out().flush();
        return kExitOk;
    }

    err() << "subcomando desconhecido: networks " << sub << '\n';
    return kExitUsage;
}

int commandPods(const QStringList &args)
{
    const QString sub = args.isEmpty() ? QStringLiteral("ls") : args.first();

    if (sub == QStringLiteral("ls") || sub == QStringLiteral("list")) {
        PodStack::RpcClient client;
        PodStack::Error error;
        const QJsonObject result = client.call(QStringLiteral("pods.list"), {}, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        struct Row {
            QString name;
            QString status;
            QString containers;
            QString id;
        };
        QVector<Row> rows;
        int nameWidth = 10;
        for (const QJsonValue &value : result.value(QStringLiteral("items")).toArray()) {
            const QJsonObject pod = value.toObject();
            Row row;
            row.name = pod.value(QStringLiteral("name")).toString();
            row.status = pod.value(QStringLiteral("status")).toString();
            row.containers = QString::number(pod.value(QStringLiteral("containers")).toInt());
            row.id = pod.value(QStringLiteral("id")).toString().left(12);
            nameWidth = qMax(nameWidth, row.name.size());
            rows.append(row);
        }
        out() << QStringLiteral("%1  %2  %3  %4\n")
                     .arg(QStringLiteral("NAME").leftJustified(nameWidth))
                     .arg(QStringLiteral("STATUS").leftJustified(10))
                     .arg(QStringLiteral("CONTAINERS").leftJustified(10))
                     .arg(QStringLiteral("ID"));
        for (const Row &row : rows) {
            out() << QStringLiteral("%1  %2  %3  %4\n")
                         .arg(row.name.leftJustified(nameWidth))
                         .arg(row.status.leftJustified(10))
                         .arg(row.containers.leftJustified(10))
                         .arg(row.id);
        }
        out().flush();
        return kExitOk;
    }

    if (sub == QStringLiteral("create")) {
        if (args.size() < 2) {
            err() << "Uso: podstack pods create <nome>\n";
            return kExitUsage;
        }
        PodStack::RpcClient client;
        PodStack::Error error;
        const QJsonObject result =
            client.call(QStringLiteral("pods.create"),
                        QJsonObject{{QStringLiteral("name"), args.at(1)}}, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        out() << "pod " << args.at(1) << " criado ("
              << result.value(QStringLiteral("Id")).toString().left(12) << ")\n";
        out().flush();
        return kExitOk;
    }

    if (sub == QStringLiteral("start") || sub == QStringLiteral("stop")
        || sub == QStringLiteral("restart")) {
        if (args.size() < 2) {
            err() << "Uso: podstack pods " << sub << " <id|nome>\n";
            return kExitUsage;
        }
        PodStack::RpcClient client;
        PodStack::Error error;
        client.call(QStringLiteral("pods.") + sub,
                    QJsonObject{{QStringLiteral("id"), args.at(1)}}, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        out() << sub << ' ' << args.at(1) << ": ok\n";
        out().flush();
        return kExitOk;
    }

    if (sub == QStringLiteral("rm") || sub == QStringLiteral("remove")) {
        QString id;
        bool force = false;
        for (int i = 1; i < args.size(); ++i) {
            if (args.at(i) == QStringLiteral("--force")) {
                force = true;
            } else if (id.isEmpty()) {
                id = args.at(i);
            }
        }
        if (id.isEmpty()) {
            err() << "Uso: podstack pods rm <id|nome> [--force]\n";
            return kExitUsage;
        }
        PodStack::RpcClient client;
        PodStack::Error error;
        client.call(QStringLiteral("pods.remove"),
                    QJsonObject{{QStringLiteral("id"), id},
                                {QStringLiteral("force"), force}},
                    &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        out() << "pod " << id << " removido\n";
        out().flush();
        return kExitOk;
    }

    err() << "subcomando desconhecido: pods " << sub << '\n';
    return kExitUsage;
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
