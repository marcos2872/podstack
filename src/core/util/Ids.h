// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

namespace PodStack {

/// Gera um UUIDv7 (48 bits de timestamp em ms, versão 7, variante RFC 4122 e
/// 74 bits aleatórios) no formato 8-4-4-4-12 minúsculo.
QString newUuidV7();

} // namespace PodStack
