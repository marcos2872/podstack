// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/Backend.h"
#include "gui/ContainerDetailDialog.h"
#include "gui/ExternalTerminal.h"
#include "gui/MachineDetailDialog.h"
#include "gui/MainWindow.h"
#include "gui/Notifications.h"
#include "gui/SingleInstance.h"
#include "gui/TerminalView.h"
#include "gui/TerminalWindow.h"
#include "gui/Translation.h"
#include "gui/Tray.h"
#include "gui/UiState.h"

#include <QDockWidget>
#include <QFile>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWidget>
#include <QRectF>
#include <QScopedPointer>
#include <QShortcut>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(PodStackPlugin)

#include <qtermwidget.h>

namespace {

QVariantMap testMachine(const QString &id, const QString &keyPath)
{
    QVariantMap machine;
    machine.insert(QStringLiteral("id"), id);
    machine.insert(QStringLiteral("name"), QStringLiteral("dev"));
    machine.insert(QStringLiteral("state"), QStringLiteral("running"));
    machine.insert(QStringLiteral("sshKeyPath"), keyPath);
    machine.insert(QStringLiteral("sshPort"), 1);
    machine.insert(QStringLiteral("sshUser"), QStringLiteral("podstack"));
    machine.insert(QStringLiteral("ssh"), QStringLiteral("podstack@127.0.0.1:1"));
    return machine;
}

class TerminalTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void constructsWindow();
    void terminalViewConfiguration();
    void terminalViewRestartsOnExit();
    void externalTerminalCommands();
    void externalTerminalDetection();
    void externalTerminalDesktopFallback();
    void externalTerminalBranches();
    void selectionState();
    void containerDetailDialog();
    void containerDetailDialogQml();
    void machineDetailDialog();
    void machineDetailDialogFromList();
    void machineDetailDialogQml();
    void machineDetailOverlayErrors();
    void mainWindowTerminalDock();
    void mainWindowQml();
    void mainWindowRestoresSection();
    void mainWindowShortcuts();
    void translationCatalog();

private:
    QTemporaryDir m_runtime;
};

