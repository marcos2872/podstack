// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/WindowsProvisioner.h"

#include "core/Logging.h"
#include "core/Password.h"
#include "machines/WindowsProvisioner.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QTimeZone>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <archive.h>
#include <archive_entry.h>

#include <memory>

namespace PodStack {
namespace {

constexpr qint64 kMaxAnswerFileBytes = 1024LL * 1024LL;

Error validationError(const QString &message, const QString &detail = {},
                      const QString &remediation = {})
{
    return Error::make(QStringLiteral("PODSTACK_VALIDATION"), message, detail, remediation);
}

Error ioError(const QString &message, const QString &detail = {})
{
    return Error::make(QStringLiteral("PODSTACK_IO_ERROR"), message, detail);
}

QString archiveErrorString(struct archive *handle)
{
    const char *message = archive_error_string(handle);
    return message ? QString::fromUtf8(message) : QString();
}

bool validateIso(const QString &path, const QString &label, Error *error)
{
    if (path.trimmed().isEmpty()) {
        if (error) {
            *error = validationError(QStringLiteral("Informe o %1.").arg(label));
        }
        return false;
    }
    const QFileInfo info(path);
    if (!info.isAbsolute()) {
        if (error) {
            *error = validationError(
                QStringLiteral("O caminho do %1 deve ser absoluto.").arg(label), path);
        }
        return false;
    }
    if (!info.isFile() || !info.isReadable()) {
        if (error) {
            *error = validationError(
                QStringLiteral("O %1 não existe ou não pode ser lido.").arg(label), path,
                QStringLiteral("Selecione um arquivo .iso válido e acessível."));
        }
        return false;
    }
    if (info.size() <= 0) {
        if (error) {
            *error = validationError(QStringLiteral("O %1 está vazio.").arg(label), path);
        }
        return false;
    }
    if (info.suffix().compare(QStringLiteral("iso"), Qt::CaseInsensitive) != 0) {
        if (error) {
            *error = validationError(QStringLiteral("O %1 precisa ser um arquivo .iso.").arg(label),
                                     path);
        }
        return false;
    }
    return true;
}

bool writeFile(const QString &path, const QByteArray &content, Error *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível gravar o arquivo."), path);
        }
        return false;
    }
    if (file.write(content) != content.size()) {
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível gravar o arquivo completo."), path);
        }
        return false;
    }
    file.close();
    if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível restringir o arquivo a 0600."), path);
        }
        return false;
    }
    return true;
}

bool writeIso(const QString &path, const QByteArray &xml, Error *error)
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
    const QByteArray options =
        QByteArrayLiteral("iso9660:volume-id=PODSTACKUNATTEND,iso9660:rockridge=strict,"
                          "iso9660:joliet=1");
    if (archive_write_set_options(handle.get(), options.constData()) != ARCHIVE_OK) {
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível configurar a ISO do unattend."),
                             archiveErrorString(handle.get()));
        }
        return false;
    }

    const QByteArray nativePath = QFile::encodeName(path);
    if (archive_write_open_filename(handle.get(), nativePath.constData()) != ARCHIVE_OK) {
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível abrir a ISO do unattend."),
                             archiveErrorString(handle.get()));
        }
        return false;
    }

    archive_entry *entry = archive_entry_new();
    if (!entry) {
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível criar a entrada da ISO."));
        }
        return false;
    }
    archive_entry_set_pathname(entry, "autounattend.xml");
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0444);
    archive_entry_set_size(entry, xml.size());
    archive_entry_set_mtime(entry, QDateTime::currentSecsSinceEpoch(), 0);

    int result = archive_write_header(handle.get(), entry);
    if (result == ARCHIVE_OK && !xml.isEmpty()) {
        const la_ssize_t written =
            archive_write_data(handle.get(), xml.constData(), static_cast<size_t>(xml.size()));
        if (written != xml.size()) {
            result = ARCHIVE_FATAL;
        }
    }
    archive_entry_free(entry);
    if (result != ARCHIVE_OK) {
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível gravar o unattend na ISO."),
                             archiveErrorString(handle.get()));
        }
        return false;
    }
    if (archive_write_close(handle.get()) != ARCHIVE_OK) {
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível finalizar a ISO do unattend."),
                             archiveErrorString(handle.get()));
        }
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

