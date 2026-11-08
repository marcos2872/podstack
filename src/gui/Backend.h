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
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    explicit Backend(QObject *parent = nullptr);

    bool connected() const { return m_connected; }
    QString error() const { return m_error; }
    QString version() const;

    Q_INVOKABLE void refresh();

signals:
    void containersChanged(const QVariantList &items);
    void stateChanged();

private:
    void onRefreshFinished();

    QFutureWatcher<QJsonObject> m_watcher;
    bool m_connected = false;
    QString m_error;
};

} // namespace PodStack
