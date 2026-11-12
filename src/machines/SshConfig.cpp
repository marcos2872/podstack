// SPDX-License-Identifier: GPL-3.0-or-later

#include "machines/SshConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

namespace PodStack {
namespace {

Error invalidParams(const QString &message, const QString &detail = {})
{
    return Error::make(QStringLiteral("PODSTACK_INVALID_PARAMS"), message, detail);
}

Error ioError(const QString &message, const QString &detail = {})
{
    return Error::make(QStringLiteral("PODSTACK_IO_ERROR"), message, detail);
}

bool fails(Error *error, const Error &value)
{
    if (error) {
        *error = value;
    }
    return false;
}

bool isValidName(const QString &name)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$"));
    return !name.isEmpty() && name.size() <= 63 && pattern.match(name).hasMatch();
}

bool hasUnsafeCharacters(const QString &value)
{
    for (const QChar character : value) {
        if (character.unicode() < 0x20 || character.unicode() == 0x7f
            || character.isSpace()) {
            return true;
        }
    }
    return false;
}

bool isValidUser(const QString &user)
{
    if (user.isEmpty() || user.size() > 32 || hasUnsafeCharacters(user)) {
        return false;
    }
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.-]*$"));
    return pattern.match(user).hasMatch();
}

QString startMarker(const QString &name)
{
    return QStringLiteral("# >>> podstack: %1").arg(name);
}

QString endMarker(const QString &name)
{
    return QStringLiteral("# <<< podstack: %1").arg(name);
}

QString blockFor(const MachineMeta &machine, bool forwardAgent)
{
    QString block = startMarker(machine.name) + QLatin1Char('\n');
    block += QStringLiteral("Host %1.podstack %1\n").arg(machine.name);
    block += QStringLiteral("  HostName 127.0.0.1\n");
    block += QStringLiteral("  Port %1\n").arg(machine.sshPort);
    block += QStringLiteral("  User %1\n").arg(machine.sshUser);
    block += QStringLiteral("  IdentityFile %1\n").arg(machine.sshKeyPath);
    block += QStringLiteral("  IdentitiesOnly yes\n");
    // known_hosts por machine: evita colisão quando a porta SSH é reutilizada
    // por outra VM (chave de host diferente).
    block += QStringLiteral("  UserKnownHostsFile %1/known_hosts\n")
                 .arg(QFileInfo(machine.sshKeyPath).absolutePath());
    if (forwardAgent) {
        block += QStringLiteral("  ForwardAgent yes\n");
    }
    block += endMarker(machine.name) + QLatin1Char('\n');
    return block;
}

/// Localiza o bloco gerenciado. Retorna false quando um marcador de início
/// existe sem o marcador de fim (arquivo editado manualmente).
bool findBlock(const QString &content, const QString &name, int *start, int *end)
{
    const QString startText = startMarker(name);
    const QString endText = endMarker(name);

    int position = 0;
    while ((position = content.indexOf(startText, position)) >= 0) {
        const bool atLineStart = position == 0 || content.at(position - 1) == QLatin1Char('\n');
        if (!atLineStart) {
            position += startText.size();
            continue;
        }
        // O marcador precisa terminar na nova linha: sem isso, uma machine
        // chamada "teste" casaria o bloco de "teste02" por prefixo.
        const int startNameEnd = position + startText.size();
        if (startNameEnd < content.size() && content.at(startNameEnd) != QLatin1Char('\n')) {
            position = startNameEnd;
            continue;
        }
        const int endPosition = content.indexOf(endText, startNameEnd);
        if (endPosition < 0) {
            return false;
        }
        const bool endAtLineStart =
            endPosition == 0 || content.at(endPosition - 1) == QLatin1Char('\n');
        if (!endAtLineStart) {
            position += startText.size();
            continue;
        }
        const int endNameEnd = endPosition + endText.size();
        if (endNameEnd < content.size() && content.at(endNameEnd) != QLatin1Char('\n')) {
            position = endNameEnd;
            continue;
        }
        *start = position;
        *end = endNameEnd;
        if (*end < content.size() && content.at(*end) == QLatin1Char('\n')) {
            ++(*end);
        }
        return true;
    }
    return true;
}

bool readExisting(const QString &path, QString *content, Error *error)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        content->clear();
        return true;
    }
    if (info.isDir()) {
        return fails(error,
                     ioError(QStringLiteral("O caminho do ssh config é um diretório."), path));
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fails(error,
                     ioError(QStringLiteral("Não foi possível ler o ssh config."),
                             file.errorString()));
    }
    *content = QString::fromUtf8(file.readAll());
    if (file.error() != QFileDevice::NoError) {
        return fails(error,
                     ioError(QStringLiteral("Falha ao ler o ssh config."),
                             file.errorString()));
    }
    return true;
}

