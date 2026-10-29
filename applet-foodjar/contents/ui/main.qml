import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid
import Desktocean.Core

// A jar of fish food standing on the desktop. Press it to pick it up and carry
// it around; shake it back and forth over a spot and after a moment food falls
// into the aquarium right below. Let go and it stays there.
PlasmoidItem {
    id: root

    readonly property url jarIcon: Qt.resolvedUrl("../images/foodjar.svg")

    Plasmoid.icon: jarIcon
    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground
    preferredRepresentation: fullRepresentation
    toolTipMainText: "Fish food"
    toolTipSubText: TankHub.available ? "Pick it up and shake it over the water to feed the fish" : "Desktocean is not your wallpaper"

    // The widget's own keyboard shortcut (its settings) shakes the jar too
    Connections {
        target: root.Plasmoid
        function onActivated() {
            root.shake();
        }
    }

    signal shake()

    // The desktop's box around this widget (an ItemContainer of the desktop's
    // AppletsLayout), which we move while the jar is carried; null in a panel
    function desktopBox() {
        for (let p = root.parent; p; p = p.parent) {
            if (p.layout !== undefined && p.dragActive !== undefined && p.layout && p.layout.positionItem !== undefined)
                return p;
        }
        return null;
    }

    fullRepresentation: Item {
        id: holder
        Layout.minimumWidth: Kirigami.Units.gridUnit * 2
        Layout.minimumHeight: Kirigami.Units.gridUnit * 2
        Layout.preferredWidth: Kirigami.Units.gridUnit * 4
        Layout.preferredHeight: Kirigami.Units.gridUnit * 5

        Connections {
            target: root
            function onShake() {
                holder.shake();
            }
        }

        function shake() {
            if (shaking.running || carrying)
                return;
            shaking.start();
        }

        // Picked up: the jar follows the cursor. Shaking it back and forth in one
        // spot tips it over, and after a moment of shaking food starts to fall
        property bool carrying: false
        property real shakeEnergy: 0          // px of back-and-forth, fading away
        readonly property bool pouring: carrying && shakeEnergy > 260
        property real lean: 0                 // tilt from the last movement
        function sprinkle() {
            TankHub.feedBelow(jarBox);
            for (let i = 0; i < flakes.count; ++i)
                flakes.itemAt(i).fall();
        }
        onCarryingChanged: {
            shakeEnergy = 0;
            lean = 0;
            if (carrying)
                shaking.stop();
        }
        Timer {
            // Shaking fades when you stop
            interval: 50
            repeat: true
            running: holder.carrying
            onTriggered: {
                holder.shakeEnergy = Math.max(0, holder.shakeEnergy - 30);
                holder.lean *= 0.7;
            }
        }
        Timer {
            // A moment of shaking before the first food, then a little more every so often
            interval: 600
            repeat: true
            running: holder.pouring
            onTriggered: holder.sprinkle()
        }
        Binding {
            target: jar
            property: "rotation"
            when: holder.carrying
            value: holder.pouring ? 150 + holder.lean : holder.lean
            restoreMode: Binding.RestoreNone
        }
        NumberAnimation {
            id: standUp
            target: jar
            property: "rotation"
            to: 0
            duration: 260
            easing.type: Easing.OutBack
        }

        // Keeps the drawing's proportions inside whatever size the widget has
        Item {
            id: jarBox
            readonly property real aspect: 64 / 84
            height: Math.min(parent.height, parent.width / aspect)
            width: height * aspect
            anchors.centerIn: parent

            Image {
                id: jar
                anchors.fill: parent
                source: root.jarIcon
                sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                smooth: true
                transformOrigin: Item.Center
                scale: mouse.pressed ? 0.95 : mouse.containsMouse ? 1.04 : 1
                Behavior on scale { NumberAnimation { duration: 120 } }
            }

            // Flakes spilling from the lid while the jar is tipped
            Repeater {
                id: flakes
                model: 6
                Rectangle {
                    id: flake
                    required property int index
                    readonly property real spread: (index - 2.5) / 2.5
                    width: Math.max(3, jarBox.width * 0.07)
                    height: width
                    radius: width / 2
                    color: index % 2 ? "#f0b060" : "#c8743a"
                    border.color: "#7a3f19"
                    border.width: 1
                    opacity: 0
                    x: jarBox.width / 2 + spread * jarBox.width * 0.25 - width / 2
                    y: jarBox.height
                    function fall() {
                        drop.restart();
                    }
                    SequentialAnimation {
                        id: drop
                        PauseAnimation { duration: 140 + flake.index * 45 }
                        PropertyAction { target: flake; property: "opacity"; value: 1 }
                        ParallelAnimation {
                            NumberAnimation { target: flake; property: "y"; from: jarBox.height * 0.75; to: jarBox.height * 1.25; duration: 420; easing.type: Easing.InQuad }
                            NumberAnimation { target: flake; property: "opacity"; from: 1; to: 0; duration: 420; easing.type: Easing.InQuad }
                        }
                    }
                }
            }
        }

        // Tip, shake twice, stand up again; the food goes in at the first shake
        SequentialAnimation {
            id: shaking
            NumberAnimation { target: jar; property: "rotation"; to: 150; duration: 180; easing.type: Easing.OutQuad }
            ScriptAction {
                script: {
                    TankHub.feedBelow(jarBox);
                    for (let i = 0; i < flakes.count; ++i)
                        flakes.itemAt(i).fall();
                }
            }
            NumberAnimation { target: jar; property: "rotation"; to: 175; duration: 90 }
            NumberAnimation { target: jar; property: "rotation"; to: 140; duration: 90 }
            NumberAnimation { target: jar; property: "rotation"; to: 175; duration: 90 }
            NumberAnimation { target: jar; property: "rotation"; to: 150; duration: 90 }
            NumberAnimation { target: jar; property: "rotation"; to: 0; duration: 260; easing.type: Easing.OutBack }
        }

        MouseArea {
            id: mouse
            anchors.fill: jarBox
            hoverEnabled: true
            cursorShape: holder.carrying ? Qt.ClosedHandCursor : Qt.OpenHandCursor

            property point grab
            property var box: null
            // Where shaking is measured from; moving far away starts afresh
            property point anchor
            property point last

            function putDown() {
                if (!holder.carrying)
                    return;
                if (box) {
                    box.layout.positionItem(box);
                    box.layout.save();
                }
                holder.carrying = false;
                standUp.start();
            }

            onPressed: mouseEvent => {
                // Picked up straight away
                box = root.desktopBox();
                standUp.stop();
                holder.carrying = true;
                const at = box ? mapToItem(box.parent, mouseEvent.x, mouseEvent.y) : mapToGlobal(mouseEvent.x, mouseEvent.y);
                anchor = last = at;
                if (box) {
                    grab = mapToItem(box, mouseEvent.x, mouseEvent.y);
                    box.layout.releaseSpace(box);
                }
            }
            onPositionChanged: mouseEvent => {
                if (!pressed || !holder.carrying)
                    return;
                const at = box ? mapToItem(box.parent, mouseEvent.x, mouseEvent.y) : mapToGlobal(mouseEvent.x, mouseEvent.y);
                const dx = at.x - last.x, dy = at.y - last.y;
                last = at;
                if (box) {
                    box.x = at.x - grab.x;
                    box.y = at.y - grab.y;
                }
                // Back and forth near one spot is shaking; carrying it across
                // the screen is not
                if (Math.hypot(at.x - anchor.x, at.y - anchor.y) > 140) {
                    anchor = at;
                    holder.shakeEnergy *= 0.3;
                } else {
                    holder.shakeEnergy = Math.min(900, holder.shakeEnergy + Math.abs(dx) + Math.abs(dy));
                }
                holder.lean = Math.max(-25, Math.min(25, holder.lean + dx * 0.6));
            }
            onReleased: putDown()
            onCanceled: putDown()
        }
    }
}
