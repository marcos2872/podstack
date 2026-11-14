// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QStringList>
#include <QWidget>

namespace PodStack {

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