QString inputLocale(const QString &locale)
{
    if (locale == QStringLiteral("pt-BR")) {
        return QStringLiteral("0416:00010416");
    }
    if (locale == QStringLiteral("en-US")) {
        return QStringLiteral("0409:00000409");
    }
    if (locale == QStringLiteral("es-ES")) {
        return QStringLiteral("0c0a:00000c0a");
    }
    return QStringLiteral("0409:00000409");
}

QString genericProductKey(const QString &edition)
{
    const QString value = edition.toLower();
    if (value.contains(QStringLiteral("education"))) {
        return QStringLiteral("YNMGQ-8RYV3-4PGQ3-C8XTP-7CFBY");
    }
    if (value.contains(QStringLiteral("enterprise"))) {
        return QStringLiteral("NPPR9-FWDCX-D2C8J-H872K-2YT43");
    }
    if (value.contains(QStringLiteral("home"))) {
        return QStringLiteral("YTMG3-N6DKC-DKB77-7M9GH-8HVX7");
    }
    if (value.contains(QStringLiteral("pro"))) {
        return QStringLiteral("VK7JG-NPHTM-C97JM-9MPGT-3V66T");
    }
    return {};
}

QString xmlEscapeSingleQuotes(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
    return escaped;
}

} // namespace

QString WindowsProvisioner::defaultEdition()
{
    return QStringLiteral("Windows 11 Pro");
}

QString WindowsProvisioner::generatePassword()
{
    return PodStack::generatePassword();
}

QString WindowsProvisioner::windowsComputerName(const QString &machineName)
{
    QString name = machineName;
    name.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9-]")));
    if (name.size() > 15) {
        name.truncate(15);
    }
    while (name.endsWith(QLatin1Char('-'))) {
        name.chop(1);
    }
    if (name.isEmpty()) {
        name = QStringLiteral("PODSTACK");
    }
    return name;
}

QString WindowsProvisioner::windowsTimeZone(const QString &ianaId)
{
    const QString id = ianaId.trimmed();
    if (id == QStringLiteral("America/Sao_Paulo")) {
        return QStringLiteral("E. South America Standard Time");
    }
    if (id == QStringLiteral("America/New_York")) {
        return QStringLiteral("Eastern Standard Time");
    }
    if (id == QStringLiteral("America/Chicago")) {
        return QStringLiteral("Central Standard Time");
    }
    if (id == QStringLiteral("America/Denver")) {
        return QStringLiteral("Mountain Standard Time");
    }
    if (id == QStringLiteral("America/Los_Angeles")) {
        return QStringLiteral("Pacific Standard Time");
    }
    if (id == QStringLiteral("Europe/London")) {
        return QStringLiteral("GMT Standard Time");
    }
    if (id == QStringLiteral("Europe/Lisbon")) {
        return QStringLiteral("GMT Standard Time");
    }
    if (id == QStringLiteral("Europe/Berlin")) {
        return QStringLiteral("W. Europe Standard Time");
    }
    if (id == QStringLiteral("Europe/Paris")) {
        return QStringLiteral("Romance Standard Time");
    }
    if (id == QStringLiteral("Asia/Tokyo")) {
        return QStringLiteral("Tokyo Standard Time");
    }
    return QStringLiteral("UTC");
}

QString WindowsProvisioner::localeName()
{
    QString name = QLocale::system().name();
    if (name.isEmpty()) {
        return QStringLiteral("en-US");
    }
    name.replace(QLatin1Char('_'), QLatin1Char('-'));
    return name;
}

bool WindowsProvisioner::validateInstallerIso(const QString &path, Error *error)
{
    if (error) {
        *error = {};
    }
    return validateIso(path, QStringLiteral("ISO do Windows"), error);
}

bool WindowsProvisioner::validateVirtioWinIso(const QString &path, Error *error)
{
    if (error) {
        *error = {};
    }
    return validateIso(path, QStringLiteral("ISO do virtio-win"), error);
}

bool WindowsProvisioner::validateAnswerFile(const QString &path, Error *error)
{
    if (error) {
        *error = {};
    }
    const QFileInfo info(path);
    if (!info.isAbsolute()) {
        if (error) {
            *error = validationError(QStringLiteral("O caminho do answer file deve ser absoluto."),
                                     path);
        }
        return false;
    }
    if (!info.isFile() || !info.isReadable()) {
        if (error) {
            *error = validationError(
                QStringLiteral("O answer file não existe ou não pode ser lido."), path);
        }
        return false;
    }
    if (info.size() > kMaxAnswerFileBytes) {
        if (error) {
            *error = validationError(
                QStringLiteral("O answer file é grande demais (máximo 1 MiB)."), path);
        }
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = ioError(QStringLiteral("Não foi possível abrir o answer file."), path);
        }
        return false;
    }
    QXmlStreamReader reader(&file);
    while (!reader.atEnd()) {
        reader.readNext();
    }
    if (reader.hasError()) {
        if (error) {
            *error = validationError(QStringLiteral("O answer file não é um XML válido."),
                                     reader.errorString());
        }
        return false;
    }
    return true;
}

