// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/MachineStore.h"

#include "core/Paths.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <sqlite3.h>

#include <memory>
#include <utility>

namespace PodStack {
namespace {

using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

void bindText(sqlite3_stmt *statement, int index, const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    sqlite3_bind_text(statement, index, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
}

QString columnText(sqlite3_stmt *statement, int column)
{
    const unsigned char *text = sqlite3_column_text(statement, column);
    return text ? QString::fromUtf8(reinterpret_cast<const char *>(text)) : QString();
}

bool execSql(sqlite3 *db, const char *sql, QString *error)
{
    char *message = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &message) != SQLITE_OK) {
        if (error) {
            *error = QString::fromUtf8(message ? message : "erro SQLite");
        }
        sqlite3_free(message);
        return false;
    }
    return true;
}

Statement prepare(sqlite3 *db, const char *sql, bool *ok)
{
    sqlite3_stmt *statement = nullptr;
    *ok = sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK;
    return Statement(statement, sqlite3_finalize);
}

QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

void restrictDatabaseFiles(const QString &path)
{
    const QStringList files{path, path + QStringLiteral("-wal"), path + QStringLiteral("-shm")};
    for (const QString &file : files) {
        if (QFileInfo::exists(file)) {
            QFile::setPermissions(file, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        }
    }
}

MachineMeta metaFromRow(sqlite3_stmt *statement)
{
    MachineMeta meta;
    meta.id = columnText(statement, 0);
    meta.name = columnText(statement, 1);
    meta.distro = columnText(statement, 2);
    meta.distroVersion = columnText(statement, 3);
    meta.provisioner = columnText(statement, 4);
    if (meta.provisioner.isEmpty()) {
        meta.provisioner = QStringLiteral("linux");
    }
    meta.arch = columnText(statement, 5);
    meta.cpus = sqlite3_column_int(statement, 6);
    meta.memoryBytes = sqlite3_column_int64(statement, 7);
    meta.diskBytes = sqlite3_column_int64(statement, 8);
    meta.diskPath = columnText(statement, 9);
    meta.seedPath = columnText(statement, 10);
    meta.sshPort = sqlite3_column_int(statement, 11);
    meta.sshUser = columnText(statement, 12);
    meta.sshKeyPath = columnText(statement, 13);
    meta.state = columnText(statement, 14);
    meta.isolated = sqlite3_column_int(statement, 15) != 0;
    meta.isolateNetwork = sqlite3_column_int(statement, 16) != 0;
    meta.forwardSshAgent = sqlite3_column_int(statement, 17) != 0;
    meta.lastBootAt = columnText(statement, 18);
    meta.createdAt = columnText(statement, 19);
    meta.updatedAt = columnText(statement, 20);
    meta.error = columnText(statement, 21);
    meta.windowsSsh = sqlite3_column_int(statement, 22) != 0;
    meta.windowsEdition = columnText(statement, 23);
    meta.installPending = sqlite3_column_int(statement, 24) != 0;
    meta.installerIso = columnText(statement, 25);
    meta.virtioWinIso = columnText(statement, 26);
    meta.errorReason = columnText(statement, 27);
    meta.guestReady = sqlite3_column_int(statement, 28) != 0;
    const QString guestOs = columnText(statement, 29);
    if (!guestOs.isEmpty()) {
        meta.guestOs = QJsonDocument::fromJson(guestOs.toUtf8()).object();
    }
    const QString guestChannels = columnText(statement, 30);
    if (!guestChannels.isEmpty()) {
        const QJsonArray channels = QJsonDocument::fromJson(guestChannels.toUtf8()).array();
        for (const QJsonValue &value : channels) {
            if (value.isObject()) {
                meta.guestChannels.append(GuestChannelInfo::fromJson(value.toObject()));
            }
        }
    }
    return meta;
}

const char *kMachineColumns = R"SQL(
SELECT id, name, distro, distro_version, provisioner, arch, cpus, memory_bytes, disk_bytes,
       disk_path, seed_path, ssh_port, ssh_user, ssh_key_path, state, isolated, isolate_network,
       forward_ssh_agent, last_boot_at, created_at, updated_at, error,
       windows_ssh, windows_edition, install_pending, installer_iso, virtio_win_iso,
       error_reason, guest_ready, guest_os, guest_channels
FROM machines
)SQL";

} // namespace

