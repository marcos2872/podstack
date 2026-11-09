#include "core/Error.h"
#include "core/Logging.h"
#include "core/RpcClient.h"
#include "core/RpcStream.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTextStream>
#include <QVector>

#include <limits>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

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
          << "  compose [ls|up|down|restart|stop|start|logs]\n"
          << "                          Projetos Docker Compose\n"
          << "  build <contexto> -t <tag> [-f Dockerfile] [--build-arg K=V]\n"
          << "                          Build de imagem\n"
          << "  builds [ls|rm]        Histórico de builds\n"
          << "  machine <list|info|logs|create|start|stop|restart|delete|distros|ssh>\n"
          << "                          Gerencia machines QEMU/KVM\n"
          << "  ssh <id|nome> [-- cmd...]\n"
          << "                          Abre SSH na machine com a chave gerenciada\n"
          << "  top [--once] [--interval S] [--json] [--plain]\n"
          << "                          Monitora CPU/memória/containers\n"
          << "  system df [--detailed] [--json]\n"
          << "                          Uso de disco e prévia de prune\n"
          << "  setup [--check|--status|--validate]\n"
          << "        [--apply podman|qemu|components|socket]\n"
          << "        [--remove-docker --yes]\n"
          << "                          Setup/onboarding da máquina\n"
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