QString WindowsProvisioner::buildXml(const WindowsProvisionOptions &options)
{
    QString xml;
    QXmlStreamWriter writer(&xml);
    writer.setAutoFormatting(true);
    writer.writeStartDocument(QStringLiteral("1.0"));
    writer.writeStartElement(QStringLiteral("unattend"));
    writer.writeAttribute(QStringLiteral("xmlns"),
                          QStringLiteral("urn:schemas-microsoft-com:unattend"));
    writer.writeAttribute(QStringLiteral("xmlns:wcm"),
                          QStringLiteral("http://schemas.microsoft.com/WMIConfig/2002/State"));
    writer.writeAttribute(QStringLiteral("xmlns:xsi"),
                          QStringLiteral("http://www.w3.org/2001/XMLSchema-instance"));

    const QString locale = options.locale.isEmpty() ? localeName() : options.locale;
    const QString timeZone = options.timeZone.isEmpty()
                                 ? windowsTimeZone(QTimeZone::systemTimeZoneId())
                                 : options.timeZone;
    const QString input = inputLocale(locale);

    const auto openSettings = [&writer](const char *pass) {
        writer.writeStartElement(QStringLiteral("settings"));
        writer.writeAttribute(QStringLiteral("pass"), QString::fromLatin1(pass));
    };
    const auto openComponent = [&writer](const char *name) {
        writer.writeStartElement(QStringLiteral("component"));
        writer.writeAttribute(QStringLiteral("name"), QString::fromLatin1(name));
        writer.writeAttribute(QStringLiteral("processorArchitecture"), QStringLiteral("amd64"));
        writer.writeAttribute(QStringLiteral("publicKeyToken"), QStringLiteral("31bf3856ad364e35"));
        writer.writeAttribute(QStringLiteral("language"), QStringLiteral("neutral"));
        writer.writeAttribute(QStringLiteral("versionScope"), QStringLiteral("nonSxS"));
    };
    const auto closeComponent = [&writer] {
        writer.writeEndElement();
        writer.writeEndElement();
    };

    // windowsPE: locale, disco, imagem e drivers virtio.
    openSettings("windowsPE");
    openComponent("Microsoft-Windows-International-Core-WinPE");
    writer.writeStartElement(QStringLiteral("SetupUILanguage"));
    writer.writeTextElement(QStringLiteral("UILanguage"), locale);
    writer.writeEndElement();
    writer.writeTextElement(QStringLiteral("InputLocale"), input);
    writer.writeTextElement(QStringLiteral("SystemLocale"), locale);
    writer.writeTextElement(QStringLiteral("UILanguage"), locale);
    writer.writeTextElement(QStringLiteral("UserLocale"), locale);
    writer.writeEndElement(); // component (o settings fecha após o Setup)

    openComponent("Microsoft-Windows-Setup");
    writer.writeStartElement(QStringLiteral("DiskConfiguration"));
    writer.writeStartElement(QStringLiteral("Disk"));
    writer.writeAttribute(QStringLiteral("wcm:action"), QStringLiteral("add"));
    writer.writeTextElement(QStringLiteral("DiskID"), QStringLiteral("0"));
    writer.writeTextElement(QStringLiteral("WillWipeDisk"), QStringLiteral("true"));
    writer.writeStartElement(QStringLiteral("CreatePartitions"));
    const auto writePartition = [&writer](int order, const QString &type, const QString &size,
                                          bool extend) {
        writer.writeStartElement(QStringLiteral("CreatePartition"));
        writer.writeAttribute(QStringLiteral("wcm:action"), QStringLiteral("add"));
        writer.writeTextElement(QStringLiteral("Order"), QString::number(order));
        writer.writeTextElement(QStringLiteral("Type"), type);
        if (extend) {
            writer.writeTextElement(QStringLiteral("Extend"), QStringLiteral("true"));
        } else {
            writer.writeTextElement(QStringLiteral("Size"), size);
        }
        writer.writeEndElement();
    };
    writePartition(1, QStringLiteral("EFI"), QStringLiteral("100"), false);
    writePartition(2, QStringLiteral("MSR"), QStringLiteral("16"), false);
    writePartition(3, QStringLiteral("Primary"), QString(), true);
    writer.writeEndElement(); // CreatePartitions
    writer.writeStartElement(QStringLiteral("ModifyPartitions"));
    const auto writeModifyPartition = [&writer](int order, int partitionId, const QString &format,
                                                const QString &label, const QString &letter) {
        writer.writeStartElement(QStringLiteral("ModifyPartition"));
        writer.writeAttribute(QStringLiteral("wcm:action"), QStringLiteral("add"));
        writer.writeTextElement(QStringLiteral("Order"), QString::number(order));
        writer.writeTextElement(QStringLiteral("PartitionID"), QString::number(partitionId));
        if (!format.isEmpty()) {
            writer.writeTextElement(QStringLiteral("Format"), format);
        }
        if (!label.isEmpty()) {
            writer.writeTextElement(QStringLiteral("Label"), label);
        }
        if (!letter.isEmpty()) {
            writer.writeTextElement(QStringLiteral("Letter"), letter);
        }
        writer.writeEndElement();
    };
    writeModifyPartition(1, 1, QStringLiteral("FAT32"), QStringLiteral("System"), QString());
    writeModifyPartition(2, 2, QString(), QString(), QString());
    writeModifyPartition(3, 3, QStringLiteral("NTFS"), QStringLiteral("Windows"),
                         QStringLiteral("C"));
    writer.writeEndElement(); // ModifyPartitions
    writer.writeEndElement(); // Disk
    writer.writeTextElement(QStringLiteral("WillShowUI"), QStringLiteral("OnError"));
    writer.writeEndElement(); // DiskConfiguration

    writer.writeStartElement(QStringLiteral("ImageInstall"));
    writer.writeStartElement(QStringLiteral("OSImage"));
    writer.writeStartElement(QStringLiteral("InstallFrom"));
    writer.writeStartElement(QStringLiteral("MetaData"));
    writer.writeAttribute(QStringLiteral("wcm:action"), QStringLiteral("add"));
    writer.writeTextElement(QStringLiteral("Key"), QStringLiteral("/IMAGE/NAME"));
    writer.writeTextElement(QStringLiteral("Value"), options.edition);
    writer.writeEndElement();
    writer.writeEndElement(); // InstallFrom
    writer.writeStartElement(QStringLiteral("InstallTo"));
    writer.writeTextElement(QStringLiteral("DiskID"), QStringLiteral("0"));
    writer.writeTextElement(QStringLiteral("PartitionID"), QStringLiteral("3"));
    writer.writeEndElement();
    writer.writeTextElement(QStringLiteral("WillShowUI"), QStringLiteral("OnError"));
    writer.writeEndElement(); // OSImage
    writer.writeEndElement(); // ImageInstall

    writer.writeStartElement(QStringLiteral("UserData"));
    const QString productKey = genericProductKey(options.edition);
    if (!productKey.isEmpty()) {
        writer.writeStartElement(QStringLiteral("ProductKey"));
        writer.writeTextElement(QStringLiteral("Key"), productKey);
        writer.writeTextElement(QStringLiteral("WillShowUI"), QStringLiteral("Never"));
        writer.writeEndElement();
    }
    writer.writeTextElement(QStringLiteral("AcceptEula"), QStringLiteral("true"));
    writer.writeTextElement(QStringLiteral("FullName"), QStringLiteral("PodStack"));
    writer.writeTextElement(QStringLiteral("Organization"), QStringLiteral("PodStack"));
    writer.writeEndElement();

    writer.writeStartElement(QStringLiteral("DriverPaths"));
    const QStringList drivers = {
        QStringLiteral("viostor"),   QStringLiteral("NetKVM"),  QStringLiteral("vioscsi"),
        QStringLiteral("viogpudo"),  QStringLiteral("Balloon"), QStringLiteral("viorng"),
        QStringLiteral("vioserial"),
    };
    int driverIndex = 1;
    for (const QString &driver : drivers) {
        for (const QLatin1Char letter : {QLatin1Char('E'), QLatin1Char('F')}) {
            writer.writeStartElement(QStringLiteral("PathAndCredentials"));
            writer.writeAttribute(QStringLiteral("wcm:action"), QStringLiteral("add"));
            writer.writeAttribute(QStringLiteral("wcm:keyValue"), QString::number(driverIndex++));
            writer.writeTextElement(QStringLiteral("Path"),
                                    QStringLiteral("%1:\\%2\\w11\\amd64").arg(letter).arg(driver));
            writer.writeEndElement();
        }
    }
    writer.writeEndElement(); // DriverPaths

    writer.writeStartElement(QStringLiteral("DynamicUpdate"));
    writer.writeTextElement(QStringLiteral("Enable"), QStringLiteral("false"));
    writer.writeTextElement(QStringLiteral("WillShowUI"), QStringLiteral("Never"));
    writer.writeEndElement();
    closeComponent();

    // specialize: nome do computador e fuso.
    openSettings("specialize");
    openComponent("Microsoft-Windows-Shell-Setup");
    writer.writeTextElement(QStringLiteral("ComputerName"),
                            windowsComputerName(options.computerName));
    writer.writeTextElement(QStringLiteral("TimeZone"), timeZone);
    writer.writeTextElement(QStringLiteral("RegisteredOwner"), QStringLiteral("PodStack"));
    writer.writeTextElement(QStringLiteral("RegisteredOrganization"), QStringLiteral("PodStack"));
    closeComponent();

    // oobeSystem: conta local, OOBE sem conta online e comandos de primeiro logon.
    openSettings("oobeSystem");
    openComponent("Microsoft-Windows-International-Core");
    writer.writeTextElement(QStringLiteral("InputLocale"), input);
    writer.writeTextElement(QStringLiteral("SystemLocale"), locale);
    writer.writeTextElement(QStringLiteral("UILanguage"), locale);
    writer.writeTextElement(QStringLiteral("UserLocale"), locale);
    writer.writeEndElement(); // component (o settings fecha após o Shell-Setup)

    openComponent("Microsoft-Windows-Shell-Setup");
    writer.writeStartElement(QStringLiteral("OOBE"));
    writer.writeTextElement(QStringLiteral("HideEULAPage"), QStringLiteral("true"));
    writer.writeTextElement(QStringLiteral("HideOEMRegistrationScreen"), QStringLiteral("true"));
    writer.writeTextElement(QStringLiteral("HideOnlineAccountScreens"), QStringLiteral("true"));
    writer.writeTextElement(QStringLiteral("HideWirelessSetupInOOBE"), QStringLiteral("true"));
    writer.writeTextElement(QStringLiteral("NetworkLocation"), QStringLiteral("Work"));
    writer.writeTextElement(QStringLiteral("ProtectYourPC"), QStringLiteral("3"));
    writer.writeEndElement();

    writer.writeStartElement(QStringLiteral("UserAccounts"));
    writer.writeStartElement(QStringLiteral("LocalAccounts"));
    writer.writeStartElement(QStringLiteral("LocalAccount"));
    writer.writeAttribute(QStringLiteral("wcm:action"), QStringLiteral("add"));
    writer.writeTextElement(QStringLiteral("Name"), options.user);
    writer.writeTextElement(QStringLiteral("DisplayName"), options.user);
    writer.writeTextElement(QStringLiteral("Group"), QStringLiteral("Administrators"));
    writer.writeStartElement(QStringLiteral("Password"));
    writer.writeTextElement(QStringLiteral("Value"), options.password);
    writer.writeTextElement(QStringLiteral("PlainText"), QStringLiteral("true"));
    writer.writeEndElement();
    writer.writeEndElement(); // LocalAccount
    writer.writeEndElement(); // LocalAccounts
    writer.writeEndElement(); // UserAccounts

    writer.writeStartElement(QStringLiteral("AutoLogon"));
    writer.writeStartElement(QStringLiteral("Password"));
    writer.writeTextElement(QStringLiteral("Value"), options.password);
    writer.writeTextElement(QStringLiteral("PlainText"), QStringLiteral("true"));
    writer.writeEndElement();
    writer.writeTextElement(QStringLiteral("Enabled"), QStringLiteral("true"));
    writer.writeTextElement(QStringLiteral("LogonCount"), QStringLiteral("1"));
    writer.writeTextElement(QStringLiteral("Username"), options.user);
    writer.writeEndElement(); // AutoLogon

    writer.writeStartElement(QStringLiteral("FirstLogonCommands"));
    int order = 1;
    const auto writeCommand = [&writer, &order](const QString &command,
                                                const QString &description) {
        writer.writeStartElement(QStringLiteral("SynchronousCommand"));
        writer.writeAttribute(QStringLiteral("wcm:action"), QStringLiteral("add"));
        writer.writeTextElement(QStringLiteral("Order"), QString::number(order++));
        writer.writeTextElement(QStringLiteral("CommandLine"), command);
        writer.writeTextElement(QStringLiteral("Description"), description);
        writer.writeEndElement();
    };
    writeCommand(QStringLiteral("cmd /c if exist E:\\virtio-win-gt-x64.msi "
                                "(msiexec /i E:\\virtio-win-gt-x64.msi /qn /norestart) else "
                                "(msiexec /i F:\\virtio-win-gt-x64.msi /qn /norestart)"),
                 QStringLiteral("Instalar drivers virtio"));
    writeCommand(
        QStringLiteral("cmd /c if exist E:\\guest-agent\\qemu-ga-x86_64.msi "
                       "(msiexec /i E:\\guest-agent\\qemu-ga-x86_64.msi /qn /norestart) else "
                       "(msiexec /i F:\\guest-agent\\qemu-ga-x86_64.msi /qn /norestart)"),
        QStringLiteral("Instalar qemu-guest-agent"));
    if (options.installSsh) {
        writeCommand(
            QStringLiteral("powershell -NoProfile -ExecutionPolicy Bypass -Command "
                           "\"Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0; "
                           "Set-Service -Name sshd -StartupType Automatic; "
                           "Start-Service sshd\""),
            QStringLiteral("Instalar e habilitar OpenSSH"));
        if (!options.sshPublicKey.trimmed().isEmpty()) {
            const QString key = xmlEscapeSingleQuotes(options.sshPublicKey.trimmed());
            const QString command =
                QStringLiteral("powershell -NoProfile -ExecutionPolicy Bypass -Command "
                               "\"New-Item -ItemType Directory -Force -Path "
                               "C:\\ProgramData\\ssh | Out-Null; Set-Content -Path "
                               "C:\\ProgramData\\ssh\\administrators_authorized_keys -Value "
                               "'%1'; icacls "
                               "C:\\ProgramData\\ssh\\administrators_authorized_keys "
                               "/inheritance:r /grant Administrators:F /grant SYSTEM:F\"")
                    .arg(key);
            writeCommand(command, QStringLiteral("Autorizar a chave SSH do PodStack"));
        }
    }
    writeCommand(QStringLiteral("shutdown /r /t 10 /c \"PodStack: concluindo a instalacao\""),
                 QStringLiteral("Reiniciar após a instalação"));
    writer.writeEndElement(); // FirstLogonCommands
    closeComponent();

    writer.writeEndElement(); // unattend
    writer.writeEndDocument();
    return xml;
}

