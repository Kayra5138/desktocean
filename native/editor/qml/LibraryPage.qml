import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Desktocean.Editor

Kirigami.ScrollablePage {
    id: page

    signal drawNew()
    signal editFish(string fishId, string name, url image, real scale, int copies)

    title: "My aquarium · " + library.swimming + " swimming"

    LibraryModel {
        id: library
    }

    // Used by Main.qml's --edit-first development flag
    function editFirstForTest() {
        const fish = library.get(0);
        if (fish.fishId)
            page.editFish(fish.fishId, fish.name, fish.image, fish.scale, fish.copies);
    }

    actions: [
        Kirigami.Action {
            text: "Draw a new fish"
            icon.name: "draw-brush"
            onTriggered: page.drawNew()
        }
    ]

    header: Kirigami.InlineMessage {
        type: Kirigami.MessageType.Error
        text: library.lastError
        visible: library.lastError !== ""
        showCloseButton: true
    }

    GridView {
        id: grid
        model: library

        readonly property int columns: Math.max(1, Math.floor(width / (Kirigami.Units.gridUnit * 14)))
        cellWidth: Math.floor(width / columns)
        cellHeight: Kirigami.Units.gridUnit * 13

        delegate: Item {
            id: cell

            required property string fishId
            required property string name
            required property url image
            required property int copies
            required property real scale

            width: grid.cellWidth
            height: grid.cellHeight

        Kirigami.Card {
            id: card
            anchors.fill: parent
            anchors.margins: Kirigami.Units.smallSpacing

            readonly property string fishId: cell.fishId
            readonly property string name: cell.name
            readonly property url image: cell.image
            readonly property int copies: cell.copies

            actions: [
                Kirigami.Action {
                    text: "Edit"
                    icon.name: "document-edit"
                    onTriggered: page.editFish(cell.fishId, cell.name, cell.image, cell.scale, cell.copies)
                },
                Kirigami.Action {
                    text: "Flip"
                    icon.name: "object-flip-horizontal"
                    onTriggered: library.flip(cell.fishId)
                },
                Kirigami.Action {
                    text: "Remove"
                    icon.name: "edit-delete-remove"
                    onTriggered: {
                        removePrompt.fishId = card.fishId;
                        removePrompt.fishName = card.name;
                        removePrompt.open();
                    }
                }
            ]

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Image {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 6
                    source: card.image
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    smooth: true
                    mipmap: true
                    opacity: card.copies > 0 ? 1 : 0.4
                }
                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 4
                    text: card.name
                    elide: Text.ElideRight
                }
                RowLayout {
                    QQC2.Label { text: card.copies > 0 ? "Swimming:" : "Resting (not in the tank)" }
                    Item { Layout.fillWidth: true }
                    QQC2.SpinBox {
                        from: 0
                        to: 20
                        value: card.copies
                        onValueModified: library.setCopies(card.fishId, value)
                    }
                }
            }
        }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: grid.count === 0
            icon.name: "draw-brush"
            text: "Your aquarium is empty"
            explanation: "Draw a fish and it will swim on your desktop."
            helpfulAction: Kirigami.Action {
                text: "Draw a fish"
                icon.name: "draw-brush"
                onTriggered: page.drawNew()
            }
        }
    }

    Kirigami.PromptDialog {
        id: removePrompt
        property string fishId
        property string fishName
        title: "Remove “" + fishName + "”?"
        subtitle: "It will leave your aquarium and its drawing will be deleted."
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: "Remove"
                icon.name: "edit-delete"
                onTriggered: {
                    library.remove(removePrompt.fishId);
                    removePrompt.close();
                }
            }
        ]
    }
}
