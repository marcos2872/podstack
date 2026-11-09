// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class QTermWidget;
class QToolButton;

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

    QTermWidget *terminal() const { return m_terminal; }

    /// Dá foco de teclado ao terminal (ao trocar para a aba/dock).
    void focusTerminal();

private:
    void openExternal();

    QString m_title;
    QString m_program;
    QStringList m_arguments;
    QLabel *m_titleLabel = nullptr;
    QToolButton *m_externalButton = nullptr;
    QTermWidget *m_terminal = nullptr;
};

} // namespace PodStack
