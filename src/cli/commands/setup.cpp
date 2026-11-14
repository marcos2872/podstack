// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/Internal.h"

namespace PodStack::CliInternal {



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

} // namespace PodStack::CliInternal
