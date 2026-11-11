// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace PodStack {

struct UsbDeviceInfo {
    int bus = 0;
    int address = 0;
    QString vendorId;
    QString productId;
    QString manufacturer;
    QString product;
    QString serial;
    QString description;

    QString key() const;
    QJsonObject toJson() const;
};

/// Lista dispositivos USB do host (exclui hubs) em /sys/bus/usb/devices.
QVector<UsbDeviceInfo> listHostUsbDevices();

/// Variante testável com raiz sysfs injetável.
QVector<UsbDeviceInfo> listUsbDevicesIn(const QString &sysBusUsb);

/// Localiza um dispositivo pelo par bus/endereço.
bool findUsbDevice(int bus, int address, UsbDeviceInfo *out, QString *error = nullptr);

} // namespace PodStack
