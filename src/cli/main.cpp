#include "core/Error.h"
#include "core/LogReader.h"
#include "core/Logging.h"
#include "core/Paths.h"
#include "core/Redaction.h"
#include "core/RpcClient.h"
#include "core/RpcStream.h"
#include "core/UpdateRunner.h"
#include "daemon/HelperClient.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTextStream>
#include <QThread>
#include <QVector>

#include <cmath>
#include <iostream>
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
constexpr int kExitOutdated = 3;
constexpr int kExitEngineUnavailable = 7;
constexpr int kExitCancelled = 9;

class DbusUpdateHelper : public PodStack::UpdateHelper {
public:
    bool call(const QString &method, const QVariantList &arguments, QVariantMap *reply,
              PodStack::Error *error) override
    {
        PodStack::HelperClient client;
        QString helperError;
        QVariantMap values;
        const bool ok = client.callMap(method, arguments, &values, &helperError);
        if (reply) {
            *reply = values;
        }
        if (ok) {
            return true;
        }
        if (error) {
            error->code = values.value(QStringLiteral("code")).toString();
            if (error->code.isEmpty()) {
                error->code = QStringLiteral("PODSTACK_HELPER_REQUIRED");
            }
            error->message = values.value(QStringLiteral("message")).toString();
            if (error->message.isEmpty()) {
                error->message = helperError;
            }
            error->detail = values.value(QStringLiteral("detail")).toString();
        }
        return false;
    }
};

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
          << "  checkpoint <id> [--leave-running] [--tcp-established] [--json]\n"
          << "                          Cria um checkpoint CRIU do container\n"
          << "  restore <id> [--tcp-established] [--json]\n"
          << "                          Restaura o checkpoint CRIU do container\n"
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
          << "        [--platform p1,p2]\n"
          << "                          Build de imagem\n"
          << "  builds [ls|rm]        Histórico de builds\n"
          << "  machine "
             "<list|info|logs|console|create|start|stop|restart|delete|rename|clone|default|"
             "distros|ssh|files|pull|push|port|snapshot|update>\n"
          << "                          Gerencia machines QEMU/KVM\n"
          << "  kube <info|env> [--cluster NOME]\n"
          << "                          Cluster Kubernetes e KUBECONFIG\n"
          << "  ssh [id|nome] [--x11|--x11-trusted] [-- cmd...]\n"
          << "                          Abre SSH (sem id usa a machine default)\n"
          << "  top [--once] [--interval S] [--json] [--plain]\n"
          << "                          Monitora CPU/memória/containers\n"
          << "  system df [--detailed] [--json]\n"
          << "                          Uso de disco e prévia de prune\n"
          << "  system disk [--json]    Espaço livre e aviso de disco baixo\n"
          << "  system emulation [--json]\n"
          << "  system logs [--follow|--since 7d|--level|--component|--json|--export|--clear]\n"
          << "                          Arquitetura do host e handlers binfmt/FEX\n"
          << "  setup [--check|--status|--validate]\n"
          << "        [--apply podman|qemu|components|socket]\n"
          << "        [--docker-impact [--json]]\n"
          << "        [--remove-docker --yes]\n"
          << "                          Setup/onboarding da máquina\n"
          << "  config <show|get|set|add|remove|reset|schema> [args]\n"
          << "                          Lê e altera configurações\n"
          << "  doctor                  Diagnóstico rápido\n"
          << "  update --check [--json] [--channel stable|beta]\n"
          << "  update --install [--yes] [--json] [--channel stable|beta]\n"
          << "                          Verifica updates assinados (exit 3 se houver)\n"
          << "  report [--include-logs] [--json]\n"
          << "                          Relatório redigido para suporte\n"
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

int commandReport(const QStringList &args)
{
    bool includeLogs = false;
    bool asJson = false;
    for (const QString &argument : args) {
        if (argument == QStringLiteral("--include-logs")) {
            includeLogs = true;
        } else if (argument == QStringLiteral("--json")) {
            asJson = true;
        } else {
            err() << "Opção desconhecida para report: " << argument << '\n';
            return kExitUsage;
        }
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result =
        client.call(QStringLiteral("system.report"),
                    QJsonObject{{QStringLiteral("includeLogs"), includeLogs}}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    const QString path = result.value(QStringLiteral("path")).toString();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        err() << "Não foi possível ler o report: " << path << '\n';
        return kExitError;
    }
    const QByteArray payload = file.readAll();
    file.close();

    if (asJson) {
        out() << QString::fromUtf8(payload);
        if (!payload.endsWith('\n')) {
            out() << '\n';
        }
        out().flush();
        return kExitOk;
    }

    const QJsonObject report = QJsonDocument::fromJson(payload).object();
    out() << "Relatório PodStack — revise antes de compartilhar.\n";
    out() << "arquivo: " << path << '\n';
    out() << "sha256: " << result.value(QStringLiteral("sha256")).toString() << '\n';
    out() << "versão: " << report.value(QStringLiteral("version")).toString() << '\n';
    out() << "plataforma: " << report.value(QStringLiteral("platform")).toString() << '\n';
    out() << "kernel: " << report.value(QStringLiteral("kernel")).toString() << '\n';
    out() << "qt: " << report.value(QStringLiteral("qt")).toString() << '\n';
    out() << "socket: " << report.value(QStringLiteral("controlSocket")).toString() << '\n';

    const QJsonObject settings = report.value(QStringLiteral("settings")).toObject();
    out() << "\nconfiguração:\n";
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
        out() << "  " << it.key() << " = "
              << QString::fromUtf8(
                     QJsonDocument(QJsonArray{it.value()}).toJson(QJsonDocument::Compact))
              << '\n';
    }

    out() << "\ndiagnóstico:\n";
    const QJsonArray checks =
        report.value(QStringLiteral("doctor")).toObject().value(QStringLiteral("checks")).toArray();
    for (const QJsonValue &value : checks) {
        const QJsonObject check = value.toObject();
        const bool ok = check.value(QStringLiteral("status")).toString() == QStringLiteral("ok");
        out() << (ok ? "  [ok]   " : "  [fail] ") << check.value(QStringLiteral("id")).toString()
              << " - " << check.value(QStringLiteral("detail")).toString() << '\n';
    }
    if (report.contains(QStringLiteral("logs"))) {
        out() << "\nlogs incluídos: " << report.value(QStringLiteral("logs")).toArray().size()
              << " linhas\n";
    }
    out().flush();
    return kExitOk;
}