MachineStore::MachineStore(QString path)
    : m_path(path.isEmpty() ? Paths::dataDir() + QStringLiteral("/podstack.db") : std::move(path))
{
}

MachineStore::~MachineStore()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool MachineStore::fail(const QString &fallback, Error *error) const
{
    if (error) {
        *error = Error::make(QStringLiteral("PODSTACK_DB_ERROR"), fallback,
                             m_db ? QString::fromUtf8(sqlite3_errmsg(m_db)) : QString());
    }
    return false;
}

bool MachineStore::open(Error *error)
{
    if (error) {
        *error = {};
    }

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_db) {
        return true;
    }

    const QFileInfo info(m_path);
    const QString parent = info.absolutePath();
    const QFileInfo parentInfo(parent);
    if (!parentInfo.exists()) {
        if (!QDir().mkpath(parent)) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_DB_ERROR"),
                                     QStringLiteral("Não foi possível criar o diretório do banco."),
                                     parent);
            }
            return false;
        }
        if (!QFile::setPermissions(parent, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                               | QFileDevice::ExeOwner)) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_DB_ERROR"),
                                     QStringLiteral("Não foi possível restringir o diretório do "
                                                    "banco a 0700."),
                                     parent);
            }
            return false;
        }
    }

    if (sqlite3_open_v2(m_path.toUtf8().constData(), &m_db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr)
        != SQLITE_OK) {
        const QString detail = m_db ? QString::fromUtf8(sqlite3_errmsg(m_db)) : QString();
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_DB_ERROR"),
                QStringLiteral("Não foi possível abrir o banco de machines."), detail);
        }
        return false;
    }

    sqlite3_busy_timeout(m_db, 5000);
    QString sqlError;
    if (!execSql(m_db, "PRAGMA journal_mode=WAL;", &sqlError)
        || !execSql(m_db, "PRAGMA foreign_keys=ON;", &sqlError)) {
        const QString &detail = sqlError;
        sqlite3_close(m_db);
        m_db = nullptr;
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_DB_ERROR"),
                                 QStringLiteral("Falha ao configurar o banco de machines."),
                                 detail);
        }
        return false;
    }

    if (!ensureSchema(error)) {
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }

    if (!QFile::setPermissions(m_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        sqlite3_close(m_db);
        m_db = nullptr;
        if (error) {
            *error = Error::make(
                QStringLiteral("PODSTACK_DB_ERROR"),
                QStringLiteral("Não foi possível restringir o banco a 0600."), m_path);
        }
        return false;
    }
    restrictDatabaseFiles(m_path);
    return true;
}

