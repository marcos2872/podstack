// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonValue>
#include <QString>

namespace PodStack::Redaction {

/// `true` para chaves de configuração/JSON que carregam segredos.
bool isSensitiveKey(const QString &key);

/// Redige segredos em texto livre: `Authorization`, `token=...`,
/// `password=...`, `secret=...`, `api_key=...` e credenciais em URLs.
QString text(const QString &input);

/// Aplica `text()` em strings e troca valores de chaves sensíveis por
/// `[redacted]`, recursivamente.
QJsonValue json(const QJsonValue &value);

} // namespace PodStack::Redaction
