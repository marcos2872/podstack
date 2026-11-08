#pragma once

#include <QJsonObject>
#include <QString>

namespace PodStack {

/// Erro de domínio com código estável (ver docs/api-rpc.md).
struct Error {
    QString code;
    QString message;
    QString detail;
    QString remediation;
    bool retryable = false;

    bool isEmpty() const { return code.isEmpty(); }
    QJsonObject toJson() const;

    static Error make(QString code, QString message, QString detail = {}, QString remediation = {},
                      bool retryable = false);
};

} // namespace PodStack
