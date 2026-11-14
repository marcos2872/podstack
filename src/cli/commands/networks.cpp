// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/Internal.h"

namespace PodStack::CliInternal {



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

} // namespace PodStack::CliInternal
