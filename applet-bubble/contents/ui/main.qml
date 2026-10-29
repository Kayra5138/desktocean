import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid
import Desktocean.Core

// A bubble wand standing on the desktop. Press it and drag: a bubble follows
// the cursor through the water. Hold it next to a fish for a moment and the
// fish swims in on its own and rides along calmly; let go and the bubble pops
// softly, and the fish swims out where you put it.
PlasmoidItem {
    id: root

    readonly property url wandIcon: Qt.resolvedUrl("../images/wand.svg")

    Plasmoid.icon: wandIcon
    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground
    preferredRepresentation: fullRepresentation
    toolTipMainText: "Bubble wand"
    toolTipSubText: TankHub.available ? "Drag a bubble next to a fish to carry it somewhere else"
                                      : "Desktocean is not your wallpaper"

    fullRepresentation: Item {
        Layout.minimumWidth: Kirigami.Units.gridUnit * 2
        Layout.minimumHeight: Kirigami.Units.gridUnit * 2
        Layout.preferredWidth: Kirigami.Units.gridUnit * 4
        Layout.preferredHeight: Kirigami.Units.gridUnit * 5

        Item {
            id: wandBox
            readonly property real aspect: 64 / 84
            height: Math.min(parent.height, parent.width / aspect)
            width: height * aspect
            anchors.centerIn: parent

            Image {
                anchors.fill: parent
                source: root.wandIcon
                sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                smooth: true
                // The film is out in the water while a bubble is being carried
                opacity: mouse.pressed ? 0.6 : 1
                scale: mouse.pressed ? 0.95 : mouse.containsMouse ? 1.04 : 1
                Behavior on scale { NumberAnimation { duration: 120 } }
                Behavior on opacity { NumberAnimation { duration: 120 } }
            }
        }

        MouseArea {
            id: mouse
            anchors.fill: wandBox
            hoverEnabled: true
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            // Keeps the drag going outside the widget
            preventStealing: true
            onPressed: mouseEvent => TankHub.carryAt(mouse, mouseEvent.x, mouseEvent.y)
            onPositionChanged: mouseEvent => {
                if (pressed)
                    TankHub.carryAt(mouse, mouseEvent.x, mouseEvent.y);
            }
            onReleased: TankHub.carryEnd()
            onCanceled: TankHub.carryEnd()
        }
    }
}
