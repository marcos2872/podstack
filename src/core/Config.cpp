#include "core/Config.h"

#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

#include <functional>

namespace PodStack {
namespace {

QStringList splitKey(const QString &key)
{
    return key.split(QLatin1Char('.'), Qt::SkipEmptyParts);
}

/// Mescla defaults e valores persistidos preservando chaves aninhadas.
QJsonValue mergeValue(const QJsonValue &base, const QJsonValue &stored)
{
    if (!base.isObject() || !stored.isObject()) {
        return stored;
    }

    QJsonObject merged = base.toObject();
    const QJsonObject storedObject = stored.toObject();
    for (auto it = storedObject.constBegin(); it != storedObject.constEnd(); ++it) {
        merged.insert(it.key(), mergeValue(merged.value(it.key()), it.value()));
    }
    return merged;
}

} // namespace

Config::Config()
{
    m_values = defaults();
}

QJsonObject Config::defaults()
{
    QJsonObject app;
    app.insert(QStringLiteral("startAtLogin"), false);
    app.insert(QStringLiteral("waitForContainerStop"), true);
    app.insert(QStringLiteral("keepRunningInTray"), true);
    app.insert(QStringLiteral("showInTray"), true);
    app.insert(QStringLiteral("notifications"), true);
    app.insert(QStringLiteral("theme"), QStringLiteral("system"));
    app.insert(QStringLiteral("language"), QStringLiteral("pt-BR"));
    app.insert(QStringLiteral("showOnboarding"), true);

    QJsonObject setup;
    setup.insert(QStringLiteral("completed"), false);
    setup.insert(QStringLiteral("completedAt"), QJsonValue::Null);
    setup.insert(QStringLiteral("dockerPolicy"), QStringLiteral("keep"));
    setup.insert(QStringLiteral("components"), QJsonArray{});
    setup.insert(QStringLiteral("lastValidation"), QJsonValue::Null);

    QJsonObject engine;
    engine.insert(QStringLiteral("mode"), QStringLiteral("rootless"));

    QJsonObject network;
    network.insert(QStringLiteral("https"), false);
    network.insert(QStringLiteral("proxy"), QStringLiteral("auto"));
    network.insert(QStringLiteral("proxyExclude"), QJsonArray{});
    network.insert(QStringLiteral("domainsMode"), QStringLiteral("localhost"));

    QJsonObject machines;
    machines.insert(QStringLiteral("default"), QJsonValue::Null);
    machines.insert(QStringLiteral("forwardPorts"), true);
    machines.insert(QStringLiteral("exposePortsToLan"), false);

    QJsonObject kubernetes;
    kubernetes.insert(QStringLiteral("enabled"), false);

    QJsonObject telemetry;
    telemetry.insert(QStringLiteral("enabled"), false);
    telemetry.insert(QStringLiteral("crashReports"), false);
    telemetry.insert(QStringLiteral("usage"), false);
    telemetry.insert(QStringLiteral("performance"), false);
    telemetry.insert(QStringLiteral("errors"), false);

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("app"), app);
    root.insert(QStringLiteral("setup"), setup);
    root.insert(QStringLiteral("engine"), engine);
    root.insert(QStringLiteral("network"), network);
    root.insert(QStringLiteral("machines"), machines);
    root.insert(QStringLiteral("kubernetes"), kubernetes);
    root.insert(QStringLiteral("telemetry"), telemetry);
    return root;
}

Config Config::load()
{
    Config config;
    config.m_values = defaults();

    const QString path = Paths::configDir() + QStringLiteral("/config.json");
    QFile file(path);
    if (!file.exists()) {
        return config;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("podstack: não foi possível ler %s", qUtf8Printable(path));
        return config;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning("podstack: config inválida em %s: %s", qUtf8Printable(path),
                 qUtf8Printable(parseError.errorString()));
        return config;
    }

    const QJsonObject stored = document.object();
    for (auto it = stored.constBegin(); it != stored.constEnd(); ++it) {
        config.m_values.insert(it.key(), mergeValue(config.m_values.value(it.key()), it.value()));
    }
    return config;
}

bool Config::save(QString *error) const
{
    if (!Paths::ensureDirs(error)) {
        return false;
    }

    const QString path = Paths::configDir() + QStringLiteral("/config.json");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("Não foi possível abrir %1: %2").arg(path, file.errorString());
        }
        return false;
    }

    const QByteArray data = QJsonDocument(m_values).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size()) {
        if (error) {
            *error = QStringLiteral("Falha ao escrever %1: %2").arg(path, file.errorString());
        }
        return false;
    }
    if (!file.commit()) {
        if (error) {
            *error = QStringLiteral("Falha ao salvar %1: %2").arg(path, file.errorString());
        }
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

QJsonValue Config::get(const QString &key) const
{
    QJsonValue current = m_values;
    const QStringList parts = splitKey(key);
    for (const QString &part : parts) {
        if (!current.isObject()) {
            return {};
        }
        current = current.toObject().value(part);
    }
    return current;
}

void Config::set(const QString &key, const QJsonValue &value)
{
    const QStringList parts = splitKey(key);
    if (parts.isEmpty()) {
        return;
    }

    std::function<void(QJsonObject &, int)> assign = [&](QJsonObject &object, int index) {
        if (index == parts.size() - 1) {
            object.insert(parts.at(index), value);
            return;
        }
        QJsonObject child = object.value(parts.at(index)).toObject();
        assign(child, index + 1);
        object.insert(parts.at(index), child);
    };
    assign(m_values, 0);
}

} // namespace PodStack
