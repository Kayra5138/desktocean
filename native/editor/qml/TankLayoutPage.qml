import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import Desktocean.Editor

// Arrange plants and decorations on a miniature of the desktop tank.
Kirigami.Page {
    id: page

    signal drawNew(string kind)
    signal editScenery(var entry)

    title: "Tank layout"
    padding: 0

    // Row of the selected placement, or -1
    property int selected: -1
    readonly property var selectedItem: selected >= 0 && selected < layout.count ? layoutRepeater.itemAt(selected) : null

    LayoutModel {
        id: layout
    }

    LibraryModel {
        id: scenery
        kind: "scenery"
        // A newly drawn plant shows up here; refresh the placements too
        onCountChanged: layout.reload()
    }

    actions: [
        Kirigami.Action {
            text: "Draw a plant"
            icon.name: "draw-brush"
            onTriggered: page.drawNew("plant")
        },
        Kirigami.Action {
            text: "Draw a decoration"
            icon.name: "draw-freehand"
            onTriggered: page.drawNew("decor")
        }
    ]

    Shortcut {
        sequences: [StandardKey.Delete]
        enabled: page.selected >= 0
        onActivated: {
            layout.remove(page.selected);
            page.selected = -1;
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // --- Miniature tank -----------------------------------------------------
        Item {
            id: area
            Layout.fillWidth: true
            Layout.fillHeight: true

            readonly property real screenAspect: Screen.width > 0 ? Screen.width / Screen.height : 16 / 9

            Rectangle {
                id: tank
                anchors.centerIn: parent
                width: Math.min(area.width - Kirigami.Units.gridUnit * 2, (area.height - Kirigami.Units.gridUnit * 2) * area.screenAspect)
                height: width / area.screenAspect
                clip: true
                radius: Kirigami.Units.cornerRadius
                gradient: Gradient {
                    GradientStop { position: 0; color: layout.themeColors.shallow }
                    GradientStop { position: 0.86; color: layout.themeColors.deep }
                }

                // Sand, matching SAND_TOP in the simulation
                Rectangle {
                    y: parent.height * 0.86
                    width: parent.width
                    height: parent.height - y
                    gradient: Gradient {
                        GradientStop { position: 0; color: layout.themeColors.sandLight }
                        GradientStop { position: 1; color: layout.themeColors.sandDark }
                    }
                }

                // Clicking empty water clears the selection
                TapHandler {
                    onTapped: page.selected = -1
                }

                QQC2.Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: parent.height * 0.4
                    width: parent.width * 0.7
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: "white"
                    opacity: 0.35
                    text: "Lower on the sand is nearer: fish swim in front of and behind things by depth.\nDrag to move · scroll to resize · Delete removes"
                }

                Repeater {
                    id: layoutRepeater
                    model: layout

                    delegate: Image {
                        id: deco

                        required property int index
                        required property string entryId
                        required property string name
                        required property url image
                        required property real aspect
                        required property real posX
                        required property real posY
                        required property real size
                        required property bool front
                        required property bool flipped

                        // Same sizing as the wallpaper: 30% of the tank height at size 1,
                        // but never wider than 35% of the tank
                        height: Math.min(tank.height * 0.3, tank.width * 0.35 * aspect) * size
                        width: height / aspect
                        x: posX * tank.width - width / 2
                        y: posY * tank.height - height
                        // Lower on the sand is nearer, as in the aquarium
                        z: posY * 100
                        source: image
                        mirror: flipped
                        smooth: true
                        mipmap: true

                        function rebind() {
                            x = Qt.binding(() => posX * tank.width - width / 2);
                            y = Qt.binding(() => posY * tank.height - height);
                        }

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -3
                            color: "transparent"
                            radius: 3
                            border.width: 2
                            border.color: Kirigami.Theme.highlightColor
                            visible: page.selected === deco.index
                        }

                        DragHandler {
                            id: drag
                            target: deco
                            onActiveChanged: {
                                if (active) {
                                    page.selected = deco.index;
                                    return;
                                }
                                layout.move(deco.index, (deco.x + deco.width / 2) / tank.width, (deco.y + deco.height) / tank.height);
                                deco.rebind();
                            }
                        }
                        TapHandler {
                            onTapped: page.selected = deco.index
                        }
                        WheelHandler {
                            onWheel: event => {
                                page.selected = deco.index;
                                layout.setScale(deco.index, deco.size * (event.angleDelta.y > 0 ? 1.08 : 1 / 1.08));
                            }
                        }
                    }
                }
            }
        }

        Kirigami.Separator { Layout.fillHeight: true }

        // --- Side panel ----------------------------------------------------------
        QQC2.ScrollView {
            id: sideScroll
            Layout.fillHeight: true
            Layout.preferredWidth: Kirigami.Units.gridUnit * 16
            QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

            ColumnLayout {
                width: sideScroll.availableWidth - Kirigami.Units.largeSpacing * 2
                x: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.smallSpacing

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.largeSpacing
                    type: Kirigami.MessageType.Error
                    text: layout.lastError || scenery.lastError
                    visible: text !== ""
                }

                Kirigami.Heading {
                    Layout.topMargin: Kirigami.Units.largeSpacing
                    level: 4
                    text: "Theme"
                }
                QQC2.ComboBox {
                    Layout.fillWidth: true
                    model: layout.themes
                    textRole: "label"
                    valueRole: "id"
                    currentIndex: Math.max(0, indexOfValue(layout.theme))
                    onActivated: layout.theme = currentValue
                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.largeSpacing
                }
                Kirigami.Heading {
                    level: 4
                    text: page.selectedItem ? page.selectedItem.name : "Nothing selected"
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    visible: !page.selectedItem
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    text: "Click a plant or decoration in the tank to change it."
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: page.selectedItem !== null
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        QQC2.Label { text: "Size:" }
                        QQC2.Slider {
                            Layout.fillWidth: true
                            from: 0.15
                            to: 3
                            value: page.selectedItem ? page.selectedItem.size : 1
                            onMoved: layout.setScale(page.selected, value)
                        }
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        opacity: 0.7
                        text: "To bring it forward, move it lower on the sand; higher up puts it further back."
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        QQC2.Button {
                            Layout.fillWidth: true
                            text: "Flip"
                            icon.name: "object-flip-horizontal"
                            onClicked: layout.flip(page.selected)
                        }
                        QQC2.Button {
                            Layout.fillWidth: true
                            text: "Copy"
                            icon.name: "edit-copy"
                            onClicked: page.selected = layout.duplicate(page.selected)
                        }
                    }
                    QQC2.Button {
                        Layout.fillWidth: true
                        text: "Remove from tank"
                        icon.name: "edit-delete-remove"
                        onClicked: {
                            layout.remove(page.selected);
                            page.selected = -1;
                        }
                    }
                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.largeSpacing
                }
                Kirigami.Heading { text: "Add to the tank"; level: 4 }

                Repeater {
                    model: scenery
                    delegate: QQC2.ItemDelegate {
                        id: entry

                        required property int index
                        required property string fishId
                        required property string name
                        required property url image
                        required property string kind
                        required property int placed

                        Layout.fillWidth: true
                        onClicked: page.selected = layout.add(fishId)

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing
                            Image {
                                Layout.preferredWidth: Kirigami.Units.gridUnit * 2.5
                                Layout.preferredHeight: Kirigami.Units.gridUnit * 2.5
                                source: entry.image
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                                mipmap: true
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    text: entry.name
                                    elide: Text.ElideRight
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    text: (entry.kind === "plant" ? "Plant" : "Decoration") + " · " + entry.placed + " in the tank"
                                    font: Kirigami.Theme.smallFont
                                    opacity: 0.7
                                }
                            }
                            QQC2.ToolButton {
                                icon.name: "document-edit"
                                onClicked: page.editScenery(scenery.get(entry.index))
                                QQC2.ToolTip.text: "Edit drawing"
                                QQC2.ToolTip.visible: hovered
                            }
                            QQC2.ToolButton {
                                icon.name: "edit-delete"
                                onClicked: {
                                    removePrompt.entryId = entry.fishId;
                                    removePrompt.entryName = entry.name;
                                    removePrompt.open();
                                }
                                QQC2.ToolTip.text: "Delete from the library"
                                QQC2.ToolTip.visible: hovered
                            }
                        }
                    }
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    text: "Click one to place another copy in the tank."
                }
                Item { Layout.preferredHeight: Kirigami.Units.largeSpacing }
            }
        }
    }

    Kirigami.PromptDialog {
        id: removePrompt
        property string entryId
        property string entryName
        title: "Delete “" + entryName + "”?"
        subtitle: "Every copy leaves the tank and the drawing is deleted."
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: "Delete"
                icon.name: "edit-delete"
                onTriggered: {
                    scenery.remove(removePrompt.entryId);
                    page.selected = -1;
                    layout.reload();
                    removePrompt.close();
                }
            }
        ]
    }
}
