import QtQuick
import QtTest
import PodStack.Design

// Contrato mínimo do design system: tokens, primitivas e acessibilidade.
TestCase {
    name: "DesignSystem"

    Component { id: buttonComponent; AppButton { text: "OK" } }
    Component { id: iconButtonComponent; AppButton { iconName: "trash" } }
    Component { id: tooltipButtonComponent; AppButton { iconName: "trash"; tooltip: "Remover" } }
    Component { id: modalComponent; AppModal { title: "Título" } }
    Component { id: tabsComponent; AppTabs { model: ["A", "B"] } }
    Component { id: confirmComponent; AppConfirmDialog { message: "Remover?" } }
    Component { id: screenStateComponent; AppScreenState { } }

    function test_themeTokens() {
        compare(Theme.radius, 10)
        verify(Theme.controlHeight > 0)
        verify(Theme.fontBody > 0)
        verify(Theme.spacing > 0)
    }

    function test_buttonDefaults() {
        const button = createTemporaryObject(buttonComponent, null)
        verify(button !== null)
        compare(button.text, "OK")
        compare(button.variant, "secondary")
        compare(button.size, "md")
    }

    function test_buttonAccessibility() {
        const plain = createTemporaryObject(buttonComponent, null)
        compare(plain.Accessible.name, "OK")

        // Ícone sem rótulo: o nome acessível cai no ícone, e a dica aparece.
        const iconButton = createTemporaryObject(iconButtonComponent, null)
        compare(iconButton.Accessible.name, "trash")
        compare(iconButton.tooltipText, "trash")

        // Tooltip explícito vira o nome acessível.
        const withTooltip = createTemporaryObject(tooltipButtonComponent, null)
        compare(withTooltip.Accessible.name, "Remover")
        compare(withTooltip.tooltipText, "Remover")
    }

    function test_modalHeader() {
        const modal = createTemporaryObject(modalComponent, null)
        verify(modal !== null)
        compare(modal.title, "Título")
        compare(modal.modal, true)
    }

    function test_tabsNavigation() {
        const tabs = createTemporaryObject(tabsComponent, null)
        verify(tabs !== null)
        compare(tabs.currentIndex, 0)
        tabs.activated(1)
        compare(tabs.currentIndex, 0) // o consumidor decide o índice
    }

    function test_screenState() {
        const view = createTemporaryObject(screenStateComponent, null)
        verify(view !== null)
        compare(view.state, "ready")
        compare(view.retrying, false)
        // `visible` efetivo depende de janela/pai; o contrato é o estado.
        view.state = "error"
        compare(view.state, "error")
        view.state = "filteredEmpty"
        compare(view.state, "filteredEmpty")
    }

    function test_confirmDialog() {
        const dialog = createTemporaryObject(confirmComponent, null)
        verify(dialog !== null)
        compare(dialog.message, "Remover?")
        compare(dialog.destructive, false)
    }

    function test_modalAsyncAction() {        const modal = createTemporaryObject(modalComponent, null)
        verify(modal !== null)
        // Sem controller a ação é local: runAction fecha na hora.
        compare(modal.busy, false)
        compare(modal.awaitsAction, false)
        compare(modal.errorText, "")
        verify(typeof modal.runAction === "function")
    }
}
