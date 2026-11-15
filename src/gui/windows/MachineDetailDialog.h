// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QWidget>

class QEvent;
class QFrame;
class QQuickWidget;

namespace PodStack {

class Backend;
class TerminalView;

/// Detalhe de uma machine como overlay integrado à janela principal.
///
/// Segue o padrão do `ContainerDetailDialog`: um `QWidget` filho da
/// `MainWindow` cobre a janela com fundo semitransparente e um card central
/// que hospeda `MachineInspector.qml` diretamente em um `QQuickWidget`. A aba
/// Terminal do QML reserva um placeholder e a sessão SSH (`TerminalView`) é
/// filha do `QQuickWidget`, posicionada sobre ele por `terminalAreaChanged` e
/// oculta enquanto outra aba está ativa.
class MachineDetailDialog : public QWidget {
    Q_OBJECT

public:
    /// `loadQml=false` é um hook de teste: monta o overlay sem carregar o
    /// módulo QML (que só existe no binário `podstack-gui`).
    explicit MachineDetailDialog(Backend *backend, QWidget *parent,
                                 bool loadQml = true);

    /// Busca a machine em `Backend::machines()` (ou em `machineInfo`) e abre o
    /// overlay. Machine inexistente mantém o overlay fechado.
    void openMachine(const QString &id, int tab = 0);

    /// Abre a partir de um mapa já resolvido (útil para testes e chamadores que
    /// já têm o item da lista).
    void openMachine(const QVariantMap &machine, int tab = 0);

    /// Encerra a sessão de terminal e esconde o overlay.
    void closeOverlay();
    void hideOverlay();

    QString machineId() const { return m_machineId; }
    QVariantMap machine() const { return m_machine; }
    bool qmlLoaded() const;
    TerminalView *terminalView() const { return m_terminal; }

    /// Aba atual no QML (`info`, `logs`, `stats`, `terminal`).
    static QString tabName(int tab);

public slots:
    /// Posiciona a sessão embutida sobre o placeholder da aba Terminal.
    /// `area` vem do QML em coordenadas do root (que preenche o QQuickWidget).
    void applyTerminalArea(const QRectF &area, bool visible);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void handleCloseRequested();
    void handleExternalTerminalRequested();
    void handleDeleteRequested(const QString &id);
    void refreshMachine();

private:
    void setupInterface(bool loadQml);
    void connectQmlSignals();
    void applyMachineToQml();
    void resetTerminalSession();
    void stopTerminalSession();
    void applyOverlayGeometry();
    void teardownSession();
    void updateTerminalError();

    QStringList sshArguments() const;
    QVariantMap findMachine(const QString &id) const;

    Backend *m_backend = nullptr;
    QString m_machineId;
    QVariantMap m_machine;
    int m_pendingTab = 0;
    QFrame *m_card = nullptr;
    QQuickWidget *m_quickWidget = nullptr;
    TerminalView *m_terminal = nullptr;
    bool m_qmlSignalsConnected = false;
    bool m_terminalVisible = false;
};

} // namespace PodStack
