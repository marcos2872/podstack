// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"
#include "machines/QemuCommand.h"

#include <QString>

namespace PodStack {

/// Abstração do processo QEMU usada pelo MachineManager (permite test doubles).
class QemuRuntime {
public:
    virtual ~QemuRuntime() = default;

    virtual bool start(const QemuSpec &spec, Error *error) = 0;
    virtual bool stop(const QemuSpec &spec, bool force, Error *error) = 0;
    virtual bool isRunning(const QemuSpec &spec, Error *error) = 0;
    virtual QString status(const QemuSpec &spec, Error *error) = 0;
    virtual QString pid(const QemuSpec &spec) = 0;
};

/// Runtime real: inicia o QEMU destacado e controla o processo via pidfile/QMP.
class QemuProcessRuntime : public QemuRuntime {
public:
    static constexpr int kPidFileTimeoutMs = 5000;
    static constexpr int kQmpTimeoutMs = 5000;
    static constexpr int kPowerdownTimeoutMs = 60000;
    static constexpr int kQuitTimeoutMs = 5000;
    static constexpr int kTermTimeoutMs = 2000;
    static constexpr int kKillTimeoutMs = 2000;

    bool start(const QemuSpec &spec, Error *error) override;
    bool stop(const QemuSpec &spec, bool force, Error *error) override;
    bool isRunning(const QemuSpec &spec, Error *error) override;
    QString status(const QemuSpec &spec, Error *error) override;
    QString pid(const QemuSpec &spec) override;
};

} // namespace PodStack
