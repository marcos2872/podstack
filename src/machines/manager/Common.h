// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/MachineManager.h"

#include "core/config/Config.h"
#include "core/util/Ids.h"
#include "core/logging/Logging.h"
#include "core/config/Paths.h"
#include "core/system/Proxy.h"
#include "machines/provision/CloudInit.h"
#include "machines/operations/MachineArchive.h"
#include "machines/operations/SerialPorts.h"
#include "machines/operations/UsbDevices.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTimeZone>

#include <utility>


#include "machines/manager/Internal.h"
