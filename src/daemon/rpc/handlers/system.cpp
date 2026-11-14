// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon/rpc/handlers/Common.h"

using namespace PodStack::DaemonInternal;

namespace PodStack {

QJsonObject Daemon::systemDf(const QJsonObject &params, Error *error)
{
    const QJsonObject json = m_podman.systemDf(error);
    if (error && !error->isEmpty()) {
        return {};
    }

    return normalizeDiskUsage(json, params.value(QStringLiteral("detailed")).toBool(true));
}

QJsonObject Daemon::systemDisk()
{
    const QJsonValue thresholdValue =
        Config::load().get(QStringLiteral("notifications.diskThresholdPercent"));
    const int threshold = thresholdValue.isDouble() ? thresholdValue.toInt() : 90;

    QVector<DiskSpaceSample> samples;
    QJsonArray errors;
    QSet<quint64> devices;
    const auto add = [&](const QString &path, const QString &kind) {
        struct stat info{};
        if (::stat(path.toUtf8().constData(), &info) == 0) {
            const quint64 device = static_cast<quint64>(info.st_dev);
            if (devices.contains(device)) {
                return;
            }
            devices.insert(device);
        }
        DiskSpaceSample sample;
        QString error;
        if (sampleDiskSpace(path, kind, &sample, &error)) {
            samples.append(sample);
        } else {
            errors.append(error);
        }
    };

    add(QStringLiteral("/"), QStringLiteral("root"));
    add(Paths::dataDir(), QStringLiteral("data"));
    add(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
            QStringLiteral("/containers"),
        QStringLiteral("podman"));

    QJsonArray filesystems;
    for (const DiskSpaceSample &sample : samples) {
        QJsonObject item;
        item.insert(QStringLiteral("path"), sample.path);
        item.insert(QStringLiteral("kind"), sample.kind);
        item.insert(QStringLiteral("totalBytes"), static_cast<double>(sample.totalBytes));
        item.insert(QStringLiteral("freeBytes"), static_cast<double>(sample.freeBytes));
        item.insert(QStringLiteral("availableBytes"), static_cast<double>(sample.availableBytes));
        item.insert(QStringLiteral("usedPercent"), sample.usedPercent);
        filesystems.append(item);
    }

    QJsonObject result;
    result.insert(QStringLiteral("checkedAt"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    result.insert(QStringLiteral("thresholdPercent"), threshold);
    result.insert(QStringLiteral("low"), isDiskLow(samples, threshold));
    result.insert(QStringLiteral("filesystems"), filesystems);
    result.insert(QStringLiteral("errors"), errors);
    return result;
}

QJsonObject Daemon::systemDoctor()
{
    QJsonArray checks;

    const bool socketExists = QFile::exists(Paths::controlSocket());
    QJsonObject socketCheck;
    socketCheck.insert(QStringLiteral("id"), QStringLiteral("daemon.socket"));
    socketCheck.insert(QStringLiteral("status"),
                       socketExists ? QStringLiteral("ok") : QStringLiteral("fail"));
    socketCheck.insert(QStringLiteral("detail"), Paths::controlSocket());
    checks.append(socketCheck);

    Error error;
    const bool podmanOk = m_podman.ping(&error);
    QJsonObject podmanCheck;
    podmanCheck.insert(QStringLiteral("id"), QStringLiteral("podman.socket"));
    podmanCheck.insert(QStringLiteral("status"),
                       podmanOk ? QStringLiteral("ok") : QStringLiteral("fail"));
    podmanCheck.insert(QStringLiteral("detail"),
                       podmanOk ? m_podman.socketPath() : error.message);
    if (!podmanOk && !error.remediation.isEmpty()) {
        podmanCheck.insert(QStringLiteral("remediation"), error.remediation);
    }
    checks.append(podmanCheck);

    QJsonObject result;
    result.insert(QStringLiteral("checks"), checks);
    return result;
}

QJsonObject Daemon::systemEmulation()
{
    QJsonArray handlers;
    bool foreign = false;
    for (const BinfmtHandler &handler : detectBinfmtHandlers()) {
        QJsonObject item;
        item.insert(QStringLiteral("name"), handler.name);
        item.insert(QStringLiteral("interpreter"), handler.interpreter);
        item.insert(QStringLiteral("enabled"), handler.enabled);
        handlers.append(item);
        if (handler.name.startsWith(QStringLiteral("qemu-")) ||
            handler.name.startsWith(QStringLiteral("FEX-"))) {
            foreign = true;
        }
    }

    QJsonObject result;
    result.insert(QStringLiteral("hostArch"), hostArchitecture());
    result.insert(QStringLiteral("handlers"), handlers);
    result.insert(QStringLiteral("foreignEmulation"), foreign);
    return result;
}

QJsonObject Daemon::systemHello() const
{
    QJsonObject podman;
    podman.insert(QStringLiteral("apiVersion"), m_podman.apiVersion());
    podman.insert(QStringLiteral("socket"), m_podman.socketPath());
    podman.insert(QStringLiteral("mode"), engineMode());

    QJsonObject result;
    result.insert(QStringLiteral("apiVersion"), 1);
    result.insert(QStringLiteral("daemonVersion"), QStringLiteral(PODSTACK_VERSION));
    result.insert(QStringLiteral("podman"), podman);
    return result;
}

QJsonObject Daemon::systemReport(const QJsonObject &params, Error *error)
{
    const Config config = Config::load();
    const bool includeLogs =
        params.value(QStringLiteral("includeLogs"))
            .toBool(config.get(QStringLiteral("diagnostics.includeLogs")).toBool(false));

    QJsonObject report;
    report.insert(QStringLiteral("generatedAt"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    report.insert(QStringLiteral("version"), QString::fromLatin1(PODSTACK_VERSION));
    report.insert(QStringLiteral("platform"), QSysInfo::prettyProductName());
    report.insert(QStringLiteral("kernel"), QSysInfo::kernelVersion());
    report.insert(QStringLiteral("qt"), QString::fromLatin1(qVersion()));
    report.insert(QStringLiteral("controlSocket"), Paths::controlSocket());

    const QJsonObject settings = config.flatValues();
    report.insert(QStringLiteral("settings"), Redaction::json(settings));
    report.insert(QStringLiteral("doctor"), systemDoctor());

    if (includeLogs) {
        report.insert(QStringLiteral("logs"),
                      collectReportLogs(Paths::stateDir() + QStringLiteral("/logs"), &m_helper));
    }

    const QByteArray payload = QJsonDocument(report).toJson(QJsonDocument::Indented);
    const QString reportsDir = Paths::stateDir() + QStringLiteral("/reports");
    if (!QDir().mkpath(reportsDir)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível criar %1.").arg(reportsDir));
        }
        return {};
    }
    QFile::setPermissions(reportsDir,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QString path =
        reportsDir +
        QStringLiteral("/podstack-report-%1.json")
            .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível escrever o report."));
        }
        return {};
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (!file.commit()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível concluir o report."));
        }
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("path"), path);
    result.insert(QStringLiteral("bytes"), payload.size());
    result.insert(
        QStringLiteral("sha256"),
        QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex()));
    return result;
}

QJsonObject Daemon::systemStatus()
{
    Error error;
    const bool ok = m_podman.ping(&error);

    QJsonObject podman;
    podman.insert(QStringLiteral("apiVersion"), m_podman.apiVersion());
    podman.insert(QStringLiteral("socket"), m_podman.socketPath());
    podman.insert(QStringLiteral("mode"), engineMode());

    QJsonObject result;
    result.insert(QStringLiteral("state"), ok ? QStringLiteral("ok") : QStringLiteral("error"));
    result.insert(QStringLiteral("engine"),
                  ok ? QStringLiteral("running") : QStringLiteral("unavailable"));
    result.insert(QStringLiteral("daemonVersion"), QStringLiteral(PODSTACK_VERSION));
    result.insert(QStringLiteral("podman"), podman);
    if (!ok) {
        result.insert(QStringLiteral("error"), error.toJson());
    }
    return result;
}

QJsonObject Daemon::updatesCheck(const QJsonObject &params, Error *error)
{
    if (params.contains(QStringLiteral("force")) &&
        !params.value(QStringLiteral("force")).isBool()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("force deve ser booleano."));
        }
        return {};
    }
    const bool force = params.value(QStringLiteral("force")).toBool(false);
    const QString channel = params.value(QStringLiteral("channel")).toString();

    QJsonObject result = m_updates.check(channel, force, error);
    if (!error || error->isEmpty()) {
        if (result.value(QStringLiteral("status")).toString() == QStringLiteral("ok")) {
            std::lock_guard<std::mutex> lock(m_settingsMutex);
            Config config = Config::load();
            config.set(QStringLiteral("updates.lastCheck"),
                       result.value(QStringLiteral("checkedAt")).toString());
            QString saveError;
            (void) config.save(&saveError);
        }
    }
    return result;
}

} // namespace PodStack
