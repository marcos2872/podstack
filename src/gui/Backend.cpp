#include "gui/Backend.h"

#include "core/Error.h"
#include "core/RpcClient.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <QtConcurrent>

namespace PodStack {

Backend::Backend(QObject *parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<QJsonObject>::finished, this,
            &Backend::onRefreshFinished);
    QTimer::singleShot(0, this, &Backend::refresh);
}

QString Backend::version() const
{
    return QStringLiteral(PODSTACK_VERSION);
}

void Backend::refresh()
{
    if (m_watcher.isRunning()) {
        return;
    }

    m_watcher.setFuture(QtConcurrent::run([] {
        RpcClient client;
        Error error;
        QJsonObject params;
        params.insert(QStringLiteral("all"), true);

        QJsonObject result = client.call(QStringLiteral("containers.list"), params, &error);
        if (!error.isEmpty()) {
            QJsonObject payload;
            payload.insert(QStringLiteral("error"), error.toJson());
            return payload;
        }
        return result;
    }));
}

void Backend::onRefreshFinished()
{
    const QJsonObject result = m_watcher.result();

    if (result.contains(QStringLiteral("error"))) {
        const QJsonObject errorObject = result.value(QStringLiteral("error")).toObject();
        const QString message = errorObject.value(QStringLiteral("message")).toString();
        if (m_connected || m_error != message) {
            m_connected = false;
            m_error = message;
            emit stateChanged();
        }
        emit containersChanged({});
        return;
    }

    if (!m_connected || !m_error.isEmpty()) {
        m_connected = true;
        m_error.clear();
        emit stateChanged();
    }

    QVariantList items;
    const QJsonArray array = result.value(QStringLiteral("items")).toArray();
    items.reserve(array.size());

    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        QVariantMap item;
        item.insert(QStringLiteral("id"), object.value(QStringLiteral("id")).toString());
        item.insert(QStringLiteral("name"), object.value(QStringLiteral("name")).toString());
        item.insert(QStringLiteral("state"), object.value(QStringLiteral("state")).toString());
        item.insert(QStringLiteral("image"), object.value(QStringLiteral("image")).toString());

        QStringList ports;
        const QJsonArray portArray = object.value(QStringLiteral("ports")).toArray();
        for (const QJsonValue &port : portArray) {
            ports.append(port.toString());
        }
        item.insert(QStringLiteral("ports"), ports.join(QStringLiteral(", ")));
        items.append(item);
    }

    emit containersChanged(items);
}

} // namespace PodStack
