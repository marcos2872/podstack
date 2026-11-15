import QtQuick
import QtTest
import PodStack.Design

// Contrato mínimo do design system: tokens, primitivas e acessibilidade.
TestCase {
    name: "DesignSystem"

    Component { id: buttonComponent; AppButton { text: "OK" } }
    Component { id: modalComponent; AppModal { title: "Título" } }
    Component { id: tabsComponent; AppTabs { model: ["A", "B"] } }
    Component { id: confirmComponent; AppConfirmDialog { message: "Remover?" } }

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

    function test_confirmDialog() {
        const dialog = createTemporaryObject(confirmComponent, null)
        verify(dialog !== null)
        compare(dialog.message, "Remover?")
        compare(dialog.destructive, false)
    }
}