void TerminalTest::initTestCase()
{
    // Isola o Backend de um podstackd real (o refresh em background esperaria
    // o timeout de 30 s se houvesse um socket aceitando a conexão).
    QVERIFY(m_runtime.isValid());
    qputenv("PODSTACK_CONTROL_SOCKET", m_runtime.filePath(QStringLiteral("control.sock")).toUtf8());
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
    QCOMPARE(terminal->contextMenuPolicy(), Qt::CustomContextMenu);

    auto *copyShortcut = view.findChild<QShortcut *>(QStringLiteral("terminalCopyShortcut"));
    QVERIFY(copyShortcut != nullptr);
    QCOMPARE(copyShortcut->key(), QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    auto *pasteShortcut = view.findChild<QShortcut *>(QStringLiteral("terminalPasteShortcut"));
    QVERIFY(pasteShortcut != nullptr);
    QCOMPARE(pasteShortcut->key(), QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    QVERIFY(view.findChild<QShortcut *>(QStringLiteral("terminalCopyInsertShortcut")) != nullptr);
    QVERIFY(view.findChild<QShortcut *>(QStringLiteral("terminalPasteInsertShortcut")) != nullptr);

    auto *title = view.findChild<QLabel *>(QStringLiteral("terminalTitle"));
    QVERIFY(title != nullptr);
    QVERIFY(!title->isHidden());
    view.setTitleVisible(false);
    QVERIFY(title->isHidden());
    view.setTitleVisible(true);
    QVERIFY(!title->isHidden());

    auto *toolbar = view.findChild<QWidget *>(QStringLiteral("terminalToolbar"));
    QVERIFY(toolbar != nullptr);
    QVERIFY(!toolbar->isHidden());
    view.setToolbarVisible(false);
    QVERIFY(toolbar->isHidden());
    view.setToolbarVisible(true);
    QVERIFY(!toolbar->isHidden());

    auto *external = view.findChild<QToolButton *>(QStringLiteral("terminalExternalButton"));
    QVERIFY(external != nullptr);
    QVERIFY(!external->text().isEmpty());
}

void TerminalTest::terminalViewRestartsOnExit()
{
    PodStack::TerminalView view(QStringLiteral("restart"), QStringLiteral("/bin/sh"),
                                {QStringLiteral("-c"), QStringLiteral("exit 0")});
    QTRY_VERIFY_WITH_TIMEOUT(view.restartCount() > 0, 5000);
}

void TerminalTest::externalTerminalCommands()
{
    const QString program = QStringLiteral("podman");
    const QStringList args{QStringLiteral("exec"), QStringLiteral("-it"), QStringLiteral("abc123"),
                           QStringLiteral("/bin/sh")};
    const QStringList direct = QStringList({program}) + args;

    QCOMPARE(
        PodStack::ExternalTerminal::commandFor(QStringLiteral("xdg-terminal-exec"), program, args),
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
    QCOMPARE(PodStack::ExternalTerminal::commandFor(QStringLiteral("ghostty"), program, args),
             QStringList({QStringLiteral("-e"), program}) + args);
    QCOMPARE(
        PodStack::ExternalTerminal::commandFor(QStringLiteral("xfce4-terminal"), program, args),
        QStringList({QStringLiteral("-x"), program}) + args);
    QCOMPARE(PodStack::ExternalTerminal::commandFor(QStringLiteral("kitty"), program, args),
             direct);
    QCOMPARE(PodStack::ExternalTerminal::commandFor(QStringLiteral("foot"), program, args), direct);
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
    QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                  QFileDevice::ExeOwner));

    const QByteArray oldPath = qgetenv("PATH");
    const QByteArray oldTerminal = qgetenv("TERMINAL");
    qputenv("PATH", dir.path().toUtf8() + ':' + oldPath);
    qunsetenv("TERMINAL");

    QCOMPARE(PodStack::ExternalTerminal::findTerminal(), fake);

    QString error;
    QVERIFY2(PodStack::ExternalTerminal::launch(
                 QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("true")}, &error),
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

void TerminalTest::externalTerminalDesktopFallback()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const auto writeScript = [&dir](const QString &name, const QByteArray &body) {
        const QString path = dir.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return QString();
        }
        file.write("#!/bin/sh\n");
        file.write(body);
        file.close();
        if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                 QFileDevice::ExeOwner)) {
            return QString();
        }
        return path;
    };

    const QString alacritty =
        writeScript(QStringLiteral("alacritty"), QByteArrayLiteral("exit 0\n"));
    QVERIFY(!alacritty.isEmpty());
    // Simula um xdg-settings que não conhece a chave e imprime o usage.
    QVERIFY(!writeScript(QStringLiteral("xdg-settings"),
                         QByteArrayLiteral("echo 'xdg-settings - get various settings'\n"
                                           "exit 0\n"))
                 .isEmpty());
    // Simula gsettings apontando para xdg-terminal-exec, que não existe.
    QVERIFY(
        !writeScript(QStringLiteral("gsettings"), QByteArrayLiteral("echo \"'xdg-terminal-exec'\"\n"
                                                                    "exit 0\n"))
             .isEmpty());

    const QByteArray oldPath = qgetenv("PATH");
    const QByteArray oldTerminal = qgetenv("TERMINAL");
    const QByteArray oldDesktop = qgetenv("XDG_CURRENT_DESKTOP");
    const QByteArray oldSession = qgetenv("DESKTOP_SESSION");
    const QByteArray oldTermProgram = qgetenv("TERM_PROGRAM");

    qputenv("PATH", dir.path().toUtf8());
    qunsetenv("TERMINAL");
    qputenv("XDG_CURRENT_DESKTOP", "niri");
    qunsetenv("DESKTOP_SESSION");
    qunsetenv("TERM_PROGRAM");

    QCOMPARE(PodStack::ExternalTerminal::findTerminal(), alacritty);

    // Restaura o ambiente para os demais testes.
    auto restore = [](const char *name, const QByteArray &value) {
        if (value.isEmpty()) {
            qunsetenv(name);
        } else {
            qputenv(name, value);
        }
    };
    restore("PATH", oldPath);
    restore("TERMINAL", oldTerminal);
    restore("XDG_CURRENT_DESKTOP", oldDesktop);
    restore("DESKTOP_SESSION", oldSession);
    restore("TERM_PROGRAM", oldTermProgram);
}

