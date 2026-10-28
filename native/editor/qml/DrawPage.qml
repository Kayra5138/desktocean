import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Desktocean.Editor

Kirigami.Page {
    id: page

    signal openLibrary()
    property alias canvas: canvas

    title: "Draw a fish"
    padding: 0

    readonly property var swatches: [
        "#f28c28", "#f7b267", "#e9d23a", "#9fd356",
        "#3e7d2a", "#3d8bd9", "#7fc3f0", "#1f4f8a",
        "#d36bb0", "#8a3f9e", "#e04848", "#ffffff",
        "#c0c0c8", "#6b6b78", "#2a2a35", "#000000"
    ]

    function showMessage(type, text) {
        message.type = type;
        message.text = text;
        message.visible = true;
    }

    function importFile(url, removeBackground) {
        const error = canvas.importImage(url, removeBackground);
        if (error)
            showMessage(Kirigami.MessageType.Error, error);
        else
            showMessage(Kirigami.MessageType.Information, "Make sure the fish faces right. Use Flip if it doesn't.");
    }

    function addToAquarium() {
        const name = nameField.text.trim() || "Fish";
        const error = canvas.saveToLibrary(name, sizeBox.currentValue, copiesBox.value);
        if (error) {
            showMessage(Kirigami.MessageType.Error, error);
            return;
        }
        showMessage(Kirigami.MessageType.Positive, "“" + name + "” is swimming in your aquarium now.");
        canvas.clear();
        nameField.clear();
    }

    actions: [
        Kirigami.Action {
            text: "Templates"
            icon.name: "view-preview"
            Kirigami.Action { text: "Classic fish"; onTriggered: canvas.loadTemplate(0) }
            Kirigami.Action { text: "Long fish"; onTriggered: canvas.loadTemplate(1) }
            Kirigami.Action { text: "Round fish"; onTriggered: canvas.loadTemplate(2) }
        },
        Kirigami.Action {
            text: "Import image…"
            icon.name: "document-import"
            onTriggered: importDialog.open()
        },
        Kirigami.Action {
            text: "Flip"
            icon.name: "object-flip-horizontal"
            enabled: !canvas.empty
            onTriggered: canvas.flipHorizontal()
        },
        Kirigami.Action {
            text: "Undo"
            icon.name: "edit-undo"
            enabled: canvas.canUndo
            shortcut: StandardKey.Undo
            displayHint: Kirigami.DisplayHint.IconOnly
            onTriggered: canvas.undo()
        },
        Kirigami.Action {
            text: "Redo"
            icon.name: "edit-redo"
            enabled: canvas.canRedo
            shortcut: StandardKey.Redo
            displayHint: Kirigami.DisplayHint.IconOnly
            onTriggered: canvas.redo()
        },
        Kirigami.Action {
            text: "Clear"
            icon.name: "edit-clear"
            enabled: !canvas.empty
            onTriggered: canvas.clear()
        },
        Kirigami.Action {
            text: "My aquarium"
            icon.name: "view-list-icons"
            onTriggered: page.openLibrary()
        }
    ]

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // --- Tools -------------------------------------------------------------
        QQC2.ScrollView {
            Layout.fillHeight: true
            Layout.preferredWidth: Kirigami.Units.gridUnit * 15
            QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

            ColumnLayout {
                width: Kirigami.Units.gridUnit * 15 - Kirigami.Units.largeSpacing * 2
                x: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Heading { text: "Tool"; level: 4; Layout.topMargin: Kirigami.Units.largeSpacing }
                RowLayout {
                    Layout.fillWidth: true
                    Repeater {
                        model: [
                            { tool: DrawingCanvas.Brush, text: "Brush", icon: "draw-brush" },
                            { tool: DrawingCanvas.Eraser, text: "Eraser", icon: "draw-eraser" },
                            { tool: DrawingCanvas.Fill, text: "Fill", icon: "color-fill" }
                        ]
                        QQC2.ToolButton {
                            required property var modelData
                            Layout.fillWidth: true
                            text: modelData.text
                            icon.name: modelData.icon
                            display: QQC2.AbstractButton.TextUnderIcon
                            checkable: true
                            checked: canvas.tool === modelData.tool
                            onClicked: canvas.tool = modelData.tool
                        }
                    }
                }

                Kirigami.Heading { text: "Brush size"; level: 4; Layout.topMargin: Kirigami.Units.largeSpacing }
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Slider {
                        Layout.fillWidth: true
                        from: 2
                        to: 80
                        value: canvas.brushSize
                        enabled: canvas.tool !== DrawingCanvas.Fill
                        onMoved: canvas.brushSize = Math.round(value)
                    }
                    QQC2.Label { text: canvas.brushSize; Layout.minimumWidth: Kirigami.Units.gridUnit * 1.5 }
                }

                Kirigami.Heading { text: "Colour"; level: 4; Layout.topMargin: Kirigami.Units.largeSpacing }
                GridLayout {
                    columns: 4
                    columnSpacing: Kirigami.Units.smallSpacing
                    rowSpacing: Kirigami.Units.smallSpacing
                    Repeater {
                        model: page.swatches
                        Rectangle {
                            required property string modelData
                            readonly property bool selected: Qt.colorEqual(canvas.color, modelData)
                            implicitWidth: Kirigami.Units.gridUnit * 2.4
                            implicitHeight: implicitWidth
                            radius: Kirigami.Units.cornerRadius
                            color: modelData
                            border.width: selected ? 3 : 1
                            border.color: selected ? Kirigami.Theme.highlightColor : Qt.rgba(0, 0, 0, 0.25)
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: canvas.color = parent.modelData
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Rectangle {
                        implicitWidth: Kirigami.Units.gridUnit * 2.4
                        implicitHeight: Kirigami.Units.gridUnit * 1.6
                        radius: Kirigami.Units.cornerRadius
                        color: canvas.color
                        border.width: 1
                        border.color: Qt.rgba(0, 0, 0, 0.25)
                    }
                    QQC2.Button {
                        Layout.fillWidth: true
                        text: "Custom…"
                        icon.name: "color-picker"
                        onClicked: colorDialog.open()
                    }
                }

                Item { Layout.fillHeight: true; Layout.minimumHeight: Kirigami.Units.largeSpacing }
                Kirigami.Separator { Layout.fillWidth: true }

                Kirigami.Heading { text: "Your fish"; level: 4; Layout.topMargin: Kirigami.Units.smallSpacing }
                QQC2.TextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: "Name (optional)"
                    onAccepted: if (!canvas.empty) page.addToAquarium()
                }
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label { text: "Size:" }
                    QQC2.ComboBox {
                        id: sizeBox
                        Layout.fillWidth: true
                        textRole: "text"
                        valueRole: "value"
                        currentIndex: 1
                        model: [
                            { text: "Small", value: 0.7 },
                            { text: "Medium", value: 1.0 },
                            { text: "Large", value: 1.4 }
                        ]
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label { text: "Copies:" }
                    QQC2.SpinBox {
                        id: copiesBox
                        Layout.fillWidth: true
                        from: 1
                        to: 10
                        value: 1
                    }
                }
                QQC2.Button {
                    Layout.fillWidth: true
                    Layout.bottomMargin: Kirigami.Units.largeSpacing
                    text: "Add to aquarium"
                    icon.name: "list-add"
                    highlighted: true
                    enabled: !canvas.empty
                    onClicked: page.addToAquarium()
                }
            }
        }

        Kirigami.Separator { Layout.fillHeight: true }

        // --- Canvas ------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Kirigami.InlineMessage {
                id: message
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.smallSpacing
                showCloseButton: true
                visible: false
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                DrawingCanvas {
                    id: canvas
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.gridUnit
                }

                // Which way is the head
                QQC2.Label {
                    x: canvas.x + canvas.viewRect.right - width - Kirigami.Units.largeSpacing
                    y: canvas.y + canvas.viewRect.y + Kirigami.Units.largeSpacing
                    text: "head this way  →"
                    color: "#555566"
                    opacity: 0.8
                    font.italic: true
                }

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width * 0.6
                    visible: canvas.empty
                    icon.name: "draw-brush"
                    text: "Draw a fish facing right"
                    explanation: "Or start from a template, or import a picture. Everything transparent becomes water."
                    // Let strokes through to the canvas
                    enabled: false
                }
            }
        }
    }

    FileDialog {
        id: importDialog
        title: "Import a picture of a fish"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.svg *.bmp *.gif)", "All files (*)"]
        onAccepted: {
            if (canvas.hasOpaqueBorder(selectedFile)) {
                backgroundPrompt.file = selectedFile;
                backgroundPrompt.open();
            } else {
                page.importFile(selectedFile, false);
            }
        }
    }

    Kirigami.PromptDialog {
        id: backgroundPrompt
        property url file
        title: "Remove the background?"
        subtitle: "This picture has a solid background. Desktocean can make it transparent so only the fish swims."
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: "Remove background"
                icon.name: "edit-select-all-layers"
                onTriggered: {
                    backgroundPrompt.close();
                    page.importFile(backgroundPrompt.file, true);
                }
            },
            Kirigami.Action {
                text: "Keep it"
                onTriggered: {
                    backgroundPrompt.close();
                    page.importFile(backgroundPrompt.file, false);
                }
            }
        ]
    }

    ColorDialog {
        id: colorDialog
        selectedColor: canvas.color
        onAccepted: canvas.color = selectedColor
    }
}
