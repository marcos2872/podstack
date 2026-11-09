// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class QTermWidget;
class QToolButton;
class QVBoxLayout;

namespace PodStack {

/// Terminal reutilizável do PodStack.
///
/// Encapsula um `QTermWidget` já configurado (DarkPastels, scrollbar, fonte
/// fixa e keytab `linux`) e uma barra com o título da sessão e o botão
/// **Abrir externo**, que relança o mesmo programa/argumentos em um emulador
/// de terminal do sistema.
class TerminalView : public QWidget {
    Q_OBJECT

public:
    explicit TerminalView(const QString &title, const QString &program,
                          const QStringList &args, QWidget *parent = nullptr);

    QString title() const { return m_title; }
    QString program() const { return m_program; }
    QStringList arguments() const { return m_arguments; }

    /// Mostra/oculta o rótulo do título na barra (o botão externo permanece).
    void setTitleVisible(bool visible);

    /// Mostra/oculta a barra inteira (título + botão externo). Usado quando o
    /// dono já oferece os próprios controles, como a aba Terminal do detalhe.
    void setToolbarVisible(bool visible);

    QTermWidget *terminal() const { return m_terminal; }

    /// Quantas vezes a sessão foi reiniciada após o shell terminar (`exit`).
    int restartCount() const { return m_restartCount; }

    /// Dá foco de teclado ao terminal (ao trocar para a aba/dock).
    void focusTerminal();

signals:
    /// O shell foi reiniciado após `exit`.
    void sessionRestarted();
    /// O shell terminou rápido demais repetidas vezes; o reinício foi desativado.
    void sessionEnded();

private slots:
    void handleTerminalFinished();

private:
    void openExternal();
    void configureTerminal(QTermWidget *terminal);
    void startShell();
    void restartSession();
    void rebuildTerminal();

    QString m_title;
    QString m_program;
    QStringList m_arguments;
    QVBoxLayout *m_layout = nullptr;
    QWidget *m_toolbar = nullptr;
    QLabel *m_titleLabel = nullptr;
    QToolButton *m_externalButton = nullptr;
    QTermWidget *m_terminal = nullptr;
    QElapsedTimer m_lastStart;
    int m_fastExits = 0;
    int m_restartCount = 0;
    bool m_restartEnabled = true;
};

} // namespace PodStack
