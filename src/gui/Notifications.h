// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <functional>

class QSystemTrayIcon;

namespace PodStack {

/// Notificações desktop de eventos de container (falha, OOM, unhealthy).
///
/// Usa org.freedesktop.Notifications quando disponível; sem servidor de
/// notificações cai para o balão do ícone da bandeja.
class NotificationService : public QObject {
    Q_OBJECT

public:
    explicit NotificationService(QObject *parent = nullptr);

    void setTrayIcon(QSystemTrayIcon *icon);
    /// Injeta o envio (usado em testes para não disparar notificações reais).
    using Sender = std::function<void(const QString &, const QString &, const QString &)>;
    void setSenderForTesting(Sender sender);

    static QString summaryFor(const QVariantMap &event);
    static QString bodyFor(const QVariantMap &event);

    /// Aplica um cooldown por container/ação para evitar enxurrada de eventos.
    bool shouldNotify(const QVariantMap &event, qint64 nowMs);

    /// Notifica uma atualização disponível (cooldown de 24 h por versão).
    void notifyUpdate(const QString &version, const QString &channel, const QString &url);
    void notifyUpdateError(const QString &reason);
    /// Avisa disco baixo com histerese de 5 pontos e cooldown de 6 h por caminho.
    void notifyDiskLow(const QVariantMap &sample);
    bool shouldNotifyDisk(const QVariantMap &sample, qint64 nowMs);

public slots:
    void notifyContainerEvent(const QVariantMap &event);

signals:
    void containerActivated(const QString &id);

private slots:
    void onActionInvoked(uint id, const QString &key);
    void onNotificationClosed(uint id, uint reason);

private:
    void send(const QString &summary, const QString &body, const QString &containerId);
    bool cooldownAllows(const QString &key, qint64 nowMs, qint64 windowMs);

    QSystemTrayIcon *m_trayIcon = nullptr;
    Sender m_sender;
    QHash<QString, qint64> m_lastSent;
    QHash<QString, bool> m_diskArmed;
    QHash<uint, QString> m_ids;
};

} // namespace PodStack
