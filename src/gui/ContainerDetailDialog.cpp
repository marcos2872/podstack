// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/ContainerDetailDialog.h"

#include "gui/Backend.h"
#include "gui/TerminalView.h"

#include <QDebug>
#include <QLabel>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QVariant>

namespace PodStack {

ContainerDetailDialog::ContainerDetailDialog(Backend *backend, QWidget *parent, bool loadQml)
    : QDialog(parent)
    , m_backend(backend)
{
    setObjectName(QStringLiteral("containerDetailDialog"));
    setModal(true);
    setWindowModality(Qt::WindowModal);
    setWindowTitle(tr("Detalhes do container"));
    resize(1100, 760);
    setMinimumSize(720, 480);

    setupInterface(loadQml);
    connect(this, &QDialog::finished, this, &ContainerDetailDialog::handleFinished);
}

void ContainerDetailDialog::setupInterface(bool loadQml)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("containerDetailTabs"));
    m_tabs->setDocumentMode(true);
    layout->addWidget(m_tabs);

    auto *detailsTab = new QWidget(m_tabs);
    auto *detailsLayout = new QVBoxLayout(detailsTab);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    detailsLayout->setSpacing(0);

    if (loadQml) {
        m_quickWidget = new QQuickWidget(detailsTab);
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
        detailsLayout->addWidget(m_quickWidget);
    } else {
        auto *placeholder = new QLabel(tr("Detalhes indisponíveis neste contexto."), detailsTab);
        placeholder->setAlignment(Qt::AlignCenter);
        detailsLayout->addWidget(placeholder);
    }

    m_tabs->addTab(detailsTab, tr("Detalhes"));

    m_terminalTab = new QWidget(m_tabs);
    auto *terminalLayout = new QVBoxLayout(m_terminalTab);
    terminalLayout->setContentsMargins(0, 0, 0, 0);
    terminalLayout->setSpacing(0);
    m_tabs->addTab(m_terminalTab, tr("Terminal"));

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1 && m_terminal) {
            m_terminal->focusTerminal();
        }
    });
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
    connect(root, SIGNAL(closeRequested()), this, SLOT(reject()));
}

bool ContainerDetailDialog::qmlLoaded() const
{
    return m_quickWidget && m_quickWidget->status() != QQuickWidget::Error
           && m_quickWidget->rootObject() != nullptr;
}

void ContainerDetailDialog::openContainer(const QString &id, int detailTab)
{
    if (id.isEmpty()) {
        return;
    }

    if (m_backend) {
        m_backend->stopContainerDetailStats();
        m_backend->stopContainerDetailLogs();
        m_backend->clearContainerDetailLogs();
    }

    m_containerId = id;
    setWindowTitle(tr("Detalhes — %1").arg(id));
    resetTerminalSession();

    if (m_tabs) {
        m_tabs->setCurrentIndex(0);
    }

    if (m_quickWidget && m_quickWidget->rootObject()) {
        QObject *root = m_quickWidget->rootObject();
        QMetaObject::invokeMethod(root, "open", Q_ARG(QVariant, id));
        if (detailTab > 0) {
            root->setProperty("tabIndex", detailTab);
        }
    }

    centerOnParent();
    show();
    raise();
    activateWindow();
}

void ContainerDetailDialog::resetTerminalSession()
{
    stopTerminalSession();
    if (!m_terminalTab || m_containerId.isEmpty()) {
        return;
    }

    m_terminal = new TerminalView(
        tr("Terminal — %1").arg(m_containerId), QStringLiteral("podman"),
        {QStringLiteral("exec"), QStringLiteral("-it"), m_containerId,
         QStringLiteral("/bin/sh")},
        m_terminalTab);
    m_terminal->setTitleVisible(false);
    m_terminalTab->layout()->addWidget(m_terminal);
    if (m_tabs && m_tabs->currentIndex() == 1) {
        m_terminal->focusTerminal();
    }
}

void ContainerDetailDialog::stopTerminalSession()
{
    if (!m_terminal) {
        return;
    }
    m_terminalTab->layout()->removeWidget(m_terminal);
    delete m_terminal;
    m_terminal = nullptr;
}

void ContainerDetailDialog::handleFinished(int result)
{
    Q_UNUSED(result)
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

void ContainerDetailDialog::centerOnParent()
{
    QWidget *anchor = parentWidget();
    if (!anchor) {
        return;
    }
    const QRect parentRect = anchor->window()->frameGeometry();
    const QPoint topLeft(parentRect.center().x() - width() / 2,
                         parentRect.center().y() - height() / 2);
    move(topLeft);
}

} // namespace PodStack
