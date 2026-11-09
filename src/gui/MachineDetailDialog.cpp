// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/MachineDetailDialog.h"

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

MachineDetailDialog::MachineDetailDialog(Backend *backend, QWidget *parent, bool loadQml)
    : QWidget(parent)
    , m_backend(backend)
{
    setObjectName(QStringLiteral("machineDetailDialog"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "QWidget#machineDetailDialog { background-color: rgba(0, 0, 0, 160); }"));

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

    if (m_backend) {
        connect(m_backend, &Backend::machineInfoChanged, this,
                &MachineDetailDialog::refreshMachine);
        connect(m_backend, &Backend::machinesChanged, this,
                &MachineDetailDialog::refreshMachine);
    }

    if (QWidget *anchor = parentWidget()) {
        anchor->installEventFilter(this);
        applyOverlayGeometry();
    }
    hide();
}

void MachineDetailDialog::setupInterface(bool loadQml)
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(kOverlayMargin, kOverlayMargin, kOverlayMargin,
                                    kOverlayMargin);
    outerLayout->setSpacing(0);

    m_card = new QFrame(this);
    m_card->setObjectName(QStringLiteral("machineDetailCard"));
    m_card->setAttribute(Qt::WA_StyledBackground, true);
    m_card->setStyleSheet(QStringLiteral("QFrame#machineDetailCard {"
                                         " background-color: #12161f;"
                                         " border: 1px solid #1d2330;"
                                         " border-radius: 14px; }"));
    outerLayout->addWidget(m_card);

    auto *cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(kCardPadding, kCardPadding, kCardPadding, kCardPadding);
    cardLayout->setSpacing(0);

    if (loadQml) {
        m_quickWidget = new QQuickWidget(m_card);
        m_quickWidget->setObjectName(QStringLiteral("machineDetailQuickWidget"));
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
                                      QStringLiteral("MachineInspector"));
        connectQmlSignals();
        cardLayout->addWidget(m_quickWidget);
    } else {
        auto *placeholder = new QLabel(tr("Detalhes indisponíveis neste contexto."), m_card);
        placeholder->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(placeholder);
    }
}

void MachineDetailDialog::connectQmlSignals()
{
    if (!m_quickWidget) {
        return;
    }
    QObject *root = m_quickWidget->rootObject();
    if (!root || m_qmlSignalsConnected) {
        return;
    }
    m_qmlSignalsConnected = true;
    root->setProperty("showTerminalTab", true);
    connect(root, SIGNAL(closeRequested()), this, SLOT(handleCloseRequested()));
    // O QML declara `rect`, que o Qt registra como QRectF no metaobjeto.
    connect(root, SIGNAL(terminalAreaChanged(QRectF,bool)), this,
            SLOT(applyTerminalArea(QRectF,bool)));
    connect(root, SIGNAL(externalTerminalRequested()), this,
            SLOT(handleExternalTerminalRequested()));
    connect(root, SIGNAL(deleteRequested(QString)), this,
            SLOT(handleDeleteRequested(QString)));

    if (m_terminal && m_terminal->parentWidget() != m_quickWidget) {
        m_terminal->setParent(m_quickWidget);
        m_terminal->hide();
    }

    if (!m_machineId.isEmpty()) {
        applyMachineToQml();
        QMetaObject::invokeMethod(root, "selectTab",
                                  Q_ARG(QVariant, tabName(m_pendingTab)));
        QMetaObject::invokeMethod(root, "syncTerminalArea");
    }
}

bool MachineDetailDialog::qmlLoaded() const
{
    return m_quickWidget && m_quickWidget->status() != QQuickWidget::Error
           && m_quickWidget->rootObject() != nullptr;
}

QString MachineDetailDialog::tabName(int tab)
{
    switch (tab) {
    case 1:
        return QStringLiteral("logs");
    case 2:
        return QStringLiteral("stats");
    case 3:
        return QStringLiteral("terminal");
    default:
        return QStringLiteral("info");
    }
}