int commandCompose(const QStringList &args)
{
    const QString sub = args.isEmpty() ? QStringLiteral("ls") : args.first();

    if (sub == QStringLiteral("ls") || sub == QStringLiteral("list")) {
        PodStack::RpcClient client;
        PodStack::Error error;
        const QJsonObject result =
            client.call(QStringLiteral("compose.projects.list"), {}, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        struct Row {
            QString name;
            QString status;
            QString services;
            QString dir;
        };
        QVector<Row> rows;
        int nameWidth = 8;
        int statusWidth = 10;
        for (const QJsonValue &value : result.value(QStringLiteral("items")).toArray()) {
            const QJsonObject project = value.toObject();
            Row row;
            row.name = project.value(QStringLiteral("name")).toString();
            row.status = project.value(QStringLiteral("status")).toString();
            QStringList services;
            for (const QJsonValue &service :
                 project.value(QStringLiteral("services")).toArray()) {
                services.append(service.toObject().value(QStringLiteral("name")).toString());
            }
            row.services = services.join(QStringLiteral(","));
            row.dir = project.value(QStringLiteral("workingDir")).toString();
            nameWidth = qMax(nameWidth, row.name.size());
            statusWidth = qMax(statusWidth, row.status.size());
            rows.append(row);
        }
        out() << QStringLiteral("%1  %2  %3  %4\n")
                     .arg(QStringLiteral("NAME").leftJustified(nameWidth))
                     .arg(QStringLiteral("STATUS").leftJustified(statusWidth))
                     .arg(QStringLiteral("SERVICES").leftJustified(20))
                     .arg(QStringLiteral("DIR"));
        for (const Row &row : rows) {
            out() << QStringLiteral("%1  %2  %3  %4\n")
                         .arg(row.name.leftJustified(nameWidth))
                         .arg(row.status.leftJustified(statusWidth))
                         .arg(row.services.leftJustified(20))
                         .arg(row.dir);
        }
        out().flush();
        return kExitOk;
    }

    if (sub == QStringLiteral("up") || sub == QStringLiteral("down")
        || sub == QStringLiteral("restart") || sub == QStringLiteral("stop")
        || sub == QStringLiteral("start")) {
        QString projectPath;
        QString service;
        bool volumes = false;
        for (int i = 1; i < args.size(); ++i) {
            if (args.at(i) == QStringLiteral("--volumes")) {
                volumes = true;
            } else if (projectPath.isEmpty()) {
                projectPath = args.at(i);
            } else if (service.isEmpty()) {
                service = args.at(i);
            }
        }
        if (projectPath.isEmpty()) {
            err() << "Uso: podstack compose " << sub
                  << " <diretório> [serviço] [--volumes]\n";
            return kExitUsage;
        }
        PodStack::RpcClient client;
        PodStack::Error error;
        client.call(QStringLiteral("compose.") + sub,
                    QJsonObject{{QStringLiteral("projectPath"), projectPath},
                                {QStringLiteral("service"), service},
                                {QStringLiteral("volumes"), volumes},
                                {QStringLiteral("detach"), true}},
                    &error, 600000);
        if (!error.isEmpty()) {
            return fail(error);
        }
        out() << "compose " << sub << " concluído\n";
        out().flush();
        return kExitOk;
    }

    if (sub == QStringLiteral("logs")) {
        if (args.size() < 2) {
            err() << "Uso: podstack compose logs <projeto> [serviço]\n";
            return kExitUsage;
        }
        QJsonObject params;
        params.insert(QStringLiteral("project"), args.at(1));
        if (args.size() > 2) {
            params.insert(QStringLiteral("service"), args.at(2));
        }
        PodStack::RpcStream stream;
        PodStack::Error error;
        const bool ok = stream.run(
            QStringLiteral("compose.logs.subscribe"), params,
            [&](const QString &method, const QJsonObject &payload) {
                if (method == QStringLiteral("compose.logs.delta")) {
                    out() << payload.value(QStringLiteral("service")).toString() << " | "
                          << payload.value(QStringLiteral("line")).toString() << '\n';
                    out().flush();
                }
            },
            &error, 0, false);
        return ok ? kExitOk : fail(error);
    }

    err() << "subcomando desconhecido: compose " << sub << '\n';
    return kExitUsage;
}

int commandBuild(const QStringList &args)
{
    QString contextPath;
    QString tag;
    QString dockerfile = QStringLiteral("Dockerfile");
    QString platform;
    QStringList buildArgList;

    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i) == QStringLiteral("-t") && i + 1 < args.size()) {
            tag = args.at(++i);
        } else if (args.at(i) == QStringLiteral("-f") && i + 1 < args.size()) {
            dockerfile = args.at(++i);
        } else if (args.at(i) == QStringLiteral("--platform") && i + 1 < args.size()) {
            platform = args.at(++i);
        } else if (args.at(i) == QStringLiteral("--build-arg") && i + 1 < args.size()) {
            buildArgList.append(args.at(++i));
        } else if (contextPath.isEmpty() && !args.at(i).startsWith(QStringLiteral("-"))) {
            contextPath = args.at(i);
        }
    }

    if (contextPath.isEmpty() || tag.isEmpty()) {
        err() << "Uso: podstack build <contexto> -t <tag> [-f Dockerfile] "
                 "[--build-arg K=V] [--platform p]\n";
        return kExitUsage;
    }

    QJsonObject buildArgs;
    for (const QString &item : buildArgList) {
        const int equals = item.indexOf(QLatin1Char('='));
        if (equals > 0) {
            buildArgs.insert(item.left(equals), item.mid(equals + 1));
        }
    }

    QJsonObject params;
    params.insert(QStringLiteral("contextPath"), contextPath);
    params.insert(QStringLiteral("dockerfile"), dockerfile);
    params.insert(QStringLiteral("tag"), tag);
    params.insert(QStringLiteral("buildArgs"), buildArgs);
    params.insert(QStringLiteral("platform"), platform);
    params.insert(QStringLiteral("trigger"), QStringLiteral("cli"));

    QString finalStatus;
    QString finalError;
    QString buildId;
    PodStack::RpcStream stream;
    PodStack::Error error;
    const bool ok = stream.run(
        QStringLiteral("builds.start"), params,
        [&](const QString &method, const QJsonObject &payload) {
            if (method == QStringLiteral("builds.delta")) {
                const QString line = payload.value(QStringLiteral("line")).toString();
                if (!line.isEmpty()) {
                    out() << line << '\n';
                    out().flush();
                }
                if (buildId.isEmpty()) {
                    buildId = payload.value(QStringLiteral("buildId")).toString();
                }
            } else if (method == QStringLiteral("builds.done")) {
                finalStatus = payload.value(QStringLiteral("status")).toString();
                finalError = payload.value(QStringLiteral("error")).toString();
                buildId = payload.value(QStringLiteral("buildId")).toString();
            }
        },
        &error);
    if (!ok) {
        return fail(error);
    }
    if (finalStatus == QStringLiteral("failed")) {
        err() << "build " << buildId << " falhou: " << finalError << '\n';
        err().flush();
        return kExitError;
    }
    if (finalStatus == QStringLiteral("cancelled")) {
        err() << "build " << buildId << " cancelado\n";
        err().flush();
        return kExitError;
    }
    out() << "build " << buildId << " concluído\n";
    out().flush();
    return kExitOk;
}

