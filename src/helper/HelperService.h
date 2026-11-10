// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusContext>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

namespace PodStack {

/// Interface D-Bus de sistema do helper privilegiado.
///
/// Todos os métodos validam a allowlist de pacotes e exigem autorização polkit
/// do processo chamador antes de executar qualquer ação.
class HelperService : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "dev.podstack.Helper1")

public:
    explicit HelperService(QObject *parent = nullptr);

public slots:
    Q_SCRIPTABLE QVariantMap InstallPackages(const QStringList &packages);
    Q_SCRIPTABLE QVariantMap RemovePackages(const QStringList &packages);
    Q_SCRIPTABLE QVariantMap OpenPorts(const QStringList &ports, const QString &protocol);
    Q_SCRIPTABLE QVariantMap ClosePorts(const QStringList &ports, const QString &protocol);

protected:
    /// Ponto de extensão para testes; em produção exige polkit via D-Bus.
    virtual bool authorize(const QString &actionId, QString *error) const;

private:
    bool validatePackages(const QStringList &packages, const QStringList &allowed,
                          QString *error) const;
    QVariantMap install(const QStringList &packages, const QString &actionId,
                        const QStringList &allowed);
    QVariantMap remove(const QStringList &packages);
    QVariantMap changePorts(const QStringList &ports, const QString &protocol,
                            const QString &actionId, bool open);
    uint callerUid() const;

    static QVariantMap result(bool ok, const QString &message);
    static QStringList installedPackages(const QStringList &packages);
};

} // namespace PodStack
