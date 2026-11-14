// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/controllers/SettingsController.h"

#include "core/config/Paths.h"
#include "core/error/Error.h"
#include "core/rpc/RpcClient.h"
#include "core/system/Proxy.h"
#include "gui/app/Autostart.h"

#include <QJsonArray>
#include <QtConcurrent>

namespace PodStack {

SettingsController::SettingsController(QObject *parent)
    : ControllerBase(parent)
{
    connect(&m_watcher, &QFutureWatcher<QJsonObject>::finished, this,
            &SettingsController::onFinished);
    connect(&m_actionWatcher, &QFutureWatcher<QJsonObject>::finished, this,
            &SettingsController::onActionFinished);
}

QVariantMap SettingsController::settings() const
{
    QVariantMap values;
    values.insert(QStringLiteral("engineMode"),
                  m_values.value(QStringLiteral("engine.mode"), QStringLiteral("rootless")));
    values.insert(QStringLiteral("theme"),
                  m_values.value(QStringLiteral("app.theme"), QStringLiteral("system")));
    values.insert(QStringLiteral("keepRunningInTray"),
                  m_values.value(QStringLiteral("app.keepRunningInTray"), true));
    values.insert(QStringLiteral("showInTray"),
                  m_values.value(QStringLiteral("app.showInTray"), true));
    values.insert(QStringLiteral("startAtLogin"),
                  m_values.value(QStringLiteral("app.startAtLogin"), false));
    values.insert(QStringLiteral("notifications"),
                  m_values.value(QStringLiteral("app.notifications"), true));
    values.insert(QStringLiteral("proxy"),
                  m_values.value(QStringLiteral("network.proxy"), QStringLiteral("auto")));
    QStringList proxyExclude;
    for (const QVariant &entry :
         m_values.value(QStringLiteral("network.proxyExclude")).toList()) {
        const QString text = entry.toString();
        if (!text.isEmpty()) {
            proxyExclude.append(text);
        }
    }
    values.insert(QStringLiteral("proxyExclude"), proxyExclude);
    return values;
}

void SettingsController::refresh()
{
    if (m_watcher.isRunning()) {
        m_refreshAgain = true;
        return;
    }
    m_watcher.setFuture(QtConcurrent::run([] {
        RpcClient client;
        Error error;
        const QJsonObject values = client.call(QStringLiteral("settings.get"), {}, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("error"), error.toJson()}};
        }
        const QJsonObject schema = client.call(QStringLiteral("settings.schema"), {}, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("error"), error.toJson()}};
        }
        QJsonObject result;
        result.insert(QStringLiteral("values"), values.value(QStringLiteral("values")));
        result.insert(QStringLiteral("schema"), schema.value(QStringLiteral("schema")));
        return result;
    }));
}

void SettingsController::onFinished()
{
    const QJsonObject result = m_watcher.result();
    if (result.contains(QStringLiteral("error"))) {
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        QString message = errorObject.value(QStringLiteral("message")).toString();
        if (message.isEmpty()) {
            message = QStringLiteral("Falha ao carregar as configurações.");
        }
        setError(message);
    } else {
        m_schema = result.value(QStringLiteral("schema")).toArray().toVariantList();
        const bool hadError = !m_error.isEmpty();
        m_error.clear();
        emit schemaChanged();
        applyValues(result.value(QStringLiteral("values")).toObject().toVariantMap());
        if (hadError) {
            emit errorChanged();
        }
    }
    if (m_refreshAgain) {
        m_refreshAgain = false;
        refresh();
    }
}

void SettingsController::setError(const QString &message)
{
    m_error = message;
    setLastError(message);
    emit errorChanged();
}

void SettingsController::applyValues(const QVariantMap &values)
{
    m_values = values;
    const int bufferLines = values.value(QStringLiteral("logs.bufferLines")).toInt();
    setLogBufferLines(bufferLines);
    emit valuesChanged();
    clearLastError();
}

void SettingsController::setSetting(const QString &key, const QVariant &value)
{
    const bool applyAutostart = key == QStringLiteral("app.startAtLogin");
    if (key.isEmpty()) {
        setError(QStringLiteral("Chave de configuração inválida."));
        return;
    }
    QJsonValue json = QJsonValue::fromVariant(value);
    if (!value.isValid() || value.isNull()) {
        json = QJsonValue(QJsonValue::Null);
    }
    enqueue(QStringLiteral("settings.set"),
            QJsonObject{{QStringLiteral("key"), key}, {QStringLiteral("value"), json}},
            applyAutostart);
}

