import QtQuick
import org.kde.kirigami as Kirigami

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

    Component {
        id: layoutPage
        TankLayoutPage {
            onDrawNew: kind => {
                window.pageStack.pop();
                drawPage.startNew(kind);
            }
            onEditScenery: entry => {
                window.pageStack.pop();
                drawPage.startEditing(entry);
            }
        }
    }

    Component {
        id: libraryPage
        LibraryPage {
            onDrawNew: window.pageStack.pop()
            onEditFish: fish => {
                window.pageStack.pop();
                drawPage.startEditing(fish);
            }
        }
    }

    // Development aid, e.g. desktocean-editor --template 0 --fill 500 270 --screenshot /tmp/editor.png
    // Optional: --swim <style id>, --wiggle-tail, --add-as <name> (saves the drawing to the library),
    // --kind <fish|plant|decor>, --select-all, --edit-first (opens the first library fish for
    // editing), --library, --layout.
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
            if (arg("--fill") !== undefined) {
                drawPage.canvas.color = "#3d8bd9";
                drawPage.canvas.fillAt(Number(arg("--fill")), Number(arg("--fill", 2)));
            }
            if (arg("--swim") !== undefined)
                drawPage.styleForTest(arg("--swim"));
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
