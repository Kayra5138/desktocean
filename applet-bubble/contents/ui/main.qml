import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid
import Desktocean.Core

// A bubble wand standing on the desktop. Double-click it and a bubble comes
// out of its ring. While the bubble is out, press the wand and drag: the
// bubble follows the cursor. Hold it next to a fish for a moment and the fish
// swims in on its own and rides along calmly. Leave the bubble alone for three
// seconds and it floats up and pops at the surface (the fish swims out up
// there); double-click to pop it right away. Without a bubble, dragging the
// wand moves it; let go and it stays where it is.
PlasmoidItem {
    id: root

    readonly property url wandIcon: Qt.resolvedUrl("../images/wand.svg")

    Plasmoid.icon: wandIcon
    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground
    preferredRepresentation: fullRepresentation
    toolTipMainText: "Bubble wand"
    toolTipSubText: TankHub.available ? "Double-click to blow a bubble, then drag from the wand to lead it to a fish"
                                      : "Desktocean is not your wallpaper"

    // The desktop's box around this widget (an ItemContainer of the desktop's
    // AppletsLayout); null in a panel
    function desktopBox() {
        for (let p = root.parent; p; p = p.parent) {
            if (p.layout !== undefined && p.dragActive !== undefined && p.layout && p.layout.positionItem !== undefined)
                return p;
        }
        return null;
    }

    fullRepresentation: Item {
        id: holder
        Layout.minimumWidth: Kirigami.Units.gridUnit * 3
        Layout.minimumHeight: Kirigami.Units.gridUnit * 4
        Layout.preferredWidth: Kirigami.Units.gridUnit * 6
        Layout.preferredHeight: Kirigami.Units.gridUnit * 8

        property bool carrying: false
        property bool bubbleOut: false
        // Carried: the wand is drawn at the cursor (lifted) while the
        // desktop's box stays put, and only moves when it is put down (the
        // desktop animates its boxes moving, which would make it lag)
        property bool lifted: false
        property point dropAt

        Component.onDestruction: if (bubbleOut) TankHub.carryEnd()



        // Three seconds without being led anywhere: up it goes, and pops
        Timer {
            id: idle
            interval: 3000
            onTriggered: {
                TankHub.carryRise();
                holder.bubbleOut = false;
            }
        }

        function toggleBubble() {
            if (bubbleOut) {
                idle.stop();
                TankHub.carryEnd();
                bubbleOut = false;
            } else {
                // Blown just above the ring
                bubbleOut = TankHub.carryAt(ring, ring.width / 2, -ring.height * 0.9);
                if (bubbleOut)
                    idle.restart();
            }
        }

        Item {
            id: wandBox
            readonly property real aspect: 64 / 84
            height: Math.min(parent.height, parent.width / aspect)
            width: height * aspect
            anchors.centerIn: parent
            transform: Translate {
                x: holder.lifted && mouse.box ? holder.dropAt.x - mouse.box.x : 0
                y: holder.lifted && mouse.box ? holder.dropAt.y - mouse.box.y : 0
            }

            Image {
                anchors.fill: parent
                source: root.wandIcon
                sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                smooth: true
                // The film has gone into the water while a bubble is out
                opacity: holder.bubbleOut ? 0.7 : 1
                scale: mouse.pressed ? 0.96 : mouse.containsMouse ? 1.03 : 1
                rotation: holder.carrying ? -8 : 0
                Behavior on scale { NumberAnimation { duration: 120 } }
                Behavior on rotation { NumberAnimation { duration: 150 } }
                Behavior on opacity { NumberAnimation { duration: 150 } }
            }

            // The ring's middle (see wand.svg): where the bubble is
            Item {
                id: ring
                x: wandBox.width * 13 / 64
                y: wandBox.height * 5 / 84
                width: wandBox.width * 38 / 64
                height: wandBox.height * 38 / 84
            }
        }

        MouseArea {
            id: mouse
            anchors.fill: wandBox
            hoverEnabled: true
            cursorShape: holder.carrying ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            preventStealing: true

            property point grab
            property var box: null
            // This press leads the bubble rather than moving the wand
            property bool leading: false

            function putDown() {
                if (!holder.carrying)
                    return;
                if (box && holder.lifted) {
                    TankHub.placeOnDesktop(box, holder.dropAt.x, holder.dropAt.y);
                    box.layout.positionItem(box);
                    box.layout.save();
                    holder.lifted = false;
                }
                holder.carrying = false;
            }

            onPressed: mouseEvent => {
                if (holder.bubbleOut) {
                    leading = true;
                    idle.stop();
                    TankHub.carryAt(mouse, mouseEvent.x, mouseEvent.y);
                    return;
                }
                leading = false;
                box = root.desktopBox();
                holder.carrying = true;
                if (box) {
                    const at = mapToItem(box.parent, mouseEvent.x, mouseEvent.y);
                    grab = Qt.point(at.x - box.x, at.y - box.y);
                    holder.dropAt = Qt.point(box.x, box.y);
                    holder.lifted = true;
                    box.layout.releaseSpace(box);
                }
            }
            onPositionChanged: mouseEvent => {
                if (pressed && leading) {
                    TankHub.carryAt(mouse, mouseEvent.x, mouseEvent.y);
                    return;
                }
                if (!pressed || !holder.carrying || !box)
                    return;
                const at = mapToItem(box.parent, mouseEvent.x, mouseEvent.y);
                // Exactly under the cursor
                holder.dropAt = Qt.point(at.x - grab.x, at.y - grab.y);
            }
            function letGo() {
                if (leading) {
                    leading = false;
                    if (holder.bubbleOut)
                        idle.restart();
                    return;
                }
                putDown();
            }
            onReleased: letGo()
            onCanceled: letGo()
            onDoubleClicked: {
                letGo();
                holder.toggleBubble();
            }
        }
    }
}
