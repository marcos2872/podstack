// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/Backend.h"
#include "gui/ContainerDetailDialog.h"
#include "gui/ExternalTerminal.h"
#include "gui/MainWindow.h"
#include "gui/Notifications.h"
#include "gui/SingleInstance.h"
#include "gui/TerminalView.h"
#include "gui/TerminalWindow.h"
#include "gui/Tray.h"

#include <QDockWidget>
#include <QFile>
#include <QLabel>
#include <QQuickWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <qtermwidget.h>

namespace {

class TerminalTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void constructsWindow();
    void terminalViewConfiguration();
    void externalTerminalCommands();
    void externalTerminalDetection();
    void containerDetailDialog();
    void containerDetailDialogQml();
    void mainWindowTerminalDock();

private:
    QTemporaryDir m_runtime;
};

void TerminalTest::initTestCase()
{
    // Isola o Backend de um podstackd real (o refresh em background esperaria
    // o timeout de 30 s se houvesse um socket aceitando a conexão).
    QVERIFY(m_runtime.isValid());
    qputenv("PODSTACK_CONTROL_SOCKET",
            m_runtime.filePath(QStringLiteral("control.sock")).toUtf8());
}

void TerminalTest::cleanupTestCase()
{
    qunsetenv("PODSTACK_CONTROL_SOCKET");
}

void TerminalTest::constructsWindow()
{
    PodStack::TerminalWindow window(QStringLiteral("abc123"), QStringLiteral("/bin/sh"),
                                    {QStringLiteral("-c"), QStringLiteral("true")});
    QVERIFY(window.windowTitle().contains(QStringLiteral("abc123")));
    auto *terminal = window.findChild<QTermWidget *>();
    QVERIFY(terminal != nullptr);
    // Backspace precisa mandar \x7f (^?), não \b (^H).
    QCOMPARE(terminal->keyBindings(), QStringLiteral("linux"));
    QVERIFY(window.findChild<PodStack::TerminalView *>() != nullptr);
}

void TerminalTest::terminalViewConfiguration()
{
    PodStack::TerminalView view(QStringLiteral("abc123"), QStringLiteral("/bin/sh"),
                                {QStringLiteral("-c"), QStringLiteral("true")});
    QCOMPARE(view.title(), QStringLiteral("abc123"));
    QCOMPARE(view.program(), QStringLiteral("/bin/sh"));
    QCOMPARE(view.arguments(), QStringList({QStringLiteral("-c"), QStringLiteral("true")}));

    auto *terminal = view.findChild<QTermWidget *>();
    QVERIFY(terminal != nullptr);
    QCOMPARE(terminal->keyBindings(), QStringLiteral("linux"));

    auto *title = view.findChild<QLabel *>(QStringLiteral("terminalTitle"));
    QVERIFY(title != nullptr);
    QVERIFY(!title->isHidden());
    view.setTitleVisible(false);
    QVERIFY(title->isHidden());
    view.setTitleVisible(true);
    QVERIFY(!title->isHidden());

    auto *external = view.findChild<QToolButton *>(QStringLiteral("terminalExternalButton"));
    QVERIFY(external != nullptr);
    QVERIFY(!external->text().isEmpty());
}

void TerminalTest::externalTerminalCommands()
{
    const QString program = QStringLiteral("podman");
    const QStringList args{QStringLiteral("exec"), QStringLiteral("-it"),
                           QStringLiteral("abc123"), QStringLiteral("/bin/sh")};
    const QStringList direct = QStringList({program}) + args;

    QCOMPARE(PodStack::ExternalTerminal::commandFor(QStringLiteral("xdg-terminal-exec"),
                                                    program, args),
             direct);
    QCOMPARE(PodStack::ExternalTerminal::commandFor(QStringLiteral("/usr/bin/gnome-terminal"),
                                                    program, args),
             QStringList({QStringLiteral("--"), program}) + args);
    QCOMPARE(PodStack::ExternalTerminal::commandFor(QStringLiteral("kgx"), program, args),
             QStringList({QStringLiteral("--"), program}) + args);
    QCOMPARE(PodStack::ExternalTerminal::commandFor(QStringLiteral("konsole"), program, args),
             QStringList({QStringLiteral("-e"), program}) + args);
    QCOMPARE(PodStack::ExternalTerminal::commandFor(QStringLiteral("xterm"), program, args),
             QStringList({QStringLiteral("-e"), program}) + args);
    QCOMPARE(PodStack::ExternalTerminal::commandFor(QStringLiteral("alacritty"), program, args),
             QStringList({QStringLiteral("-e"), program}) + args);
    QCOMPARE(PodStack::ExternalTerminal::commandFor(QStringLiteral("xfce4-terminal"),
                                                    program, args),
             QStringList({QStringLiteral("-x"), program}) + args);
    QCOMPARE(PodStack::ExternalTerminal::commandFor(QStringLiteral("kitty"), program, args),
             direct);
    QCOMPARE(PodStack::ExternalTerminal::commandFor(QStringLiteral("foot"), program, args),
             direct);
    QCOMPARE(PodStack::ExternalTerminal::commandFor(QStringLiteral("wezterm"), program, args),
             QStringList({QStringLiteral("start"), QStringLiteral("--"), program}) + args);
}