void printMachineUsage(QTextStream &stream)
{
    stream << "Uso: podstack machine <comando> [opções]\n\n"
           << "Comandos:\n"
           << "  list [--json]            Lista machines\n"
           << "  info <id|nome> [--json]  Mostra os dados de uma machine\n"
           << "  logs <id|nome> [--tail N] [--json]\n"
           << "                          Mostra as últimas linhas do console.log\n"
           << "  create DISTRO[:VER] NOME [--cpus N] [--memory 4G] [--disk 20G]\n"
           << "         [--user U] [--user-data ARQUIVO] [--isolate-network]\n"
           << "                          Cria uma machine (a senha entra na T4-04)\n"
           << "  start <id|nome> [--json] Inicia uma machine\n"
           << "  stop <id|nome> [--force] [--json]\n"
           << "                          Para uma machine\n"
           << "  restart <id|nome> [--json]\n"
           << "                          Reinicia uma machine\n"
           << "  delete <id|nome> [--force] [--json]\n"
           << "                          Remove uma machine e seus dados\n"
           << "  ssh <id|nome> [-- cmd...]\n"
           << "                          Abre SSH com a chave gerenciada\n"
           << "  distros [--json]         Lista imagens cloud e cache local\n";
    stream.flush();
}

bool parseSize(const QString &text, qint64 *bytes)
{
    if (!bytes) {
        return false;
    }

    QString value = text.trimmed();
    if (value.isEmpty()) {
        return false;
    }

    qint64 multiplier = 1;
    const QChar suffix = value.back().toUpper();
    if (suffix == QLatin1Char('K') || suffix == QLatin1Char('M')
        || suffix == QLatin1Char('G') || suffix == QLatin1Char('T')) {
        if (suffix == QLatin1Char('K')) {
            multiplier = 1024LL;
        } else if (suffix == QLatin1Char('M')) {
            multiplier = 1024LL * 1024LL;
        } else if (suffix == QLatin1Char('G')) {
            multiplier = 1024LL * 1024LL * 1024LL;
        } else {
            multiplier = 1024LL * 1024LL * 1024LL * 1024LL;
        }
        value.chop(1);
    }

    bool ok = false;
    const qint64 number = value.toLongLong(&ok);
    if (!ok || number <= 0
        || number > std::numeric_limits<qint64>::max() / multiplier) {
        return false;
    }
    *bytes = number * multiplier;
    return true;
}

PodStack::Error errorFromJson(const QJsonObject &object)
{
    PodStack::Error error;
    error.code = object.value(QStringLiteral("code")).toString();
    error.message = object.value(QStringLiteral("message")).toString();
    error.detail = object.value(QStringLiteral("detail")).toString();
    error.remediation = object.value(QStringLiteral("remediation")).toString();
    error.retryable = object.value(QStringLiteral("retryable")).toBool();
    if (error.message.isEmpty()) {
        error.message = QStringLiteral("Falha ao criar a machine.");
    }
    if (error.code.isEmpty()) {
        error.code = QStringLiteral("PODSTACK_INTERNAL");
    }
    return error;
}

QString formatSize(qint64 bytes)
{
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    if (bytes <= 0) {
        return QStringLiteral("-");
    }
    return QStringLiteral("%1 GiB").arg(QString::number(bytes / kGiB, 'f', 1));
}

QString machineSsh(const QJsonObject &machine)
{
    const int port = machine.value(QStringLiteral("sshPort")).toInt();
    if (port <= 0) {
        return QStringLiteral("-");
    }
    QString user = machine.value(QStringLiteral("sshUser")).toString();
    if (user.isEmpty()) {
        user = QStringLiteral("podstack");
    }
    return QStringLiteral("%1@127.0.0.1:%2").arg(user).arg(port);
}

