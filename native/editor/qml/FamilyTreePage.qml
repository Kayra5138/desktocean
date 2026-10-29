import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import Desktocean.Editor

// Who is whose baby: drawn fish on top, each baby a row below its parents,
// with lines to them. Clicking a fish opens it for editing.
Kirigami.Page {
    id: page

    signal editFish(var fish)

    // Highlights this fish with its parents and babies, and scrolls to it
    property string focusId: ""

    title: "Family tree"
    padding: 0

    LibraryModel {
        id: library
        onCountChanged: page.relayout()
    }

    readonly property real nodeWidth: Kirigami.Units.gridUnit * 7
    readonly property real nodeHeight: Kirigami.Units.gridUnit * 6.5
    readonly property real gapX: Kirigami.Units.gridUnit
    readonly property real gapY: Kirigami.Units.gridUnit * 3
    readonly property real margin: Kirigami.Units.gridUnit

    // [{fishId, row, name, image, generation, parents, lostParents, x, y}]
    property var nodes: []
    property var byId: ({})
    // Fish related to focusId (itself, parents, babies); empty = no focus
    property var related: ({})
    property real treeWidth: 0
    property real treeHeight: 0
    readonly property bool hasFamilies: nodes.some(n => n.generation > 0)

    function relayout() {
        const all = library.familyTree();
        const rows = [];
        for (const n of all)
            (rows[n.generation] = rows[n.generation] || []).push(n);
        const ids = {};
        let widest = 0;
        for (let g = 0; g < rows.length; ++g) {
            const row = rows[g] || [];
            // Babies sit under their parents: order by where the parents are
            if (g > 0) {
                const key = n => {
                    const xs = n.parents.map(p => ids[p] ? ids[p].x : undefined).filter(x => x !== undefined);
                    return xs.length ? xs.reduce((a, b) => a + b, 0) / xs.length : Infinity;
                };
                row.forEach(n => n.key = key(n));
                // Brothers and sisters stay together
                row.sort((a, b) => a.key - b.key || a.parents.join().localeCompare(b.parents.join()) || a.row - b.row);
            }
            row.forEach((n, i) => {
                n.x = i * (nodeWidth + gapX);
                n.y = margin + g * (nodeHeight + gapY);
                ids[n.fishId] = n;
            });
            widest = Math.max(widest, row.length * (nodeWidth + gapX) - gapX);
            rows[g] = row;
        }
        // Centre every row
        const width = Math.max(widest + 2 * margin, page.width - Kirigami.Units.gridUnit);
        for (const row of rows) {
            const used = row.length * (nodeWidth + gapX) - gapX;
            row.forEach(n => n.x += (width - used) / 2);
        }
        treeWidth = width;
        treeHeight = margin * 2 + rows.length * (nodeHeight + gapY) - gapY;
        byId = ids;
        nodes = all;
        updateRelated();
        lines.requestPaint();
        Qt.callLater(scrollToFocus);
    }

    function updateRelated() {
        const set = {};
        const focus = byId[focusId];
        if (focus) {
            set[focusId] = true;
            focus.parents.forEach(p => set[p] = true);
            nodes.forEach(n => { if (n.parents.includes(focusId)) set[n.fishId] = true; });
        }
        related = set;
    }

    function scrollToFocus() {
        const focus = byId[focusId];
        if (!focus)
            return;
        const flick = flickable;
        flick.contentX = Math.max(0, Math.min(focus.x + nodeWidth / 2 - flick.width / 2, flick.contentWidth - flick.width));
        flick.contentY = Math.max(0, Math.min(focus.y + nodeHeight / 2 - flick.height / 2, flick.contentHeight - flick.height));
    }

    onFocusIdChanged: {
        updateRelated();
        lines.requestPaint();
        scrollToFocus();
    }
    onWidthChanged: relayout()
    Component.onCompleted: relayout()

    QQC2.ScrollView {
        anchors.fill: parent

        Flickable {
            id: flickable
            contentWidth: page.treeWidth
            contentHeight: page.treeHeight
            clip: true

            Item {
                id: tree
                width: page.treeWidth
                height: page.treeHeight

                // Lines from each baby up to its parents
                Canvas {
                    id: lines
                    anchors.fill: parent
                    onPaint: {
                        const ctx = getContext("2d");
                        ctx.reset();
                        ctx.lineWidth = 2;
                        const focused = Object.keys(page.related).length > 0;
                        for (const child of page.nodes) {
                            for (const id of child.parents) {
                                const parent = page.byId[id];
                                if (!parent)
                                    continue;
                                const strong = !focused || (page.related[child.fishId] && page.related[id]
                                    && (child.fishId === page.focusId || id === page.focusId));
                                ctx.strokeStyle = Qt.alpha(Kirigami.Theme.textColor, strong ? 0.55 : 0.12);
                                const x0 = parent.x + page.nodeWidth / 2, y0 = parent.y + page.nodeHeight;
                                const x1 = child.x + page.nodeWidth / 2, y1 = child.y;
                                const bend = page.gapY * 0.6;
                                ctx.beginPath();
                                ctx.moveTo(x0, y0);
                                ctx.bezierCurveTo(x0, y0 + bend, x1, y1 - bend, x1, y1);
                                ctx.stroke();
                            }
                        }
                    }
                }

                Repeater {
                    model: page.nodes
                    delegate: QQC2.ItemDelegate {
                        id: node
                        required property var modelData
                        readonly property bool isFocus: modelData.fishId === page.focusId
                        x: modelData.x
                        y: modelData.y
                        width: page.nodeWidth
                        height: page.nodeHeight
                        opacity: Object.keys(page.related).length === 0 || page.related[modelData.fishId] ? 1 : 0.35
                        highlighted: isFocus
                        onClicked: page.editFish(library.get(modelData.row))

                        QQC2.ToolTip.text: "Edit " + modelData.name
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                        background: Rectangle {
                            radius: Kirigami.Units.cornerRadius
                            color: node.isFocus ? Qt.alpha(Kirigami.Theme.highlightColor, 0.25)
                                 : node.hovered ? Qt.alpha(Kirigami.Theme.highlightColor, 0.12)
                                 : Kirigami.Theme.backgroundColor
                            border.width: node.isFocus ? 2 : 1
                            border.color: node.isFocus ? Kirigami.Theme.highlightColor : Qt.alpha(Kirigami.Theme.textColor, 0.2)
                        }

                        contentItem: Column {
                            spacing: Kirigami.Units.smallSpacing
                            Image {
                                width: parent.width
                                height: Kirigami.Units.gridUnit * 3.5
                                source: node.modelData.image
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                smooth: true
                                mipmap: true
                            }
                            QQC2.Label {
                                width: parent.width
                                text: node.modelData.name
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                font: Kirigami.Theme.smallFont
                            }
                            QQC2.Label {
                                width: parent.width
                                visible: node.modelData.lostParents > 0
                                text: node.modelData.parents.length === 0 ? "parents have left" : "a parent has left"
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                font: Kirigami.Theme.smallFont
                                opacity: 0.6
                            }
                        }
                    }
                }
            }
        }
    }

    Kirigami.PlaceholderMessage {
        anchors.horizontalCenter: parent.horizontalCenter
        y: page.treeHeight + Kirigami.Units.gridUnit * 2
        width: parent.width - Kirigami.Units.gridUnit * 4
        visible: page.nodes.length > 0 && !page.hasFamilies
        text: "No babies yet"
        explanation: "Fish that become friends in the tank sometimes have babies. They will appear here under their parents."
    }

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        visible: page.nodes.length === 0
        icon.name: "draw-brush"
        text: "Your aquarium is empty"
    }
}
