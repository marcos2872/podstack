// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/ContainerDetailDialog.h"

#include "gui/Backend.h"
#include "gui/ExternalTerminal.h"
#include "gui/TerminalView.h"

#include <QColor>
#include <QDebug>
#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QVBoxLayout>
#include <QVariant>

namespace {

constexpr int kOverlayMargin = 24;
constexpr int kCardPadding = 8;

} // namespace

namespace PodStack {

ContainerDetailDialog::ContainerDetailDialog(Backend *backend, QWidget *parent, bool loadQml)
    : QWidget(parent)
    , m_backend(backend)
{
    setObjectName(QStringLiteral("containerDetailDialog"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "QWidget#containerDetailDialog { background-color: rgba(0, 0, 0, 160); }"));

    // A UI QML e o QTermWidget são escuros; alinhe os widgets nativos do card
    // (terminal) com o restante do app.
    QPalette darkPalette = palette();
    darkPalette.setColor(QPalette::Window, QColor(QStringLiteral("#0e1118")));
    darkPalette.setColor(QPalette::WindowText, QColor(QStringLiteral("#e7eaf0")));
    darkPalette.setColor(QPalette::Base, QColor(QStringLiteral("#12161f")));
    darkPalette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#171c27")));
    darkPalette.setColor(QPalette::Text, QColor(QStringLiteral("#e7eaf0")));
    darkPalette.setColor(QPalette::Button, QColor(QStringLiteral("#171c27")));
    darkPalette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#e7eaf0")));
    darkPalette.setColor(QPalette::Highlight, QColor(QStringLiteral("#6366f1")));
    darkPalette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
    darkPalette.setColor(QPalette::Mid, QColor(QStringLiteral("#1d2330")));
    setPalette(darkPalette);

    setupInterface(loadQml);

    if (QWidget *anchor = parentWidget()) {
        anchor->installEventFilter(this);
        applyOverlayGeometry();
    }
    hide();
}

void ContainerDetailDialog::setupInterface(bool loadQml)
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(kOverlayMargin, kOverlayMargin, kOverlayMargin,
                                    kOverlayMargin);
    outerLayout->setSpacing(0);

    m_card = new QFrame(this);
    m_card->setObjectName(QStringLiteral("containerDetailCard"));
    m_card->setAttribute(Qt::WA_StyledBackground, true);
    m_card->setStyleSheet(QStringLiteral("QFrame#containerDetailCard {"
                                         " background-color: #12161f;"
                                         " border: 1px solid #1d2330;"
                                         " border-radius: 14px; }"));
    outerLayout->addWidget(m_card);

    auto *cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(kCardPadding, kCardPadding, kCardPadding, kCardPadding);
    cardLayout->setSpacing(0);

    if (loadQml) {
        m_quickWidget = new QQuickWidget(m_card);
        m_quickWidget->setObjectName(QStringLiteral("containerDetailQuickWidget"));
        m_quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
        m_quickWidget->setClearColor(palette().color(QPalette::Window));
        QQmlContext *context = m_quickWidget->rootContext();
        context->setContextProperty(QStringLiteral("backend"), m_backend);
        connect(m_quickWidget, &QQuickWidget::statusChanged, this,
                [this](QQuickWidget::Status status) {
                    if (status == QQuickWidget::Error) {
                        for (const QQmlError &error : m_quickWidget->errors()) {
                            qWarning().noquote() << error.toString();
                        }
                    } else if (status == QQuickWidget::Ready) {
                        connectQmlSignals();
                    }
                });
        m_quickWidget->loadFromModule(QStringLiteral("PodStack"),
                                      QStringLiteral("ContainerDetail"));
        connectQmlSignals();
        cardLayout->addWidget(m_quickWidget);
    } else {
        auto *placeholder = new QLabel(tr("Detalhes indisponíveis neste contexto."), m_card);
        placeholder->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(placeholder);
    }
}

void ContainerDetailDialog::connectQmlSignals()
{
    if (!m_quickWidget) {
        return;
    }
    QObject *root = m_quickWidget->rootObject();
    if (!root || m_qmlSignalsConnected) {
        return;
    }
    m_qmlSignalsConnected = true;
    connect(root, SIGNAL(closeRequested()), this, SLOT(handleCloseRequested()));
    // O QML declara `rect`, que o Qt registra como QRectF no metaobjeto.
    connect(root, SIGNAL(terminalAreaChanged(QRectF,bool)), this,
            SLOT(applyTerminalArea(QRectF,bool)));
    connect(root, SIGNAL(externalTerminalRequested()), this,
            SLOT(handleExternalTerminalRequested()));
}