void TerminalTest::externalTerminalBranches()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const auto writeScript = [&dir](const QString &name) {
        const QString path = dir.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return QString();
        }
        file.write("#!/bin/sh\nexit 0\n");
        file.close();
        if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                 QFileDevice::ExeOwner)) {
            return QString();
        }
        return path;
    };

    for (const QString &name :
         {QStringLiteral("alacritty"), QStringLiteral("ghostty"), QStringLiteral("foot"),
          QStringLiteral("wezterm"), QStringLiteral("konsole"), QStringLiteral("kitty"),
          QStringLiteral("xfce4-terminal"), QStringLiteral("gnome-terminal"),
          QStringLiteral("kgx")}) {
        QVERIFY2(!writeScript(name).isEmpty(), qPrintable(name));
    }

    const QByteArray oldPath = qgetenv("PATH");
    const QByteArray oldTerminal = qgetenv("TERMINAL");
    const QByteArray oldDesktop = qgetenv("XDG_CURRENT_DESKTOP");
    const QByteArray oldSession = qgetenv("DESKTOP_SESSION");
    const QByteArray oldTermProgram = qgetenv("TERM_PROGRAM");
    const auto restore = [&] {
        qputenv("PATH", oldPath);
        if (oldTerminal.isEmpty()) {
            qunsetenv("TERMINAL");
        } else {
            qputenv("TERMINAL", oldTerminal);
        }
        if (oldDesktop.isEmpty()) {
            qunsetenv("XDG_CURRENT_DESKTOP");
        } else {
            qputenv("XDG_CURRENT_DESKTOP", oldDesktop);
        }
        if (oldSession.isEmpty()) {
            qunsetenv("DESKTOP_SESSION");
        } else {
            qputenv("DESKTOP_SESSION", oldSession);
        }
        if (oldTermProgram.isEmpty()) {
            qunsetenv("TERM_PROGRAM");
        } else {
            qputenv("TERM_PROGRAM", oldTermProgram);
        }
    };

    qputenv("PATH", dir.path().toUtf8());
    qunsetenv("TERMINAL");
    qunsetenv("XDG_CURRENT_DESKTOP");
    qunsetenv("DESKTOP_SESSION");

    const QList<QPair<QString, QString>> termPrograms = {
        {QStringLiteral("ghostty"), QStringLiteral("ghostty")},
        {QStringLiteral("WezTerm"), QStringLiteral("wezterm")},
        {QStringLiteral("foot"), QStringLiteral("foot")},
        {QStringLiteral("konsole"), QStringLiteral("konsole")},
        {QStringLiteral("kitty"), QStringLiteral("kitty")},
        {QStringLiteral("xfce4-terminal"), QStringLiteral("xfce4-terminal")},
        {QStringLiteral("gnome-terminal"), QStringLiteral("gnome-terminal")},
        {QStringLiteral("alacritty"), QStringLiteral("alacritty")},
    };
    for (const auto &[termProgram, expected] : termPrograms) {
        qputenv("TERM_PROGRAM", termProgram.toUtf8());
        QCOMPARE(PodStack::ExternalTerminal::findTerminal(), dir.filePath(expected));
    }

    qunsetenv("TERM_PROGRAM");
    qputenv("XDG_CURRENT_DESKTOP", "KDE");
    QCOMPARE(PodStack::ExternalTerminal::findTerminal(), dir.filePath(QStringLiteral("konsole")));
    qputenv("XDG_CURRENT_DESKTOP", "GNOME");
    QCOMPARE(PodStack::ExternalTerminal::findTerminal(), dir.filePath(QStringLiteral("kgx")));
    qputenv("XDG_CURRENT_DESKTOP", "XFCE");
    QCOMPARE(PodStack::ExternalTerminal::findTerminal(),
             dir.filePath(QStringLiteral("xfce4-terminal")));

    QString error;
    qputenv("PATH", QByteArrayLiteral("/nonexistent"));
    qunsetenv("XDG_CURRENT_DESKTOP");
    qunsetenv("TERMINAL");
    QVERIFY(!PodStack::ExternalTerminal::launch(QStringLiteral("/bin/true"), {}, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!PodStack::ExternalTerminal::launch(QString(), {}, &error));
    QVERIFY(!error.isEmpty());

    qputenv("PATH", dir.path().toUtf8());
    qputenv("TERMINAL", dir.filePath(QStringLiteral("alacritty")).toUtf8());
    QVERIFY2(PodStack::ExternalTerminal::launch(QStringLiteral("/bin/true"), {}, &error),
             qPrintable(error));
    QTest::qWait(100);
    QCOMPARE(PodStack::ExternalTerminal::commandFor(QStringLiteral("desconhecido"),
                                                    QStringLiteral("/bin/true"),
                                                    {QStringLiteral("arg")}),
             QStringList({QStringLiteral("/bin/true"), QStringLiteral("arg")}));

    restore();
}

