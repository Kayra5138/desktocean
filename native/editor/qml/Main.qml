import QtQuick
import org.kde.kirigami as Kirigami
import Desktocean.Editor

Kirigami.ApplicationWindow {
    id: window

    title: "Draw a fish"
    width: Kirigami.Units.gridUnit * 72
    height: Kirigami.Units.gridUnit * 44
    minimumWidth: Kirigami.Units.gridUnit * 48
    minimumHeight: Kirigami.Units.gridUnit * 32

    pageStack.columnView.columnResizeMode: Kirigami.ColumnView.SingleColumn
    pageStack.initialPage: DrawPage {
        id: drawPage
        onOpenLibrary: window.pageStack.push(libraryPage)
        onOpenLayout: window.pageStack.push(layoutPage)
    }

    // Runs `action` now, or after the user agrees to drop an unsaved drawing
    function unlessUnsaved(action) {
        if (!drawPage.canvas.dirty) {
            action();
            return;
        }
        discardPrompt.pending = action;
        discardPrompt.open();
    }

    // Returns the page it opened
    function openPage(page) {
        while (pageStack.depth > 1)
            pageStack.pop();
        if (page === "fish")
            return pageStack.push(libraryPage);
        else if (page === "layout")
            return pageStack.push(layoutPage);
        return null;
    }

    // `desktocean-editor fish.desktocean` (or opening the file) imports the shared fish
    function importSharedFiles(args) {
        const files = args.filter(a => a.endsWith(".desktocean") && !a.startsWith("-"));
        if (files.length === 0)
            return false;
        const library = openPage("fish");
        for (const file of files)
            library.importFile(file);
        return true;
    }

    // Closing with an unsaved drawing asks first
    property bool closeConfirmed: false
    onClosing: close => {
        if (closeConfirmed || !drawPage.canvas.dirty)
            return;
        close.accepted = false;
        unlessUnsaved(() => {
            window.closeConfirmed = true;
            window.close();
        });
    }

    Kirigami.PromptDialog {
        id: discardPrompt
        property var pending: null
        title: "Discard your drawing?"
        subtitle: "It has not been added to the aquarium yet."
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: "Discard"
                icon.name: "edit-delete"
                onTriggered: {
                    discardPrompt.close();
                    const action = discardPrompt.pending;
                    discardPrompt.pending = null;
                    if (action)
                        action();
                }
            },
            Kirigami.Action {
                text: "Keep drawing"
                icon.name: "draw-brush"
                onTriggered: {
                    discardPrompt.pending = null;
                    discardPrompt.close();
                }
            }
        ]
    }

    // Another launch (e.g. "Draw a fish…" while the editor is open) brings this window forward
    Connections {
        target: AppInstance
        function onActivated(args) {
            window.show();
            window.raise();
            window.requestActivate();
            const i = args.indexOf("--page");
            if (window.importSharedFiles(args))
                return;
            if (i >= 0)
                window.openPage(args[i + 1]);
        }
    }

    Component {
        id: layoutPage
        TankLayoutPage {
            onDrawNew: kind => window.unlessUnsaved(() => {
                window.pageStack.pop();
                drawPage.startNew(kind);
            })
            onEditScenery: entry => window.unlessUnsaved(() => {
                window.pageStack.pop();
                drawPage.startEditing(entry);
            })
        }
    }

    // `desktocean-editor --page fish|layout` opens straight on that page
    Component.onCompleted: {
        const args = Qt.application.arguments.slice(1);
        const i = args.indexOf("--page");
        if (importSharedFiles(args))
            return;
        if (i >= 0)
            openPage(args[i + 1]);
    }

    Component {
        id: libraryPage
        LibraryPage {
            onDrawNew: window.pageStack.pop()
            onEditFish: fish => window.editFromList(fish)
            onOpenFamily: fishId => window.pageStack.push(familyPage, { focusId: fishId })
        }
    }

    Component {
        id: familyPage
        FamilyTreePage {
            onEditFish: fish => window.editFromList(fish)
        }
    }

    // Back to the drawing page with a library fish open
    function editFromList(fish) {
        unlessUnsaved(() => {
            while (pageStack.depth > 1)
                pageStack.pop();
            drawPage.startEditing(fish);
        });
    }

    // Development aid, e.g. desktocean-editor --template 0 --fill 500 270 --screenshot /tmp/editor.png
    // Optional: --swim <style id>, --wiggle-tail, --add-as <name> (saves the drawing to the library),
    // --kind <fish|plant|decor>, --symmetry <h|v>, --stroke x0,y0,x1,y1,... (a red brush stroke),
    // --select-all, --edit-first (opens the first library fish for
    // editing), --names-first (opens its names dialog), --share-first <path> (exports it),
    // --family [row] (the family tree, centred on that library row),
    // --library, --layout. A .desktocean file among the arguments is imported.
    Timer {
        readonly property var args: Qt.application.arguments
        function arg(name, offset) {
            const i = args.indexOf(name);
            return i >= 0 ? args[i + (offset || 1)] : undefined;
        }
        running: args.indexOf("--screenshot") >= 0
        interval: 800
        onTriggered: {
            if (arg("--template") !== undefined)
                drawPage.canvas.loadTemplate(Number(arg("--template")));
            if (arg("--symmetry") !== undefined)
                drawPage.canvas.symmetry = arg("--symmetry") === "v" ? DrawingCanvas.LeftRight : DrawingCanvas.TopBottom;
            if (arg("--stroke") !== undefined) {
                drawPage.canvas.color = "#e04848";
                drawPage.canvas.strokeForTest(arg("--stroke").split(",").map(Number));
            }
            if (arg("--fill") !== undefined) {
                drawPage.canvas.color = "#3d8bd9";
                drawPage.canvas.fillAt(Number(arg("--fill")), Number(arg("--fill", 2)));
            }
            if (arg("--swim") !== undefined)
                drawPage.styleForTest(arg("--swim"));
            if (args.indexOf("--mouth-tool") >= 0)
                drawPage.canvas.tool = DrawingCanvas.Mouth;
            if (arg("--mouth") !== undefined)
                drawPage.canvas.markMouth(Number(arg("--mouth")), Number(arg("--mouth", 2)));
            if (args.indexOf("--wiggle-tail") >= 0)
                drawPage.canvas.paintTailWiggleForTest();
            if (arg("--add-as") !== undefined) {
                if (arg("--kind") !== undefined)
                    drawPage.kind = arg("--kind");
                const error = drawPage.canvas.saveToLibrary(drawPage.meta(arg("--add-as")));
                console.log(error ? "add failed: " + error : "added " + arg("--add-as"));
            }
            if (args.indexOf("--select-all") >= 0)
                drawPage.canvas.selectAll();
            if (args.indexOf("--edit-first") >= 0)
                window.pageStack.push(libraryPage).editFirstForTest();
            if (args.indexOf("--names-first") >= 0)
                window.pageStack.push(libraryPage).namesFirstForTest();
            if (arg("--share-first") !== undefined)
                window.pageStack.push(libraryPage).shareFirstForTest(arg("--share-first"));
            if (args.indexOf("--family") >= 0) {
                const row = arg("--family");
                window.pageStack.push(libraryPage).familyForTest(/^[0-9]+$/.test(row) ? Number(row) : undefined);
            }
            if (args.indexOf("--library") >= 0)
                window.pageStack.push(libraryPage);
            if (args.indexOf("--layout") >= 0)
                window.pageStack.push(layoutPage);
            shot.start();
        }
    }
    Timer {
        id: shot
        interval: 800
        onTriggered: window.pageStack.grabToImage(result => {
            result.saveToFile(Qt.application.arguments[Qt.application.arguments.indexOf("--screenshot") + 1]);
            Qt.quit();
        })
    }
}
