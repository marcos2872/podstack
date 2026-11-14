// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>

namespace PodStack {

// Normaliza o payload bruto de /libpod/system/df em seções estáveis usadas
// pela CLI e pela GUI. O objeto `json` original é preservado em `json`.
// Quando `detailed` é falso, o campo `items` de cada seção é omitido, mas as
// contagens e os totais continuam presentes.
QJsonObject normalizeDiskUsage(const QJsonObject &json, bool detailed = true);

} // namespace PodStack