bool MachineStore::ensureSchema(Error *error)
{
    const char *schema = R"SQL(
CREATE TABLE IF NOT EXISTS machines (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL UNIQUE,
  distro TEXT NOT NULL,
  distro_version TEXT,
  provisioner TEXT NOT NULL DEFAULT 'linux',
  arch TEXT NOT NULL,
  cpus INTEGER NOT NULL,
  memory_bytes INTEGER NOT NULL,
  disk_bytes INTEGER NOT NULL,
  disk_path TEXT NOT NULL,
  seed_path TEXT,
  ssh_port INTEGER NOT NULL,
  ssh_user TEXT NOT NULL,
  ssh_key_path TEXT NOT NULL,
  state TEXT NOT NULL,
  isolated INTEGER NOT NULL DEFAULT 0,
  isolate_network INTEGER NOT NULL DEFAULT 0,
  forward_ssh_agent INTEGER NOT NULL DEFAULT 0,
  http_port INTEGER,
  https_port INTEGER,
  last_boot_at TEXT,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  error TEXT,
  windows_ssh INTEGER NOT NULL DEFAULT 0,
  windows_edition TEXT,
  install_pending INTEGER NOT NULL DEFAULT 0,
  installer_iso TEXT,
  virtio_win_iso TEXT,
  error_reason TEXT,
  guest_ready INTEGER NOT NULL DEFAULT 0,
  guest_os TEXT,
  guest_channels TEXT
);
CREATE TABLE IF NOT EXISTS machine_ports (
  id TEXT PRIMARY KEY,
  machine_id TEXT NOT NULL REFERENCES machines(id) ON DELETE CASCADE,
  host_port INTEGER NOT NULL,
  guest_port INTEGER NOT NULL,
  protocol TEXT NOT NULL DEFAULT 'tcp',
  expose_lan INTEGER NOT NULL DEFAULT 0,
  label TEXT
);
CREATE TABLE IF NOT EXISTS machine_mounts (
  id TEXT PRIMARY KEY,
  machine_id TEXT NOT NULL REFERENCES machines(id) ON DELETE CASCADE,
  source TEXT NOT NULL,
  target TEXT NOT NULL,
  read_only INTEGER NOT NULL DEFAULT 0,
  tag TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS machine_serial_ports (
  machine_id TEXT NOT NULL REFERENCES machines(id) ON DELETE CASCADE,
  path TEXT NOT NULL,
  PRIMARY KEY (machine_id, path)
);
CREATE TABLE IF NOT EXISTS machine_usb_devices (
  machine_id TEXT NOT NULL REFERENCES machines(id) ON DELETE CASCADE,
  bus INTEGER NOT NULL,
  address INTEGER NOT NULL,
  vendor_id TEXT NOT NULL,
  product_id TEXT NOT NULL,
  description TEXT,
  PRIMARY KEY (machine_id, bus, address)
);
CREATE TABLE IF NOT EXISTS machine_settings (
  machine_id TEXT PRIMARY KEY REFERENCES machines(id) ON DELETE CASCADE,
  audio_enabled INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_machines_state ON machines(state);
)SQL";

    QString sqlError;
    if (!execSql(m_db, schema, &sqlError)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_DB_ERROR"),
                                 QStringLiteral("Falha ao criar o schema de machines."), sqlError);
        }
        return false;
    }

    // Migração aditiva: bancos criados antes do M9 não têm as colunas Windows.
    QSet<QString> existingColumns;
    sqlite3_stmt *columns = nullptr;
    if (sqlite3_prepare_v2(m_db, "PRAGMA table_info(machines);", -1, &columns, nullptr) !=
        SQLITE_OK) {
        return fail(QStringLiteral("Falha ao inspecionar o schema de machines."), error);
    }
    while (sqlite3_step(columns) == SQLITE_ROW) {
        const auto *name = sqlite3_column_text(columns, 1);
        if (name) {
            existingColumns.insert(QString::fromUtf8(reinterpret_cast<const char *>(name)));
        }
    }
    sqlite3_finalize(columns);

    const struct Migration {
        const char *name;
        const char *definition;
    } migrations[] = {
        {"provisioner", "TEXT NOT NULL DEFAULT 'linux'"},
        {"windows_ssh", "INTEGER NOT NULL DEFAULT 0"},
        {"windows_edition", "TEXT"},
        {"install_pending", "INTEGER NOT NULL DEFAULT 0"},
        {"installer_iso", "TEXT"},
        {"virtio_win_iso", "TEXT"},
        {"error_reason", "TEXT"},
        {"guest_ready", "INTEGER NOT NULL DEFAULT 0"},
        {"guest_os", "TEXT"},
        {"guest_channels", "TEXT"},
    };
    for (const Migration &migration : migrations) {
        if (existingColumns.contains(QString::fromLatin1(migration.name))) {
            continue;
        }
        const QByteArray sql = QByteArrayLiteral("ALTER TABLE machines ADD COLUMN ") +
                               migration.name + ' ' + migration.definition + ';';
        if (!execSql(m_db, sql.constData(), &sqlError)) {
            if (error) {
                *error =
                    Error::make(QStringLiteral("PODSTACK_DB_ERROR"),
                                QStringLiteral("Falha ao migrar o schema de machines."), sqlError);
            }
            return false;
        }
    }
    return true;
}

bool MachineStore::upsert(const MachineMeta &meta, Error *error)
{
    if (error) {
        *error = {};
    }
    if (meta.id.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("A machine precisa de um id."));
        }
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!open(error)) {
        return false;
    }

    QString sqlError;
    if (!execSql(m_db, "BEGIN IMMEDIATE;", &sqlError)) {
        return fail(QStringLiteral("Não foi possível iniciar a transação de upsert."), error);
    }
    const auto abort = [this, error](const QString &fallback) {
        const QString detail = m_db ? QString::fromUtf8(sqlite3_errmsg(m_db)) : QString();
        QString ignored;
        execSql(m_db, "ROLLBACK;", &ignored);
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_DB_ERROR"), fallback, detail);
        }
        return false;
    };

    const char *machineSql = R"SQL(
