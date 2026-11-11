// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/CloudInit.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>

#include <archive.h>
#include <archive_entry.h>
#include <crypt.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>

namespace PodStack {
namespace {

constexpr int kMaxHostnameLength = 253;
constexpr int kMaxLabelLength = 63;
constexpr int kMaxUserLength = 32;
constexpr int kMaxMachineIdLength = 128;
constexpr int kSaltLength = 16;
constexpr char kSaltAlphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";

bool isAsciiAlphaNumeric(QChar character)
{
    const ushort value = character.unicode();
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9');
}

bool isValidHostname(const QString &hostname)
{
    if (hostname.isEmpty() || hostname.size() > kMaxHostnameLength) {
        return false;
    }
    const QStringList labels = hostname.split(QLatin1Char('.'));
    for (const QString &label : labels) {
        if (label.isEmpty() || label.size() > kMaxLabelLength ||
            label.startsWith(QLatin1Char('-')) || label.endsWith(QLatin1Char('-'))) {
            return false;
        }
        for (const QChar character : label) {
            if (!isAsciiAlphaNumeric(character) && character != QLatin1Char('-')) {
                return false;
            }
        }
    }
    return true;
}

bool isValidUser(const QString &user)
{
    if (user.isEmpty() || user.size() > kMaxUserLength) {
        return false;
    }
    const QChar first = user.front();
    if (!(first == QLatin1Char('_') || (first.unicode() >= 'a' && first.unicode() <= 'z'))) {
        return false;
    }
    for (const QChar character : user) {
        const ushort value = character.unicode();
        const bool valid = (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
                           value == '_' || value == '-';
        if (!valid) {
            return false;
        }
    }
    return true;
}

bool isValidMachineId(const QString &machineId)
{
    if (machineId.isEmpty() || machineId.size() > kMaxMachineIdLength) {
        return false;
    }
    for (const QChar character : machineId) {
        if (!isAsciiAlphaNumeric(character) && character != QLatin1Char('.') &&
            character != QLatin1Char('_') && character != QLatin1Char(':') &&
            character != QLatin1Char('-')) {
            return false;
        }
    }
    return true;
}

bool hasControlCharacters(const QString &value)
{
    for (const QChar character : value) {
        if (character.unicode() < 0x20 || character.unicode() == 0x7f) {
            return true;
        }
    }
    return false;
}

bool isValidEnvironmentName(const QString &name)
{
    if (name.isEmpty()) {
        return false;
    }
    const QChar first = name.front();
    if (!(first == QLatin1Char('_') || (first.unicode() >= 'A' && first.unicode() <= 'Z') ||
          (first.unicode() >= 'a' && first.unicode() <= 'z'))) {
        return false;
    }
    for (const QChar character : name) {
        const ushort value = character.unicode();
        if (!((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '_')) {
            return false;
        }
    }
    return true;
}

QString yamlQuote(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

QString passwordHash(const QString &password)
{
    QString salt = QStringLiteral("$6$");
    QRandomGenerator *random = QRandomGenerator::system();
    const int alphabetSize = static_cast<int>(sizeof(kSaltAlphabet)) - 1;
    salt.reserve(salt.size() + kSaltLength);
    for (int index = 0; index < kSaltLength; ++index) {
        salt.append(QLatin1Char(kSaltAlphabet[random->bounded(alphabetSize)]));
    }

    crypt_data data{};
    const QByteArray passwordBytes = password.toUtf8();
    const QByteArray saltBytes = salt.toLatin1();
    const char *hash = crypt_r(passwordBytes.constData(), saltBytes.constData(), &data);
    return hash ? QString::fromLatin1(hash) : QString();
}

QString metaDataFor(const CloudInitOptions &options)
{
    return QStringLiteral("instance-id: %1\nlocal-hostname: %2\n")
        .arg(yamlQuote(options.machineId), yamlQuote(options.hostname));
}

QString guestSetupScript()
{
    return QStringLiteral(
        "#!/bin/sh\n"
        "set -u\n"
        ". /etc/os-release 2>/dev/null || true\n"
        "user=\"${1:-root}\"\n"
        "ssh_service=ssh\n"
        "need_reboot=0\n"
        "if command -v apt-get >/dev/null 2>&1; then\n"
        "  export DEBIAN_FRONTEND=noninteractive\n"
        "  apt-get install -y xauth x11-apps pulseaudio alsa-utils || true\n"
        "  if [ \"${ID:-}\" = \"debian\" ]; then\n"
        "    arch=$(dpkg --print-architecture 2>/dev/null || echo amd64)\n"
        "    apt-get install -y \"linux-image-$arch\" || true\n"
        "    cloud_kernels=$(dpkg-query -W -f='${Package}\\n' 'linux-image-*cloud*' "
        "2>/dev/null || true)\n"
        "    if [ -n \"$cloud_kernels\" ]; then apt-get purge -y $cloud_kernels || true; fi\n"
        "    update-grub || true\n"
        "    need_reboot=1\n"
        "  else\n"
        "    apt-get install -y \"linux-modules-extra-$(uname -r)\" || true\n"
        "    modprobe snd-hda-intel 2>/dev/null || true\n"
        "  fi\n"
        "  if command -v pulseaudio >/dev/null 2>&1; then\n"
        "    uid=$(id -u \"$user\" 2>/dev/null || echo 0)\n"
        "    runuser -u \"$user\" -- env XDG_RUNTIME_DIR=\"/run/user/$uid\" pulseaudio "
        "--start --exit-idle-time=-1 2>/dev/null || true\n"
        "  fi\n"
        "elif command -v dnf >/dev/null 2>&1; then\n"
        "  dnf install -y xauth xorg-x11-apps \"kernel-modules-extra-$(uname -r)\" "
        "pipewire pipewire-pulseaudio wireplumber alsa-utils || true\n"
        "  modprobe snd-hda-intel 2>/dev/null || true\n"
        "  uid=$(id -u \"$user\" 2>/dev/null || echo 0)\n"
        "  loginctl enable-linger \"$user\" 2>/dev/null || true\n"
        "  runuser -u \"$user\" -- env XDG_RUNTIME_DIR=\"/run/user/$uid\" systemctl "
        "--user enable --now pipewire.socket pipewire-pulse.socket wireplumber "
        "2>/dev/null || true\n"
        "  ssh_service=sshd\n"
        "fi\n"
        "mkdir -p /etc/ssh/sshd_config.d\n"
        "printf 'X11Forwarding yes\\n' > /etc/ssh/sshd_config.d/10-podstack-x11.conf\n"
        "if command -v sshd >/dev/null 2>&1 && ! sshd -T 2>/dev/null | "
        "grep -qi '^x11forwarding yes'; then\n"
        "  sed -i 's/^#\\?X11Forwarding.*/X11Forwarding yes/' /etc/ssh/sshd_config || true\n"
        "fi\n"
        "systemctl restart \"$ssh_service\" 2>/dev/null || "
        "systemctl restart sshd 2>/dev/null || true\n"
        "if [ \"$need_reboot\" = \"1\" ]; then reboot; fi\n");
}

QString userDataFor(const CloudInitOptions &options, const QString &hash)
{
    if (!options.userData.isEmpty()) {
        return options.userData;
    }

    const QString user = options.user.isEmpty() ? QStringLiteral("podstack") : options.user;
    QString data = QStringLiteral("#cloud-config\n"
                                  "users:\n"
                                  "  - name: %1\n"
                                  "    shell: /bin/bash\n"
                                  "    sudo: 'ALL=(ALL) NOPASSWD:ALL'\n")
                       .arg(yamlQuote(user));
    if (hash.isEmpty()) {
        data += QStringLiteral("    lock_passwd: true\n");
    } else {
        data += QStringLiteral("    lock_passwd: false\n    passwd: %1\n").arg(yamlQuote(hash));
    }
    if (!options.sshAuthorizedKeys.isEmpty()) {
        data += QStringLiteral("    ssh_authorized_keys:\n");
        for (const QString &key : options.sshAuthorizedKeys) {
            data += QStringLiteral("      - %1\n").arg(yamlQuote(key));
        }
    }
    data += QStringLiteral("ssh_pwauth: false\n");
    data += QStringLiteral("package_update: true\n"
                           "packages:\n"
                           "  - xauth\n"
                           "write_files:\n");
    if (!options.environment.isEmpty()) {
        data += QStringLiteral("  - path: /etc/environment\n"
                               "    append: true\n"
                               "    permissions: '0644'\n"
                               "    content: |\n");
        for (auto it = options.environment.constBegin(); it != options.environment.constEnd();
             ++it) {
            data += QStringLiteral("      %1=%2\n").arg(it.key(), it.value());
        }
    }
    data += QStringLiteral("  - path: /etc/ssh/sshd_config.d/10-podstack-x11.conf\n"
                           "    permissions: '0644'\n"
                           "    content: |\n"
                           "      X11Forwarding yes\n"
                           "  - path: /usr/local/sbin/podstack-guest-setup\n"
                           "    permissions: '0755'\n"
                           "    content: |\n");
    const QStringList script = guestSetupScript().split(QLatin1Char('\n'));
    for (const QString &line : script) {
        data += QStringLiteral("      ") + line + QLatin1Char('\n');
    }
    data += QStringLiteral("runcmd:\n"
                           "  - /usr/local/sbin/podstack-guest-setup %1\n")
                .arg(yamlQuote(user));
    return data;
}

QString networkConfigFor(const CloudInitOptions &options)
{
    if (!options.networkConfig.isEmpty()) {
        return options.networkConfig;
    }
    return QStringLiteral("version: 2\n"
                          "ethernets:\n"
                          "  eth0:\n"
                          "    match:\n"
                          "      name: 'en*'\n"
                          "    dhcp4: true\n");
}

QString archiveErrorString(archive *handle)
{
    const char *message = archive_error_string(handle);
    return message ? QString::fromLocal8Bit(message) : QString();
}

Error ioError(const QString &message, const QString &detail = {})
{
    return Error::make(QStringLiteral("PODSTACK_IO_ERROR"), message, detail);
}

bool ensureParentDirectory(const QString &parent, Error *error)
{
    const QFileInfo info(parent);
    if (info.exists()) {
        if (!info.isDir()) {
            if (error) {
                *error = ioError(QStringLiteral("O caminho do seed não é um diretório."), parent);
            }
            return false;
        }
        return true;
    }

    if (!QDir().mkpath(parent)) {
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível criar o diretório do seed."), parent);
        }
        return false;
    }
    QFile::setPermissions(parent,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return true;
}

bool writeIso(const QString &path, const QByteArray &metaData, const QByteArray &userData,
              const QByteArray &networkConfig, Error *error)
{
    using ArchivePtr = std::unique_ptr<archive, decltype(&archive_write_free)>;
    ArchivePtr handle(archive_write_new(), &archive_write_free);
    if (!handle) {
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível inicializar o escritor de ISO."));
        }
        return false;
    }

    if (archive_write_set_format_iso9660(handle.get()) != ARCHIVE_OK) {
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível configurar o formato ISO9660."),
                             archiveErrorString(handle.get()));
        }
        return false;
    }

    // rockridge=strict preserva o modo 0600 das entradas; o alias `rockridge=1`
    // (useful) limpa os bits de escrita e publicaria os arquivos como 0444.
    const QByteArray options =
        QByteArrayLiteral("iso9660:volume-id=cidata,iso9660:rockridge=strict,"
                          "iso9660:joliet=1");
    if (archive_write_set_options(handle.get(), options.constData()) != ARCHIVE_OK) {
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível configurar a ISO de seed."),
                             archiveErrorString(handle.get()));
        }
        return false;
    }