bool ensureSshDirectory(const QString &path, Error *error)
{
    const QFileInfo info(path);
    if (info.exists()) {
        if (!info.isDir()) {
            return fails(error,
                         ioError(QStringLiteral("O caminho de ~/.ssh não é um diretório."),
                                 path));
        }
    } else if (!QDir().mkpath(path)) {
        return fails(error,
                     ioError(QStringLiteral("Não foi possível criar o diretório ~/.ssh."), path));
    }

    if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                         | QFileDevice::ExeOwner)) {
        return fails(error,
                     ioError(QStringLiteral("Não foi possível restringir ~/.ssh a 0700."), path));
    }
    return true;
}

bool writeContent(const QString &path, const QString &content, Error *error)
{
    const QFileInfo info(path);
    if (info.exists() && info.isDir()) {
        return fails(error,
                     ioError(QStringLiteral("O caminho do ssh config é um diretório."), path));
    }
    if (!ensureSshDirectory(info.absolutePath(), error)) {
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return fails(error,
                     ioError(QStringLiteral("Não foi possível abrir o ssh config para escrita."),
                             file.errorString()));
    }

    const QByteArray payload = content.toUtf8();
    if (file.write(payload) != payload.size()) {
        file.cancelWriting();
        return fails(error,
                     ioError(QStringLiteral("Não foi possível gravar o ssh config."),
                             file.errorString()));
    }
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        file.cancelWriting();
        return fails(error,
                     ioError(QStringLiteral("Não foi possível restringir o ssh config a 0600."),
                             path));
    }
    if (!file.commit()) {
        return fails(error,
                     ioError(QStringLiteral("Não foi possível publicar o ssh config."),
                             file.errorString()));
    }
    return true;
}

} // namespace

SshConfig::SshConfig(const QString &path)
{
    m_path = path.trimmed();
    if (m_path.isEmpty()) {
        const QString overridePath = qEnvironmentVariable("PODSTACK_SSH_CONFIG").trimmed();
        m_path = !overridePath.isEmpty()
                     ? overridePath
                     : QDir::homePath() + QStringLiteral("/.ssh/config");
    }
    m_path = QFileInfo(m_path).absoluteFilePath();
}

bool SshConfig::sync(const MachineMeta &machine, bool forwardAgent, Error *error)
{
    if (error) {
        *error = {};
    }

    if (!isValidName(machine.name)) {
        return fails(error, invalidParams(QStringLiteral("Nome inválido para o ssh config."),
                                          machine.name));
    }
    if (machine.sshPort < 1 || machine.sshPort > 65535) {
        return fails(error, invalidParams(QStringLiteral("Porta SSH inválida."),
                                          QString::number(machine.sshPort)));
    }
    if (!isValidUser(machine.sshUser)) {
        return fails(error, invalidParams(QStringLiteral("Usuário SSH inválido."),
                                          machine.sshUser));
    }
    if (machine.sshKeyPath.isEmpty() || hasUnsafeCharacters(machine.sshKeyPath)) {
        return fails(error, invalidParams(QStringLiteral("Caminho da chave SSH inválido."),
                                          machine.sshKeyPath));
    }

    QString content;
    if (!readExisting(m_path, &content, error)) {
        return false;
    }

    const QString block = blockFor(machine, forwardAgent || machine.forwardSshAgent);
    int start = 0;
    int end = 0;
    const bool found = findBlock(content, machine.name, &start, &end);
    if (!found) {
        return fails(error,
                     ioError(QStringLiteral("O bloco do ssh config está incompleto."),
                             machine.name));
    }

    if (start >= 0 && end > 0 && !content.isEmpty()) {
        const QString current = content.mid(start, end - start);
        if (current == block) {
            // Já está idempotente; ainda assim garante as permissões.
            if (!ensureSshDirectory(QFileInfo(m_path).absolutePath(), error)) {
                return false;
            }
            if (!QFile::setPermissions(m_path, QFileDevice::ReadOwner
                                                 | QFileDevice::WriteOwner)) {
                return fails(error,
                             ioError(QStringLiteral("Não foi possível restringir o ssh config "
                                                    "a 0600."),
                                     m_path));
            }
            return true;
        }
        content.replace(start, end - start, block);
    } else {
        if (!content.isEmpty() && !content.endsWith(QLatin1Char('\n'))) {
            content += QLatin1Char('\n');
        }
        content += block;
    }

    return writeContent(m_path, content, error);
}

bool SshConfig::remove(const QString &name, Error *error)
{
    if (error) {
        *error = {};
    }
    if (!isValidName(name)) {
        return fails(error, invalidParams(QStringLiteral("Nome inválido para o ssh config."),
                                          name));
    }

    const QFileInfo info(m_path);
    if (!info.exists()) {
        return true;
    }

    QString content;
    if (!readExisting(m_path, &content, error)) {
        return false;
    }

    int start = 0;
    int end = 0;
    const bool found = findBlock(content, name, &start, &end);
    if (!found) {
        return fails(error,
                     ioError(QStringLiteral("O bloco do ssh config está incompleto."), name));
    }
    if (start < 0 || end <= start) {
        return true;
    }

    content.remove(start, end - start);
    return writeContent(m_path, content, error);
}

} // namespace PodStack
