#include "core/Config.h"

#include "core/Paths.h"
#include "core/Settings.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
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

constexpr int kSchemaVersion = 2;
constexpr int kLegacyHttpPort = 8080;
constexpr int kLegacyHttpsPort = 8443;

/// Migra os defaults antigos do proxy para as portas atuais. Só age quando a
/// config persistida ainda está no schema anterior; valores customizados são
/// preservados.
bool migrateProxyPorts(QJsonObject *stored)
{
    if (stored->value(QStringLiteral("schemaVersion")).toInt(1) >= kSchemaVersion) {
        return false;
    }

    QJsonObject network = stored->value(QStringLiteral("network")).toObject();
    bool changed = false;
    const auto migrate = [&](const QString &key, int legacy, const QString &settingKey) {
        if (network.value(key).toInt() != legacy) {
            return;
        }
        const SettingDef *definition = Settings::find(settingKey);
        if (!definition) {
            return;
        }
        network.insert(key, definition->defaultValue);
        changed = true;
    };
    migrate(QStringLiteral("httpPort"), kLegacyHttpPort, QStringLiteral("network.httpPort"));
    migrate(QStringLiteral("httpsPort"), kLegacyHttpsPort, QStringLiteral("network.httpsPort"));

    if (changed) {
        stored->insert(QStringLiteral("network"), network);
    }
    stored->insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    return changed;
}

} // namespace

Config::Config()
{
    m_values = defaults();
}

QJsonObject Config::defaults()
{
    QJsonObject root = Settings::defaults();
    root.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    return root;
}

QJsonObject Config::schema()
{
    return Settings::schemaJson();
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

    QJsonObject stored = document.object();
    const bool migrated = migrateProxyPorts(&stored);
    for (auto it = stored.constBegin(); it != stored.constEnd(); ++it) {
        config.m_values.insert(it.key(), mergeValue(config.m_values.value(it.key()), it.value()));
    }
    if (migrated) {
        const QString backupPath =
            path + QStringLiteral(".bak-") +
            QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmssZ"));
        if (QFile::copy(path, backupPath)) {
            QFile::setPermissions(backupPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        } else {
            qWarning("podstack: não foi possível criar o backup %s", qUtf8Printable(backupPath));
        }

        QString saveError;
        if (!config.save(&saveError)) {
            qWarning("podstack: não foi possível persistir a migração de portas: %s",
                     qUtf8Printable(saveError));
        }
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

bool Config::validate(const QString &key, const QJsonValue &value, Error *error)
{
    if (error) {
        *error = {};
    }
    const SettingDef *definition = Settings::find(key);
    if (!definition) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_VALIDATION"),
                                 QStringLiteral("Chave de configuração desconhecida: %1.")
                                     .arg(key),
                                 key,
                                 QStringLiteral("Consulte `podstack config schema`."));
        }
        return false;
    }
    return Settings::validate(*definition, value, error);
}

bool Config::reset(const QString &key, Error *error)
{
    if (error) {
        *error = {};
    }
    const SettingDef *definition = Settings::find(key);
    if (!definition) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_VALIDATION"),
                                 QStringLiteral("Chave de configuração desconhecida: %1.")
                                     .arg(key),
                                 key,
                                 QStringLiteral("Consulte `podstack config schema`."));
        }
        return false;
    }
    set(key, definition->defaultValue);
    return true;
}

void Config::resetAll()
{
    const QJsonValue schemaVersion = m_values.value(QStringLiteral("schemaVersion"));
    const QJsonValue setup = m_values.value(QStringLiteral("setup"));
    m_values = defaults();
    if (!schemaVersion.isUndefined()) {
        m_values.insert(QStringLiteral("schemaVersion"), schemaVersion);
    }
    if (setup.isObject()) {
        m_values.insert(QStringLiteral("setup"), setup);
    }
}

QJsonObject Config::flatValues(const QString &onlyKey) const
{
    QJsonObject values;
    for (const SettingDef &definition : Settings::definitions()) {
        if (!onlyKey.isEmpty() && definition.key != onlyKey) {
            continue;
        }
        values.insert(definition.key, get(definition.key));
    }
    return values;
}

} // namespace PodStack
