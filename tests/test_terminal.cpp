#include "gui/TerminalWindow.h"

#include <QTest>

#include <qtermwidget.h>

namespace {

class TerminalTest : public QObject {
    Q_OBJECT

private slots:
    void constructsWindow();
};

void TerminalTest::constructsWindow()
{
    PodStack::TerminalWindow window(QStringLiteral("abc123"), QStringLiteral("/bin/sh"),
                                    {QStringLiteral("-c"), QStringLiteral("true")});
    QVERIFY(window.windowTitle().contains(QStringLiteral("abc123")));
    QVERIFY(window.findChild<QTermWidget *>() != nullptr);
}

} // namespace

QTEST_MAIN(TerminalTest)
#include "test_terminal.moc"
