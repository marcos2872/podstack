#pragma once

#include "core/Error.h"
#include "podman/PodmanClient.h"

#include <QHash>
#include <QJsonObject>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

namespace PodStack {

class Daemon {
public:
    using Notify = std::function<void(const QString &method, const QJsonObject &params)>;

    Daemon();

    QJsonObject handle(const QString &method, const QJsonObject &params, const Notify &notify,
                       Error *error);

private:
    struct Subscription {
        std::atomic_bool cancel{false};
    };

    QJsonObject systemHello() const;
    QJsonObject systemStatus();
    QJsonObject systemDoctor();
    QJsonObject containersList(const QJsonObject &params, Error *error);
    QJsonObject containerAction(const QString &method, const QJsonObject &params, Error *error);
    QJsonObject containerExec(const QJsonObject &params, Error *error);
    QJsonObject containerInspect(const QJsonObject &params, Error *error);
    QJsonObject subscribeLogs(const QJsonObject &params, const Notify &notify, Error *error);
    QJsonObject subscribeEvents(const Notify &notify, Error *error);
    QJsonObject unsubscribe(const QJsonObject &params);

    QString requireId(const QJsonObject &params, Error *error) const;
    QString createSubscription(const std::shared_ptr<Subscription> &subscription);
    void removeSubscription(const QString &id);

    PodmanClient m_podman;
    std::mutex m_subscriptionsMutex;
    QHash<QString, std::shared_ptr<Subscription>> m_subscriptions;
    int m_nextSubscription = 1;
};

} // namespace PodStack
