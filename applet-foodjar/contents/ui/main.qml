import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid
import Desktocean.Core

// A jar of fish food standing on the desktop. Click it: it shakes, a few flakes
// spill out and food drops into the aquarium right below it. Move the jar to
// feed somewhere else.
PlasmoidItem {
    id: root

    readonly property url jarIcon: Qt.resolvedUrl("../images/foodjar.svg")

    Plasmoid.icon: jarIcon
    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground
    preferredRepresentation: fullRepresentation
    toolTipMainText: "Fish food"
    toolTipSubText: TankHub.available ? "Click to feed the fish below" : "Desktocean is not your wallpaper"

    // The widget's own keyboard shortcut (its settings) shakes the jar too
    Connections {
        target: root.Plasmoid
        function onActivated() {
            root.shake();
        }
    }

    signal shake()

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
            if (shaking.running)
                return;
            shaking.start();
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
            cursorShape: Qt.PointingHandCursor
            onClicked: holder.shake()
        }
    }
}
