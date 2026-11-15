import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Modal de confirmação com ação destrutiva opcional.
AppModal {
    id: control

    property string message: ""
    property string confirmText: qsTr("Confirmar")
    property string cancelText: qsTr("Cancelar")
    property bool destructive: false

    signal confirmed()
    signal cancelled()

    width: Math.min(parent ? parent.width - 48 : 460, 460)
    closePolicy: Popup.CloseOnEscape

    onRejected: control.cancelled()

    footer: Item {
        implicitHeight: 56

        RowLayout {
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacing

            AppButton {
                text: control.cancelText
                variant: "secondary"
                onClicked: {
                    control.cancelled()
                    control.close()
                }
            }

            AppButton {
                text: control.confirmText
                variant: control.destructive ? "danger" : "primary"
                onClicked: {
                    control.confirmed()
                    control.close()
                }
            }
        }
    }

    contentItem: Text {
        text: control.message
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontUi
        wrapMode: Text.Wrap
        topPadding: 12
    }
}
