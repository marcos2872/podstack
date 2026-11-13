// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

namespace PodStack {

/// Senha aleatória para contas de guest (24 caracteres, classes mistas).
QString generatePassword(int length = 24);

} // namespace PodStack