    const QByteArray nativePath = QFile::encodeName(path);
    if (archive_write_open_filename(handle.get(), nativePath.constData()) != ARCHIVE_OK) {
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível abrir o seed para escrita."),
                             archiveErrorString(handle.get()));
        }
        return false;
    }

    struct SeedFile {
        const char *name;
        const QByteArray *data;
    };
    const SeedFile files[] = {
        {"meta-data", &metaData},
        {"user-data", &userData},
        {"network-config", &networkConfig},
    };

    const qint64 mtime = QDateTime::currentSecsSinceEpoch();
    for (const SeedFile &file : files) {
        archive_entry *entry = archive_entry_new();
        if (!entry) {
            if (error) {
                *error = ioError(QStringLiteral("Não foi possível criar uma entrada da ISO."));
            }
            return false;
        }

        archive_entry_set_pathname(entry, file.name);
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0600);
        archive_entry_set_size(entry, file.data->size());
        archive_entry_set_mtime(entry, mtime, 0);

        int result = archive_write_header(handle.get(), entry);
        if (result == ARCHIVE_OK && !file.data->isEmpty()) {
            const la_ssize_t written = archive_write_data(handle.get(), file.data->constData(),
                                                          static_cast<size_t>(file.data->size()));
            if (written != file.data->size()) {
                result = ARCHIVE_FATAL;
            }
        }
        archive_entry_free(entry);

        if (result != ARCHIVE_OK) {
            if (error) {
                *error = ioError(QStringLiteral("Não foi possível gravar o seed cloud-init."),
                                 archiveErrorString(handle.get()));
            }
            return false;
        }
    }

    if (archive_write_close(handle.get()) != ARCHIVE_OK) {
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível finalizar a ISO do seed."),
                             archiveErrorString(handle.get()));
        }
        return false;
    }
    return true;
}

} // namespace

