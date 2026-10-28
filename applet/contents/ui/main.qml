import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.extras as PlasmaExtras
import org.kde.plasma.plasmoid
import Desktocean.Core

// Panel or desktop widget for the Desktocean wallpaper: feed, pause, open the
// editor, and a glance at how the fish are doing.
PlasmoidItem {
    id: root

    readonly property url fishIcon: Qt.resolvedUrl("../icons/desktocean.svg")
    readonly property bool hungry: TankHub.available && TankHub.hunger > 0.7

    function summary() {
        if (!TankHub.available)
            return "Desktocean is not your wallpaper";
        const parts = [TankHub.fishCount + " fish"];
        parts.push(TankHub.hunger > 0.7 ? "hungry" : TankHub.hunger < 0.2 ? "full" : "peckish");
        if (TankHub.friendships > 0)
            parts.push(TankHub.friendships + (TankHub.friendships === 1 ? " friendship" : " friendships"));
        if (TankHub.paused)
            parts.push("paused");
        return parts.join(" · ");
    }

    Plasmoid.icon: fishIcon
    toolTipMainText: "Desktocean"
    toolTipSubText: summary()

    compactRepresentation: MouseArea {
        hoverEnabled: true
        onClicked: root.expanded = !root.expanded

        Kirigami.Icon {
            anchors.fill: parent
            source: root.fishIcon
            active: parent.containsMouse
            opacity: TankHub.paused ? 0.5 : 1
        }
        // A small food dot when the fish are hungry
        Rectangle {
            visible: root.hungry
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: Math.max(6, parent.width * 0.3)
            height: width
            radius: width / 2
            color: "#c8743a"
            border.color: "#7a3f19"
        }
    }

    fullRepresentation: PlasmaExtras.Representation {
        Layout.preferredWidth: Kirigami.Units.gridUnit * 18
        Layout.preferredHeight: content.implicitHeight + Kirigami.Units.largeSpacing * 2
        collapseMarginsHint: true

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true
                Kirigami.Icon {
                    source: root.fishIcon
                    Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    PlasmaExtras.Heading {
                        level: 4
                        text: "Desktocean"
                    }
                    PlasmaComponents.Label {
                        Layout.fillWidth: true
                        text: root.summary()
                        wrapMode: Text.WordWrap
                        opacity: 0.8
                    }
                }
            }

            // Hunger bar
            ColumnLayout {
                Layout.fillWidth: true
                visible: TankHub.available
                spacing: 0
                PlasmaComponents.Label {
                    text: "Hunger"
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                }
                PlasmaComponents.ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 1
                    value: TankHub.hunger
                }
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.smallSpacing
                columns: 2
                enabled: TankHub.available

                PlasmaComponents.Button {
                    Layout.fillWidth: true
                    text: "Feed"
                    icon.name: "food"
                    onClicked: TankHub.feed()
                }
                PlasmaComponents.Button {
                    Layout.fillWidth: true
                    text: TankHub.paused ? "Resume" : "Pause"
                    icon.name: TankHub.paused ? "media-playback-start" : "media-playback-pause"
                    onClicked: TankHub.paused = !TankHub.paused
                }
                PlasmaComponents.Button {
                    Layout.fillWidth: true
                    Layout.columnSpan: 2
                    visible: TankHub.foodCount > 0
                    text: "Clear food"
                    icon.name: "edit-clear-all"
                    onClicked: TankHub.clearFood()
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.smallSpacing
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 3
                PlasmaComponents.ToolButton {
                    Layout.fillWidth: true
                    text: "Draw"
                    icon.name: "draw-brush"
                    display: PlasmaComponents.AbstractButton.TextUnderIcon
                    onClicked: {
                        TankHub.openEditor();
                        root.expanded = false;
                    }
                }
                PlasmaComponents.ToolButton {
                    Layout.fillWidth: true
                    text: "My fish"
                    icon.name: "view-list-icons"
                    display: PlasmaComponents.AbstractButton.TextUnderIcon
                    onClicked: {
                        TankHub.openEditor("fish");
                        root.expanded = false;
                    }
                }
                PlasmaComponents.ToolButton {
                    Layout.fillWidth: true
                    text: "Layout"
                    icon.name: "view-grid"
                    display: PlasmaComponents.AbstractButton.TextUnderIcon
                    onClicked: {
                        TankHub.openEditor("layout");
                        root.expanded = false;
                    }
                }
            }

            PlasmaComponents.Label {
                Layout.fillWidth: true
                visible: !TankHub.available
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: "Pick Desktocean as your wallpaper: right-click the desktop → Desktop and Wallpaper."
            }
        }
    }
}
