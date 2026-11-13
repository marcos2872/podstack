#pragma once

#include <QJsonObject>
#include <QString>

namespace PodStack {

/// Erro de domínio com código estável (ver docs/engineering/api-rpc.md).
struct Error {
    QString code;
    QString message;
    QString detail;
    QString remediation;
    QString reason; // classificação estável opcional (ex.: backend_missing)
    bool retryable = false;

    bool isEmpty() const { return code.isEmpty(); }
    QJsonObject toJson() const;

    static Error make(QString code, QString message, QString detail = {}, QString remediation = {},
                      bool retryable = false);
};

} // namespace PodStack
