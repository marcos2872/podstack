// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/UnixSignals.h"

#include <QObject>
#include <QSocketNotifier>
#include <QString>

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>

namespace PodStack {
namespace {

int g_signalPipe[2] = {-1, -1};
QSocketNotifier *g_notifier = nullptr;

void terminationHandler(int signalNumber)
{
    const int savedErrno = errno;
    const char byte = static_cast<char>(signalNumber);
    if (g_signalPipe[1] >= 0) {
        const ssize_t ignored = ::write(g_signalPipe[1], &byte, 1);
        (void) ignored;
    }
    errno = savedErrno;
}

bool createPipe(QString *error)
{
    if (::pipe(g_signalPipe) != 0) {
        if (error) {
            *error = QStringLiteral("não foi possível criar o pipe de sinais");
        }
        return false;
    }
    for (int fd : {g_signalPipe[0], g_signalPipe[1]}) {
        ::fcntl(fd, F_SETFD, FD_CLOEXEC);
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    }
    return true;
}

void closePipe()
{
    for (int &fd : g_signalPipe) {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }
}

} // namespace

bool installTerminationHandler(QObject *context, std::function<void()> callback,
                               QString *error)
{
    if (error) {
        error->clear();
    }
    if (context == nullptr || !callback) {
        if (error) {
            *error = QStringLiteral("contexto ou callback inválido");
        }
        return false;
    }

    // Uma instalação por processo; o handler de sinal não pode trocar de
    // callback com segurança.
    if (g_notifier != nullptr) {
        return true;
    }

    if (!createPipe(error)) {
        return false;
    }

    struct sigaction action {};
    action.sa_handler = terminationHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    if (::sigaction(SIGTERM, &action, nullptr) != 0
        || ::sigaction(SIGINT, &action, nullptr) != 0) {
        if (error) {
            *error = QStringLiteral("não foi possível instalar o handler de SIGTERM/SIGINT");
        }
        struct sigaction defaultAction {};
        defaultAction.sa_handler = SIG_DFL;
        sigemptyset(&defaultAction.sa_mask);
        ::sigaction(SIGTERM, &defaultAction, nullptr);
        ::sigaction(SIGINT, &defaultAction, nullptr);
        closePipe();
        return false;
    }

    g_notifier = new QSocketNotifier(g_signalPipe[0], QSocketNotifier::Read, context);
    QObject::connect(g_notifier, &QSocketNotifier::activated, context, [callback]() {
        char buffer[32];
        while (::read(g_signalPipe[0], buffer, sizeof(buffer)) > 0) {
        }
        callback();
    });
    return true;
}

} // namespace PodStack
