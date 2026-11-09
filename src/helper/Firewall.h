// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"

#include <QString>
#include <QStringList>

namespace PodStack::Firewall {

/// Detecta um backend de firewall suportado no PATH.
///
/// A ordem de preferência é `firewall-cmd` (firewalld) e depois `ufw`. Quando
/// `backend` não é nulo, recebe `firewalld` ou `ufw`.
bool available(QString *backend = nullptr);

/// Valida portas (1..65535, no máximo 32) e protocolo (`tcp`/`udp`).
///
/// Quando `normalized` não é nulo recebe as portas sem duplicatas e sem espaços.
bool validatePorts(const QStringList &ports, const QString &protocol,
                   QStringList *normalized = nullptr, Error *error = nullptr);

/// Abre as portas no firewall em modo runtime (sem `--permanent`).
///
/// Portas são validadas (1..65535, no máximo 32, sem duplicatas) e o protocolo
/// deve ser `tcp` ou `udp`. Em caso de falha parcial, as portas já abertas são
/// fechadas antes de retornar.
bool openPorts(const QStringList &ports, const QString &protocol, Error *error = nullptr);

/// Remove as regras runtime previamente abertas.
bool closePorts(const QStringList &ports, const QString &protocol, Error *error = nullptr);

} // namespace PodStack::Firewall