void SettingsController::resetSetting(const QString &key)
{
    QJsonObject params;
    if (!key.isEmpty()) {
        params.insert(QStringLiteral("key"), key);
    }
    const bool applyAutostart = key.isEmpty() || key == QStringLiteral("app.startAtLogin");
    enqueue(QStringLiteral("settings.reset"), params, applyAutostart);
}

void SettingsController::enqueue(const QString &method, const QJsonObject &params,
                                 bool applyAutostart)
{
    Request request;
    request.method = method;
    request.params = params;
    request.applyAutostart = applyAutostart;
    m_queue.enqueue(request);
    startNext();
}

void SettingsController::startNext()
{
    if (m_settingsActionActive || m_queue.isEmpty()) {
        return;
    }
    m_current = m_queue.dequeue();
    m_settingsActionActive = true;
    const QString method = m_current.method;
    const QJsonObject params = m_current.params;
    m_actionWatcher.setFuture(QtConcurrent::run([method, params] {
        RpcClient client;
        Error error;
        const QJsonObject result = client.call(method, params, &error);
        if (!error.isEmpty()) {
            return QJsonObject{{QStringLiteral("error"), error.toJson()}};
        }
        return result;
    }));
}

void SettingsController::onActionFinished()
{
    const QJsonObject result = m_actionWatcher.result();
    const Request request = m_current;
    const QString key = request.params.value(QStringLiteral("key")).toString();
    bool ok = true;
    QString message;

    if (result.contains(QStringLiteral("error"))) {
        ok = false;
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        message = errorObject.value(QStringLiteral("message")).toString();
        const QString remediation = errorObject.value(QStringLiteral("remediation")).toString();
        if (!remediation.isEmpty()) {
            message += QLatin1Char(' ') + remediation;
        }
    } else {
        if (result.contains(QStringLiteral("values"))) {
            applyValues(result.value(QStringLiteral("values")).toObject().toVariantMap());
        }
        if (result.value(QStringLiteral("restartRequired")).toBool(false) && !key.isEmpty()
            && !m_pendingRestartKeys.contains(key)) {
            m_pendingRestartKeys.append(key);
            emit pendingRestartKeysChanged();
        }
        if (request.applyAutostart) {
            QString autostartError;
            const bool enabled = request.params.value(QStringLiteral("value")).toBool();
            if (!Autostart::setEnabled(enabled, &autostartError)) {
                ok = false;
                message = autostartError;
            }
        }
    }

    if (ok) {
        if (!m_error.isEmpty()) {
            m_error.clear();
            emit errorChanged();
        }
        clearLastError();
    } else {
        setError(message);
    }

    emit settingFinished(key, ok, message);
    m_settingsActionActive = false;
    startNext();
}

void SettingsController::saveEngineMode(const QString &mode)
{
    if (mode != QStringLiteral("rootless") && mode != QStringLiteral("rootful")) {
        setError(QStringLiteral("Modo de engine inválido."));
        return;
    }
    setSetting(QStringLiteral("engine.mode"), mode);
}

void SettingsController::saveTraySettings(bool keepRunning, bool showInTray,
                                          bool startAtLogin, bool notifications)
{
    setSetting(QStringLiteral("app.keepRunningInTray"), keepRunning);
    setSetting(QStringLiteral("app.showInTray"), showInTray);
    setSetting(QStringLiteral("app.startAtLogin"), startAtLogin);
    setSetting(QStringLiteral("app.notifications"), notifications);
}

void SettingsController::saveProxySettings(const QString &proxy, const QString &excludeText)
{
    const QString normalizedProxy = proxy.trimmed();
    QStringList excludes;
    for (const QString &part : excludeText.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString entry = part.trimmed();
        if (!entry.isEmpty()) {
            excludes.append(entry);
        }
    }

    Error validation;
    if (!Proxy::validate(normalizedProxy, excludes, &validation)) {
        setError(validation.message);
        return;
    }

    QJsonArray excludeArray;
    for (const QString &entry : excludes) {
        excludeArray.append(entry);
    }
    setSetting(QStringLiteral("network.proxy"), normalizedProxy);
    setSetting(QStringLiteral("network.proxyExclude"), excludeArray.toVariantList());
}

void SettingsController::saveTheme(const QString &theme)
{
    if (theme != QStringLiteral("system") && theme != QStringLiteral("dark")
        && theme != QStringLiteral("light")) {
        setError(QStringLiteral("Tema inválido."));
        return;
    }
    setSetting(QStringLiteral("app.theme"), theme);
}

} // namespace PodStack
