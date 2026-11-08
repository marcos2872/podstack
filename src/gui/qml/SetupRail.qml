import QtQuick
import QtQuick.Layouts
import PodStack

// Rail lateral com as 7 etapas do onboarding.
Item {
    id: root

    property int currentStep: 0
    property var steps: [
        { title: qsTr("Boas-vindas"), hint: qsTr("o que será feito") },
        { title: qsTr("Diagnóstico"), hint: qsTr("detecção read-only") },
        { title: qsTr("Podman"), hint: qsTr("engine e socket") },
        { title: qsTr("QEMU/KVM"), hint: qsTr("máquinas Linux") },
        { title: qsTr("Componentes"), hint: qsTr("obrigatórios e opcionais") },
        { title: qsTr("Validação"), hint: qsTr("checklist final") },
        { title: qsTr("Pronto"), hint: qsTr("abrir o PodStack") }
    ]

    implicitWidth: 220

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 4

        Repeater {
            model: root.steps.length

            delegate: Rectangle {
                id: stepItem
                required property int index
                readonly property bool active: index === root.currentStep
                readonly property bool done: index < root.currentStep
                Layout.fillWidth: true
                implicitHeight: 46
                radius: Theme.radiusSm
                color: active ? Theme.accentSoft : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 8
                    spacing: 9

                    Rectangle {
                        implicitWidth: 22
                        implicitHeight: 22
                        radius: 11
                        color: stepItem.done ? Theme.successSoft
                                             : stepItem.active ? Theme.indigo500 : Theme.bgElevated
                        border.width: stepItem.active || stepItem.done ? 0 : 1
                        border.color: Theme.borderStrong

                        Text {
                            anchors.centerIn: parent
                            visible: !stepItem.done
                            text: stepItem.index + 1
                            color: stepItem.active ? "#ffffff" : Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMicro
                            font.weight: Font.DemiBold
                        }

                        AppIcon {
                            anchors.centerIn: parent
                            visible: stepItem.done
                            name: "check"
                            size: 12
                            color: Theme.success
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Text {
                            Layout.fillWidth: true
                            text: root.steps[stepItem.index].title
                            color: stepItem.active ? Theme.textPrimary : Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            font.weight: stepItem.active ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            text: root.steps[stepItem.index].hint
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMicro
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: note.implicitHeight + 20
            radius: Theme.radiusSm
            color: Theme.bgSurface2
            border.width: 1
            border.color: Theme.border

            Text {
                id: note
                anchors.fill: parent
                anchors.margins: 10
                text: qsTr("Nada é alterado sem sua confirmação.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }
    }
}
