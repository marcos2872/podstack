#pragma once

#include "core/Error.h"

#include <QJsonObject>
#include <QString>

#include <functional>

namespace PodStack {

/// Acompanha uma subscription JSON-RPC de forma bloqueante (CLI).
/// Recebe notificações até o método terminar em `.done` ou `.error`.
class RpcStream {
public:
    using Event = std::function<void(const QString &method, const QJsonObject &params)>;

    explicit RpcStream(QString socketPath = {});

    bool run(const QString &method, const QJsonObject &params, const Event &onEvent,
             Error *error, int timeoutMs = 0, bool requireDone = true);

    QString socketPath() const { return m_socketPath; }

private:
    QString m_socketPath;
};

} // namespace PodStack