void TerminalTest::selectionState()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.loadFromModule(QStringLiteral("PodStack"), QStringLiteral("SelectionState"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> state(component.create());
    QVERIFY(state);

    const auto eval = [&](const QString &code) {
        QQmlExpression expression(engine.rootContext(), state.data(), code);
        const QVariant result = expression.evaluate();
        if (expression.hasError()) {
            qWarning().noquote() << expression.error().toString();
        }
        return result;
    };

    QCOMPARE(state->property("count").toInt(), 0);
    eval(QStringLiteral("selectOnly('a', 0)"));
    QCOMPARE(state->property("count").toInt(), 1);
    QVERIFY(eval(QStringLiteral("has('a')")).toBool());

    eval(QStringLiteral("toggle('b', 1)"));
    QCOMPARE(state->property("count").toInt(), 2);

    eval(QStringLiteral("selectRange(['a', 'b', 'c', 'd'], 3)"));
    QCOMPARE(state->property("count").toInt(), 3);
    QVERIFY(eval(QStringLiteral("has('b')")).toBool());
    QVERIFY(eval(QStringLiteral("has('d')")).toBool());

    eval(QStringLiteral("prune(['b', 'c'])"));
    QCOMPARE(state->property("count").toInt(), 2);
    QVERIFY(!eval(QStringLiteral("has('a')")).toBool());

    eval(QStringLiteral("clear()"));
    QCOMPARE(state->property("count").toInt(), 0);
}

void TerminalTest::containerDetailDialog()
{
    PodStack::Backend backend;
    QWidget host;
    host.resize(1200, 800);
    PodStack::ContainerDetailDialog overlay(&backend, &host, /*loadQml=*/false);

    // Integrado: o detalhe é um widget filho, não uma janela flutuante.
    QVERIFY(!overlay.isWindow());
    QCOMPARE(overlay.parentWidget(), &host);
    QVERIFY(overlay.isHidden());
    QVERIFY(overlay.testAttribute(Qt::WA_StyledBackground));
    QCOMPARE(overlay.geometry(), host.rect());
    QVERIFY(overlay.findChild<QFrame *>(QStringLiteral("containerDetailCard")) != nullptr);
    // O card hospeda o conteúdo QML diretamente: não há mais abas nativas
    // Detalhes/Terminal no nível do overlay.
    QVERIFY(overlay.findChild<QTabWidget *>() == nullptr);
    QVERIFY(overlay.findChild<QQuickWidget *>() == nullptr);
    QCOMPARE(overlay.containerId(), QString());

    overlay.openContainer(QString());
    QCOMPARE(overlay.containerId(), QString());
    QVERIFY(overlay.isHidden());

    host.show();
    overlay.openContainer(QStringLiteral("abc123"));
    QCOMPARE(overlay.containerId(), QStringLiteral("abc123"));
    QVERIFY(!overlay.isHidden());
    QCOMPARE(overlay.geometry(), host.rect());
    auto *terminal = overlay.terminalView();
    QVERIFY(terminal != nullptr);
    // A sessão é criada ao abrir, mas só aparece quando o QML confirma que a
    // aba Terminal está ativa e informa a área do placeholder.
    QVERIFY(terminal->isHidden());
    QCOMPARE(terminal->program(), QStringLiteral("podman"));
    QCOMPARE(terminal->arguments(),
             QStringList({QStringLiteral("exec"), QStringLiteral("-it"), QStringLiteral("abc123"),
                          QStringLiteral("/bin/sh")}));
    auto *terminalToolbar = terminal->findChild<QWidget *>(QStringLiteral("terminalToolbar"));
    QVERIFY(terminalToolbar != nullptr);
    QVERIFY(terminalToolbar->isHidden());

    const QRectF area(12, 34, 320, 200);
    overlay.applyTerminalArea(area, true);
    QVERIFY(!terminal->isHidden());
    QCOMPARE(terminal->geometry(), area.toRect());

    // Trocar de aba apenas oculta a sessão; ela continua viva.
    overlay.applyTerminalArea(QRectF(), false);
    QVERIFY(terminal->isHidden());
    QCOMPARE(overlay.terminalView(), terminal);

    // Reabrir reaproveita o overlay e recria a sessão do terminal.
    overlay.openContainer(QStringLiteral("def456"));
    QCOMPARE(overlay.containerId(), QStringLiteral("def456"));
    QCOMPARE(overlay.findChildren<PodStack::TerminalView *>().size(), 1);
    QCOMPARE(overlay.terminalView()->arguments().at(2), QStringLiteral("def456"));
    QVERIFY(overlay.terminalView()->isHidden());

    // O overlay acompanha o tamanho da janela principal.
    host.resize(1000, 700);
    QCOMPARE(overlay.geometry(), host.rect());

    overlay.closeOverlay();
    QCOMPARE(overlay.containerId(), QString());
    QVERIFY(overlay.isHidden());
    QVERIFY(overlay.terminalView() == nullptr);
    QVERIFY(overlay.findChild<PodStack::TerminalView *>() == nullptr);
}

void TerminalTest::containerDetailDialogQml()
{
    PodStack::Backend backend;
    QWidget host;
    host.resize(1200, 800);
    PodStack::ContainerDetailDialog overlay(&backend, &host, /*loadQml=*/true);
    if (!overlay.qmlLoaded()) {
        QSKIP("Módulo QML PodStack indisponível neste binário de teste.");
    }
    auto *quick = overlay.findChild<QQuickWidget *>();
    QVERIFY(quick != nullptr);
    QObject *root = quick->rootObject();
    QVERIFY(root != nullptr);

    host.show();
    overlay.openContainer(QStringLiteral("abc123"));
    QCOMPARE(overlay.containerId(), QStringLiteral("abc123"));

    // A aba Terminal (índice 7) informa a área do placeholder e o C++ mostra a
    // sessão embutida como filha do QQuickWidget; voltar oculta sem destruir.
    root->setProperty("tabIndex", 7);
    QTest::qWait(50);
    QVERIFY(overlay.terminalView() != nullptr);
    QCOMPARE(overlay.terminalView()->parentWidget(), quick);
    QVERIFY(!overlay.terminalView()->isHidden());
    QVERIFY(overlay.terminalView()->geometry().width() > 0);
    QVERIFY(overlay.terminalView()->geometry().height() > 0);

    root->setProperty("tabIndex", 0);
    QTest::qWait(10);
    QVERIFY(overlay.terminalView()->isHidden());

    overlay.closeOverlay();
    QCOMPARE(overlay.containerId(), QString());
}

void TerminalTest::machineDetailDialog()
{
    PodStack::Backend backend;
    QWidget host;
    host.resize(1200, 800);
    PodStack::MachineDetailDialog overlay(&backend, &host, /*loadQml=*/false);

    // Integrado: o detalhe é um widget filho, não uma janela flutuante.
    QVERIFY(!overlay.isWindow());
    QCOMPARE(overlay.parentWidget(), &host);
    QVERIFY(overlay.isHidden());
    QVERIFY(overlay.testAttribute(Qt::WA_StyledBackground));
    QCOMPARE(overlay.geometry(), host.rect());
    QVERIFY(overlay.findChild<QFrame *>(QStringLiteral("machineDetailCard")) != nullptr);
    QVERIFY(overlay.findChild<QQuickWidget *>() == nullptr);
    QCOMPARE(overlay.machineId(), QString());

    overlay.openMachine(QString());
    QCOMPARE(overlay.machineId(), QString());
    QVERIFY(overlay.isHidden());

    const QString keyPath = m_runtime.filePath(QStringLiteral("machine_key"));
    QFile key(keyPath);
    QVERIFY(key.open(QIODevice::WriteOnly | QIODevice::Truncate));
    key.close();
    QVERIFY(key.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));

    host.show();
    const QVariantMap machine = testMachine(QStringLiteral("m1"), keyPath);
    overlay.openMachine(machine);
    QCOMPARE(overlay.machineId(), QStringLiteral("m1"));
    QVERIFY(!overlay.isHidden());
    QCOMPARE(overlay.geometry(), host.rect());
    auto *terminal = overlay.terminalView();
    QVERIFY(terminal != nullptr);
    QCOMPARE(terminal->program(), QStringLiteral("ssh"));
    QCOMPARE(terminal->arguments(),
             QStringList({QStringLiteral("-i"), keyPath, QStringLiteral("-p"), QStringLiteral("1"),
                          QStringLiteral("-o"), QStringLiteral("IdentitiesOnly=yes"),
                          QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=accept-new"),
                          QStringLiteral("podstack@127.0.0.1")}));
    // A sessão é criada ao abrir, mas só aparece quando o QML confirma que a
    // aba Terminal está ativa e informa a área do placeholder.
    QVERIFY(terminal->isHidden());
    auto *terminalToolbar = terminal->findChild<QWidget *>(QStringLiteral("terminalToolbar"));
    QVERIFY(terminalToolbar != nullptr);
    QVERIFY(terminalToolbar->isHidden());

    const QRectF area(12, 34, 320, 200);
    overlay.applyTerminalArea(area, true);
    QVERIFY(!terminal->isHidden());
    QCOMPARE(terminal->geometry(), area.toRect());

    // Trocar de aba apenas oculta a sessão; ela continua viva.
    overlay.applyTerminalArea(QRectF(), false);
    QVERIFY(terminal->isHidden());
    QCOMPARE(overlay.terminalView(), terminal);

    // Sem a machine no Backend, o `openMachine(id)` não abre nada.
    overlay.closeOverlay();
    overlay.openMachine(QStringLiteral("m1"));
    QCOMPARE(overlay.machineId(), QString());
    QVERIFY(overlay.isHidden());

    // Reabrir reaproveita o overlay e recria a sessão SSH.
    overlay.openMachine(machine);
    QCOMPARE(overlay.machineId(), QStringLiteral("m1"));
    QCOMPARE(overlay.findChildren<PodStack::TerminalView *>().size(), 1);
    QVERIFY(overlay.terminalView()->isHidden());

    // Com o agent encaminhado, a sessão embutida recebe -A.
    QVariantMap forwardMachine = machine;
    forwardMachine.insert(QStringLiteral("forwardSshAgent"), true);
    overlay.openMachine(forwardMachine);
    QVERIFY(overlay.terminalView() != nullptr);
    QVERIFY(overlay.terminalView()->arguments().contains(QStringLiteral("-A")));

    // Sem chave SSH não há sessão; o QML mostraria o erro no placeholder.
    QVariantMap noKey = machine;
    noKey.insert(QStringLiteral("id"), QStringLiteral("m2"));
    noKey.insert(QStringLiteral("sshKeyPath"), QString());
    overlay.openMachine(noKey);
    QCOMPARE(overlay.machineId(), QStringLiteral("m2"));
    QVERIFY(overlay.terminalView() == nullptr);
    QVERIFY(overlay.findChild<PodStack::TerminalView *>() == nullptr);

    // O overlay acompanha o tamanho da janela principal.
    host.resize(1000, 700);
    QCOMPARE(overlay.geometry(), host.rect());

    overlay.closeOverlay();
    QCOMPARE(overlay.machineId(), QString());
    QVERIFY(overlay.isHidden());
    QVERIFY(overlay.terminalView() == nullptr);
    QVERIFY(overlay.findChild<PodStack::TerminalView *>() == nullptr);
}

