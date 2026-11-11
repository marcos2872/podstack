// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/SerialPorts.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

namespace PodStack {
namespace {

QString kindForName(const QString &name)
{
    if (name.startsWith(QStringLiteral("ttyUSB"))) {
        return QStringLiteral("usb");
    }
    if (name.startsWith(QStringLiteral("ttyACM"))) {
        return QStringLiteral("acm");
    }
    return QStringLiteral("uart");
}

QString readTrimmed(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll()).trimmed();
}

QString uartTypeName(int type)
{
    switch (type) {
    case 1:
        return QStringLiteral("8250");
    case 2:
        return QStringLiteral("16450");
    case 3:
        return QStringLiteral("16550");
    case 4:
        return QStringLiteral("16550A");
    case 5:
        return QStringLiteral("16650");
    case 6:
        return QStringLiteral("16750");
    case 7:
        return QStringLiteral("16850");
    case 8:
        return QStringLiteral("16950");
    default:
        return {};
    }
}

QString uartDescription(const QString &sysClassTty, const QString &name, bool *present)
{
    const QString sysPath = sysClassTty + QLatin1Char('/') + name;
    bool ok = false;
    const int type = readTrimmed(sysPath + QStringLiteral("/type")).toInt(&ok);
    if (!ok || type <= 0) {
        if (present) {
            *present = false;
        }
        return {};
    }
    if (present) {
        *present = true;
    }
    const QString typeName = uartTypeName(type);
    return typeName.isEmpty() ? QStringLiteral("UART") : QStringLiteral("UART %1").arg(typeName);
}

QString usbDescription(const QString &sysClassTty, const QString &name, const char *fallback)
{
    QString directory = sysClassTty + QLatin1Char('/') + name + QStringLiteral("/device");
    for (int level = 0; level < 4; ++level) {
        const QDir current(directory);
        const QString manufacturer = readTrimmed(current.filePath(QStringLiteral("manufacturer")));
        const QString product = readTrimmed(current.filePath(QStringLiteral("product")));
        if (!manufacturer.isEmpty() || !product.isEmpty()) {
            const QString combined = QStringList{manufacturer, product}
                                         .filter(QRegularExpression(QStringLiteral("\\S")))
                                         .join(QLatin1Char(' '));
            if (!combined.isEmpty()) {
                return combined;
            }
        }
        const QString idVendor = readTrimmed(current.filePath(QStringLiteral("idVendor")));
        const QString idProduct = readTrimmed(current.filePath(QStringLiteral("idProduct")));
        if (!idVendor.isEmpty() && !idProduct.isEmpty()) {
            return QStringLiteral("%1:%2").arg(idVendor, idProduct);
        }
        directory += QStringLiteral("/..");
    }
    return QString::fromLatin1(fallback);
}

} // namespace

QJsonObject SerialPortInfo::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("path"), path);
    object.insert(QStringLiteral("kind"), kind);
    object.insert(QStringLiteral("description"), description);
    object.insert(QStringLiteral("readable"), readable);
    return object;
}

QVector<SerialPortInfo> listSerialPortsIn(const QString &devDir, const QString &sysClassTty)
{
    QVector<SerialPortInfo> ports;
    const QDir dev(devDir);
    const QFileInfoList entries = dev.entryInfoList(
        {QStringLiteral("ttyUSB*"), QStringLiteral("ttyACM*"), QStringLiteral("ttyS*")},
        QDir::System | QDir::Files, QDir::Name);
    for (const QFileInfo &entry : entries) {
        SerialPortInfo info;
        info.path = entry.canonicalFilePath();
        if (info.path.isEmpty()) {
            info.path = entry.absoluteFilePath();
        }
        info.kind = kindForName(entry.fileName());
        if (info.kind == QStringLiteral("uart")) {
            bool present = false;
            info.description = uartDescription(sysClassTty, entry.fileName(), &present);
            if (!present) {
                continue;
            }
        } else if (info.kind == QStringLiteral("usb")) {
            info.description =
                usbDescription(sysClassTty, entry.fileName(), "Adaptador USB serial");
        } else {
            info.description = usbDescription(sysClassTty, entry.fileName(), "Dispositivo CDC-ACM");
        }
        info.readable = entry.isReadable();
        ports.append(info);
    }
    return ports;
}

QVector<SerialPortInfo> listHostSerialPorts()
{
    return listSerialPortsIn(QStringLiteral("/dev"), QStringLiteral("/sys/class/tty"));
}

bool validateSerialPortPath(const QString &path, QString *error)
{
    if (error) {
        error->clear();
    }
    if (path.isEmpty() || !QDir::isAbsolutePath(path)) {
        if (error) {
            *error = QStringLiteral("O caminho da porta serial deve ser absoluto.");
        }
        return false;
    }
    const QFileInfo info(path);
    if (!info.exists()) {
        if (error) {
            *error = QStringLiteral("A porta serial não existe: %1").arg(path);
        }
        return false;
    }
    if (!info.isReadable()) {
        if (error) {
            *error = QStringLiteral("A porta serial não pode ser lida: %1").arg(path);
        }
        return false;
    }
    return true;
}

} // namespace PodStack
