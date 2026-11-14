// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/Internal.h"

namespace PodStack::CliInternal {

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

    if (sub == QStringLiteral("start") || sub == QStringLiteral("stop") ||
        sub == QStringLiteral("restart")) {
        if (args.size() < 2) {
            err() << "Uso: podstack pods " << sub << " <id|nome>\n";
            return kExitUsage;
        }
        PodStack::RpcClient client;
        PodStack::Error error;
        client.call(QStringLiteral("pods.") + sub, QJsonObject{{QStringLiteral("id"), args.at(1)}},
                    &error);
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
                    QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("force"), force}},
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

} // namespace PodStack::CliInternal
