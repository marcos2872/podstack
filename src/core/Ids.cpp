// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/Ids.h"

#include <QByteArray>
#include <QDateTime>
#include <QRandomGenerator>

namespace PodStack {

QString newUuidV7()
{
    QByteArray bytes(16, '\0');

    const quint64 milliseconds =
        static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
    for (int index = 0; index < 6; ++index) {
        const int shift = 8 * (5 - index);
        bytes[index] = static_cast<char>((milliseconds >> shift) & 0xff);
    }

    QRandomGenerator *random = QRandomGenerator::system();
    for (int index = 6; index < bytes.size(); ++index) {
        bytes[index] = static_cast<char>(random->bounded(256));
    }

    // Versão 7 no nibble alto do byte 6 e variante RFC 4122 (10xx) no byte 8.
    bytes[6] = static_cast<char>((static_cast<quint8>(bytes.at(6)) & 0x0f) | 0x70);
    bytes[8] = static_cast<char>((static_cast<quint8>(bytes.at(8)) & 0x3f) | 0x80);

    const QString hex = QString::fromLatin1(bytes.toHex());
    return hex.left(8) + QLatin1Char('-') + hex.mid(8, 4) + QLatin1Char('-')
           + hex.mid(12, 4) + QLatin1Char('-') + hex.mid(16, 4) + QLatin1Char('-')
           + hex.mid(20, 12);
}

} // namespace PodStack
