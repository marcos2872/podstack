import QtQuick
import QtQuick.Controls
import PodStack.Design

// Indicador de carregamento com as cores do tema.
BusyIndicator {
    id: control

    /// Diâmetro visual. O desenho é feito em 24 px e escalado.
    property int size: 24

    implicitWidth: size
    implicitHeight: size
    running: visible

    contentItem: Item {
        implicitWidth: control.size
        implicitHeight: control.size

        Item {
            id: spinner
            width: 24
            height: 24
            anchors.centerIn: parent
            scale: control.size / 24

            Repeater {
                model: 8

                Rectangle {
                    required property int index
                    width: 3
                    height: 7
                    radius: 1.5
                    color: Theme.indigo400
                    opacity: 0.25 + 0.75 * index / 8
                    x: 10.5 + 8 * Math.sin(index * Math.PI / 4)
                    y: 8.5 - 8 * Math.cos(index * Math.PI / 4)
                    transformOrigin: Item.Center
                    rotation: index * 45
                }
            }

            NumberAnimation on rotation {
                from: 0
                to: 360
                duration: 900
                loops: Animation.Infinite
                running: control.running
            }
        }
    }
}