bool ContainerDetailDialog::qmlLoaded() const
{
    return m_quickWidget && m_quickWidget->status() != QQuickWidget::Error
           && m_quickWidget->rootObject() != nullptr;
}

void ContainerDetailDialog::openContainer(const QString &id, int detailTab)
{
    if (id.isEmpty() || !parentWidget()) {
        return;
    }

    if (m_backend) {
        m_backend->stopContainerDetailStats();
        m_backend->stopContainerDetailLogs();
        m_backend->clearContainerDetailLogs();
    }

    m_containerId = id;
    resetTerminalSession();

    if (m_quickWidget && m_quickWidget->rootObject()) {
        QObject *root = m_quickWidget->rootObject();
        QMetaObject::invokeMethod(root, "open", Q_ARG(QVariant, id));
        if (detailTab > 0) {
            root->setProperty("tabIndex", detailTab);
        }
        // O QML pode não emitir nada se a aba inicial não mudou; reaplica a
        // área depois de abrir para o terminal acompanhar o placeholder.
        QMetaObject::invokeMethod(root, "syncTerminalArea");
    }

    applyOverlayGeometry();
    show();
    raise();
    // Mantém o teclado no overlay (o QML da MainWindow continua por baixo).
    if (m_quickWidget) {
        m_quickWidget->setFocus(Qt::OtherFocusReason);
    }
}

void ContainerDetailDialog::resetTerminalSession()
{
    stopTerminalSession();
    if (m_containerId.isEmpty() || !m_card) {
        return;
    }

    // Sem QML (teste) a sessão vive no card; com QML, ela é filha do
    // QQuickWidget para usar as coordenadas do root sobre o placeholder.
    QWidget *parent = m_quickWidget ? static_cast<QWidget *>(m_quickWidget)
                                    : static_cast<QWidget *>(m_card);
    m_terminal = new TerminalView(
        tr("Terminal — %1").arg(m_containerId), QStringLiteral("podman"),
        {QStringLiteral("exec"), QStringLiteral("-it"), m_containerId,
         QStringLiteral("/bin/sh")},
        parent);
    // A barra do TerminalView duplicaria o cabeçalho da aba Terminal; o QML
    // oferece o botão "Nova sessão em janela".
    m_terminal->setTitleVisible(false);
    m_terminal->setToolbarVisible(false);
    m_terminal->hide();
}

void ContainerDetailDialog::stopTerminalSession()
{
    if (!m_terminal) {
        return;
    }
    delete m_terminal;
    m_terminal = nullptr;
    m_terminalVisible = false;
}

void ContainerDetailDialog::applyTerminalArea(const QRectF &area, bool visible)
{
    if (!m_terminal) {
        return;
    }
    if (!visible || area.width() <= 0 || area.height() <= 0) {
        m_terminal->hide();
        m_terminalVisible = false;
        return;
    }

    const bool wasVisible = m_terminalVisible;
    m_terminal->setGeometry(area.toRect());
    m_terminal->show();
    m_terminal->raise();
    m_terminalVisible = true;
    if (!wasVisible) {
        m_terminal->focusTerminal();
    }
}

void ContainerDetailDialog::handleExternalTerminalRequested()
{
    if (m_containerId.isEmpty()) {
        return;
    }

    QString error;
    if (ExternalTerminal::launch(QStringLiteral("podman"),
                                 {QStringLiteral("exec"), QStringLiteral("-it"),
                                  m_containerId, QStringLiteral("/bin/sh")},
                                 &error)) {
        return;
    }

    qWarning().noquote() << "Falha ao abrir a nova sessão em janela:" << error;
    QMessageBox::warning(this, tr("Terminal externo"), error);
}

void ContainerDetailDialog::closeOverlay()
{
    teardownSession();
    hide();
}

void ContainerDetailDialog::hideOverlay()
{
    closeOverlay();
}

void ContainerDetailDialog::handleCloseRequested()
{
    closeOverlay();
}

void ContainerDetailDialog::teardownSession()
{
    if (m_backend) {
        m_backend->stopContainerDetailStats();
        m_backend->stopContainerDetailLogs();
        m_backend->clearContainerDetailLogs();
    }
    if (m_quickWidget && m_quickWidget->rootObject()) {
        // Evita que uma ação em voo reabra os streams depois do fechamento.
        QObject *root = m_quickWidget->rootObject();
        root->setProperty("removePending", false);
        root->setProperty("reloadPending", false);
        root->setProperty("containerId", QString());
    }
    stopTerminalSession();
    m_containerId.clear();
}

void ContainerDetailDialog::applyOverlayGeometry()
{
    if (QWidget *anchor = parentWidget()) {
        setGeometry(anchor->rect());
    }
}

bool ContainerDetailDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        applyOverlayGeometry();
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace PodStack
