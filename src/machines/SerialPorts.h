// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace PodStack {

struct SerialPortInfo {
    QString path;
    QString kind; // usb | acm | uart
    QString description;
    bool readable = false;

    QJsonObject toJson() const;
};

/// Lista portas seriais do host em /dev (ttyUSB*, ttyACM*, ttyS*).
QVector<SerialPortInfo> listHostSerialPorts();

/// Valida um caminho de porta serial para anexar a uma machine.
bool validateSerialPortPath(const QString &path, QString *error = nullptr);

} // namespace PodStack