INSERT INTO machines
(id, name, distro, distro_version, provisioner, arch, cpus, memory_bytes, disk_bytes, disk_path,
 seed_path, ssh_port, ssh_user, ssh_key_path, state, isolated, isolate_network,
 forward_ssh_agent, last_boot_at, created_at, updated_at, error,
 windows_ssh, windows_edition, install_pending, installer_iso, virtio_win_iso,
 error_reason, guest_ready, guest_os, guest_channels)
VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,
        ?, ?, ?, ?)
ON CONFLICT(id) DO UPDATE SET
  name = excluded.name,
  distro = excluded.distro,
  distro_version = excluded.distro_version,
  provisioner = excluded.provisioner,
  arch = excluded.arch,
  cpus = excluded.cpus,
  memory_bytes = excluded.memory_bytes,
  disk_bytes = excluded.disk_bytes,
  disk_path = excluded.disk_path,
  seed_path = excluded.seed_path,
  ssh_port = excluded.ssh_port,
  ssh_user = excluded.ssh_user,
  ssh_key_path = excluded.ssh_key_path,
  state = excluded.state,
  isolated = excluded.isolated,
  isolate_network = excluded.isolate_network,
  forward_ssh_agent = excluded.forward_ssh_agent,
  last_boot_at = excluded.last_boot_at,
  created_at = excluded.created_at,
  updated_at = excluded.updated_at,
  error = excluded.error,
  windows_ssh = excluded.windows_ssh,
  windows_edition = excluded.windows_edition,
  install_pending = excluded.install_pending,
  installer_iso = excluded.installer_iso,
  virtio_win_iso = excluded.virtio_win_iso,
  error_reason = excluded.error_reason,
  guest_ready = excluded.guest_ready,
  guest_os = excluded.guest_os,
  guest_channels = excluded.guest_channels
)SQL";

    bool ok = false;
    Statement machine = prepare(m_db, machineSql, &ok);
    if (!ok) {
        return abort(QStringLiteral("Falha ao preparar o upsert da machine."));
    }

    bindText(machine.get(), 1, meta.id);
    bindText(machine.get(), 2, meta.name);
    bindText(machine.get(), 3, meta.distro);
    bindText(machine.get(), 4, meta.distroVersion);
    bindText(machine.get(), 5, meta.provisioner);
    bindText(machine.get(), 6, meta.arch);
    sqlite3_bind_int(machine.get(), 7, meta.cpus);
    sqlite3_bind_int64(machine.get(), 8, meta.memoryBytes);
    sqlite3_bind_int64(machine.get(), 9, meta.diskBytes);
    bindText(machine.get(), 10, meta.diskPath);
    bindText(machine.get(), 11, meta.seedPath);
    sqlite3_bind_int(machine.get(), 12, meta.sshPort);
    bindText(machine.get(), 13, meta.sshUser);
    bindText(machine.get(), 14, meta.sshKeyPath);
    bindText(machine.get(), 15, meta.state);
    sqlite3_bind_int(machine.get(), 16, meta.isolated ? 1 : 0);
    sqlite3_bind_int(machine.get(), 17, meta.isolateNetwork ? 1 : 0);
    sqlite3_bind_int(machine.get(), 18, meta.forwardSshAgent ? 1 : 0);
    bindText(machine.get(), 19, meta.lastBootAt);
    bindText(machine.get(), 20, meta.createdAt);
    bindText(machine.get(), 21, meta.updatedAt);
    bindText(machine.get(), 22, meta.error);
    sqlite3_bind_int(machine.get(), 23, meta.windowsSsh ? 1 : 0);
    bindText(machine.get(), 24, meta.windowsEdition);
    sqlite3_bind_int(machine.get(), 25, meta.installPending ? 1 : 0);
    bindText(machine.get(), 26, meta.installerIso);
    bindText(machine.get(), 27, meta.virtioWinIso);
    bindText(machine.get(), 28, meta.errorReason);
    sqlite3_bind_int(machine.get(), 29, meta.guestReady ? 1 : 0);
    bindText(machine.get(), 30,
             QString::fromUtf8(QJsonDocument(meta.guestOs).toJson(QJsonDocument::Compact)));
    QJsonArray channelArray;
    for (const GuestChannelInfo &channel : meta.guestChannels) {
        channelArray.append(channel.toJson());
    }
    bindText(machine.get(), 31,
             QString::fromUtf8(QJsonDocument(channelArray).toJson(QJsonDocument::Compact)));

    if (sqlite3_step(machine.get()) != SQLITE_DONE) {
        return abort(QStringLiteral("Falha ao gravar a machine."));
    }
    machine.reset();

    bool deleteOk = false;
    Statement deletePorts = prepare(
        m_db, "DELETE FROM machine_ports WHERE machine_id = ?;", &deleteOk);
    if (!deleteOk) {
        return abort(QStringLiteral("Falha ao preparar a limpeza das portas."));
    }
    bindText(deletePorts.get(), 1, meta.id);
    if (sqlite3_step(deletePorts.get()) != SQLITE_DONE) {
        return abort(QStringLiteral("Falha ao limpar as portas da machine."));
    }
    deletePorts.reset();

    Statement deleteMounts = prepare(
        m_db, "DELETE FROM machine_mounts WHERE machine_id = ?;", &deleteOk);
    if (!deleteOk) {
        return abort(QStringLiteral("Falha ao preparar a limpeza dos mounts."));
    }
    bindText(deleteMounts.get(), 1, meta.id);
    if (sqlite3_step(deleteMounts.get()) != SQLITE_DONE) {
        return abort(QStringLiteral("Falha ao limpar os mounts da machine."));
    }
    deleteMounts.reset();

    Statement deleteSerialPorts =
        prepare(m_db, "DELETE FROM machine_serial_ports WHERE machine_id = ?;", &deleteOk);
    if (!deleteOk) {
        return abort(QStringLiteral("Falha ao preparar a limpeza das portas seriais."));
    }
    bindText(deleteSerialPorts.get(), 1, meta.id);
    if (sqlite3_step(deleteSerialPorts.get()) != SQLITE_DONE) {
        return abort(QStringLiteral("Falha ao limpar as portas seriais da machine."));
    }
    deleteSerialPorts.reset();

    Statement deleteUsbDevices =
        prepare(m_db, "DELETE FROM machine_usb_devices WHERE machine_id = ?;", &deleteOk);
    if (!deleteOk) {
        return abort(QStringLiteral("Falha ao preparar a limpeza dos dispositivos USB."));
    }
    bindText(deleteUsbDevices.get(), 1, meta.id);
    if (sqlite3_step(deleteUsbDevices.get()) != SQLITE_DONE) {
        return abort(QStringLiteral("Falha ao limpar os dispositivos USB da machine."));
    }
    deleteUsbDevices.reset();

    const char *portSql = R"SQL(
