// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/Internal.h"

namespace PodStack::CliInternal {



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

} // namespace PodStack::CliInternal
