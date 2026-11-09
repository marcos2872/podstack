// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QStringList>
#include <QWidget>

class QTermWidget;

namespace PodStack {

/// Cria e inicia um QTermWidget com a configuração visual comum aos terminais
/// do PodStack (esquema DarkPastels, scrollbar à direita e fonte monoespaçada).
/// O chamador assume a posse do widget retornado.
QTermWidget *createTerminalWidget(const QString &program, const QStringList &args,
                                  QWidget *parent = nullptr);

/// Janela standalone mantida como fallback/legado. A GUI principal embute os
/// terminais em `MainWindow`; esta classe continua disponível para testes e
/// para abrir uma sessão fora da janela principal.
class TerminalWindow : public QWidget {
    Q_OBJECT

public:
    explicit TerminalWindow(const QString &containerId, const QString &program = {},
                            const QStringList &args = {}, QWidget *parent = nullptr);
};

} // namespace PodStack