INSERT INTO machine_ports (id, machine_id, host_port, guest_port, protocol, expose_lan, label)
VALUES (?, ?, ?, ?, ?, ?, ?)
)SQL";
    for (const MachinePort &port : meta.ports) {
        bool portOk = false;
        Statement statement = prepare(m_db, portSql, &portOk);
        if (!portOk) {
            return abort(QStringLiteral("Falha ao preparar a porta da machine."));
        }
        bindText(statement.get(), 1, port.id);
        bindText(statement.get(), 2, meta.id);
        sqlite3_bind_int(statement.get(), 3, port.hostPort);
        sqlite3_bind_int(statement.get(), 4, port.guestPort);
        bindText(statement.get(), 5, port.protocol);
        sqlite3_bind_int(statement.get(), 6, port.exposeLan ? 1 : 0);
        bindText(statement.get(), 7, port.label);
        if (sqlite3_step(statement.get()) != SQLITE_DONE) {
            return abort(QStringLiteral("Falha ao gravar a porta da machine."));
        }
    }

    const char *mountSql = R"SQL(
INSERT INTO machine_mounts (id, machine_id, source, target, read_only, tag)
VALUES (?, ?, ?, ?, ?, ?)
)SQL";
    for (const MachineMount &mount : meta.mounts) {
        bool mountOk = false;
        Statement statement = prepare(m_db, mountSql, &mountOk);
        if (!mountOk) {
            return abort(QStringLiteral("Falha ao preparar o mount da machine."));
        }
        bindText(statement.get(), 1, mount.id);
        bindText(statement.get(), 2, meta.id);
        bindText(statement.get(), 3, mount.source);
        bindText(statement.get(), 4, mount.target);
        sqlite3_bind_int(statement.get(), 5, mount.readOnly ? 1 : 0);
        bindText(statement.get(), 6, mount.tag);
        if (sqlite3_step(statement.get()) != SQLITE_DONE) {
            return abort(QStringLiteral("Falha ao gravar o mount da machine."));
        }
    }

    const char *serialSql = R"SQL(
