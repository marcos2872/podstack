// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/CaddyProxy.h"

#include "core/Logging.h"
#include "core/Paths.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QThread>

#include <utility>

namespace PodStack {
namespace {

constexpr int kReadyTimeoutMs = 8000;

} // namespace

CaddyProxy::CaddyProxy(QString directory, QObject *parent)
    : QObject(parent), m_directory(directory.isEmpty() ? Paths::dataDir() + QStringLiteral("/caddy")
                                                       : std::move(directory))
{
}

CaddyProxy::~CaddyProxy()
{
    stop();
}

QString CaddyProxy::configText(quint16 httpsPort, quint16 httpPort, const QString &storageDir)
{
    return QStringLiteral("# Gerado pelo PodStack; não edite manualmente.\n"
                          "{\n"
                          "    admin off\n"
                          "    auto_https disable_redirects\n"
                          "    skip_install_trust\n"
                          "    https_port %1\n"
                          "    storage file_system \"%2\"\n"
                          "}\n"
                          "\n"
                          "*.localhost, *.podstack.local {\n"
                          "    tls internal {\n"
                          "        on_demand\n"
                          "    }\n"
                          "    reverse_proxy 127.0.0.1:%3\n"
                          "}\n")
        .arg(httpsPort)
        .arg(storageDir)
        .arg(httpPort);
}

bool CaddyProxy::start(quint16 httpsPort, quint16 httpPort, Error *error)
{
    if (error) {
        *error = {};
    }
    stop();

    if (httpsPort == 0 || httpPort == 0 || httpsPort == httpPort) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_INVALID_ARGUMENT"),
                                 QStringLiteral("Portas HTTP/HTTPS inválidas."));
        }
        return false;
    }

    const QString binary = QStandardPaths::findExecutable(QStringLiteral("caddy"));
    if (binary.isEmpty()) {
        if (error) {
            *error =
                Error::make(QStringLiteral("PODSTACK_NOT_FOUND"),
                            QStringLiteral("Caddy não encontrado. Instale o componente caddy."));
        }
        return false;
    }

    QDir dir(m_directory);
    if (!dir.exists() && !QDir().mkpath(m_directory)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível criar %1.").arg(m_directory));
        }
        return false;
    }
    QFile::setPermissions(m_directory,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    const QString storage = dir.filePath(QStringLiteral("storage"));
    if (!QDir().mkpath(storage)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível criar %1.").arg(storage));
        }
        return false;
    }
    QFile::setPermissions(storage,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    QSaveFile file(configPath());
    const QByteArray content = configText(httpsPort, httpPort, storage).toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size() ||
        !file.commit()) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível escrever o Caddyfile."));
        }
        return false;
    }

    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setProgram(binary);
    process->setArguments({QStringLiteral("run"), QStringLiteral("--config"), configPath(),
                           QStringLiteral("--adapter"), QStringLiteral("caddyfile")});
    connect(process, &QProcess::readyRead, this, [process] {
        const QByteArray output = process->readAll().trimmed();
        if (!output.isEmpty()) {
            qCDebug(lcRpc) << "caddy:" << output;
        }
    });
    process->start();
    if (!process->waitForStarted(5000)) {
        if (error) {
            *error = Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                                 QStringLiteral("Não foi possível iniciar o caddy."),
                                 process->errorString());
        }
        delete process;
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    bool ready = false;
    while (timer.elapsed() < kReadyTimeoutMs) {
        if (process->state() == QProcess::NotRunning) {
            break;
        }
        QTcpSocket probe;
        probe.connectToHost(QHostAddress::LocalHost, httpsPort);
        if (probe.waitForConnected(200)) {
            ready = true;
            break;
        }
        QThread::msleep(100);
    }
    if (!ready) {
        const QString output = QString::fromUtf8(process->readAll()).trimmed();
        process->kill();
        process->waitForFinished(2000);
        process->deleteLater();
        if (error) {
            *error =
                Error::make(QStringLiteral("PODSTACK_IO_ERROR"),
                            QStringLiteral("Caddy não abriu 127.0.0.1:%1.").arg(httpsPort), output);
        }
        return false;
    }

    m_process = process;
    m_httpsPort = httpsPort;
    qCInfo(lcRpc) << "HTTPS de *.localhost/*.podstack.local em 127.0.0.1:" << httpsPort;
    return true;
}

void CaddyProxy::stop()
{
    if (!m_process) {
        return;
    }
    m_process->disconnect(this);
    m_process->terminate();
    if (!m_process->waitForFinished(3000)) {
        m_process->kill();
        m_process->waitForFinished(2000);
    }
    m_process->deleteLater();
    m_process = nullptr;
    m_httpsPort = 0;
}

bool CaddyProxy::running() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

quint16 CaddyProxy::httpsPort() const
{
    return m_httpsPort;
}

QString CaddyProxy::directory() const
{
    return m_directory;
}

QString CaddyProxy::configPath() const
{
    return QDir(m_directory).filePath(QStringLiteral("Caddyfile"));
}

QString CaddyProxy::rootCertPath() const
{
    return QDir(m_directory).filePath(QStringLiteral("storage/pki/authorities/local/root.crt"));
}

} // namespace PodStack