QVariantMap MachineDetailDialog::findMachine(const QString &id) const
{
    if (!m_backend || id.isEmpty()) {
        return {};
    }
    const QVariantList machines = m_backend->machines();
    for (const QVariant &value : machines) {
        const QVariantMap machine = value.toMap();
        if (machine.value(QStringLiteral("id")).toString() == id) {
            return machine;
        }
    }
    const QVariantMap info = m_backend->machineInfo();
    if (info.value(QStringLiteral("id")).toString() == id) {
        return info;
    }
    return {};
}

void MachineDetailDialog::openMachine(const QString &id, int tab)
{
    if (id.isEmpty()) {
        return;
    }
    const QVariantMap machine = findMachine(id);
    if (machine.isEmpty()) {
        return;
    }
    openMachine(machine, tab);
}

void MachineDetailDialog::openMachine(const QVariantMap &machine, int tab)
{
    const QString id = machine.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || !parentWidget()) {
        return;
    }

    if (m_backend) {
        m_backend->stopMachineStats();
    }

    m_machineId = id;
    m_machine = machine;
    m_pendingTab = tab;
    applyMachineToQml();
    resetTerminalSession();

    if (m_quickWidget && m_quickWidget->rootObject()) {
        QObject *root = m_quickWidget->rootObject();
        root->setProperty("showTerminalTab", true);
        QMetaObject::invokeMethod(root, "selectTab", Q_ARG(QVariant, tabName(tab)));
        // A aba inicial pode não mudar; reaplica a área para o terminal
        // acompanhar o placeholder depois de abrir.
        QMetaObject::invokeMethod(root, "syncTerminalArea");
    }

    if (m_backend) {
        m_backend->loadMachineInfo(id);
    }

    applyOverlayGeometry();
    show();
    raise();
    // Mantém o teclado no overlay (o QML da MainWindow continua por baixo).
    if (m_quickWidget) {
        m_quickWidget->setFocus(Qt::OtherFocusReason);
    }
}

void MachineDetailDialog::applyMachineToQml()
{
    if (!m_quickWidget || !m_quickWidget->rootObject()) {
        return;
    }
    m_quickWidget->rootObject()->setProperty("machine", m_machine);
    updateTerminalError();
}

void MachineDetailDialog::updateTerminalError()
{
    if (!m_quickWidget || !m_quickWidget->rootObject()) {
        return;
    }
    const QString keyPath = m_machine.value(QStringLiteral("sshKeyPath")).toString();
    const QString message =
        keyPath.isEmpty()
            ? tr("Chave SSH indisponível para esta machine. Abra o detalhe quando a "
                 "chave estiver criada.")
            : QString();
    m_quickWidget->rootObject()->setProperty("terminalError", message);
}

QStringList MachineDetailDialog::sshArguments() const
{
    const QString keyPath = m_machine.value(QStringLiteral("sshKeyPath")).toString();
    if (keyPath.isEmpty()) {
        return {};
    }
    int port = m_machine.value(QStringLiteral("sshPort")).toInt();
    if (port <= 0) {
        port = 22;
    }
    QString user = m_machine.value(QStringLiteral("sshUser")).toString();
    if (user.isEmpty()) {
        user = QStringLiteral("podstack");
    }
    return {
        QStringLiteral("-i"),
        keyPath,
        QStringLiteral("-p"),
        QString::number(port),
        QStringLiteral("-o"),
        QStringLiteral("IdentitiesOnly=yes"),
        QStringLiteral("-o"),
        QStringLiteral("StrictHostKeyChecking=accept-new"),
        QStringLiteral("%1@127.0.0.1").arg(user),
    };
}