INSERT INTO machine_serial_ports (machine_id, path) VALUES (?, ?)
)SQL";
    for (const QString &path : meta.serialPorts) {
        bool serialOk = false;
        Statement statement = prepare(m_db, serialSql, &serialOk);
        if (!serialOk) {
            return abort(QStringLiteral("Falha ao preparar a porta serial da machine."));
        }
        bindText(statement.get(), 1, meta.id);
        bindText(statement.get(), 2, path);
        if (sqlite3_step(statement.get()) != SQLITE_DONE) {
            return abort(QStringLiteral("Falha ao gravar a porta serial da machine."));
        }
    }

    const char *usbSql = R"SQL(
INSERT INTO machine_usb_devices (machine_id, bus, address, vendor_id, product_id, description)
VALUES (?, ?, ?, ?, ?, ?)
)SQL";
    for (const MachineUsbDevice &device : meta.usbDevices) {
        bool usbOk = false;
        Statement statement = prepare(m_db, usbSql, &usbOk);
        if (!usbOk) {
            return abort(QStringLiteral("Falha ao preparar o dispositivo USB da machine."));
        }
        bindText(statement.get(), 1, meta.id);
        sqlite3_bind_int(statement.get(), 2, device.bus);
        sqlite3_bind_int(statement.get(), 3, device.address);
        bindText(statement.get(), 4, device.vendorId);
        bindText(statement.get(), 5, device.productId);
        bindText(statement.get(), 6, device.description);
        if (sqlite3_step(statement.get()) != SQLITE_DONE) {
            return abort(QStringLiteral("Falha ao gravar o dispositivo USB da machine."));
        }
    }

    bool settingsOk = false;
    Statement settings = prepare(
        m_db, "INSERT OR REPLACE INTO machine_settings (machine_id, audio_enabled) VALUES (?, ?);",
        &settingsOk);
    if (!settingsOk) {
        return abort(QStringLiteral("Falha ao preparar as configurações da machine."));
    }
    bindText(settings.get(), 1, meta.id);
    sqlite3_bind_int(settings.get(), 2, meta.audioEnabled ? 1 : 0);
    if (sqlite3_step(settings.get()) != SQLITE_DONE) {
        return abort(QStringLiteral("Falha ao gravar as configurações da machine."));
    }

    if (!execSql(m_db, "COMMIT;", &sqlError)) {
        return abort(QStringLiteral("Falha ao confirmar o upsert da machine."));
    }
    restrictDatabaseFiles(m_path);
    return true;
}
bool MachineStore::updateState(const QString &id, const QString &state,
                               const QString &errorText, Error *error)
{
    if (error) {
        *error = {};
    }

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!open(error)) {
        return false;
    }

    const char *sql = R"SQL(
UPDATE machines SET state = ?, error = ?, updated_at = ? WHERE id = ?
)SQL";
    bool ok = false;
    Statement statement = prepare(m_db, sql, &ok);
    if (!ok) {
        return fail(QStringLiteral("Falha ao preparar a atualização de estado."), error);
    }

    bindText(statement.get(), 1, state);
    bindText(statement.get(), 2, errorText);
    bindText(statement.get(), 3, nowIso());
    bindText(statement.get(), 4, id);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        return fail(QStringLiteral("Falha ao atualizar o estado da machine."), error);
    }
    if (sqlite3_changes(m_db) == 0) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Machine não encontrada."), id);
        }
        return false;
    }
    restrictDatabaseFiles(m_path);
    return true;
}

