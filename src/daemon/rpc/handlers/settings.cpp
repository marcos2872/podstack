// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon/rpc/handlers/Common.h"

using namespace PodStack::DaemonInternal;

namespace PodStack {

QJsonObject Daemon::settingsGet(const QJsonObject &params, Error *error)
{
    QString key;
    const QJsonValue keyValue = params.value(QStringLiteral("key"));
    if (!keyValue.isUndefined() && !keyValue.isNull()) {
        if (!keyValue.isString() || keyValue.toString().trimmed().isEmpty()) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("O campo key deve ser uma string."),
                                     QStringLiteral("key"));
            }
            return {};
        }
        key = keyValue.toString().trimmed();
        if (!Settings::find(key)) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_VALIDATION"),
                                     QStringLiteral("Chave de configuração desconhecida: %1.")
                                         .arg(key),
                                     key,
                                     QStringLiteral("Consulte `podstack config schema`."));
            }
            return {};
        }
    }

    std::lock_guard<std::mutex> lock(m_settingsMutex);
    const Config config = Config::load();
    return QJsonObject{{QStringLiteral("values"), config.flatValues(key)}};
}

QJsonObject Daemon::settingsReset(const QJsonObject &params, Error *error)
{
    QString key;
    const QJsonValue keyValue = params.value(QStringLiteral("key"));
    if (!keyValue.isUndefined() && !keyValue.isNull()) {
        if (!keyValue.isString() || keyValue.toString().trimmed().isEmpty()) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("O campo key deve ser uma string."),
                                     QStringLiteral("key"));
            }
            return {};
        }
        key = keyValue.toString().trimmed();
    }

    std::lock_guard<std::mutex> lock(m_settingsMutex);
    Config config = Config::load();
    bool restartRequired = false;

    if (!key.isEmpty()) {
        const SettingDef *definition = Settings::find(key);
        if (!definition) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_VALIDATION"),
                                     QStringLiteral("Chave de configuração desconhecida: %1.")
                                         .arg(key),
                                     key,
                                     QStringLiteral("Consulte `podstack config schema`."));
            }
            return {};
        }
        const QJsonValue previous = config.get(key);
        config.reset(key);
        restartRequired = definition->restartRequired
                          && !(previous == definition->defaultValue);
    } else {
        const QJsonObject previous = config.flatValues();
        config.resetAll();
        for (const SettingDef &definition : Settings::definitions()) {
            if (definition.restartRequired
                && !(previous.value(definition.key) == definition.defaultValue)) {
                restartRequired = true;
                break;
            }
        }
    }

    QString saveError;
    if (!config.save(&saveError)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível salvar a configuração."),
                                 saveError,
                                 QStringLiteral("Verifique as permissões de ~/.config/podstack."));
        }
        return {};
    }

    QJsonObject result;
    result.insert(QStringLiteral("values"), config.flatValues());
    result.insert(QStringLiteral("restartRequired"), restartRequired);
    return result;
}

QJsonObject Daemon::settingsSchema() const
{
    return Settings::schemaJson();
}

QJsonObject Daemon::settingsSet(const QJsonObject &params, Error *error)
{
    const QJsonValue keyValue = params.value(QStringLiteral("key"));
    if (!keyValue.isString() || keyValue.toString().trimmed().isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("O campo key é obrigatório."),
                                 QStringLiteral("key"));
        }
        return {};
    }
    if (!params.contains(QStringLiteral("value"))) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("O campo value é obrigatório."),
                                 QStringLiteral("value"));
        }
        return {};
    }

    const QString key = keyValue.toString().trimmed();
    const QJsonValue value = params.value(QStringLiteral("value"));

    std::lock_guard<std::mutex> lock(m_settingsMutex);
    Config config = Config::load();

    Error validation;
    if (!Config::validate(key, value, &validation)) {
        if (error) {
            *error = validation;
        }
        return {};
    }

    const QJsonValue previous = config.get(key);
    config.set(key, value);

    QString saveError;
    if (!config.save(&saveError)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível salvar a configuração."),
                                 saveError,
                                 QStringLiteral("Verifique as permissões de ~/.config/podstack."));
        }
        return {};
    }

    const SettingDef *definition = Settings::find(key);
    const bool restartRequired = definition && definition->restartRequired
                                 && !(previous == value);
    Logging::reload();
    QJsonObject result;
    result.insert(QStringLiteral("values"), config.flatValues());
    result.insert(QStringLiteral("restartRequired"), restartRequired);
    result.insert(QStringLiteral("key"), key);
    return result;
}

} // namespace PodStack
