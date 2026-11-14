// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDBusArgument>
#include <QMetaType>
#include <QVariant>
#include <QVariantMap>

namespace PodStack::DbusUtils {

/// Converte o primeiro argumento de uma resposta D-Bus em `QVariantMap`.
///
/// Respostas de `a{sv}` chegam embrulhadas em `QDBusArgument`; o
/// `qvariant_cast<QVariantMap>` devolve vazio nesse caso. O helper usa esse
/// formato para devolver `{ok, message}`.
inline QVariantMap variantMapFromReply(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        QVariantMap map;
        const QDBusArgument argument = value.value<QDBusArgument>();
        argument >> map;
        return map;
    }
    return value.toMap();
}

} // namespace PodStack::DbusUtils
