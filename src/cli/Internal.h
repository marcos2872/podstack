// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/config/Paths.h"
#include "core/error/Error.h"
#include "core/helper/HelperClient.h"
#include "core/logging/LogReader.h"
#include "core/logging/Logging.h"
#include "core/logging/Redaction.h"
#include "core/rpc/DaemonBootstrap.h"
#include "core/rpc/RpcClient.h"
#include "core/rpc/RpcStream.h"
#include "core/update/UpdateRunner.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTextStream>
#include <QThread>
#include <QVector>

#include <cmath>
#include <iostream>
#include <limits>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

namespace PodStack::CliInternal {

inline constexpr int kExitOk = 0;
inline constexpr int kExitError = 1;
inline constexpr int kExitUsage = 2;
inline constexpr int kExitOutdated = 3;
inline constexpr int kExitEngineUnavailable = 7;
inline constexpr int kExitCancelled = 9;

QTextStream &out();

QTextStream &err();

void printUsage();

int fail(const PodStack::Error &error);

QString humanBytes(qint64 bytes);

QString epochDate(qint64 epoch);

void printMachineUsage(QTextStream &stream);

bool parseSize(const QString &text, qint64 *bytes);

PodStack::Error errorFromJson(const QJsonObject &object);

QString formatSize(qint64 bytes);

QString machineSsh(const QJsonObject &machine);

QString machineDistroLabel(const QJsonObject &machine);

bool machinePortIsSsh(const QJsonObject &port);

QJsonArray machinePortsWithoutSsh(const QJsonArray &ports);

QString machinePortLabel(const QJsonObject &machine);

void printTopSample(const QJsonObject &sample);

QString imageLabel(const QJsonObject &item);

int levelRank(const QString &level);

bool validLogLevel(const QString &level);

bool validLogComponent(const QString &component);

QDate parseSince(const QString &text);

void printKubeUsage(QTextStream &stream);

int kubeStatus(const QString &cluster, QJsonObject *result, PodStack::Error *error);

void printConfigUsage(QTextStream &stream);

QString formatConfigValue(const QJsonValue &value);

bool fetchConfigSchema(QHash<QString, QJsonObject> *schema, PodStack::Error *error);

bool parseConfigValue(const QJsonObject &definition, const QString &raw, QJsonValue *value,
                      QString *error);

class DbusUpdateHelper : public PodStack::UpdateHelper {
public:
    bool call(const QString &method, const QVariantList &arguments, QVariantMap *reply,
              PodStack::Error *error) override
    {
        PodStack::HelperClient client;
        QString helperError;
        QVariantMap values;
        const bool ok = client.callMap(method, arguments, &values, &helperError);
        if (reply) {
            *reply = values;
        }
        if (ok) {
            return true;
        }
        if (error) {
            error->code = values.value(QStringLiteral("code")).toString();
            if (error->code.isEmpty()) {
                error->code = QStringLiteral("PODSTACK_HELPER_REQUIRED");
            }
            error->message = values.value(QStringLiteral("message")).toString();
            if (error->message.isEmpty()) {
                error->message = helperError;
            }
            error->detail = values.value(QStringLiteral("detail")).toString();
        }
        return false;
    }
};

int commandPs(const QStringList &args);

int commandLifecycle(const QString &action, const QStringList &args);

int commandInspect(const QStringList &args);

int commandExec(const QStringList &args);

int commandCheckpoint(const QStringList &args);

int commandRestore(const QStringList &args);

int commandImages(const QStringList &args);

int commandVolumes(const QStringList &args);

int commandNetworks(const QStringList &args);

int commandPods(const QStringList &args);

int commandCompose(const QStringList &args);

int commandBuild(const QStringList &args);

int commandBuilds(const QStringList &args);

int commandMachine(const QStringList &args);

int machineListCommand(const QStringList &args);

int machineInfoCommand(const QStringList &args);

int machineLogsCommand(const QStringList &args);

int machineConsoleCommand(const QStringList &args);

int machineCreateCommand(const QStringList &args);

int machineLifecycleCommand(const QString &action, const QStringList &args);

int machineDeleteCommand(const QStringList &args);

int machineExportCommand(const QStringList &args);

int machineImportCommand(const QStringList &args);

int machineRenameCommand(const QStringList &args);

int machineCloneCommand(const QStringList &args);

int machineUpdateCommand(const QStringList &args);

int machinePortListCommand(const QStringList &args);

int machinePortAddCommand(const QStringList &args);

int machinePortRemoveCommand(const QStringList &args);

int machinePortCommand(const QStringList &args);

int machineSerialCommand(const QStringList &args);

int machineAudioCommand(const QStringList &args);

int machineUsbCommand(const QStringList &args);

int machineSnapshotListCommand(const QStringList &args);

int machineSnapshotCreateCommand(const QStringList &args);

int machineSnapshotRestoreCommand(const QStringList &args);

int machineSnapshotRemoveCommand(const QStringList &args);

int machineSnapshotCommand(const QStringList &args);

int machineDefaultCommand(const QStringList &args);

int machineSshCommand(const QStringList &args);

int machineFilesCommand(const QStringList &args);

int machineTransferCommand(const QString &direction, const QStringList &args);

int commandVersion();

int commandStatus();

int commandReport(const QStringList &args);

int commandUpdate(const QStringList &args);

int commandDoctor();

int commandTop(const QStringList &args);

int commandSystemLogs(const QStringList &args);

int commandSystem(const QStringList &args);

int commandKube(const QStringList &args);

int kubeInfoCommand(const QStringList &args);

int kubeEnvCommand(const QStringList &args);

int kubeJobCommand(const QString &method, const QString &label, const QJsonObject &params);

int kubeStartCommand();

int kubeDisableCommand(const QStringList &args);

int kubeContextCommand(const QStringList &args);

int commandSetup(const QStringList &args);

int commandConfigShow(const QStringList &args);

int commandConfigGet(const QStringList &args);

int commandConfigSet(const QStringList &args);

int commandConfigList(const QString &operation, const QStringList &args);

int commandConfigReset(const QStringList &args);

int commandConfigSchema(const QStringList &args);

int commandConfig(const QStringList &args);

} // namespace PodStack::CliInternal