bool CloudInit::writeSeedIso(const QString &path, const CloudInitOptions &options, Error *error)
{
    if (error) {
        *error = {};
    }

    if (path.isEmpty()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("O caminho do seed é obrigatório."));
        }
        return false;
    }
    if (!isValidMachineId(options.machineId)) {
        if (error) {
            *error =
                Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                            QStringLiteral("instance-id inválido para o seed."), options.machineId);
        }
        return false;
    }
    if (!isValidHostname(options.hostname)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Hostname inválido (esperado RFC1123)."),
                                 options.hostname);
        }
        return false;
    }

    const QString user = options.user.isEmpty() ? QStringLiteral("podstack") : options.user;
    if (!isValidUser(user)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                 QStringLiteral("Usuário inválido para o seed."), user);
        }
        return false;
    }
    for (const QString &key : options.sshAuthorizedKeys) {
        if (key.isEmpty() || hasControlCharacters(key)) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("Chave SSH inválida para o seed."));
            }
            return false;
        }
    }
    for (auto it = options.environment.constBegin(); it != options.environment.constEnd(); ++it) {
        if (!isValidEnvironmentName(it.key()) || hasControlCharacters(it.value())) {
            if (error) {
                *error = Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"),
                                     QStringLiteral("Variável de ambiente inválida para o seed."),
                                     it.key());
            }
            return false;
        }
    }

    QString hash;
    if (!options.password.isEmpty()) {
        hash = passwordHash(options.password);
        if (hash.isEmpty()) {
            if (error) {
                *error = ioError(QStringLiteral("Não foi possível gerar o hash da senha."));
            }
            return false;
        }
    }

    CloudInitOptions normalized = options;
    normalized.user = user;
    const QByteArray metaData = metaDataFor(normalized).toUtf8();
    const QByteArray userData = userDataFor(normalized, hash).toUtf8();
    const QByteArray networkConfig = networkConfigFor(normalized).toUtf8();

    const QFileInfo target(path);
    if (target.isDir()) {
        if (error) {
            *error = ioError(QStringLiteral("O caminho do seed aponta para um diretório."), path);
        }
        return false;
    }
    if (!ensureParentDirectory(target.absolutePath(), error)) {
        return false;
    }

    const QString temporaryPath = path + QStringLiteral(".tmp");
    const QFileInfo temporary(temporaryPath);
    if (temporary.isDir()) {
        if (error) {
            *error = ioError(QStringLiteral("O arquivo temporário do seed é um diretório."),
                             temporaryPath);
        }
        return false;
    }
    QFile::remove(temporaryPath);

    if (!writeIso(temporaryPath, metaData, userData, networkConfig, error)) {
        QFile::remove(temporaryPath);
        return false;
    }

    if (!QFile::setPermissions(temporaryPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        QFile::remove(temporaryPath);
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível restringir o seed a 0600."),
                             temporaryPath);
        }
        return false;
    }

    const QByteArray temporaryBytes = QFile::encodeName(temporaryPath);
    const QByteArray targetBytes = QFile::encodeName(path);
    if (std::rename(temporaryBytes.constData(), targetBytes.constData()) != 0) {
        const QString detail = QString::fromLocal8Bit(std::strerror(errno));
        QFile::remove(temporaryPath);
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível publicar o seed."), detail);
        }
        return false;
    }
    return true;
}

} // namespace PodStack
