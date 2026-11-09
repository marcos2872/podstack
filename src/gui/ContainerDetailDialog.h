// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QWidget>

class QEvent;
class QFrame;
class QQuickWidget;
class QTabWidget;

namespace PodStack {

class Backend;
class TerminalView;

/// Detalhe de um container como overlay integrado à janela principal.
///
/// Reúne o conteúdo de `ContainerDetail.qml` (aba **Detalhes**) e um terminal
/// embutido (`podman exec -it`) na aba **Terminal**. O widget cobre o parent
/// (a `MainWindow`) com um fundo semitransparente e um card central; não é uma
/// janela separada. É reaproveitado entre aberturas e encerra streams/sessão
/// ao fechar.
class ContainerDetailDialog : public QWidget {
    Q_OBJECT

public:
    /// `loadQml=false` é um hook de teste: monta o overlay sem carregar o
    /// módulo QML (que só existe no binário `podstack-gui`).
    explicit ContainerDetailDialog(Backend *backend, QWidget *parent,
                                   bool loadQml = true);

    void openContainer(const QString &id, int detailTab = 0);

    /// Encerra streams/sessão de terminal e esconde o overlay.
    void closeOverlay();
    void hideOverlay();

    QString containerId() const { return m_containerId; }
    bool qmlLoaded() const;
    QTabWidget *tabs() const { return m_tabs; }
    TerminalView *terminalView() const { return m_terminal; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void handleCloseRequested();

private:
    void setupInterface(bool loadQml);
    void connectQmlSignals();
    void resetTerminalSession();
    void stopTerminalSession();
    void applyOverlayGeometry();
    void teardownSession();

    Backend *m_backend = nullptr;
    QString m_containerId;
    QFrame *m_card = nullptr;
    QTabWidget *m_tabs = nullptr;
    QQuickWidget *m_quickWidget = nullptr;
    QWidget *m_terminalTab = nullptr;
    TerminalView *m_terminal = nullptr;
    bool m_qmlSignalsConnected = false;
};

} // namespace PodStack