bool WindowsProvisioner::prepare(const QString &machineDir, const WindowsProvisionOptions &options,
                                 const QString &userAnswerFile, WindowsProvisionResult *result,
                                 Error *error)
{
    if (error) {
        *error = {};
    }
    if (!result) {
        if (error) {
            *error = validationError(QStringLiteral("Destino do unattend inválido."));
        }
        return false;
    }
    *result = {};

    const QFileInfo directoryInfo(machineDir);
    if (machineDir.isEmpty() || !directoryInfo.isAbsolute() || !directoryInfo.isDir()) {
        if (error) {
            *error = ioError(QStringLiteral("O diretório da machine não existe."), machineDir);
        }
        return false;
    }

    QString xml;
    if (!userAnswerFile.isEmpty()) {
        if (!validateAnswerFile(userAnswerFile, error)) {
            return false;
        }
        QFile file(userAnswerFile);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) {
                *error =
                    ioError(QStringLiteral("Não foi possível ler o answer file."), userAnswerFile);
            }
            return false;
        }
        xml = QString::fromUtf8(file.readAll());
    } else {
        xml = buildXml(options);
    }

    const QString answerPath = machineDir + QStringLiteral("/autounattend.xml");
    const QString isoPath = machineDir + QStringLiteral("/autounattend.iso");
    if (!writeFile(answerPath, xml.toUtf8(), error)) {
        return false;
    }
    if (!writeIso(isoPath, xml.toUtf8(), error)) {
        QFile::remove(answerPath);
        return false;
    }

    result->xml = xml;
    result->answerFilePath = answerPath;
    result->answerIsoPath = isoPath;
    return true;
}

} // namespace PodStack
