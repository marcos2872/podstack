// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gui/controllers/ControllerBase.h"

#include <QFutureWatcher>
#include <QJsonObject>
#include <QQueue>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace PodStack {

class SettingsController : public ControllerBase {
    Q_OBJECT
    Q_PROPERTY(QVariantList schema READ schema NOTIFY schemaChanged)
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QStringList pendingRestartKeys READ pendingRestartKeys
               NOTIFY pendingRestartKeysChanged)

public:
    explicit SettingsController(QObject *parent = nullptr);

    QVariantList schema() const { return m_schema; }
    QVariantMap values() const { return m_values; }
    QString error() const { return m_error; }
    QStringList pendingRestartKeys() const { return m_pendingRestartKeys; }

    /// Mapa resumido usado pela bandeja e pelo tema.
    Q_INVOKABLE QVariantMap settings() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE void resetSetting(const QString &key);
    Q_INVOKABLE void saveEngineMode(const QString &mode);
    Q_INVOKABLE void saveTheme(const QString &theme);
    Q_INVOKABLE void saveTraySettings(bool keepRunning, bool showInTray, bool startAtLogin,
                                      bool notifications = true);
    Q_INVOKABLE void saveProxySettings(const QString &proxy, const QString &excludeText);

signals:
    void schemaChanged();
    void valuesChanged();
    void errorChanged();
    void pendingRestartKeysChanged();
    void settingFinished(const QString &key, bool ok, const QString &error);

private:
    struct Request {
        QString method;
        QJsonObject params;
        bool applyAutostart = false;
    };

    void setError(const QString &message);
    void applyValues(const QVariantMap &values);
    void enqueue(const QString &method, const QJsonObject &params, bool applyAutostart = false);
    void startNext();
    void onFinished();
    void onActionFinished();

    QVariantList m_schema;
    QVariantMap m_values;
    QString m_error;
    QStringList m_pendingRestartKeys;
    QFutureWatcher<QJsonObject> m_watcher;
    QFutureWatcher<QJsonObject> m_actionWatcher;
    QQueue<Request> m_queue;
    Request m_current;
    bool m_settingsActionActive = false;
    bool m_refreshAgain = false;
};

} // namespace PodStack
