// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QVariantList>

namespace PodStack {

/// Cliente mínimo do helper privilegiado no barramento de sistema.
///
/// Concentra a descoberta do serviço e a chamada D-Bus usadas pelo daemon e
/// pelo setup, sem expor a interface para a GUI.
class HelperClient {
public:
    bool available() const;

    /// Chama um método do helper e devolve `true` apenas quando `{ok:true}`.
    ///
    /// `error` recebe a mensagem devolvida pelo helper ou pelo barramento.
    bool call(const QString &method, const QVariantList &arguments, QString *error = nullptr,
              int timeoutMs = 30000) const;

    /// Igual a `call`, mas devolve o mapa completo (jobId, estado, etc.).
    bool callMap(const QString &method, const QVariantList &arguments, QVariantMap *reply,
                 QString *error = nullptr, int timeoutMs = 30000) const;
};

} // namespace PodStack