QString machineDistroLabel(const QJsonObject &machine)
{
    QString distro = machine.value(QStringLiteral("distro")).toString();
    const QString version = machine.value(QStringLiteral("distroVersion")).toString();
    if (!version.isEmpty() && !distro.contains(QLatin1Char(':'))) {
        distro += QLatin1Char(':') + version;
    }
    return distro;
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
    const QJsonObject machine =
        client.call(QStringLiteral("machines.get"),
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
          << "disco: " << formatSize(machine.value(QStringLiteral("diskBytes")).toInteger())
          << '\n'
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
    const QJsonObject result =
        client.call(QStringLiteral("machines.logs"),
                    QJsonObject{{QStringLiteral("id"), id},
                                {QStringLiteral("tail"), tail}},
                    &error, 30000);
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

int machineCreateCommand(const QStringList &args)
{
    int cpus = 2;
    qint64 memoryBytes = 4LL * 1024LL * 1024LL * 1024LL;
    qint64 diskBytes = 20LL * 1024LL * 1024LL * 1024LL;
    QString user;
    QString userDataPath;
    bool isolateNetwork = false;
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
            err() << "não foi possível ler " << userDataPath << ": " << file.errorString()
                  << '\n';
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
    const QString display = name.isEmpty() ? result.value(QStringLiteral("id")).toString()
                                           : name;
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

int machineSshCommand(const QStringList &args)
{
    QString id;
    QStringList command;
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
        if (id.isEmpty() && !arg.startsWith(QLatin1Char('-'))) {
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
    const QJsonObject result =
        client.call(QStringLiteral("machines.ssh"),
                    QJsonObject{{QStringLiteral("id"), id}}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    const QString host = result.value(QStringLiteral("host")).toString();
    const int port = result.value(QStringLiteral("port")).toInt();
    const QString user = result.value(QStringLiteral("user")).toString();
    const QString identityFile = result.value(QStringLiteral("identityFile")).toString();
    if (host.isEmpty() || port <= 0 || user.isEmpty() || identityFile.isEmpty()) {
        err() << "resposta SSH inválida do daemon\n";
        err().flush();
        return kExitError;
    }

    QStringList sshArguments{
        QStringLiteral("-i"),
        identityFile,
        QStringLiteral("-p"),
        QString::number(port),
        QStringLiteral("-o"),
        QStringLiteral("IdentitiesOnly=yes"),
        QStringLiteral("-o"),
        QStringLiteral("StrictHostKeyChecking=accept-new"),
        QStringLiteral("%1@%2").arg(user, host),
    };
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

void printTopSample(const QJsonObject &sample)
{
    const QJsonObject host = sample.value(QStringLiteral("host")).toObject();
    const QJsonObject totals = sample.value(QStringLiteral("totals")).toObject();

    out() << "HOST   cpu " << QString::number(host.value(QStringLiteral("cpuPercent")).toDouble(),
                                             'f', 1)
          << "%  mem " << humanBytes(host.value(QStringLiteral("memUsed")).toInteger()) << " / "
          << humanBytes(host.value(QStringLiteral("memTotal")).toInteger()) << "  net "
          << humanBytes(host.value(QStringLiteral("netRx")).toInteger()) << "/s rx, "
          << humanBytes(host.value(QStringLiteral("netTx")).toInteger()) << "/s tx\n";
    out() << "TOTAL  cpu "
          << QString::number(totals.value(QStringLiteral("cpuPercent")).toDouble(), 'f', 1)
          << "%  mem " << humanBytes(totals.value(QStringLiteral("memUsed")).toInteger())
          << " / " << humanBytes(totals.value(QStringLiteral("memLimit")).toInteger())
          << "  pids " << totals.value(QStringLiteral("pids")).toInt() << '\n';

    struct Row {
        QString name;
        double cpu = 0.0;
        qint64 mem = 0;
        int pids = 0;
        qint64 rx = 0;
        qint64 tx = 0;
    };
    QVector<Row> rows;
    int nameWidth = 4;
    for (const QJsonValue &value : sample.value(QStringLiteral("containers")).toArray()) {
        const QJsonObject container = value.toObject();
        Row row;
        row.name = container.value(QStringLiteral("name")).toString();
        if (row.name.isEmpty()) {
            row.name = container.value(QStringLiteral("id")).toString().left(12);
        }
        row.cpu = container.value(QStringLiteral("cpuPercent")).toDouble();
        row.mem = container.value(QStringLiteral("memUsed")).toInteger();
        row.pids = container.value(QStringLiteral("pids")).toInt();
        row.rx = container.value(QStringLiteral("netRx")).toInteger();
        row.tx = container.value(QStringLiteral("netTx")).toInteger();
        nameWidth = qMax(nameWidth, row.name.size());
        rows.append(row);
    }

    out() << QStringLiteral("%1  %2  %3  %4  %5\n")
                 .arg(QStringLiteral("NOME").leftJustified(nameWidth))
                 .arg(QStringLiteral("CPU%").rightJustified(6))
                 .arg(QStringLiteral("MEM").rightJustified(10))
                 .arg(QStringLiteral("PIDS").rightJustified(5))
                 .arg(QStringLiteral("NET RX/TX"));
    for (const Row &row : rows) {
        out() << QStringLiteral("%1  %2  %3  %4  %5/%6\n")
                     .arg(row.name.leftJustified(nameWidth))
                     .arg(QString::number(row.cpu, 'f', 1).rightJustified(6))
                     .arg(humanBytes(row.mem).rightJustified(10))
                     .arg(QString::number(row.pids).rightJustified(5))
                     .arg(humanBytes(row.rx), humanBytes(row.tx));
    }
    out().flush();
}

int commandTop(const QStringList &args)
{
    bool once = false;
    bool json = false;
    bool plain = false;
    int interval = 2;

    for (int i = 0; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--once")) {
            once = true;
        } else if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (arg == QStringLiteral("--plain")) {
            plain = true;
        } else if (arg == QStringLiteral("--interval") && i + 1 < args.size()) {
            bool ok = false;
            interval = args.at(++i).toInt(&ok);
            if (!ok) {
                err() << "--interval exige um número inteiro\n";
                return kExitUsage;
            }
        } else if (arg == QStringLiteral("--help") || arg == QStringLiteral("-h")) {
            out() << "Uso: podstack top [--once] [--interval S] [--json] [--plain]\n";
            out().flush();
            return kExitOk;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }
    interval = qBound(1, interval, 10);

    int samples = 0;
    bool printed = false;
    const int timeoutMs = once ? interval * 1000 + 3000 : 0;
    const bool clearScreen = !json && !plain && ::isatty(STDOUT_FILENO);

    PodStack::RpcStream stream;
    PodStack::Error error;
    const bool ok = stream.run(
        QStringLiteral("system.stats.subscribe"),
        QJsonObject{{QStringLiteral("intervalSeconds"), interval}},
        [&](const QString &method, const QJsonObject &payload) {
            if (method != QStringLiteral("system.stats.delta")) {
                return;
            }
            ++samples;
            if (once && samples < 2) {
                return;
            }
            if (json) {
                out() << QJsonDocument(payload).toJson(QJsonDocument::Compact) << '\n';
            } else {
                if (clearScreen) {
                    out() << "\033[2J\033[H";
                }
                printTopSample(payload);
            }
            out().flush();
            printed = true;
        },
        &error, timeoutMs, false);

    if (!ok && !(once && error.code == QStringLiteral("PODSTACK_TIMEOUT") && printed)) {
        return fail(error);
    }
    if (once && !printed) {
        err() << "nenhuma amostra de stats recebida\n";
        err().flush();
        return kExitError;
    }
    return kExitOk;
}

QString imageLabel(const QJsonObject &item)
{
    const QString repository = item.value(QStringLiteral("repository")).toString();
    const QString tag = item.value(QStringLiteral("tag")).toString();
    if (repository.isEmpty() || repository == QStringLiteral("<none>")) {
        return QStringLiteral("<none>:<none>");
    }
    return tag.isEmpty() ? repository : repository + QLatin1Char(':') + tag;
}

int commandSystem(const QStringList &args)
{
    if (args.isEmpty()) {
        err() << "Uso: podstack system df [--detailed] [--json]\n";
        return kExitUsage;
    }
    const QString sub = args.first();
    if (sub == QStringLiteral("--help") || sub == QStringLiteral("-h")
        || sub == QStringLiteral("help")) {
        out() << "Uso: podstack system df [--detailed] [--json]\n";
        out().flush();
        return kExitOk;
    }
    if (sub != QStringLiteral("df")) {
        err() << "subcomando desconhecido: system " << sub << '\n';
        err() << "Uso: podstack system df [--detailed] [--json]\n";
        return kExitUsage;
    }

    bool detailed = false;
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--detailed")) {
            detailed = true;
        } else if (arg == QStringLiteral("--json")) {
            json = true;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result = client.call(
        QStringLiteral("system.df"),
        QJsonObject{{QStringLiteral("detailed"), detailed}}, &error, 60000);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    const QJsonObject sections = result.value(QStringLiteral("sections")).toObject();
    const auto section = [&sections](const QString &name) {
        return sections.value(name).toObject();
    };
    const auto printSummaryRow = [](const QString &name, const QJsonObject &value) {
        out() << QStringLiteral("%1  %2  %3  %4\n")
                     .arg(name.leftJustified(12))
                     .arg(QString::number(value.value(QStringLiteral("count")).toInt())
                              .rightJustified(6))
                     .arg(humanBytes(static_cast<qint64>(
                              value.value(QStringLiteral("sizeBytes")).toDouble()))
                              .rightJustified(11))
                     .arg(humanBytes(static_cast<qint64>(
                              value.value(QStringLiteral("reclaimableBytes")).toDouble()))
                              .rightJustified(11));
    };

    if (!detailed) {
        out() << QStringLiteral("%1  %2  %3  %4\n")
                     .arg(QStringLiteral("SEÇÃO").leftJustified(12))
                     .arg(QStringLiteral("ITENS").rightJustified(6))
                     .arg(QStringLiteral("TAMANHO").rightJustified(11))
                     .arg(QStringLiteral("RECUPERÁVEL").rightJustified(11));
        printSummaryRow(QStringLiteral("IMAGENS"), section(QStringLiteral("images")));
        printSummaryRow(QStringLiteral("CONTAINERS"), section(QStringLiteral("containers")));
        printSummaryRow(QStringLiteral("VOLUMES"), section(QStringLiteral("volumes")));
        out() << QStringLiteral("TOTAL recuperável: ")
              << humanBytes(static_cast<qint64>(
                     result.value(QStringLiteral("totalReclaimableBytes")).toDouble()))
              << '\n';
        out().flush();
        return kExitOk;
    }

    const auto items = [](const QJsonObject &value) {
        return value.value(QStringLiteral("items")).toArray();
    };

    out() << "IMAGENS\n";
    const QJsonArray images = items(section(QStringLiteral("images")));
    for (const QJsonValue &value : images) {
        const QJsonObject item = value.toObject();
        QStringList flags;
        if (item.value(QStringLiteral("dangling")).toBool()) {
            flags.append(QStringLiteral("dangling"));
        }
        flags.append(item.value(QStringLiteral("unused")).toBool()
                         ? QStringLiteral("sem uso")
                         : QStringLiteral("em uso"));
        out() << "  " << imageLabel(item).leftJustified(48) << "  "
              << humanBytes(static_cast<qint64>(
                     item.value(QStringLiteral("sizeBytes")).toDouble()))
                     .rightJustified(10)
              << "  " << flags.join(QStringLiteral(" · ")) << '\n';
    }
    if (images.isEmpty()) {
        out() << "  (nenhuma imagem)\n";
    }

    out() << "CONTAINERS\n";
    const QJsonArray containers = items(section(QStringLiteral("containers")));
    for (const QJsonValue &value : containers) {
        const QJsonObject item = value.toObject();
        out() << "  " << item.value(QStringLiteral("name")).toString().leftJustified(48) << "  "
              << humanBytes(static_cast<qint64>(
                     item.value(QStringLiteral("sizeBytes")).toDouble()))
                     .rightJustified(10)
              << "  " << item.value(QStringLiteral("status")).toString()
              << (item.value(QStringLiteral("running")).toBool()
                      ? QStringLiteral(" (em execução)")
                      : QStringLiteral(" (parado)"))
              << '\n';
    }
    if (containers.isEmpty()) {
        out() << "  (nenhum container)\n";
    }

    out() << "VOLUMES\n";
    const QJsonArray volumes = items(section(QStringLiteral("volumes")));
    for (const QJsonValue &value : volumes) {
        const QJsonObject item = value.toObject();
        const bool inUse = item.value(QStringLiteral("inUse")).toBool();
        out() << "  " << item.value(QStringLiteral("name")).toString().leftJustified(48) << "  "
              << humanBytes(static_cast<qint64>(
                     item.value(QStringLiteral("sizeBytes")).toDouble()))
                     .rightJustified(10)
              << "  " << (inUse ? QStringLiteral("em uso · não será removido")
                                : QStringLiteral("sem uso · será removido"))
              << '\n';
    }
    if (volumes.isEmpty()) {
        out() << "  (nenhum volume)\n";
    }

    out() << "TOTAL recuperável: "
          << humanBytes(static_cast<qint64>(
                 result.value(QStringLiteral("totalReclaimableBytes")).toDouble()))
          << '\n';
    out().flush();
    return kExitOk;
}

int commandMachine(const QStringList &args)
{
    if (args.isEmpty()) {
        printMachineUsage(err());
        return kExitUsage;
    }

    const QString subcommand = args.first();
    if (subcommand == QStringLiteral("--help") || subcommand == QStringLiteral("-h")
        || subcommand == QStringLiteral("help")) {
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
    if (subcommand == QStringLiteral("create")) {
        return machineCreateCommand(args);
    }
    if (subcommand == QStringLiteral("start") || subcommand == QStringLiteral("stop")
        || subcommand == QStringLiteral("restart")) {
        return machineLifecycleCommand(subcommand, args);
    }
    if (subcommand == QStringLiteral("delete") || subcommand == QStringLiteral("rm")) {
        return machineDeleteCommand(args);
    }
    if (subcommand == QStringLiteral("ssh")) {
        return machineSshCommand(args.mid(1));
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
                          QStringLiteral("CACHE").leftJustified(7),
                          QStringLiteral("URL"));
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

int commandBuilds(const QStringList &args)
{
    const QString sub = args.isEmpty() ? QStringLiteral("ls") : args.first();

    if (sub == QStringLiteral("ls") || sub == QStringLiteral("list")) {
        PodStack::RpcClient client;
        PodStack::Error error;
        const QJsonObject result =
            client.call(QStringLiteral("builds.list"),
                        QJsonObject{{QStringLiteral("limit"), 50}}, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        struct Row {
            QString id;
            QString image;
            QString status;
            QString duration;
            QString started;
        };
        QVector<Row> rows;
        int imageWidth = 10;
        for (const QJsonValue &value : result.value(QStringLiteral("items")).toArray()) {
            const QJsonObject build = value.toObject();
            Row row;
            row.id = build.value(QStringLiteral("id")).toString();
            row.image = build.value(QStringLiteral("imageRef")).toString();
            row.status = build.value(QStringLiteral("status")).toString();
            row.duration = QString::number(build.value(QStringLiteral("durationMs")).toInt()
                                           / 1000.0, 'f', 1)
                           + QStringLiteral("s");
            row.started = build.value(QStringLiteral("startedAt")).toString();
            imageWidth = qMax(imageWidth, row.image.size());
            rows.append(row);
        }
        out() << QStringLiteral("%1  %2  %3  %4  %5\n")
                     .arg(QStringLiteral("ID").leftJustified(18))
                     .arg(QStringLiteral("IMAGE").leftJustified(imageWidth))
                     .arg(QStringLiteral("STATUS").leftJustified(10))
                     .arg(QStringLiteral("DURATION").leftJustified(9))
                     .arg(QStringLiteral("STARTED"));
        for (const Row &row : rows) {
            out() << QStringLiteral("%1  %2  %3  %4  %5\n")
                         .arg(row.id.leftJustified(18))
                         .arg(row.image.leftJustified(imageWidth))
                         .arg(row.status.leftJustified(10))
                         .arg(row.duration.leftJustified(9))
                         .arg(row.started);
        }
        out().flush();
        return kExitOk;
    }

    if (sub == QStringLiteral("rm") || sub == QStringLiteral("remove")) {
        if (args.size() < 2) {
            err() << "Uso: podstack builds rm <id>\n";
            return kExitUsage;
        }
        PodStack::RpcClient client;
        PodStack::Error error;
        client.call(QStringLiteral("builds.remove"),
                    QJsonObject{{QStringLiteral("id"), args.at(1)}}, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        out() << "build " << args.at(1) << " removido\n";
        out().flush();
        return kExitOk;
    }

    err() << "subcomando desconhecido: builds " << sub << '\n';
    return kExitUsage;
}

int commandSetup(const QStringList &args)
{
    PodStack::RpcClient client;
    PodStack::Error error;

    const auto printChecks = [](const QJsonArray &checks) {
        for (const QJsonValue &value : checks) {
            const QJsonObject check = value.toObject();
            const QString status = check.value(QStringLiteral("status")).toString();
            const QString version = check.value(QStringLiteral("version")).toString();
            const QString detail = check.value(QStringLiteral("detail")).toString();
            out() << QStringLiteral("%1  %2")
                         .arg(status.leftJustified(9))
                         .arg(check.value(QStringLiteral("id")).toString());
            if (!version.isEmpty()) {
                out() << QStringLiteral("  ") << version;
            }
            out() << QStringLiteral("  ") << detail << '\n';
        }
    };

    const bool removeDocker = args.contains(QStringLiteral("--remove-docker"));
    if (removeDocker) {
        if (!args.contains(QStringLiteral("--yes"))) {
            err() << "A remoção do Docker exige --yes e aviso explícito de perda de dados.\n";
            err().flush();
            return kExitUsage;
        }
        const QJsonObject result = client.call(
            QStringLiteral("setup.removeDocker"),
            QJsonObject{{QStringLiteral("confirm"), true}}, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        out() << "Job iniciado: " << result.value(QStringLiteral("jobId")).toString() << '\n';
        out().flush();
        return kExitOk;
    }

    QString action;
    const int applyIndex = args.indexOf(QStringLiteral("--apply"));
    if (applyIndex >= 0) {
        if (applyIndex + 1 < args.size() && !args.at(applyIndex + 1).startsWith(QStringLiteral("--"))) {
            action = args.at(applyIndex + 1);
        } else {
            action = QStringLiteral("podman");
        }
    }
    if (args.contains(QStringLiteral("--install-podman"))) {
        action = QStringLiteral("podman");
    } else if (args.contains(QStringLiteral("--install-qemu"))) {
        action = QStringLiteral("qemu");
    } else if (args.contains(QStringLiteral("--install-components"))) {
        action = QStringLiteral("components");
    } else if (args.contains(QStringLiteral("--enable-socket"))) {
        action = QStringLiteral("socket");
    }

    if (!action.isEmpty()) {
        QString type = action;
        if (action == QStringLiteral("podman")) {
            type = QStringLiteral("install-podman");
        } else if (action == QStringLiteral("qemu")) {
            type = QStringLiteral("install-qemu");
        } else if (action == QStringLiteral("components")) {
            type = QStringLiteral("install-components");
        } else if (action == QStringLiteral("socket")) {
            type = QStringLiteral("enable-socket");
        }

        QJsonObject params;
        params.insert(QStringLiteral("action"), type);
        if (type == QStringLiteral("install-components")
            && args.contains(QStringLiteral("--caddy"))) {
            params.insert(QStringLiteral("components"), QJsonArray{QStringLiteral("caddy")});
        }
        const QJsonObject result =
            client.call(QStringLiteral("setup.apply"), params, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        out() << "Job iniciado: " << result.value(QStringLiteral("jobId")).toString()
              << " (" << result.value(QStringLiteral("state")).toString() << ")\n";
        out().flush();
        return kExitOk;
    }

    if (args.contains(QStringLiteral("--validate"))) {
        const QJsonObject result = client.call(QStringLiteral("setup.validate"), {}, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        printChecks(result.value(QStringLiteral("checks")).toArray());
        out() << (result.value(QStringLiteral("ok")).toBool()
                      ? QStringLiteral("Validação OK.\n")
                      : QStringLiteral("Validação com pendências.\n"));
        out().flush();
        return result.value(QStringLiteral("ok")).toBool() ? kExitOk : 1;
    }

    if (args.contains(QStringLiteral("--check"))) {
        const QJsonObject result = client.call(QStringLiteral("setup.check"), {}, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        printChecks(result.value(QStringLiteral("checks")).toArray());
        out().flush();
        return kExitOk;
    }

    const QJsonObject status = client.call(QStringLiteral("setup.status"), {}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }
    out() << "Configuração concluída: "
          << (status.value(QStringLiteral("completed")).toBool() ? "sim" : "não") << '\n'
          << "Onboarding habilitado: "
          << (status.value(QStringLiteral("showOnboarding")).toBool() ? "sim" : "não") << '\n'
          << "Política do Docker: "
          << status.value(QStringLiteral("dockerPolicy")).toString() << '\n';
    printChecks(status.value(QStringLiteral("checks")).toArray());
    out().flush();
    return kExitOk;
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
    if (command == QStringLiteral("ssh")) {
        return machineSshCommand(args);
    }
    if (command == QStringLiteral("setup")) {
        return commandSetup(args);
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
    if (command == QStringLiteral("help") || command == QStringLiteral("--help")
        || command == QStringLiteral("-h")) {
        printUsage();
        return kExitOk;
    }

    err() << "comando desconhecido: " << command << '\n';
    printUsage();
    return kExitUsage;
}
