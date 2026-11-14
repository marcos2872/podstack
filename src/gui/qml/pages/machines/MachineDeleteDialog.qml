import QtQuick
import PodStack.Design

// Confirmação de remoção de machine.
AppConfirmDialog {
    id: control

    property string machineId: ""

    title: qsTr("Remover machine?")
    message: qsTr("A machine e o disco serão removidos. Esta ação não pode ser desfeita.")
    confirmText: qsTr("Remover")
    destructive: true

    onConfirmed: {
        if (control.machineId.length > 0) {
            backend.machines.remove(control.machineId, true)
        }
    }
}
