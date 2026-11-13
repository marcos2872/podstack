// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "helper/UpdateService.h"

#include <QDBusContext>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

#include <memory>

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
    Q_SCRIPTABLE QVariantMap InstallDnsZone(const QString &zone, const QString &upstream);
    Q_SCRIPTABLE QVariantMap RemoveDnsZone(const QString &zone);
    Q_SCRIPTABLE QVariantMap BindPorts(const QStringList &ports, const QString &httpTarget,
                                       const QString &httpsTarget);
    Q_SCRIPTABLE QVariantMap UnbindPorts(const QStringList &ports);
    Q_SCRIPTABLE QVariantMap TrustCa(const QString &certPath, const QString &fingerprint);
    Q_SCRIPTABLE QVariantMap UntrustCa(const QString &fingerprint);
    Q_SCRIPTABLE QVariantMap StageUpdate(const QByteArray &manifest, const QByteArray &signature,
                                         const QString &artifactPath, const QString &channel);
    Q_SCRIPTABLE QVariantMap InstallUpdate(const QString &jobId);
    Q_SCRIPTABLE QVariantMap GetUpdateStatus(const QString &jobId);

protected:
    /// Ponto de extensão para testes; em produção exige polkit via D-Bus.
    virtual bool authorize(const QString &actionId, QString *error) const;

    /// Pontos de extensão para teste sem tocar em `/etc` nem no systemd.
    virtual QString resolvedDirectory() const;
    virtual bool reloadResolved(QString *error) const;
    virtual bool avahiActive() const;
    virtual QString systemUnitDirectory() const;
    virtual bool runSystemctl(const QStringList &arguments, QString *error) const;
    virtual QString caAnchorsDirectory() const;
    virtual bool runTrustUpdate(QString *error) const;
    virtual UpdateService *updateService();

private:
    bool validatePackages(const QStringList &packages, const QStringList &allowed,
                          QString *error) const;
    QVariantMap install(const QStringList &packages, const QString &actionId,
                        const QStringList &allowed);
    QVariantMap remove(const QStringList &packages);
    QVariantMap changePorts(const QStringList &ports, const QString &protocol,
                            const QString &actionId, bool open);
    QVariantMap changeDnsZone(const QString &zone, const QString &upstream, bool install);
    QVariantMap changePortBindings(const QStringList &ports, const QString &httpTarget,
                                   const QString &httpsTarget, bool bind);
    QVariantMap changeCaTrust(const QString &certPath, const QString &fingerprint, bool trust);
    uint callerUid() const;

    static QVariantMap result(bool ok, const QString &message);
    static QVariantMap updateFailure(const Error &error);
    static QStringList installedPackages(const QStringList &packages);

    std::unique_ptr<UpdateService> m_updateService;
};

} // namespace PodStack
