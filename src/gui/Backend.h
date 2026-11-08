#pragma once

#include <QFutureWatcher>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantList>

namespace PodStack {

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    explicit Backend(QObject *parent = nullptr);

    bool connected() const { return m_connected; }
    QString error() const { return m_error; }
    QString lastError() const { return m_lastError; }
    QString version() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void lifecycle(const QString &action, const QString &id);
    Q_INVOKABLE void removeContainer(const QString &id, bool force, bool volumes);

signals:
    void containersChanged(const QVariantList &items);
    void stateChanged();
    void lastErrorChanged();
    void actionFinished(bool ok, const QString &error);

private:
    void onRefreshFinished();
    void onActionFinished();
    void runAction(const QString &method, const QJsonObject &params);

    QFutureWatcher<QJsonObject> m_watcher;
    QFutureWatcher<QJsonObject> m_actionWatcher;
    bool m_connected = false;
    QString m_error;
    QString m_lastError;
};

} // namespace PodStack