int commandUpdate(const QStringList &args)
{
    bool check = false;
    bool install = false;
    bool yes = false;
    bool json = false;
    bool runnerMode = false;
    bool resumeMode = false;
    bool clearState = false;
    QString channel;
    QString manifestPath;

    for (int index = 0; index < args.size(); ++index) {
        const QString &arg = args.at(index);
        if (arg == QStringLiteral("--check")) {
            check = true;
        } else if (arg == QStringLiteral("--install")) {
            install = true;
        } else if (arg == QStringLiteral("--yes") || arg == QStringLiteral("-y")) {
            yes = true;
        } else if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (arg == QStringLiteral("--runner")) {
            runnerMode = true;
        } else if (arg == QStringLiteral("--resume")) {
            resumeMode = true;
        } else if (arg == QStringLiteral("--clear-state")) {
            clearState = true;
        } else if (arg == QStringLiteral("--channel") || arg == QStringLiteral("--manifest")) {
            if (index + 1 >= args.size() || args.at(index + 1).startsWith(QStringLiteral("--"))) {
                err() << "Uso: podstack update --check|--install [--yes] [--json] "
                         "[--channel stable|beta]\n";
                err().flush();
                return kExitUsage;
            }
            if (arg == QStringLiteral("--channel")) {
                channel = args.at(++index);
            } else {
                manifestPath = args.at(++index);
            }
        } else {
            err() << "opção desconhecida para update: " << arg << '\n';
            err().flush();
            return kExitUsage;
        }
    }

    if (check == install) {
        err() << "Uso: podstack update --check|--install [--yes] [--json] "
                 "[--channel stable|beta]\n";
        err().flush();
        return kExitUsage;
    }
    if (check && (resumeMode || clearState)) {
        err() << "--resume/--clear-state são exclusivos de --install.\n";
        err().flush();
        return kExitUsage;
    }

    if (check) {
        QJsonObject params;
        params.insert(QStringLiteral("force"), true);
        if (!channel.isEmpty()) {
            params.insert(QStringLiteral("channel"), channel);
        }

        PodStack::RpcClient client;
        PodStack::Error error;
        const QJsonObject result = client.call(QStringLiteral("updates.check"), params, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }

        const QString status = result.value(QStringLiteral("status")).toString();
        if (json) {
            QJsonObject document = result;
            document.insert(QStringLiteral("api"), 1);
            out() << QJsonDocument(document).toJson(QJsonDocument::Indented);
            out().flush();
        } else if (status == QStringLiteral("ok")) {
            const QString current = result.value(QStringLiteral("currentVersion")).toString();
            const QString latest = result.value(QStringLiteral("latestVersion")).toString();
            if (result.value(QStringLiteral("updateAvailable")).toBool()) {
                out() << QStringLiteral("PodStack %1 disponível (canal %2); atual: %3.\n")
                             .arg(latest, result.value(QStringLiteral("channel")).toString(),
                                  current);
                const QString url = result.value(QStringLiteral("releaseUrl")).toString();
                if (!url.isEmpty()) {
                    out() << url << '\n';
                }
            } else {
                out() << QStringLiteral("PodStack %1 está atualizado (canal %2).\n")
                             .arg(current, result.value(QStringLiteral("channel")).toString());
            }
            out().flush();
        } else {
            const QString reason = result.value(QStringLiteral("reason")).toString();
            if (status == QStringLiteral("unavailable")) {
                err() << "Não foi possível verificar updates: " << reason << '\n';
                err().flush();
            } else {
                out() << (reason.isEmpty() ? QStringLiteral("Nenhum update disponível.\n")
                                           : reason + QStringLiteral("\n"));
                out().flush();
            }
        }

        if (status == QStringLiteral("unavailable")) {
            return kExitError;
        }
        if (status == QStringLiteral("ok")
            && result.value(QStringLiteral("updateAvailable")).toBool()) {
            return kExitOutdated;
        }
        return kExitOk;
    }

    DbusUpdateHelper helper;
    PodStack::UpdateRunnerOptions options;
    options.channel = channel;
    PodStack::UpdateRunner runner(&helper, options);
    PodStack::Error error;
    if (clearState) {
        QFile::remove(PodStack::Paths::stateDir() + QStringLiteral("/updates/state.json"));
        out() << QStringLiteral("Estado do update removido.\n");
        out().flush();
        return kExitOk;
    }
    if (!resumeMode && !yes && !runnerMode && !isatty(STDIN_FILENO)) {
        err() << "confirmação necessária: use --yes em modo não interativo.\n";
        err().flush();
        return kExitUsage;
    }
    bool prepared = false;
    if (resumeMode) {
        prepared = runner.resume(&error);
    } else {
        prepared = manifestPath.isEmpty() ? runner.prepare(&error)
                                          : runner.prepareFromManifest(manifestPath, &error);
    }
    if (!prepared) {
        QJsonObject document;
        document.insert(QStringLiteral("api"), 1);
        document.insert(QStringLiteral("command"), QStringLiteral("update-install"));
        document.insert(QStringLiteral("status"), QStringLiteral("failed"));
        QJsonObject jsonError;
        jsonError.insert(QStringLiteral("code"), error.code);
        jsonError.insert(QStringLiteral("message"), error.message);
        jsonError.insert(QStringLiteral("detail"), error.detail);
        document.insert(QStringLiteral("error"), jsonError);
        if (json || runnerMode) {
            out() << QJsonDocument(document).toJson(QJsonDocument::Indented);
        } else {
            err() << "Falha ao preparar o update: " << error.message;
            if (!error.detail.isEmpty()) {
                err() << " (" << error.detail << ')';
            }
            err() << '\n';
        }
        out().flush();
        err().flush();
        return PodStack::UpdateRunner::exitCodeFor(error);
    }

    if (!yes && !runnerMode) {
        if (!isatty(STDIN_FILENO)) {
            err() << "confirmação necessária: use --yes em modo não interativo.\n";
            err().flush();
            return kExitUsage;
        }
        out() << QStringLiteral("Instalar PodStack %1 e reiniciar os serviços? [s/N] ")
                     .arg(runner.version());
        out().flush();
        std::string answer;
        std::getline(std::cin, answer);
        const QString normalized = QString::fromStdString(answer).trimmed().toLower();
        if (normalized != QStringLiteral("s") && normalized != QStringLiteral("sim")
            && normalized != QStringLiteral("y") && normalized != QStringLiteral("yes")) {
            out() << QStringLiteral("cancelado\n");
            out().flush();
            return kExitCancelled;
        }
    }

    if (!json) {
        out() << QStringLiteral("Instalando PodStack %1...\n").arg(runner.version());
        out().flush();
    }
    const bool installed = resumeMode ? prepared : runner.install(&error);

    QJsonObject document;
    document.insert(QStringLiteral("api"), 1);
    document.insert(QStringLiteral("command"), QStringLiteral("update-install"));
    document.insert(QStringLiteral("status"),
                    installed ? QStringLiteral("ok") : QStringLiteral("failed"));
    document.insert(QStringLiteral("version"), runner.version());
    document.insert(QStringLiteral("phase"), runner.phase());
    document.insert(QStringLiteral("jobId"), runner.jobId());
    if (!installed) {
        QJsonObject jsonError;
        jsonError.insert(QStringLiteral("code"), error.code);
        jsonError.insert(QStringLiteral("message"), error.message);
        jsonError.insert(QStringLiteral("detail"), error.detail);
        document.insert(QStringLiteral("error"), jsonError);
    }
    if (json || runnerMode) {
        out() << QJsonDocument(document).toJson(QJsonDocument::Indented);
        out().flush();
    }
    if (installed) {
        if (!json) {
            out() << QStringLiteral("Update instalado; abra o PodStack novamente.\n");
            out().flush();
        }
        return kExitOk;
    }

    if (!json) {
        err() << "Falha ao instalar o update: " << error.message;
        if (!error.detail.isEmpty()) {
            err() << " (" << error.detail << ')';
        }
        err() << '\n';
        err().flush();
    }
    return PodStack::UpdateRunner::exitCodeFor(error);
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
    QStringList platforms;
    QStringList buildArgList;

    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i) == QStringLiteral("-t") && i + 1 < args.size()) {
            tag = args.at(++i);
        } else if (args.at(i) == QStringLiteral("-f") && i + 1 < args.size()) {
            dockerfile = args.at(++i);
        } else if (args.at(i) == QStringLiteral("--platform") && i + 1 < args.size()) {
            for (const QString &part : args.at(++i).split(QLatin1Char(','),
                                                           Qt::SkipEmptyParts)) {
                const QString platform = part.trimmed();
                if (!platform.isEmpty() && !platforms.contains(platform)) {
                    platforms.append(platform);
                }
            }
        } else if (args.at(i) == QStringLiteral("--build-arg") && i + 1 < args.size()) {
            buildArgList.append(args.at(++i));
        } else if (contextPath.isEmpty() && !args.at(i).startsWith(QStringLiteral("-"))) {
            contextPath = args.at(i);
        }
    }

    if (contextPath.isEmpty() || tag.isEmpty()) {
        err() << "Uso: podstack build <contexto> -t <tag> [-f Dockerfile] "
                 "[--build-arg K=V] [--platform p1,p2]\n";
        return kExitUsage;
    }

    QJsonObject buildArgs;
    for (const QString &item : buildArgList) {
        const int equals = item.indexOf(QLatin1Char('='));
        if (equals > 0) {
            buildArgs.insert(item.left(equals), item.mid(equals + 1));
        }
    }

    QJsonArray platformArray;
    for (const QString &platform : platforms) {
        platformArray.append(platform);
    }

    QJsonObject params;
    params.insert(QStringLiteral("contextPath"), contextPath);
    params.insert(QStringLiteral("dockerfile"), dockerfile);
    params.insert(QStringLiteral("tag"), tag);
    params.insert(QStringLiteral("buildArgs"), buildArgs);
    params.insert(QStringLiteral("platforms"), platformArray);
    if (platforms.size() == 1) {
        params.insert(QStringLiteral("platform"), platforms.first());
    }
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
           << "  console <id|nome> [--tail BYTES] [--no-follow] [--json]\n"
           << "                          Lê o console serial (follow por padrão; --json\n"
           << "                          só com --no-follow)\n"
           << "  create DISTRO[:VER] NOME [--cpus N] [--memory 4G] [--disk 20G]\n"
           << "         [--user U] [--user-data ARQUIVO] [--isolate-network]\n"
           << "         [--forward-ssh-agent] [--isolated]\n"
           << "                          Cria uma machine (a senha entra na T4-04)\n"
           << "  start <id|nome> [--json] Inicia uma machine\n"
           << "  stop <id|nome> [--force] [--json]\n"
           << "                          Para uma machine\n"
           << "  restart <id|nome> [--json]\n"
           << "                          Reinicia uma machine\n"
           << "  delete <id|nome> [--force] [--json]\n"
           << "                          Remove uma machine e seus dados\n"
           << "  rename <id|nome> <novo> [--json]\n"
           << "                          Renomeia uma machine (atualiza o SSH)\n"
           << "  clone <id|nome> <novo> [--json]\n"
           << "                          Cria uma cópia independente da machine\n"
           << "  update <id|nome> [--cpus N] [--memory 4G] [--disk 40G]\n"
           << "         [--forward-agent|--no-forward-agent] [--json]\n"
           << "                          Atualiza recursos ou o SSH agent\n"
           << "  port ls <id|nome> [--json]\n"
           << "                          Lista as portas encaminhadas da machine\n"
           << "  port add <id|nome> --host N --guest N [--udp] [--lan] [--label L]\n"
           << "                          Adiciona uma porta (reinicia se rodando)\n"
           << "  port rm <id|nome> <portId|hostPort> [--json]\n"
           << "                          Remove uma porta (a SSH é gerenciada)\n"
           << "  serial list <id|nome> [--json]\n"
           << "                          Lista portas seriais do host e as anexadas\n"
           << "  serial attach <id|nome> <porta> [--json]\n"
           << "                          Anexa uma porta serial (reinicia se rodando)\n"
           << "  serial detach <id|nome> <porta> [--json]\n"
           << "                          Desanexa uma porta serial\n"
           << "  usb list <id|nome> [--json]\n"
           << "                          Lista dispositivos USB do host e os anexados\n"
           << "  usb attach <id|nome> BUS:ADDR [--json]\n"
           << "                          Anexa um dispositivo USB (reinicia se rodando)\n"
           << "  usb detach <id|nome> BUS:ADDR [--json]\n"
           << "                          Desanexa um dispositivo USB\n"
           << "  audio <id|nome> on|off [--json]\n"
           << "                          Liga/desliga áudio PipeWire (reinicia se rodando)\n"
           << "  snapshot ls <id|nome> [--json]\n"
           << "                          Lista os snapshots internos do disco\n"
           << "  snapshot create <id|nome> <nome> [--json]\n"
           << "                          Cria um snapshot (para/reinicia se rodando)\n"
           << "  snapshot restore <id|nome> <nome> [--json]\n"
           << "                          Restaura o disco (substitui o estado atual)\n"
           << "  snapshot rm <id|nome> <nome> [--json]\n"
           << "                          Remove um snapshot do disco\n"
           << "  default [<id|nome>] [--json]\n"
           << "                          Mostra ou define a machine default\n"
           << "  ssh [id|nome] [--x11|--x11-trusted] [-- cmd...]\n"
           << "                          Abre SSH (sem id usa a machine default; -A se\n"
           << "                          o SSH agent estiver encaminhado; -X/-Y para X11)\n"
           << "  files ls <id|nome> [--path P] [--json]\n"
           << "                          Lista arquivos da machine via SSH\n"
           << "  pull <id|nome> <remoto> <local>\n"
           << "                          Baixa um arquivo/diretório da machine (scp)\n"
           << "  push <id|nome> <local> <remoto>\n"
           << "                          Envia um arquivo/diretório para a machine (scp)\n"
           << "  export <id|nome> <arquivo> [--force] [--json]\n"
           << "                          Exporta a machine para um .tar.zst (pausa)\n"
           << "  import <arquivo> [--name N] [--force] [--json]\n"
           << "                          Restaura uma machine de um .tar.zst\n"
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

    const QJsonObject params{{QStringLiteral("id"), id},
                             {QStringLiteral("tailBytes"), tailBytes}};

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
                const QJsonObject errorObject =
                    payload.value(QStringLiteral("error")).toObject();
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
                const QJsonObject errorObject =
                    payload.value(QStringLiteral("error")).toObject();
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
    const QJsonObject result =
        client.call(QStringLiteral("machines.rename"),
                    QJsonObject{{QStringLiteral("id"), id},
                                {QStringLiteral("name"), name}},
                    &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    out() << "machine " << result.value(QStringLiteral("id")).toString()
          << " renomeada para " << result.value(QStringLiteral("name")).toString() << '\n';
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
                const QJsonObject errorObject =
                    payload.value(QStringLiteral("error")).toObject();
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
        } else if (arg == QStringLiteral("--forward-agent")
                   || arg == QStringLiteral("--no-forward-agent")) {
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
    const QString display = name.isEmpty() ? result.value(QStringLiteral("id")).toString()
                                           : name;
    out() << "machine " << display << " atualizada: "
          << result.value(QStringLiteral("cpus")).toInt() << " CPUs, "
          << formatSize(result.value(QStringLiteral("memoryBytes")).toInteger()) << " RAM, "
          << formatSize(result.value(QStringLiteral("diskBytes")).toInteger()) << " disco ("
          << result.value(QStringLiteral("state")).toString() << ")\n";
    out().flush();
    return kExitOk;
}