void TerminalTest::externalTerminalDetection()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString fake = dir.filePath(QStringLiteral("xdg-terminal-exec"));
    QFile script(fake);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Text));
    script.write("#!/bin/sh\nexit 0\n");
    script.close();
    QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                  | QFileDevice::ExeOwner));

    const QByteArray oldPath = qgetenv("PATH");
    const QByteArray oldTerminal = qgetenv("TERMINAL");
    qputenv("PATH", dir.path().toUtf8() + ':' + oldPath);
    qunsetenv("TERMINAL");

    QCOMPARE(PodStack::ExternalTerminal::findTerminal(), fake);

    QString error;
    QVERIFY2(PodStack::ExternalTerminal::launch(QStringLiteral("/bin/sh"),
                                                {QStringLiteral("-c"),
                                                 QStringLiteral("true")},
                                                &error),
             qPrintable(error));
    // Deixa o processo falso iniciar antes de remover o diretório temporário.
    QTest::qWait(100);

    if (oldPath.isEmpty()) {
        qunsetenv("PATH");
    } else {
        qputenv("PATH", oldPath);
    }
    if (oldTerminal.isEmpty()) {
        qunsetenv("TERMINAL");
    } else {
        qputenv("TERMINAL", oldTerminal);
    }
}

void TerminalTest::containerDetailDialog()
{
    PodStack::Backend backend;
    PodStack::ContainerDetailDialog dialog(&backend, nullptr, /*loadQml=*/false);

    QVERIFY(dialog.isModal());
    QCOMPARE(dialog.windowModality(), Qt::WindowModal);
    QVERIFY(dialog.tabs() != nullptr);
    QCOMPARE(dialog.tabs()->count(), 2);
    QCOMPARE(dialog.tabs()->tabText(0), QStringLiteral("Detalhes"));
    QCOMPARE(dialog.tabs()->tabText(1), QStringLiteral("Terminal"));
    QCOMPARE(dialog.containerId(), QString());

    dialog.openContainer(QString());
    QCOMPARE(dialog.containerId(), QString());

    dialog.openContainer(QStringLiteral("abc123"));
    QCOMPARE(dialog.containerId(), QStringLiteral("abc123"));
    auto *terminal = dialog.terminalView();
    QVERIFY(terminal != nullptr);
    QCOMPARE(terminal->program(), QStringLiteral("podman"));
    QCOMPARE(terminal->arguments(),
             QStringList({QStringLiteral("exec"), QStringLiteral("-it"),
                          QStringLiteral("abc123"), QStringLiteral("/bin/sh")}));

    // Reabrir reaproveita o diálogo e recria a sessão do terminal.
    dialog.openContainer(QStringLiteral("def456"));
    QCOMPARE(dialog.containerId(), QStringLiteral("def456"));
    QCOMPARE(dialog.findChildren<PodStack::TerminalView *>().size(), 1);
    QCOMPARE(dialog.terminalView()->arguments().at(2), QStringLiteral("def456"));

    dialog.reject();
    QCOMPARE(dialog.containerId(), QString());
    QVERIFY(dialog.terminalView() == nullptr);
    QVERIFY(dialog.findChild<PodStack::TerminalView *>() == nullptr);
}

void TerminalTest::containerDetailDialogQml()
{
    PodStack::Backend backend;
    PodStack::ContainerDetailDialog dialog(&backend, nullptr, /*loadQml=*/true);
    if (!dialog.qmlLoaded()) {
        QSKIP("Módulo QML PodStack indisponível neste binário de teste.");
    }
    QVERIFY(dialog.findChild<QQuickWidget *>() != nullptr);
    dialog.openContainer(QStringLiteral("abc123"));
    QCOMPARE(dialog.containerId(), QStringLiteral("abc123"));
    dialog.reject();
}

void TerminalTest::mainWindowTerminalDock()
{
    PodStack::Backend backend;
    PodStack::TrayController tray(false);
    PodStack::NotificationService notifications;
    PodStack::SingleInstance singleInstance;
    // Sem QML: o módulo QML pertence ao binário podstack-gui; aqui validamos o
    // dock e a criação/adição de sessões QTermWidget.
    PodStack::MainWindow window(&backend, &tray, &notifications, &singleInstance,
                                /*startHidden=*/true, /*loadQml=*/false);

    QVERIFY(window.terminalDock() != nullptr);
    QCOMPARE(window.terminalDock()->windowTitle(), QStringLiteral("Terminal"));
    QVERIFY(window.terminalTabs() != nullptr);
    QCOMPARE(window.terminalTabs()->count(), 0);

    window.openTerminal(QStringLiteral("abc123"), QStringLiteral("/bin/sh"),
                        {QStringLiteral("-c"), QStringLiteral("true")});
    QCOMPARE(window.terminalTabs()->count(), 1);
    QCOMPARE(window.terminalTabs()->tabText(0), QStringLiteral("abc123"));
    auto *view = window.findChild<PodStack::TerminalView *>();
    QVERIFY(view != nullptr);
    QCOMPARE(view->title(), QStringLiteral("abc123"));
    QVERIFY(window.findChild<QTermWidget *>() != nullptr);
    QVERIFY(!window.terminalDock()->isHidden());
    // Deixa o shell de teste sair e ser recolhido antes de destruir o widget.
    QTest::qWait(200);

    QVERIFY(QMetaObject::invokeMethod(window.terminalTabs(), "tabCloseRequested",
                                      Qt::DirectConnection, Q_ARG(int, 0)));
    QCOMPARE(window.terminalTabs()->count(), 0);
    QVERIFY(window.terminalDock()->isHidden());
    QVERIFY(window.findChild<QTermWidget *>() == nullptr);
}

} // namespace

QTEST_MAIN(TerminalTest)
#include "test_terminal.moc"
