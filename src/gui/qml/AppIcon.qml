import QtQuick
import Qt5Compat.GraphicalEffects

// Ícone monocromático a partir de SVG (assets/icons), tingido pelo tema.
Item {
    id: root

    property string name
    property int size: 16
    property color color: Theme.textSecondary

    implicitWidth: size
    implicitHeight: size

    Image {
        id: image
        anchors.fill: parent
        source: root.name.length > 0
                    ? "qrc:/qt/qml/PodStack/assets/icons/" + root.name + ".svg"
                    : ""
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        fillMode: Image.PreserveAspectFit
        smooth: true
    }

    ColorOverlay {
        anchors.fill: parent
        source: image
        color: root.color
        visible: image.status === Image.Ready
    }
}