void TerminalTest::machineDetailDialogFromList()
{
    PodStack::Backend backend;
    const QString socketPath = m_runtime.filePath(QStringLiteral("control.sock"));
    QLocalServer server;
    QVERIFY2(server.listen(socketPath), qPrintable(server.errorString()));
    const QVariantMap machine = testMachine(QStringLiteral("server-machine"), QString());
    const QJsonObject machineJson = QJsonObject::fromVariantMap(machine);

    connect(&server, &QLocalServer::newConnection, &server, [&server, machineJson] {
        while (QLocalSocket *socket = server.nextPendingConnection()) {
            const auto serve = [socket, machineJson] {
                QByteArray buffer =
                    socket->property("podstackTestBuffer").toByteArray() + socket->readAll();
                int newline = buffer.indexOf('\n');
                while (newline >= 0) {
                    const QByteArray line = buffer.left(newline);
                    buffer.remove(0, newline + 1);
                    const QJsonObject request = QJsonDocument::fromJson(line).object();
                    const QString method = request.value(QStringLiteral("method")).toString();
                    QJsonObject result;
                    if (method == QStringLiteral("machines.list")) {
                        result.insert(QStringLiteral("items"), QJsonArray{machineJson});
                    } else {
                        result = machineJson;
                    }
                    QJsonObject response;
                    response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
                    response.insert(QStringLiteral("id"), request.value(QStringLiteral("id")));
                    response.insert(QStringLiteral("result"), result);
                    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
                    socket->flush();
                    newline = buffer.indexOf('\n');
                }
                socket->setProperty("podstackTestBuffer", buffer);
            };
            connect(socket, &QLocalSocket::readyRead, socket, serve);
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            serve();
        }
    });

    backend.refreshMachines();
    QTRY_COMPARE_WITH_TIMEOUT(backend.machines().size(), 1, 5000);

    QWidget host;
    host.resize(1200, 800);
    PodStack::MachineDetailDialog overlay(&backend, &host, /*loadQml=*/false);
    host.show();
    QSignalSpy infoSpy(&backend, &PodStack::Backend::machineInfoChanged);
    overlay.openMachine(QStringLiteral("server-machine"));
    QCOMPARE(overlay.machineId(), QStringLiteral("server-machine"));
    QVERIFY(!overlay.isHidden());
    QCOMPARE(overlay.machine().value(QStringLiteral("name")).toString(), QStringLiteral("dev"));
    QTRY_VERIFY_WITH_TIMEOUT(infoSpy.count() > 0, 5000);

    overlay.closeOverlay();
    QVERIFY(overlay.isHidden());
    server.close();
    QLocalServer::removeServer(socketPath);
}

