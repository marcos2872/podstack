// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/Internal.h"

namespace PodStack::CliInternal {

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
            for (const QString &part : args.at(++i).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
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

int commandBuilds(const QStringList &args)
{
    const QString sub = args.isEmpty() ? QStringLiteral("ls") : args.first();

    if (sub == QStringLiteral("ls") || sub == QStringLiteral("list")) {
        PodStack::RpcClient client;
        PodStack::Error error;
        const QJsonObject result = client.call(QStringLiteral("builds.list"),
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
            row.duration = QString::number(
                               build.value(QStringLiteral("durationMs")).toInt() / 1000.0, 'f', 1) +
                           QStringLiteral("s");
            row.started = build.value(QStringLiteral("startedAt")).toString();
            QStringList platforms;
            for (const QJsonValue &platform : build.value(QStringLiteral("platforms")).toArray()) {
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

} // namespace PodStack::CliInternal
