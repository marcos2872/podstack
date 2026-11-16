// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/config/Config.h"
#include "core/error/Error.h"
#include "core/helper/HelperClient.h"
#include "podman/PodmanClient.h"

#include <QJsonObject>
#include <QMap>
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

/// Portas publicadas no inspect do container (`HostConfig.PortBindings`),
/// agrupadas por protocolo. Usado pelo expose LAN e para fechar as regras de
/// firewall quando o container é removido.
struct PublishedPorts {
    QMap<QString, QStringList> byProtocol;
    bool any = false;
    bool wildcardOnly = true;
    bool unsupportedProtocol = false;
};
PublishedPorts publishedPorts(const QJsonObject &inspect);
qint64 consoleTailBytes(const QJsonObject &params);
bool checkMachineConsoleFile(const QString &directory, bool *exists, Error *error);
Error helperRequiredError();

/// Valida o nome do container no mesmo formato do Podman (letra/dígito
/// inicial, depois letras, dígitos, `_`, `.` e `-`, até 255). Vazio é válido
/// (o engine gera um nome).
bool validateContainerName(const QString &name, QString *error);
/// Nome livre a partir de `name` (sufixo `-2`, `-3`, ...) quando há conflito.
QString suggestContainerName(const QString &name, const QStringList &taken);
/// Portas 1–65535 e sem `hostPort`+protocolo repetido.
bool validatePortMappings(const QVector<PortMapping> &ports, QString *error);
/// Mounts: origem absoluta e existente, destino absoluto, sem `..`.
bool validateMounts(const QVector<MountMapping> &mounts, QString *error);

bool readMachineConsoleTail(const QString &path, qint64 tailBytes, QByteArray *data,
                            qint64 *endOffset, Error *error);
bool readMachineConsoleChunk(const QString &path, qint64 offset, qint64 maxBytes, QByteArray *data,
                             qint64 *endOffset, Error *error);
QJsonArray collectReportLogs(const QString &directory, HelperClient *helper);

} // namespace PodStack::DaemonInternal
