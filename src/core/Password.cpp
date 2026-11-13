// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/Password.h"

#include <QRandomGenerator>

namespace PodStack {

QString generatePassword(int length)
{
    static const QString alphabet =
        QStringLiteral("ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789!@#%*+-_=");
    if (length < 1) {
        return {};
    }
    QString password;
    password.reserve(length);
    auto *generator = QRandomGenerator::system();
    for (int i = 0; i < length; ++i) {
        password.append(alphabet.at(generator->bounded(alphabet.size())));
    }
    return password;
}

} // namespace PodStack
