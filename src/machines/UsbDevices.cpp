// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/UsbDevices.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStringList>

namespace PodStack {
namespace {

QString readTrimmed(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll()).trimmed();
}

bool readInt(const QString &path, int *value)
{
    bool ok = false;
    const int parsed = readTrimmed(path).toInt(&ok);
    if (!ok) {
        return false;
    }
    *value = parsed;
    return true;
}

QString descriptionFor(const UsbDeviceInfo &device)
{
    const QStringList parts = QStringList{device.manufacturer, device.product}.filter(
        QRegularExpression(QStringLiteral("\\S")));
    if (!parts.isEmpty()) {
        return parts.join(QLatin1Char(' '));
    }
    return QStringLiteral("%1:%2").arg(device.vendorId.toUpper(), device.productId.toUpper());
}

} // namespace

QString UsbDeviceInfo::key() const
{
    return QStringLiteral("%1:%2").arg(bus).arg(address);
}

QJsonObject UsbDeviceInfo::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("bus"), bus);
    object.insert(QStringLiteral("address"), address);
    object.insert(QStringLiteral("vendorId"), vendorId);
    object.insert(QStringLiteral("productId"), productId);
    object.insert(QStringLiteral("manufacturer"), manufacturer);
    object.insert(QStringLiteral("product"), product);
    object.insert(QStringLiteral("serial"), serial);
    object.insert(QStringLiteral("description"), description);
    return object;
}

QVector<UsbDeviceInfo> listUsbDevicesIn(const QString &sysBusUsb)
{
    QVector<UsbDeviceInfo> devices;
    const QDir root(sysBusUsb);
    const QFileInfoList entries = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        UsbDeviceInfo device;
        if (!readInt(entry.filePath() + QStringLiteral("/busnum"), &device.bus) ||
            !readInt(entry.filePath() + QStringLiteral("/devnum"), &device.address) ||
            device.bus <= 0 || device.address <= 0) {
            continue;
        }

        const QString deviceClass = readTrimmed(entry.filePath() + QStringLiteral("/bDeviceClass"));
        if (deviceClass.compare(QStringLiteral("09"), Qt::CaseInsensitive) == 0) {
            continue; // hub
        }

        device.vendorId = readTrimmed(entry.filePath() + QStringLiteral("/idVendor"));
        device.productId = readTrimmed(entry.filePath() + QStringLiteral("/idProduct"));
        if (device.vendorId.isEmpty() || device.productId.isEmpty()) {
            continue;
        }
        device.manufacturer = readTrimmed(entry.filePath() + QStringLiteral("/manufacturer"));
        device.product = readTrimmed(entry.filePath() + QStringLiteral("/product"));
        device.serial = readTrimmed(entry.filePath() + QStringLiteral("/serial"));
        device.description = descriptionFor(device);

        bool duplicate = false;
        for (const UsbDeviceInfo &existing : devices) {
            if (existing.bus == device.bus && existing.address == device.address) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            devices.append(device);
        }
    }
    return devices;
}

QVector<UsbDeviceInfo> listHostUsbDevices()
{
    const QString overrideRoot =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("PODSTACK_SYSFS_USB"));
    return listUsbDevicesIn(overrideRoot.isEmpty() ? QStringLiteral("/sys/bus/usb/devices")
                                                   : overrideRoot);
}

bool findUsbDevice(int bus, int address, UsbDeviceInfo *out, QString *error)
{
    if (error) {
        error->clear();
    }
    for (const UsbDeviceInfo &device : listHostUsbDevices()) {
        if (device.bus == bus && device.address == address) {
            if (out) {
                *out = device;
            }
            return true;
        }
    }
    if (error) {
        *error = QStringLiteral("Dispositivo USB %1:%2 não encontrado.").arg(bus).arg(address);
    }
    return false;
}

} // namespace PodStack
