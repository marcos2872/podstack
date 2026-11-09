// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QString>

class QQuickWidget;
class QTabWidget;
class QWidget;

namespace PodStack {

class Backend;
class TerminalView;

/// Modal de detalhe de um container.
///
/// Reúne o conteúdo de `ContainerDetail.qml` (aba **Detalhes**) e um terminal
/// embutido (`podman exec -it`) na aba **Terminal**. O diálogo é modal para a
/// janela principal, é reaproveitado entre aberturas e encerra streams/sessão
/// ao fechar.
class ContainerDetailDialog : public QDialog {
    Q_OBJECT

public:
    /// `loadQml=false` é um hook de teste: monta o diálogo sem carregar o
    /// módulo QML (que só existe no binário `podstack-gui`).
    explicit ContainerDetailDialog(Backend *backend, QWidget *parent = nullptr,
                                   bool loadQml = true);

    void openContainer(const QString &id, int detailTab = 0);

    QString containerId() const { return m_containerId; }
    bool qmlLoaded() const;
    QTabWidget *tabs() const { return m_tabs; }
    TerminalView *terminalView() const { return m_terminal; }

private slots:
    void handleFinished(int result);

private:
    void setupInterface(bool loadQml);
    void connectQmlSignals();
    void resetTerminalSession();
    void stopTerminalSession();
    void centerOnParent();

    Backend *m_backend = nullptr;
    QString m_containerId;
    QTabWidget *m_tabs = nullptr;
    QQuickWidget *m_quickWidget = nullptr;
    QWidget *m_terminalTab = nullptr;
    TerminalView *m_terminal = nullptr;
    bool m_qmlSignalsConnected = false;
};

} // namespace PodStack