bool machinePortIsSsh(const QJsonObject &port)
{
    if (port.value(QStringLiteral("guestPort")).toInt() != 22) {
        return false;
    }
    const QString protocol = port.value(QStringLiteral("protocol")).toString();
    return protocol.isEmpty() || protocol.compare(QStringLiteral("tcp"), Qt::CaseInsensitive) == 0;
}

QJsonArray machinePortsWithoutSsh(const QJsonArray &ports)
{
    QJsonArray result;
    for (const QJsonValue &value : ports) {
        const QJsonObject port = value.toObject();
        if (!machinePortIsSsh(port)) {
            result.append(port);
        }
    }
    return result;
}

QString machinePortLabel(const QJsonObject &machine)
{
    const QString name = machine.value(QStringLiteral("name")).toString();
    return name.isEmpty() ? machine.value(QStringLiteral("id")).toString() : name;
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
    const QJsonObject result =
        client.call(QStringLiteral("machines.ports.list"),
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
        hostWidth = qMax(hostWidth,
                         QString::number(port.value(QStringLiteral("hostPort")).toInt()).size());
        guestWidth = qMax(guestWidth,
                          QString::number(port.value(QStringLiteral("guestPort")).toInt()).size());
        protoWidth = qMax(protoWidth,
                          port.value(QStringLiteral("protocol")).toString().size());
        bindWidth = qMax(bindWidth,
                         (port.value(QStringLiteral("exposeLan")).toBool()
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
                      QStringLiteral("BIND").leftJustified(bindWidth),
                      QStringLiteral("LABEL"));
    for (const QJsonValue &value : ports) {
        const QJsonObject port = value.toObject();
        out() << QStringLiteral("%1  %2  %3  %4  %5  %6\n")
                     .arg(port.value(QStringLiteral("id")).toString().leftJustified(idWidth),
                          QString::number(port.value(QStringLiteral("hostPort")).toInt())
                              .leftJustified(hostWidth),
                          QString::number(port.value(QStringLiteral("guestPort")).toInt())
                              .leftJustified(guestWidth),
                          port.value(QStringLiteral("protocol")).toString()
                              .leftJustified(protoWidth),
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
    const QJsonObject listed =
        client.call(QStringLiteral("machines.ports.list"),
                    QJsonObject{{QStringLiteral("id"), id}}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    QJsonArray ports =
        machinePortsWithoutSsh(listed.value(QStringLiteral("items")).toArray());
    QJsonObject port;
    port.insert(QStringLiteral("hostPort"), hostPort);
    port.insert(QStringLiteral("guestPort"), guestPort);
    port.insert(QStringLiteral("protocol"),
                udp ? QStringLiteral("udp") : QStringLiteral("tcp"));
    port.insert(QStringLiteral("exposeLan"), exposeLan);
    port.insert(QStringLiteral("label"), label);
    ports.append(port);

    const QJsonObject result =
        client.call(QStringLiteral("machines.ports.set"),
                    QJsonObject{{QStringLiteral("id"), id},
                                {QStringLiteral("ports"), ports}},
                    &error, 180000);
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
    const QJsonObject listed =
        client.call(QStringLiteral("machines.ports.list"),
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
        if (port.value(QStringLiteral("id")).toString() == target
            || (numeric && port.value(QStringLiteral("hostPort")).toInt() == hostPort)) {
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
                    QStringLiteral("Mais de uma porta usa esse hostPort; use o ID."),
                    target));
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
                    QJsonObject{{QStringLiteral("id"), id},
                                {QStringLiteral("ports"), remaining}},
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
    if (args.isEmpty() || args.first() == QStringLiteral("--help")
        || args.first() == QStringLiteral("-h") || args.first() == QStringLiteral("help")) {
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
    const QJsonObject result =
        client.call(QStringLiteral("machines.snapshots.list"),
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
                    QJsonObject{{QStringLiteral("id"), id},
                                {QStringLiteral("name"), snapshotName}},
                    &error, 180000);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    out() << "snapshot " << result.value(QStringLiteral("name")).toString()
          << " criado na machine " << id << '\n';
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
                    QJsonObject{{QStringLiteral("id"), id},
                                {QStringLiteral("name"), snapshotName}},
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
                    QJsonObject{{QStringLiteral("id"), id},
                                {QStringLiteral("name"), snapshotName}},
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
    if (args.isEmpty() || args.first() == QStringLiteral("--help")
        || args.first() == QStringLiteral("-h") || args.first() == QStringLiteral("help")) {
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
    const QJsonObject result =
        client.call(QStringLiteral("machines.default"), params, &error);
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
    if (args.size() < 2 || args.at(1) == QStringLiteral("--help")
        || args.at(1) == QStringLiteral("-h") || args.at(1) == QStringLiteral("help")) {
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
    const QJsonObject result =
        client.call(QStringLiteral("machines.files.list"),
                    QJsonObject{{QStringLiteral("id"), id},
                                {QStringLiteral("path"), path}},
                    &error);
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
                      QStringLiteral("MODIFICADO").leftJustified(17),
                      QStringLiteral("NOME"));
    const QJsonArray items = result.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const bool directory = item.value(QStringLiteral("directory")).toBool();
        out() << QStringLiteral("%1 %2 %3 %4\n")
                     .arg((directory ? QStringLiteral("dir") : QStringLiteral("arquivo"))
                              .leftJustified(8),
                          (directory ? QStringLiteral("-")
                                     : humanBytes(item.value(QStringLiteral("size"))
                                                      .toInteger()))
                              .leftJustified(10),
                          item.value(QStringLiteral("modified"))
                              .toString()
                              .leftJustified(17),
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
        out() << "baixado " << result.value(QStringLiteral("path")).toString(remote)
              << " -> "
              << result.value(QStringLiteral("destination")).toString(local) << '\n';
    } else {
        out() << "enviado " << result.value(QStringLiteral("sourcePath")).toString(local)
              << " -> " << result.value(QStringLiteral("path")).toString(remote) << '\n';
    }
    out().flush();
    return kExitOk;
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

int levelRank(const QString &level)
{
    if (level == QStringLiteral("error")) {
        return 0;
    }
    if (level == QStringLiteral("warn")) {
        return 1;
    }
    if (level == QStringLiteral("debug")) {
        return 3;
    }
    if (level == QStringLiteral("trace")) {
        return 4;
    }
    return 2;
}

bool validLogLevel(const QString &level)
{
    return level == QStringLiteral("error") || level == QStringLiteral("warn")
           || level == QStringLiteral("info") || level == QStringLiteral("debug")
           || level == QStringLiteral("trace");
}

bool validLogComponent(const QString &component)
{
    static const QStringList components{
        QStringLiteral("daemon"), QStringLiteral("gui"),    QStringLiteral("cli"),
        QStringLiteral("qml"),    QStringLiteral("setup"),  QStringLiteral("update"),
        QStringLiteral("helper"), QStringLiteral("package")};
    return components.contains(component);
}

QDate parseSince(const QString &text)
{
    const QString value = text.trimmed().toLower();
    if (value.endsWith(QLatin1Char('d'))) {
        bool ok = false;
        const int days = value.chopped(1).toInt(&ok);
        if (ok && days > 0 && days <= 30) {
            return QDate::currentDate().addDays(-(days - 1));
        }
        return {};
    }
    return QDate::fromString(value.left(10), Qt::ISODate);
}

int commandSystemLogs(const QStringList &args)
{
    bool json = false;
    bool follow = false;
    bool clear = false;
    QString exportPath;
    QString sinceText;
    QString level;
    QString component;

    for (int index = 1; index < args.size(); ++index) {
        const QString &arg = args.at(index);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (arg == QStringLiteral("--follow")) {
            follow = true;
        } else if (arg == QStringLiteral("--clear")) {
            clear = true;
        } else if (arg == QStringLiteral("--export") || arg == QStringLiteral("--since")
                   || arg == QStringLiteral("--level") || arg == QStringLiteral("--component")) {
            if (index + 1 >= args.size() || args.at(index + 1).startsWith(QStringLiteral("--"))) {
                err() << "opção sem valor: " << arg << '\n';
                err().flush();
                return kExitUsage;
            }
            const QString value = args.at(++index);
            if (arg == QStringLiteral("--export")) {
                exportPath = value;
            } else if (arg == QStringLiteral("--since")) {
                sinceText = value;
            } else if (arg == QStringLiteral("--level")) {
                level = value;
            } else {
                component = value;
            }
        } else {
            err() << "opção desconhecida para system logs: " << arg << '\n';
            err().flush();
            return kExitUsage;
        }
    }

    if (!level.isEmpty() && !validLogLevel(level)) {
        err() << "nível inválido: " << level << '\n';
        err().flush();
        return kExitUsage;
    }
    if (!component.isEmpty() && !validLogComponent(component)) {
        err() << "componente inválido: " << component << '\n';
        err().flush();
        return kExitUsage;
    }
    if (follow && json) {
        err() << "--follow não combina com --json.\n";
        err().flush();
        return kExitUsage;
    }

    if (clear) {
        PodStack::Error error;
        if (!PodStack::clearUserLogs(&error)) {
            return fail(error);
        }
        out() << QStringLiteral("Logs do usuário removidos.\n");
        out().flush();
        return kExitOk;
    }

    PodStack::LogReadOptions options;
    options.level = level;
    options.component = component;
    options.since = parseSince(sinceText);
    if (!sinceText.isEmpty() && !options.since.isValid()) {
        err() << "data inválida para --since: " << sinceText << '\n';
        err().flush();
        return kExitUsage;
    }

    PodStack::LogReadResult result;
    PodStack::Error error;
    if (!PodStack::readUserLogs(options, &result, &error)) {
        return fail(error);
    }

    if (component.isEmpty() || component == QStringLiteral("helper")
        || component == QStringLiteral("package")) {
        PodStack::HelperClient helper;
        QVariantMap reply;
        QString helperError;
        if (helper.callMap(QStringLiteral("GetLogs"), {}, &reply, &helperError, 60000)) {
            const int requestedLevel = level.isEmpty() ? 4 : levelRank(level);
            for (const QVariant &rawValue : reply.value(QStringLiteral("lines")).toList()) {
                const QString raw = rawValue.toString();
                const QJsonObject object = QJsonDocument::fromJson(raw.toUtf8()).object();
                const QString helperComponent =
                    object.value(QStringLiteral("jobId")).toString().isEmpty()
                        ? QStringLiteral("helper")
                        : QStringLiteral("package");
                if (!component.isEmpty() && component != helperComponent) {
                    continue;
                }
                const QString lineLevel =
                    object.value(QStringLiteral("level")).toString(QStringLiteral("info"));
                if (levelRank(lineLevel) > requestedLevel) {
                    continue;
                }
                if (result.lines.size() >= options.maxLines) {
                    result.truncated = true;
                    break;
                }
                PodStack::LogLine line;
                line.ts = object.value(QStringLiteral("ts")).toString();
                line.level = lineLevel;
                line.component = helperComponent;
                line.message = PodStack::Redaction::text(
                    object.value(QStringLiteral("message")).toString());
                line.raw = PodStack::Redaction::text(raw);
                line.levelValue = levelRank(lineLevel);
                result.lines.append(line);
            }
            if (reply.value(QStringLiteral("truncated")).toBool()) {
                result.truncated = true;
            }
        }
    }

    if (!exportPath.isEmpty()) {
        if (!PodStack::exportLogs(result.lines, exportPath, &error)) {
            return fail(error);
        }
    }

    if (json) {
        QJsonArray lines;
        for (const PodStack::LogLine &line : result.lines) {
            QJsonObject object;
            object.insert(QStringLiteral("ts"), line.ts);
            object.insert(QStringLiteral("level"), line.level);
            object.insert(QStringLiteral("component"), line.component);
            object.insert(QStringLiteral("message"), line.message);
            lines.append(object);
        }
        QJsonObject document;
        document.insert(QStringLiteral("api"), 1);
        document.insert(QStringLiteral("lines"), lines);
        document.insert(QStringLiteral("truncated"), result.truncated);
        document.insert(QStringLiteral("files"), QJsonArray::fromStringList(result.files));
        out() << QJsonDocument(document).toJson(QJsonDocument::Indented);
        out().flush();
    } else if (!follow) {
        for (const PodStack::LogLine &line : result.lines) {
            out() << line.ts << QLatin1Char(' ') << QLatin1Char('[') << line.component
                  << QStringLiteral("] ") << line.level << QStringLiteral(": ") << line.message
                  << '\n';
        }
        out().flush();
    }

    if (!follow) {
        return kExitOk;
    }
    int lastCount = result.lines.size();
    for (;;) {
        QThread::msleep(500);
        PodStack::LogReadResult next;
        if (!PodStack::readUserLogs(options, &next, &error)) {
            return fail(error);
        }
        if (next.lines.size() < lastCount) {
            lastCount = 0;
        }
        for (int index = lastCount; index < next.lines.size(); ++index) {
            const PodStack::LogLine &line = next.lines.at(index);
            out() << line.ts << QLatin1Char(' ') << QLatin1Char('[') << line.component
                  << QStringLiteral("] ") << line.level << QStringLiteral(": ") << line.message
                  << '\n';
        }
        out().flush();
        lastCount = next.lines.size();
    }
}

int commandSystem(const QStringList &args)
{
    if (args.isEmpty()) {
        err() << "Uso: podstack system df [--detailed] [--json]\n"
              << "     podstack system disk [--json]\n";
        return kExitUsage;
    }
    const QString &sub = args.first();
    if (sub == QStringLiteral("--help") || sub == QStringLiteral("-h")
        || sub == QStringLiteral("help")) {
        out() << "Uso: podstack system df [--detailed] [--json]\n"
              << "     podstack system disk [--json]\n";
        out().flush();
        return kExitOk;
    }
    if (sub == QStringLiteral("logs")) {
        return commandSystemLogs(args);
    }
    if (sub == QStringLiteral("emulation")) {
        const bool json = args.contains(QStringLiteral("--json"));
        PodStack::RpcClient client;
        PodStack::Error error;
        const QJsonObject result = client.call(QStringLiteral("system.emulation"), {}, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        if (json) {
            out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
            out().flush();
            return kExitOk;
        }
        out() << "host: " << result.value(QStringLiteral("hostArch")).toString() << '\n';
        const QJsonArray handlers = result.value(QStringLiteral("handlers")).toArray();
        if (handlers.isEmpty()) {
            out() << "binfmt: nenhum handler qemu/FEX habilitado ("
                  << "instale qemu-user-static/binfmt)\n";
        } else {
            out() << "binfmt:\n";
            for (const QJsonValue &value : handlers) {
                const QJsonObject handler = value.toObject();
                out() << "  " << handler.value(QStringLiteral("name")).toString() << " -> "
                      << handler.value(QStringLiteral("interpreter")).toString() << '\n';
            }
        }
        out() << "emulação de arquitetura estrangeira: "
              << (result.value(QStringLiteral("foreignEmulation")).toBool() ? "sim" : "não")
              << '\n';
        out().flush();
        return kExitOk;
    }
    if (sub == QStringLiteral("disk")) {
        const bool json = args.contains(QStringLiteral("--json"));
        PodStack::RpcClient client;
        PodStack::Error error;
        const QJsonObject result = client.call(QStringLiteral("system.disk"), {}, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        if (json) {
            out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
            out().flush();
            return kExitOk;
        }
        out() << QStringLiteral("%1  %2  %3  %4\n")
                     .arg(QStringLiteral("CAMINHO").leftJustified(28))
                     .arg(QStringLiteral("TIPO").leftJustified(8))
                     .arg(QStringLiteral("USO").rightJustified(7))
                     .arg(QStringLiteral("LIVRE").rightJustified(11));
        const QJsonArray filesystems = result.value(QStringLiteral("filesystems")).toArray();
        for (const QJsonValue &value : filesystems) {
            const QJsonObject item = value.toObject();
            out() << QStringLiteral("%1  %2  %3  %4\n")
                         .arg(item.value(QStringLiteral("path")).toString().leftJustified(28))
                         .arg(item.value(QStringLiteral("kind")).toString().leftJustified(8))
                         .arg(QStringLiteral("%1%")
                                  .arg(QString::number(
                                      item.value(QStringLiteral("usedPercent")).toDouble(), 'f', 1))
                                  .rightJustified(7))
                         .arg(humanBytes(
                                  static_cast<qint64>(
                                      item.value(QStringLiteral("availableBytes")).toDouble()))
                                  .rightJustified(11));
        }
        out() << (result.value(QStringLiteral("low")).toBool()
                      ? QStringLiteral("Disco baixo: sim (limite %1%).\n")
                            .arg(result.value(QStringLiteral("thresholdPercent")).toInt())
                      : QStringLiteral("Disco baixo: não (limite %1%).\n")
                            .arg(result.value(QStringLiteral("thresholdPercent")).toInt()));
        out().flush();
        return kExitOk;
    }
    if (sub != QStringLiteral("df")) {
        err() << "subcomando desconhecido: system " << sub << '\n';
        err() << "Uso: podstack system df [--detailed] [--json]\n"
              << "     podstack system disk [--json]\n";
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

void printKubeUsage(QTextStream &stream)
{
    stream << "Uso: podstack kube <comando> [opções]\n\n"
           << "Comandos:\n"
           << "  info [--cluster NOME] [--json]  Estado do cluster\n"
           << "  env [--cluster NOME]            Imprime export KUBECONFIG=...\n"
           << "  start                           Cria a VM e instala o k3s\n"
           << "  stop|disable [--force]          Remove a VM e o kubeconfig\n"
           << "  context [use <nome>]            Mostra ou troca o contexto\n";
    stream.flush();
}

int kubeStatus(const QString &cluster, QJsonObject *result, PodStack::Error *error)
{
    PodStack::RpcClient client;
    *result = client.call(QStringLiteral("k8s.status"),
                          QJsonObject{{QStringLiteral("cluster"), cluster}}, error);
    return error->isEmpty() ? kExitOk : fail(*error);
}

int kubeInfoCommand(const QStringList &args)
{
    QString cluster = QStringLiteral("default");
    bool json = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (arg == QStringLiteral("--cluster") && i + 1 < args.size()) {
            cluster = args.at(++i);
        } else if (arg.startsWith(QStringLiteral("--cluster="))) {
            cluster = arg.mid(QStringLiteral("--cluster=").size());
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }

    QJsonObject result;
    PodStack::Error error;
    const int code = kubeStatus(cluster, &result, &error);
    if (code != kExitOk) {
        return code;
    }
    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    out() << "cluster: " << result.value(QStringLiteral("name")).toString() << '\n'
          << "estado: " << result.value(QStringLiteral("state")).toString() << '\n';
    const QString context = result.value(QStringLiteral("contextName")).toString();
    if (!context.isEmpty()) {
        out() << "contexto: " << context << '\n';
    }
    const int apiPort = result.value(QStringLiteral("apiPort")).toInt();
    if (apiPort > 0) {
        out() << "api: 127.0.0.1:" << apiPort << '\n';
    }
    const QString kubeconfig = result.value(QStringLiteral("kubeconfigPath")).toString();
    if (!kubeconfig.isEmpty()) {
        out() << "kubeconfig: " << kubeconfig << '\n';
    }
    out().flush();
    return kExitOk;
}

int kubeEnvCommand(const QStringList &args)
{
    QString cluster = QStringLiteral("default");
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--cluster") && i + 1 < args.size()) {
            cluster = args.at(++i);
        } else if (arg.startsWith(QStringLiteral("--cluster="))) {
            cluster = arg.mid(QStringLiteral("--cluster=").size());
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }

    QJsonObject result;
    PodStack::Error error;
    const int code = kubeStatus(cluster, &result, &error);
    if (code != kExitOk) {
        return code;
    }
    const QString kubeconfig = result.value(QStringLiteral("kubeconfigPath")).toString();
    if (kubeconfig.isEmpty()) {
        const QString state = result.value(QStringLiteral("state")).toString();
        return fail(PodStack::Error::make(
            QStringLiteral("PODSTACK_NOT_FOUND"),
            QStringLiteral("O cluster Kubernetes ainda não tem kubeconfig."),
            QStringLiteral("estado: %1").arg(state),
            QStringLiteral("Habilite o cluster (k8s.enable) antes de usar kube env.")));
    }
    out() << "export KUBECONFIG=" << kubeconfig << '\n';
    out().flush();
    return kExitOk;
}

int kubeJobCommand(const QString &method, const QString &label, const QJsonObject &params)
{
    PodStack::RpcStream stream;
    PodStack::Error error;
    bool failed = false;
    QString failure;
    const bool ok = stream.run(
        method, params,
        [&](const QString &event, const QJsonObject &payload) {
            if (event.endsWith(QStringLiteral(".delta"))) {
                const QString stage = payload.value(QStringLiteral("stage")).toString();
                const int progress = payload.value(QStringLiteral("progress")).toInt();
                if (!stage.isEmpty()) {
                    out() << stage << ' ' << progress << "%\n";
                    out().flush();
                }
            } else if (event.endsWith(QStringLiteral(".done"))) {
                const QJsonObject errorObject = payload.value(QStringLiteral("error")).toObject();
                if (!errorObject.isEmpty()) {
                    failed = true;
                    failure = errorObject.value(QStringLiteral("message")).toString();
                }
            }
        },
        &error, 900000);
    if (!ok) {
        return fail(error);
    }
    if (failed) {
        return fail(PodStack::Error::make(QStringLiteral("PODSTACK_IO_ERROR"), failure));
    }
    out() << label << "\n";
    out().flush();
    return kExitOk;
}

int kubeStartCommand()
{
    return kubeJobCommand(QStringLiteral("k8s.enable"), QStringLiteral("cluster pronto"),
                          QJsonObject{});
}

int kubeDisableCommand(const QStringList &args)
{
    bool force = false;
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QStringLiteral("--force")) {
            force = true;
        } else {
            err() << "opção desconhecida: " << args.at(i) << '\n';
            return kExitUsage;
        }
    }
    return kubeJobCommand(QStringLiteral("k8s.disable"), QStringLiteral("cluster removido"),
                          QJsonObject{{QStringLiteral("force"), force}});
}

int kubeContextCommand(const QStringList &args)
{
    if (args.size() >= 3 && args.at(1) == QStringLiteral("use")) {
        PodStack::RpcClient client;
        PodStack::Error error;
        const QJsonObject result =
            client.call(QStringLiteral("k8s.context.use"),
                        QJsonObject{{QStringLiteral("context"), args.at(2)}}, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        out() << "contexto: " << result.value(QStringLiteral("context")).toString() << '\n';
        out().flush();
        return kExitOk;
    }
    if (args.size() != 1) {
        err() << "Uso: podstack kube context [use <nome>]\n";
        return kExitUsage;
    }

    QJsonObject status;
    PodStack::Error error;
    const int code = kubeStatus(QStringLiteral("default"), &status, &error);
    if (code != kExitOk) {
        return code;
    }
    const QString path = status.value(QStringLiteral("kubeconfigPath")).toString();
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        err() << "kubeconfig indisponível\n";
        return kExitError;
    }
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("current-context:"))) {
            out() << line.mid(QStringLiteral("current-context:").size()).trimmed() << '\n';
            out().flush();
            return kExitOk;
        }
    }
    err() << "current-context não encontrado\n";
    return kExitError;
}

int commandKube(const QStringList &args)
{
    if (args.isEmpty()) {
        printKubeUsage(err());
        return kExitUsage;
    }
    const QString &subcommand = args.first();
    if (subcommand == QStringLiteral("--help") || subcommand == QStringLiteral("-h") ||
        subcommand == QStringLiteral("help")) {
        printKubeUsage(out());
        return kExitOk;
    }
    if (subcommand == QStringLiteral("info")) {
        return kubeInfoCommand(args);
    }
    if (subcommand == QStringLiteral("env")) {
        return kubeEnvCommand(args);
    }
    if (subcommand == QStringLiteral("start")) {
        return kubeStartCommand();
    }
    if (subcommand == QStringLiteral("stop") || subcommand == QStringLiteral("disable")) {
        return kubeDisableCommand(args);
    }
    if (subcommand == QStringLiteral("context")) {
        return kubeContextCommand(args);
    }
    err() << "comando kube desconhecido: " << subcommand << '\n';
    printKubeUsage(err());
    return kExitUsage;
}

int commandMachine(const QStringList &args)
{
    if (args.isEmpty()) {
        printMachineUsage(err());
        return kExitUsage;
    }

    const QString &subcommand = args.first();
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
    if (subcommand == QStringLiteral("console")) {
        return machineConsoleCommand(args);
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
            QString platforms;
        };
        QVector<Row> rows;
        int imageWidth = 10;
        int platformWidth = 9;
        bool hasPlatforms = false;
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
            QStringList platforms;
            for (const QJsonValue &platform :
                 build.value(QStringLiteral("platforms")).toArray()) {
                const QString value = platform.toString();
                if (!value.isEmpty()) {
                    platforms.append(value);
                }
            }
            row.platforms = platforms.join(QLatin1Char(','));
            imageWidth = qMax(imageWidth, row.image.size());
            platformWidth = qMax(platformWidth, row.platforms.size());
            hasPlatforms = hasPlatforms || !row.platforms.isEmpty();
            rows.append(row);
        }
        if (hasPlatforms) {
            out() << QStringLiteral("%1  %2  %3  %4  %5  %6\n")
                         .arg(QStringLiteral("ID").leftJustified(18))
                         .arg(QStringLiteral("IMAGE").leftJustified(imageWidth))
                         .arg(QStringLiteral("STATUS").leftJustified(10))
                         .arg(QStringLiteral("DURATION").leftJustified(9))
                         .arg(QStringLiteral("STARTED").leftJustified(24))
                         .arg(QStringLiteral("PLATFORMS"));
        } else {
            out() << QStringLiteral("%1  %2  %3  %4  %5\n")
                         .arg(QStringLiteral("ID").leftJustified(18))
                         .arg(QStringLiteral("IMAGE").leftJustified(imageWidth))
                         .arg(QStringLiteral("STATUS").leftJustified(10))
                         .arg(QStringLiteral("DURATION").leftJustified(9))
                         .arg(QStringLiteral("STARTED"));
        }
        for (const Row &row : rows) {
            if (hasPlatforms) {
                out() << QStringLiteral("%1  %2  %3  %4  %5  %6\n")
                             .arg(row.id.leftJustified(18))
                             .arg(row.image.leftJustified(imageWidth))
                             .arg(row.status.leftJustified(10))
                             .arg(row.duration.leftJustified(9))
                             .arg(row.started.leftJustified(24))
                             .arg(row.platforms.leftJustified(platformWidth));
            } else {
                out() << QStringLiteral("%1  %2  %3  %4  %5\n")
                             .arg(row.id.leftJustified(18))
                             .arg(row.image.leftJustified(imageWidth))
                             .arg(row.status.leftJustified(10))
                             .arg(row.duration.leftJustified(9))
                             .arg(row.started);
            }
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

    const auto printDockerImpact = [](const QJsonObject &impact) {
        const QJsonObject containers = impact.value(QStringLiteral("containers")).toObject();
        out() << QStringLiteral("Impacto do Docker: %1 containers (%2 em execução), "
                                "%3 imagens, %4 redes e %5 volumes.\n")
                     .arg(containers.value(QStringLiteral("total")).toInt())
                     .arg(containers.value(QStringLiteral("running")).toInt())
                     .arg(impact.value(QStringLiteral("images")).toInt())
                     .arg(impact.value(QStringLiteral("networks")).toInt())
                     .arg(impact.value(QStringLiteral("volumes")).toInt());
    };

    const bool dockerImpactFlag = args.contains(QStringLiteral("--docker-impact"));
    const bool removeDocker = args.contains(QStringLiteral("--remove-docker"));
    if (dockerImpactFlag) {
        const QJsonObject impact = client.call(QStringLiteral("setup.dockerImpact"), {}, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        if (args.contains(QStringLiteral("--json"))) {
            QJsonObject json = impact;
            json.insert(QStringLiteral("api"), 1);
            out() << QJsonDocument(json).toJson(QJsonDocument::Indented);
        } else if (impact.value(QStringLiteral("available")).toBool()) {
            printDockerImpact(impact);
        } else {
            out() << "Inventário do Docker indisponível: "
                  << impact.value(QStringLiteral("error")).toString() << '\n';
        }
        out().flush();
        return kExitOk;
    }

    if (removeDocker) {
        if (!args.contains(QStringLiteral("--yes"))) {
            err() << "A remoção do Docker exige --yes e aviso explícito de perda de dados.\n";
            err().flush();
            return kExitUsage;
        }
        PodStack::Error impactError;
        const QJsonObject impact =
            client.call(QStringLiteral("setup.dockerImpact"), {}, &impactError);
        if (impactError.isEmpty() && impact.value(QStringLiteral("available")).toBool()) {
            printDockerImpact(impact);
            out().flush();
        }
        const QJsonObject result =
            client.call(QStringLiteral("setup.removeDocker"),
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

void printConfigUsage(QTextStream &stream)
{
    stream << "Uso: podstack config <comando> [opções]\n\n"
           << "Comandos:\n"
           << "  show [--json]           Lista chaves e valores\n"
           << "  get <chave> [--json]    Mostra uma chave\n"
           << "  set <chave> <valor>     Define uma chave (valida tipo/faixa/enum)\n"
           << "  add <chave> <valor>     Adiciona item a uma lista\n"
           << "  remove <chave> <valor>  Remove item de uma lista\n"
           << "  reset [chave] [--yes]   Restaura uma chave ou todas\n"
           << "  schema [--json]         Mostra o schema (86 chaves)\n";
    stream.flush();
}

QString formatConfigValue(const QJsonValue &value)
{
    if (value.isNull() || value.isUndefined()) {
        return QStringLiteral("null");
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isDouble()) {
        const double raw = value.toDouble();
        if (std::floor(raw) == raw) {
            return QString::number(static_cast<qint64>(raw));
        }
        return QString::number(raw);
    }
    if (value.isArray()) {
        QStringList entries;
        for (const QJsonValue &entry : value.toArray()) {
            entries.append(entry.toString());
        }
        return entries.join(QLatin1Char(','));
    }
    if (value.isObject()) {
        return QString::fromUtf8(
            QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    return value.toString();
}

bool fetchConfigSchema(QHash<QString, QJsonObject> *schema, PodStack::Error *error)
{
    PodStack::RpcClient client;
    const QJsonObject result =
        client.call(QStringLiteral("settings.schema"), {}, error);
    if (error && !error->isEmpty()) {
        return false;
    }
    schema->clear();
    for (const QJsonValue &value : result.value(QStringLiteral("schema")).toArray()) {
        const QJsonObject definition = value.toObject();
        const QString key = definition.value(QStringLiteral("key")).toString();
        if (!key.isEmpty()) {
            schema->insert(key, definition);
        }
    }
    return true;
}

bool parseConfigValue(const QJsonObject &definition, const QString &raw,
                      QJsonValue *value, QString *error)
{
    const QString type = definition.value(QStringLiteral("type")).toString();
    const QString trimmed = raw.trimmed();

    if (type == QStringLiteral("bool")) {
        const QString normalized = trimmed.toLower();
        if (normalized == QStringLiteral("true") || normalized == QStringLiteral("1")
            || normalized == QStringLiteral("sim") || normalized == QStringLiteral("yes")) {
            *value = true;
            return true;
        }
        if (normalized == QStringLiteral("false") || normalized == QStringLiteral("0")
            || normalized == QStringLiteral("não") || normalized == QStringLiteral("nao")
            || normalized == QStringLiteral("no")) {
            *value = false;
            return true;
        }
        *error = QStringLiteral("esperado true ou false.");
        return false;
    }

    if (type == QStringLiteral("int") || type == QStringLiteral("int64")) {
        bool ok = false;
        const qint64 number = trimmed.toLongLong(&ok);
        if (!ok) {
            *error = QStringLiteral("esperado um número inteiro.");
            return false;
        }
        *value = static_cast<double>(number);
        return true;
    }

    if (type == QStringLiteral("stringList")) {
        if (trimmed.startsWith(QLatin1Char('['))) {
            QJsonParseError parseError{};
            const QJsonDocument document =
                QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
                *error = QStringLiteral("lista JSON inválida.");
                return false;
            }
            *value = document.array();
            return true;
        }
        QJsonArray entries;
        const QStringList parts = trimmed.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            const QString entry = part.trimmed();
            if (!entry.isEmpty()) {
                entries.append(entry);
            }
        }
        *value = entries;
        return true;
    }

    if (type == QStringLiteral("timestamp")) {
        if (trimmed.isEmpty() || trimmed.compare(QStringLiteral("null"),
                                                 Qt::CaseInsensitive)
                                     == 0) {
            *value = QJsonValue::Null;
            return true;
        }
        *value = raw;
        return true;
    }

    if (type == QStringLiteral("string") || type == QStringLiteral("enum")) {
        const bool nullable = definition.value(QStringLiteral("nullable")).toBool();
        if (nullable && (trimmed.isEmpty()
                         || trimmed.compare(QStringLiteral("null"), Qt::CaseInsensitive)
                                == 0)) {
            *value = QJsonValue::Null;
            return true;
        }
        *value = raw;
        return true;
    }

    *error = QStringLiteral("tipo de schema desconhecido: %1.").arg(type);
    return false;
}

int commandConfigShow(const QStringList &args)
{
    bool json = false;
    for (const QString &arg : args) {
        if (arg == QStringLiteral("--json") || arg == QStringLiteral("--format=json")) {
            json = true;
        } else if (arg.startsWith(QStringLiteral("--format="))) {
            const QString format = arg.mid(9);
            if (format != QStringLiteral("json") && format != QStringLiteral("text")) {
                err() << "formato desconhecido: " << format << '\n';
                return kExitUsage;
            }
            json = format == QStringLiteral("json");
        } else if (arg == QStringLiteral("--format")) {
            // `show --format json` é tratado no laço abaixo.
        } else if (arg == QStringLiteral("text") || arg == QStringLiteral("json")) {
            json = arg == QStringLiteral("json");
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result = client.call(QStringLiteral("settings.get"), {}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    const QJsonObject values = result.value(QStringLiteral("values")).toObject();
    if (json) {
        out() << QJsonDocument(values).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        out() << it.key() << '=' << formatConfigValue(it.value()) << '\n';
    }
    out().flush();
    return kExitOk;
}

int commandConfigGet(const QStringList &args)
{
    QString key;
    bool json = false;
    for (const QString &arg : args) {
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (key.isEmpty() && !arg.startsWith(QStringLiteral("-"))) {
            key = arg;
        } else {
            err() << "argumento inesperado: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (key.isEmpty()) {
        err() << "Uso: podstack config get <chave> [--json]\n";
        return kExitUsage;
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result =
        client.call(QStringLiteral("settings.get"),
                    QJsonObject{{QStringLiteral("key"), key}}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }
    const QJsonObject values = result.value(QStringLiteral("values")).toObject();
    if (json) {
        out() << QJsonDocument(values).toJson(QJsonDocument::Indented);
    } else {
        out() << formatConfigValue(values.value(key)) << '\n';
    }
    out().flush();
    return kExitOk;
}

int commandConfigSet(const QStringList &args)
{
    if (args.size() != 2) {
        err() << "Uso: podstack config set <chave> <valor>\n";
        return kExitUsage;
    }
    const QString &key = args.at(0);
    const QString &raw = args.at(1);

    QHash<QString, QJsonObject> schema;
    PodStack::Error error;
    if (!fetchConfigSchema(&schema, &error)) {
        return fail(error);
    }
    if (!schema.contains(key)) {
        err() << "chave desconhecida: " << key << '\n';
        err() << "  ação: consulte `podstack config schema`.\n";
        err().flush();
        return kExitError;
    }

    QJsonValue value;
    QString parseError;
    if (!parseConfigValue(schema.value(key), raw, &value, &parseError)) {
        err() << "valor inválido para " << key << ": " << parseError << '\n';
        err().flush();
        return kExitUsage;
    }

    PodStack::RpcClient client;
    const QJsonObject result =
        client.call(QStringLiteral("settings.set"),
                    QJsonObject{{QStringLiteral("key"), key},
                                {QStringLiteral("value"), value}},
                    &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    const QJsonObject values = result.value(QStringLiteral("values")).toObject();
    out() << key << '=' << formatConfigValue(values.value(key)) << '\n';
    if (result.value(QStringLiteral("restartRequired")).toBool()) {
        out() << "reinício necessário: reinicie o podstackd para aplicar.\n";
    }
    out().flush();
    return kExitOk;
}

int commandConfigList(const QString &operation, const QStringList &args)
{
    if (args.size() != 2) {
        err() << "Uso: podstack config " << operation << " <chave> <valor>\n";
        return kExitUsage;
    }
    const QString &key = args.at(0);
    const QString entry = args.at(1).trimmed();
    if (entry.isEmpty()) {
        err() << "valor vazio não é permitido.\n";
        return kExitUsage;
    }

    QHash<QString, QJsonObject> schema;
    PodStack::Error error;
    if (!fetchConfigSchema(&schema, &error)) {
        return fail(error);
    }
    if (!schema.contains(key)) {
        err() << "chave desconhecida: " << key << '\n';
        err().flush();
        return kExitError;
    }
    if (schema.value(key).value(QStringLiteral("type")).toString()
        != QStringLiteral("stringList")) {
        err() << key << " não é uma lista de strings.\n";
        err().flush();
        return kExitError;
    }

    PodStack::RpcClient client;
    const QJsonObject current =
        client.call(QStringLiteral("settings.get"),
                    QJsonObject{{QStringLiteral("key"), key}}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }
    QJsonArray entries =
        current.value(QStringLiteral("values")).toObject().value(key).toArray();

    if (operation == QStringLiteral("add")) {
        bool present = false;
        for (const QJsonValue &value : entries) {
            if (value.toString() == entry) {
                present = true;
                break;
            }
        }
        if (!present) {
            entries.append(entry);
        }
    } else {
        QJsonArray filtered;
        bool removed = false;
        for (const QJsonValue &value : entries) {
            if (value.toString() == entry) {
                removed = true;
                continue;
            }
            filtered.append(value);
        }
        if (!removed) {
            err() << "valor não encontrado em " << key << ": " << entry << '\n';
            err().flush();
            return kExitError;
        }
        entries = filtered;
    }

    const QJsonObject result =
        client.call(QStringLiteral("settings.set"),
                    QJsonObject{{QStringLiteral("key"), key},
                                {QStringLiteral("value"), entries}},
                    &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    const QJsonObject values = result.value(QStringLiteral("values")).toObject();
    out() << key << '=' << formatConfigValue(values.value(key)) << '\n';
    if (result.value(QStringLiteral("restartRequired")).toBool()) {
        out() << "reinício necessário: reinicie o podstackd para aplicar.\n";
    }
    out().flush();
    return kExitOk;
}

int commandConfigReset(const QStringList &args)
{
    QString key;
    bool yes = false;
    for (const QString &arg : args) {
        if (arg == QStringLiteral("--yes") || arg == QStringLiteral("-y")) {
            yes = true;
        } else if (arg == QStringLiteral("--all")) {
            key.clear();
        } else if (key.isEmpty() && !arg.startsWith(QStringLiteral("-"))) {
            key = arg;
        } else {
            err() << "argumento inesperado: " << arg << '\n';
            return kExitUsage;
        }
    }

    if (!yes) {
        if (!isatty(STDIN_FILENO)) {
            err() << "confirmação necessária: use --yes em modo não interativo.\n";
            err().flush();
            return kExitUsage;
        }
        out() << (key.isEmpty()
                      ? QStringLiteral("Restaurar todas as configurações? [s/N] ")
                      : QStringLiteral("Restaurar %1 para o padrão? [s/N] ").arg(key));
        out().flush();
        std::string answer;
        std::getline(std::cin, answer);
        QString normalized = QString::fromStdString(answer).trimmed().toLower();
        if (normalized != QStringLiteral("s") && normalized != QStringLiteral("sim")
            && normalized != QStringLiteral("y") && normalized != QStringLiteral("yes")) {
            out() << "cancelado\n";
            out().flush();
            return kExitOk;
        }
    }

    QJsonObject params;
    if (!key.isEmpty()) {
        params.insert(QStringLiteral("key"), key);
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result =
        client.call(QStringLiteral("settings.reset"), params, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    out() << (key.isEmpty() ? QStringLiteral("todas as configurações restauradas")
                            : QStringLiteral("%1 restaurada").arg(key))
          << '\n';
    if (result.value(QStringLiteral("restartRequired")).toBool()) {
        out() << "reinício necessário: reinicie o podstackd para aplicar.\n";
    }
    out().flush();
    return kExitOk;
}

int commandConfigSchema(const QStringList &args)
{
    bool json = false;
    for (const QString &arg : args) {
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result =
        client.call(QStringLiteral("settings.schema"), {}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    for (const QJsonValue &value : result.value(QStringLiteral("schema")).toArray()) {
        const QJsonObject definition = value.toObject();
        out() << definition.value(QStringLiteral("key")).toString() << '\t'
              << definition.value(QStringLiteral("type")).toString() << '\t'
              << formatConfigValue(definition.value(QStringLiteral("default"))) << '\n';
    }
    out().flush();
    return kExitOk;
}

int commandConfig(const QStringList &args)
{
    const QString sub = args.isEmpty() ? QStringLiteral("show") : args.first();
    const QStringList rest = args.mid(1);

    if (sub == QStringLiteral("--help") || sub == QStringLiteral("-h")
        || sub == QStringLiteral("help")) {
        printConfigUsage(out());
        return kExitOk;
    }
    if (sub == QStringLiteral("show") || sub == QStringLiteral("ls")
        || sub == QStringLiteral("list")) {
        return commandConfigShow(rest);
    }
    if (sub == QStringLiteral("get")) {
        return commandConfigGet(rest);
    }
    if (sub == QStringLiteral("set")) {
        return commandConfigSet(rest);
    }
    if (sub == QStringLiteral("add")) {
        return commandConfigList(QStringLiteral("add"), rest);
    }
    if (sub == QStringLiteral("remove") || sub == QStringLiteral("rm")) {
        return commandConfigList(QStringLiteral("remove"), rest);
    }
    if (sub == QStringLiteral("reset")) {
        return commandConfigReset(rest);
    }
    if (sub == QStringLiteral("schema")) {
        return commandConfigSchema(rest);
    }

    err() << "subcomando desconhecido: config " << sub << '\n';
    printConfigUsage(err());
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