void TerminalTest::machineDetailDialogQml()
{
    PodStack::Backend backend;
    QWidget host;
    host.resize(1200, 800);
    PodStack::MachineDetailDialog overlay(&backend, &host, /*loadQml=*/true);
    if (!overlay.qmlLoaded()) {
        QSKIP("Módulo QML PodStack indisponível neste binário de teste.");
    }
    auto *quick = overlay.findChild<QQuickWidget *>();
    QVERIFY(quick != nullptr);
    QObject *root = quick->rootObject();
    QVERIFY(root != nullptr);
    QVERIFY(root->property("showTerminalTab").toBool());
    QVERIFY(root->property("showFilesTab").toBool());
    QCOMPARE(PodStack::MachineDetailDialog::tabName(4), QStringLiteral("files"));

    const QString keyPath = m_runtime.filePath(QStringLiteral("machine_qml_key"));
    QFile key(keyPath);
    QVERIFY(key.open(QIODevice::WriteOnly | QIODevice::Truncate));
    key.close();
    QVERIFY(key.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));

    host.show();
    overlay.openMachine(testMachine(QStringLiteral("m1"), keyPath), 3);
    QCOMPARE(overlay.machineId(), QStringLiteral("m1"));
    QTRY_VERIFY(overlay.terminalView() != nullptr);
    QCOMPARE(overlay.terminalView()->parentWidget(), quick);
    QTRY_VERIFY(!overlay.terminalView()->isHidden());
    QVERIFY(overlay.terminalView()->geometry().width() > 0);
    QVERIFY(overlay.terminalView()->geometry().height() > 0);

    QVERIFY(QMetaObject::invokeMethod(root, "selectTab", Q_ARG(QVariant, QStringLiteral("info"))));
    QTRY_VERIFY(overlay.terminalView()->isHidden());

    QVERIFY(QMetaObject::invokeMethod(root, "close"));
    QTRY_VERIFY(overlay.isHidden());
    QCOMPARE(overlay.machineId(), QString());
    QVERIFY(overlay.findChild<PodStack::TerminalView *>() == nullptr);
}

