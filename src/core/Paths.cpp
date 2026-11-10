#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
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

QString cacheDir()
{
    QString cache = qEnvironmentVariable("XDG_CACHE_HOME");
    if (cache.isEmpty()) {
        cache = baseLocation(QStandardPaths::GenericCacheLocation);
    }
    if (cache.isEmpty()) {
        cache = QDir::homePath() + QStringLiteral("/.cache");
    }
    return cache + QStringLiteral("/podstack");
}

QString cloudImagesDir()
{
    return cacheDir() + QStringLiteral("/images");
}

QString machinesDir()
{
    return dataDir() + QStringLiteral("/machines");
}

QString machineDir(const QString &id)
{
    static const QRegularExpression validId(QStringLiteral("^[A-Za-z0-9._:-]+$"));
    if (id.isEmpty() || id == QStringLiteral(".") || id.contains(QStringLiteral(".."))
        || !validId.match(id).hasMatch()) {
        return {};
    }
    return machinesDir() + QLatin1Char('/') + id;
}

QString runtimeDir()
{
    return runtimeBase() + QStringLiteral("/podstack");
}

QString controlSocket()
{
    const QString override = qEnvironmentVariable("PODSTACK_CONTROL_SOCKET");
    if (!override.isEmpty()) {
        return override;
    }
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
    const QStringList dirs{configDir(), dataDir(), stateDir(), cacheDir(), cloudImagesDir(),
                           machinesDir(), runtimeDir()};
    for (const QString &dir : dirs) {
        if (!ensureDir(dir, error)) {
            return false;
        }
    }
    return true;
}

} // namespace PodStack::Paths
