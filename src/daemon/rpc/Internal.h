// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/error/Error.h"
#include "core/config/Config.h"
#include "podman/PodmanClient.h"
#include "core/helper/HelperClient.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace PodStack::DaemonInternal {

inline constexpr qint64 kMachineConsoleDefaultTail = 64LL * 1024;
inline constexpr qint64 kMachineConsoleMaxBytes = 512LL * 1024;

QString selectedPodmanSocket();
QString engineMode();
bool portNumberFromJson(const QJsonValue &value, int *number);
QMap<QString, QString> labelsFromParams(const QJsonObject &params);
QStringList aliasesFromParams(const QJsonObject &params);
QStringList proxyExcludesFromConfig(const Config &config);
void applyProxyEnvironment(ContainerCreateSpec &spec);
void applyCaTrustMount(ContainerCreateSpec &spec);
bool isWildcardHostIp(const QString &hostIp);
qint64 consoleTailBytes(const QJsonObject &params);
bool checkMachineConsoleFile(const QString &directory, bool *exists, Error *error);
Error helperRequiredError();

bool readMachineConsoleTail(const QString &path, qint64 tailBytes, QByteArray *data,
                            qint64 *endOffset, Error *error);
bool readMachineConsoleChunk(const QString &path, qint64 offset, qint64 maxBytes,
                             QByteArray *data, qint64 *endOffset, Error *error);
QJsonArray collectReportLogs(const QString &directory, HelperClient *helper);

} // namespace PodStack::DaemonInternal
