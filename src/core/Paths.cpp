#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

#include <unistd.h>

namespace PodStack::Paths {
namespace {

QString baseLocation(QStandardPaths::StandardLocation location)
{
    return QStandardPaths::writableLocation(location);
}

QString runtimeBase()
{
    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty()) {
        runtime = QStringLiteral("/tmp/podstack-%1").arg(::getuid());
    }
    return runtime;
}

bool ensureDir(const QString &path, QString *error)
{
    QDir dir;
    if (dir.mkpath(path)) {
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                        | QFileDevice::ExeOwner);
        return true;
    }
    if (error) {
        *error = QStringLiteral("Não foi possível criar o diretório: %1").arg(path);
    }
    return false;
}

} // namespace

QString configDir()
{
    return baseLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/podstack");
}

QString dataDir()
{
    return baseLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/podstack");
}

QString stateDir()
{
    QString state = baseLocation(QStandardPaths::GenericStateLocation);
    if (state.isEmpty()) {
        state = baseLocation(QStandardPaths::GenericDataLocation);
    }
    return state + QStringLiteral("/podstack");
}

QString runtimeDir()
{
    return runtimeBase() + QStringLiteral("/podstack");
}

QString controlSocket()
{
    return runtimeDir() + QStringLiteral("/control.sock");
}

QString podmanSocket()
{
    const QString override = qEnvironmentVariable("PODSTACK_PODMAN_SOCKET");
    if (!override.isEmpty()) {
        return override;
    }
    return runtimeBase() + QStringLiteral("/podman/podman.sock");
}

bool ensureDirs(QString *error)
{
    const QStringList dirs{configDir(), dataDir(), stateDir(), runtimeDir()};
    for (const QString &dir : dirs) {
        if (!ensureDir(dir, error)) {
            return false;
        }
    }
    return true;
}

} // namespace PodStack::Paths
