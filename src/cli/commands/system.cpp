// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/Internal.h"

namespace PodStack::CliInternal {

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
        if (status == QStringLiteral("ok") &&
            result.value(QStringLiteral("updateAvailable")).toBool()) {
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
        if (normalized != QStringLiteral("s") && normalized != QStringLiteral("sim") &&
            normalized != QStringLiteral("y") && normalized != QStringLiteral("yes")) {
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

int commandDoctor()
{
    // O check do socket roda no cliente: com o socket parado o daemon não
    // responde e o check antigo (dentro de system.doctor) nunca executava.
    const QString socketPath = PodStack::Paths::controlSocket();
    PodStack::DaemonBootstrap::Probe probe = PodStack::DaemonBootstrap::probe(socketPath);
    if (!probe.connectable) {
        const PodStack::DaemonBootstrap::Status status =
            PodStack::DaemonBootstrap::ensureRunning(socketPath);
        if (status == PodStack::DaemonBootstrap::Status::Started ||
            status == PodStack::DaemonBootstrap::Status::AlreadyRunning) {
            probe = PodStack::DaemonBootstrap::probe(socketPath);
        }
    }

    out() << (probe.connectable ? "[ok]   " : "[fail] ") << "daemon.socket - " << socketPath
          << (probe.socketExists ? "" : " (ausente)") << '\n';
    if (!probe.connectable) {
        if (probe.unitInstalled) {
            out() << "       ação: " << PodStack::DaemonBootstrap::manualRemedy() << '\n';
        } else {
            out() << "       ação: instale o pacote do PodStack (unit podstack.socket ausente)\n";
        }
        out().flush();
        return kExitEngineUnavailable;
    }

    PodStack::RpcClient client(socketPath);
    PodStack::Error error;
    const QJsonObject result = client.call(QStringLiteral("system.doctor"), {}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    bool allOk = true;
    const QJsonArray checks = result.value(QStringLiteral("checks")).toArray();
    for (const QJsonValue &value : checks) {
        const QJsonObject check = value.toObject();
        const QString id = check.value(QStringLiteral("id")).toString();
        // O daemon reporta o próprio socket; o check do cliente o substitui.
        if (id == QStringLiteral("daemon.socket")) {
            continue;
        }
        const bool ok = check.value(QStringLiteral("status")).toString() == QStringLiteral("ok");
        allOk = allOk && ok;
        out() << (ok ? "[ok]   " : "[fail] ") << id << " - "
              << check.value(QStringLiteral("detail")).toString() << '\n';
        const QString remediation = check.value(QStringLiteral("remediation")).toString();
        if (!remediation.isEmpty()) {
            out() << "       ação: " << remediation << '\n';
        }
    }
    out().flush();
    return allOk ? kExitOk : kExitError;
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
        } else if (arg == QStringLiteral("--export") || arg == QStringLiteral("--since") ||
                   arg == QStringLiteral("--level") || arg == QStringLiteral("--component")) {
            if (index + 1 >= args.size() || args.at(index + 1).startsWith(QStringLiteral("--"))) {
                err() << "opção sem valor: " << arg << '\n';
                err().flush();
                return kExitUsage;
            }
            const QString &value = args.at(++index);
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

    if (component.isEmpty() || component == QStringLiteral("helper") ||
        component == QStringLiteral("package")) {
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
                line.message =
                    PodStack::Redaction::text(object.value(QStringLiteral("message")).toString());
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
    if (sub == QStringLiteral("--help") || sub == QStringLiteral("-h") ||
        sub == QStringLiteral("help")) {
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
    const QJsonObject result =
        client.call(QStringLiteral("system.df"),
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
                     .arg(
                         humanBytes(static_cast<qint64>(
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
        flags.append(item.value(QStringLiteral("unused")).toBool() ? QStringLiteral("sem uso")
                                                                   : QStringLiteral("em uso"));
        out() << "  " << imageLabel(item).leftJustified(48) << "  "
              << humanBytes(static_cast<qint64>(item.value(QStringLiteral("sizeBytes")).toDouble()))
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
              << humanBytes(static_cast<qint64>(item.value(QStringLiteral("sizeBytes")).toDouble()))
                     .rightJustified(10)
              << "  " << item.value(QStringLiteral("status")).toString()
              << (item.value(QStringLiteral("running")).toBool() ? QStringLiteral(" (em execução)")
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
              << humanBytes(static_cast<qint64>(item.value(QStringLiteral("sizeBytes")).toDouble()))
                     .rightJustified(10)
              << "  "
              << (inUse ? QStringLiteral("em uso · não será removido")
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

} // namespace PodStack::CliInternal
