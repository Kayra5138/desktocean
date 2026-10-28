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
    }

    Component {
        id: libraryPage
        LibraryPage {
            onDrawNew: window.pageStack.pop()
            onEditFish: (fishId, name, image, scale, copies) => {
                window.pageStack.pop();
                drawPage.startEditing(fishId, name, image, scale, copies);
            }
        }
    }

    // Development aid, e.g. desktocean-editor --template 0 --fill 500 270 --screenshot /tmp/editor.png
    // Optional: --add-as <name> saves the drawing to the library, --select-all, --edit-first
    // (opens the first library fish for editing), --library opens "My aquarium".
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
            if (arg("--add-as") !== undefined) {
                const error = drawPage.canvas.saveToLibrary(arg("--add-as"), 1.0, 1);
                console.log(error ? "add failed: " + error : "added " + arg("--add-as"));
            }
            if (args.indexOf("--select-all") >= 0)
                drawPage.canvas.selectAll();
            if (args.indexOf("--edit-first") >= 0)
                window.pageStack.push(libraryPage).editFirstForTest();
            if (args.indexOf("--library") >= 0)
                window.pageStack.push(libraryPage);
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
