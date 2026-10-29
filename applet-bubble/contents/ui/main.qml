import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid
import Desktocean.Core

// A bubble wand standing on the desktop. Double-click it and a bubble comes
// out of its ring (as many as you like). Press a bubble itself
// and drag it: hold it next to a fish for a moment and the fish swims in on
// its own and rides along calmly. Fling it and it sails on, popping if it
// hits a side; leave it alone for three seconds and it floats up and pops at
// the surface. Drag the wand to move it; let go and it stays where it is.
//
// The bubble lives in the wallpaper, which never gets clicks; the wand puts
// an invisible handle over it in the desktop's window so the bubble can be
// grabbed directly.
PlasmoidItem {
    id: root

    readonly property url wandIcon: Qt.resolvedUrl("../images/wand.svg")

    Plasmoid.icon: wandIcon
    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground
    preferredRepresentation: fullRepresentation
    toolTipMainText: "Bubble wand"
    toolTipSubText: TankHub.available ? "Double-click to blow a bubble, then drag the bubble to a fish"
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
        // Bubbles out in the water (their grab handles); the wand looks spent while any are
        property var handles: []
        readonly property bool bubbleOut: handles.length > 0
        // Carried: the wand is drawn at the cursor (lifted) while the
        // desktop's box stays put, and only moves when it is put down (the
        // desktop animates its boxes moving, which would make it lag)
        property bool lifted: false
        property point dropAt

        Component.onDestruction: {
            for (const h of handles) {
                TankHub.carryEnd(h.bubbleId);
                h.destroy();
            }
        }

        // Blown just above the ring; it floats there on its own. Each bubble
        // gets a grab handle over it in the desktop window's top layer.
        function blowBubble() {
            const id = TankHub.carryNew(ring, ring.width / 2, -ring.height * 0.9);
            const top = holder.Window.window ? holder.Window.window.contentItem : null;
            if (!id || !top)
                return;
            const h = handleComponent.createObject(top, { bubbleId: id });
            handles = handles.concat([h]);
        }

        function forget(h) {
            handles = handles.filter(other => other !== h);
            h.destroy();
        }


        Component {
            id: handleComponent
            MouseArea {
                id: handle
                required property int bubbleId
                // Above everything on the desktop (icons, widgets)
                z: 1e6
                visible: false
                hoverEnabled: true
                preventStealing: true
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor

                // Where on the bubble it was grabbed, so it doesn't jump
                property point grab

                // Keeps the handle on its bubble; goes away when the bubble pops
                Timer {
                    interval: 16
                    repeat: true
                    running: true
                    triggeredOnStart: true
                    onTriggered: {
                        const r = TankHub.carryBubbleRect(handle.bubbleId, handle.parent);
                        if (r.width <= 0) {
                            holder.forget(handle);
                            return;
                        }
                        if (!handle.pressed) {
                            handle.x = r.x;
                            handle.y = r.y;
                            handle.width = r.width;
                            handle.height = r.height;
                        }
                        handle.visible = true;
                    }
                }

                function lead(mouseEvent) {
                    const p = mapToItem(parent, mouseEvent.x, mouseEvent.y);
                    if (!TankHub.carryAt(bubbleId, parent, p.x - grab.x, p.y - grab.y))
                        holder.forget(handle);
                }
                onPressed: mouseEvent => {
                    grab = Qt.point(mouseEvent.x - width / 2, mouseEvent.y - height / 2);
                    lead(mouseEvent);
                }
                onPositionChanged: mouseEvent => {
                    if (pressed)
                        lead(mouseEvent);
                }
                onReleased: TankHub.carryRelease(bubbleId)
                onCanceled: TankHub.carryRelease(bubbleId)
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

            // The ring's middle (see wand.svg): the bubble comes out just above it
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
                if (!pressed || !holder.carrying || !box)
                    return;
                const at = mapToItem(box.parent, mouseEvent.x, mouseEvent.y);
                // Exactly under the cursor
                holder.dropAt = Qt.point(at.x - grab.x, at.y - grab.y);
            }
            onReleased: putDown()
            onCanceled: putDown()
            onDoubleClicked: {
                putDown();
                holder.blowBubble();
            }
        }
    }
}
