#include "core/Error.h"

#include <QJsonObject>

#include <utility>

namespace PodStack {

QJsonObject Error::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("code"), code);
    obj.insert(QStringLiteral("message"), message);
    if (!detail.isEmpty()) {
        obj.insert(QStringLiteral("detail"), detail);
    }
    if (!remediation.isEmpty()) {
        obj.insert(QStringLiteral("remediation"), remediation);
    }
    if (!reason.isEmpty()) {
        obj.insert(QStringLiteral("reason"), reason);
    }
    obj.insert(QStringLiteral("retryable"), retryable);
    return obj;
}

Error Error::make(QString code, QString message, QString detail, QString remediation, bool retryable)
{
    Error error;
    error.code = std::move(code);
    error.message = std::move(message);
    error.detail = std::move(detail);
    error.remediation = std::move(remediation);
    error.retryable = retryable;
    return error;
}

} // namespace PodStack