bool MachineStore::remove(const QString &id, Error *error)
{
    if (error) {
        *error = {};
    }

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!open(error)) {
        return false;
    }

    bool ok = false;
    Statement statement = prepare(m_db, "DELETE FROM machines WHERE id = ?", &ok);
    if (!ok) {
        return fail(QStringLiteral("Falha ao preparar a remoção da machine."), error);
    }
    bindText(statement.get(), 1, id);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        return fail(QStringLiteral("Falha ao remover a machine."), error);
    }
    if (sqlite3_changes(m_db) == 0) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Machine não encontrada."), id);
        }
        return false;
    }
    restrictDatabaseFiles(m_path);
    return true;
}

bool MachineStore::loadRelations(MachineMeta &meta, Error *error) const
{
    bool ok = false;
    Statement ports = prepare(m_db,
                              R"SQL(
SELECT id, host_port, guest_port, protocol, expose_lan, label
FROM machine_ports WHERE machine_id = ? ORDER BY host_port, id
)SQL",
                              &ok);
    if (!ok) {
        fail(QStringLiteral("Falha ao consultar as portas da machine."), error);
        return false;
    }
    bindText(ports.get(), 1, meta.id);
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(ports.get())) == SQLITE_ROW) {
        MachinePort port;
        port.id = columnText(ports.get(), 0);
        port.hostPort = sqlite3_column_int(ports.get(), 1);
        port.guestPort = sqlite3_column_int(ports.get(), 2);
        port.protocol = columnText(ports.get(), 3);
        port.exposeLan = sqlite3_column_int(ports.get(), 4) != 0;
        port.label = columnText(ports.get(), 5);
        meta.ports.append(port);
    }
    if (rc != SQLITE_DONE) {
        fail(QStringLiteral("Falha ao ler as portas da machine."), error);
        return false;
    }

    Statement mounts = prepare(m_db,
                               R"SQL(
SELECT id, source, target, read_only, tag
FROM machine_mounts WHERE machine_id = ? ORDER BY target, id
)SQL",
                               &ok);
    if (!ok) {
        fail(QStringLiteral("Falha ao consultar os mounts da machine."), error);
        return false;
    }
    bindText(mounts.get(), 1, meta.id);
    while ((rc = sqlite3_step(mounts.get())) == SQLITE_ROW) {
        MachineMount mount;
        mount.id = columnText(mounts.get(), 0);
        mount.source = columnText(mounts.get(), 1);
        mount.target = columnText(mounts.get(), 2);
        mount.readOnly = sqlite3_column_int(mounts.get(), 3) != 0;
        mount.tag = columnText(mounts.get(), 4);
        meta.mounts.append(mount);
    }
    if (rc != SQLITE_DONE) {
        fail(QStringLiteral("Falha ao ler os mounts da machine."), error);
        return false;
    }

    Statement serialPorts = prepare(m_db,
                                    R"SQL(
SELECT path FROM machine_serial_ports WHERE machine_id = ? ORDER BY path
)SQL",
                                    &ok);
    if (!ok) {
        fail(QStringLiteral("Falha ao consultar as portas seriais da machine."), error);
        return false;
    }
    bindText(serialPorts.get(), 1, meta.id);
    while ((rc = sqlite3_step(serialPorts.get())) == SQLITE_ROW) {
        meta.serialPorts.append(columnText(serialPorts.get(), 0));
    }
    if (rc != SQLITE_DONE) {
        fail(QStringLiteral("Falha ao ler as portas seriais da machine."), error);
        return false;
    }

    Statement usbDevices = prepare(m_db,
                                   R"SQL(
SELECT bus, address, vendor_id, product_id, description
FROM machine_usb_devices WHERE machine_id = ? ORDER BY bus, address
)SQL",
                                   &ok);
    if (!ok) {
        fail(QStringLiteral("Falha ao consultar os dispositivos USB da machine."), error);
        return false;
    }
    bindText(usbDevices.get(), 1, meta.id);
    while ((rc = sqlite3_step(usbDevices.get())) == SQLITE_ROW) {
        MachineUsbDevice device;
        device.bus = sqlite3_column_int(usbDevices.get(), 0);
        device.address = sqlite3_column_int(usbDevices.get(), 1);
        device.vendorId = columnText(usbDevices.get(), 2);
        device.productId = columnText(usbDevices.get(), 3);
        device.description = columnText(usbDevices.get(), 4);
        meta.usbDevices.append(device);
    }
    if (rc != SQLITE_DONE) {
        fail(QStringLiteral("Falha ao ler os dispositivos USB da machine."), error);
        return false;
    }

    Statement settings =
        prepare(m_db, "SELECT audio_enabled FROM machine_settings WHERE machine_id = ?;", &ok);
    if (!ok) {
        fail(QStringLiteral("Falha ao consultar as configurações da machine."), error);
        return false;
    }
    bindText(settings.get(), 1, meta.id);
    if (sqlite3_step(settings.get()) == SQLITE_ROW) {
        meta.audioEnabled = sqlite3_column_int(settings.get(), 0) != 0;
    }
    return true;
}

