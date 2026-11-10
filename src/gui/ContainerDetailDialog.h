// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QRectF>
#include <QString>
#include <QWidget>

class QEvent;
class QFrame;
class QQuickWidget;

namespace PodStack {

class Backend;
class TerminalView;

/// Detalhe de um container como overlay integrado à janela principal.
///
/// O card hospeda diretamente o conteúdo de `ContainerDetail.qml` em um
/// `QQuickWidget`; o terminal é uma das abas internas do QML (entre Arquivos e
/// Inspect). A sessão (`podman exec -it`) é um `TerminalView` filho do
/// `QQuickWidget`, posicionado sobre o placeholder da aba Terminal e oculto
/// enquanto outra aba está ativa. O widget cobre o parent (a `MainWindow`) com
/// um fundo semitransparente e um card central; não é uma janela separada.
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
    TerminalView *terminalView() const { return m_terminal; }

public slots:
    /// Posiciona a sessão embutida sobre o placeholder da aba Terminal.
    /// `area` vem do QML em coordenadas do root (que preenche o QQuickWidget).
    void applyTerminalArea(const QRectF &area, bool visible);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void handleCloseRequested();
    void handleExternalTerminalRequested();

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
    QQuickWidget *m_quickWidget = nullptr;
    TerminalView *m_terminal = nullptr;
    bool m_qmlSignalsConnected = false;
    bool m_terminalVisible = false;
};

} // namespace PodStack
