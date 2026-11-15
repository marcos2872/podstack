// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/Internal.h"

namespace PodStack::CliInternal {

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

} // namespace PodStack::CliInternal
