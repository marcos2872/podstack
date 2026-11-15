// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/Internal.h"

namespace PodStack::CliInternal {

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
            row.size =
                humanBytes(static_cast<qint64>(volume.value(QStringLiteral("size")).toDouble()));
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
        const QJsonObject result = client.call(
            QStringLiteral("volumes.create"),
            QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("driver"), driver}},
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
                    QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("force"), force}},
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
        const QJsonObject result = client.call(QStringLiteral("volumes.prune"), {}, &error, 600000);
        if (!error.isEmpty()) {
            return fail(error);
        }
        out() << "liberado "
              << humanBytes(
                     static_cast<qint64>(result.value(QStringLiteral("reclaimedBytes")).toDouble()))
              << '\n';
        out().flush();
        return kExitOk;
    }

    if (sub == QStringLiteral("clone") || sub == QStringLiteral("export") ||
        sub == QStringLiteral("import") || sub == QStringLiteral("import-host")) {
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
        client.call(sub == QStringLiteral("import-host") ? QStringLiteral("volumes.importHostPath")
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

} // namespace PodStack::CliInternal
