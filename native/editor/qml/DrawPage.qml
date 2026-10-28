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

    // Set while a fish from the library is open for editing
    property string editingId: ""
    property string editingName: ""
    readonly property bool editing: editingId !== ""

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
        const error = canvas.saveToLibrary(name, sizeBox.currentValue, Math.max(1, copiesBox.value), styleBox.currentValue);
        if (error) {
            showMessage(Kirigami.MessageType.Error, error);
            return;
        }
        showMessage(Kirigami.MessageType.Positive, "“" + name + "” is swimming in your aquarium now.");
        resetForm();
    }

    function saveChanges() {
        const name = nameField.text.trim() || editingName;
        const error = canvas.saveOver(editingId, name, sizeBox.currentValue, copiesBox.value, styleBox.currentValue);
        if (error) {
            showMessage(Kirigami.MessageType.Error, error);
            return;
        }
        showMessage(Kirigami.MessageType.Positive, "“" + name + "” has been updated in your aquarium.");
        resetForm();
    }

    // `fish` is a LibraryModel row: {fishId, name, image, wiggle, swimStyle, scale, copies}
    function startEditing(fish) {
        const error = canvas.loadImage(fish.image, fish.wiggle);
        if (error) {
            showMessage(Kirigami.MessageType.Error, error);
            return;
        }
        const scale = fish.scale;
        editingId = fish.fishId;
        editingName = fish.name;
        nameField.text = fish.name;
        styleBox.currentIndex = Math.max(0, styleBox.indexOfValue(fish.swimStyle));
        // Closest size preset
        let best = 0;
        for (let i = 0; i < sizeBox.model.length; ++i) {
            if (Math.abs(sizeBox.model[i].value - scale) < Math.abs(sizeBox.model[best].value - scale))
                best = i;
        }
        sizeBox.currentIndex = best;
        copiesBox.value = fish.copies;
        message.visible = false;
    }

    // Used by Main.qml's development flags
    function styleForTest(id) {
        styleBox.currentIndex = Math.max(0, styleBox.indexOfValue(id));
    }

    function resetForm() {
        editingId = "";
        editingName = "";
        canvas.clear();
        canvas.motionMode = false;
        nameField.clear();
        copiesBox.value = 1;
        styleBox.currentIndex = 0;
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
            text: canvas.hasSelection ? "Flip selection" : "Flip"
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
            id: toolScroll
            Layout.fillHeight: true
            Layout.preferredWidth: Kirigami.Units.gridUnit * 16
            QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

            ColumnLayout {
                // availableWidth leaves room for the vertical scroll bar
                width: toolScroll.availableWidth - Kirigami.Units.largeSpacing * 2
                x: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Heading { text: "Tool"; level: 4; Layout.topMargin: Kirigami.Units.largeSpacing }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 4
                    columnSpacing: 0
                    rowSpacing: 0
                    Repeater {
                        model: [
                            { tool: DrawingCanvas.Brush, text: "Brush", icon: "draw-brush" },
                            { tool: DrawingCanvas.Eraser, text: "Eraser", icon: "draw-eraser" },
                            { tool: DrawingCanvas.Fill, text: "Fill", icon: "color-fill" },
                            { tool: DrawingCanvas.Picker, text: "Pick", icon: "color-picker" },
                            { tool: DrawingCanvas.Line, text: "Line", icon: "draw-line" },
                            { tool: DrawingCanvas.Rectangle, text: "Box", icon: "draw-rectangle" },
                            { tool: DrawingCanvas.Ellipse, text: "Ellipse", icon: "draw-ellipse" },
                            { tool: DrawingCanvas.Select, text: "Select", icon: "select-rectangular" }
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
                            QQC2.ToolTip.text: modelData.text
                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                        }
                    }
                }
                QQC2.CheckBox {
                    visible: canvas.tool === DrawingCanvas.Rectangle || canvas.tool === DrawingCanvas.Ellipse
                    text: "Filled shape"
                    checked: canvas.shapeFilled
                    onToggled: canvas.shapeFilled = checked
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    visible: canvas.tool === DrawingCanvas.Select && !canvas.hasSelection
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    text: "Drag a box around part of the drawing to move it. Ctrl+A selects everything."
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: canvas.hasSelection
                    QQC2.Button {
                        Layout.fillWidth: true
                        icon.name: "object-flip-horizontal"
                        text: "Flip"
                        onClicked: canvas.flipHorizontal()
                    }
                    QQC2.Button {
                        Layout.fillWidth: true
                        icon.name: "edit-delete"
                        text: "Delete"
                        onClicked: canvas.deleteSelection()
                    }
                    QQC2.Button {
                        Layout.fillWidth: true
                        icon.name: "dialog-ok-apply"
                        text: "Done"
                        onClicked: canvas.commitSelection()
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
                        enabled: canvas.motionMode || [DrawingCanvas.Brush, DrawingCanvas.Eraser, DrawingCanvas.Line, DrawingCanvas.Rectangle, DrawingCanvas.Ellipse].includes(canvas.tool)
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

                Kirigami.Heading { text: "Motion"; level: 4; Layout.topMargin: Kirigami.Units.largeSpacing }
                QQC2.ComboBox {
                    id: styleBox
                    Layout.fillWidth: true
                    model: preview.styles
                    textRole: "label"
                    valueRole: "id"
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 6
                    radius: Kirigami.Units.cornerRadius
                    clip: true
                    gradient: Gradient {
                        GradientStop { position: 0; color: "#2185a8" }
                        GradientStop { position: 1; color: "#0a2a44" }
                    }
                    SwimPreview {
                        id: preview
                        anchors.fill: parent
                        canvas: canvas
                        swimStyle: styleBox.currentValue ?? "classic"
                    }
                    QQC2.Label {
                        anchors.centerIn: parent
                        visible: canvas.empty
                        text: "Your fish will swim here"
                        color: "white"
                        opacity: 0.7
                    }
                }
                QQC2.Button {
                    Layout.fillWidth: true
                    checkable: true
                    checked: canvas.motionMode
                    onToggled: canvas.motionMode = checked
                    icon.name: "transform-move"
                    text: canvas.motionMode ? "Done painting wiggle" : "Paint wiggle…"
                    enabled: !canvas.empty
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: canvas.motionMode
                    spacing: Kirigami.Units.smallSpacing
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Repeater {
                            model: [
                                { brush: DrawingCanvas.More, text: "More" },
                                { brush: DrawingCanvas.Less, text: "Stiffer" },
                                { brush: DrawingCanvas.Normal, text: "Normal" }
                            ]
                            QQC2.ToolButton {
                                required property var modelData
                                Layout.fillWidth: true
                                text: modelData.text
                                checkable: true
                                checked: canvas.wiggleBrush === modelData.brush
                                onClicked: canvas.wiggleBrush = modelData.brush
                            }
                        }
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        opacity: 0.7
                        text: "Paint over fins and tail: red parts wiggle more, blue parts stay stiff."
                    }
                    QQC2.Button {
                        Layout.fillWidth: true
                        text: "Reset wiggle"
                        icon.name: "edit-reset"
                        enabled: canvas.hasWiggle
                        onClicked: canvas.resetWiggle()
                    }
                }

                Item { Layout.fillHeight: true; Layout.minimumHeight: Kirigami.Units.largeSpacing }
                Kirigami.Separator { Layout.fillWidth: true }

                Kirigami.Heading {
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    level: 4
                    text: page.editing ? "Editing “" + page.editingName + "”" : "Your fish"
                    elide: Text.ElideRight
                }
                QQC2.TextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: "Name (optional)"
                    onAccepted: if (!canvas.empty) page.editing ? page.saveChanges() : page.addToAquarium()
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
                        // 0 keeps an existing fish in the library without it swimming
                        from: page.editing ? 0 : 1
                        to: 10
                        value: 1
                    }
                }
                QQC2.Button {
                    Layout.fillWidth: true
                    Layout.bottomMargin: page.editing ? 0 : Kirigami.Units.largeSpacing
                    text: page.editing ? "Save changes" : "Add to aquarium"
                    icon.name: page.editing ? "document-save" : "list-add"
                    highlighted: true
                    enabled: !canvas.empty
                    onClicked: page.editing ? page.saveChanges() : page.addToAquarium()
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.bottomMargin: Kirigami.Units.largeSpacing
                    visible: page.editing
                    QQC2.Button {
                        Layout.fillWidth: true
                        text: "Save as new"
                        icon.name: "document-save-as"
                        enabled: !canvas.empty
                        onClicked: page.addToAquarium()
                    }
                    QQC2.Button {
                        Layout.fillWidth: true
                        text: "Cancel"
                        icon.name: "dialog-cancel"
                        onClicked: page.resetForm()
                    }
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

    Shortcut {
        sequences: [StandardKey.Delete, "Backspace"]
        enabled: canvas.hasSelection
        onActivated: canvas.deleteSelection()
    }
    Shortcut {
        sequence: "Escape"
        enabled: canvas.hasSelection
        onActivated: canvas.commitSelection()
    }
    Shortcut {
        sequence: StandardKey.SelectAll
        enabled: !nameField.activeFocus
        onActivated: canvas.selectAll()
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
