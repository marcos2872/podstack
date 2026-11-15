// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/rpc/StreamClient.h"
#include "gui/controllers/ControllerBase.h"

#include <QFutureWatcher>
#include <QJsonObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace PodStack {

class MachineController : public ControllerBase {
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(QVariantList distros READ distros NOTIFY distrosChanged)
    Q_PROPERTY(QVariantMap info READ info NOTIFY infoChanged)
    Q_PROPERTY(QVariantList serialPorts READ serialPorts NOTIFY serialPortsChanged)
    Q_PROPERTY(QVariantList usbDevices READ usbDevices NOTIFY usbDevicesChanged)
    Q_PROPERTY(QVariantList snapshots READ snapshots NOTIFY snapshotsChanged)
    Q_PROPERTY(QString snapshotsError READ snapshotsError NOTIFY snapshotsChanged)
    Q_PROPERTY(bool createActive READ createActive NOTIFY createStateChanged)
    Q_PROPERTY(QString jobKind READ jobKind NOTIFY createStateChanged)
    Q_PROPERTY(QString createJobId READ createJobId NOTIFY createStateChanged)
    Q_PROPERTY(QString createStage READ createStage NOTIFY createStateChanged)
    Q_PROPERTY(int createProgress READ createProgress NOTIFY createStateChanged)
    Q_PROPERTY(QString createError READ createError NOTIFY createStateChanged)

public:
    explicit MachineController(QObject *parent = nullptr);

    QVariantList items() const { return m_machines; }
    QVariantList distros() const { return m_machineDistros; }
    QVariantMap info() const { return m_machineInfo; }
    QVariantList serialPorts() const { return m_machineSerialPorts; }
    QVariantList usbDevices() const { return m_machineUsbDevices; }
    QVariantList snapshots() const { return m_machineSnapshots; }
    QString snapshotsError() const { return m_machineSnapshotsError; }
    bool createActive() const { return m_machineCreateActive; }
    QString jobKind() const { return m_machineJobKind; }
    QString createJobId() const { return m_machineCreateJobId; }
    QString createStage() const { return m_machineCreateStage; }
    int createProgress() const { return m_machineCreateProgress; }
    QString createError() const { return m_machineCreateError; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshDistros();
    Q_INVOKABLE void loadInfo(const QString &id);
    Q_INVOKABLE void refreshSerialPorts(const QString &id);
    Q_INVOKABLE void attachSerialPort(const QString &id, const QString &path);
    Q_INVOKABLE void detachSerialPort(const QString &id, const QString &path);
    Q_INVOKABLE void refreshUsbDevices(const QString &id);
    Q_INVOKABLE void attachUsbDevice(const QString &id, int bus, int address);
    Q_INVOKABLE void detachUsbDevice(const QString &id, int bus, int address);
    Q_INVOKABLE void setAudio(const QString &id, bool enabled);
    Q_INVOKABLE void refreshSnapshots(const QString &id);
    Q_INVOKABLE void createSnapshot(const QString &id, const QString &name);
    Q_INVOKABLE void restoreSnapshot(const QString &id, const QString &name);
    Q_INVOKABLE void removeSnapshot(const QString &id, const QString &name);
    Q_INVOKABLE void lifecycle(const QString &action, const QString &id, bool force = false);
    Q_INVOKABLE void remove(const QString &id, bool force);
    Q_INVOKABLE void rename(const QString &id, const QString &name);
    Q_INVOKABLE void clone(const QString &id, const QString &name);
    Q_INVOKABLE void update(const QVariantMap &spec);
    Q_INVOKABLE void setPorts(const QString &id, const QVariantList &ports);
    Q_INVOKABLE void create(const QVariantMap &spec);
    Q_INVOKABLE void cancelCreate();
    Q_INVOKABLE void resetCreateState();
    Q_INVOKABLE void exportTo(const QString &id, const QString &path, bool force = false);
    Q_INVOKABLE void importFrom(const QString &path, const QString &name = QString(),
                                bool force = false);
    Q_INVOKABLE QString archivePath(const QString &id) const;
    Q_INVOKABLE void batch(const QStringList &ids, const QString &action, bool force = false);
    Q_INVOKABLE void openSsh(const QString &id);

signals:
    void itemsChanged();
    void distrosChanged();
    void infoChanged();
    void serialPortsChanged();
    void usbDevicesChanged();
    void snapshotsChanged();
    void createStateChanged();
    void actionFinished(bool ok, const QString &error);
    void terminalRequested(const QString &title, const QString &program, const QStringList &args);

protected:
    void onActionFinished(const QString &method, const QJsonObject &params,
                          const QJsonObject &result) override;

private:
    void onMachinesFinished();
    void onMachineDistrosFinished();
    void onMachineInfoFinished();
    void onMachineSerialFinished();
    void onMachineUsbFinished();
    void onMachineSnapshotsFinished();

    QFutureWatcher<QJsonObject> m_machinesWatcher;
    QFutureWatcher<QJsonObject> m_machineDistrosWatcher;
    QFutureWatcher<QJsonObject> m_machineInfoWatcher;
    QFutureWatcher<QJsonObject> m_machineSerialWatcher;
    QFutureWatcher<QJsonObject> m_machineUsbWatcher;
    QFutureWatcher<QJsonObject> m_machineSnapshotsWatcher;
    QFutureWatcher<QJsonObject> m_machineSshWatcher;
    StreamClient m_machineCreateStream;

    QVariantList m_machines;
    QVariantList m_machineDistros;
    QVariantMap m_machineInfo;
    QVariantList m_machineSerialPorts;
    QVariantList m_machineUsbDevices;
    QVariantList m_machineSnapshots;
    QString m_machineSnapshotsError;
    QString m_pendingMachineSnapshotsId;
    QString m_pendingMachineInfoId;
    bool m_machineDistrosLoaded = false;
    bool m_machinesRefreshAgain = false;
    bool m_machineCreateActive = false;
    QString m_machineJobKind;
    QString m_machineCreateJobId;
    QString m_machineCreateStage;
    int m_machineCreateProgress = 0;
    QString m_machineCreateError;
    QString m_machineSshTitle;
};

} // namespace PodStack
