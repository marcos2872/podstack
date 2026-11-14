// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/Internal.h"

namespace PodStack::CliInternal {

int commandCompose(const QStringList &args)
{
    const QString sub = args.isEmpty() ? QStringLiteral("ls") : args.first();

    if (sub == QStringLiteral("ls") || sub == QStringLiteral("list")) {
        PodStack::RpcClient client;
        PodStack::Error error;
        const QJsonObject result = client.call(QStringLiteral("compose.projects.list"), {}, &error);
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
            for (const QJsonValue &service : project.value(QStringLiteral("services")).toArray()) {
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

    if (sub == QStringLiteral("up") || sub == QStringLiteral("down") ||
        sub == QStringLiteral("restart") || sub == QStringLiteral("stop") ||
        sub == QStringLiteral("start")) {
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
            err() << "Uso: podstack compose " << sub << " <diretório> [serviço] [--volumes]\n";
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

} // namespace PodStack::CliInternal