void TerminalTest::machineDetailOverlayErrors()
{
    PodStack::Backend backend;
    QWidget host;
    host.resize(1200, 800);
    PodStack::MachineDetailDialog overlay(&backend, &host, /*loadQml=*/true);
    if (!overlay.qmlLoaded()) {
        QSKIP("Módulo QML PodStack indisponível neste binário de teste.");
    }

    overlay.openMachine(QStringLiteral("missing"));
    QVERIFY(overlay.isHidden());
    QVERIFY(overlay.machineId().isEmpty());
    overlay.openMachine(QVariantMap{});
    QVERIFY(overlay.isHidden());
    QVERIFY(overlay.machineId().isEmpty());

    overlay.applyTerminalArea(QRectF(1, 1, 10, 10), true);
    overlay.hideOverlay();
    QVERIFY(overlay.isHidden());

    QCOMPARE(PodStack::MachineDetailDialog::tabName(0), QStringLiteral("info"));
    QCOMPARE(PodStack::MachineDetailDialog::tabName(1), QStringLiteral("logs"));
    QCOMPARE(PodStack::MachineDetailDialog::tabName(2), QStringLiteral("stats"));
    QCOMPARE(PodStack::MachineDetailDialog::tabName(3), QStringLiteral("terminal"));
    QCOMPARE(PodStack::MachineDetailDialog::tabName(4), QStringLiteral("files"));
    QCOMPARE(PodStack::MachineDetailDialog::tabName(99), QStringLiteral("info"));

    overlay.closeOverlay();
    QVERIFY(overlay.machineId().isEmpty());
}

void TerminalTest::mainWindowTerminalDock()
{
    PodStack::Backend backend;
    PodStack::TrayController tray(false);
    PodStack::NotificationService notifications;
    PodStack::SingleInstance singleInstance;
    // Sem QML: valida o dock e a criação/adição de sessões QTermWidget.
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
                                      Qt::DirectConnection, Q_ARG(int, -1)));
    QCOMPARE(window.terminalTabs()->count(), 1);
    QVERIFY(QMetaObject::invokeMethod(window.terminalTabs(), "tabCloseRequested",
                                      Qt::DirectConnection, Q_ARG(int, 0)));
    QCOMPARE(window.terminalTabs()->count(), 0);
    QVERIFY(window.terminalDock()->isHidden());
    QVERIFY(window.findChild<QTermWidget *>() == nullptr);

    window.close();
    QVERIFY(!window.isVisible());
}

void TerminalTest::mainWindowRestoresSection()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString statePath = dir.filePath(QStringLiteral("ui-state.json"));
    qputenv("PODSTACK_UI_STATE", statePath.toUtf8());

    PodStack::UiState state;
    state.section = QStringLiteral("machines");
    state.containerTab = 3;
    state.machineTab = QStringLiteral("logs");
    QVERIFY(state.save(statePath));

    PodStack::Backend backend;
    PodStack::TrayController tray(false);
    PodStack::NotificationService notifications;
    PodStack::SingleInstance singleInstance;
    PodStack::MainWindow window(&backend, &tray, &notifications, &singleInstance,
                                /*startHidden=*/true, /*loadQml=*/true);

    QTRY_VERIFY_WITH_TIMEOUT(window.qmlLoaded(), 5000);
    auto *quick = qobject_cast<QQuickWidget *>(window.centralWidget());
    QVERIFY(quick != nullptr);
    QObject *root = quick->rootObject();
    QVERIFY(root != nullptr);
    QCOMPARE(root->property("section").toString(), QStringLiteral("machines"));

    QMetaObject::invokeMethod(root, "openSection", Q_ARG(QVariant, QStringLiteral("images")));
    QTRY_COMPARE_WITH_TIMEOUT(root->property("section").toString(), QStringLiteral("images"), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(
        [&statePath]() {
            PodStack::UiState saved;
            return saved.load(statePath) && saved.section == QStringLiteral("images");
        }(),
        3000);

    qunsetenv("PODSTACK_UI_STATE");
}

