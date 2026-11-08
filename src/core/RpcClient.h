#pragma once

#include "core/Error.h"

#include <QJsonObject>
#include <QString>

namespace PodStack {

/// Cliente JSON-RPC 2.0 bloqueante sobre o Unix socket do podstackd.
/// Use em worker threads (CLI/GUI); nunca no thread da UI.
class RpcClient {
public:
    explicit RpcClient(QString socketPath = {});

    QJsonObject call(const QString &method, const QJsonObject &params, Error *error,
                     int timeoutMs = 30000);

    QString socketPath() const { return m_socketPath; }

private:
    QString m_socketPath;
    int m_nextId = 1;
};

} // namespace PodStack
