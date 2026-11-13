// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"
#include "machines/MachineMeta.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace PodStack {

/// Resultado de uma sonda de canal de guest.
struct GuestProbeResult {
    bool ready = false;
    QJsonObject osInfo;
    QVector<GuestChannelInfo> channels;
    QString error;
};

/// Acesso ao qemu-guest-agent e sonda SSH de uma machine.
class GuestChannel {
public:
    GuestChannel(QString agentSocketPath, QString qmpSocketPath = {},
                 bool reconnectOnFailure = false);

    static bool probeSsh(const MachineMeta &machine, int timeoutMs, Error *error = nullptr);

    QString agentSocketPath() const { return m_agentSocketPath; }

    bool ping(Error *error = nullptr);
    bool getOsInfo(QJsonObject *info, Error *error = nullptr);
    bool getInfo(QJsonObject *info, Error *error = nullptr);
    bool networkInterfaces(QJsonArray *interfaces, Error *error = nullptr);
    bool exec(const QString &path, const QStringList &arguments, int timeoutMs, int *exitCode,
              QByteArray *stdoutData, QByteArray *stderrData, Error *error = nullptr);
    bool readFile(const QString &path, qint64 maxBytes, QByteArray *data, Error *error = nullptr);
    bool writeFile(const QString &path, const QByteArray &data, Error *error = nullptr);

    /// Reconstroi o chardev/device do agente na ordem exigida pelo QEMU.
    bool reconnect(Error *error = nullptr);

private:
    QString m_agentSocketPath;
    QString m_qmpSocketPath;
    bool m_reconnectOnFailure = false;
};

} // namespace PodStack