void TerminalTest::mainWindowShortcuts()
{
    PodStack::Backend backend;
    PodStack::TrayController tray(false);
    PodStack::NotificationService notifications;
    PodStack::SingleInstance singleInstance;
    PodStack::MainWindow window(&backend, &tray, &notifications, &singleInstance,
                                /*startHidden=*/true, /*loadQml=*/true);
    QTRY_VERIFY_WITH_TIMEOUT(window.qmlLoaded(), 5000);

    auto *quick = qobject_cast<QQuickWidget *>(window.centralWidget());
    QVERIFY(quick != nullptr);
    QObject *root = quick->rootObject();
    QVERIFY(root != nullptr);

    const QStringList shortcutNames{
        QStringLiteral("shortcutNew"),
        QStringLiteral("shortcutSettings"),
        QStringLiteral("shortcutSidebar"),
        QStringLiteral("shortcutRefresh"),
        QStringLiteral("shortcutQuit"),
        QStringLiteral("shortcutContainerLogs"),
        QStringLiteral("shortcutContainerTerminal"),
        QStringLiteral("shortcutContainerLifecycle"),
    };
    for (const QString &name : shortcutNames) {
        QVERIFY2(root->findChild<QObject *>(name) != nullptr, qPrintable(name));
    }

    auto *containerList = root->findChild<QObject *>(QStringLiteral("containerList"));
    QVERIFY(containerList != nullptr);
    QCOMPARE(containerList->property("activeFocusOnTab").toBool(), true);
}

void TerminalTest::translationCatalog()
{
    QVERIFY(PodStack::installTranslation(qApp, QStringLiteral("en-US")));
    QCOMPARE(QCoreApplication::translate("Main", "Buscar containers"),
             QStringLiteral("Search containers"));
    QCOMPARE(QCoreApplication::translate("TrayController", "Sair"), QStringLiteral("Quit"));

    QVERIFY(PodStack::installTranslation(qApp, QStringLiteral("pt-BR")));
    QCOMPARE(QCoreApplication::translate("Main", "Buscar containers"),
             QStringLiteral("Buscar containers"));

    QVERIFY(PodStack::installTranslation(qApp, QStringLiteral("fr-FR")));
    QVERIFY(!PodStack::installTranslation(nullptr, QStringLiteral("en-US")));

    QVERIFY(PodStack::installTranslation(qApp, QStringLiteral("en-US")));
}

void TerminalTest::mainWindowQml()
{
    PodStack::Backend backend;
    PodStack::TrayController tray(false);
    PodStack::NotificationService notifications;
    PodStack::SingleInstance singleInstance;
    PodStack::MainWindow window(&backend, &tray, &notifications, &singleInstance,
                                /*startHidden=*/true, /*loadQml=*/true);

    QTRY_VERIFY_WITH_TIMEOUT(window.qmlLoaded(), 5000);
    QVERIFY(!window.qmlFailed());
    QVERIFY(window.centralWidget() != nullptr);
    QVERIFY(window.terminalDock() != nullptr);

    // O dialogo de configuracoes usa roles heterogeneos (string/bool/numero);
    // abrir e reconstruir as linhas precisa continuar funcionando.
    auto *quick = qobject_cast<QQuickWidget *>(window.centralWidget());
    QVERIFY(quick != nullptr);
    QObject *rootObject = quick->rootObject();
    QVERIFY(rootObject != nullptr);
    QObject *k8sPage = rootObject->findChild<QObject *>(QStringLiteral("kubernetesPage"));
    QVERIFY(k8sPage != nullptr);
    QCOMPARE(k8sPage->property("tab").toInt(), 0);
    k8sPage->setProperty("tab", 1);
    QCOMPARE(k8sPage->property("tab").toInt(), 1);
    k8sPage->setProperty("tab", 0);
    QVERIFY(rootObject->findChild<QObject *>(QStringLiteral("k8sLogsDialog")) != nullptr);
    QVERIFY(rootObject->findChild<QObject *>(QStringLiteral("k8sDeleteDialog")) != nullptr);
    QVERIFY(rootObject->findChild<QObject *>(QStringLiteral("k8sForwardDialog")) != nullptr);
    QVERIFY(rootObject->findChild<QObject *>(QStringLiteral("k8sClusterActionDialog")) != nullptr);

    QObject *settings = rootObject->findChild<QObject *>(QStringLiteral("settingsDialog"));
    QVERIFY(settings != nullptr);
    QVERIFY(QMetaObject::invokeMethod(settings, "open"));
    // Sem daemon no teste, o schema fica vazio; o importante é o dialog abrir
    // sem ReferenceError e o array de linhas existir.
    QVERIFY(settings->property("settingsRows").isValid());
    QVERIFY(QMetaObject::invokeMethod(settings, "close"));

    window.reveal();
    window.hideToTray();
    window.openContainerDetail(QStringLiteral("missing"));
    window.openContainerDetail(QString());
    window.openContainerDetailTab(QString(), 0);
    window.openMachineDetail(QStringLiteral("missing"));
    window.openMachineDetail(QString());
    window.showMinimized();
    window.reveal();
}

} // namespace

QTEST_MAIN(TerminalTest)
#include "test_terminal.moc"
