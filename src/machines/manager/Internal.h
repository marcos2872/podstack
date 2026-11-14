// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/config/Config.h"
#include "core/error/Error.h"
#include "machines/MachineManager.h"

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QTimeZone>

namespace PodStack::MachineInternal {

inline constexpr qint64 kMinimumMemoryBytes = 1024LL * 1024LL * 1024LL;
inline constexpr qint64 kMinimumDiskBytes = 1024LL * 1024LL * 1024LL;
inline constexpr int kMaximumCpus = 256;
inline constexpr int kMaximumMounts = 8;
inline constexpr int kPortAttempts = 20;
inline constexpr int kQemuImgStartTimeoutMs = 5000;
inline constexpr int kQemuImgTimeoutMs = 60000;
inline constexpr int kSshKeygenStartTimeoutMs = 5000;
inline constexpr int kSshKeygenTimeoutMs = 30000;
inline constexpr int kMaxProcessOutput = 4096;
inline constexpr qint64 kMaxPublicKeyBytes = 64LL * 1024;
inline const QString kAavmfCodeArm64 = QStringLiteral("/usr/share/AAVMF/AAVMF_CODE.fd");
inline const QString kAavmfVarsArm64 = QStringLiteral("/usr/share/AAVMF/AAVMF_VARS.fd");

Error invalidParams(const QString &message, const QString &detail = {},
                    const QString &remediation = {});

QTimeZone utcTimeZone();
bool validMachineName(const QString &name);
Error invalidMachineName(const QString &name);
bool validSnapshotName(const QString &name);
Error invalidSnapshotName(const QString &name);
bool fails(Error *error, const Error &value);
QString processDetail(const QByteArray &output);
QString errorText(const Error &error);
QString normalizedPortProtocol(const QString &protocol);
bool isSshPort(const MachinePort &port);
bool samePort(const MachinePort &left, const MachinePort &right);
QStringList proxyExcludesFromConfig(const Config &config);
QMap<QString, QString> proxyEnvironmentFromConfig();
void setOwnerOnly(const QString &path, bool directory);
void cleanupDirectory(const QString &directory);
void cleanupRuntimeFiles(const QString &directory, const MachineMeta &meta);
bool processReferencesPath(qint64 pid, const QString &root);

} // namespace PodStack::MachineInternal
