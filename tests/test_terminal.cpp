// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/Backend.h"
#include "gui/MainWindow.h"
#include "gui/Notifications.h"
#include "gui/SingleInstance.h"
#include "gui/TerminalWindow.h"
#include "gui/Tray.h"

#include <QDockWidget>
#include <QTabWidget>
#include <QTest>

#include <qtermwidget.h>

namespace {

class TerminalTest : public QObject {
    Q_OBJECT

private slots:
    void constructsWindow();
    void mainWindowTerminalDock();
};

void TerminalTest::constructsWindow()
{
    PodStack::TerminalWindow window(QStringLiteral("abc123"), QStringLiteral("/bin/sh"),
                                    {QStringLiteral("-c"), QStringLiteral("true")});
    QVERIFY(window.windowTitle().contains(QStringLiteral("abc123")));
    QVERIFY(window.findChild<QTermWidget *>() != nullptr);
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
