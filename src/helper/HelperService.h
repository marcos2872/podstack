// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "helper/updates/UpdateService.h"

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
    // `callerPid` é o PID do processo interativo que pediu a ação. O daemon roda
    // fora da sessão gráfica, e o polkit resolve o agente de autenticação a
    // partir do subject: autorizar o daemon nunca acha a sessão e o prompt não
    // abre. Quem chama da sessão (GUI/CLI) pode passar 0 e o helper usa o PID do
    // próprio remetente D-Bus, que já é o processo da sessão.
    Q_SCRIPTABLE QVariantMap InstallPackages(const QStringList &packages, uint callerPid = 0);
    Q_SCRIPTABLE QVariantMap RemovePackages(const QStringList &packages, uint callerPid = 0);
    Q_SCRIPTABLE QVariantMap OpenPorts(const QStringList &ports, const QString &protocol,
                                       uint callerPid = 0);
    Q_SCRIPTABLE QVariantMap ClosePorts(const QStringList &ports, const QString &protocol,
                                        uint callerPid = 0);
    Q_SCRIPTABLE QVariantMap InstallDnsZone(const QString &zone, const QString &upstream,
                                            uint callerPid = 0);
    Q_SCRIPTABLE QVariantMap RemoveDnsZone(const QString &zone, uint callerPid = 0);
    Q_SCRIPTABLE QVariantMap BindPorts(const QStringList &ports, const QString &httpTarget,
                                       const QString &httpsTarget, uint callerPid = 0);
    Q_SCRIPTABLE QVariantMap UnbindPorts(const QStringList &ports, uint callerPid = 0);
    Q_SCRIPTABLE QVariantMap TrustCa(const QString &certPath, const QString &fingerprint,
                                     uint callerPid = 0);
    Q_SCRIPTABLE QVariantMap UntrustCa(const QString &fingerprint, uint callerPid = 0);
    Q_SCRIPTABLE QVariantMap StageUpdate(const QByteArray &manifest, const QByteArray &signature,
                                         const QString &artifactPath, const QString &channel,
                                         uint callerPid = 0);
    Q_SCRIPTABLE QVariantMap InstallUpdate(const QString &jobId, uint callerPid = 0);
    Q_SCRIPTABLE QVariantMap GetUpdateStatus(const QString &jobId, uint callerPid = 0);
    Q_SCRIPTABLE QVariantMap GetLogs(const QString &jobId, uint callerPid = 0);

protected:
    /// Ponto de extensão para testes; em produção exige polkit via D-Bus.
    virtual bool authorize(const QString &actionId, uint callerPid, QString *error) const;

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
                        const QStringList &allowed, uint callerPid);
    QVariantMap remove(const QStringList &packages, uint callerPid);
    QVariantMap changePorts(const QStringList &ports, const QString &protocol,
                            const QString &actionId, bool open, uint callerPid);
    QVariantMap changeDnsZone(const QString &zone, const QString &upstream, bool install,
                              uint callerPid);
    QVariantMap changePortBindings(const QStringList &ports, const QString &httpTarget,
                                   const QString &httpsTarget, bool bind, uint callerPid);
    QVariantMap changeCaTrust(const QString &certPath, const QString &fingerprint, bool trust,
                              uint callerPid);
    uint callerUid() const;
    /// PID do remetente D-Bus (o daemon, quando ele repassa a chamada).
    uint senderPid() const;
    /// PID que o polkit deve autorizar: o informado pelo chamador, validado
    /// contra o `uid` dele, ou o do próprio remetente quando não há nenhum.
    /// Retorna false (falha fechada) quando o PID informado é de outro usuário.
    bool authorizationPid(uint callerPid, uint *pid, QString *error) const;

    static QVariantMap result(bool ok, const QString &message);
    static QVariantMap updateFailure(const Error &error);
    /// Consulta distro-aware (rpm/dpkg-query/pacman). Retorna false e preenche
    /// `error` se a ferramenta não existir ou falhar; pacote ausente não é erro.
    static bool installedPackages(const QStringList &packages, QStringList *installed,
                                  QString *error);

    std::unique_ptr<UpdateService> m_updateService;
};

} // namespace PodStack