MachineMeta MachineStore::get(const QString &id, Error *error) const
{
    if (error) {
        *error = {};
    }

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!const_cast<MachineStore *>(this)->open(error)) {
        return {};
    }

    const QString sql = QString::fromUtf8(kMachineColumns) + QStringLiteral(" WHERE id = ?");
    bool ok = false;
    Statement statement = prepare(m_db, sql.toUtf8().constData(), &ok);
    if (!ok) {
        fail(QStringLiteral("Falha ao consultar a machine."), error);
        return {};
    }
    bindText(statement.get(), 1, id);

    MachineMeta meta;
    const int rc = sqlite3_step(statement.get());
    if (rc == SQLITE_ROW) {
        meta = metaFromRow(statement.get());
    } else if (rc != SQLITE_DONE) {
        fail(QStringLiteral("Falha ao ler a machine."), error);
        return {};
    }
    statement.reset();

    if (meta.id.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Machine não encontrada."), id);
        }
        return {};
    }
    if (!loadRelations(meta, error)) {
        return {};
    }
    return meta;
}

MachineMeta MachineStore::getByName(const QString &name, Error *error) const
{
    if (error) {
        *error = {};
    }

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!const_cast<MachineStore *>(this)->open(error)) {
        return {};
    }

    const QString sql = QString::fromUtf8(kMachineColumns) + QStringLiteral(" WHERE name = ?");
    bool ok = false;
    Statement statement = prepare(m_db, sql.toUtf8().constData(), &ok);
    if (!ok) {
        fail(QStringLiteral("Falha ao consultar a machine pelo nome."), error);
        return {};
    }
    bindText(statement.get(), 1, name);

    MachineMeta meta;
    const int rc = sqlite3_step(statement.get());
    if (rc == SQLITE_ROW) {
        meta = metaFromRow(statement.get());
    } else if (rc != SQLITE_DONE) {
        fail(QStringLiteral("Falha ao ler a machine pelo nome."), error);
        return {};
    }
    statement.reset();

    if (meta.id.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                                 QStringLiteral("Machine não encontrada."), name);
        }
        return {};
    }
    if (!loadRelations(meta, error)) {
        return {};
    }
    return meta;
}

QVector<MachineMeta> MachineStore::list(Error *error) const
{
    if (error) {
        *error = {};
    }

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!const_cast<MachineStore *>(this)->open(error)) {
        return {};
    }

    const QString sql = QString::fromUtf8(kMachineColumns)
                        + QStringLiteral(" ORDER BY created_at, name");
    bool ok = false;
    Statement statement = prepare(m_db, sql.toUtf8().constData(), &ok);
    if (!ok) {
        fail(QStringLiteral("Falha ao listar machines."), error);
        return {};
    }

    QVector<MachineMeta> machines;
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(statement.get())) == SQLITE_ROW) {
        machines.append(metaFromRow(statement.get()));
    }
    if (rc != SQLITE_DONE) {
        fail(QStringLiteral("Falha ao ler a lista de machines."), error);
        return {};
    }
    statement.reset();

    for (MachineMeta &meta : machines) {
        if (!loadRelations(meta, error)) {
            return {};
        }
    }
    return machines;
}

} // namespace PodStack