void MachineDetailDialog::resetTerminalSession()
{
    stopTerminalSession();
    if (m_machineId.isEmpty() || !m_card) {
        return;
    }

    updateTerminalError();
    const QStringList args = sshArguments();
    if (args.isEmpty()) {
        // Sem chave não há sessão; o QML mostra o erro no placeholder.
        return;
    }

    // Sem QML (teste) a sessão vive no card; com QML, ela é filha do
    // QQuickWidget para usar as coordenadas do root sobre o placeholder.
    QWidget *parent = m_quickWidget ? static_cast<QWidget *>(m_quickWidget)
                                    : static_cast<QWidget *>(m_card);
    QString title = m_machine.value(QStringLiteral("name")).toString();
    if (title.isEmpty()) {
        title = m_machineId;
    }
    m_terminal = new TerminalView(tr("Terminal — %1").arg(title), QStringLiteral("ssh"),
                                  args, parent);
    // A barra do TerminalView duplicaria o cabeçalho da aba Terminal; o QML
    // oferece o botão "Nova sessão em janela".
    m_terminal->setTitleVisible(false);
    m_terminal->setToolbarVisible(false);
    m_terminal->hide();
}

void MachineDetailDialog::stopTerminalSession()
{
    if (!m_terminal) {
        return;
    }
    delete m_terminal;
    m_terminal = nullptr;
    m_terminalVisible = false;
}

void MachineDetailDialog::applyTerminalArea(const QRectF &area, bool visible)
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

void MachineDetailDialog::handleExternalTerminalRequested()
{
    if (m_machineId.isEmpty()) {
        return;
    }

    const QStringList args = sshArguments();
    if (args.isEmpty()) {
        const QString message =
            tr("Chave SSH indisponível para esta machine.");
        qWarning().noquote() << message;
        QMessageBox::warning(this, tr("Terminal externo"), message);
        return;
    }

    QString error;
    if (ExternalTerminal::launch(QStringLiteral("ssh"), args, &error)) {
        return;
    }

    qWarning().noquote() << "Falha ao abrir a nova sessão SSH em janela:" << error;
    QMessageBox::warning(this, tr("Terminal externo"), error);
}

void MachineDetailDialog::handleDeleteRequested(const QString &id)
{
    if (id.isEmpty()) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Remover machine?"),
        tr("A machine e o disco serão removidos. Esta ação não pode ser desfeita."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    if (m_backend) {
        m_backend->removeMachine(id, true);
    }
    closeOverlay();
}

void MachineDetailDialog::refreshMachine()
{
    if (!m_backend || m_machineId.isEmpty()) {
        return;
    }

    QVariantMap merged = m_machine;
    const QVariantMap info = m_backend->machineInfo();
    if (info.value(QStringLiteral("id")).toString() == m_machineId) {
        for (auto it = info.cbegin(); it != info.cend(); ++it) {
            merged.insert(it.key(), it.value());
        }
    }
    const QVariantMap listItem = findMachine(m_machineId);
    if (!listItem.isEmpty()) {
        // A lista é atualizada logo após start/stop/restart; ela manda no
        // estado para os botões do inspetor não ficarem presos.
        for (const QString &key : {QStringLiteral("state"), QStringLiteral("error"),
                                   QStringLiteral("lastBootAt"),
                                   QStringLiteral("updatedAt")}) {
            if (listItem.contains(key)) {
                merged.insert(key, listItem.value(key));
            }
        }
    }
    m_machine = merged;
    applyMachineToQml();
}

void MachineDetailDialog::closeOverlay()
{
    teardownSession();
    hide();
}

void MachineDetailDialog::hideOverlay()
{
    closeOverlay();
}

void MachineDetailDialog::handleCloseRequested()
{
    closeOverlay();
}

void MachineDetailDialog::teardownSession()
{
    if (m_backend) {
        m_backend->stopMachineStats();
    }
    if (m_quickWidget && m_quickWidget->rootObject()) {
        QObject *root = m_quickWidget->rootObject();
        root->setProperty("terminalError", QString());
        root->setProperty("machine", QVariantMap());
    }
    stopTerminalSession();
    m_machineId.clear();
    m_machine.clear();
}

void MachineDetailDialog::applyOverlayGeometry()
{
    if (QWidget *anchor = parentWidget()) {
        setGeometry(anchor->rect());
    }
}

bool MachineDetailDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        applyOverlayGeometry();
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace PodStack
