import QtCore
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Desktocean.Editor

Kirigami.ScrollablePage {
    id: page

    signal drawNew()
    signal editFish(var fish)
    // Family tree, centred on a fish ("" for none)
    signal openFamily(string fishId)

    title: "My fish · " + library.swimming + " swimming"

    LibraryModel {
        id: library
    }

    // Used by Main.qml's --edit-first development flag
    function editFirstForTest() {
        const fish = library.get(0);
        if (fish.fishId)
            page.editFish(fish);
    }

    function showMessage(type, text) {
        message.type = type;
        message.text = text;
        message.visible = true;
    }

    // Adds the fish in a .desktocean file (URL or path) and says how it went
    function importFile(file) {
        const result = library.importFish(file);
        if (result.error)
            showMessage(Kirigami.MessageType.Error, result.error);
        else
            showMessage(Kirigami.MessageType.Positive, result.kind === "fish"
                ? "“" + result.name + "” has joined your aquarium."
                : "“" + result.name + "” is in the tank. Open Tank layout to move it.");
    }

    function exportTo(fishId, file) {
        const error = library.exportFish(fishId, file);
        if (error)
            showMessage(Kirigami.MessageType.Error, error);
        else
            showMessage(Kirigami.MessageType.Positive, "Saved. Send the file to a friend: opening it adds the fish to their aquarium.");
    }

    // Used by Main.qml's --share-first development flag
    function shareFirstForTest(path) {
        exportTo(library.get(0).fishId, "file://" + path);
        console.warn("share-first:", message.text);
    }

    // Used by Main.qml's --family development flag
    function familyForTest(row) {
        page.openFamily(row === undefined ? "" : library.get(row).fishId);
    }

    // Used by Main.qml's --names-first development flag
    function namesFirstForTest() {
        namesDialog.edit(library.get(0));
    }

    actions: [
        Kirigami.Action {
            text: "Draw a new fish"
            icon.name: "draw-brush"
            onTriggered: page.drawNew()
        },
        Kirigami.Action {
            text: "Family tree"
            icon.name: "view-list-tree"
            onTriggered: page.openFamily("")
        },
        Kirigami.Action {
            text: "Import fish…"
            icon.name: "document-import"
            onTriggered: importDialog.open()
        }
    ]

    header: Kirigami.InlineMessage {
        id: message
        visible: false
        showCloseButton: true
    }

    Connections {
        target: library
        function onLastErrorChanged() {
            if (library.lastError)
                page.showMessage(Kirigami.MessageType.Error, library.lastError);
        }
    }

    GridView {
        id: grid
        model: library

        readonly property int columns: Math.max(1, Math.floor(width / (Kirigami.Units.gridUnit * 14)))
        cellWidth: Math.floor(width / columns)
        cellHeight: Kirigami.Units.gridUnit * 16

        delegate: Item {
            id: cell

            required property string fishId
            required property string name
            required property url image
            required property int copies
            required property real scale
            required property int index
            required property string friends
            required property string rivals
            required property string family
            required property string characters
            required property string kind

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
                    onTriggered: page.editFish(library.get(cell.index))
                },
                Kirigami.Action {
                    text: "Names…"
                    icon.name: "edit-rename"
                    visible: cell.kind === "fish"
                    displayHint: Kirigami.DisplayHint.AlwaysHide
                    onTriggered: namesDialog.edit(library.get(cell.index))
                },
                Kirigami.Action {
                    text: "Family…"
                    icon.name: "view-list-tree"
                    visible: cell.kind === "fish"
                    displayHint: Kirigami.DisplayHint.AlwaysHide
                    onTriggered: page.openFamily(cell.fishId)
                },
                Kirigami.Action {
                    text: "Share…"
                    icon.name: "document-share"
                    displayHint: Kirigami.DisplayHint.AlwaysHide
                    onTriggered: {
                        shareDialog.fishId = cell.fishId;
                        shareDialog.currentFolder = StandardPaths.writableLocation(StandardPaths.DocumentsLocation);
                        shareDialog.selectedFile = shareDialog.currentFolder + "/" + library.exportName(cell.fishId);
                        shareDialog.open();
                    }
                },
                Kirigami.Action {
                    text: "Flip"
                    icon.name: "object-flip-horizontal"
                    // In the card's "more" menu
                    displayHint: Kirigami.DisplayHint.AlwaysHide
                    onTriggered: library.flip(cell.fishId)
                },
                Kirigami.Action {
                    text: "Remove"
                    icon.name: "edit-delete-remove"
                    // Deleting can't be undone: keep it in the "more" menu too
                    displayHint: Kirigami.DisplayHint.AlwaysHide
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
                QQC2.Label {
                    Layout.fillWidth: true
                    visible: cell.characters !== ""
                    text: "Character: " + cell.characters
                    elide: Text.ElideRight
                    font: Kirigami.Theme.smallFont
                    opacity: 0.8
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    visible: cell.family !== ""
                    text: "🐣 " + cell.family
                    elide: Text.ElideRight
                    font: Kirigami.Theme.smallFont
                    opacity: 0.8
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    visible: cell.friends !== ""
                    text: "♥ Friends with " + cell.friends
                    elide: Text.ElideRight
                    color: Kirigami.Theme.positiveTextColor
                    font: Kirigami.Theme.smallFont
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    visible: cell.rivals !== ""
                    text: "⚔ Rivals with " + cell.rivals
                    elide: Text.ElideRight
                    color: Kirigami.Theme.negativeTextColor
                    font: Kirigami.Theme.smallFont
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

    // The species' name and a name for every fish swimming; blank = automatic
    Kirigami.Dialog {
        id: namesDialog
        property string fishId
        property var defaults: []
        title: "Names"
        padding: Kirigami.Units.largeSpacing
        preferredWidth: Kirigami.Units.gridUnit * 22
        standardButtons: Kirigami.Dialog.Save | Kirigami.Dialog.Cancel

        function edit(fish) {
            fishId = fish.fishId;
            speciesField.text = fish.customName;
            speciesField.placeholderText = fish.customName ? "" : fish.name + " (automatic)";
            defaults = fish.defaultFishNames;
            copyNames.model = 0;
            copyNames.model = fish.copies;
            for (let k = 0; k < fish.copies; ++k)
                copyNames.itemAt(k).text = fish.fishNames[k] || "";
            open();
            speciesField.forceActiveFocus();
        }

        onAccepted: {
            const names = [];
            for (let k = 0; k < copyNames.count; ++k)
                names.push(copyNames.itemAt(k).text);
            library.rename(fishId, speciesField.text);
            library.setFishNames(fishId, names);
        }

        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            QQC2.Label { text: "Species" ; font.bold: true }
            QQC2.TextField {
                id: speciesField
                Layout.fillWidth: true
            }
            QQC2.Label {
                Layout.topMargin: Kirigami.Units.largeSpacing
                text: copyNames.count === 1 ? "The fish" : "Each fish"
                font.bold: true
                visible: copyNames.count > 0
            }
            QQC2.Label {
                Layout.fillWidth: true
                visible: copyNames.count > 1
                text: "Leave a name blank to number it after its species."
                wrapMode: Text.WordWrap
                font: Kirigami.Theme.smallFont
                opacity: 0.8
            }
            Repeater {
                id: copyNames
                model: 0
                QQC2.TextField {
                    required property int index
                    Layout.fillWidth: true
                    placeholderText: namesDialog.defaults[index] ?? ""
                }
            }
        }
    }

    FileDialog {
        id: shareDialog
        property string fishId
        title: "Share a fish"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "desktocean"
        nameFilters: ["Desktocean fish (*.desktocean)"]
        onAccepted: page.exportTo(fishId, selectedFile)
    }

    FileDialog {
        id: importDialog
        title: "Import a fish"
        nameFilters: ["Desktocean fish (*.desktocean)", "All files (*)"]
        onAccepted: page.importFile(selectedFile)
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
